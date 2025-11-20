#include "WebInterface/wiCommon.h"
#include "WebInterface/wiCommonInternal.h"
#include "HttpLib.h"
#include "net.h"
#include "httputil.h"
#include "timing.h"
#include "EString.h"
#include "StringUtil.h"
#include "file.h"
#include "utilitiesLib.h"
#include "clearsilver.h"
#include "wininclude.h"
#include "logging.h"
#include "crypt.h"
#include "GlobalTypes.h"

#include "AutoGen/wiCommonInternal_h_ast.h"

// Requires the following template files to work: WebSite.cs, MessageBox.cs

char gWebInterfaceDirectory[MAX_PATH] = "";
AUTO_CMD_STRING(gWebInterfaceDirectory, WebInterfaceDirectory) ACMD_CMDLINE;

char gWebInterfaceTemplates[MAX_PATH] = "";
AUTO_CMD_STRING(gWebInterfaceTemplates, WebInterfaceTemplates) ACMD_CMDLINE;

static char wiVersion[64] = "";
static WICommonSettings sWebSettings = {0};

void wiCommonInitDefaultDirectories(const char *interfaceDir, const char *templateDir)
{
	if (gWebInterfaceDirectory[0] == 0)
		strcpy(gWebInterfaceDirectory, interfaceDir);
	if (gWebInterfaceTemplates[0] == 0)
		strcpy(gWebInterfaceTemplates, templateDir);
}

WIAccessLevel wiGetAccessLevel(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	return pWebRequest->eAccessLevel;
}

void wiSetResult(SA_PARAM_NN_VALID WICWebRequest *pWebRequest, WIResult eResult)
{
	pWebRequest->eResult = eResult;
}

HttpRequest *wiGetHttpRequest(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	return pWebRequest->pReq;
}

SA_RET_OP_STR const char *wiGetHeader(SA_PARAM_NN_VALID WICWebRequest *pWebRequest, SA_PARAM_NN_STR const char *pHeader)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pHeader)) return NULL;

	return hrFindSafeHeader(pWebRequest->pReq, pHeader);
}

SA_RET_OP_STR const char *wiGetPath(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;

	return pWebRequest->pReq->path;
}

HttpMethod wiGetMethod(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return HTTPMETHOD_UNKNOWN;
	if (!verify(pWebRequest->pReq)) return HTTPMETHOD_UNKNOWN;

	return pWebRequest->pReq->method;
}

char *wiGetEscapedString(WICWebRequest *pWebRequest, const char *pArg)
{
	char *pEscapedStr = NULL;

	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;

	estrCopyWithHTMLEscaping(&pEscapedStr, hrFindSafeValue(pWebRequest->pReq, pArg), false);
	return pEscapedStr;
}

SA_RET_OP_STR const char *wiGetString(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
									 SA_PARAM_NN_STR const char *pArg)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;

	return hrFindSafeValue(pWebRequest->pReq, pArg);
}

bool wiGetBool(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
			   SA_PARAM_NN_STR const char *pArg)
{
	if (!verify(pWebRequest)) return false;
	if (!verify(pWebRequest->pReq)) return false;

	return hrFindBool(pWebRequest->pReq, pArg);
}

int wiGetInt(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
			 SA_PARAM_NN_STR const char *pArg,
			 int iFailValue)
{
	if (!verify(pWebRequest)) return false;
	if (!verify(pWebRequest->pReq)) return false;

	return hrFindInt(pWebRequest->pReq, pArg, iFailValue);
}

bool wiValuePresent(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
					SA_PARAM_NN_STR const char *pArg)
{
	if (!verify(pWebRequest)) return false;
	if (!verify(pWebRequest->pReq)) return false;

	return hrFindValue(pWebRequest->pReq, pArg) != NULL;
}

SA_RET_OP_VALID void * wiGetSessionStruct(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
									   SA_PARAM_NN_STR const char *pKey,
									   SA_PARAM_NN_VALID ParseTable *pti)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;
	if (!verify(pKey && *pKey)) return NULL;
	if (!verify(pti)) return NULL;

	return hrGetSessionStruct(pWebRequest->pReq, pKey, pti);
}

