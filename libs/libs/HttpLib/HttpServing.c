#include "net/net.h"
#include "HttpServing.h"
#include "autogen/HttpServing_c_ast.h"
#include "autogen/HttpLib_h_ast.h"
#include "netipfilter.h"
#include "HttpLib.h"
#include "tokenstore.h"
#include "file.h"
#include "sock.h"
#include "../../utilities/SentryServer/Sentry_comm.h"
#include "stashtable.h"
#include "cmdparse.h"
#include "WinUtil.h"
#include "StringUtil.h"
#include "utilitiesLib.h"
#include "objPath.h"
#include "httpXpathSupport.h"
#include "logging.h"
#include "HttpServingStats.h"
#include "SentryServerComm.h"
#include "StringCache.h"
#include "XMLRPC.h"
#include "CrypticPorts.h"
#include "textparserJSON.h"
#include "GlobalEnums.h"
#include "CmdParseJson.h"
#include "AutoGen/HttpXpathSupport_h_ast.h"

#define COMMAND_RETURN_OBSOLETE_TIME 600

#define HTTPSERVING_PER_FILE_BUFFER_SIZE (512 * 1024)

#define MAX_CACHED_JPEGS 2048

static U32 iTotalRequests = 0;

bool gbHttpServingAllowCommandsInURL = false;
AUTO_CMD_INT(gbHttpServingAllowCommandsInURL, AllowCommandsInURL);

bool gbHttpServingAllowCommandsInURL_LocalHostOnly = false;
AUTO_CMD_INT(gbHttpServingAllowCommandsInURL_LocalHostOnly, AllowCommandsInURL_LocalHostOnly);


bool gbNoJpegCaching = false;
AUTO_CMD_INT(gbNoJpegCaching, NoJpegCaching) ACMD_CMDLINE;

bool gbLogServerMonitor = false;
AUTO_CMD_INT(gbLogServerMonitor, LogServerMonitor) ACMD_CMDLINE;

static bool sbAllowJSONRPC = false;
AUTO_CMD_INT(sbAllowJSONRPC, AllowJSONRPC);

typedef struct HttpClientState
{
	HttpClientStateDefault baseState; // Must be at the front of the struct!
	NetLink *pParentLink;
	int iClientID;
} HttpClientState;

static U32 *spHttpDisabledPorts = NULL;

HttpClientState **ppClientStates = NULL;

HttpClientState *FindClientState(int iID)
{
	FOR_EACH_IN_EARRAY(ppClientStates, HttpClientState, pClientState)
	{
		if (pClientState->iClientID == iID)
		{
			return pClientState;
		}
	}
	FOR_EACH_END;

	return NULL;
}

AUTO_ENUM;
typedef enum
{
	REQUEST_VIEWXPATH,
	REQUEST_COMMAND,
	REQUEST_PROCESSCOMMAND,
	REQUEST_EDITFIELD,
	REQUEST_COMMITEDIT,
} enumPendingRequestType;

AUTO_STRUCT;
typedef struct PendingHttpRequest
{
	enumPendingRequestType eType;
	int iRequestID;
	NetLink *pLink; NO_AST
	UrlArgumentList *pUrlArgs;
	U64 iRequestTimeTicks; //so we can time out requests that seem not to be happening
	char *pCommandString; AST(ESTRING)//for REQUEST_COMMAND
	char *pFieldPath; AST(ESTRING) //for REQUEST_EDITFIELD
} PendingHttpRequest;

AUTO_STRUCT;
typedef struct PendingFileRequest
{
	int iRequestID;
	NetLink *pLink; NO_AST
	U64 iTotalFileSize; 
	U64 iBytesSent;
	U8 *pPendingBuffer; NO_AST
	U64 iReadHead;
	U64 iWriteHead;

	int iUpdatesReceived;
	bool bHeaderSent;
	bool bFirstRequestSent;
	char *pFileName;
	char *pErrorString;

} PendingFileRequest;

static PendingFileRequest **sppPendingFileRequests = NULL;

static int siNextFileRequestID = 1;

AUTO_STRUCT;
typedef struct CommandReturnValue
{
	int iCommandRequestID;
	int iClientID;
	char *pResultString; AST(ESTRING)
	U32 iObsoletionTime;
} CommandReturnValue;



//TODO this could be a stash table for faster access
//earray of command values that have been returned 
StashTable sCommandReturnValues = NULL;

StashTable sXMLRPCReturnLinks = NULL;

//earray of pending requests (TODO: turn into a stash table or something)
static PendingHttpRequest **sppRequests = NULL;
static int siNextRequestID = 1;

static char *spHTMLHeader = NULL;
static char *spHTMLFooter = NULL;
static char *spHTMLStaticHome = NULL;

static HttpServingXPathCallback *spXPathCB = NULL;
static HttpServingCommandCallback *spCommandCB = NULL;
static HttpServingCanServeCB *spCanServeCB = NULL;
static HttpServingJpegCallback *spJpegCB = NULL;
static FileServingCommandCallBack *spFileCB = NULL;

static int iCommandRequestID = 1;

static char *spDefaultRedirectString = NULL;

static NetListen *spHttpListen = NULL;

void IssueXpathRequest(int iRequestID, GlobalType eType, ContainerID iID, char *pXPath, UrlArgument **ppURLArgs, int iAccessLevel,
	const char *pUserNameAndIP);
static void CleanupFileRequest(int iIndex);

//---------------stuff relating to bitmaps
AUTO_STRUCT;
typedef struct PendingJpegRequest
{
	char *pJpegNameAndArgs; AST(ESTRING)
	U64 iRequestTimeClockTicks;
	int iRequestID;
	NetLink *pLink; NO_AST
	int iErrorAsText;
} PendingJpegRequest;

typedef struct CachedJpeg
{
	char *pData;
	int iDataSize;
	U32 iExpirationTime; // 0 = never
} CachedJpeg;



//hopefully will clean this up at some point in the future if it gets used a lot. This is how
//we divert servermonitor queries into the fancy template/pages that James W whips up
char *DEFAULT_LATELINK_httpServing_GetOverrideHTMLFileForServerMon(const char *pBaseURL, const char *pXPath, UrlArgumentList *pURLArgs)
{
	if (stricmp(pBaseURL, "/viewxpath") == 0
		&& (pXPath && strstri(pXPath, "].notes["))
		&& urlFindNonZeroInt(pURLArgs, "pretty") == 1
		&& urlFindNonZeroInt(pURLArgs, "json") == 0
		&& urlFindNonZeroInt(pURLArgs, "xml") == 0)
	{
		return "server/mcp/static_home/notes/note.html";
	}

	return NULL;
}


bool HttpServing_IsDisabled(U32 iPortNum)
{
	return ea32Find(&spHttpDisabledPorts, iPortNum) != -1;
}

void HttpServing_Disable(U32 iPortNum, bool bDisable)
{
	if (bDisable)
	{
		ea32PushUnique(&spHttpDisabledPorts, iPortNum);
	}
	else
	{
		int iIndex = ea32Find(&spHttpDisabledPorts, iPortNum);
		if (iIndex != -1)
		{
			ea32RemoveFast(&spHttpDisabledPorts, iIndex);
		}
	}
}


void HttpServingLog(const char *pAction, bool bFoundInCache, bool bWGS, U64 iTicks)
{
	static U64 sCPUSpeed = 0;
	
	
	if (gbLogServerMonitor)
	{
		if (!sCPUSpeed)
		{
			sCPUSpeed = timerCpuSpeed64();
		}

		objLog(LOG_SERVERMONITOR, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "HttpRequest", 
			NULL, "Type %s Time %f cache %d wgs %d", pAction, (float)((double)(iTicks) / (double)(sCPUSpeed)), bFoundInCache, bWGS);
	}
}


static PendingJpegRequest **sppPendingJpegRequests = NULL;
static StashTable cachedJpegsByNameAndArgs = NULL;
static int siNextJpegRequestID = 0;

static void HttpServingHereIsRequestedJpeg_Internal(NetLink *pLink, char *pData, int iDataSize, char *pJpegName, char *pErrorMessage, bool bErrorAsText)
{
	if (iDataSize)
	{
		httpSendBytes(pLink, pData, iDataSize);
	}
	else if(bErrorAsText)
	{
#if !PLATFORM_CONSOLE
		if (!(pErrorMessage && pErrorMessage[0]))
		{
			pErrorMessage = "Unknown Jpeg Error";
		}

		httpSendBytesWith404Error(pLink, pErrorMessage, (U32)strlen(pErrorMessage));
#endif
	}
	else
	{
#ifndef _XBOX
		char outFileName[CRYPTIC_MAX_PATH];
		char *pBuf;
		int iBufSize;

		if (!(pErrorMessage && pErrorMessage[0]))
		{
			pErrorMessage = "Unknown Jpeg Error";
		}

		sprintf(outFileName, "%s/jpgerror.jpg", fileTempDir());

		stringToJpeg(pErrorMessage, outFileName);

		pBuf = fileAlloc(outFileName, &iBufSize);

		if (pBuf)
		{
			httpSendBytesWith404Error(pLink, pBuf, iBufSize);
			free(pBuf);
		}
#endif
	}
}

void HttpServing_JpegReturn(int iRequestID, char *pData, int iDataSize, int iLifeSpan, char *pErrorMessage)
{
	int i;

	if (!cachedJpegsByNameAndArgs)
	{
		cachedJpegsByNameAndArgs = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);
	}

	//first, find the request
	for (i=0; i < eaSize(&sppPendingJpegRequests); i++)
	{
		if (sppPendingJpegRequests[i]->iRequestID == iRequestID)
		{
			CachedJpeg *pCachedJpeg;
			HttpServingHereIsRequestedJpeg_Internal(sppPendingJpegRequests[i]->pLink, pData, iDataSize, sppPendingJpegRequests[i]->pJpegNameAndArgs, pErrorMessage, sppPendingJpegRequests[i]->iErrorAsText);

			HttpServingLog("jpeg", false, true, timerCpuTicks64() - sppPendingJpegRequests[i]->iRequestTimeClockTicks);

			if (iDataSize && !gbNoJpegCaching && stashGetOccupiedSlots(cachedJpegsByNameAndArgs) < MAX_CACHED_JPEGS)
			{
				if (stashFindPointer(cachedJpegsByNameAndArgs, sppPendingJpegRequests[i]->pJpegNameAndArgs, &pCachedJpeg))
				{
					stashRemovePointer(cachedJpegsByNameAndArgs, sppPendingJpegRequests[i]->pJpegNameAndArgs, NULL);
					free(pCachedJpeg->pData);
					free(pCachedJpeg);
				}

				pCachedJpeg = malloc(sizeof(CachedJpeg));
				pCachedJpeg->iDataSize = iDataSize;
				pCachedJpeg->iExpirationTime = iLifeSpan ? timeSecondsSince2000() + iLifeSpan : iLifeSpan;
				pCachedJpeg->pData = pData;

				stashAddPointer(cachedJpegsByNameAndArgs, sppPendingJpegRequests[i]->pJpegNameAndArgs, pCachedJpeg, false);
			}
			else
			{
				free(pData);
			}

			StructDestroy(parse_PendingJpegRequest, sppPendingJpegRequests[i]);
			eaRemoveFast(&sppPendingJpegRequests, i);
			return;
		}
	}

	free(pData);
}



int SortUrlArgsByName(const UrlArgument **pInfo1, const UrlArgument **pInfo2)
{
	return stricmp((*pInfo1)->arg, (*pInfo2)->arg);
}

