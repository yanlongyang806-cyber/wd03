/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gclCostumeOnly.h"
#include "GameClientLib.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "EString.h"
#include "GraphicsLib.h"
#include "GfxHeadshot.h"
#include "gclCostumeUI.h"
#include "LoginCommon.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "CostumeCommonTailor.h"
#include "StringUtil.h"
#include "gclLogin.h"
#include "../../libs/graphicsLib/GfxTextures.h"
#include "dynSequencer.h"
#include "CharacterCreationUI.h"
#include "SimpleParser.h"
#include "StringUtil.h"
#include "NotifyCommon.h"
#include "GlobalTypes.h"
#include "StringCache.h"
#include "HttpClient.h"
#include "url.h"
#include "file.h"
#include "crypt.h"
#include "gclDialogBox.h"
#include "GameStringFormat.h"
#include "gclEntity.h"
#include "fileutil.h"
#include "UIGen.h"
#include "contact_common.h"
#include "GameAccountDataCommon.h"
#include "species_common.h"
#include "Character.h"
#include "Guild.h"
#include "Player.h"
#include "Organization.h"
#include "gclLogin.h"

#include "EntitySavedData_h_ast.h"
#include "Entity_h_ast.h"
#include "gclCostumeOnly_h_ast.h"

#define GCL_SCREENSHOT_MINIMUM_HEIGHT_WIDTH 32
#define GCL_SCREENSHOT_MAXIMUM_HEIGHT_WIDTH 1800
#define GCL_COSTUME_ONLY_MAXIMUM_FILES 1000

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct CostumeOnly_CostumeListInfo
{
	U32 bfGenderFilter;
	char pcSpecies[64];
} CostumeOnly_CostumeListInfo;

CostumesOnlyInfo *g_CostumeOnlyData;

static bool gNoUpload = false;
AUTO_CMD_INT(gNoUpload, CostumesNoUpload) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_HIDE;

static char *gUploadURL = NULL;
AUTO_COMMAND ACMD_NAME(CostumesUploadURL) ACMD_ACCESSLEVEL(0) ACMD_CMDLINE ACMD_HIDE;
void gclCostumesOnlyUploadURL(const char *url)
{
	char *tmp;
	SAFE_FREE(gUploadURL);
	gUploadURL = strdup(url);
	tmp = gUploadURL;
	while(*tmp)
	{
		if(*tmp == '\\')
			*tmp = '/';
		tmp += 1;
	}
}

static const unsigned short randomValue[256] =
{
	0xbcd1, 0xbb65, 0x42c2, 0xdffe, 0x9666, 0x431b, 0x8504, 0xeb46,
	0x6379, 0xd460, 0xcf14, 0x53cf, 0xdb51, 0xdb08, 0x12c8, 0xf602,
	0xe766, 0x2394, 0x250d, 0xdcbb, 0xa678, 0x02af, 0xa5c6, 0x7ea6,
	0xb645, 0xcb4d, 0xc44b, 0xe5dc, 0x9fe6, 0x5b5c, 0x35f5, 0x701a,
	0x220f, 0x6c38, 0x1a56, 0x4ca3, 0xffc6, 0xb152, 0x8d61, 0x7a58,
	0x9025, 0x8b3d, 0xbf0f, 0x95a3, 0xe5f4, 0xc127, 0x3bed, 0x320b,
	0xb7f3, 0x6054, 0x333c, 0xd383, 0x8154, 0x5242, 0x4e0d, 0x0a94,
	0x7028, 0x8689, 0x3a22, 0x0980, 0x1847, 0xb0f1, 0x9b5c, 0x4176,
	0xb858, 0xd542, 0x1f6c, 0x2497, 0x6a5a, 0x9fa9, 0x8c5a, 0x7743,
	0xa8a9, 0x9a02, 0x4918, 0x438c, 0xc388, 0x9e2b, 0x4cad, 0x01b6,
	0xab19, 0xf777, 0x365f, 0x1eb2, 0x091e, 0x7bf8, 0x7a8e, 0x5227,
	0xeab1, 0x2074, 0x4523, 0xe781, 0x01a3, 0x163d, 0x3b2e, 0x287d,
	0x5e7f, 0xa063, 0xb134, 0x8fae, 0x5e8e, 0xb7b7, 0x4548, 0x1f5a,
	0xfa56, 0x7a24, 0x900f, 0x42dc, 0xcc69, 0x02a0, 0x0b22, 0xdb31,
	0x71fe, 0x0c7d, 0x1732, 0x1159, 0xcb09, 0xe1d2, 0x1351, 0x52e9,
	0xf536, 0x5a4f, 0xc316, 0x6bf9, 0x8994, 0xb774, 0x5f3e, 0xf6d6,
	0x3a61, 0xf82c, 0xcc22, 0x9d06, 0x299c, 0x09e5, 0x1eec, 0x514f,
	0x8d53, 0xa650, 0x5c6e, 0xc577, 0x7958, 0x71ac, 0x8916, 0x9b4f,
	0x2c09, 0x5211, 0xf6d8, 0xcaaa, 0xf7ef, 0x287f, 0x7a94, 0xab49,
	0xfa2c, 0x7222, 0xe457, 0xd71a, 0x00c3, 0x1a76, 0xe98c, 0xc037,
	0x8208, 0x5c2d, 0xdfda, 0xe5f5, 0x0b45, 0x15ce, 0x8a7e, 0xfcad,
	0xaa2d, 0x4b5c, 0xd42e, 0xb251, 0x907e, 0x9a47, 0xc9a6, 0xd93f,
	0x085e, 0x35ce, 0xa153, 0x7e7b, 0x9f0b, 0x25aa, 0x5d9f, 0xc04d,
	0x8a0e, 0x2875, 0x4a1c, 0x295f, 0x1393, 0xf760, 0x9178, 0x0f5b,
	0xfa7d, 0x83b4, 0x2082, 0x721d, 0x6462, 0x0368, 0x67e2, 0x8624,
	0x194d, 0x22f6, 0x78fb, 0x6791, 0xb238, 0xb332, 0x7276, 0xf272,
	0x47ec, 0x4504, 0xa961, 0x9fc8, 0x3fdc, 0xb413, 0x007a, 0x0806,
	0x7458, 0x95c6, 0xccaa, 0x18d6, 0xe2ae, 0x1b06, 0xf3f6, 0x5050,
	0xc8e8, 0xf4ac, 0xc04c, 0xf41c, 0x992f, 0xae44, 0x5f1b, 0x1113,
	0x1738, 0xd9a8, 0x19ea, 0x2d33, 0x9698, 0x2fe9, 0x323f, 0xcde2,
	0x6d71, 0xe37d, 0xb697, 0x2c4f, 0x4373, 0x9102, 0x075d, 0x8e25,
	0x1672, 0xec28, 0x6acb, 0x86cc, 0x186e, 0x9414, 0xd674, 0xd1a5
};


static int checksum(const unsigned char* buf, S32 matchSize)
{
	int i = matchSize;
	unsigned short low  = 0;
	unsigned short high = 0;

	for (; i != 0; i -= 1)
	{
		low  += randomValue[*buf++];
		high += low;
	}

	return high << 16 | low;
}

void CostumeOnly_Destruct_Internal(CostumesOnlyInfo **ppData)
{
	if((*ppData))
	{
		if((*ppData)->pTexture)
		{
			gfxHeadshotRelease((*ppData)->pTexture);
			(*ppData)->pTexture = NULL;
		}
		
		StructDestroy(parse_CostumesOnlyInfo, (*ppData));
		*ppData = NULL;
	}
}

// Destroy the costume only data
AUTO_COMMAND ACMD_NAME("CostumeOnly_Destruct") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_Destruct(void)
{
	if(g_CostumeOnlyData)
	{
		CostumeOnly_Destruct_Internal(&g_CostumeOnlyData);
		g_CostumeOnlyData = NULL;
	}
}

// Utility functions for HTTP upload
char *xmlParse(const char *data, const char *tag)
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

typedef struct httpCallbackData
{
	char *request;
	void *userdata;
} httpCallbackData;

static void httpRequestConnectedCallback(HttpClient *client, httpCallbackData *pending)
{
	httpClientSendBytesRaw(client, pending->request, estrLength(&pending->request));
	estrDestroy(&pending->request);
}

static void httpRequestCallback(HttpClient *client, const char *data, int len, httpCallbackData *pending)
{
	char *code = xmlParse(data, "code");
	char *message = xmlParse(data, "message");
	if(!code)
	{
		estrPrintf(&code, "256");
		estrPrintf(&message, "Unknown error");
	}
	if(code && stricmp(code, "0")!=0)
	{
		char *title = NULL, *body = NULL;
		FormatGameMessageKey(&title, "CostumesOnly.ErrorTitle", STRFMT_STRING("code", code), STRFMT_STRING("message", NULL_TO_EMPTY(message)), STRFMT_END);
		FormatGameMessageKey(&body, "CostumesOnly.ErrorBody", STRFMT_STRING("code", code), STRFMT_STRING("message", NULL_TO_EMPTY(message)), STRFMT_END);
		GameDialogGenericMessage(title, body);
		estrDestroy(&title);
		estrDestroy(&body);
	}
	estrDestroy(&code);
	estrDestroy(&message);
	httpClientDestroy(&client);
	free(pending);
}

static void httpRequestTimeout(HttpClient *client, httpCallbackData *pending)
{
	char *title = NULL, *body = NULL;
	FormatGameMessageKey(&title, "CostumesOnly.ErrorTitle", STRFMT_STRING("code", "254"), STRFMT_STRING("message", "Upload timed out"), STRFMT_END);
	FormatGameMessageKey(&body, "CostumesOnly.ErrorBody", STRFMT_STRING("code", "254"), STRFMT_STRING("message", "Upload timed out"), STRFMT_END);
	GameDialogGenericMessage(title, body);
	estrDestroy(&title);
	estrDestroy(&body);
	httpClientDestroy(&client);
	estrDestroy(&pending->request);
	free(pending);
}

