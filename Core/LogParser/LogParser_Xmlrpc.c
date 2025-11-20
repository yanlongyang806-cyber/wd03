// XML-RPC interface to LogParser

#include "earray.h"
#include "EString.h"
#include "GenericHttpServing.h"
#include "LogParser.h"
#include "LogParserFilteredLogFile.h"
#include "LogParser_Xmlrpc_c_ast.h"
#include "timing_profiler.h"
#include "LogParserLaunching.h"
#include "sock.h"

// Number of results to return per request.
// Presently, the controller allocates a single memory block to relay an XML-RPC response, so the result needs to be small
// enough so that the controller doesn't run out of memory.
static const U64 uResultsPerRequest = 1024;

// Array of all log lines parsed.
AUTO_STRUCT;
typedef struct GetScanLogLinesResponse
{
	const char *pStatus;			AST(UNOWNED)
	int uPercent;
	STRING_EARRAY ppLogs;			AST(ESTRING)
	U64 uSize;
	U64 uBase;
	U64 uBound;
} GetScanLogLinesResponse;

// Return an array of log lines.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
GetScanLogLinesResponse *GetScanLogLines(U64 uBase)
{
	const char *const *logs;
	GetScanLogLinesResponse *response;
	U64 i;

	PERFINFO_AUTO_START_FUNC();

	// Restart the LogParser timeout timer.
	GenericHttpServer_Activity();

	// Make sure processing is finished.
	response = StructCreate(parse_GetScanLogLinesResponse);

	if (!gStandAloneOptions.bCreateFilteredLogFile)
	{
		response->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return response;
	}

	if (gbCurrentlyScanningDirectories)
	{
		response->pStatus = "PROCESS";
		response->uPercent = giDirScanningPercent;
		PERFINFO_AUTO_STOP();
		return response;
	}

	response->pStatus = "OK";

	if (!gStandAloneOptions.bWebFilteredScan)
	{
		PERFINFO_AUTO_STOP();
		return response;
	}

	// Page logs to return.
	logs = GetFilteredLogMemory();
	response->uSize = eaSize(&logs);
	response->uBase = uBase;
	if (response->uBase >= response->uSize)
	{
		PERFINFO_AUTO_STOP();
		return response;
	}
	response->uBound = MIN(response->uSize, uBase + uResultsPerRequest);
	devassert(response->uBound >= response->uBase && response->uBound - response->uBase <= uResultsPerRequest);

	// Copy logs.
	eaSetSize(&response->ppLogs, response->uBound - response->uBase);
	for (i = response->uBase; i != response->uBound; ++i)
		response->ppLogs[i - response->uBase] = estrDup(logs[i]);

	PERFINFO_AUTO_STOP();
	return response;
}

// Parameters for web filtering run.
AUTO_STRUCT;
typedef struct RunFilteredScanInfo
{
	U32 PlayerId;
	U32 AccountId;
	U32 StartingTime;
	U32 EndingTime;
	const char *Categories;

	// Allow a filtered scan with no specified playerid/accountid
	bool AllowNoObject;

	// Use these flags to change the filtered scan mode to use a file download
	// Normally a filtered scan requested this way is run as a "web filtered scan" which is stored in memory
	// "AllowDownload" reverts to normal filtered log files, so we don't have to request large log parses in 1024 chunks
	// "CompressDownload" activates the uncapped compressed mode, so we can request arbitrary numbers of logs
	bool AllowDownload;
	bool CompressDownload;
} RunFilteredScanInfo;

// Result of RunFilteredScan().
AUTO_STRUCT;
typedef struct RunFilteredScanResponse
{
	const char *pStatus;			AST(UNOWNED)
} RunFilteredScanResponse;

// Start a LogParser run.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
RunFilteredScanResponse *RunFilteredScan(RunFilteredScanInfo *pInfo)
{
	RunFilteredScanResponse *response;

	PERFINFO_AUTO_START_FUNC();

	// Restart the LogParser timeout timer.
	GenericHttpServer_Activity();

	response = StructCreate(parse_RunFilteredScanResponse);
	if (!gbStandAlone || gbCurrentlyScanningDirectories || !(pInfo->AllowNoObject || pInfo->PlayerId || pInfo->AccountId) )
	{
		response->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return response;
	}

	RunWebFilter(pInfo->PlayerId, pInfo->AccountId, pInfo->StartingTime, pInfo->EndingTime, pInfo->Categories, pInfo->AllowDownload, pInfo->CompressDownload);

	// Run scan.
	BeginDirectoryScanning();

	response->pStatus = "OK";
	PERFINFO_AUTO_STOP();
	return response;
}

// Result of AbortFilteredScan().
AUTO_STRUCT;
typedef struct AbortFilteredScanResponse
{
	const char *pStatus;			AST(UNOWNED)
} AbortFilteredScanResponse;