//given a requested jpeg name and arg list, generates a string containing both, with only the
//args beginning with "jpg", with the args in a deterministic order. This is so that
//two requests issued with url args in different orders will still hit the same cached jpeg
void GetJpegNameAndArgsString(char **ppOutEString, char *pJpegName, UrlArgumentList *pArgList)
{
	UrlArgument **ppJpgArgs = NULL;
	int i;


	for (i=0; i < eaSize(&pArgList->ppUrlArgList); i++)
	{
		if (strStartsWith(pArgList->ppUrlArgList[i]->arg, "jpg"))
		{
			eaPush(&ppJpgArgs, pArgList->ppUrlArgList[i]);
		}
	}

	eaQSort(ppJpgArgs, SortUrlArgsByName);

	estrCopy2(ppOutEString, pJpegName);

	for (i=0; i < eaSize(&ppJpgArgs); i++)
	{
		estrConcatf(ppOutEString, "&%s=%s", ppJpgArgs[i]->arg, ppJpgArgs[i]->value);
	}

	eaDestroy(&ppJpgArgs);
}




static void HttpServingRequestJpeg(char *pJpegName, UrlArgumentList *pArgList, NetLink *pLink)
{
	PendingJpegRequest *pRequest;
	char *pTempString = NULL;
	U64 iRequestTime = timerCpuTicks64();
	estrStackCreate(&pTempString);
	GetJpegNameAndArgsString(&pTempString, pJpegName, pArgList);

	if (!cachedJpegsByNameAndArgs)
	{
		cachedJpegsByNameAndArgs = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);
	}
	else
	{

		CachedJpeg *pCachedJpeg;


		if (stashFindPointer(cachedJpegsByNameAndArgs, pTempString, &pCachedJpeg))
		{
			if (pCachedJpeg->iExpirationTime && pCachedJpeg->iExpirationTime <= timeSecondsSince2000())
			{
				stashRemovePointer(cachedJpegsByNameAndArgs, pTempString, NULL);
				free(pCachedJpeg->pData);
				free(pCachedJpeg);
			}
			else
			{
				HttpServingHereIsRequestedJpeg_Internal(pLink, pCachedJpeg->pData, pCachedJpeg->iDataSize, pJpegName, NULL, false);
				estrDestroy(&pTempString);
				HttpServingLog("jpeg", true, true, timerCpuTicks64() - iRequestTime);
				return;
			}
		}
	}

	pRequest = StructCreate(parse_PendingJpegRequest);
	pRequest->iRequestID = siNextJpegRequestID++;
	pRequest->iRequestTimeClockTicks = iRequestTime;
	estrCopy2(&pRequest->pJpegNameAndArgs, pTempString);
	pRequest->pLink = pLink;

	if(urlFindBoundedInt(pArgList, "texterror", &pRequest->iErrorAsText, 0, 1) != 1)
	{
		pRequest->iErrorAsText = 0;
	}

	eaPush(&sppPendingJpegRequests, pRequest);

	spJpegCB(pJpegName, pArgList, pRequest->iRequestID);
	estrDestroy(&pTempString);
}

static void httpSendJsonRpcError(NetLink *link, char *pJsonString, FORMAT_STR const char *pFmt, ...)
{
	char *pFullErrorString = NULL;
	char *pOutString;
	Packet	*pak;
	size_t len;

	estrStackCreate(&pFullErrorString);
	estrGetVarArgs(&pFullErrorString, pFmt);

	pOutString = GetJsonRPCErrorString(pJsonString, "%s", pFullErrorString);

	pak = pktCreateRaw(link);
	len = strlen(pOutString);
	httpSendBasicHeader(link, len, "application/json");
	pktSendBytesRaw(pak,pOutString,(int)len);
	pktSendRaw(&pak);

	estrDestroy(&pFullErrorString);
}

static char *spJsonRpcRestrictedGroupName = NULL;
static char *spJsonRpcRestrictedCategoryNames = NULL;

AUTO_COMMAND;
void HttpServing_RestrictIPGroupToJsonRpcCategories(char *pGroupName, char *pCategoryNames)
{
	estrCopy2(&spJsonRpcRestrictedGroupName, pGroupName);
	estrCopy2(&spJsonRpcRestrictedCategoryNames, pCategoryNames);
}

//NULL = no restrictions. Otherwise, returns a comma-separated list of command category names, and the IP can only execute
//JSONRpC commands in those categories, and not do any other httpserving stuff at all
char *IPIsOnlyAllowedCertainJsonRpcCategories(U32 iIP)
{
	if (spJsonRpcRestrictedGroupName && ipfIsIpInGroup(spJsonRpcRestrictedGroupName, iIP))
	{
		return spJsonRpcRestrictedCategoryNames;
	}

	return NULL;
}