static void httpRequest(UrlArgumentList *args, void *userdata)
{
	char *request=NULL, *query_string=NULL, *post_data=NULL, *extra_headers=NULL, *multipart_boundary=NULL;
	char *url_tmp, *host, *host_tmp, *port_tmp, *path;
	int i, respCode=0, port=80;
	HttpClient *client;
	httpCallbackData *pending;
	HttpMethod method = HTTPMETHOD_GET;

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

	// Bake the remaining args
	for(i=0; i<eaSize(&args->ppUrlArgList); i++)
	{
		UrlArgument *arg = args->ppUrlArgList[i];
		if(arg->method == HTTPMETHOD_HEADER)
		{
			estrConcatf(&extra_headers, "%s: %s\r\n", arg->arg, arg->value);
		}
		else if(multipart_boundary)
			continue; // If we already have a multipart boundary, we don't need to process non-header args in the first pass
		else if(arg->method == HTTPMETHOD_MULTIPART)
		{
			// Switch gears, encode this as a multipart/form-data message
			estrPrintf(&multipart_boundary, "--------------------------------%u%u%u", randomU32(), randomU32(), randomU32());
			method = HTTPMETHOD_POST;
		}
		else if(arg->method == HTTPMETHOD_GET)
		{
			estrConcatf(&query_string, "%s%s=", (query_string?"&":""), arg->arg);
			urlEscape(arg->value, &query_string, true, false);
		}
		else if(arg->method == HTTPMETHOD_POST)
		{
			estrConcatf(&post_data, "%s%s=", (post_data?"&":""), arg->arg);
			urlEscape(arg->value, &post_data, true, false);
			method = HTTPMETHOD_POST;
		}
	}
	// If we detected a multipart argument, run a second pass to create the multipart data
	if(multipart_boundary)
	{
		estrPrintf(&post_data, "--%s", multipart_boundary);
		for(i=0; i<eaSize(&args->ppUrlArgList); i++)
		{
			UrlArgument *arg = args->ppUrlArgList[i];
			estrConcatf(&post_data, "\r\nContent-Disposition: form-data");
			if(arg->arg)
				estrConcatf(&post_data, "; name=\"%s\"", arg->arg);
			if(arg->filename)
				estrConcatf(&post_data, "; filename=\"%s\"", arg->filename);
			estrConcatf(&post_data, "\r\n");
			if(arg->content_type)
				estrConcatf(&post_data, "Content-Type: %s\r\n", arg->content_type);
			estrConcatf(&post_data, "\r\n");
			if(arg->length)
				estrConcat(&post_data, arg->value_ext?arg->value_ext:arg->value, arg->length);
			else
				estrConcatf(&post_data, "%s", arg->value);
			estrConcatf(&post_data, "\r\n--%s", multipart_boundary);
		}
		estrConcatf(&post_data, "--");
	}

	// Create the HTTP request
	if(method == HTTPMETHOD_GET)
		estrPrintf(&request, "GET");
	else if(method == HTTPMETHOD_POST)
		estrPrintf(&request, "POST");
	estrConcatf(&request, " %s", path);
	if(query_string)
	{
		estrConcatf(&request, "?%s", query_string);
		estrDestroy(&query_string);
	}
	estrConcatf(&request, " HTTP/1.1\r\n");
	estrConcatf(&request, "Host: %s\r\n", host);
	estrConcatf(&request, "User-Agent: " ORGANIZATION_NAME_SINGLEWORD " HttpClient/gclCostumesOnly\r\n");
	if(post_data)
	{
		if(multipart_boundary)
			estrConcatf(&request, "Content-Type: multipart/form-data; boundary=%s\r\n", multipart_boundary);
		else
			estrConcatf(&request, "Content-Type: application/x-www-form-urlencoded\r\n");
		estrConcatf(&request, "Content-Length: %u\r\n", estrLength(&post_data));
	}
	if(extra_headers)
		estrConcatf(&request, "%s", extra_headers);
	estrConcatf(&request, "\r\n");
	if(post_data)
	{
		estrConcat(&request, post_data, estrLength(&post_data));
		estrConcatf(&request, "\r\n");
	}

	// Create the callback data
	pending = calloc(1, sizeof(httpCallbackData));
	pending->request = request;
	pending->userdata = userdata;

	// Send the HTTP request
	client = httpClientConnect(host, port, httpRequestConnectedCallback, NULL, httpRequestCallback, httpRequestTimeout, commDefault(), false, HTTPCLIENT_DEFAULT_TIMEOUT);
	assert(client);
	httpClientSetUserData(client, pending);

	// Cleanup
	SAFE_FREE(host);
	SAFE_FREE(url_tmp);
	estrDestroy(&query_string);
	estrDestroy(&post_data);
	estrDestroy(&extra_headers);
	estrDestroy(&multipart_boundary);
	urlDestroy(&args);
}

// callback for a costume shot written to disk
// This is where it should be sent to server (when file name is passed to it).
void HeadShotForCostumesOnlyDone(HeadshotForCostumesOnly *pUserData)
{
	StructDestroy(parse_PlayerCostume, pUserData->pPlayerCostume);
	if (pUserData->pTexture)
	{	
		texUnloadRawData(pUserData->pTexture);
	}

	if(pUserData->pFileName && !gNoUpload)
	{
		//
		// This is where you can send the created file to whatever server you want.
		//
		U32 iLen;
		char *pData, pScreenshotDir[MAX_PATH], md5_base[1024], md5[33];
		UrlArgumentList *pArgs;
		UrlArgument *pPhoto;

		fileSpecialDir("screenshots", SAFESTR(pScreenshotDir));
		strcat(pScreenshotDir, "/");
		strcat(pScreenshotDir, pUserData->pFileName);
		pData = fileAlloc(pScreenshotDir, &iLen);
		if(!pData)
		{
			char *title = NULL, *body = NULL;
			FormatGameMessageKey(&title, "CostumesOnly.ErrorTitle", STRFMT_STRING("code", "255"), STRFMT_STRING("message", STACK_SPRINTF("Cannot read in costume image %s", pScreenshotDir)), STRFMT_END);
			FormatGameMessageKey(&body, "CostumesOnly.ErrorBody", STRFMT_STRING("code", "255"), STRFMT_STRING("message", STACK_SPRINTF("Cannot read in costume image %s", pScreenshotDir)), STRFMT_END);
			GameDialogGenericMessage(title, body);
			estrDestroy(&title);
			estrDestroy(&body);
			estrDestroy(&pUserData->pFileName);
			free(pUserData);
			return;
		}
		pArgs = urlToUrlArgumentList(gUploadURL ? gUploadURL : "http://www.champions-online.com/wgsGameImage/post"); // Use URL "http://jgalvin:85/wgsGameImage/post" for testing
		pPhoto = urlAddValueExt(pArgs, "file", pData, HTTPMETHOD_MULTIPART);
		pPhoto->length = iLen;
		pPhoto->filename = StructAllocString(pUserData->pFileName);
		pPhoto->content_type = StructAllocString("image/jpg");
		sprintf(md5_base, "%s%u%s", gGCLState.loginName, gGCLState.accountID, pUserData->pFileName);
		cryptMD5Hex(md5_base, (int)strlen(md5_base), SAFESTR(md5));
		urlAddValue(pArgs, "accountname", gGCLState.loginName, HTTPMETHOD_POST);
		urlAddValue(pArgs, "key", md5, HTTPMETHOD_POST);
		httpRequest(pArgs, NULL);
		free(pData);
	}

	estrDestroy(&pUserData->pFileName);
	free(pUserData);
	
}

static void AddVersionTag(unsigned char **pestr)
{
	estrConcat(pestr, "\x1c\x02\x00\x00\x02\x00\x02", 7); // version
}

static void AddIptcTagConcat(unsigned char **pestr, unsigned char tag, const char *pch, S32 len)
{
	if(len>0 && len < 0xffff)
	{
		estrConcat(pestr, "\x1c\x02", 2);
		estrConcatChar(pestr, tag);
		estrConcatChar(pestr, *(((char*)(&len))+1));
		estrConcatChar(pestr, *(((char*)(&len))+0));
		estrConcat(pestr, pch, len);
	}
}

static void AddIptcTag(unsigned char **pestr, unsigned char tag, const char *pch)
{
	U32 len = (U32)strlen(pch);
	if(len>0 && len < 0xffff)
	{
		estrConcat(pestr, "\x1c\x02", 2);
		estrConcatChar(pestr, tag);
		estrConcatChar(pestr, *(((char*)(&len))+1));
		estrConcatChar(pestr, *(((char*)(&len))+0));
		estrAppend2(pestr, pch);
	}
}

static void CreatePhotoshopBlock(unsigned char **pestrDest, unsigned char **pestr)
{
	int len1, len = estrLength(pestr);
	if(len&1)
	{
		estrConcatChar(pestr, 0);
		len++;
	}

	estrConcat(pestrDest, "Photoshop 3.0\x00", 14); 
	len1 = estrLength(pestrDest);
	estrConcat(pestrDest, "8BIM\x04\x04\x00\x00", 8);
	len1 = estrLength(pestrDest);
	estrConcatChar(pestrDest, *(((char*)(&len))+3));
	estrConcatChar(pestrDest, *(((char*)(&len))+2));
	estrConcatChar(pestrDest, *(((char*)(&len))+1));
	estrConcatChar(pestrDest, *(((char*)(&len))+0));
	estrConcat(pestrDest, *pestr, len);
	len1 = estrLength(pestrDest);
}