SA_RET_NN_STR const char *wiGetUsername(SA_PARAM_NN_VALID WICWebRequest *pWebRequest)
{
	const char *pUsername = NULL;

	if (!verify(pWebRequest)) return "";
	if (!verify(pWebRequest->pReq)) return "";

	pUsername = hrFindAuthUsername(pWebRequest->pReq);

	return pUsername ? pUsername : "";
}

void wiSetAttachmentMode(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
						 SA_PARAM_NN_STR const char *pFileName)
{
	if (!verify(pWebRequest)) return;
	if (!verify(pFileName)) return;

	PERFINFO_AUTO_START_FUNC();

	pWebRequest->eResult = WIR_Attachment;
	pWebRequest->pRawFileName = estrDup(pFileName);

	PERFINFO_AUTO_STOP_FUNC();
}

#undef wiAppendStringf
void wiAppendStringf(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
					 FORMAT_STR const char *pFormat, ...)
{
	if (!verify(pWebRequest)) return;
	if (!verify(pFormat)) return;

	PERFINFO_AUTO_START_FUNC();

	VA_START(args, pFormat);
	{
		estrConcatfv(pWebRequest->pOutput, pFormat, args);
	}
	VA_END();

	if (pWebRequest->eResult == WIR_Invalid)
	{
		pWebRequest->eResult = WIR_Success;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void wiAppendFile(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
				  SA_PARAM_NN_STR const char *pFileName)
{
	FILE *file = NULL;
	char *pFullFileName = NULL;

	if (!verify(pWebRequest)) return;
	if (!verify(pFileName)) return;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pFullFileName);
	estrPrintf(&pFullFileName, "%s%s", gWebInterfaceDirectory, pFileName);

	file = fileOpen(pFullFileName, "rb");

	if (devassertmsgf(file, "Could not open %s for reading.", pFullFileName))
	{
		char buff[255];

		memset(buff, 0, ARRAY_SIZE(buff));

		while (fread(buff, sizeof(char), ARRAY_SIZE(buff) - 1, file) > 0)
		{
			wiAppendStringf(pWebRequest, "%s", buff);
			memset(buff, 0, ARRAY_SIZE(buff));
		}

		fclose(file);
	}

	estrDestroy(&pFullFileName);

	PERFINFO_AUTO_STOP_FUNC();
}

void wiAppendStruct(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
					SA_PARAM_NN_STR const char *pTemplate,
					SA_PARAM_NN_VALID ParseTable *tpi,
					SA_PARAM_NN_VALID void *pStruct)
{
	char *pRendered = NULL;

	if (!verify(pWebRequest)) return;
	if (!verify(pTemplate)) return;
	if (!verify(tpi)) return;
	if (!verify(pStruct)) return;

	PERFINFO_AUTO_START_FUNC();

	pRendered = renderTemplateEx(pTemplate, tpi, pStruct, pWebRequest->bDumpHDF, NULL);
	wiAppendStringf(pWebRequest, "%s", pRendered);
	estrDestroy(&pRendered);

	PERFINFO_AUTO_STOP_FUNC();
}

void wiCommonRedirectf(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
				 FORMAT_STR const char *pFormat, ...)
{
	char *pFullURL = NULL;
	va_list ap;

	va_start(ap, pFormat);
		estrConcatfv(&pFullURL, pFormat, ap);
	va_end(ap);

	if (pFullURL[0] == '/' && estrLength(&pFullURL) > 1)
	{
		httpRedirect(pWebRequest->pReq->link, pFullURL + 1);
	}
	else
	{
		httpRedirect(pWebRequest->pReq->link, pFullURL);
	}

	wiSetResult(pWebRequest, WIR_Redirect);

	estrDestroy(&pFullURL);
}

static void wiLogRequest(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
						 U32 uTimeMS)
{
	WICWebRequestData data = {0};
	
	char *pLogText = NULL;
	const char *pUsername = NULL;
	const char *pReferer = NULL;

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pLogText);

	StructInit(parse_WICWebRequestData, &data);
	data.eaVars = hrGetAllVariables(pWebRequest->pReq);

	estrConcatf(&pLogText, "HTTP WI %s (time: %u",
		StaticDefineIntRevLookupNonNull(HttpMethodEnum, wiGetMethod(pWebRequest)),
		uTimeMS);

	pUsername = wiGetUsername(pWebRequest);

	if (pUsername && *pUsername)
	{
		estrConcatf(&pLogText, ", user: %s):", pUsername);
	}
	else
	{
		estrConcatf(&pLogText, ", unknown user):");
	}

	estrConcatf(&pLogText, " %s %s",
		StaticDefineIntRevLookupNonNull(WIResultEnum, pWebRequest->eResult), pWebRequest->pReq->path);

	pReferer = wiGetHeader(pWebRequest, "Referer");

	if (pReferer && *pReferer)
	{
		estrConcatf(&pLogText, " (ref: %s)", pReferer);
	}
	else
	{
		estrConcatf(&pLogText, " (unknown ref)");
	}

	if (data.eaVars)
	{
		char *pDataText = NULL;

		estrStackCreate(&pDataText);

		ParserWriteText(&pDataText, parse_WICWebRequestData, &data, 0, 0, 0);

		estrConcatf(&pLogText, " (vars: %s)", pDataText);

		estrDestroy(&pDataText);
	}

	StructDeInit(parse_WICWebRequestData, &data);

	log_printf(LOG_ACCOUNT_SERVER_WEB, "%s", pLogText);

	estrDestroy(&pLogText);

	PERFINFO_AUTO_STOP_FUNC();
}

