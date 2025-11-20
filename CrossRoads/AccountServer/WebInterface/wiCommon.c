#include "wiCommon.h"
#include "HttpLib.h"
#include "net.h"
#include "httputil.h"
#include "timing.h"
#include "EString.h"
#include "StringUtil.h"
#include "wiCommon_c_ast.h"
#include "wiAccounts.h"
#include "wiProducts.h"
#include "wiProductKeys.h"
#include "wiSubscriptions.h"
#include "wiAdmin.h"
#include "file.h"
#include "AccountServer.h"
#include "utilitiesLib.h"
#include "clearsilver.h"
#include "WebInterface.h"
#include "wininclude.h"
#include "logging.h"
#include "billing.h"
#include "crypt.h"

char gWebInterfaceDirectory[MAX_PATH] = "server/AccountServer/WebRoot/";
AUTO_CMD_STRING(gWebInterfaceDirectory, WebInterfaceDirectory) ACMD_CMDLINE;

char gWebInterfaceTemplates[MAX_PATH] = "server/AccountServer/templates/";
AUTO_CMD_STRING(gWebInterfaceTemplates, WebInterfaceTemplates) ACMD_CMDLINE;

AUTO_ENUM;
typedef enum ASWIResult
{
	ASWIR_Invalid = 0,
	ASWIR_Success,
	ASWIR_NotFound,
	ASWIR_Forbidden,
	ASWIR_AllForbidden,
	ASWIR_RawFile,
	ASWIR_Legacy,
	ASWIR_Attachment,
} ASWIResult;

AUTO_STRUCT;
typedef struct ASWebRequest
{
	HttpRequest *pReq;		NO_AST
	char **pOutput;			NO_AST
	AccountServerAccessLevel eAccessLevel;
	char *pRawFileName; AST(ESTRING)
	ASWIResult eResult;
	bool bDumpHDF : 1;
} ASWebRequest;

SA_RET_OP_STR const char *wiGetHeader(SA_PARAM_NN_VALID ASWebRequest *pWebRequest, SA_PARAM_NN_STR const char *pHeader)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pHeader)) return NULL;

	return hrFindSafeHeader(pWebRequest->pReq, pHeader);
}

SA_RET_OP_STR const char *wiGetPath(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;

	return pWebRequest->pReq->path;
}

HttpMethod wiGetMethod(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return HTTPMETHOD_UNKNOWN;
	if (!verify(pWebRequest->pReq)) return HTTPMETHOD_UNKNOWN;

	return pWebRequest->pReq->method;
}

char *wiGetEscapedString(ASWebRequest *pWebRequest, const char *pArg)
{
	char *pEscapedStr = NULL;

	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;

	estrCopyWithHTMLEscaping(&pEscapedStr, hrFindSafeValue(pWebRequest->pReq, pArg), false);
	return pEscapedStr;
}

SA_RET_OP_STR const char *wiGetString(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
									 SA_PARAM_NN_STR const char *pArg)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;

	return hrFindSafeValue(pWebRequest->pReq, pArg);
}

bool wiGetBool(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
			   SA_PARAM_NN_STR const char *pArg)
{
	if (!verify(pWebRequest)) return false;
	if (!verify(pWebRequest->pReq)) return false;

	return hrFindBool(pWebRequest->pReq, pArg);
}

int wiGetInt(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
			 SA_PARAM_NN_STR const char *pArg,
			 int iFailValue)
{
	if (!verify(pWebRequest)) return false;
	if (!verify(pWebRequest->pReq)) return false;

	return hrFindInt(pWebRequest->pReq, pArg, iFailValue);
}

bool wiValuePresent(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
					SA_PARAM_NN_STR const char *pArg)
{
	if (!verify(pWebRequest)) return false;
	if (!verify(pWebRequest->pReq)) return false;

	return hrFindValue(pWebRequest->pReq, pArg) != NULL;
}

SA_RET_OP_VALID void * wiGetSessionStruct(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
									   SA_PARAM_NN_STR const char *pKey,
									   SA_PARAM_NN_VALID ParseTable *pti)
{
	if (!verify(pWebRequest)) return NULL;
	if (!verify(pWebRequest->pReq)) return NULL;
	if (!verify(pKey && *pKey)) return NULL;
	if (!verify(pti)) return NULL;

	return hrGetSessionStruct(pWebRequest->pReq, pKey, pti);
}

SA_RET_NN_STR const char *wiGetUsername(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	const char *pUsername = NULL;

	if (!verify(pWebRequest)) return "";
	if (!verify(pWebRequest->pReq)) return "";

	pUsername = hrFindAuthUsername(pWebRequest->pReq);

	return pUsername ? pUsername : "";
}

void wiSetAttachmentMode(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
						 SA_PARAM_NN_STR const char *pFileName)
{
	if (!verify(pWebRequest)) return;
	if (!verify(pFileName)) return;

	PERFINFO_AUTO_START_FUNC();

	pWebRequest->eResult = ASWIR_Attachment;
	pWebRequest->pRawFileName = estrDup(pFileName);

	PERFINFO_AUTO_STOP_FUNC();
}