// Create photoshop block with tags for account name, character name, costume (string) and return it in pestr
static void CreateNameAndCostumeInfo(char *pcAccountName, char *pName, char *pCostume, char **pestr, Gender eGender, const char *pcSpecies)
{
	char *estrData = NULL;
	char * esGender = NULL;	
	char * esSpecies = NULL;	
	char *esCheckSum = NULL;
	U32 iChecksum;

	estrCreate(&estrData);
	AddVersionTag(&estrData); // must be first

	AddIptcTag(&estrData, 25, GetProductName());
	AddIptcTag(&estrData, 25, GetShortProductName());
	if(eGender != Gender_Unknown)
	{
		estrPrintf(&esGender, "Gender:%s", StaticDefineIntRevLookup(GenderEnum, eGender));
		AddIptcTag(&estrData, 25, esGender);
	}
	if (pcSpecies)
	{
		estrPrintf(&esSpecies, "Species:%s", pcSpecies);
		AddIptcTag(&estrData, 25, esSpecies);
	}

	AddIptcTag(&estrData, 120,  pcAccountName);	
	AddIptcTag(&estrData, 120,  pName);
	iChecksum = checksum(pCostume, (S32)strlen(pCostume));
	estrPrintf(&esCheckSum, "7799%10.10d", iChecksum);
	estrConcatChar(&esCheckSum, 0);
	AddIptcTagConcat(&estrData, 120, esCheckSum, estrLength(&esCheckSum));
	AddIptcTag(&estrData, 200, "\x00\x08");
	AddIptcTag(&estrData, 202, pCostume);

	CreatePhotoshopBlock(pestr, &estrData);

	estrDestroy(&esCheckSum);
	estrDestroy(&esGender);
	estrDestroy(&esSpecies);
	estrDestroy(&estrData);
}

// Write the costume out to disk
void WriteHeadshotForCostumesOnly(char *pcAccountName, char *pCharacterName, char *pFileName, PlayerCostume *pCostume, SpeciesDef *pSpecies, CharacterClass* pClass,
								  int iSizeX, int iSizeY, char *pBGTextureName, Vec3 camPos, Vec3 camDir, float fFOV,
								  const char *pPoseString, float animDelta, bool forceBodyshot, Color bgColor,
								  const char *framename)
{
	HeadshotForCostumesOnly *pHandle;
	char *pUnescapedString = NULL;
	WLCostume *pWLCostume;
	BasicTexture *pTexture = NULL;
	char *pPoseInfo = NULL;
	WLCostume **eaSubCostumes = NULL;
	char *esCostumeString = NULL;
	CostumeOnlyForDisk *pCostumeOnlyForDisk;
	Gender eGender = Gender_Unknown;
	PCSkeletonDef* pSkel;
	
	if(!g_CostumeOnlyData || !g_CostumeOnlyData->bInitialized)
	{
		return;
	}

	if(pBGTextureName && pBGTextureName[0])
	{
		if (!texFind(pBGTextureName, true))
		{
			// error
			return;
		}

		pTexture = texLoadRawData(pBGTextureName, TEX_LOAD_IN_BACKGROUND, WL_FOR_UI);

		if (!pTexture)
		{
			// error
			return;
		}
	}

	pHandle = calloc(sizeof(HeadshotForCostumesOnly), 1);

	pHandle->pTexture = pTexture;

	pHandle->pPlayerCostume = StructClone(parse_PlayerCostume, pCostume);

	estrStackCreate(&pPoseInfo);
	estrCopy2(&pPoseInfo, pPoseString);
	if(strcmp(pPoseInfo, "") == 0)
	{
		estrAppend2(&pPoseInfo, "IDLE");
	}
	estrAppend2(&pPoseInfo, " NOLOD");
	estrTrimLeadingAndTrailingWhitespace(&pPoseInfo);
	
	g_CostumeOnlyData->bfg.bFreeOnRelease = false;
	dynBitFieldGroupClearAll(&g_CostumeOnlyData->bfg);
	dynBitFieldGroupAddBits(&g_CostumeOnlyData->bfg, pPoseInfo, 0);
	estrDestroy(&pPoseInfo);

	pWLCostume = costumeGenerate_CreateWLCostume(pHandle->pPlayerCostume, pSpecies, NULL, NULL, NULL, NULL, NULL, "Saved.", 0, 0, true, &eaSubCostumes);

	if (pWLCostume)
	{
		char *ppCostumeInfo = NULL;
		if (!pWLCostume->bComplete)
		{
			if (pTexture)
			{
				texUnloadRawData(pTexture);
			}

			wlCostumeFree(pWLCostume);
			FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
			{
				wlCostumeFree(pSubCostume);
			}
			FOR_EACH_END;

			StructDestroy(parse_PlayerCostume, pHandle->pPlayerCostume);
			// error message?
			free(pHandle);
			return;
		}

		if (RefSystem_ReferentFromString("wlCostume", pWLCostume->pcName))
		{
			const char* name = pWLCostume->pcName;
			wlCostumeFree(pWLCostume);
			FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
			{
				wlCostumeFree(pSubCostume);
			}
			FOR_EACH_END;

			pWLCostume = RefSystem_ReferentFromString("wlCostume", name);
		}
		else
		{
			RefSystem_AddReferent("wlCostume", pWLCostume->pcName, pWLCostume);
			FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
			{
				wlCostumePushSubCostume(pSubCostume, pWLCostume);
			}
			FOR_EACH_END;
		}
		
		// Create the costume string for saving to disk
		// currently only one item in the struct (the costume)
		pCostumeOnlyForDisk = StructCreate(parse_CostumeOnlyForDisk);
		pCostumeOnlyForDisk->pCostumeV5 = StructClone(parse_PlayerCostume, pCostume);
		
		// create the full struct string
		ParserWriteText(&esCostumeString, parse_CostumeOnlyForDisk, pCostumeOnlyForDisk, 0, 0, 0);
		
		// done with the creation of the string for saving to disk, destroy the struct used.
		StructDestroy(parse_CostumeOnlyForDisk, pCostumeOnlyForDisk);

		// create extra info for jpg, in this case the costume and the characters name and gender
		pSkel = GET_REF(pCostume->hSkeleton);
		if(pSkel)
		{	
			eGender = pSkel->eGender;
		}				

		CreateNameAndCostumeInfo(pcAccountName, pCharacterName, esCostumeString, &ppCostumeInfo, eGender, pSpecies ? pSpecies->pcName : NULL);

		// destroy the original string
		estrDestroy(&esCostumeString);

		if(g_CostumeOnlyData->bSendToServer)
		{
			estrPrintf(&pHandle->pFileName, "%s", pFileName);
		}
		else
		{
			estrDestroy(&pHandle->pFileName)		;
		}		
		if (framename)
		{
			gfxHeadshotCaptureCostumeAndSave(pFileName, iSizeX, iSizeY, pWLCostume, pTexture, framename,
				bgColor, forceBodyshot,
				&g_CostumeOnlyData->bfg, g_CostumeOnlyData->pcAnimKeyword, NULL, fFOV, NULL, HeadShotForCostumesOnlyDone,
				pHandle, &ppCostumeInfo);
		}
		else
		{
			gfxHeadshotCaptureCostumeSceneAndSave(pFileName, iSizeX, iSizeY, pWLCostume, pTexture,
				bgColor, &g_CostumeOnlyData->bfg, g_CostumeOnlyData->pcAnimKeyword, NULL, camPos, camDir, fFOV, NULL, HeadShotForCostumesOnlyDone,
				pHandle, animDelta, forceBodyshot, &ppCostumeInfo);
		}

		// done with this			
		estrDestroy(&ppCostumeInfo);
	}
	else
	{
		if (pTexture)
		{
			texUnloadRawData(pTexture);
		}		
		StructDestroy(parse_PlayerCostume, pHandle->pPlayerCostume);
		// error message?
		free(pHandle);
	}
}

// Get the portrait texture for the given character choice, with the given background image, 
// full control over camera position and direction
SA_ORET_OP_VALID BasicTexture *CostumeOnly_CameraTexture(CostumesOnlyInfo *pCostumeInfo, const char *pchBackground,
													 F32 fCameraX, F32 fCameraY, F32 fCameraZ,
													 F32 fCamDirX, F32 fCamDirY, F32 fCamDirZ, F32 fAnimationFrame,
													 const char *pcPose, bool bTransparent)
{
	WLCostume *pWLCostume;
	char *pPoseInfo = NULL;
	WLCostume **eaSubCostumes = NULL;
	Vec3 vCameraPos = {0}, vCameraDir = {0};

	if(!pCostumeInfo)
	{
		return NULL;
	}

	dynBitFieldClear(&pCostumeInfo->bfg.flashBits);
	dynBitFieldClear(&pCostumeInfo->bfg.toggleBits);

	estrStackCreate(&pPoseInfo);
	estrCopy2(&pPoseInfo, pcPose);
	if(strcmp(pPoseInfo, "") == 0)
	{
		estrAppend2(&pPoseInfo, "IDLE");
	}
	
	estrAppend2(&pPoseInfo, " NOLOD");
	estrTrimLeadingAndTrailingWhitespace(&pPoseInfo);
	dynBitFieldGroupAddBits(&pCostumeInfo->bfg, pPoseInfo, 0);
	estrDestroy(&pPoseInfo);

	pWLCostume = costumeGenerate_CreateWLCostume((PlayerCostume *)pCostumeInfo->pPlayerCostume, NULL/*species*/, NULL, NULL, NULL, NULL, NULL, "View.", 0, 0, true, &eaSubCostumes);

	if(pWLCostume)
	{
		if (!pWLCostume->bComplete)
		{
			wlCostumeFree(pWLCostume);
			FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
			{
				wlCostumeFree(pSubCostume);
			}
			FOR_EACH_END;
			
			return NULL;
		}

		// remove it from ref system

		//check if it's already in the dictionary
		if (RefSystem_ReferentFromString("wlCostume", pWLCostume->pcName))
		{
			WLCostume * pWLCostume_old = RefSystem_ReferentFromString("wlCostume", pWLCostume->pcName);
		
			RefSystem_RemoveReferent(pWLCostume_old, true);
		
			wlCostumeFree(pWLCostume_old);

		}
		
		RefSystem_AddReferent("wlCostume", pWLCostume->pcName, pWLCostume);
		FOR_EACH_IN_EARRAY(eaSubCostumes, WLCostume, pSubCostume)
		{
			wlCostumePushSubCostume(pSubCostume, pWLCostume);
		}
		FOR_EACH_END;

		vCameraDir[0] = fCamDirX;
		vCameraDir[1] = fCamDirY;
		vCameraDir[2] = fCamDirZ;
		
		vCameraPos[0] = fCameraX;
		vCameraPos[1] = fCameraY;
		vCameraPos[2] = fCameraZ;

		pCostumeInfo->pTexture = gfxHeadshotCaptureCostumeScene("CharacterPortrait", pCostumeInfo->fWidth, pCostumeInfo->fHeight, pWLCostume, texFindAndFlag(pchBackground, true, WL_FOR_UI), NULL, 
			!bTransparent ? ColorBlack : ColorTransparent, false, &pCostumeInfo->bfg, pCostumeInfo->pcAnimKeyword, NULL, vCameraPos, vCameraDir, -1, fAnimationFrame, NULL, NULL );
		return NULL;

	}
	
	return NULL;
}