static void HttpHandleQuery(NetLink *link, HttpClientState *pClientState, char *pDataString, HttpAuthenticationResult result)
{
	static char *tempURL = NULL;
	char *pFoundURL;
	UrlArgumentList *pUrlArgs = NULL;

	int accesslevel = -1;
	U64 iRequestTime = timerCpuTicks64();
	bool bWGSRequest = false;
	const char *arg;

	if (pClientState->baseState.pPostHeaderString && *pClientState->baseState.pPostHeaderString)
	{
		char *pQuestionMark;
		estrCopy2(&tempURL, pClientState->baseState.pPostHeaderString);
		pFoundURL = urlFind(tempURL, "POST");
		if (!pFoundURL)
		{
			return;
		}

		//ABW some queries come in with the entire URL in the POST, not sure if this is the right way to handle it
		if (strStartsWith(pFoundURL, "/rpc") && (pQuestionMark  = strchr(pFoundURL, '?')))
		{
			*pQuestionMark = 0;
			pUrlArgs = urlToUrlArgumentList(pQuestionMark + 1);
		}
		else
		{
			pUrlArgs = urlToUrlArgumentList(pDataString);
		}

		estrCopy2(&pUrlArgs->pBaseURL, pFoundURL);
	}
	else
	{
		estrCopy2(&tempURL, pDataString);
		pFoundURL = urlFind(tempURL, "GET");
		if (!pFoundURL)
			return;

		pUrlArgs = urlToUrlArgumentList(pFoundURL);
	}

	if (HttpServing_IsDisabled(linkGetListenPort(link)))
	{
		httpSendServerError(link, "Serving disabled");
		return;
	}


	if (IPIsOnlyAllowedCertainJsonRpcCategories(linkGetIp(link)) && !strStartsWith(pUrlArgs->pBaseURL, "/rpc"))
	{	
		httpSendStr(link, "You are not allowed to do that");
		StructDestroy(parse_UrlArgumentList, pUrlArgs);
		return;
	}


#ifndef _XBOX
	if (strStartsWith(pUrlArgs->pBaseURL, "/xmlrpc"))
	{
		PendingXMLRPCRequest *req = NULL;
		char *path = pUrlArgs->pBaseURL + 1;
		iCommandRequestID++;

		//add the request to the pending stash
		if (!sXMLRPCReturnLinks) sXMLRPCReturnLinks = stashTableCreateInt(16);
		if (stashIntRemovePointer(sXMLRPCReturnLinks, iCommandRequestID, &req))
		{
			Errorf("HTTP Serving got duplicate command return values... how?");
			StructDestroy(parse_PendingXMLRPCRequest, req);
		}
		req = StructCreate(parse_PendingXMLRPCRequest);
		req->iRequestID = iCommandRequestID;
		req->iRequestTimeClockTicks = timerCpuTicks64();
		req->pLink = link;
		if (strStartsWith(path, "xmlrpcslow"))
		{
			req->requestTimeout = 600.0;
		}
		path = strchr(path, '/');

		((HttpClientStateDefault*)pClientState)->req = req;
		stashIntAddPointer(sXMLRPCReturnLinks, iCommandRequestID, req, true);

		if (result != HAR_FAILURE && path && path[1])
		{	
			GlobalType type = 0;
			ContainerID id = 0;
			char *estr = 0;
			accesslevel = httpFindAuthenticationAccessLevel(link, ((HttpClientStateDefault*)pClientState)->iAuthStringCRC, pUrlArgs);
			estrStackCreate(&estr);
			estrCopyWithURIUnescaping(&estr, path+1);

			if (DecodeContainerTypeAndIDFromString(estr,&type,&id))
			{
				estrDestroy(&estr);
				spCommandCB(type, id,((HttpClientState*)linkGetUserData(link))->iClientID, iCommandRequestID, 0, pDataString, accesslevel, httpFindAuthenticationUsernameAndIP(link, &((HttpClientState*)linkGetUserData(link))->baseState));
				if (pUrlArgs) StructDestroy(parse_UrlArgumentList, pUrlArgs);
				
				//logging happens in the httpserving_commandreturn
				return;
			}
			estrDestroy(&estr);
		}
		httpProcessXMLRPC(link, (HttpClientStateDefault*)pClientState, pDataString, result);
		HttpServingLog("xmlrpc", false, false, timerCpuTicks64() - pClientState->baseState.requestTime);
		if (pUrlArgs) StructDestroy(parse_UrlArgumentList, pUrlArgs);
		return;
	}
#endif //ndef _XBOX

	// The /xmlrpc code handles its own authentication failures. This catches the rest.
	accesslevel = httpFindAuthenticationAccessLevel(link, pClientState->baseState.iAuthStringCRC, pUrlArgs);
	if(accesslevel < 0)
	{
		httpSendAuthRequiredHeader(link);
		if (pUrlArgs) StructDestroy(parse_UrlArgumentList, pUrlArgs);
		return;
	}

	if (urlFindValue(pUrlArgs, "xml")) bWGSRequest = true;
	if (arg = urlFindValue(pUrlArgs, "format")) if (stricmp(arg, "html") != 0) bWGSRequest = true;



	if (strcmp(pUrlArgs->pBaseURL, "/") == 0)
	{
		httpRedirect(link, spDefaultRedirectString);
		HttpServingLog("redirect", false, false, timerCpuTicks64() - iRequestTime);
		StructDestroy(parse_UrlArgumentList, pUrlArgs);
		return;
	}

	if (strStartsWith(pUrlArgs->pBaseURL, "/static"))
	{
		char * estr = NULL;

		estrStackCreate(&estr);
		estrCopy2(&estr,pUrlArgs->pBaseURL);
		estrReplaceOccurrences(&estr, "/static", spHTMLStaticHome);

		if (fileIsAbsolutePathInternal(estr))
		{
			estrPrintf(&estr, "Could not find static file (absolute path not allowed): %s\n", pUrlArgs->pBaseURL);
			httpSendFileNotFoundError(link, estr);
		}
		else
		{
			if (fileExists(estr))
			{
				httpSendFile(link, estr, httpChooseSensibleContentTypeForFileName(estr));
			}
			else
			{
				estrPrintf(&estr, "Could not find static file: %s\n", pUrlArgs->pBaseURL);
				httpSendFileNotFoundError(link, estr);
			}
		}

		HttpServingLog("static", false, bWGSRequest, timerCpuTicks64() - iRequestTime);


		estrDestroy(&estr);
	}
	else if (strStartsWith(pUrlArgs->pBaseURL, "/rpc"))
	{
		const char *pXPath = urlFindValue(pUrlArgs, "xpath");
		GlobalType eContainerType;
		ContainerID iContainerID;
		char *pCategoryRestrictions = IPIsOnlyAllowedCertainJsonRpcCategories(linkGetIp(link));


		if (!pXPath || !pXPath[0])
		{
			eContainerType = GetAppGlobalType();
			iContainerID = GetAppGlobalID();
		}
		else
		{
			if (!DecodeContainerTypeAndIDFromString(pXPath, &eContainerType, &iContainerID))
			{
				httpSendJsonRpcError(link, pDataString, "Error while parsing XPATH %s... couldn't decode container type/ID", pXPath);
				StructDestroy(parse_UrlArgumentList, pUrlArgs);
				return;
			}
		}


		if (pCategoryRestrictions)
		{
			char *pFixedUpCommandString = NULL;
			estrStackCreate(&pFixedUpCommandString);

			if (!AddArgumentToJsonCommand(&pFixedUpCommandString, pDataString, "CategoryRestrictions", pCategoryRestrictions))
			{
				httpSendJsonRpcError(link, pDataString, "Unable to parse JSON");
				estrDestroy(&pFixedUpCommandString);
				StructDestroy(parse_UrlArgumentList, pUrlArgs);
				return;
			}

/*
			estrInsertf(&pFixedUpCommandString, pOpenBrace - pFixedUpCommandString + 1,
				"\n\t\"CategoryRestrictions\": \"%s\",",
				pCategoryRestrictions);*/

			spCommandCB(eContainerType, iContainerID, pClientState->iClientID,
				++iCommandRequestID, 
				CMDSRV_JSONRPC | CMDSRV_NON_CACHED_RETURN | ( sbAllowJSONRPC ? CMDSRV_ALWAYS_ALLOW_JSONRPC : 0),
				pFixedUpCommandString, 
				httpFindAuthenticationAccessLevel(link, ((HttpClientStateDefault*)pClientState)->iAuthStringCRC, pUrlArgs), 
				httpFindAuthenticationUsernameAndIP(link, &pClientState->baseState));

			estrDestroy(&pFixedUpCommandString);
		}
		else
		{
			spCommandCB(eContainerType, iContainerID, pClientState->iClientID,
					++iCommandRequestID, 
					CMDSRV_JSONRPC | CMDSRV_NON_CACHED_RETURN | ( sbAllowJSONRPC ? CMDSRV_ALWAYS_ALLOW_JSONRPC : 0),
					pDataString, 
					httpFindAuthenticationAccessLevel(link, ((HttpClientStateDefault*)pClientState)->iAuthStringCRC, pUrlArgs), 
					httpFindAuthenticationUsernameAndIP(link, &pClientState->baseState));
		}

		//James W and Alex W stuck this in without hugely understanding what's going on, because not doing so left us in a state
		//where the postHeaderString had stuff in it that was never cleared, causing all further /viewxpaths to fail
		estrZeroAndDestroy(&pClientState->baseState.pPostHeaderString);
		estrDestroy(&pClientState->baseState.pPostDataString);
	}
	else if (strStartsWith(pUrlArgs->pBaseURL, "/viewxpath") || strStartsWith(pUrlArgs->pBaseURL, "/command") 
		|| strStartsWith(pUrlArgs->pBaseURL, "/process") 
		|| strStartsWith(pUrlArgs->pBaseURL, "/edit") || strStartsWith(pUrlArgs->pBaseURL, "/commit"))
	{
		const char *pXPath = urlFindValue(pUrlArgs, "xpath");
		char *pFirstDot;
		char *pHTMLFileToDirectlyServer;

		GlobalType eContainerType;
		ContainerID iContainerID;
		PendingHttpRequest *pRequest;

		if (spCanServeCB)
		{
			char *pError = spCanServeCB();

			if (pError)
			{
				httpSendStr(link, pError);
				StructDestroy(parse_UrlArgumentList, pUrlArgs);
				return;
			}
		}


		
		pHTMLFileToDirectlyServer = httpServing_GetOverrideHTMLFileForServerMon(pUrlArgs->pBaseURL, 
			pXPath, pUrlArgs);

		if (!pHTMLFileToDirectlyServer)
		{
			pHTMLFileToDirectlyServer = DEFAULT_LATELINK_httpServing_GetOverrideHTMLFileForServerMon(pUrlArgs->pBaseURL, 
				pXPath, pUrlArgs);
		}


		if (pHTMLFileToDirectlyServer)
		{
			httpSendFile(link, pHTMLFileToDirectlyServer, NULL);
			StructDestroy(parse_UrlArgumentList, pUrlArgs);
			
			return;

		}


		if (!pXPath || !pXPath[0] || strcmp(pXPath, ".") == 0)
		{
			httpRedirect(link, spDefaultRedirectString);
			StructDestroy(parse_UrlArgumentList, pUrlArgs);
			return;
		}

		pFirstDot = strchr(pXPath, '.');


		if (!DecodeContainerTypeAndIDFromString(pXPath, &eContainerType, &iContainerID))
		{
			httpSendStr(link, STACK_SPRINTF("Error while parsing XPATH %s... couldn't decode container type/ID", pXPath));
			StructDestroy(parse_UrlArgumentList, pUrlArgs);
			return;
		}


	
		pRequest = StructCreate(parse_PendingHttpRequest);
		pRequest->iRequestID = siNextRequestID++;
		pRequest->pLink = link;
		pRequest->pUrlArgs = pUrlArgs;
		pRequest->iRequestTimeTicks = timerCpuTicks64();

		if (strStartsWith(pUrlArgs->pBaseURL, "/viewxpath"))
		{
			pRequest->eType = REQUEST_VIEWXPATH;
		}
		else if (strStartsWith(pUrlArgs->pBaseURL, "/edit"))
		{
			pRequest->eType = REQUEST_EDITFIELD;
			estrCopy2(&pRequest->pFieldPath, urlFindValue(pUrlArgs, "fieldpath"));
		}
		else if (strStartsWith(pUrlArgs->pBaseURL, "/commit"))
		{
			pRequest->eType = REQUEST_COMMITEDIT;
			estrCopy2(&pRequest->pFieldPath, urlFindValue(pUrlArgs, "fieldpath"));
			estrCopy2(&pRequest->pCommandString, "Apply Transaction");
		}
		else if (strStartsWith(pUrlArgs->pBaseURL, "/command"))
		{
			pRequest->eType = REQUEST_COMMAND;
			estrCopy2(&pRequest->pCommandString, urlFindValue(pUrlArgs, "cmd"));
			if (!estrLength(&pRequest->pCommandString))
			{
				httpSendStr(link, STACK_SPRINTF("Error while trying to execute command for xpath %s... couldn't find \"cmd\"", pXPath));
				
				//should destroy pUrlArgs, which is now a substruct of pRequest
				StructDestroy(parse_PendingHttpRequest, pRequest);
				return;
			}
		}
		else if (strStartsWith(pUrlArgs->pBaseURL, "/process"))
		{
			if(strcmp(urlFindSafeValue(pUrlArgs, "confirm"), "Yes"))
			{
				// In theory, we should never get here, as the No button's javascript should be performing a "back()" operation
				httpSendClientError(link, "Operation cancelled.");

				//should destroy pUrlArgs, which is now a substruct of pRequest
				StructDestroy(parse_PendingHttpRequest, pRequest);
				return;
			}

			pRequest->eType = REQUEST_PROCESSCOMMAND;
			estrCopy2(&pRequest->pCommandString, urlFindValue(pUrlArgs, "cmd"));
			if (!estrLength(&pRequest->pCommandString))
			{
				httpSendStr(link, STACK_SPRINTF("Error while trying to process command for xpath %s... couldn't find \"cmd\"", pXPath));

				//should destroy pUrlArgs, which is now a substruct of pRequest
				StructDestroy(parse_PendingHttpRequest, pRequest);
				return;
			}
		}

		


		eaPush(&sppRequests, pRequest);

		if (pFirstDot)
		{
			*pFirstDot = '.';
		}

		IssueXpathRequest(pRequest->iRequestID, eContainerType, iContainerID, pFirstDot ? pFirstDot : "", pUrlArgs->ppUrlArgList, accesslevel,
			httpFindAuthenticationUsernameAndIP(pRequest->pLink, &pClientState->baseState));

		pUrlArgs = NULL; //so it won't get destroyed when we want it hanging around off our request
	}
	else if (strStartsWith(pUrlArgs->pBaseURL, "/waitForCommand"))
	{
		int iRequestID = atoi(urlFindSafeValue(pUrlArgs, "commandid"));
		int iRequestCount = atoi(urlFindSafeValue(pUrlArgs, "count"));
		CommandReturnValue *pReturnVal;

		char *pOutString = NULL;
		bool bOutputXML = false;

		estrStackCreate(&pOutString);
		if (urlFindSafeValue(pUrlArgs, "xml")[0]=='1' ||
			strcmpi(urlFindSafeValue(pUrlArgs, "format"), "xml") == 0)
			bOutputXML = true;
		
		if (stashIntRemovePointer(sCommandReturnValues, iRequestID, &pReturnVal))
		{
			// To fix this, one would have to implement HTTP session control. You can do this if you'd like to.
			// Alternatively you could change the redirection response code based on User Agent.
			// IE7 incorrectly automatically redirects on 307 without user confirmation. This can be exploited to make
			// IE7 maintain the connection while redirecting. Firefox does the correct confirmation so you'd have to
			// check the user agent and only do a 307 if you have an MSIE user agent.
			//XXXXXX: Nixing this check because IE7 likes to open multiple connections _just because_.
			//if (pReturnVal->iClientID == ((HttpClientState*)linkGetUserData(link))->iClientID)
			//{
				if (bOutputXML)
				{
					if (stricmp_safe(pReturnVal->pResultString, SERVERMON_CMD_RESULT_HIDDEN) == 0)
					{
						estrPrintf(&pOutString, "<command_response value=\"success_hidden\"></command_response>");
	
					}
					else
					{
						estrPrintf(&pOutString, "<command_response value=\"success\">%s</command_response>",pReturnVal->pResultString);
					}
				}
				else
				{
					estrPrintf(&pOutString, "<html><body>\n%s<br><a href=\"/viewxpath?xpath=%s\">Done</a>\n</body></html>", 
						pReturnVal->pResultString, urlFindSafeValue(pUrlArgs, "oldxpath"));
				}
				httpSendStr(link, pOutString);

				StructDestroy(parse_CommandReturnValue, pReturnVal);

			//}
			//else
			//{
			//	httpSendStr(link, "Hey... you are trying to look at a command ID that belongs to someone else. Cut it out!");
			//}
		}
		else
		{
			if (bOutputXML)
			{
				estrPrintf(&pOutString, 
					"<command_response value=\"defer\" />\n<form action=\"waitForCommand\"><input name=\"commandid\" value=\"%d\"><input name=\"count\" value=\"%d\"></form>",
					iRequestID,
					iRequestCount+1);
				httpSendStr(link, pOutString);
			}
			else
			{
				httpSendStr(link, "<meta http-equiv=\"refresh\" content=\"1\">Waiting for a command to complete...");
			}
		}

		HttpServingLog("waitForCommand", false, bWGSRequest, timerCpuTicks64() - iRequestTime);

		estrDestroy(&pOutString);

	}
	else if ((gbHttpServingAllowCommandsInURL || gbHttpServingAllowCommandsInURL_LocalHostOnly &&  linkIsLocalHost(link)) && strStartsWith(pUrlArgs->pBaseURL, "/directcommand"))
	{
		const char *pCmdString = urlFindValue(pUrlArgs, "command");
		if (pCmdString)
		{
			char *pResultString = NULL;
			char *pTempString = NULL;
			bool bResult = cmdParseAndReturn(pCmdString, &pResultString, CMD_CONTEXT_HOWCALLED_DIRECT_SERVER_MONITORING);

			if (bResult)
			{
				estrPrintf(&pTempString, "Command %s executed - return value: %s", pCmdString, pResultString);
				estrCopyWithHTMLEscaping(&pResultString, pTempString, false);
				httpSendStr(link, pResultString);
			}
			else
			{
				estrPrintf(&pTempString, "Command %s couldn't be executed", pCmdString);
				estrCopyWithHTMLEscaping(&pResultString, pTempString, false);
				httpSendStr(link, pResultString);
			}

			estrDestroy(&pResultString);
			estrDestroy(&pTempString);
		}
		else
		{
			httpSendClientError(link, "couldn't find command to execute");
		}

		HttpServingLog("directCommand", false, bWGSRequest, timerCpuTicks64() - iRequestTime);

	}
	else if (strStartsWith(pUrlArgs->pBaseURL, "/viewimage"))
	{
		const char *pImageName = urlFindSafeValue(pUrlArgs, "imageName");
		const char *pTitle = urlFindSafeValue(pUrlArgs, "imageTitle");
	
		if (!pImageName)
		{
			httpSendClientError(link, "badly formatted image request");
		}
		else if (!(strEndsWith(pImageName, ".jpg") || strEndsWith(pImageName, ".png")))
		{
			httpSendClientError(link, "image names must end with .jpg or .png");
		}
		else
		{
			char *pTempString = NULL;
			char *pArgsString = NULL;
			int i;

			estrStackCreate(&pTempString);
			estrStackCreate(&pArgsString);	
			estrCopy2(&pArgsString, "");


			//all url arguments that begin with "jpg" are passed into the bowels of the jpeg finding callback system
			for (i=0; i < eaSize(&pUrlArgs->ppUrlArgList); i++)
			{
				if (strStartsWith(pUrlArgs->ppUrlArgList[i]->arg, "jpg"))
				{
					estrConcatf(&pArgsString, "&%s=%s", pUrlArgs->ppUrlArgList[i]->arg, pUrlArgs->ppUrlArgList[i]->value);
				}
			}


			if (pTitle)
			{
				estrPrintf(&pTempString, "<html><div><IMG src=\"%s%s\" alt=\"%s\"></div>%s</html>", pImageName, pArgsString, pTitle, pTitle);
			}
			else
			{
				estrPrintf(&pTempString, "<html><IMG src=\"%s%s\"></html>", pImageName, pArgsString);

			}
			httpSendStr(link, pTempString);
			estrDestroy(&pArgsString);
			estrDestroy(&pTempString);
		}

		HttpServingLog("viewimage", false, bWGSRequest, timerCpuTicks64() - iRequestTime);

	}
	else if (strStartsWith(pUrlArgs->pBaseURL, "/file"))
	{
		if (!spFileCB)
		{
			httpSendFileNotFoundError(link, "File serving not supported");
		}
		else
		{

			char *pTempString = NULL;
			PendingFileRequest *pNewFileRequest = StructCreate(parse_PendingFileRequest);
			pNewFileRequest->iRequestID = siNextFileRequestID++;
			pNewFileRequest->pLink = link;
			estrStackCreate(&pTempString);
			estrCopyWithURIUnescaping(&pTempString, pUrlArgs->pBaseURL + 5);
			pNewFileRequest->pFileName = strdup(pTempString);
			estrDestroy(&pTempString);

			eaPush(&sppPendingFileRequests, pNewFileRequest);


		}


	
	/*	char simpleFileName[CRYPTIC_MAX_PATH];
		char fullFileName[CRYPTIC_MAX_PATH];
		int i;
		bool bFound = false;

		getFileNameNoDir(simpleFileName, pUrlArgs->pBaseURL);

		for (i=0; i < eaSize(&sppDownloadDirs); i++)
		{
			sprintf(fullFileName, "%s\\%s", sppDownloadDirs[i], simpleFileName);
			backSlashes(fullFileName);
			if (fileExists(fullFileName))
			{
				sendFileToLink(link, fullFileName, "application/octet-stream", false);
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			httpSendFileNotFoundError(link, "File Not Found");

		}*/
		
	}

	// /time: Compare server time with browser's time to verify that clocks are synchronized.
	else if (strStartsWith(pUrlArgs->pBaseURL, "/time"))
	{
		char *document = NULL;
		SYSTEMTIME now;
		GetSystemTime(&now);
		estrStackCreate(&document);
		estrPrintf(&document,
			"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
			"<html xmlns=\"http://www.w3.org/1999/xhtml\">"
			"  <head><meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\" />"
			"  <title>JavaScript Time Synchronization Check</title></head>\n"
			"<body><h1>JavaScript Time Synchronization Check</h1><div><script type=\"text/javascript\">\n"
			"  //<![CDATA[\n"
			"  var now = new Date()\n"
			"  var server = new Date()\n"
			"  server.setTime(Date.UTC(%u, %u, %u, %u, %u, %u, %u))\n"
			"  document.write('<table><tr><td><b>Local</b></td><td>' + now.toUTCString() + '</td></tr><tr><td><b>Server</b></td><td>' + server.toUTCString()"
				" + '</td></tr><tr><td><b>Delta</b></td><td>' + (now.getTime() - server.getTime())/1000 + ' seconds</td></tr></table>')\n"
			"  //]]>\n"
			"</script></div></body></html>\n",
			now.wYear, now.wMonth - 1, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
		httpSendHeader(link, estrLength(&document), "Content-Type", "text/html; charset=utf-8", "Cache-Control", "no-cache", NULL);
		httpSendBytesRaw(link, document, estrLength(&document));
		httpSendComplete(link);
		estrDestroy(&document);
	}

	else if (strstri(pUrlArgs->pBaseURL, ".jpg") || strstri(pUrlArgs->pBaseURL, ".png"))
	{
		char *pFirstAmpersand = strchr(pUrlArgs->pBaseURL, '&');

		if (!pFirstAmpersand)
		{
			UrlArgumentList jpegArgs = {0};
			HttpServingRequestJpeg(pUrlArgs->pBaseURL + 1, &jpegArgs, link);
		}
		else
		{
			UrlArgumentList *pJpegArgs = urlToUrlArgumentList_Internal(pFirstAmpersand + 1, true);
			*pFirstAmpersand = 0;
			HttpServingRequestJpeg(pUrlArgs->pBaseURL + 1, pJpegArgs, link);
			StructDestroy(parse_UrlArgumentList, pJpegArgs);
		}
	}
	else
	{
		httpSendFileNotFoundError(link, "not doing anything");
	}

	if (pUrlArgs)
	{
		StructDestroy(parse_UrlArgumentList, pUrlArgs);
	}
}



static void HttpServingMsg(Packet *pkt,int cmd,NetLink *link, HttpClientState *pClientState)
{
	httpProcessPostDataAuthenticated(link, (HttpClientStateDefault*)pClientState, pkt, HttpHandleQuery);
}

static int HttpServingConnect(NetLink *link, HttpClientState *pClientState)
{
	static int iNextClientID = 1;
	pClientState->pParentLink = link;
	pClientState->iClientID = iNextClientID++;
	eaPush(&ppClientStates, pClientState);

	HttpStats_NewLink(link);

	return 1;
}

static int HttpServingDisconnect(NetLink *link, HttpClientState *pClientState)
{
	int i;

	HttpStats_LinkClosed(link);


	for (i = eaSize(&sppRequests) - 1; i >=0; i--)
	{
		if (sppRequests[i]->pLink == link)
		{
			StructDestroy(parse_PendingHttpRequest, sppRequests[i]);
			eaRemoveFast(&sppRequests, i);
		}
	}

	for (i = eaSize(&sppPendingJpegRequests) - 1; i >=0 ; i--)
	{
		if (sppPendingJpegRequests[i]->pLink == link)
		{
			StructDestroy(parse_PendingJpegRequest, sppPendingJpegRequests[i]);
			eaRemoveFast(&sppPendingJpegRequests, i);
		}
	}

	for (i = eaSize(&sppPendingFileRequests) -1; i >=0; i--)
	{
		if (sppPendingFileRequests[i]->pLink == link)
		{
			spFileCB(sppPendingFileRequests[i]->pFileName, sppPendingFileRequests[i]->iRequestID, FILESERVING_CANCEL,
				0, HttpServing_FileFulfill);

			CleanupFileRequest(i);
		}
	}

	FOR_EACH_IN_STASHTABLE(sXMLRPCReturnLinks, PendingXMLRPCRequest, req) {
		if (req->pLink == link)
		{
			if (stashIntRemovePointer(sXMLRPCReturnLinks, req->iRequestID, &req))
			{
				StructDestroy(parse_PendingXMLRPCRequest, req);
			}
		}
	} FOR_EACH_END;

	if (pClientState)
	{
		httpCleanupClientState(&pClientState->baseState);
		eaFindAndRemoveFast(&ppClientStates, pClientState);
	}

	return 0;

}

static char * findCloseParen(char* start)
{
	int count = 1;
	do
	{
		if (*start == '\0') return NULL;
		if (*start == '(') count++;
		if (*start == ')') 
		{
			count--;
			if (!count) return start;
		}
		start++;
	} while (count);
	return NULL;
}

static bool sbLogAllHttpRequestsFulfilled = false;
AUTO_CMD_INT(sbLogAllHttpRequestsFulfilled, LogAllHttpRequestsFulfilled);


//This is a dummy auto command to use with redirect AST_COMMANDS.
AUTO_COMMAND;
void Redirect(void) { return; }

static void RequestFulfilled(int iRequestID, bool bIsRootStruct, bool bIsArray, char *pRedirectString0, 
	void *pStruct, ParseTable **ppTPI, int iColumn, int iIndex, char *pPrefix, char *pSuffix, char *pExtraStylesheets, char *pExtraScripts,
	char *pErrorString, bool bFulfilledFromCache, bool bNeedsAccessLevelChecksForWriting)
{
	int iRequestNum;
	bool bRedirected = false;

	if (sbLogAllHttpRequestsFulfilled)
	{
		log_printf(LOG_SERVERMONITOR, "Got a request fulfilled, %s cache\n", bFulfilledFromCache ? "FROM": "NOT FROM");
	}

	for (iRequestNum=0; iRequestNum < eaSize(&sppRequests); iRequestNum++)
	{
		if (sppRequests[iRequestNum]->iRequestID == iRequestID)
		{
			PendingHttpRequest *pRequest = sppRequests[iRequestNum];
			bool bOutputXML = false;
			bool bNoCache = false;
			char *pTableName = NULL;

			WriteHTMLContext context = {0};
			char *pOutString = NULL;

			HttpClientState *pClientState = (HttpClientState*)linkGetUserData(pRequest->pLink);

			if (sbLogAllHttpRequestsFulfilled)
			{
				char *pTPIString = NULL;
					log_printf(LOG_SERVERMONITOR, "Found request...\n");
				objLogWithStruct(LOG_SERVERMONITOR, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, pRequest, parse_PendingHttpRequest);
			
				log_printf(LOG_SERVERMONITOR, "HttpCientState...\n");
				objLogWithStruct(LOG_SERVERMONITOR, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, &pClientState->baseState, parse_HttpClientStateDefault);

				if (ppTPI && pStruct)
				{
					ParseTableWriteText(&pTPIString, *ppTPI, "TPI", PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL);
					log_printf(LOG_SERVERMONITOR, "TPI: %s\n", pTPIString);
					objLogWithStruct(LOG_SERVERMONITOR, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, pStruct, *ppTPI);
					estrDestroy(&pTPIString);
				}
				else
				{
					log_printf(LOG_SERVERMONITOR, "No struct... error string %s\n", pErrorString);
				}
			}

			if (urlFindSafeValue(pRequest->pUrlArgs, "xml")[0]=='1' ||
				strcmpi(urlFindSafeValue(pRequest->pUrlArgs, "format"), "xml") == 0)
			{
				bOutputXML = true;
			}

			if (urlFindValue(pRequest->pUrlArgs, "nocache"))
			{
				bNoCache = true;
			}

			if (pErrorString && pErrorString[0])
			{
				httpSendClientError(sppRequests[iRequestNum]->pLink, pErrorString);
			}
			else if (pRedirectString0 && pRedirectString0[0])
			{
				bRedirected = true;
				httpRedirect(sppRequests[iRequestNum]->pLink, pRedirectString0);
			}
			else
			{
				initWriteHTMLContext(&context, bIsArray, pRequest->pUrlArgs, 0, urlFindValue(pRequest->pUrlArgs, "xpath"),
					"viewxpath", "command", "process", bNeedsAccessLevelChecksForWriting);
			

				if (pRequest->eType == REQUEST_EDITFIELD)
				{
					char *pValue = 0;
					ParseTable *pTPIToUse = NULL;
					void *pStructToUse = NULL;

					if (bIsRootStruct)
					{
						pTPIToUse = ppTPI[0];
						pStructToUse = pStruct;
					}
					else
					{
						if (TOK_GET_TYPE(ppTPI[0][iColumn].type) == TOK_STRUCT_X)
						{
							pTPIToUse = ppTPI[0][iColumn].subtable;
							pStructToUse = TokenStoreGetPointer(ppTPI[0], iColumn, pStruct, iIndex, NULL);
						}
					}

					estrStackCreate(&pValue);
					if (objPathGetEString(pRequest->pFieldPath, pTPIToUse, pStructToUse, &pValue))
					{
						estrConcatf(&pOutString, 
							"<command_response value=\"edit\" />"
								"<form action=\"commit\">"
									"<input name=\"xpath\" value=\"%s\" type=\"hidden\" >"
									"<input name=\"fieldpath\" value=\"%s\" type=\"hidden\">"
									"New value for %s: <input name=\"value\" value=\"%s\">"
								"</form>",
							urlFindSafeValue(pRequest->pUrlArgs, "xpath"),
							pRequest->pFieldPath,
							pRequest->pFieldPath,
							pValue
						);
					}
					else
					{
						estrConcatf(&pOutString, "<command_response value=\"error\">Could not edit field:%s</command_response>", pRequest->pFieldPath);			
					}
					estrDestroy(&pValue);
				}
				else if (pRequest->eType == REQUEST_COMMITEDIT)
				{
					//Transmogrify commitedit command into a process command pointing to "Apply Transaction"
					char * pTransString = 0;
					estrStackCreate(&pTransString);
					estrPrintf(&pTransString, "set %s = \"%s\"", pRequest->pFieldPath, urlFindSafeValue(pRequest->pUrlArgs, "value"));
					urlAddValue(pRequest->pUrlArgs,"fieldTransaction String", pTransString, HTTPMETHOD_GET);
					pRequest->eType = REQUEST_PROCESSCOMMAND;
					estrDestroy(&pTransString);
				}

				if (pRequest->eType == REQUEST_COMMAND || pRequest->eType == REQUEST_PROCESSCOMMAND)
				{
					ParseTable *pTPIToUse = NULL;
					void *pStructToUse = NULL;

					if (bIsRootStruct)
					{
						pTPIToUse = ppTPI[0];
						pStructToUse = pStruct;
					}
					else
					{
						if (TOK_GET_TYPE(ppTPI[0][iColumn].type) == TOK_STRUCT_X)
						{
							pTPIToUse = ppTPI[0][iColumn].subtable;
							pStructToUse = TokenStoreGetPointer(ppTPI[0], iColumn, pStruct, iIndex, NULL);
						}
					}

					if (pTPIToUse && pStructToUse)
					{
						int i;
						char commandNameStringToFind[1024];
						sprintf(commandNameStringToFind, "$COMMANDNAME(%s)", pRequest->pCommandString);

						FORALL_PARSETABLE(pTPIToUse, i)
						{
							if (TOK_GET_TYPE(pTPIToUse[i].type) == TOK_COMMAND
								|| GetBoolFromTPIFormatString(&pTPIToUse[i], "command"))
							{
								char *pCmdString = NULL;
								bool bFoundCommand = false;

								estrStackCreate(&pCmdString);

								if (GetBoolFromTPIFormatString(&pTPIToUse[i], "command"))
								{
									assert(FieldWriteText(pTPIToUse, i, pStructToUse, 0, &pCmdString, false));
								}
								else
								{
									estrCopy2(&pCmdString, (const char*)pTPIToUse[i].param);
								}


								if (strstr(pCmdString, "$COMMANDNAME("))
								{
									if (strstr(pCmdString, commandNameStringToFind))
									{
										bFoundCommand = true;
									}
								}
								else
								{
									bFoundCommand = (strcmp((const char *)pTPIToUse[i].name, pRequest->pCommandString) == 0);
								}


								if (!bFoundCommand)
								{
									estrDestroy(&pCmdString);
								}
								else
								{
									bool bExecuteImmediately = false;
									bool bNoReturn = false;
									bool bExecuteOnMCP = false;
									
									char *pCmdNameString = NULL;
									char *pRedirectString = NULL;
									bool bDontExecute = false;


									//remove $COMMANDNAME(x), which no one function call will do because of
									//the argument
									if ((pCmdNameString = strstr(pCmdString, "$COMMANDNAME(")))
									{
										char *pRightParens = strchr(pCmdNameString, ')');

										if (pRightParens)
										{
											estrRemove(&pCmdString, pCmdNameString - pCmdString, pRightParens - pCmdNameString + 1);
										}
									}
																			   
									if ((pRedirectString = strstr(pCmdString, "$REDIRECT_AND_EXEC(")))
									{
										int len = 0;
										char *pEndRedirect = NULL;
										char *pFoundRedirectString = pRedirectString;
										pEndRedirect = findCloseParen(pRedirectString + strlen("$REDIRECT_AND_EXEC("));

										if (pEndRedirect)
										{
											estrStackCreate(&pRedirectString);
											estrCopy2(&pRedirectString, pFoundRedirectString);
											estrRemoveUpToFirstOccurrence(&pRedirectString, '(');
											estrTruncateAtFirstOccurrence(&pRedirectString, ')');

											estrRemove(&pCmdString, pFoundRedirectString - pCmdString, pEndRedirect - pFoundRedirectString + 1);

											bExecuteImmediately = true;
										}
										else
										{
											pRedirectString = NULL;
										}
									} 
									else if ((pRedirectString = strstr(pCmdString, "$REDIRECT(")))
									{
										int len = 0;
										char *pEndRedirect = NULL;
										estrRemove(&pCmdString, pRedirectString - pCmdString, (int)strlen("$REDIRECT("));
										pEndRedirect = findCloseParen(pRedirectString);
										if (pEndRedirect)
										{
											estrRemove(&pCmdString, pEndRedirect - pCmdString, 1);
											len = pEndRedirect - pRedirectString;
											pEndRedirect = pRedirectString;
											pRedirectString = NULL;
											estrStackCreate(&pRedirectString);
											estrPrintf(&pRedirectString, "%s", pEndRedirect);
											estrSetSize(&pRedirectString, len);

											bExecuteImmediately = true;
										}
										else
										{
											pRedirectString = NULL;
										}

										bDontExecute = true;
									}


									estrReplaceOccurrences(&pCmdString, "$BROWSERIP", makeIpStr(linkGetIp(pRequest->pLink)));
							
									estrReplaceOccurrences(&pCmdString, "$HIDE", "");

									if (strstr(pCmdString, "$NOCONFIRM"))
									{
										bExecuteImmediately = true;
										estrReplaceOccurrences(&pCmdString, "$NOCONFIRM", "");
									}

									if (strstr(pCmdString, "$NORETURN"))
									{
										bNoReturn = true;
										estrReplaceOccurrences(&pCmdString, "$NORETURN", "");
									}

									if (strstr(pCmdString, "$EXECUTEONMPC"))
									{
										bExecuteOnMCP = true;
										estrReplaceOccurrences(&pCmdString, "$EXECUTEONMPC", "");
									}

									if (strstr(pCmdString, "$USERNAME"))
									{
										const char *pUserName = NULL;

										if (pClientState)
										{
											pUserName = httpFindAuthenticationUsername(&pClientState->baseState);
										}

										if (!pUserName)
										{
											pUserName = "UNKNOWN";
										}

										estrReplaceOccurrences(&pCmdString, "$USERNAME", pUserName);
									}

									log_printf(LOG_COMMANDS, "%s ran servermonitor command:%s", makeIpStr(linkGetIp(pRequest->pLink)), pCmdString);

									if (pRequest->eType == REQUEST_COMMAND && !bExecuteImmediately)
									{
										char *pTempString = NULL;
										estrStackCreate(&pTempString);
										
										//***This function clobbers the first parameter estr.
										ParserWriteConfirmationForm(&pTempString, 
											pCmdString,
											ppTPI[0],
											iColumn,
											pStruct,
											iIndex,
											bIsRootStruct,
											&context);

										estrDestroy(&pCmdString);
										
										if (bOutputXML) 
										{
											estrConcatf(&pOutString, "<command_response value=\"form\" name=\"%s\">%s</command_response>",
														pTPIToUse[i].name,
														pTempString);
										}
										else 
										{
											estrPrintf(&pOutString, "<b>%s</b><br><br>\n%s",pTPIToUse[i].name, pTempString);
										}

										estrDestroy(&pTempString);
										if (pRedirectString) estrDestroy(&pRedirectString);
										break;
									}
									else
									{	
										GlobalType eContainerType;
										ContainerID iContainerID;
										char *pCmdResponse = NULL;
										char *pRedirectResponse = NULL;
										char *pCmdCopy = NULL;

										//Packet *pPak;

										if (pRedirectString) 
										{
											estrStackCreate(&pRedirectResponse);
											ParserGenerateCommand(
												&pRedirectResponse,
												pRedirectString,
												ppTPI[0],
												iColumn,
												pStruct,
												iIndex,
												bIsRootStruct,
												&context);
										}

										estrStackCreate(&pCmdResponse);
										ParserGenerateCommand(
											&pCmdResponse,
											pCmdString,
											ppTPI[0],
											iColumn,
											pStruct,
											iIndex,
											bIsRootStruct,
											&context);
										estrDestroy(&pCmdString);

										//output the command response first so the command parser can grok it.
										estrPrintf(&pOutString, "%s",pCmdResponse);
										estrDestroy(&pCmdResponse);
										
										if (pRedirectString)
										{
											if (!bDontExecute)
											{
												int iAccessLevelToUse = httpFindAuthenticationAccessLevel(pRequest->pLink, pClientState->baseState.iAuthStringCRC, pRequest->pUrlArgs);
												//if we are redirect AND executing, execute now before the strings get trashed, assume NORETURN

												if (DecodeContainerTypeAndIDFromString(urlFindSafeValue(pRequest->pUrlArgs, "xpath"), &eContainerType, &iContainerID))
												{
													spCommandCB(eContainerType, iContainerID, pClientState->iClientID,
														++iCommandRequestID, CMDSRV_NORETURN, pOutString, iAccessLevelToUse, httpFindAuthenticationUsernameAndIP(pRequest->pLink, &pClientState->baseState));
												}
											}


											if (bOutputXML) 
											{	//in case we wanted an xml response, don't redirect.
												estrConcatf(&pOutString, "<command_response value=\"redirect\" href=\"%s\" />", pRedirectResponse);
											}
											else 
											{
												httpRedirect(pRequest->pLink, pRedirectResponse);
												bRedirected = true;
											}
											estrDestroy(&pRedirectResponse);
											estrDestroy(&pRedirectString);
											
										} 
										else if (bExecuteOnMCP)
										{
											globCmdParse(pOutString);
											if (bOutputXML) 
											{	//in case we wanted an xml response, don't redirect.
												estrConcatf(&pOutString, "<command_response value=\"onmcp\" />");
											}
											else {
												httpRedirect(pRequest->pLink, STACK_SPRINTF("/viewxpath?xpath=%s", urlFindSafeValue(pRequest->pUrlArgs, "oldxpath")));
												bRedirected = true;
											}
										}
										else
										{
											int iAccessLevelToUse = httpFindAuthenticationAccessLevel(pRequest->pLink, pClientState->baseState.iAuthStringCRC, pRequest->pUrlArgs);

											//the command to execute is now in pOutString. Execute it on the correct server
											if (!DecodeContainerTypeAndIDFromString(urlFindSafeValue(pRequest->pUrlArgs, "xpath"), &eContainerType, &iContainerID))
											{
												if (bOutputXML)
												{	//reissue command request.
													estrConcatf(&pOutString, "<command_response value=\"error\">%s</command_response>",urlFindSafeValue(pRequest->pUrlArgs, "xpath"));
												}
												else
												{
													estrPrintf(&pOutString, "Error while decoding container string %s", urlFindSafeValue(pRequest->pUrlArgs, "xpath"));
												}
												estrDestroy(&pCmdResponse);
												break;
											}

											//XXXXXX: XMLify this????
											spCommandCB(eContainerType, iContainerID, pClientState->iClientID,
												++iCommandRequestID, bNoReturn ? CMDSRV_NORETURN : 0, pOutString, iAccessLevelToUse, httpFindAuthenticationUsernameAndIP(pRequest->pLink, &pClientState->baseState));


										/*	pPak = pktCreate(svrGetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_MONITORING_COMMAND);
											
											PutContainerTypeIntoPacket(pPak, eContainerType);
											PutContainerIDIntoPacket(pPak, iContainerID);
		
											pktSendBits(pPak, 32, ((HttpClientState*)linkGetUserData(pRequest->pLink))->iClientID);
											pktSendBits(pPak, 32, ++iCommandRequestID);

											PutContainerIDIntoPacket(pPak, gServerLibState.containerID);

											pktSendString(pPak, pOutString);
											pktSendBits(pPak, 1, bNoReturn);


											pktSend(&pPak);	*/							

										
											if (bOutputXML)
											{	//reissue command request.
												estrConcatf(&pOutString, 
													"<command_response value=\"defer\" /><form action=\"waitForCommand\"><input name=\"commandid\" value=\"%d\"><input name=\"count\" value=\"1\"></form>",
													iCommandRequestID);
											}
											else
											{
												httpRedirect(pRequest->pLink, STACK_SPRINTF("/waitForCommand?commandid=%d&oldxpath=%s", iCommandRequestID, urlFindSafeValue(pRequest->pUrlArgs, "oldxpath")));
												bRedirected = true;
											}
											
										}
										break;

									}
								}
							}
						}
					}
				}


		
				//we get here if either the thing wasn't a command, or it was a command but failed somehow
				//in its command-ness. In either case, we display it as a normal TPI

				if (!estrLength(&pOutString) && !bRedirected)
				{
					if (bIsRootStruct && urlFindSafeValue(pRequest->pUrlArgs, "xml")[0] == '1' ||
						strcmpi(urlFindSafeValue(pRequest->pUrlArgs, "format"), "xml") == 0)
					{
						ParserWriteXML(&pOutString, ppTPI[0], pStruct);
					}
					else if (urlFindSafeValue(pRequest->pUrlArgs, "json")[0] == '1' ||
						strcmpi(urlFindSafeValue(pRequest->pUrlArgs, "format"), "json") == 0)
					{
						if (bIsRootStruct)
						{
							ParserWriteJSON(&pOutString, ppTPI[0], pStruct, 0, 0, 0);
						}
						else
						{
							ParserWriteJSONEx(&pOutString, ppTPI[0], iColumn, pStruct, iIndex, 0);
						}
					}
					else if (urlFindSafeValue(pRequest->pUrlArgs, "textparser")[0] == '1' ||
						strcmpi(urlFindSafeValue(pRequest->pUrlArgs, "format"), "textparser") == 0)
					{
						if (stricmp(ParserGetTableName(ppTPI[0]), "HttpGlobObjWrapper") == 0)
						{
							HttpGlobObjWrapper *pParent = (HttpGlobObjWrapper*)pStruct;
							void *pRealStruct = pParent->pStruct;
							ParseTable *pRealTPI = (ppTPI[0])[PARSE_HTTPGLOBOBJWRAPPER_STRUCT_INDEX].subtable;

							estrConcatf(&pOutString, "//TextParser generated page using parseTable %s\n\n", ParserGetTableName(pRealTPI));
							ParserWriteText(&pOutString, pRealTPI, pRealStruct, 0, 0, 0);

						}
						else
						{
							estrConcatf(&pOutString, "//TextParser generated page using parseTable %s\n\n", ParserGetTableName(ppTPI[0]));
							ParserWriteText(&pOutString, ppTPI[0], pStruct, 0, 0, 0);
						}
					}
					else
					{
						bool bUpdate = (urlFindSafeValue(pRequest->pUrlArgs, "update")[0] == '1');
				

						if (spHTMLHeader && !bUpdate)
						{
							estrConcatf(&pOutString, "%s", spHTMLHeader);
							if (pExtraStylesheets)
								estrReplaceOccurrences(&pOutString, "<!--#EXTRA_STYLESHEETS -->", pExtraStylesheets);
							if (pExtraStylesheets)
								estrReplaceOccurrences(&pOutString, "<!--#EXTRA_SCRIPTS -->", pExtraScripts);
						}

						if (pPrefix)
						{
							estrConcatf(&pOutString, "%s", pPrefix);
						}

						
						SetHTMLAccessLevel(httpFindAuthenticationAccessLevel(pRequest->pLink, pClientState->baseState.iAuthStringCRC, pRequest->pUrlArgs));



						if (bIsRootStruct)
						{
						
							ParserWriteHTML(&pOutString, ppTPI[0], pStruct, &context);
							
						}
						else
						{
							ParserWriteHTMLEx(&pOutString, ppTPI[0], iColumn, pStruct, iIndex, &context);
						}

						if (pSuffix)
						{
							estrConcatf(&pOutString, "%s", pSuffix);
						}



						if (spHTMLFooter && !bUpdate)
						{
							char *pCustomFooterString = NULL;
							char *pFooterString = NULL;
							estrStackCreate(&pCustomFooterString);
							estrStackCreate(&pFooterString);

							estrCopy2(&pFooterString, spHTMLFooter);
							
							AddCustomHTMLFooter(&pCustomFooterString);
							if (estrLength(&pCustomFooterString))
							{
								char *pCloseBodyTag = strrstr(pFooterString, "</body>");
								if (pCloseBodyTag)
								{
									estrInsertf(&pFooterString, pCloseBodyTag - pOutString, "\n%s\n", pCustomFooterString);
								}						
							}
							estrDestroy(&pCustomFooterString);
							estrConcatf(&pOutString, "%s", pFooterString);
							estrDestroy(&pFooterString);
						}

						//Add the shard title via javascript
						if (!bUpdate)
						{
							char *shardName = GetShardNameFromShardInfoString();
							estrConcatf(&pOutString, "<script language=\"javascript\" type=\"text/javascript\">document.title = document.title + \" - %s\";</script>", shardName);
						}


					}

				}

				if (pOutString && !bRedirected)
				{
					int len = (int)strlen(pOutString);
					if (bNoCache)
						httpSendHeader(pRequest->pLink, len, "Cache-Control", "no-cache", NULL);
					else
						httpSendHeader(pRequest->pLink, len, NULL);
					httpSendBytesRaw(pRequest->pLink, pOutString, len);
					httpSendComplete(pRequest->pLink);
				}
		
	

				estrDestroy(&pOutString);


				shutdownWriteHTMLContext(&context);


			}

			HttpServingLog(StaticDefineIntRevLookup(enumPendingRequestTypeEnum, pRequest->eType), bFulfilledFromCache, bOutputXML, timerCpuTicks64() - pRequest->iRequestTimeTicks);


			StructDestroy(parse_PendingHttpRequest, sppRequests[iRequestNum]);
			eaRemoveFast(&sppRequests, iRequestNum);
			return;
		}
	}

}


//given a list of URL arguments, sorts out the ones that are evaluated server side and puts them
//into a different list
void FindServerSideURLArgs(UrlArgument ***pppOutArgs, UrlArgument **ppInArgs)
{
	int i;

	for (i=0; i < eaSize(&ppInArgs); i++)
	{
		if (strStartsWith(ppInArgs[i]->arg, "svr"))
		{
			UrlArgument *pNewArg = StructCreate(parse_UrlArgument);

			StructCopyAll(parse_UrlArgument, ppInArgs[i], pNewArg);

			eaPush(pppOutArgs, pNewArg);
		}
	}
}





typedef struct CachedXpath
{
	bool bIsRootStruct; 
	bool bIsArray;
	char *pRedirectString;

	ParseTable **ppTPI;
	void *pStruct;
	int iColumn; 
	int iIndex;
	S64 iObsoletionTime; //milliseconds since 2000
	char *pPrefixString; 
	char *pSuffixString;
	char *pExtraStylesheets;
	char *pExtraScripts;
} CachedXPath;


//these are requests that are waiting around to be issued
AUTO_STRUCT;
typedef struct CachedRequest
{
	int iRequestID;
	GlobalType eType;
	ContainerID iID;
	char *pXPath; AST(ESTRING)
	UrlArgument **ppServerSideURLArgs;
	int iAccessLevel;
	const char *pAuthNameAndIP; AST(POOL_STRING)
} CachedRequest;



static CachedRequest **sppCachedRequests = NULL;

//remember what requests were sent out, so that when they come back, we can figure out what
//xpath was originally associated with a request
static CachedRequest **sppRequestsThatWereSentOut = NULL;


StashTable sCachedXPathTable = NULL;

void IssueXpathRequest(int iRequestID, GlobalType eType, ContainerID iID, char *pXPath, UrlArgument **ppURLArgs, int iAccessLevel,
	const char *pAuthNameAndIP)
{
	CachedRequest *pCachedRequest = StructCreate(parse_CachedRequest);

	pCachedRequest->iRequestID = iRequestID;
	pCachedRequest->iID = iID;
	pCachedRequest->eType = eType;
	pCachedRequest->iAccessLevel = iAccessLevel;
	estrCopy2(&pCachedRequest->pXPath, pXPath);
	pCachedRequest->pAuthNameAndIP = allocAddString(pAuthNameAndIP);

	FindServerSideURLArgs(&pCachedRequest->ppServerSideURLArgs, ppURLArgs);

	eaPush(&sppCachedRequests, pCachedRequest);
}

void DestroyCachedXPath(CachedXPath *pCachedXPath)
{
	if (pCachedXPath->pStruct)
	{
		assert(pCachedXPath->ppTPI && pCachedXPath->ppTPI[0]);
		StructDestroyVoid(pCachedXPath->ppTPI[0], pCachedXPath->pStruct);
		ParseTableFree(&pCachedXPath->ppTPI);
	}
	else
	{
		assert(!pCachedXPath->ppTPI);
	}

	estrDestroy(&pCachedXPath->pRedirectString);
	estrDestroy(&pCachedXPath->pPrefixString);
	estrDestroy(&pCachedXPath->pSuffixString);
	estrDestroy(&pCachedXPath->pExtraStylesheets);
	estrDestroy(&pCachedXPath->pExtraScripts);

	free(pCachedXPath);
}

void httpCleanupXMLRPCRequests(void)
{
	EArray32Handle timeouts = NULL;
	if (!sXMLRPCReturnLinks) return;

	PERFINFO_AUTO_START_FUNC();

	FOR_EACH_IN_STASHTABLE(sXMLRPCReturnLinks, PendingXMLRPCRequest, req) {
		if (timerSeconds64(timerCpuTicks64() - req->iRequestTimeClockTicks) > req->requestTimeout)
			ea32Push(&timeouts, req->iRequestID);
	} FOR_EACH_END;

	while (ea32Size(&timeouts) > 0)
	{
		U32 id = ea32Pop(&timeouts);
		PendingXMLRPCRequest *req;
		if (stashIntRemovePointer(sXMLRPCReturnLinks, id, &req))
		{
			if (linkConnected(req->pLink) && !linkDisconnected(req->pLink))
			{
				char *out = NULL;
				XMLMethodResponse *response = XMLRPC_BuildMethodResponse(NULL, XMLRPC_FAULT_RESPONSETIMEOUT, "The XMLRPC call timed out.");
				
				estrStackCreate(&out);
				XMLRPC_WriteOutMethodResponse(response, &out);
				StructDestroy(parse_XMLMethodResponse, response);
				httpSendStr(req->pLink, out);
				estrDestroy(&out);
			}
			StructDestroy(parse_PendingXMLRPCRequest, req);
		}
	}
	ea32Destroy(&timeouts);
	PERFINFO_AUTO_STOP();
}


void ProcessXPathRequests(void)
{
	int requestNum;
	char *pKeyString = NULL;

	CachedRequest **ppDupedCachedRequests = NULL;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pKeyString);

	eaCopy(&ppDupedCachedRequests, &sppCachedRequests);
	eaDestroy(&sppCachedRequests);


	for (requestNum=0; requestNum < eaSize(&ppDupedCachedRequests); requestNum++)
	{
		CachedRequest *pRequest = ppDupedCachedRequests[requestNum];
		//Packet *pPak;
		CachedXPath *pCachedXPath;
		bool bFoundCachedCopy = false;


		estrPrintf(&pKeyString, "%s %u %s %d %p", GlobalTypeToName(pRequest->eType), pRequest->iID, pRequest->pXPath, pRequest->iAccessLevel,
			pRequest->pAuthNameAndIP);


		//we can only used cached XPaths for requests with no server-side URL args
		if (pRequest->ppServerSideURLArgs == NULL && sCachedXPathTable && stashFindPointer(sCachedXPathTable, pKeyString, &pCachedXPath))
		{
			if (pCachedXPath->iObsoletionTime < timeMsecsSince2000())
			{
				CachedXPath *pOtherXPath = NULL;
//				printf("Found a cached xpath for %s.... but it was obsolete\n", pKeyString);
				stashRemovePointer(sCachedXPathTable, pKeyString, &pOtherXPath);

				assert(pOtherXPath == pCachedXPath);
			
				DestroyCachedXPath(pCachedXPath);
			}
			else
			{
//				printf("Found a cached xpath for %s... using it\n", pKeyString);
				RequestFulfilled(pRequest->iRequestID, pCachedXPath->bIsRootStruct,
					pCachedXPath->bIsArray, pCachedXPath->pRedirectString, pCachedXPath->pStruct,
					pCachedXPath->ppTPI, pCachedXPath->iColumn, pCachedXPath->iIndex, pCachedXPath->pPrefixString, 
					pCachedXPath->pSuffixString, pCachedXPath->pExtraStylesheets, pCachedXPath->pExtraScripts, NULL, true, false);

				StructDestroy(parse_CachedRequest, pRequest);

				bFoundCachedCopy = true;
			}
		}
		else
		{
//			printf("No cached xpath for %s\n", pKeyString);
		}

		if (!bFoundCachedCopy)
		{
			eaPush(&sppRequestsThatWereSentOut, pRequest);
			spXPathCB(pRequest->eType, pRequest->iID, pRequest->iRequestID, pRequest->pXPath, pRequest->ppServerSideURLArgs, pRequest->iAccessLevel, 0, pRequest->pAuthNameAndIP);

		/*	if (svrGetControllerLink())
			{
				int i;
				pPak = pktCreate(svrGetControllerLink(), TO_CONTROLLER_MCP_REQUESTS_XPATH_FOR_HTTP);
				PutContainerTypeIntoPacket(pPak, pRequest->eType);
				PutContainerIDIntoPacket(pPak, pRequest->iID);
				PutContainerIDIntoPacket(pPak, gServerLibState.containerID);
				pktSendBits(pPak, 32, pRequest->iRequestID);
				pktSendString(pPak, pRequest->pXPath);

				for (i=0; i < eaSize(&pRequest->ppServerSideURLArgs); i++)
				{
					pktSendString(pPak, pRequest->ppServerSideURLArgs[i]->arg);
					pktSendString(pPak, pRequest->ppServerSideURLArgs[i]->value);
				}

				pktSendString(pPak, "");


				pktSend(&pPak);
			}*/


		}
	}

	estrDestroy(&pKeyString);
	eaDestroy(&ppDupedCachedRequests);
	PERFINFO_AUTO_STOP();
}


