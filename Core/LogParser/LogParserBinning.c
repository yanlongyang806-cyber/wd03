#include "LogParser.h"
#include "LogParserBinning.h"
#include "file.h"
#include "LogParsing_h_ast.h"
#include "LogParserBinning_c_ast.h"
#include "estring.h"
#include "error.h"
#include "LogParserGraphs.h"
#include "LogParserConstructedObjects.h"
#include "LogParserFilteredLogFile.h"
#include "LogParserAggregator.h"

#define MAX_LOGS_PER_FILE 100000

bool gbLoadingBins = false;

AUTO_STRUCT;
typedef struct ParsedLogList
{
	ParsedLog **ppList;
} ParsedLogList;


static ParsedLogList sLogsForBinning = {0};

static int iNextFileNum = 0;

void LogParserBinning_Reset(char *pDescriptionString)
{
	char systemString[1024];
	if(gStandAloneOptions.pBinnedLogFileDirectory && gStandAloneOptions.pBinnedLogFileDirectory[0])
	{
		sprintf(systemString, "erase %s\\LogParserBins\\%s\\*.*", fileTempDir(), gStandAloneOptions.pBinnedLogFileDirectory);
	}
	else
	{
		sprintf(systemString, "erase %s\\LogParserBins\\*.*", fileTempDir());
	}

	backSlashes(systemString);

	strcat(systemString, " /Q");

	system(systemString);

	iNextFileNum = 0;
	StructDeInit(parse_ParsedLogList, &sLogsForBinning);
}

void LogParserBinning_AddLog(ParsedLog *pInLog)
{
	ParsedLog *pLog = StructClone(parse_ParsedLog, pInLog);

	assert(pLog);

	if (pLog->iParsedLogFlags & PARSEDLOG_FLAG_MESSAGEWASPARSEDINTOSTRUCT)
	{
		estrDestroy(&pLog->pMessage);
	}
	else
	{
		if (estrLength(&pLog->pMessage) > 1000)
		{
			estrSetSize(&pLog->pMessage, 1000);
		}
	}



	eaPush(&sLogsForBinning.ppList, pLog);

	if (eaSize(&sLogsForBinning.ppList) == MAX_LOGS_PER_FILE)
	{
		char fileName[CRYPTIC_MAX_PATH];
		if(gStandAloneOptions.pBinnedLogFileDirectory && gStandAloneOptions.pBinnedLogFileDirectory[0])
		{
			sprintf(fileName, "%s/LogParserBins/%s/Bin_%d.bin", fileTempDir(), gStandAloneOptions.pBinnedLogFileDirectory, iNextFileNum++);
		}
		else
		{
			sprintf(fileName, "%s/LogParserBins/Bin_%d.bin", fileTempDir(), iNextFileNum++);
		}

		printf("About to write %d logs into %s\n", MAX_LOGS_PER_FILE, fileName);
	
		
		ParserWriteBinaryFile(fileName,NULL, parse_ParsedLogList, &sLogsForBinning, NULL, NULL, NULL, NULL, 0, 0, NULL, 0);

		StructDeInit(parse_ParsedLogList, &sLogsForBinning);
	}
}

void LogParserBinning_Close(void)
{
	if (eaSize(&sLogsForBinning.ppList))
	{
		char fileName[CRYPTIC_MAX_PATH];
		if(gStandAloneOptions.pBinnedLogFileDirectory && gStandAloneOptions.pBinnedLogFileDirectory[0])
		{
			sprintf(fileName, "%s/LogParserBins/%s/Bin_%d.bin", fileTempDir(), gStandAloneOptions.pBinnedLogFileDirectory, iNextFileNum++);
		}
		else
		{
			sprintf(fileName, "%s/LogParserBins/Bin_%d.bin", fileTempDir(), iNextFileNum++);
		}

		printf("About to write %d logs into %s\n", eaSize(&sLogsForBinning.ppList), fileName);
	
		
		ParserWriteBinaryFile(fileName,NULL, parse_ParsedLogList, &sLogsForBinning, NULL, NULL, NULL, NULL, 0, 0, NULL, 0);

		StructDeInit(parse_ParsedLogList, &sLogsForBinning);
	}
}
	
void LogParserBinning_GetCurDescriptionString(char **ppEString)
{
	estrPrintf(ppEString, "temp");
}


AUTO_COMMAND ACMD_NAME(LoadLogsFromBin) ACMD_ACCESSLEVEL(0);
void LogParserBinning_ParserAllLogsFromBin(void)
{
	int i, j;
	bool bReactivateBinnedFiles = false;

	if(gStandAloneOptions.bCreateBinnedLogFiles)
	{
		printf("Turning off creation of binned files.\n");
		gStandAloneOptions.bCreateBinnedLogFiles = false;
		bReactivateBinnedFiles = true;
	}
	
	if (gStandAloneOptions.bCreateFilteredLogFile)
	{
		InitFilteredLogFile(gStandAloneOptions.bCompressFilteredLogFile);
	}

	loadstart_printf("Beginning binary load...");

	gbLoadingBins = true;

	Graph_ResetSystem();
	LPCO_ResetSystem();
	LPAggregator_ResetSystem();

	for (i=0; ; i++)
	{
		char fileName[CRYPTIC_MAX_PATH];
		if(gStandAloneOptions.pBinnedLogFileDirectory && gStandAloneOptions.pBinnedLogFileDirectory[0])
		{
			sprintf(fileName, "%s/LogParserBins/%s/Bin_%d.bin", fileTempDir(), gStandAloneOptions.pBinnedLogFileDirectory, i);
		}
		else
		{
			sprintf(fileName, "%s/LogParserBins/Bin_%d.bin", fileTempDir(), i);
		}
		if (!fileExists(fileName))
		{
			break;
		}

		printf("About to load logs from %s\n", fileName);

		ParserOpenReadBinaryFile(NULL, fileName, parse_ParsedLogList, &sLogsForBinning, NULL, NULL, NULL, NULL, 0, 0, 0);

		printf("Loaded... about to parse %d logs\n", eaSize(&sLogsForBinning.ppList));

		for (j=0 ; j < eaSize(&sLogsForBinning.ppList); j++)
		{
			ProcessSingleLog("test.txt", sLogsForBinning.ppList[j], false);
			sLogsForBinning.ppList[j] = NULL;

			ProcessAllProceduralLogs();

		}

		StructDeInit(parse_ParsedLogList, &sLogsForBinning);
	}

	gbLoadingBins = false;

	CloseFilteredLogFile();

	if(bReactivateBinnedFiles)
	{
		printf("Turning creation of binned files back on.\n");
		gStandAloneOptions.bCreateBinnedLogFiles = true;
	}

	loadend_printf("done\n");
}


#include "LogParserBinning_c_ast.c"