#undef wiAppendStringf
void wiAppendStringf(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
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

	if (pWebRequest->eResult == ASWIR_Invalid)
	{
		pWebRequest->eResult = ASWIR_Success;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

void wiAppendFile(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
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

void wiAppendStruct(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
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

AUTO_STRUCT;
typedef struct ASWIMessageBox
{
	const char *pSubject; AST(UNOWNED)
	const char *pMessage; AST(UNOWNED)
	const char *pType;	  AST(UNOWNED)
	const char *pReferer; AST(UNOWNED)
} ASWIMessageBox;

void wiAppendMessageBox(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
						SA_PARAM_NN_STR const char *pSubject,
						SA_PARAM_NN_STR const char *pMessage,
						WebMessageBoxFlags options)
{
	ASWIMessageBox messageBox = {0};

	if (!verify(pWebRequest)) return;
	if (!verify(pSubject)) return;
	if (!verify(pMessage)) return;

	PERFINFO_AUTO_START_FUNC();

	StructInit(parse_ASWIMessageBox, &messageBox);

	if (options & WMBF_Error)
	{
		messageBox.pType = "error";
	}
	else
	{
		messageBox.pType = "highlight";
	}

	messageBox.pSubject = pSubject;
	messageBox.pMessage = pMessage;

	if (options & WMBF_BackButton)
	{
		messageBox.pReferer = wiGetHeader(pWebRequest, "Referer");
	}

	wiAppendStruct(pWebRequest, "MessageBox.cs", parse_ASWIMessageBox, &messageBox);

	StructDeInit(parse_ASWIMessageBox, &messageBox);

	PERFINFO_AUTO_STOP_FUNC();
}

static bool wiHandleDefaultRequest(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

	wiAppendFile(pWebRequest, "index.html");

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

static bool wiHandleLegacy(SA_PARAM_NN_VALID ASWebRequest *pWebRequest)
{
	if (!verify(pWebRequest)) return false;

	PERFINFO_AUTO_START_FUNC();

	hrCallLegacyHandler(pWebRequest->pReq, httpLegacyHandlePost, httpLegacyHandleGet);
	pWebRequest->eResult = ASWIR_Legacy;

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}

AUTO_STRUCT;
typedef struct ASWebSite
{
	const char *pVersion;			AST(UNOWNED)
	const char *pBuildVersion;		AST(UNOWNED)
	char *pInstance;				AST(ESTRING)
	const char *pAccess;			AST(UNOWNED)
	char *pContent;
	const char *pExtension;			AST(UNOWNED)
} ASWebSite;

AUTO_STRUCT;
typedef struct ASWebRequestData
{
	EARRAY_OF(HttpVars) eaVars;
} ASWebRequestData;

static void wiLogRequest(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
						 U32 uTimeMS)
{
	ASWebRequestData data = {0};
	
	char *pLogText = NULL;
	const char *pUsername = NULL;
	const char *pReferer = NULL;

	if (!verify(pWebRequest)) return;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&pLogText);

	StructInit(parse_ASWebRequestData, &data);
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
		StaticDefineIntRevLookupNonNull(ASWIResultEnum, pWebRequest->eResult), pWebRequest->pReq->path);

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

		ParserWriteText(&pDataText, parse_ASWebRequestData, &data, 0, 0, 0);

		estrConcatf(&pLogText, " (vars: %s)", pDataText);

		estrDestroy(&pDataText);
	}

	StructDeInit(parse_ASWebRequestData, &data);

	log_printf(LOG_ACCOUNT_SERVER_WEB, "%s", pLogText);

	estrDestroy(&pLogText);

	PERFINFO_AUTO_STOP_FUNC();
}

static void accountServerHttpHandle(SA_PARAM_NN_VALID HttpRequest *pReq,
									UserData pUserdata)
{
	static ASWebSite webSite = {0};
	ASWebRequest webRequest = {0};
	AccountServerAccessLevel eLevel = ASAL_Invalid;
	int iAccessLevel = -1;
	U32 uStart = 0;

	if (!verify(pReq)) return;

	PERFINFO_AUTO_START_FUNC();

	uStart = timeGetTime();

	if (!webSite.pInstance || !webSite.pVersion)
	{
		webSite.pExtension = WI_EXTENSION;

		webSite.pVersion = ACCOUNT_SERVER_VERSION;
		webSite.pBuildVersion = GetUsefulVersionString();
		estrPrintf(&webSite.pInstance, "%s", GetShardNameFromShardInfoString());
	}

	if (!devassert(webSite.pInstance && webSite.pVersion))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	StructInit(parse_ASWebRequest, &webRequest);

	iAccessLevel = httpFindAuthenticationAccessLevel(pReq->link, hrFindAuthString(pReq) ? cryptPasswordHashString(hrFindAuthString(pReq)) : 0, NULL);

	if (iAccessLevel > -1)
	{
		eLevel = ASGetAccessLevel(iAccessLevel);
		webSite.pAccess = StaticDefineIntRevLookupNonNull(AccountServerAccessLevelEnum, eLevel);
	}
	else
	{
		eLevel = ASAL_Invalid;
		webSite.pAccess = NULL;
	}
	webRequest.eAccessLevel = eLevel;

	estrClear(&webSite.pContent);

	webRequest.pOutput = &webSite.pContent;
	webRequest.pReq = pReq;

	if (eLevel == ASAL_Invalid)
	{
		webRequest.eResult = ASWIR_AllForbidden;
	}
	else
	{
		bool bHandled = false;

		webRequest.bDumpHDF = hrFindBool(pReq, "dumphdf");

		if (!stricmp_safe(pReq->path, "/") || !stricmp_safe(pReq->path, "/index" WI_EXTENSION))
		{
			bHandled = wiHandleDefaultRequest(&webRequest);
		}
		else if (strStartsWith(pReq->path, WI_ACCOUNTS_DIR))
		{
			bHandled = wiHandleAccounts(&webRequest);
		}
		else if (strStartsWith(pReq->path, WI_SUBSCRIPTIONS_DIR))
		{
			bHandled = wiHandleSubscriptions(&webRequest);
		}
		else if (strStartsWith(pReq->path, WI_PRODUCTS_DIR))
		{
			bHandled = wiHandleProducts(&webRequest);
		}
		else if (strStartsWith(pReq->path, WI_PRODUCTKEYS_DIR))
		{
			bHandled = wiHandleProductKeys(&webRequest);
		}
		else if (strStartsWith(pReq->path, WI_ADMIN_DIR))
		{
			bHandled = wiHandleAdmin(&webRequest);
		}
		else if (strStartsWith(pReq->path, WI_LEGACY_DIR))
		{
			bHandled = wiHandleLegacy(&webRequest);
		}

		// If we haven't handled it yet, see if it is a file request.
		if (!bHandled)
		{
			char *pFileToSend = NULL;

			estrPrintf(&pFileToSend, "%s%s", gWebInterfaceDirectory, pReq->path);

			if (fileExists(pFileToSend))
			{
				bHandled = true;
				webRequest.eResult = ASWIR_RawFile;
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
			webRequest.eResult = ASWIR_NotFound;
		}
	}

	switch (webRequest.eResult)
	{
		xcase ASWIR_AllForbidden:
			httpSendPermissionDeniedError(pReq->link, "403: Forbidden.  You do not have access to the account server web interface.");
		
		xcase ASWIR_RawFile:
			httpSendFile(pReq->link, webRequest.pRawFileName, httpChooseSensibleContentTypeForFileName(webRequest.pRawFileName));
	
		xcase ASWIR_Attachment:
			httpSendAttachment(pReq->link, webRequest.pRawFileName, webSite.pContent, estrLength(&webSite.pContent));

		xcase ASWIR_Legacy:
			// Do nothing; it's already handled

		xcase ASWIR_Forbidden:
			// Bleed through
		case ASWIR_NotFound:
			// Bleed through
		case ASWIR_Success:
			// Bleed through
		default:
			if (devassertmsg(webRequest.eResult != ASWIR_Invalid, "HTTP request not handled!"))
			{
				char *pRendered = NULL;
				
				switch (webRequest.eResult)
				{
					xcase ASWIR_Forbidden:
						wiAppendMessageBox(&webRequest, "403: Forbidden", "You do not have sufficient access to view the requested page.", WMBF_Error);
					xcase ASWIR_NotFound:
						wiAppendMessageBox(&webRequest, "404: Page Not Found", "The page you have requested could not be found.", WMBF_Error);
				}
				
				pRendered = renderTemplateEx("WebSite.cs", parse_ASWebSite, &webSite, webRequest.bDumpHDF, NULL);

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
							xcase ASWIR_Forbidden:
								httpSendPermissionDeniedError(pReq->link, pRendered);
							xcase ASWIR_NotFound:
								httpSendFileNotFoundError(pReq->link, pRendered);
							xcase ASWIR_Success:
								httpSendStr(pReq->link, pRendered);
						}	
					}
					
					estrDestroy(&pRendered);
				}
			}
	}

	wiLogRequest(&webRequest, timeGetTime() - uStart);
	StructDeInit(parse_ASWebRequest, &webRequest);

	PERFINFO_AUTO_STOP_FUNC();
}

void accountServerHttpInit(unsigned int port)
{
	NetListen *pListen = NULL;

	if (!port) return;

	PERFINFO_AUTO_START_FUNC();

	hrSetHandler(accountServerHttpHandle, NULL);

	pListen = commListen(commDefault(), LINKTYPE_UNSPEC, LINK_HTTP, port, httpDefaultMsgHandler, httpDefaultConnectHandler, httpDefaultDisconnectHandler, sizeof(HttpClientStateDefault));
	
	devassertmsgf(pListen, "Warning: Unable to bind to port %u. Not initializing web interface.", port);

	csSetCustomLoadPath(gWebInterfaceTemplates);

	httpEnableAuthentication(port, ACCOUNT_SERVER_INTERNAL_NAME, DEFAULT_HTTP_CATEGORY_FILTER);

	PERFINFO_AUTO_STOP_FUNC();
}

#include "wiCommon_h_ast.c"
#include "wiCommon_c_ast.c"