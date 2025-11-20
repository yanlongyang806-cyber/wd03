#include "LogParserFrontEndFiltering.h"
#include "file.h"
#include "wininclude.h"
#include "utils.h"
#include "logParsing.h"
#include "LogParser.h"
#include "LogParser_h_ast.h"


bool gbDoFrontEndFiltering = false;

static char *spFrontEndFilteringDirectory = NULL;
static int siFrontEndFilteringMSecondsPerFile = 0;

static FILE *spCurrentFile = NULL;
static U64 siCurrentFileCreationTime = 0;
static int siCurrentFileIndex = 0;

static U32 siLogTimeAtCurrentFileOpen = 0;
static U64 siTotalLogsAtCurrentFileOpen = 0;
static int siNumLogsAcceptedThisFile = 0;

static void WriteFooter(FILE *pFile)
{
	U32 siCurLogFileTime = LogParsing_GetMostRecentLogTimeRead();
	U64 siCurTotalLogsRead = LogParsing_GetTotalLogsRead();
	
	char time1[128];
	char time2[128];

	if (siLogTimeAtCurrentFileOpen == 0)
	{
		sprintf(time1, "(Never)");
	}
	else
	{
		timeMakeLocalDateStringFromSecondsSince2000(time1, siLogTimeAtCurrentFileOpen);
	}

	timeMakeLocalDateStringFromSecondsSince2000(time2, siCurLogFileTime);

	fprintf(pFile, "//Filter found %d/%d logs with timestamps from %s to %s\n",
		siNumLogsAcceptedThisFile, (int)(siCurTotalLogsRead - siTotalLogsAtCurrentFileOpen), time1, time2);
}

void FrontEndFiltering_Tick(void)
{
	U64 iCurTime = timeGetTime();

	if (spCurrentFile && (iCurTime - siCurrentFileCreationTime) > siFrontEndFilteringMSecondsPerFile)
	{
		WriteFooter(spCurrentFile);
		fclose(spCurrentFile);
		spCurrentFile = NULL;
	}

	if (!spCurrentFile)
	{
		char curFileName[CRYPTIC_MAX_PATH];
		siCurrentFileIndex++;
		siCurrentFileCreationTime = iCurTime;
		sprintf(curFileName, "%s\\Logs_%06d.txt", spFrontEndFilteringDirectory, siCurrentFileIndex);
		if (siCurrentFileIndex == 1)
		{
			mkdirtree_const(curFileName);
		}
		spCurrentFile = fopen(curFileName, "wt");
		assertmsgf(spCurrentFile, "Unable to open %s for FrontEndFiltering", curFileName);

		siNumLogsAcceptedThisFile = 0;
		siLogTimeAtCurrentFileOpen = LogParsing_GetMostRecentLogTimeRead();
		siTotalLogsAtCurrentFileOpen = LogParsing_GetTotalLogsRead();
	}
	else if (!siLogTimeAtCurrentFileOpen)
	{
		siLogTimeAtCurrentFileOpen = LogParsing_GetMostRecentLogTimeRead();
	}
}

void FrontEndFiltering_AddLog(ParsedLog *pLog)
{
	FrontEndFiltering_Tick();
	siNumLogsAcceptedThisFile++;
	fprintf(spCurrentFile, "%s", pLog->pRawLogLine);
}

AUTO_COMMAND;
void FrontEndFiltering_Start(char *pOutDir, int iSecondsPerFile, char *pStandAloneOptions_SuperEsc)
{
	char *pUnEscaped = NULL;
	gbDoFrontEndFiltering = true;
	estrCopy2(&spFrontEndFilteringDirectory, pOutDir);
	siFrontEndFilteringMSecondsPerFile = iSecondsPerFile * 1000;
	StructInit(parse_LogParserStandAloneOptions, &gStandAloneOptions);

	estrSuperUnescapeString(&pUnEscaped, pStandAloneOptions_SuperEsc);

	ParserReadText(pUnEscaped, parse_LogParserStandAloneOptions, &gStandAloneOptions, 0);

	estrDestroy(&pUnEscaped);

	gStandAloneOptions.bCreateFilteredLogFile = true;
}

void FrontEndFiltering_Done(void)
{
	if (spCurrentFile)
	{
		char filename[CRYPTIC_MAX_PATH];
		WriteFooter(spCurrentFile);
		fclose(spCurrentFile);
		sprintf(filename, "%s\\done.txt", spFrontEndFilteringDirectory);
		spCurrentFile = fopen(filename, "wt");
		if (spCurrentFile)
		{
			fprintf(spCurrentFile, "All done");
			fclose(spCurrentFile);
		}

		exit(0);
	}
}