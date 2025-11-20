#include "file.h"
#include "earray.h"
#include "utils.h"
#include "utilitiesLib.h"
#include "time.h"
#include "wininclude.h"
#include "rand.h"
#include "logging.h"
#include "textParser.h"
#include "GslUtils_c_ast.h"
#include "stringUtil.h"
#include "GslUtils.h"

#define CATEGORY_DISTRIBUTION_FILE "server/CategoryDistributionForLogStressTest.txt"

//log server stress test mode... gameservers load in a bunch of actual log lines which they use as
//a template to randomly generate log lines at a tunable rate, to stress test the log server

static char **sppLoadedTemplateLogLines = NULL;
static int siLogsPerSecond = 0;
static float sfLogsPerMillisecond = 0.0f;

//munge this percentage of alphanum/digit characters in order to preserve entropy
static float sfMungePercent = 0.15f;


static U32 *spCategoryRandomizationTable = NULL;

char randomAlpha(void)
{
	int iInt = randomIntRange(0, 51);
	if (iInt > 25)
	{
		return 'a' - 26 + iInt;
	}

	return 'A' + iInt;
}

char randomDigit(void)
{
	return '0' + randomIntRange(0,9);
}

enumLogCategory GetRandomLogCategory(void)
{
	if (spCategoryRandomizationTable)
	{
		int iIndex = randomIntRange(0, ea32Size(&spCategoryRandomizationTable) - 1);
		return spCategoryRandomizationTable[iIndex];
	}

	return randomIntRange(0, LOG_LAST - 1);
}

static void DoALog(void)
{
	static char *spTestLine = NULL;
	int iIndex = randomIntRange(0, eaSize(&sppLoadedTemplateLogLines) - 1);
	int i;
	int iLen;
	char *pRawLine = sppLoadedTemplateLogLines[iIndex] + 24;
	char *pColon = strchr(pRawLine, ':');
	assertmsgf(pColon, "Template log line for stress test badly formatted: %s", pRawLine);
	estrCopy2(&spTestLine, pColon + 2);
	iLen = strlen(spTestLine);

	for (i = 0; i < iLen; i++)
	{
		char c = spTestLine[i];
		if (isalpha(c) && randomPositiveF32() < sfMungePercent)
		{
			spTestLine[i] = randomAlpha();
		}
		else if (isdigit(c) && randomPositiveF32() < sfMungePercent)
		{
			spTestLine[i] = randomDigit();
		}
	}

	log_printf(GetRandomLogCategory(), "STRESSTEST: %s", spTestLine);

}

static void LogServerStressTestModeTickFunction(void)
{
	static S64 siLastTime = 0;
	S64 iTimePassed;
	S64 iCurTime;

	float fLogsPerThisTimePeriod;
	double fProbabilityOfALog;
	U32 iProbabilityOfALog_Billions;
	int iCounter = 0;

	if (!siLastTime)
	{
		siLastTime = timeGetTime();
		return;
	}

	iCurTime = timeGetTime();
	iTimePassed = iCurTime - siLastTime;
	siLastTime = iCurTime;

	fLogsPerThisTimePeriod = sfLogsPerMillisecond * iTimePassed;

	//if fLogsPerThisTimePeriod > 5 then abandon the fancy random math and do less fancy random math
	//due to the RNG breaking down 
	if (fLogsPerThisTimePeriod > 5.0f)
	{
		int i;
		int iNumToDo = fLogsPerThisTimePeriod * (randomPositiveF32() * 0.4f + 0.8f);

		for (i = 0; i < iNumToDo; i++)
		{
			DoALog();
		}
	}
	else
	{



		fProbabilityOfALog = ((double)fLogsPerThisTimePeriod) / (double)(1.0f + fLogsPerThisTimePeriod);

		//some clever math here... suppose we want an average of 1 log per millisecond. The probability will be 0.5 (1 / 1+1). But
		//there's always a chance of more logs, as we check the probability over and over again. So half the time there will be zero logs,
		//half the time there will be at least one log. But half of THAT time there will be two logs, and half of THAT time there
		//will be three logs, etc.

		iProbabilityOfALog_Billions = 1000000000 * fProbabilityOfALog;

		//cap at 100 per tick just to be safe
		while ((U32)randomIntRange(0, 1000000000) < iProbabilityOfALog_Billions && iCounter++ < 100)
		{
			DoALog();
		}
	}
}

AUTO_STRUCT;
typedef struct CategoryDistributionNode
{
	enumLogCategory eCategory;
	int iCount;
	int iRatio; 
} CategoryDistributionNode;


static void LoadCategoryDistributions(void)
{
	char *pBuf = fileAlloc(CATEGORY_DISTRIBUTION_FILE, NULL);
	char **ppLines = NULL;
	S64 iTotal = 0;
	S64 iTotalRatio = 0;
	
	CategoryDistributionNode **ppNodes = NULL;

	DivideString(pBuf, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);
	
	FOR_EACH_IN_EARRAY(ppLines, char, pLine)
	{
		char **ppWords = NULL;
		CategoryDistributionNode *pNode;
		DivideString(pLine, " ", &ppWords, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

		assertmsgf(eaSize(&ppWords) == 2, "Unable to parse line %s from %s", pLine, CATEGORY_DISTRIBUTION_FILE);
		
		pNode = StructCreate(parse_CategoryDistributionNode);
		pNode->eCategory = StaticDefineInt_FastStringToInt(enumLogCategoryEnum, ppWords[0], -1);

		assertmsgf(pNode->eCategory != -1, "Unrecognized category %s from %s", ppWords[0], CATEGORY_DISTRIBUTION_FILE);

		if (!StringToInt_Paranoid(ppWords[1], &pNode->iCount))
		{
			assertmsgf(0, "Unable to parse line %s from %s", pLine, CATEGORY_DISTRIBUTION_FILE);
		}

		eaPush(&ppNodes, pNode);

		iTotal += pNode->iCount;

		eaDestroyEx(&ppWords, NULL);
	}
	FOR_EACH_END;

	eaDestroyEx(&ppLines, NULL);

	FOR_EACH_IN_EARRAY(ppNodes, CategoryDistributionNode, pNode)
	{
		int i;

		pNode->iRatio = ((S64)(pNode->iCount)) * 1000 / iTotal;
		if (pNode->iRatio == 0)
		{
			pNode->iRatio = 1;
		}

		for (i = 0; i < pNode->iRatio; i++)
		{
			ea32Push(&spCategoryRandomizationTable, pNode->eCategory);
		}
	
	}
	FOR_EACH_END;

	eaDestroyStruct(&ppNodes, parse_CategoryDistributionNode);
}
AUTO_COMMAND_REMOTE;
void BeginLogServerStressTestMode(int iLogsPerSecond)
{
	if (!sppLoadedTemplateLogLines)
	{
		char *pBuf = fileAlloc("server/SampleLogsForStressTest.txt", NULL);
		assertmsgf(pBuf, "Couldn't load server/SampleLogsForStressTest.txt... can't do log server stress test");
		DivideString(pBuf, "\n", &sppLoadedTemplateLogLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);
		free(pBuf);

		UtilitiesLib_AddExtraTickFunction(LogServerStressTestModeTickFunction);

		if (fileExists(CATEGORY_DISTRIBUTION_FILE))
		{
			LoadCategoryDistributions();
		}
	}

	siLogsPerSecond = iLogsPerSecond;
	sfLogsPerMillisecond = ((float)siLogsPerSecond) / 1000.0f;
}

bool GslIsDoingLoginServerStressTest(void)
{
	return !!sppLoadedTemplateLogLines;
}

#include "GslUtils_c_ast.c"
