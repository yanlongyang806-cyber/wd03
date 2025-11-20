#include "LogParser.h"
#include "LogParserFilteredLogFile.h"
#include "FileUtil2.h"
#include "estring.h"
#include "timing.h"
#include "LogParsing_h_ast.h"

#define MAX_LOG_LINES_FOR_FILTERED_LOG_FILE 100000

static FILE *spCurFile = NULL;
static bool sbCurFileCompressed = false;
static char *sMostRecentFileName = NULL;
static int siLogCount = 0;

// In memory mode, filter to a buffer instead of to a file.
static bool bMemoryMode = false;
static STRING_EARRAY eaMemoryLogs = NULL;

// Cap for filtered log file length when uncompressed.
int siMaxLogLinesForFilteredLogFile = MAX_LOG_LINES_FOR_FILTERED_LOG_FILE;
AUTO_CMD_INT(siMaxLogLinesForFilteredLogFile, MaxLogLinesForFilteredLogFile);

void FilteredLogFile_InitSystem(void)
{
	PurgeDirectoryOfOldFiles_Secs("c:\\LogParser\\Downloads", 24 * 60 * 60, NULL, false);
}


void InitFilteredLogFile(bool bCompressed)
{
	static int i = 0;
	static char filename[CRYPTIC_MAX_PATH];
	static char filenameGz[CRYPTIC_MAX_PATH];

	char *pPrefix = NULL;

	AbortFilteredLogFile();
	siLogCount = 0;

	do
	{
		do
		{
			i++;
			sprintf(filename, "c:\\LogParser\\Downloads\\FilteredLogs_%d.txt", i);
			sprintf(filenameGz, "c:\\LogParser\\Downloads\\FilteredLogs_%d.txt.gz", i);
		}
		while(fileExists(filename) || fileExists(filenameGz));

		sMostRecentFileName = bCompressed ? filenameGz : filename;
		makeDirectoriesForFile(sMostRecentFileName);

		if (bCompressed)
		{
			spCurFile = fopen(sMostRecentFileName, "wbz");
			sbCurFileCompressed = true;
		}
		else
		{
			spCurFile = fopen(sMostRecentFileName, "wb");
			sbCurFileCompressed = false;

		}
	}
	while (!spCurFile);

	estrPrintf(&pPrefix, "Filtered Log results generated starting at %s (utc) on %s.\nFiltering:\n", 
		timeGetDateStringFromSecondsSince2000(timeSecondsSince2000()), getHostName());
	ParserWriteText(&pPrefix, parse_LogParsingRestrictions, &gStandAloneOptions.parsingRestrictions, 0, 0, 0);

	estrAddPrefixToLines(&pPrefix, "//");
	estrConcatf(&pPrefix, "%s", "\n");

	fprintf(spCurFile, "%s", pPrefix);
	estrDestroy(&pPrefix);
}

void InitFilteredLogMemory(void)
{
	bMemoryMode = true;
	eaDestroyEString(&eaMemoryLogs);
}

void AbortFilteredLogFile(void)
{
	if (spCurFile)
	{
		fclose(spCurFile);
		spCurFile = NULL;
	}

	sMostRecentFileName = NULL;
}

	
void CloseFilteredLogFile(void)
{
	if (spCurFile)
	{
		fclose(spCurFile);
		spCurFile = NULL;
	}
}

void AddLogToFilteredLogFile(ParsedLog *pLog)
{
	if(!pLog->pRawLogLine)
	{
		// Nothing to write out.
		return;
	}

	if (bMemoryMode)
	{
		PERFINFO_AUTO_START_FUNC();
		if (eaSize(&eaMemoryLogs) < siMaxLogLinesForFilteredLogFile)
			eaPush(&eaMemoryLogs, estrDup(pLog->pRawLogLine));
	}
	else if (siLogCount < siMaxLogLinesForFilteredLogFile || sbCurFileCompressed)
	{
		fprintf(spCurFile, "%s\n", pLog->pRawLogLine);
		siLogCount++;
		if (siLogCount == siMaxLogLinesForFilteredLogFile && !sbCurFileCompressed)
		{
			fprintf(spCurFile, "//LOG FILTERING STOPPED after %d lines\n", siMaxLogLinesForFilteredLogFile);
		}
		PERFINFO_AUTO_STOP();
		return;
	}
}
		

const char *FilteredLogFile_GetRecentFileName(void)
{
	return sMostRecentFileName;
}

const char *FilteredLogFile_GetDownloadPath(void)
{
	static char *pDownloadPath = NULL;
	char tempName[CRYPTIC_MAX_PATH];

	getFileNameNoDir(tempName, FilteredLogFile_GetRecentFileName());
	estrPrintf(&pDownloadPath, "/file/logparser/%u/fileSystem/%s", GetAppGlobalID(), tempName);
	return pDownloadPath;
}

const char *const *GetFilteredLogMemory(void)
{
	return eaMemoryLogs;
}