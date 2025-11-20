#include "GenericHttpServing.h"
#include "HttpServing.h"
#include "httpXPathSupport.h"
#include "HttpLib.h"
#include "cmdParse.h"
//#include "ServerlibCmdParse.h"
#include "StringUtil.h"
#include "httpJpegLibrary.h"
#include "StatusReporting.h"
#include "GenericFileServing.h"
#include "GlobalEnums.h"

//if true, then trying to do generic HTTP serving and failing to is NOT fatal
static int sbAllowGenericHttpServingFailure = 0;
AUTO_CMD_INT(sbAllowGenericHttpServingFailure, AllowGenericHttpServingFailure);

//if non-zero, then the serving will try n ports until it finds one that is open
static int siPortRange = 0;
AUTO_CMD_INT(siPortRange, PortRange);

static char pGenericServingStaticDir[MAX_PATH] = "server/MCP/static_home";

static U32 siLastActivityTime = 0;
static char *spPrefixString = NULL;
static char *spSuffixString = NULL;

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setGenericServingStaticDir (char * path)
{
	strcpy(pGenericServingStaticDir, path);
}
char * getGenericServingStaticDir(void)
{
	return pGenericServingStaticDir;
}

void GenericHttpServing_SetPrefixAndSuffixStrings(char *pPrefix, char *pSuffix)
{
	estrCopy2(&spPrefixString, pPrefix);
	estrCopy2(&spSuffixString, pSuffix);
}



void GenericHttpServingXpath_ReturnCB(U32 iReqID1, U32 iReqID2, StructInfoForHttpXpath *pStructInfo)
{
	if (spPrefixString)
	{
		estrConcatf(&pStructInfo->pPrefixString, "%s", spPrefixString);
	}

	if (spSuffixString)
	{
		estrConcatf(&pStructInfo->pSuffixString, "%s", spSuffixString);
	}
	HttpServing_XPathReturn(iReqID1, pStructInfo);
}

void GenericHttpServingXPathCB(GlobalType eContainerType, ContainerID iContainerID, int iRequestID, char *pXPath,
	UrlArgument **ppServerSideURLArgs, int iAccessLevel, GetHttpFlags eFlags, const char *pAuthNameAndIP)
{

	UrlArgumentList url = {0};
	UrlArgumentList *pUrlListToUse;
	url.pBaseURL = pXPath;
	url.ppUrlArgList = ppServerSideURLArgs;

	pUrlListToUse = StructClone(parse_UrlArgumentList, &url);

	//add Auth name and IP as "fake" URL arg just so that they get spawned out to everywhere that might
	//use them...
	urlRemoveValue(pUrlListToUse, "__AUTH");
	urlAddValue(pUrlListToUse, "__AUTH", pAuthNameAndIP, HTTPMETHOD_GET);


	GetStructForHttpXpath(pUrlListToUse, iAccessLevel, iRequestID, 0, GenericHttpServingXpath_ReturnCB, eFlags | GETHTTPFLAG_FULLY_LOCAL_SERVERING);

	StructDestroy(parse_UrlArgumentList, pUrlListToUse);

	GenericHttpServer_Activity();
}


void GenericHttpServingSlowCommandReturnFunc(ContainerID iMCPID, int iRequestID, int iClientID, CommandServingFlags eFlags, char *pMessageString, void *pUserData)
{
	HttpServing_CommandReturn( iRequestID, iClientID, eFlags, pMessageString);
}

void GenericHttpServingCommandCB(GlobalType eContainerType, ContainerID iContainerID, int iClientID, int iRequestID,
	CommandServingFlags eFlags, char *pCommandString, int iAccessLevel, const char *pAuthNameAndIP)
{
	if (eFlags & CMDSRV_NORETURN)
	{
		if (!cmdParseForServerMonitor(eFlags, pCommandString, iAccessLevel, NULL, 0, 0, 0, 0, 0, pAuthNameAndIP, NULL))
		{
			HttpServing_CommandReturn(	iRequestID, iClientID, eFlags, "Execution failed... talk to a programmer. Most likely the AST_COMMAND was set up wrong" );
		}
		else
		{
			HttpServing_CommandReturn(	iRequestID, iClientID, eFlags, SERVERMON_CMD_RESULT_HIDDEN);
		}



	}
	else
	{
		char *pRetString = NULL;
		bool bSlowReturn = false;

		estrStackCreate(&pRetString);

		cmdParseForServerMonitor(eFlags, pCommandString, iAccessLevel, &pRetString, iClientID, iRequestID, 0, GenericHttpServingSlowCommandReturnFunc, NULL, pAuthNameAndIP, &bSlowReturn);

		if (!bSlowReturn)
		{
			HttpServing_CommandReturn(	iRequestID, iClientID, eFlags, pRetString);
		}

		estrDestroy(&pRetString);
	}
	GenericHttpServer_Activity();
}