void HttpServing_XPathReturn(int iRequestID, StructInfoForHttpXpath *pStructInfo)
{
	ParseTable **ppTPI = NULL;
	void *pStruct = NULL;
	int i;
	bool bStructExistsLocally = false;

	if (pStructInfo->pErrorString && pStructInfo->pErrorString[0])
	{
		RequestFulfilled(iRequestID, false, false, NULL, 
			NULL, NULL, 0, 0, NULL, NULL, NULL, NULL, pStructInfo->pErrorString, false, false);


		//when we get errors back, never cache the result in case it changes
		return;
	}

	if (!pStructInfo->pRedirectString || !pStructInfo->pRedirectString[0])
	{
		if (pStructInfo->pLocalStruct)
		{
			bStructExistsLocally = true;
			pStruct = pStructInfo->pLocalStruct;
			ppTPI = &pStructInfo->pLocalTPI;
		}
		else
		{
			int iTableSize;
			char *pTableName;

			if (!ParseTableReadText(pStructInfo->pTPIString, &ppTPI, &iTableSize, &pTableName, PARSETABLESENDFLAG_FOR_HTTP_EXTERNAL))
			{
				assertmsg(0, "ParseTableReadText failed");
			}

			assertmsg(eaSize(&ppTPI) > 0, "Didn't get any parse tables");

		
			pStruct = StructCreateVoid(ppTPI[0]);
			ParserReadText(pStructInfo->pStructString, ppTPI[0], pStruct, 0);
		}
	}

	RequestFulfilled(iRequestID, pStructInfo->bIsRootStruct, pStructInfo->bIsArray, pStructInfo->pRedirectString, 
		pStruct, ppTPI, pStructInfo->iColumn, pStructInfo->iIndex, pStructInfo->pPrefixString, pStructInfo->pSuffixString,
		pStructInfo->pExtraStylesheets, pStructInfo->pExtraScripts, NULL, false, bStructExistsLocally);

	//now add this struct/tpi to our cache, so that the next time we request it, we can just find it locally
	for (i=0; i < eaSize(&sppRequestsThatWereSentOut); i++)
	{
		if (sppRequestsThatWereSentOut[i]->iRequestID == iRequestID)
		{
			//if it exists locally, can't cache it as we don't own it
			if (!bStructExistsLocally)
			{
				CachedXPath *pCachedXPath;
				char *pKeyString = NULL;

				estrStackCreate(&pKeyString);
				
				estrPrintf(&pKeyString, "%s %u %s %d %p", GlobalTypeToName(sppRequestsThatWereSentOut[i]->eType), 
					sppRequestsThatWereSentOut[i]->iID, sppRequestsThatWereSentOut[i]->pXPath, sppRequestsThatWereSentOut[i]->iAccessLevel,
					sppRequestsThatWereSentOut[i]->pAuthNameAndIP);


				if (!sCachedXPathTable)
				{
					sCachedXPathTable = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);
				}

				//we must have sent out two requests for the same xpath at the same time, and this is the second one
				//returning
				if (stashFindPointer(sCachedXPathTable, pKeyString, &pCachedXPath))
				{
					estrDestroy(&pKeyString);
					break;
				}

				assert(ppTPI && pStruct || !ppTPI &&!pStruct);

				pCachedXPath = calloc(sizeof(CachedXPath), 1);

				pCachedXPath->bIsRootStruct = pStructInfo->bIsRootStruct;
				pCachedXPath->bIsArray = pStructInfo->bIsArray;
				pCachedXPath->ppTPI = ppTPI;
				pCachedXPath->pStruct = pStruct;
				pCachedXPath->iColumn = pStructInfo->iColumn;
				pCachedXPath->iIndex = pStructInfo->iIndex;
				estrCopy2(&pCachedXPath->pRedirectString, pStructInfo->pRedirectString);
				estrCopy2(&pCachedXPath->pPrefixString, pStructInfo->pPrefixString);
				estrCopy2(&pCachedXPath->pSuffixString, pStructInfo->pSuffixString);
				estrCopy2(&pCachedXPath->pExtraStylesheets, pStructInfo->pExtraStylesheets);
				estrCopy2(&pCachedXPath->pExtraScripts, pStructInfo->pExtraScripts);

				switch (pStructInfo->ePersistType)
				{
				case HTTPXPATHPERSIST_DYNAMIC:
					pCachedXPath->iObsoletionTime = timeMsecsSince2000() + 1000;
					break;

				case HTTPXPATHPERSIST_STATIC:
					pCachedXPath->iObsoletionTime = timeMsecsSince2000() + 1000 * 60 * 120;
					break;
				}

	
				stashAddPointer(sCachedXPathTable, pKeyString, pCachedXPath, true);

				estrDestroy(&pKeyString);
			}

			StructDestroy(parse_CachedRequest, sppRequestsThatWereSentOut[i]);
			eaRemoveFast(&sppRequestsThatWereSentOut, i);
			return;
		}
	}

	//only get here if we didn't put the thing into the stash table
	if (pStruct)
	{
		StructDestroyVoid(ppTPI[0], pStruct);
		ParseTableFree(&ppTPI);
	}

}


