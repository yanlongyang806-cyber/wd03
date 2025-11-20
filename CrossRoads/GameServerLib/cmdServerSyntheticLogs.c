/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "cmdServerCharacter.h"
#include "file.h"
#include "timing.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "StringUtil.h"
#include "Regex.h"

#include "AutoGen/cmdServerSyntheticLogs_c_ast.h"

AUTO_ENUM;
typedef enum SyntheticLogDataFixupType
{
	SyntheticLogDataFixupType_Replace,
	SyntheticLogDataFixupType_Append,
	SyntheticLogDataFixupType_Fixup,
	SyntheticLogDataFixupType_FixupEnum
} SyntheticLogDataFixupType;

AUTO_STRUCT;
typedef struct SyntheticLogDataFixup {
	bool bBreakIfDebuggerPresent;			AST(NAME(BreakIfDebuggerPresent))
	bool bBreakIfDebuggerPresentAndMatched;	AST(NAME(BreakIfDebuggerPresentAndMatched))
	bool bPrintIfMatched;					AST(NAME(PrintIfMatched))

	char *pcRegex;							AST(NAME(Regex))
	SyntheticLogDataFixupType type;			AST(NAME(Type))
	char *pcEnumName;						AST(NAME(EnumName))
} SyntheticLogDataFixup;

AUTO_STRUCT;
typedef struct SyntheticLogDataReplicate {
	GlobalType eGlobalType;								AST(NAME(GlobalType))
	bool bReplicate;									AST(NAME(Replicate))
	bool bBreakIfDebuggerPresent;						AST(NAME(BreakIfDebuggerPresent))

	SyntheticLogDataFixup **eaSyntheticLogDataFixup;	AST(NAME(Fixup))

	U64 iTransactionsProcessed;							AST(NAME(TransactionsProcessed))
	U64 iTransactionsCreated;							AST(NAME(TransactionsCreated))
} SyntheticLogDataReplicate;

AUTO_STRUCT;
typedef struct SyntheticLogData {
	char *pcInputFolder;										AST(NAME(InputFolder))
	char *pcOutputFolder;										AST(NAME(OutputFolder))

	U32 iMultiplier;											AST(NAME(Multiplier))
	U32 iOffset;												AST(NAME(Offset))

	SyntheticLogDataReplicate **eaSyntheticLogDataReplicate;	AST(NAME(Replicate))

	U64 iTransactionsProcessed;									AST(NAME(TransactionsProcessed))
	U64 iTransactionsCreated;									AST(NAME(TransactionsCreated))

	FileWrapper *pCurrentOutputFile;							NO_AST
	U64 iCurrentSequenceNumber;									NO_AST
} SyntheticLogData;

static SyntheticLogData *s_pSyntheticLogData = NULL;