// Create a new character during the login process. Uses full camera position and direction
void CostumeOnly_WriteCostumeToDisk(const char *pchBackground, F32 fWidth, F32 fHeight, 
									F32 fCameraX, F32 fCameraY, F32 fCameraZ, F32 fCameraDirX, F32 fCameraDirY, F32 fCameraDirZ, F32 fFOV,
									F32 fAnimationFrame,
									const char *pPose, Color bgColor,
									const char *framename)
{
	Vec3 vCamPos = {0};
	Vec3 vCamDir = {0};
	char *esName = NULL;

	vCamPos[0] = fCameraX;
	vCamPos[1] = fCameraY;
	vCamPos[2] = fCameraZ;
	
	vCamDir[0] = fCameraDirX;
	vCamDir[1] = fCameraDirY;
	vCamDir[2] = fCameraDirZ;

	estrStackCreate(&esName);

	if(!g_CostumeOnlyData->pCharacterName || (!*g_CostumeOnlyData->pCharacterName) || StringIsInvalidCharacterName(g_CostumeOnlyData->pCharacterName, 0))
	{
		estrPrintf(&g_CostumeOnlyData->pCharacterName, "NoName");
	}

	// create file name
	if(pchBackground && pchBackground[0])
	{
		estrPrintf(&esName, "Costume_%s_%s_%s_%d.jpg", gGCLState.displayName, g_CostumeOnlyData->pCharacterName, pchBackground, timeSecondsSince2000());
	}
	else
	{
		estrPrintf(&esName, "Costume_%s_%s_%d.jpg", gGCLState.displayName, g_CostumeOnlyData->pCharacterName, timeSecondsSince2000());
	}

	WriteHeadshotForCostumesOnly(gGCLState.displayName, g_CostumeOnlyData->pCharacterName, esName, 
		(PlayerCostume *)g_CostumeOnlyData->pPlayerCostume, g_CostumeOnlyData->pcSpecies ? RefSystem_ReferentFromString("Species", g_CostumeOnlyData->pcSpecies) : NULL, NULL, fWidth, fHeight, (char *)pchBackground, vCamPos,
		vCamDir, fFOV, pPose, fAnimationFrame, false, bgColor, framename);

	estrDestroy(&esName);
}

// Just check to see if this name is valid
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_CheckName");
bool CostumeOnlyExpr_CheckName(const char *pchName)
{
	if(g_pFakePlayer)
	{
		const char *pNewName = entGetPersistedName((Entity *)g_pFakePlayer);
		bool bValid = pNewName && *pNewName;

		if (bValid)
		{
			if(g_pFakePlayer->pSaved->savedDescription && g_pFakePlayer->pSaved->savedDescription[0])
			{
				if(!CharacterCreation_IsDescriptionValidWithErrorMessage( g_pFakePlayer->pSaved->savedDescription ))
				{
					return false;
				}
			}
			if(CharacterCreation_IsNameValidWithErrorMessage( pNewName, kNotifyType_NameInvalid ))
			{	

				if(g_pFakePlayer->pSaved)
				{
					strcpy_s(g_pFakePlayer->pSaved->savedName, strlen(pchName) + 1, pchName);
					//trim the trailing whitespace
					removeTrailingWhiteSpaces(g_pFakePlayer->pSaved->savedName);
					return true;
				}
			}
		}
		else
		{
			char *pchMesg = NULL;
			StringCreateNameError(&pchMesg, STRINGERR_MIN_LENGTH);
			notify_NotifySend(NULL, kNotifyType_NameInvalid, pchMesg, NULL, NULL);
			estrDestroy(&pchMesg);
		}
	}

	return false;

}

// initialize the costumes only data. 
// if there is already a headshot created it will destroy the old costume and reset for a new headshot
void CostumeOnly_InitCostumeOnlyData(CostumesOnlyInfo **ppData, F32 fWidth, F32 fHeight)
{
	if(!(*ppData))
	{
		*ppData = StructCreate(parse_CostumesOnlyInfo);
	}

	if(*ppData)
	{
		if((*ppData)->pTexture && (!nearf(texWidth((*ppData)->pTexture), fWidth) || !nearf(texHeight((*ppData)->pTexture), fHeight)))
		{
			if((*ppData)->pTexture)
			{
				gfxHeadshotRelease((*ppData)->pTexture);
			}
			(*ppData)->pTexture = NULL;
		}

		if((*ppData)->pPlayerCostume)
		{
			StructDestroyNoConst(parse_PlayerCostume, (*ppData)->pPlayerCostume);
			(*ppData)->pPlayerCostume = NULL;
		}

		(*ppData)->bInitialized = false;
		(*ppData)->bHeadShotFinal = false;	
		(*ppData)->fWidth = fWidth;
		(*ppData)->fHeight = fHeight;
		(*ppData)->bInitialized = true;
	}
}

// Use the current fake character and costume from the editor for initialization.
bool CostumeOnly_InitFromCreation(CostumesOnlyInfo **ppData, F32 fWidth, F32 fHeight)
{
	Entity *pEntityPlayer = entActivePlayerPtr();
	
	if(!pEntityPlayer)
	{
		// create character case
		pEntityPlayer = (Entity *)g_pFakePlayer;
	}
	
	CostumeOnly_InitCostumeOnlyData(ppData, fWidth, fHeight);
    if((*ppData) && g_characterSelectionData && CostumeUI_GetCostume() && pEntityPlayer && pEntityPlayer->pSaved)
	{
		char buffer[256];

		// record the character name
		estrPrintf(&(*ppData)->pCharacterName, "%s", pEntityPlayer->pSaved->savedName);
		(*ppData)->pPlayerCostume = StructCloneNoConst(parse_PlayerCostume, CostumeUI_GetCostume());
		{
			SpeciesDef *pSpecies = CostumeUI_GetSpecies();
			if (pSpecies)
			{
				(*ppData)->pcSpecies = allocAddString(pSpecies->pcName);
			}
		}

		// create name for this costume
		sprintf(buffer,"%s_%s",gGCLState.displayName, (*ppData)->pCharacterName);
		(*ppData)->pPlayerCostume->pcName = allocAddString(buffer);
		
		costumeTailor_StripUnnecessary((*ppData)->pPlayerCostume);
		
		return true;
	}
	
	return false;
}

// initialize from the current player (costume by index)
bool CostumeOnly_InitFromCharacterSlot(CostumesOnlyInfo **ppData, F32 fWidth, F32 fHeight, S32 iIndex)
{
	Entity *pPlayerEntity = entActivePlayerPtr();
	if(pPlayerEntity && pPlayerEntity->pSaved && iIndex >= 0 && iIndex < eaSize(&pPlayerEntity->pSaved->costumeData.eaCostumeSlots))
	{
		CostumeOnly_InitCostumeOnlyData(ppData, fWidth, fHeight);
		if(*ppData)
		{
			// record the character name
			estrPrintf(&(*ppData)->pCharacterName, "%s", pPlayerEntity->pSaved->savedName);
			(*ppData)->pPlayerCostume = StructCloneDeConst(parse_PlayerCostume, pPlayerEntity->pSaved->costumeData.eaCostumeSlots[iIndex]->pCostume);
			(*ppData)->pcSpecies = allocAddString(REF_STRING_FROM_HANDLE(pPlayerEntity->pChar->hSpecies));
			return true;
		}
	}
	
	return false;
}

// Get the camera position and direction using a camera rotation (around Y) a distance, height
// and some camera moves left right and up down. Height is modified by the skeleton height.
void CostumeOnly_SimpleLayout(F32 fCameraRot, F32 fCameraDistance, F32 fCameraHeight, F32 fCameraLeftRight, F32 fCameraUpDown,
	Vec3 vPosOut, Vec3 vDirOut)
{
	F32 fCameraX, fCameraY, fCameraZ,fCamDirX, fCamDirY, fCamDirZ, fHeightMod = 1.0f;
	Vec3 vAxis = {0, 1, 0}, vCameraIn = {0}, vCameraOut;

	if(!g_CostumeOnlyData->pPlayerCostume)
	{
		return;
	}

	// modify the height based on the skeleton
	fHeightMod = g_CostumeOnlyData->pPlayerCostume->fHeight / 6.0f;

	vCameraIn[2] = fCameraDistance * fHeightMod;
	rotateVecAboutAxis(RAD(fCameraRot), vAxis, vCameraIn, vCameraOut);

	// look at target from height rotated around y axis
	fCameraX = vCameraOut[0];	
	fCameraY = fCameraHeight * fHeightMod;
	fCameraZ = vCameraOut[2];
	fCamDirX = -fCameraX;
	fCamDirZ = -fCameraZ;
	fCamDirY = -fCameraY;		

	// Shift the camera left or right
	vCameraIn[2] = fCameraLeftRight * fHeightMod;
	rotateVecAboutAxis(RAD(fCameraRot + 90.0f), vAxis, vCameraIn, vCameraOut);
	fCameraX += vCameraOut[0];
	fCameraZ += vCameraOut[2];

	// Shift the camera up/down
	fCameraY += fCameraUpDown * fHeightMod;

	vPosOut[0] = fCameraX;
	vPosOut[1] = fCameraY;
	vPosOut[2] = fCameraZ;

	vDirOut[0] = fCamDirX;
	vDirOut[1] = fCamDirY;
	vDirOut[2] = fCamDirZ;
}

// Initialize from the character creator screen, input is image width and height
AUTO_COMMAND ACMD_NAME("CostumeOnly_InitFromCreation") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_InitFromCreation_External(F32 fWidth, F32 fHeight)
{
	CostumeOnly_InitFromCreation(&g_CostumeOnlyData, fWidth, fHeight);
}