void HttpServing_CommandReturn(	int iRequestID, int iClientID, CommandServingFlags eFlags, char *pReturnString)
{
	CommandReturnValue *pPreExistingStruct;


	//not sure if this ifndef is needed now that xmlrpc is in httplib
#ifndef _XBOX
	if (strStartsWith(pReturnString, "<?xml"))
	{	//push the result to the link
		PendingXMLRPCRequest *req = NULL;
		if (!stashIntRemovePointer(sXMLRPCReturnLinks, iRequestID, &req))
		{
			Errorf("No return link found for returning remote XMLRPC request. Link may have timed out.");
			return;
		}
		
		//log the event
		HttpServingLog("xmlrpc remote", false, false, timerCpuTicks64() - req->iRequestTimeClockTicks);

		if (linkConnected(req->pLink) && !linkDisconnected(req->pLink))
		{
			httpSendStr(req->pLink, pReturnString);
		}
		StructDestroy(parse_PendingXMLRPCRequest, req);
	}
	else
#endif
	{
		if (eFlags & CMDSRV_NON_CACHED_RETURN)
		{
			HttpClientState *pClientState = FindClientState(iClientID);

			//only json rpc commands currently supported for non-cached return
			assert(eFlags & CMDSRV_JSONRPC);

			if (pClientState)
			{
				if (pClientState->pParentLink)
				{
					Packet	*pak = pktCreateRaw(pClientState->pParentLink);
					size_t len = strlen(pReturnString);
					httpSendBasicHeader(pClientState->pParentLink, len, "application/json");
					pktSendBytesRaw(pak,pReturnString,(int)len);
					pktSendRaw(&pak);
				}
			}
		}
		else
		{
			CommandReturnValue *pReturnStruct = StructCreate(parse_CommandReturnValue);

			pReturnStruct->iCommandRequestID = iRequestID;
			pReturnStruct->iClientID = iClientID;
			pReturnStruct->iObsoletionTime = timeSecondsSince2000() + COMMAND_RETURN_OBSOLETE_TIME;

			//wrapping the result in html tags should be done by the request handler serving the html; Not this function!
			//estrConcatf(&pReturnStruct->pResultString, "<html><body>\n%s\n</body></html>", pReturnString);
			estrConcatf(&pReturnStruct->pResultString, "%s", pReturnString);

			if (!sCommandReturnValues)
			{
				sCommandReturnValues = stashTableCreateInt(16);
			}

			if (stashIntFindPointer(sCommandReturnValues, iRequestID, &pPreExistingStruct))
			{
				Errorf("HTTP Serving got duplicate command return values... how?");
				StructDestroy(parse_CommandReturnValue, pPreExistingStruct);
				stashIntRemovePointer(sCommandReturnValues, iRequestID, NULL);
			}

			stashIntAddPointer(sCommandReturnValues, iRequestID, pReturnStruct, true);
		}
	}
}


