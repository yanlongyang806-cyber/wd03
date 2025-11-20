#pragma once
#include "wiCommon_h_ast.h"

typedef struct HttpRequest HttpRequest;
typedef struct ASWebRequest ASWebRequest;
typedef enum HttpMethod HttpMethod;

#define WI_EXTENSION ".html"

AUTO_ENUM;
typedef enum WebMessageBoxFlags
{
	WMBF_BackButton				= BIT(0),
	WMBF_Error					= BIT(1),

	WMBF_MAX, EIGNORE
} WebMessageBoxFlags;
#define WebMessageBoxFlags_NUMBITS 3
STATIC_ASSERT(WMBF_MAX == ((1 << (WebMessageBoxFlags_NUMBITS-2))+1));

SA_RET_OP_STR const char *wiGetHeader(SA_PARAM_NN_VALID ASWebRequest *pWebRequest, SA_PARAM_NN_STR const char *pHeader);

SA_RET_OP_STR const char *wiGetPath(SA_PARAM_NN_VALID ASWebRequest *pWebRequest);

HttpMethod wiGetMethod(SA_PARAM_NN_VALID ASWebRequest *pWebRequest);

SA_RET_OP_STR const char *wiGetString(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
									 SA_PARAM_NN_STR const char *pArg);

// Eventually this will be made to escape HTML entities
SA_RET_OP_STR char *wiGetEscapedString(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
									   SA_PARAM_NN_STR const char *pArg);

bool wiGetBool(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
			   SA_PARAM_NN_STR const char *pArg);

int wiGetInt(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
			 SA_PARAM_NN_STR const char *pArg,
			 int iFailValue);

bool wiValuePresent(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
					SA_PARAM_NN_STR const char *pArg);

#define wiSubmitted(pWebRequest, pArg) (wiGetMethod(pWebRequest) == HTTPMETHOD_POST && wiValuePresent(pWebRequest, pArg))

SA_RET_OP_VALID void * wiGetSessionStruct(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
									   SA_PARAM_NN_STR const char *pKey,
									   SA_PARAM_NN_VALID ParseTable *pti);

SA_RET_NN_STR const char *wiGetUsername(SA_PARAM_NN_VALID ASWebRequest *pWebRequest);

void wiSetAttachmentMode(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
						 SA_PARAM_NN_STR const char *pFileName);

void wiAppendStringf(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
					 FORMAT_STR const char *pFormat, ...);
#define wiAppendStringf(pWebRequest, pFormat, ...) wiAppendStringf(pWebRequest, FORMAT_STRING_CHECKED(pFormat), __VA_ARGS__)

void wiAppendFile(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
				  SA_PARAM_NN_STR const char *pFileName);

void wiAppendMessageBox(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
						SA_PARAM_NN_STR const char *pSubject,
						SA_PARAM_NN_STR const char *pMessage,
						WebMessageBoxFlags options);

void wiAppendStruct(SA_PARAM_NN_VALID ASWebRequest *pWebRequest,
					SA_PARAM_NN_STR const char *pTemplate,
					SA_PARAM_NN_VALID ParseTable *tpi,
					SA_PARAM_NN_VALID void *pStruct);

void accountServerHttpInit(unsigned int port);