static WIAccessLevel wiCommonDefaultALvlCB(int iAccessLevel)
{
	if (iAccessLevel < 0)
		return WIAL_Invalid;
	if (iAccessLevel <  WIAL_Normal)
		return WIAL_Limited;
	if (iAccessLevel <  WIAL_Admin)
		return WIAL_Normal;
	if (iAccessLevel <  WIAL_SuperAdmin)
		return WIAL_Admin;
	return WIAL_SuperAdmin;
}

static void wiHttpHandle(SA_PARAM_NN_VALID HttpRequest *pReq,
									UserData pUserdata)
{
	static WICommonWebSite webSite = {0};
	WICWebRequest webRequest = {0};
	U32 uStart = 0;

	if (!verify(pReq)) return;

	PERFINFO_AUTO_START_FUNC();

	uStart = timeGetTime();

	if (!webSite.pInstance || !webSite.pVersion)
	{
		webSite.pExtension = WI_EXTENSION;

		webSite.pVersion = wiVersion;
		webSite.pBuildVersion = GetUsefulVersionString();
		estrPrintf(&webSite.pInstance, "%s", GetShardNameFromShardInfoString());
	}

	if (!devassert(webSite.pInstance && webSite.pVersion))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	StructInit(parse_WICWebRequest, &webRequest);

	webRequest.iAccessLevel = httpFindAuthenticationAccessLevel(pReq->link, hrFindAuthString(pReq) ? cryptPasswordHashString(hrFindAuthString(pReq)) : 0, NULL);

	if (sWebSettings.alvlCB)
		webRequest.eAccessLevel = sWebSettings.alvlCB(webRequest.iAccessLevel);
	else
		webRequest.eAccessLevel = wiCommonDefaultALvlCB(webRequest.iAccessLevel);
	webSite.pAccess = StaticDefineIntRevLookupNonNull(WIAccessLevelEnum, webRequest.eAccessLevel);

	estrClear(&webSite.pContent);

	webRequest.pOutput = &webSite.pContent;
	webRequest.pReq = pReq;

	if (webRequest.eAccessLevel == WIAL_Invalid)
	{
		webRequest.eResult = WIR_AllForbidden;
	}
	else
	{
		bool bHandled = false;

		webRequest.bDumpHDF = hrFindBool(pReq, "dumphdf");

		bHandled = sWebSettings.httpCB(&webRequest);

		// If we haven't handled it yet, see if it is a file request.
		if (!bHandled)
		{
			char *pFileToSend = NULL;

			estrPrintf(&pFileToSend, "%s%s", gWebInterfaceDirectory, pReq->path);

			if (fileExists(pFileToSend))
			{
				bHandled = true;
				webRequest.eResult = WIR_RawFile;
				webRequest.pRawFileName = pFileToSend;
			}
			else
			{
				estrDestroy(&pFileToSend);
			}
		}

		// If we still haven't handled it, it's a 404
		if (!bHandled)
		{
			webRequest.eResult = WIR_NotFound;
		}
	}

	switch (webRequest.eResult)
	{
		xcase WIR_AllForbidden:
		{
			char *errormsg = NULL;
			estrStackCreate(&errormsg);
			estrPrintf(&errormsg, "403: Forbidden.  You do not have access to the %s web interface.", 
				GlobalTypeToName(GetAppGlobalType()));
			httpSendPermissionDeniedError(pReq->link, errormsg);
			estrDestroy(&errormsg);
		}
		
		xcase WIR_RawFile:
			httpSendFile(pReq->link, webRequest.pRawFileName, httpChooseSensibleContentTypeForFileName(webRequest.pRawFileName));
	
		xcase WIR_Attachment:
			httpSendAttachment(pReq->link, webRequest.pRawFileName, webSite.pContent, estrLength(&webSite.pContent));

		xcase WIR_Legacy:
			// Do nothing; it's already handled

		xcase WIR_Forbidden:
			// Bleed through
		case WIR_NotFound:
			// Bleed through
		case WIR_Success:
			// Bleed through
		default:
			if (devassertmsg(webRequest.eResult != WIR_Invalid, "HTTP request not handled!"))
			{
				char *pRendered = NULL;
				bool bErrorPassThrough = true;
				
				switch (webRequest.eResult)
				{
					xcase WIR_Forbidden:
						if (sWebSettings.errorCB)
							bErrorPassThrough = sWebSettings.errorCB(&webRequest, WIR_Forbidden);
						else
						{
							httpSendPermissionDeniedError(webRequest.pReq->link, "");
							bErrorPassThrough = false;
						}
					xcase WIR_NotFound:
						if (sWebSettings.errorCB)
							bErrorPassThrough = sWebSettings.errorCB(&webRequest, WIR_NotFound);
						else
						{					
							httpSendFileNotFoundError(webRequest.pReq->link, "");
							bErrorPassThrough = false;
						}
				}

				if (!bErrorPassThrough)
					break;
				
				if (devassert(sWebSettings.baseSiteTemplate))
					pRendered = renderTemplateEx(sWebSettings.baseSiteTemplate, parse_WICommonWebSite, &webSite, webRequest.bDumpHDF, NULL);

				if (devassert(pRendered))
				{
					if (webRequest.bDumpHDF)
					{
						char *pOutput = NULL;

						estrStackCreate(&pOutput);

						estrPrintf(&pOutput, "<html><head><title>HDF Dump</title><body><pre>%s</pre></body></html>", pRendered);

						httpSendStr(pReq->link, pOutput);

						estrDestroy(&pOutput);
					}
					else
					{
						switch (webRequest.eResult)
						{
							xcase WIR_Forbidden:
								httpSendPermissionDeniedError(pReq->link, pRendered);
							xcase WIR_NotFound:
								httpSendFileNotFoundError(pReq->link, pRendered);
							xcase WIR_Success:
								httpSendStr(pReq->link, pRendered);
						}	
					}
					
					estrDestroy(&pRendered);
				}
			}
	}

	wiLogRequest(&webRequest, timeGetTime() - uStart);
	StructDeInit(parse_WICWebRequest, &webRequest);

	PERFINFO_AUTO_STOP_FUNC();
}