void PeriodicCleanup(void)
{
	S64 iCurTime = timeMsecsSince2000();

	U32 iCurTimeSecs = timeSecondsSince2000();

	PERFINFO_AUTO_START_FUNC();

	if (sCommandReturnValues)
	{

		StashTableIterator stashIterator;
		StashElement element;
		stashGetIterator(sCommandReturnValues, &stashIterator);

		while (stashGetNextElement(&stashIterator, &element))
		{
			CommandReturnValue *pRetVal = stashElementGetPointer(element);

			if (pRetVal->iObsoletionTime < iCurTimeSecs)
			{
				stashIntRemovePointer(sCommandReturnValues, stashElementGetIntKey(element), NULL);
				StructDestroy(parse_CommandReturnValue, pRetVal);
			}
		}
	}


	if (sCachedXPathTable)
	{
		StashTableIterator stashIterator;
		StashElement element;
		stashGetIterator(sCachedXPathTable, &stashIterator);

		while (stashGetNextElement(&stashIterator, &element))
		{
			CachedXPath *pCachedXPath = stashElementGetPointer(element);

			if (pCachedXPath->iObsoletionTime < iCurTime)
			{
				CachedXPath *pOtherXPath = NULL;

				stashRemovePointer(sCachedXPathTable, stashElementGetStringKey(element), &pOtherXPath);

				assert(pOtherXPath == pCachedXPath);

				DestroyCachedXPath(pCachedXPath);
			}
		}
	}

	if (cachedJpegsByNameAndArgs)
	{
		StashTableIterator stashIterator;
		StashElement element;
		stashGetIterator(cachedJpegsByNameAndArgs, &stashIterator);

		while (stashGetNextElement(&stashIterator, &element))
		{
			CachedJpeg *pCachedJPEG = stashElementGetPointer(element);

			if (pCachedJPEG->iExpirationTime && pCachedJPEG->iExpirationTime < iCurTime)
			{
				stashRemovePointer(cachedJpegsByNameAndArgs, stashElementGetStringKey(element), NULL);

				free(pCachedJPEG->pData);
				free(pCachedJPEG);
			}
		}
	}
	PERFINFO_AUTO_STOP();
}