bool CostumeOnly_InitFromCostume(CostumesOnlyInfo **ppData, PlayerCostume *pCostume, const char* strName, F32 fWidth, F32 fHeight)
{
	
	CostumeOnly_InitCostumeOnlyData(ppData, fWidth, fHeight);
	if((*ppData) && pCostume && !nullStr(strName))
	{
		char buffer[256];
		
		estrPrintf(&(*ppData)->pCharacterName, "%s", strName);
		(*ppData)->pPlayerCostume = StructCloneDeConst(parse_PlayerCostume, pCostume);

		// create name for this costume
		sprintf(buffer,"%s_%s",gGCLState.displayName, (*ppData)->pCharacterName);
		(*ppData)->pPlayerCostume->pcName = allocAddString(buffer);
		
		costumeTailor_StripUnnecessary((*ppData)->pPlayerCostume);
		
		return true;
	}
	
	return false;
}

bool CostumeOnly_InitFromCostume_External(PlayerCostume *pCostume, const char* strName, F32 fWidth, F32 fHeight)
{
	return CostumeOnly_InitFromCostume(&g_CostumeOnlyData, pCostume, strName, fWidth, fHeight);
}


// Write this costume to disk, uses camera position and direction
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_Finalize");
void CostumeOnlyExpr_Finalize(const char *pchBackground,
	F32 fCameraX, F32 fCameraY, F32 fCameraZ,
	F32 fCamDirX, F32 fCamDirY, F32 fCamDirZ, F32 fAnimationFrame,
	const char *pPose, bool bTransparent, bool bSendToServer)
{
	if(g_CostumeOnlyData->fWidth >= GCL_SCREENSHOT_MINIMUM_HEIGHT_WIDTH && g_CostumeOnlyData->fHeight >= GCL_SCREENSHOT_MINIMUM_HEIGHT_WIDTH &&
		g_CostumeOnlyData->fWidth <= GCL_SCREENSHOT_MAXIMUM_HEIGHT_WIDTH && g_CostumeOnlyData->fHeight <= GCL_SCREENSHOT_MAXIMUM_HEIGHT_WIDTH)
	{
		g_CostumeOnlyData->bSendToServer = bSendToServer;
		CostumeOnly_WriteCostumeToDisk(pchBackground, g_CostumeOnlyData->fWidth, g_CostumeOnlyData->fHeight, 
			fCameraX, fCameraY, fCameraZ,
			fCamDirX, fCamDirY, fCamDirZ, -1,
			fAnimationFrame,
			pPose, bTransparent ? ColorTransparent : ColorBlack,
			NULL);
	}
}

// Write this costume to disk using rotation, height (for camera aiming), and some camera position shifts
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_FinalizeSimple");
void CostumeOnlyExpr_FinalizeSimple(const char *pchBackground,
							  F32 fCameraRot, F32 fCameraDistance, F32 fCameraHeight, F32 fCameraLeftRight, F32 fCameraUpDown,
							  F32 fAnimationFrame,
							  const char *pPose, bool bTransparent, bool bSendToServer)
{
	if(g_CostumeOnlyData->pPlayerCostume &&
		g_CostumeOnlyData->fWidth >= GCL_SCREENSHOT_MINIMUM_HEIGHT_WIDTH && g_CostumeOnlyData->fHeight >= GCL_SCREENSHOT_MINIMUM_HEIGHT_WIDTH &&
		g_CostumeOnlyData->fWidth <= GCL_SCREENSHOT_MAXIMUM_HEIGHT_WIDTH && g_CostumeOnlyData->fHeight <= GCL_SCREENSHOT_MAXIMUM_HEIGHT_WIDTH)
	{
		Vec3 vPos, vDir;
		g_CostumeOnlyData->bSendToServer = bSendToServer;
		CostumeOnly_SimpleLayout(fCameraRot, fCameraDistance, fCameraHeight, fCameraLeftRight, fCameraUpDown, vPos, vDir);
		
		CostumeOnly_WriteCostumeToDisk(pchBackground, g_CostumeOnlyData->fWidth, g_CostumeOnlyData->fHeight, 
			vPos[0], vPos[1], vPos[2],
			vDir[0], vDir[1], vDir[2], -1,
			fAnimationFrame,
			pPose, bTransparent ? ColorTransparent : ColorBlack,
			NULL);
	}
}

// Write this costume to disk using rotation, height (for camera aiming), and some camera position shifts
void CostumeOnly_FinalizeStyle(HeadshotStyleDef *pDef, F32 fAnimationFrame, const char *pPose, bool bSendToServer)
{
	if(g_CostumeOnlyData->pPlayerCostume &&
		g_CostumeOnlyData->fWidth >= GCL_SCREENSHOT_MINIMUM_HEIGHT_WIDTH && g_CostumeOnlyData->fHeight >= GCL_SCREENSHOT_MINIMUM_HEIGHT_WIDTH &&
		g_CostumeOnlyData->fWidth <= GCL_SCREENSHOT_MAXIMUM_HEIGHT_WIDTH && g_CostumeOnlyData->fHeight <= GCL_SCREENSHOT_MAXIMUM_HEIGHT_WIDTH)
	{
		Vec3 vPos, vDir;
		g_CostumeOnlyData->bSendToServer = bSendToServer;
		CostumeOnly_SimpleLayout(0, 9.5, -1, 0, 3.75, vPos, vDir);

		CostumeOnly_WriteCostumeToDisk(pDef->pchBackground, g_CostumeOnlyData->fWidth, g_CostumeOnlyData->fHeight, 
			vPos[0], vPos[1], vPos[2],
			vDir[0], vDir[1], vDir[2], contact_HeadshotStyleDefGetFOV(pDef, -1),
			fAnimationFrame,
			pPose, colorFromRGBA(pDef->uiBackgroundColor),
			pDef->pchFrame);
	}
}

// write a costume to disk, uses full camera position and direction
AUTO_COMMAND ACMD_NAME("CostumeOnly_WriteToDisk") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_WriteToDisk(const char *pchBackground,
					  F32 fCameraX, F32 fCameraY, F32 fCameraZ,
					  F32 fCamDirX, F32 fCamDirY, F32 fCamDirZ, F32 fAnimationFrame,
					  const char *pPose, bool bTransparent, bool bSendToServer)
{
	CostumeOnlyExpr_Finalize(pchBackground, 
		fCameraX, fCameraY, fCameraZ,
		fCamDirX, fCamDirY, fCamDirZ, fAnimationFrame,
		pPose, bTransparent, bSendToServer);
		
}

// Get the texture for displaying on the UI. This is the full camera position, direction version. 
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_GetTexture");
SA_RET_OP_VALID BasicTexture *CostumeOnly_GetTexture(const char *pchBackground,
												  F32 fCameraX, F32 fCameraY, F32 fCameraZ,
												  F32 fCamDirX, F32 fCamDirY, F32 fCamDirZ, F32 fAnimationFrame,
												  const char *pPose, bool bTansparent)
{
	if(g_CostumeOnlyData && g_CostumeOnlyData->bInitialized)
	{
		if(g_CostumeOnlyData->pTexture && g_CostumeOnlyData->bHeadShotFinal)
		{
			return g_CostumeOnlyData->pTexture;
		}
		else if(g_CostumeOnlyData->pTexture)
		{
			if(gfxHeadshotRaisePriority(g_CostumeOnlyData->pTexture))
			{
				g_CostumeOnlyData->bHeadShotFinal = true;
				gfxHeadshotReleaseFreeTexture(g_CostumeOnlyData->pTexture, false);
				return g_CostumeOnlyData->pTexture;
			}
			else
			{
				return NULL;
			}
		}
		else
		{
			CostumeOnly_CameraTexture(g_CostumeOnlyData, pchBackground, 
				fCameraX, fCameraY, fCameraZ,
				fCamDirX, fCamDirY, fCamDirZ, fAnimationFrame,
				pPose, bTansparent);
		}
	}
	
	return NULL;
}

// Get the texture for displaying on the UI. This is the simpler camera rotation, modified height system with camera left right and up down
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_GetTextureSimple");
SA_RET_OP_VALID BasicTexture *CostumeOnly_GetTextureSimple(const char *pchBackground,
												  F32 fCameraRot, F32 fCameraDistance, F32 fCameraHeight, F32 fCameraLeftRight, F32 fCameraUpDown,
												  F32 fAnimationFrame,
												  const char *pPose, bool bTansparent)
{
	if(g_CostumeOnlyData && g_CostumeOnlyData->bInitialized)
	{
		if(g_CostumeOnlyData->pTexture && g_CostumeOnlyData->bHeadShotFinal)
		{
			return g_CostumeOnlyData->pTexture;
		}
		else if(g_CostumeOnlyData->pTexture)
		{
			if(gfxHeadshotRaisePriority(g_CostumeOnlyData->pTexture))
			{
				g_CostumeOnlyData->bHeadShotFinal = true;
				gfxHeadshotReleaseFreeTexture(g_CostumeOnlyData->pTexture, false);
				return g_CostumeOnlyData->pTexture;
			}
			else
			{
				return NULL;
			}
		}
		else
		{
			Vec3 vPos, vDir;
			CostumeOnly_SimpleLayout(fCameraRot, fCameraDistance, fCameraHeight, fCameraLeftRight, fCameraUpDown, vPos, vDir);
			
			CostumeOnly_CameraTexture(g_CostumeOnlyData, pchBackground, 
				vPos[0], vPos[1], vPos[2],
				vDir[0], vDir[1], vDir[2],
				fAnimationFrame,
				pPose, bTansparent);
		}
	}

	return NULL;
}

// Get the texture for displaying on the UI. This is the simpler camera rotation, modified height system with camera left right and up down
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_IsInStance");
bool CostumeOnly_IsInStance(const char *pcStance)
{
	if(g_CostumeOnlyData && g_CostumeOnlyData->pPlayerCostume)
	{
		if(stricmp(g_CostumeOnlyData->pPlayerCostume->pcStance, pcStance) == 0)
		{
			return true;
		}
	}
	
	return false;
}

#define DefaultSaveBackground "CC_Comic_Page_Blue"
//#define DefaultSaveBackground "HeadshotStyle_Default_01"