void wiCommonHttpInit(unsigned int port, const char *productAuthName, const char *version, WICommonSettings *settings)
{
	NetListen *pListen = NULL;

	if (!port) return;

	PERFINFO_AUTO_START_FUNC();

	strcpy(wiVersion, version);
	hrSetHandler(wiHttpHandle, NULL);
	sWebSettings.httpCB = settings->httpCB;
	sWebSettings.alvlCB = settings->alvlCB;
	sWebSettings.errorCB = settings->errorCB;
	sWebSettings.baseSiteTemplate = StructAllocString(settings->baseSiteTemplate);

	pListen = commListen(commDefault(), LINKTYPE_UNSPEC, LINK_HTTP, port, httpDefaultMsgHandler, httpDefaultConnectHandler, httpDefaultDisconnectHandler, sizeof(HttpClientStateDefault));
	
	devassertmsgf(pListen, "Warning: Unable to bind to port %u. Not initializing web interface.", port);

	csSetCustomLoadPath(gWebInterfaceTemplates);

	httpEnableAuthentication(port, productAuthName, DEFAULT_HTTP_CATEGORY_FILTER);

	PERFINFO_AUTO_STOP_FUNC();
}

#include "AutoGen/wiCommon_h_ast.c"
#include "AutoGen/wiCommonInternal_h_ast.c"