static void SyntheticLogDataCB(const char *command, U64 sequence, U32 timestamp)
{
	U32 uType;
	U32 uID;
	const char *pStr = NULL;
	U32 i;
	char timestampStr[64];
	SyntheticLogDataReplicate *pSyntheticLogDataReplicate = NULL;

	if(nullStr(command) || !strStartsWith(command, "dbUpdateContainer "))
		return;

	pStr = strchr_fast(command, ' ');
	pStr++;
	uType = atoi(pStr);

	pStr = strchr_fast(pStr, ' ');
	pStr++;
	uID = atoi(pStr);

	pStr = strchr_fast(pStr, ' ');
	pStr++;

	FOR_EACH_IN_EARRAY_FORWARDS(s_pSyntheticLogData->eaSyntheticLogDataReplicate, SyntheticLogDataReplicate, pSyntheticLogDataReplicateIter)
	{
		if(uType == (U32)pSyntheticLogDataReplicateIter->eGlobalType)
		{
			pSyntheticLogDataReplicate = pSyntheticLogDataReplicateIter;
			break;
		}
	}
	FOR_EACH_END;

	if(!pSyntheticLogDataReplicate)
	{
		pSyntheticLogDataReplicate = StructCreate(parse_SyntheticLogDataReplicate);
		pSyntheticLogDataReplicate->eGlobalType = uType;
		eaPush(&s_pSyntheticLogData->eaSyntheticLogDataReplicate, pSyntheticLogDataReplicate);
	}

	s_pSyntheticLogData->iTransactionsProcessed++;
	pSyntheticLogDataReplicate->iTransactionsProcessed++;

	timeMakeDateStringFromSecondsSince2000(timestampStr, timestamp);

	{
		char *pNewStr = NULL;
		char *pTempStr = NULL;

		if(pSyntheticLogDataReplicate->bBreakIfDebuggerPresent && IsDebuggerPresent())
			_DbgBreak();

		for(i = 0; i < s_pSyntheticLogData->iMultiplier; i++)
		{
			U32 uNewID = uID + s_pSyntheticLogData->iOffset * i;

			if(!pSyntheticLogDataReplicate->bReplicate && i > 0)
				break;

			if(pSyntheticLogDataReplicate->eaSyntheticLogDataFixup)
			{
				bool bPrint = false;
				if(!pNewStr)
					estrStackCreateSize(&pNewStr, 512);

				estrCopy2(&pNewStr, pStr);
				FOR_EACH_IN_EARRAY_FORWARDS(pSyntheticLogDataReplicate->eaSyntheticLogDataFixup, SyntheticLogDataFixup, pSyntheticLogDataFixup)
				{
					char *pStrIter = pNewStr;
					int iNumMatches;
					int pMatches[100];

					if(pSyntheticLogDataFixup->bBreakIfDebuggerPresent && IsDebuggerPresent())
						_DbgBreak();

					do
					{
						iNumMatches = regexMatch(pSyntheticLogDataFixup->pcRegex, pStrIter, pMatches);
						if(iNumMatches >= 2)
						{
							if(pSyntheticLogDataFixup->bBreakIfDebuggerPresentAndMatched && IsDebuggerPresent())
								_DbgBreak();
							bPrint = bPrint || pSyntheticLogDataFixup->bPrintIfMatched;

							if(!pTempStr)
								estrStackCreateSize(&pTempStr, 512);

							if(SyntheticLogDataFixupType_Replace == pSyntheticLogDataFixup->type)
								estrConcatf(&pTempStr, "%.*s%d%.*s", pMatches[2], pStrIter, uNewID, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
							else if(SyntheticLogDataFixupType_Append == pSyntheticLogDataFixup->type)
								estrConcatf(&pTempStr, "%.*s%d%.*s", pMatches[3], pStrIter, uNewID, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
							else if(SyntheticLogDataFixupType_Fixup == pSyntheticLogDataFixup->type)
							{
								char buf[64];
								U32 uMatchID;
								U32 uActualID;

								strncpy(buf, pStrIter + pMatches[2], pMatches[3] - pMatches[2]);
								uMatchID = atol(buf);
								uActualID = uMatchID + s_pSyntheticLogData->iOffset * i;

								estrConcatf(&pTempStr, "%.*s%d%.*s", pMatches[2], pStrIter, uActualID, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
							}
							else if(SyntheticLogDataFixupType_FixupEnum == pSyntheticLogDataFixup->type && !nullStr(pSyntheticLogDataFixup->pcEnumName))
							{
								char buf[128];
								U32 uMatchInt;

								StaticDefineInt *pStaticDefineInt = FindNamedStaticDefine(pSyntheticLogDataFixup->pcEnumName);
								if(pStaticDefineInt)
								{
									strncpy(buf, pStrIter + pMatches[2], pMatches[3] - pMatches[2]);
									if(sscanf(buf, "%d", &uMatchInt))
									{
									
										const char *pcEnumStr = StaticDefineIntRevLookupNonNull(pStaticDefineInt, uMatchInt);
										estrConcatf(&pTempStr, "%.*s\"%s\"%.*s", pMatches[2], pStrIter, pcEnumStr, pMatches[1] - pMatches[3], pStrIter + pMatches[3]);
									}
									else
										estrConcatf(&pTempStr, "%.*s", pMatches[1], pStrIter);
								}
								else
									estrConcatf(&pTempStr, "%.*s", pMatches[1], pStrIter);
							}
							else
								estrConcatf(&pTempStr, "%.*s", pMatches[1], pStrIter);

							pStrIter += pMatches[1];
						}
					} while(iNumMatches >= 2);

					estrAppend2(&pTempStr, pStrIter);
					estrCopy2(&pNewStr, pTempStr);
					estrClear(&pTempStr);
				}
				FOR_EACH_END;

				if(bPrint)
					fprintf(fileGetStderr(), "%s\n", pNewStr);

				fprintf(s_pSyntheticLogData->pCurrentOutputFile, "%llu %s: dbUpdateContainer %u %u %s",
					s_pSyntheticLogData->iCurrentSequenceNumber, timestampStr, uType, uNewID, pNewStr);
				s_pSyntheticLogData->iCurrentSequenceNumber++;
				s_pSyntheticLogData->iTransactionsCreated++;
				pSyntheticLogDataReplicate->iTransactionsCreated++;
			}
			else
			{
				fprintf(s_pSyntheticLogData->pCurrentOutputFile, "%llu %s: dbUpdateContainer %u %u %s",
					s_pSyntheticLogData->iCurrentSequenceNumber, timestampStr, uType, uNewID, pStr);
				s_pSyntheticLogData->iCurrentSequenceNumber++;
				s_pSyntheticLogData->iTransactionsCreated++;
				pSyntheticLogDataReplicate->iTransactionsCreated++;
			}
		}
		estrDestroy(&pNewStr);
		estrDestroy(&pTempStr);
	}
}

static bool CreateSyntheticLogFileCB(const char *filename)
{
	char output_filename[CRYPTIC_MAX_PATH];

	char *base_filename = strrchr(filename, '\\');
	base_filename++;
	sprintf(output_filename, "%s\\%s", s_pSyntheticLogData->pcOutputFolder, base_filename);

	if(0 == stricmp(output_filename, filename))
	{
		fprintf(fileGetStderr(), "ABORT: Could not create synthetic incremental log file for writing because it is the same file as the one we are reading: %s\n", output_filename);
		return false;
	}

	if(s_pSyntheticLogData->pCurrentOutputFile)
		fclose(s_pSyntheticLogData->pCurrentOutputFile);

	s_pSyntheticLogData->pCurrentOutputFile = fopen(output_filename, "w");
	if(!s_pSyntheticLogData->pCurrentOutputFile)
	{
		fprintf(fileGetStderr(), "ABORT: Could not create synthetic incremental log file for writing: %s\n", output_filename);
		return false;
	}

	fprintf(fileGetStderr(), "Creating synthetic incremental log: %s\n", output_filename);

	return true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(CreateSyntheticLogData);
void cmdCreateSyntheticLogData(const char *createSyntheticLogData_filename)
{
	U32 iStartTimeSecs = timerCpuSeconds();
	char **eaFiles = NULL;
	bool bPrevStrictMerge = gContainerSource.strictMerge;

	s_pSyntheticLogData = StructCreate(parse_SyntheticLogData);	
	s_pSyntheticLogData->iCurrentSequenceNumber = 1;

	printf("Using ObjectDB to create synthetic log data for performance testing.\n");

	if(PARSERESULT_ERROR == ParserReadTextFile(createSyntheticLogData_filename, parse_SyntheticLogData, s_pSyntheticLogData, 0))
	{
		fprintf(fileGetStderr(), "ABORT: Could not parse SyntheticLogData file: %s.\n", createSyntheticLogData_filename);
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(nullStr(s_pSyntheticLogData->pcInputFolder))
	{
		fprintf(fileGetStderr(), "ABORT: No InputFolder in SyntheticLogData! This command line option must refer to a folder containing inc_pending.log files to expand.\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(nullStr(s_pSyntheticLogData->pcOutputFolder))
	{
		fprintf(fileGetStderr(), "ABORT: No OutputFolder specified in SyntheticLogData! This command line option must refer to a folder where the expanded log files should be written.\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(0 == stricmp(s_pSyntheticLogData->pcInputFolder, s_pSyntheticLogData->pcOutputFolder))
	{
		fprintf(fileGetStderr(), "ABORT: InputFolder and OutputFolder must refer to separate paths!\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	if(0 == s_pSyntheticLogData->iMultiplier)
	{
		fprintf(fileGetStderr(), "ABORT: SyntheticLogData multiplier value must be greater than 0.\n");
		StructDestroy(parse_SyntheticLogData, s_pSyntheticLogData);
		return;
	}

	s_pSyntheticLogData->iTransactionsProcessed = 0;
	s_pSyntheticLogData->iTransactionsCreated = 0;

	fprintf(fileGetStderr(), "Multiplier of %u will be used to expand logs for containers marked to be replicated.\n", s_pSyntheticLogData->iMultiplier);

	objSetCommandLogFileReplayCallback(CreateSyntheticLogFileCB);
	objSetCommandReplayCallback(SyntheticLogDataCB);
	gContainerSource.strictMerge = true;

	eaFiles = FindReplayLogs(s_pSyntheticLogData->pcInputFolder);
	EARRAY_FOREACH_BEGIN(eaFiles, i);
	{
		objReplayLog(eaFiles[i]);
	}
	EARRAY_FOREACH_END;
	eaDestroyEx(&eaFiles, NULL);

	objSetCommandLogFileReplayCallback(NULL);
	objSetCommandReplayCallback(NULL);
	gContainerSource.strictMerge = bPrevStrictMerge;

	fprintf(fileGetStderr(), "Processed %llu and created %llu transactions. (%u seconds)\n",
		s_pSyntheticLogData->iTransactionsProcessed, s_pSyntheticLogData->iTransactionsCreated, timerCpuSeconds() - iStartTimeSecs);

	ParserWriteTextFile(createSyntheticLogData_filename, parse_SyntheticLogData, s_pSyntheticLogData, 0, 0);

	if(s_pSyntheticLogData->pCurrentOutputFile)
		fclose(s_pSyntheticLogData->pCurrentOutputFile);

	StructDestroySafe(parse_SyntheticLogData, &s_pSyntheticLogData);
}

#include "AutoGen/cmdServerSyntheticLogs_c_ast.c"