NetLink *pLinkToSentryServer = NULL;

AUTO_COMMAND;
void SystemCommandOnBrowsingMachine(char *pIP, ACMD_SENTENCE pCmdString)
{
#ifndef _XBOX
	Packet *pPak;

	if (strcmp(pIP, "127.0.0.1") == 0)
	{
		system_detach(pCmdString, 0, 0);

		return;
	}

	if (!pLinkToSentryServer)
	{
		if (!(pLinkToSentryServer = commConnectWait(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,gSentryServerName,SENTRYSERVERMONITOR_PORT,0,0,0,0,3)))
		{
			return;
		}
	}

	pPak = pktCreate(pLinkToSentryServer, MONITORCLIENT_LAUNCH);
	pktSendString(pPak, pIP);
	pktSendString(pPak, pCmdString);
	pktSend(&pPak);
#endif
}


static void DoFileRequestDataSending(PendingFileRequest *pRequest)
{
	if (pRequest->iTotalFileSize == 0)
	{
		return;
	}

	if(linkSendBufWasFull(pRequest->pLink))
	{
		linkClearSendBufWasFull(pRequest->pLink);
		return;
	}

	while (pRequest->iReadHead < pRequest->iWriteHead && !linkSendBufWasFull(pRequest->pLink))
	{
		int iBytesToSend = 1024;
		Packet *pPak;

		if (pRequest->iReadHead + iBytesToSend > pRequest->iWriteHead)
		{
			iBytesToSend = pRequest->iWriteHead - pRequest->iReadHead;
		}
		
		pPak = pktCreateRaw(pRequest->pLink);
		pktSendBytesRaw(pPak, pRequest->pPendingBuffer + pRequest->iReadHead, iBytesToSend);
		pktSendRaw(&pPak);
		pRequest->iBytesSent += iBytesToSend;
		pRequest->iReadHead += iBytesToSend;
	}

	//check if we are completely done
	if (pRequest->iBytesSent == pRequest->iTotalFileSize)
	{
		return;
	}

	//now we've sent all we can send for now. See if we need to request more data
	if (pRequest->iReadHead == pRequest->iWriteHead 
		&& pRequest->iReadHead == HTTPSERVING_PER_FILE_BUFFER_SIZE)
	{

		pRequest->iReadHead = pRequest->iWriteHead = 0;
		spFileCB(pRequest->pFileName, pRequest->iRequestID, FILESERVING_PUMP,
				HTTPSERVING_PER_FILE_BUFFER_SIZE, HttpServing_FileFulfill);
	}
}