// Write the current editor costume to disk in the screen shot directory. It uses project specific defined
// camera angles.
AUTO_COMMAND ACMD_NAME("CostumeOnly_WriteEditorCostume") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_WriteEditorCostume(F32 fWidth, F32 fHeight)
{
	if(CostumeOnly_InitFromCreation(&g_CostumeOnlyData, fWidth, fHeight))
	{
		CostumeOnlyExpr_FinalizeSimple(DefaultSaveBackground, 0, 9.5, -1, 0, 3.75, 4.0, "", true, false);
	}
}

AUTO_COMMAND ACMD_NAME("CostumeOnly_WriteEditorCostumeBG") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_WriteEditorCostumeBG(F32 fWidth, F32 fHeight, const char *bg)
{
	if(CostumeOnly_InitFromCreation(&g_CostumeOnlyData, fWidth, fHeight))
	{
		CostumeOnlyExpr_FinalizeSimple(bg, 0, 9.5, -1, 0, 3.75, 4.0, "", true, false);
	}
}

AUTO_COMMAND ACMD_NAME("CostumeOnly_WriteEditorCostumeStyle") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_WriteEditorCostumeStyle(F32 fWidth, F32 fHeight, const char *headshotStyle)
{
	if(CostumeOnly_InitFromCreation(&g_CostumeOnlyData, fWidth, fHeight))
	{
		HeadshotStyleDef *pDef = RefSystem_ReferentFromString("HeadshotStyleDef", headshotStyle);
		if (pDef)
		{
			CostumeOnly_FinalizeStyle(pDef, 4.0, "", false);
		}
	}
}

// Write out a character's costume (by slot index) It uses project specific defined
// camera angles.
AUTO_COMMAND ACMD_NAME("CostumeOnly_WriteCostumeSlot") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_WriteCostumeSlot(F32 fWidth, F32 fHeight, S32 iIndex)
{
	if(CostumeOnly_InitFromCharacterSlot(&g_CostumeOnlyData, fWidth, fHeight, iIndex))
	{
		CostumeOnlyExpr_FinalizeSimple(DefaultSaveBackground, 0, 9.5, -1, 0, 3.75, 4.0, "", true, false);
	}
}

