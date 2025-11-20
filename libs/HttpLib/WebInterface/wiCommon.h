#pragma once
#include "AutoGen/wiCommon_h_ast.h"

typedef struct HttpRequest HttpRequest;
typedef struct WICWebRequest WICWebRequest;
typedef enum HttpMethod HttpMethod;

#define WI_EXTENSION ".html"

// These are default access level values
AUTO_ENUM;
typedef enum WIAccessLevel
{
	WIAL_SuperAdmin		= 9,
	WIAL_Admin			= 7,
	WIAL_Normal			= 4,
	WIAL_Limited		= 0,
	WIAL_Invalid		= -1,
} WIAccessLevel;

AUTO_ENUM;
typedef enum WIResult
{
	WIR_Invalid = 0,
	WIR_Success,
	WIR_NotFound,
	WIR_Forbidden,
	WIR_AllForbidden,
	WIR_RawFile,
	WIR_Legacy,
	WIR_Attachment,
	WIR_Redirect,
} WIResult;

WIAccessLevel wiGetAccessLevel(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);
void wiSetResult(SA_PARAM_NN_VALID WICWebRequest *pWebRequest, WIResult eResult);
HttpRequest *wiGetHttpRequest(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);

SA_RET_OP_STR const char *wiGetHeader(SA_PARAM_NN_VALID WICWebRequest *pWebRequest, SA_PARAM_NN_STR const char *pHeader);

SA_RET_OP_STR const char *wiGetPath(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);

HttpMethod wiGetMethod(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);

SA_RET_OP_STR const char *wiGetString(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
									 SA_PARAM_NN_STR const char *pArg);

// Eventually this will be made to escape HTML entities
SA_RET_OP_STR char *wiGetEscapedString(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
									   SA_PARAM_NN_STR const char *pArg);

bool wiGetBool(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
			   SA_PARAM_NN_STR const char *pArg);

int wiGetInt(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
			 SA_PARAM_NN_STR const char *pArg,
			 int iFailValue);

bool wiValuePresent(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
					SA_PARAM_NN_STR const char *pArg);

#define wiSubmitted(pWebRequest, pArg) (wiGetMethod(pWebRequest) == HTTPMETHOD_POST && wiValuePresent(pWebRequest, pArg))

SA_RET_OP_VALID void * wiGetSessionStruct(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
									   SA_PARAM_NN_STR const char *pKey,
									   SA_PARAM_NN_VALID ParseTable *pti);

SA_RET_NN_STR const char *wiGetUsername(SA_PARAM_NN_VALID WICWebRequest *pWebRequest);

void wiSetAttachmentMode(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
						 SA_PARAM_NN_STR const char *pFileName);

void wiAppendStringf(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
					 FORMAT_STR const char *pFormat, ...);
#define wiAppendStringf(pWebRequest, pFormat, ...) wiAppendStringf(pWebRequest, FORMAT_STRING_CHECKED(pFormat), __VA_ARGS__)

void wiAppendFile(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
				  SA_PARAM_NN_STR const char *pFileName);

void wiAppendStruct(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
					SA_PARAM_NN_STR const char *pTemplate,
					SA_PARAM_NN_VALID ParseTable *tpi,
					SA_PARAM_NN_VALID void *pStruct);

void wiCommonRedirectf(SA_PARAM_NN_VALID WICWebRequest *pWebRequest,
				 FORMAT_STR const char *pFormat, ...);

// Template directory must be set before call to wiCommonHttpInit
void wiCommonInitDefaultDirectories(const char *interfaceDir, const char *templateDir);

typedef bool (*WICommonHandleHttpCB)(WICWebRequest *pWebRequest);
typedef WIAccessLevel (*WICommonGetALvlCB)(int iAccessLevel);
// Returns true if HTTP handler should continue to render base website, false if error handler generates entire response
typedef bool (*WICommonHandleHttpErrorCB)(WICWebRequest *pWebRequest, WIResult eResult);

typedef struct WICommonSettings
{
	WICommonHandleHttpCB httpCB;
	WICommonGetALvlCB alvlCB;
	WICommonHandleHttpErrorCB errorCB;
	const char *baseSiteTemplate;
} WICommonSettings;

void wiCommonHttpInit(unsigned int port, const char *productAuthName, const char *version, WICommonSettings *settings);