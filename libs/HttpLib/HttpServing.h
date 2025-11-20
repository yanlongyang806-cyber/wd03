#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"
#include "fileServing.h"
#include "HttpXpathSupport.h"
#include "earray.h"

#define PUT_REAL_ACCESS_LEVEL_HERE 9

typedef struct UrlArgument UrlArgument;
typedef struct UrlArgumentList UrlArgumentList;
typedef struct StructInfoForHttpXpath StructInfoForHttpXpath;



typedef void HttpServingXPathCallback(GlobalType eContainerType, ContainerID iContainerID, int iRequestID, char *pXPath,
	UrlArgument **ppServerSideURLArgs, int iAccessLevel, GetHttpFlags eFlags, const char *pAuthNameAndIP);
typedef void HttpServingCommandCallback(GlobalType eContainerType, ContainerID iContainerID, int iClientID, int iRequestID,
	CommandServingFlags eFlags, char *pCommandString, int iAccessLevel, const char *pAuthNameAndIP);
typedef void HttpServingJpegCallback(char *pJpegName, UrlArgumentList *pArgList, int iRequestID);



//any string returned is an error string explaining why HTTP serving currently can not happen
typedef char *HttpServingCanServeCB(void);


bool HttpServing_Begin_MultiplePorts(U32 **ppPortNums, HttpServingXPathCallback *pXPathCB, HttpServingCommandCallback *pCommandCB,
	HttpServingCanServeCB *pCanServeCB, HttpServingJpegCallback *pJpegCB, FileServingCommandCallBack *pFileCB, 
	char *pDefaultRedirectString, char *pHeaderFileName, char *pFooterFileName,
	char *pStaticHomeDirName, const char *authProductName, const char *commandCategoryFilter);

static __forceinline bool HttpServing_Begin(U32 iPortNum, HttpServingXPathCallback *pXPathCB, HttpServingCommandCallback *pCommandCB,
	HttpServingCanServeCB *pCanServeCB, HttpServingJpegCallback *pJpegCB, FileServingCommandCallBack *pFileCB, 
	char *pDefaultRedirectString, char *pHeaderFileName, char *pFooterFileName,
	char *pStaticHomeDirName, const char *authProductName, const char *commandCategoryFilter)
{
	U32 *pPortNums = NULL;
	bool bRetVal;
	ea32Push(&pPortNums, iPortNum);
	bRetVal = HttpServing_Begin_MultiplePorts(&pPortNums, pXPathCB, pCommandCB,
		pCanServeCB, pJpegCB, pFileCB, pDefaultRedirectString, pHeaderFileName, pFooterFileName,
		pStaticHomeDirName, authProductName, commandCategoryFilter);
	ea32Destroy(&pPortNums);
	return bRetVal;
}


void HttpServing_Tick(void);

void HttpServing_CommandReturn(	int iRequestID, int iClientID, CommandServingFlags eFlags, char *pReturnString);
void HttpServing_XPathReturn(int iRequestID, StructInfoForHttpXpath *pStructInfo);

void HttpServing_JpegReturn( int iRequestID, char *pData, int iDataSize, int iLifeSpan, char *pErrorMessage );

void HttpServing_FileFulfill( int iRequestID, char *pErrorString,
	U64 iTotalSize, U64 iCurBeginByteOffset, U64 iCurNumBytes, U8 *pCurData);

//if true, then commands can be executed by visiting the /directcommand url
extern bool gbHttpServingAllowCommandsInURL;

bool HttpServing_IsDisabled(U32 iPortNum);
void HttpServing_Disable(U32 iPortNum, bool bDisable);

//sets it so that IPs in a named group (as defined in IPFilters) will be able to execute jsonrpc commands in 
//a comma-separated list of categories, and not do anything else at all (ie, no normal servermonitoring)
void HttpServing_RestrictIPGroupToJsonRpcCategories(char *pGroupName, char *pCategoryNames);

LATELINK;
void AddCustomHTMLFooter(char **ppOutString);

//when this result string is sent back to the server monitoring code from executing a command, then
//don't actually display anything, no need to click through the return etc
#define SERVERMON_CMD_RESULT_HIDDEN "__SVMON_RESULT_HIDDEN__"


//to serve up "pretty" webpages in place of normal webpages, use this.
LATELINK;
char *httpServing_GetOverrideHTMLFileForServerMon(const char *pBaseURL, const char *pXPath, UrlArgumentList *pURLArgs);

/*
static HttpServingXPathCallback *spXPathCB = NULL;
static HttpServingCommandCallback *spCommandCB = NULL;
static HttpServingCanServeCB *pCanServeCB = NULL;
static char *spDefaultRedirectString = NULL;*/