PendingFileRequest *FindFileRequestFromID(int iRequestID)
{
	int i;

	for (i=0; i < eaSize(&sppPendingFileRequests); i++)
	{
		if (sppPendingFileRequests[i]->iRequestID == iRequestID)
		{
			return sppPendingFileRequests[i];
		}
	}

	return NULL;
}

void HttpServing_FileFulfill( int iRequestID, char *pErrorString,
	U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, U8 *pCurData)
{
	PendingFileRequest *pRequest = FindFileRequestFromID(iRequestID);

	if (!pRequest)
	{
		return;
	}

	if (pErrorString)
	{
		pRequest->pErrorString = strdup(pErrorString);
		return;
	}

	if (pRequest->iTotalFileSize && pRequest->iTotalFileSize != iTotalSize)
	{
		pRequest->pErrorString = strdup("File Size mismatch");
		return;
	}

	if (pRequest->iWriteHead + iCurNumBytes > HTTPSERVING_PER_FILE_BUFFER_SIZE)
	{
		pRequest->pErrorString = strdup("Too much data received... something was corrupted");
		return;
	}


	pRequest->iUpdatesReceived++;
	pRequest->iTotalFileSize = iTotalSize;

	memcpy(pRequest->pPendingBuffer + pRequest->iWriteHead, pCurData, iCurNumBytes);

	free(pCurData);

	pRequest->iWriteHead += iCurNumBytes;
}



static void CleanupFileRequest(int iIndex)
{
	PendingFileRequest *pRequest = sppPendingFileRequests[iIndex];
	SAFE_FREE(pRequest->pPendingBuffer);
	SAFE_FREE(pRequest->pFileName);
	SAFE_FREE(pRequest->pErrorString);

	free(pRequest);
	eaRemoveFast(&sppPendingFileRequests, iIndex);
}



static void UpdateHttpFileServing(void)
{
	int iReqNum;

	PERFINFO_AUTO_START_FUNC();

	for (iReqNum=eaSize(&sppPendingFileRequests) - 1; iReqNum >= 0; iReqNum--)
	{
		PendingFileRequest *pRequest = sppPendingFileRequests[iReqNum];

		if (pRequest->pErrorString)
		{
			httpSendFileNotFoundError(pRequest->pLink, pRequest->pErrorString);
			spFileCB(pRequest->pFileName, pRequest->iRequestID, FILESERVING_CANCEL,
				0, HttpServing_FileFulfill);
			CleanupFileRequest(iReqNum);
			continue;
		}


		if (!pRequest->bFirstRequestSent)
		{
			pRequest->pPendingBuffer = malloc(HTTPSERVING_PER_FILE_BUFFER_SIZE);
			spFileCB(pRequest->pFileName, pRequest->iRequestID, FILESERVING_BEGIN,
				HTTPSERVING_PER_FILE_BUFFER_SIZE, HttpServing_FileFulfill);
			pRequest->bFirstRequestSent = true;
			continue;
		}

		if (!pRequest->iUpdatesReceived)
		{
			continue;
		}

		if (!pRequest->bHeaderSent)
		{
			httpSendBasicHeader(pRequest->pLink, pRequest->iTotalFileSize, httpChooseSensibleContentTypeForFileName(pRequest->pFileName));
			pRequest->bHeaderSent = true;
		}

		DoFileRequestDataSending(pRequest);

		if (pRequest->iBytesSent == pRequest->iTotalFileSize)
		{
			CleanupFileRequest(iReqNum);
		}
	}
	PERFINFO_AUTO_STOP();
}

static NetComm *spHttpComm = NULL;

bool HttpServing_Begin_MultiplePorts(U32 **ppPortNums, HttpServingXPathCallback *pXPathCB, HttpServingCommandCallback *pCommandCB,
	HttpServingCanServeCB *pCanServeCB, HttpServingJpegCallback *pJpegCB, FileServingCommandCallBack *pFileCB,
	char *pDefaultRedirectString, char *pHeaderFileName, char *pFooterFileName,
	char *pStaticHomeDirName, const char *authProductName, const char *commandCategoryFilter)
{
	int iSize;
	int i;
	const char *headerName;
	
	if (spHttpComm)
	{
		return false;
	}

	if (!ea32Size(ppPortNums))
	{
		return false;
	}

	if(authProductName)
	{

		for (i=0; i < ea32Size(ppPortNums); i++)
		{
			httpEnableAuthentication((*ppPortNums)[i], authProductName, commandCategoryFilter);
		}
	}

	assertmsg(pXPathCB && pCommandCB && pDefaultRedirectString, "Invalid arguments to HttpServing_Begin");

	spXPathCB = pXPathCB;
	spCommandCB = pCommandCB;
	spCanServeCB = pCanServeCB;
	spJpegCB = pJpegCB;
	spFileCB = pFileCB;
	spDefaultRedirectString = strdup(pDefaultRedirectString);
	
	headerName = pHeaderFileName ? pHeaderFileName : "server/MCP/templates/mcpHtmlHeader.txt";
	spHTMLHeader = fileAlloc(headerName, &iSize);
	assertmsgf(spHTMLHeader, "HTTP serving templates missing (%s); probably missing server data", headerName);
	spHTMLFooter = fileAlloc(pFooterFileName ? pFooterFileName : "server/MCP/templates/mcpHtmlFooter.txt", &iSize);
	assert(spHTMLFooter);

	//TODO: validate pStaticHomeDirName and check that the directory exists
	spHTMLStaticHome = pStaticHomeDirName;
	

	spHttpComm = commCreate(0,1);

	for (i=0; i < ea32Size(ppPortNums); i++)
	{
		if (!(spHttpListen = commListen(spHttpComm,LINKTYPE_HTTP_SERVER, LINK_HTTP, (*ppPortNums)[i],HttpServingMsg,HttpServingConnect,HttpServingDisconnect,sizeof(HttpClientState))))
		{
			commDestroy(&spHttpComm);
			free(spHTMLHeader);
			free(spHTMLFooter);

			return false;
		}
	}

	return true;



	
}

void HttpServing_Tick(void)
{
	if (spHttpComm)
	{
		static U32 iLastCleanupTime = 0;
		static U32 iNextFPSTime = 0;
		static U32 iLastStatsUpdateTime = 0;
		static int iFrameCount;
		
		U32 iCurTime = timeSecondsSince2000();

		PERFINFO_AUTO_START_FUNC();

		ProcessXPathRequests();
		httpProcessAuthentications();

		httpCleanupXMLRPCRequests();

		commMonitor(spHttpComm);


		if (iCurTime- iLastCleanupTime > 60)
		{
			PeriodicCleanup();
			iLastCleanupTime = iCurTime;
		}

		if (gbLogServerMonitor)
		{
			if (!iNextFPSTime)
			{
				iNextFPSTime = iCurTime + 10;
			}
			else
			{
				if (iCurTime > iNextFPSTime)
				{
					objLog(LOG_SERVERMONITOR, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, "mcpfps", NULL, "fps %d", iFrameCount / 10);
					iNextFPSTime = iCurTime + 10;
					iFrameCount = 0;
				}
				else
				{
					iFrameCount++;
				}
			}
		}

		updateLinkStates();

		UpdateHttpFileServing();


		if (iLastStatsUpdateTime != iCurTime)
		{
			iLastStatsUpdateTime = iCurTime;
			HttpStats_Update(iCurTime);
		}

		PERFINFO_AUTO_STOP();
	}
}

static U32 **sppIPList; 

void GetAllConnectedCB(NetLink* link,void *user_data)
{
	if (linkConnected(link) && !linkDisconnected(link))
	{
		ea32Push(sppIPList, linkGetIp(link));
	}
}

static int intRevCompare(const int *i1, const int *i2){
	if(*i1 < *i2)
		return 1;

	if(*i1 == *i2)
		return 0;

	return -1;
}

void DEFAULT_LATELINK_AddCustomHTMLFooter(char **ppOutString)
{
}




#include "autogen/HttpServing_c_ast.c"