// Write out a character's costume (by slot index) It uses project specific defined
// camera angles and is a 300 x 400 shot
AUTO_COMMAND ACMD_NAME("MakeCostumeJPeg") ACMD_ACCESSLEVEL(0);
void CostumeOnly_MakeCostumeSlotJPeg(S32 iIndex)
{
	if(CostumeOnly_InitFromCharacterSlot(&g_CostumeOnlyData, 300.0f, 400.0f, iIndex))
	{
		CostumeOnlyExpr_FinalizeSimple(DefaultSaveBackground, 0, 9.5, -1, 0, 3.75, 4.0, "", true, false);
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(MakeCostumeJPeg);
void CostumeOnly_MakeCostumeActiveJPeg(void)
{
	S32 iIndex;
	
	Entity *pPlayerEntity = entActivePlayerPtr();
	if(pPlayerEntity && pPlayerEntity->pSaved)
	{
		iIndex = pPlayerEntity->pSaved->costumeData.iActiveCostume;
		if(CostumeOnly_InitFromCharacterSlot(&g_CostumeOnlyData, 300.0f, 400.0f, iIndex))
		{
			CostumeOnlyExpr_FinalizeSimple(DefaultSaveBackground, 0, 9.5, -1, 0, 3.75, 4.0, "", true, false);
			// notify not using kNotifyType_Costume_JPeg_Saved due to conflicts with release and tip 09-08-2009
//			notify_NotifySend(NULL, kNotifyType_Costume_JPeg_Saved, TranslateMessageKeySafe("CharacterEditor.CostumeSaved"), NULL, NULL);	// from charactereditor_borrows.uigen.ms
			notify_NotifySend(NULL, kNotifyType_CostumeChanged, TranslateMessageKeySafe("CharacterEditor.CostumeSaved"), NULL, NULL);	// from charactereditor_borrows.uigen.ms

		}
	}
}

// get S16 or S32 values from a buffer (that has already been checked for for correct size)
S32 BigEndianToLittle(char *pBuffer, S32 iBytes)
{
	S32 iReturnVal = 0, i;
	
	if(iBytes != 2 && iBytes != 4)
	{
		return 0;
	}
	
	for(i = 0; i < iBytes; ++i)
	{
		iReturnVal <<= 8;	// shift 8 bits
		iReturnVal += (unsigned char)pBuffer[i];
	}
	
	return iReturnVal;
}

// Get the costume from disk, If **ppCostumeOnly is NULL then trhis is just used to check if this 
// is a costume embedded jpg file with the correct gender
bool GetCostumeOnlyFromBuffer(CostumeOnlyForDisk **ppCostumeOnly, char *pBuffer, S32 iSize, U32 bfGenderFilter, const char *pcSpecies, char **eOutNames, SpeciesDef **ppCostumeSpecies)
{
	S32 tagType;
	bool bCorrectProduct = false;
	bool bGenderSet = false;
	bool bGenderCorrect = false;
	bool bSpeciesSet = false;
	bool bSpeciesCorrect = false;
	U32 iCheckSum = 0, iCostumeChecksum;
	char achTempText[128];

	if(iSize > 256)	// small files can't contain any costume data
	{
		if(pBuffer[24]=='P' && pBuffer[24+13] == 0)
		{
			// possibly a photoshop block header
			if(strstr(&pBuffer[24], "Photoshop 3.0") != NULL)
			{
				S32 lenOffset = 24 + 14 + 8; // header + photoshop + 8im
				S32 iDataLen = BigEndianToLittle(&pBuffer[lenOffset], sizeof(S32));
				lenOffset+= 4 + 7; // length plus version
				
				if(iDataLen > 0 && iDataLen + lenOffset < iSize)
				{
					// data won't run past buffer length
					// start decoding the Iptc Tags
					S32 iIndex = 0;
					
					while(iIndex + lenOffset + 5 < iSize)	// +5 1c 02 tag sh sl
					{
						if(pBuffer[iIndex + lenOffset] == 0x1c && pBuffer[iIndex + lenOffset + 1] == 0x02)
						{
							iIndex += 2;	// 1c 02
							tagType = (unsigned char)pBuffer[iIndex + lenOffset];
							++iIndex;		// tag
							if(tagType == 202)
							{
								// found costume only data
								S32 iCostumeOnlySize = BigEndianToLittle(&pBuffer[iIndex + lenOffset], sizeof(S16));
								iIndex += sizeof(S16);
								
								// check for checksum
								if(iCheckSum != 0)
								{
									iCostumeChecksum = checksum(&pBuffer[iIndex + lenOffset], iCostumeOnlySize);
									if(iCostumeChecksum != iCheckSum)
									{
										// bad checksum
										return false;
									}
								}
								
								// Must have found correct product by now
								if(bCorrectProduct && ((!bGenderSet && !bfGenderFilter) || bGenderCorrect) && ((!bSpeciesSet && (!pcSpecies || !*pcSpecies)) || bSpeciesCorrect))
								{
									if(iCostumeOnlySize > 0 && iCostumeOnlySize + iIndex + lenOffset < iSize)
									{
										if(ppCostumeOnly == NULL)
										{
											// this costume appears correct
											return true;
										}
										// create the costume only struct	
										pBuffer[iIndex + lenOffset + iCostumeOnlySize] = 0;	// null terminate, damages data but we aren't using it anyway
										*ppCostumeOnly = StructCreate(parse_CostumeOnlyForDisk);
										if(ParserReadText(&pBuffer[iIndex + lenOffset], parse_CostumeOnlyForDisk, *ppCostumeOnly, 0))
										{
											return true;
										}
										
										StructDestroy(parse_CostumeOnlyForDisk, *ppCostumeOnly);
									}
								}
								break;
							}
							else if(tagType == 25)
							{
								// possibly gender or product name
								S32 iDataSize = BigEndianToLittle(&pBuffer[iIndex + lenOffset], sizeof(S16));
								iIndex += sizeof(S16);
								if(iDataSize < 0)
								{
									// bad data, negative value for this tag
									break;
								}
								else if(iDataSize > 0 && strncmp(GetProductName(), &pBuffer[iIndex + lenOffset], iDataSize) == 0)
								{
									bCorrectProduct = true;
								}
								else if(iDataSize > 7 && bfGenderFilter != 0 && strncmp(&pBuffer[iIndex + lenOffset], "Gender:", 7) == 0)
								{
									Gender eGenderValue;
									strncpy(achTempText, &pBuffer[iIndex + lenOffset + 7], iDataSize - 7);
									eGenderValue = StaticDefineIntGetInt(GenderEnum, achTempText);
									bGenderSet = true;
									if(eGenderValue >= 0 && eGenderValue < Gender_MAX && (bfGenderFilter & BIT(eGenderValue)) != 0)
									{
										bGenderCorrect = true;
									}
								}
								else if(iDataSize > 8 && strncmp(&pBuffer[iIndex + lenOffset], "Species:", 8) == 0)
								{
									strncpy(achTempText, &pBuffer[iIndex + lenOffset + 8], iDataSize - 8);
									if (pcSpecies && *pcSpecies)
									{
										// Check name if validating
										if(strcmp(achTempText, pcSpecies) == 0)
										{
											bSpeciesCorrect = true;
										}
										bSpeciesSet = true;
									}
									if (ppCostumeSpecies)
									{
										*ppCostumeSpecies = RefSystem_ReferentFromString("Species", achTempText);
									}
								}

								iIndex += iDataSize;
							}
							else if (tagType == 120)
							{
								S32 iDataSize = BigEndianToLittle(&pBuffer[iIndex + lenOffset], sizeof(S16));
								S32 iStringIndex;
								iIndex += sizeof(S16);
								if(iDataSize < 0)
								{
									// bad data, negative value for this tag
									break;
								}
								
								if(iDataSize > 4 && strncmp(&pBuffer[iIndex + lenOffset], "7799",4) == 0)
								{
									// found checksum
									iCheckSum = atoi(&pBuffer[iIndex + lenOffset + 4]);
								}
								else if(eOutNames != NULL)
								{
									for(iStringIndex = 0; iStringIndex < iDataSize; ++iStringIndex)
									{
										estrConcatChar(eOutNames, pBuffer[iIndex + lenOffset + iStringIndex]);
									}
								}
								iIndex += iDataSize;
							}
							else
							{
								// skip data
								S32 iSkipValue;
								iSkipValue = BigEndianToLittle(&pBuffer[iIndex + lenOffset], sizeof(S16));
								iIndex += sizeof(S16);
								if(iSkipValue < 0)
								{
									// bad data, negative value for this tag
									break;
								}
								iIndex += iSkipValue;
							}
						}
						else
						{
							// not an iptc tag !
							break;
						}
					}
				}
			}
		}
	}
	
	return false;
}

PlayerCostume *CostumeOnly_LoadCostume(const char *pcCostumeName)
{
	PlayerCostume *pCostume = NULL;
	if(pcCostumeName && pcCostumeName[0])
	{
		char * eFilePath = NULL;
		char * pFileBuffer;
		CostumeOnlyForDisk *pCostumeOnly = NULL;
		SpeciesDef *pSpecies = NULL;
		char pScreenshotDir[MAX_PATH];
		S32 iSize;

		fileSpecialDir("screenshots", SAFESTR(pScreenshotDir));
		strcat(pScreenshotDir, "/");

		// attempt to load from screenshot directory
		estrStackCreate(&eFilePath);
		estrConcatf(&eFilePath, "%s%s", pScreenshotDir, pcCostumeName);
		pFileBuffer = fileAlloc(eFilePath, &iSize);
		if(pFileBuffer)
		{
			// decode jpg header here, if valid file then create the CostumeOnlyForDisk structure
			if(GetCostumeOnlyFromBuffer(&pCostumeOnly, pFileBuffer, iSize, 0, NULL, NULL, &pSpecies))
			{
				// If have V0 structure, convert it to V5
				if (pCostumeOnly->pCostumeV0) {
					pCostumeOnly->pCostumeV5 = CONTAINER_RECONST(PlayerCostume, costumeLoad_UpgradeCostumeV0toV5(CONTAINER_NOCONST(PlayerCostumeV0, pCostumeOnly->pCostumeV0)));
					StructDestroy(parse_PlayerCostumeV0, pCostumeOnly->pCostumeV0);
					pCostumeOnly->pCostumeV0 = NULL;
				}

				pCostume = pCostumeOnly->pCostumeV5;
				pCostumeOnly->pCostumeV5 = NULL;

				StructDestroy(parse_CostumeOnlyForDisk, pCostumeOnly);
			}

			free(pFileBuffer);
		}

		estrDestroy(&eFilePath);
	}
	return pCostume;
}

void gclGuild_SetLoadedUniform(PlayerCostume *pCostume, SpeciesDef *pSpecies);

// load a costume from disk and place it into the guild UI for use
AUTO_COMMAND ACMD_NAME("CostumeOnly_LoadCostumeToGuildUI") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_LoadCostumeToGuildUI(const char *pcCostumeName)
{
	PlayerCostume *pCostume = NULL;
	if(pcCostumeName && pcCostumeName[0])
	{
		char * eFilePath = NULL;
		char * pFileBuffer;
		CostumeOnlyForDisk *pCostumeOnly = NULL;
		SpeciesDef *pSpecies = NULL;
		char pScreenshotDir[MAX_PATH];
		S32 iSize;

		fileSpecialDir("screenshots", SAFESTR(pScreenshotDir));
		strcat(pScreenshotDir, "/");

		// attempt to load from screenshot directory
		estrConcatf(&eFilePath, "%s%s",pScreenshotDir,pcCostumeName);
		pFileBuffer = fileAlloc(eFilePath, &iSize);
		if(pFileBuffer)
		{
			// decode jpg header here, if valid file then create the CostumeOnlyForDisk structure
			if(GetCostumeOnlyFromBuffer(&pCostumeOnly, pFileBuffer, iSize, 0, NULL, NULL, &pSpecies))
			{
				// If have V0 structure, convert it to V5
				if (pCostumeOnly->pCostumeV0) {
					pCostumeOnly->pCostumeV5 = CONTAINER_RECONST(PlayerCostume, costumeLoad_UpgradeCostumeV0toV5(CONTAINER_NOCONST(PlayerCostumeV0, pCostumeOnly->pCostumeV0)));
					StructDestroy(parse_PlayerCostumeV0, pCostumeOnly->pCostumeV0);
					pCostumeOnly->pCostumeV0 = NULL;
				}
				if(pCostumeOnly && pCostumeOnly->pCostumeV5)
				{
					gclGuild_SetLoadedUniform(pCostumeOnly->pCostumeV5, pSpecies);
				}

				StructDestroy(parse_CostumeOnlyForDisk, pCostumeOnly);

			}

			free(pFileBuffer);
		}

		estrDestroy(&eFilePath);
	}
}

// load a costume from disk and place it into the editor, must be same gender
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenCostumeBoneGroupToEditor");
bool CostumeOnly_LoadCostumeBoneGroupToEditor(const char *pcCostumeName, const char *pcBoneGroup)
{
	PlayerCostume *pCostume = (PlayerCostume *)CostumeUI_GetCostume();
	bool bSuccess = false;
	if(pcCostumeName && pcCostumeName[0] && pCostume)
	{
		char * eFilePath = NULL;
		char * pFileBuffer;
		CostumeOnlyForDisk *pCostumeOnly = NULL;
		char pScreenshotDir[MAX_PATH];
		S32 iSize;

		fileSpecialDir("screenshots", SAFESTR(pScreenshotDir));
		strcat(pScreenshotDir, "/");

		// attempt to load from screenshot directory
		estrConcatf(&eFilePath, "%s%s",pScreenshotDir,pcCostumeName);
		pFileBuffer = fileAlloc(eFilePath, &iSize);
		if(pFileBuffer)
		{
			U32 bfGenderFilter = Gender_Unknown;
			Gender eGender;
			const char *pcSpecies = NULL;
			SpeciesDef *pSpecies = NULL;
			PCSkeletonDef* pSkel = GET_REF(pCostume->hSkeleton);

			if(pSkel)
			{
				eGender = pSkel->eGender;
				if (pSkel->eGender != Gender_Unknown)
				{
					bfGenderFilter = BIT(pSkel->eGender);
				}
			}

			pcSpecies = REF_STRING_FROM_HANDLE(pCostume->hSpecies);

			// decode jpg header here, if valid file then create the CostumeOnlyForDisk structure
			if(GetCostumeOnlyFromBuffer(&pCostumeOnly, pFileBuffer, iSize, pcBoneGroup && *pcBoneGroup ? 0 : bfGenderFilter, pcBoneGroup && *pcBoneGroup ? NULL : pcSpecies, NULL, &pSpecies))
			{
				// If have V0 structure, convert it to V5
				if (pCostumeOnly->pCostumeV0) {
					pCostumeOnly->pCostumeV5 = CONTAINER_RECONST(PlayerCostume, costumeLoad_UpgradeCostumeV0toV5(CONTAINER_NOCONST(PlayerCostumeV0, pCostumeOnly->pCostumeV0)));
					StructDestroy(parse_PlayerCostumeV0, pCostumeOnly->pCostumeV0);
					pCostumeOnly->pCostumeV0 = NULL;
				}

				if(pCostumeOnly->pCostumeV5)
				{
					Entity *pPlayerEnt = entActivePlayerPtr();
					Entity *pEnt = CostumeUI_GetSourceEnt();
					GameAccountData *pData = entity_GetGameAccount(pPlayerEnt);
					PlayerCostume **eaUnlockedCostumes = NULL;
					PlayerCostume *pBaseCostume = pcBoneGroup && *pcBoneGroup ? StructClone(parse_PlayerCostume, pCostume) : NULL;
					PCSlotType *pSlotType = CostumeUI_GetSlotType();

					// Force the species onto the costume. Probably not the ideal solution, but it at least
					// makes an effort.
					if (pcSpecies && *pcSpecies && (!IS_HANDLE_ACTIVE(pCostumeOnly->pCostumeV5->hSpecies) || pcSpecies != REF_STRING_FROM_HANDLE(pCostumeOnly->pCostumeV5->hSpecies)))
					{
						SET_HANDLE_FROM_STRING(g_hSpeciesDict, pcSpecies, pCostumeOnly->pCostumeV5->hSpecies);
					}

					costumeEntity_GetUnlockCostumes(pEnt && pEnt->pSaved ? pEnt->pSaved->costumeData.eaUnlockedCostumeRefs : NULL, pData, pPlayerEnt, pEnt ? pEnt : pPlayerEnt, &eaUnlockedCostumes);

					// Overlay the loaded costume onto the base costume.
					if (pBaseCostume && costumeTailor_ApplyCostumeOverlay(pBaseCostume, NULL, pCostumeOnly->pCostumeV5, eaUnlockedCostumes, pcBoneGroup, pSlotType, true, false, true, true))
					{
						StructDestroy(parse_PlayerCostume, pCostumeOnly->pCostumeV5);
						pCostumeOnly->pCostumeV5 = pBaseCostume;
						pBaseCostume = NULL;
					}

					// make sure there are no extra parts in this (weapons) that aren't required and don't have a category etc
					costumeTailor_StripUnnecessary(CONTAINER_NOCONST(PlayerCostume, pCostumeOnly->pCostumeV5));

					// validate the costume
					if(!costumeValidate_ValidatePlayerCreated(pCostumeOnly->pCostumeV5, GET_REF(pCostumeOnly->pCostumeV5->hSpecies), pSlotType, entActivePlayerPtr(), CostumeUI_GetSourceEnt(), NULL, NULL, NULL, false))
					{
						Guild *pGuild = guild_GetGuild(pPlayerEnt);
						GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pPlayerEnt);
						costumeTailor_MakeCostumeValid(CONTAINER_NOCONST(PlayerCostume, pCostumeOnly->pCostumeV5), GET_REF(pCostumeOnly->pCostumeV5->hSpecies), eaUnlockedCostumes, pSlotType, true, false, false, pGuild, false, pExtract, false, NULL);
						// Need to strip due to fact that MakeCostumeValid removes some eaRegionCategories but the parts are still there.
						costumeTailor_StripUnnecessary(CONTAINER_NOCONST(PlayerCostume, pCostumeOnly->pCostumeV5));
					}

					if(costumeValidate_ValidatePlayerCreated(pCostumeOnly->pCostumeV5, GET_REF(pCostumeOnly->pCostumeV5->hSpecies), pSlotType, entActivePlayerPtr(), CostumeUI_GetSourceEnt(), NULL, NULL, NULL, false))
					{
						// replace costume in editor
						PCSkeletonDef* pSkelNew = GET_REF(pCostumeOnly->pCostumeV5->hSkeleton);
						if(pSkelNew && pSkelNew == pSkel)
						{
							CostumeUI_SetCostumeAndRegen(CONTAINER_NOCONST(PlayerCostume, pCostumeOnly->pCostumeV5));
							bSuccess = true;
						}
					}

					StructDestroySafe(parse_PlayerCostume, &pBaseCostume);
				}

				StructDestroy(parse_CostumeOnlyForDisk, pCostumeOnly);

			}

			free(pFileBuffer);
		}

		estrDestroy(&eFilePath);
	}
	return bSuccess;
}

// load a costume from disk and place it into the editor, must be same gender
AUTO_COMMAND ACMD_NAME("CostumeOnly_LoadCostumeToEditor") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void CostumeOnly_LoadCostumeToEditor(const char *pcCostumeName)
{
	CostumeOnly_LoadCostumeBoneGroupToEditor(pcCostumeName, NULL);
}

typedef enum CostumeListMode
{
	CostumeList,
	CostumeListAll,
	CostumeListAllIncludingNoGender,
} CostumeListMode;

// earray
struct CostumeListState
{
	CostumeOnly_CostumeList **eaCostumeList;
	int iLastUpdateFrame;
	bool bQueueDestroy;
	bool bReload;
	CostumeListMode eMode;

} s_CostumeListState;

FileScanAction CostumeOnly_DeleteCostumeScreenshot(char *pDir, struct _finddata32_t* data, void *pUserData)
{
	if(pUserData)
	{
		// Use the same file restrictions as the picker find files
		if((data->attrib & _A_SUBDIR) == 0 && strstr(data->name, "Costume_") != NULL && strEndsWith(data->name, ".jpg"))
		{
			const char *pchTargetName = pUserData;
			if (!stricmp(data->name, pchTargetName))
			{
				char * eFilePath = NULL;
				char pScreenshotDir[MAX_PATH];

				fileSpecialDir("screenshots", SAFESTR(pScreenshotDir));
				strcat(pScreenshotDir, "/");

				// attempt to load from screenshot directory
				estrConcatf(&eFilePath, "%s%s",pScreenshotDir, data->name);
				estrConcatChar(&eFilePath, '\0');

				remove(eFilePath);

				estrDestroy(&eFilePath);
				return FSA_STOP;
			}
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}

FileScanAction CostumeOnly_CostumeListPickerFindFiles(char *pDir, struct _finddata32_t* data, void *pUserData)
{
	if(pUserData)
	{
		if((data->attrib & _A_SUBDIR) == 0 && strstr(data->name, "Costume_") != NULL && strEndsWith(data->name, ".jpg"))
		{
			U32 bfGenderFilter =  ((CostumeOnly_CostumeListInfo *)pUserData)->bfGenderFilter;
			const char *pcSpecies = ((CostumeOnly_CostumeListInfo *)pUserData)->pcSpecies;
			char * eFilePath = NULL;
			char * pFileBuffer;
			char pScreenshotDir[MAX_PATH];
			S32 iSize;

			fileSpecialDir("screenshots", SAFESTR(pScreenshotDir));
			strcat(pScreenshotDir, "/");

			// attempt to load from screenshot directory
			estrConcatf(&eFilePath, "%s%s",pScreenshotDir, data->name);
			pFileBuffer = fileAlloc(eFilePath, &iSize);
			if(pFileBuffer && eaSize(&s_CostumeListState.eaCostumeList) < GCL_COSTUME_ONLY_MAXIMUM_FILES)
			{
				char *eNames = NULL;
				estrCreate(&eNames);
				// NULL as first parameter indicates no actual costume should be created, its just a check on the file
				// see if this is a file we care about
				if(GetCostumeOnlyFromBuffer(NULL, pFileBuffer, iSize, bfGenderFilter, pcSpecies, &eNames, NULL))
				{
					char cDate[MAX_PATH]={0};
					char cNameBuffer[MAX_PATH];
					char *pToken = NULL;
					char *pContext = NULL;
					CostumeOnly_CostumeList *pCopy;

					// create the date
					strcpy(cNameBuffer, data->name);
					pToken = strtok_s(cNameBuffer, "_.", &pContext);
					while(pToken != NULL)
					{
						if(atoi(pToken) > 28382400)
						{
							timeMakeDateStringFromSecondsSince2000(cDate, atoi(pToken));
							break;
						}
						pToken = strtok_s(NULL, "_.", &pContext);
					}

					pCopy = StructCreate(parse_CostumeOnly_CostumeList);

					// the file name
					estrPrintf(&pCopy->eFileName, "%s", data->name);

					// what to display users
					estrPrintf(&pCopy->eDisplayName, "%s %s", eNames, cDate);

					eaPush(&s_CostumeListState.eaCostumeList, pCopy);

				}
				estrDestroy(&eNames);

				free(pFileBuffer);
			}
			estrDestroy(&eFilePath);
		}
	}
	return FSA_EXPLORE_DIRECTORY;
}

void CostumeOnly_OncePerFrame(void)
{
	CostumeOnly_CostumeListInfo listInfo = { 0, "" };

	if (s_CostumeListState.bQueueDestroy || s_CostumeListState.bReload)
	{
		eaDestroyStruct(&s_CostumeListState.eaCostumeList, parse_CostumeOnly_CostumeList);
	}
	
	if (s_CostumeListState.bReload)
	{
		char pchScreenshotDir[MAX_PATH];

		if (s_CostumeListState.eMode == CostumeList)
		{
			PlayerCostume *pCostume = (PlayerCostume *)CostumeUI_GetCostume();

			if(pCostume)	
			{
				PCSkeletonDef* pSkel = GET_REF(pCostume->hSkeleton);
				if(pSkel && pSkel->eGender != Gender_Unknown)
				{	
					listInfo.bfGenderFilter = BIT(pSkel->eGender);
				}
				if (GET_REF(pCostume->hSpecies))
				{
					const char *speciesName = REF_STRING_FROM_HANDLE(pCostume->hSpecies);
					ANALYSIS_ASSUME(speciesName != NULL);
					strcat(listInfo.pcSpecies, speciesName);
				}
				else
				{
					SpeciesDef *pSpecies = CostumeUI_GetSpecies();
					if (pSpecies)
					{
						strcat(listInfo.pcSpecies, pSpecies->pcName);
					}
				}
			}
		}

		if (s_CostumeListState.eMode == CostumeListAll)
		{
			listInfo.bfGenderFilter = BIT(Gender_Male) | BIT(Gender_Female);
			listInfo.pcSpecies[0] = '\0';
		}

		eaCreate(&s_CostumeListState.eaCostumeList);
		fileSpecialDir("screenshots", SAFESTR(pchScreenshotDir));
		fileScanAllDataDirs(pchScreenshotDir, CostumeOnly_CostumeListPickerFindFiles, &listInfo);
	}

	s_CostumeListState.bQueueDestroy = false;
	s_CostumeListState.bReload = false;
}

CostumeOnly_CostumeList*** CostumeOnly_CostumeListModel(void)
{
	return &s_CostumeListState.eaCostumeList;
}
	
void CostumeOnly_DestroyCostumeList(void)
{
	s_CostumeListState.bQueueDestroy = true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_CreateCostumeList");
void CostumeOnly_CreateCostumeList(void)
{
	s_CostumeListState.eMode = CostumeList;
	s_CostumeListState.bReload = true;
}

// create the list of files for the costumes that are available
//
// NOTE: This requires the costumes to have a gender.  Use AllIncludingNoGender if you want things
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeOnly_CreateCostumeListAll");
void CostumeOnly_CreateCostumeListAll(void)
{
	s_CostumeListState.eMode = CostumeListAll;
	s_CostumeListState.bReload = true;
}

// because CostumeOnly_CreateCostumeListAll requires gender to be set
void CostumeOnly_CreateCostumeListAllIncludingNoGender(void)
{
	s_CostumeListState.eMode = CostumeListAllIncludingNoGender;
	s_CostumeListState.bReload = true;
}

// Set this gen's model to the list of valid files that could be applied to
// the editor. Note final checks are not done at this time on the files.
// its possibly for one of these files to fail upon actual application to editor
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetJpgCostumeList);
S32 CostumeOnly_Expr_GenGetCostumeList(SA_PARAM_NN_VALID UIGen *pGen)
{
	ui_GenSetList(pGen, &s_CostumeListState.eaCostumeList, parse_CostumeOnly_CostumeList);
	return eaSize(&s_CostumeListState.eaCostumeList);
}

// Delete a costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DeleteCostumeFile);
void CostumeOnly_Expr_DeleteCostume(const char *pchFilename)
{
	char pScreenshotDir[MAX_PATH];
	fileSpecialDir("screenshots", SAFESTR(pScreenshotDir));
	fileScanAllDataDirs(pScreenshotDir, CostumeOnly_DeleteCostumeScreenshot, (void *)pchFilename);
	s_CostumeListState.bReload = true;
}

// Load the file into the editor
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(DestroyFileList);
void CostumeOnly_Expr_DestroyFileList()
{
	s_CostumeListState.bQueueDestroy = true;
	s_CostumeListState.bReload = true;
}

// Load the file into the editor
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenFileToGuildUI);
void CostumeOnly_Expr_GenFileToGuildUI(const char *pcFileName)
{
	CostumeOnly_LoadCostumeToGuildUI(pcFileName);
}

// Load the file into the editor
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenFileToEditor);
bool CostumeOnly_Expr_GenFileToEditor(const char *pcFileName)
{
	return CostumeOnly_LoadCostumeBoneGroupToEditor(pcFileName, NULL);
}

#include "gclCostumeOnly_h_ast.c"