// Abort a LogParser run.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
AbortFilteredScanResponse *AbortFilteredScan()
{
	AbortFilteredScanResponse *response;

	PERFINFO_AUTO_START_FUNC();

	// Restart the LogParser timeout timer.
	GenericHttpServer_Activity();

	response = StructCreate(parse_AbortFilteredScanResponse);
	if (!gbCurrentlyScanningDirectories)
	{
		response->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return response;
	}
	AbortDirectoryScanning();
	response->pStatus = "OK";

	PERFINFO_AUTO_STOP();
	return response;
}

// Result of KillStandAloneLogParser().
AUTO_STRUCT;
typedef struct KillStandAloneLogParserResponse
{
	const char *pStatus;			AST(UNOWNED)
} KillStandAloneLogParserResponse;

// Kill a stand-alone LogParser.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
KillStandAloneLogParserResponse *KillStandAloneLogParser()
{
	KillStandAloneLogParserResponse *response;

	PERFINFO_AUTO_START_FUNC();

	// Restart the LogParser timeout timer.
	GenericHttpServer_Activity();

	response = StructCreate(parse_KillStandAloneLogParserResponse);

	if(!gbStandAlone)
	{
		response->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return response;
	}
	
	AbortDirectoryScanning();

	gbStandAloneForceExit = true;

	response->pStatus = "OK";

	PERFINFO_AUTO_STOP();
	return response;
}

// Result of LaunchWebFilterStandAlone().
AUTO_STRUCT;
typedef struct LaunchWebFilterStandAloneResponse
{
	const char *pStatus;			AST(UNOWNED)
	char *pLocal;				AST(ESTRING)
	char *pPublic;				AST(ESTRING)
} LaunchWebFilterStandAloneResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
LaunchWebFilterStandAloneResponse *LaunchWebFilterStandAlone(RunFilteredScanInfo *pInfo)
{
	LaunchWebFilterStandAloneResponse *response;
	char *pExtraArgs = NULL;
	U32 iPort;

	PERFINFO_AUTO_START_FUNC();

	response = StructCreate(parse_LaunchWebFilterStandAloneResponse);

	if(gbStandAlone)
	{
		response->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return response;
	}

	estrStackCreate(&pExtraArgs);
	if(pInfo->Categories)
		estrPrintf(&pExtraArgs, "-RunWebFilter %d %d %d %d \"%s\" %d %d", pInfo->PlayerId, pInfo->AccountId, pInfo->StartingTime, pInfo->EndingTime, pInfo->Categories, pInfo->AllowDownload, pInfo->CompressDownload);
	else
		estrPrintf(&pExtraArgs, "-RunWebFilterNoCategories %d %d %d %d %d %d", pInfo->PlayerId, pInfo->AccountId, pInfo->StartingTime, pInfo->EndingTime, pInfo->AllowDownload, pInfo->CompressDownload);
	iPort = LogParserLaunching_LaunchOne(0, pExtraArgs, 0, 0);
	estrDestroy(&pExtraArgs);

	if (!iPort)
	{
		response->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return response;
	}

	estrCreate(&response->pLocal);

	estrPrintf(&response->pLocal, "%s:%d", makeIpStr(getHostLocalIp()), iPort);

	estrCreate(&response->pPublic);

	estrPrintf(&response->pPublic, "%s:%d", makeIpStr(getHostPublicIp()), iPort);

	response->pStatus = "OK";

	PERFINFO_AUTO_STOP();
	return response;
}

// Result of LaunchWebFilterStandAlone().
AUTO_STRUCT;
typedef struct GetStandAloneLogParserListResponse
{
	const char *pStatus;			AST(UNOWNED)
	STRING_EARRAY pStandAloneLogParsers; AST(ESTRING)
	int iCount;
} GetStandAloneLogParserListResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
GetStandAloneLogParserListResponse *GetStandAloneLogParserList()
{
	GetStandAloneLogParserListResponse *response;

	PERFINFO_AUTO_START_FUNC();

	response = StructCreate(parse_GetStandAloneLogParserListResponse);

	if(gbStandAlone)
	{
		response->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return response;
	}

	response->iCount = FillStandAloneList(&response->pStandAloneLogParsers);

	response->pStatus = "OK";

	PERFINFO_AUTO_STOP();
	return response;
}

// Result of GetFilteredLogDownloadLink().
AUTO_STRUCT;
typedef struct GetFilteredLogDownloadLinkResponse
{
	const char *pStatus;		AST(UNOWNED)
	char *pLink;				AST(ESTRING)
} GetFilteredLogDownloadLinkResponse;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
GetFilteredLogDownloadLinkResponse *GetFilteredLogDownloadLink(void)
{
	GetFilteredLogDownloadLinkResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();

	pResponse = StructCreate(parse_GetFilteredLogDownloadLinkResponse);

	if(!gbStandAlone || gbCurrentlyScanningDirectories || !FilteredLogFile_GetRecentFileName())
	{
		pResponse->pStatus = "FAIL";
		PERFINFO_AUTO_STOP();
		return pResponse;
	}

	pResponse->pStatus = "OK";
	estrCopy2(&pResponse->pLink, FilteredLogFile_GetDownloadPath());

	PERFINFO_AUTO_STOP();

	return pResponse;
}

#include "LogParser_Xmlrpc_c_ast.c"