void GenericHttpServing_JpegReturnCB(char *pData, int iDataSize, int iLifeSpan, char *pMessage, void *pUserData)
{
	char *pDataCopy = NULL;
	if (pData)
	{
		pDataCopy = malloc(iDataSize);
		memcpy(pDataCopy, pData, iDataSize);
	}

	HttpServing_JpegReturn((int)((intptr_t)pUserData), pDataCopy, iDataSize, iLifeSpan, pMessage);
}


void GenericHttpServing_JpegCallback(char *pJpegName, UrlArgumentList *pArgList, int iRequestID)
{

	char *pFirstUnderscore;
	char *pSecondUnderscore;

	GlobalType eServerType;
	ContainerID iServerID;

	GenericHttpServer_Activity();


	pFirstUnderscore = strchr(pJpegName, '_');

	if (!pFirstUnderscore)
	{
		HttpServing_JpegReturn(iRequestID, NULL, 0, 0, "Bad syntax in JPEG request");
		return;
	}

	*pFirstUnderscore = 0;
	pSecondUnderscore = strchr(pFirstUnderscore + 1, '_');

	if (!pSecondUnderscore)
	{
		HttpServing_JpegReturn(iRequestID, NULL, 0, 0, "Bad syntax in JPEG request");
		return;
	}

	*pSecondUnderscore = 0;

	eServerType = NameToGlobalType(pJpegName);
	if (eServerType != GetAppGlobalType())
	{
		HttpServing_JpegReturn(iRequestID, NULL, 0, 0, "Wrong server type in JPEG request");
		return;
	}

	if (!StringToInt(pFirstUnderscore + 1, &iServerID))
	{
		HttpServing_JpegReturn(iRequestID, NULL, 0, 0, "Bad server ID syntax in JPEG request");
		return;
	}

	if (iServerID != 0 && iServerID != GetAppGlobalID())
	{
		HttpServing_JpegReturn(iRequestID, NULL, 0, 0, "Wrong server ID in JPEG request");
		return;
	}

	JpegLibrary_GetJpeg(pSecondUnderscore + 1, pArgList, GenericHttpServing_JpegReturnCB, (void*)((intptr_t)iRequestID));
}

AUTO_COMMAND ACMD_NAME(SetPortRange);
void GenericHttpServer_SetPortRange(int iAmount)
{
	siPortRange = iAmount;
}

bool GenericHttpServing_BeginInternal(int iPortNum, 
									  const char *authProductName, 
									  const char *commandCategoryFilter, 
									  HttpServingXPathCallback XPathCB,
									  HttpServingCommandCallback CmdCB,
									  HttpServingJpegCallback jpgCB,
									  GenericHttpServingFlags eFlags)
{
	int i;
	bool bStarted = false;

	GenericHttpServer_Activity();

	for (i=iPortNum; i <= iPortNum + siPortRange; i++)
	{
		if (HttpServing_Begin(	i, 
								XPathCB ? XPathCB : GenericHttpServingXPathCB, 
								CmdCB ? CmdCB : GenericHttpServingCommandCB, 
								NULL, 
								jpgCB ? jpgCB : GenericHttpServing_JpegCallback, 
								GenericFileServing_CommandCallBack,
								STACK_SPRINTF("/viewxpath?xpath=%s[0].custom", GlobalTypeToName(GetAppGlobalType())), 
								(eFlags & GHSFLAG_AUTOUPDATE_ALWAYS_ON) ? "server/MCP/templates/MCPHtmlHeader_AutoUpdateOn.txt" : NULL,
								NULL, 
								getGenericServingStaticDir(), 
								authProductName, 
								commandCategoryFilter))
		{
			StatusReporting_SetGenericPortNum(i);
			bStarted = true;
			break;
		}
	}

	if (!bStarted && !sbAllowGenericHttpServingFailure)
	{
		assertmsgf(0, "Unable to do generic http serving on port %d%s, this is fatal. If you wish to allow this, add -AllowGenericHttpServingFailure",
			iPortNum, siPortRange ? " (plus range)" : "");
	}

	return bStarted;
}

bool GenericHttpServing_Begin(int iPortNum, const char *authProductName, const char *commandCategoryFilter, GenericHttpServingFlags eFlags)
{
	return GenericHttpServing_BeginInternal(iPortNum, authProductName, commandCategoryFilter, NULL, NULL, NULL, eFlags);
}

bool GenericHttpServing_BeginCBs(	int iPortNum, 
									const char *authProductName, 
									const char *commandCategoryFilter, 
									HttpServingXPathCallback XPathCB,
									HttpServingCommandCallback CmdCB,
									HttpServingJpegCallback jpgCB,
									GenericHttpServingFlags eFlags)
{
	return GenericHttpServing_BeginInternal(iPortNum, authProductName, commandCategoryFilter, XPathCB, CmdCB, jpgCB, eFlags);
}


void GenericHttpServing_Tick(void)
{
	HttpServing_Tick();
}

void GenericHttpServer_Activity(void)
{
	siLastActivityTime = timeSecondsSince2000();
}

U32 GenericHttpServer_GetLastActivityTime(void)
{
	return siLastActivityTime;
}


