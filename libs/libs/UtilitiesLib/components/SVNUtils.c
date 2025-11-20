#include "SVNUtils.h"
#include "wininclude.h"
#include "fileutil.h"
#include "timing.h"
#include "estring.h"
#include "fileUtil2.h"
#include "utf8.h"

#include "autogen/SVNUtils_h_ast.c"




#if !PLATFORM_CONSOLE

static char *spTempFileName = NULL;


static char *GetSvnExe(void)
{
	static char *spRetVal = NULL;

	if (!spRetVal)
	{	
		estrPrintf(&spRetVal, "svn --username %s --password \"%s\" --no-auth-cache ",
			SVN_GetUserName(), SVN_GetPassword());
	}

	return spRetVal;
}


bool SVN_UpdateFolders(char **ppFolderList, U32 iFailureTime)
{
	char *pSystemString = NULL;
	char *pSharedString = NULL;
	int iRetVal = 0;
	QueryableProcessHandle *pHandle;

	if (eaSize(&ppFolderList))
	{
		U32 iStartingTime;

		estrStackCreate(&pSystemString);
		estrStackCreate(&pSharedString);

		FindSharedPrefix(&pSharedString, ppFolderList);

		assertmsg(estrLength(&pSharedString), "Folders provided to SVN_UpdateFolder must share a prefix so atomic updating can be done");


		estrConcatf(&pSystemString, "%s update %s", GetSvnExe(), GetJunctionNameFromFolderName(pSharedString));

		forwardSlashes(pSystemString);
		

		iStartingTime = timeSecondsSince2000();

		if (!(pHandle = StartQueryableProcess(pSystemString, NULL, true, false, false, NULL)))
		{
			estrDestroy(&pSystemString);
			estrDestroy(&pSharedString);
			return false;
		}

		while (1)
		{

			if (QueryableProcessComplete(&pHandle, &iRetVal))
			{
				break;
			}

			if (iFailureTime && timeSecondsSince2000() - iStartingTime > iFailureTime)
			{
				estrDestroy(&pSystemString);
				estrDestroy(&pSharedString);
				KillQueryableProcess(&pHandle);
				return false;
			}

			Sleep(1);
		}

		estrDestroy(&pSystemString);
		estrDestroy(&pSharedString);
	}


	return (iRetVal == 0);
}



bool SVN_AttemptCleanup(char **ppFolderList, U32 iFailureTime)
{
	char *pSystemString = NULL;
	char *pSharedString = NULL;
	int iRetVal = 0;
	QueryableProcessHandle *pHandle;

	if (eaSize(&ppFolderList))
	{
		U32 iStartingTime;

		estrStackCreate(&pSystemString);
		estrStackCreate(&pSharedString);

		FindSharedPrefix(&pSharedString, ppFolderList);

		assertmsg(estrLength(&pSharedString), "Folders provided to SVN_UpdateFolder must share a prefix so atomic updating can be done");


		estrConcatf(&pSystemString, "%s cleanup %s", GetSvnExe(), GetJunctionNameFromFolderName(pSharedString));

		forwardSlashes(pSystemString);
		

		iStartingTime = timeSecondsSince2000();

		if (!(pHandle = StartQueryableProcess(pSystemString, NULL, true, false, false, NULL)))
		{
			estrDestroy(&pSystemString);
			estrDestroy(&pSharedString);
			return false;
		}

		while (1)
		{

			if (QueryableProcessComplete(&pHandle, &iRetVal))
			{
				break;
			}

			if (iFailureTime && timeSecondsSince2000() - iStartingTime > iFailureTime)
			{
				estrDestroy(&pSystemString);
				estrDestroy(&pSharedString);
				KillQueryableProcess(&pHandle);
				return false;
			}

			Sleep(1);
		}

		estrDestroy(&pSystemString);
		estrDestroy(&pSharedString);
	}


	return (iRetVal == 0);
}



int SVN_GetRevNumOfFolders(char *pFolderNames, char **ppFolderList, char **ppRepositoryURL, U32 iFailureTime)
{
	int iHighestRevNum = 0;
	static char **ppFolderNames = NULL;
	int i;
	char systemString[1024];
	bool bFailed = false;
	
	eaDestroyEx(&ppFolderNames, NULL);

	if (pFolderNames)
	{
		DivideString(pFolderNames, ";", &ppFolderNames, DIVIDESTRING_POSTPROCESS_FORWARDSLASHES);
	}
	else
	{
		//somewhat sillily duplicate the list, just so we don't have to worry about who owns it when
		//we free it
		for (i=0; i < eaSize(&ppFolderList); i++)
		{
			eaPush(&ppFolderNames, strdup(ppFolderList[i]));
			forwardSlashes(ppFolderNames[i]);
		}
	}


	if (!GetTempFileName_UTF8(".", "SPR", 0, &spTempFileName))
	{
		char *pErrorBuf = NULL;
		FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pErrorBuf, NULL);

		assertmsgf(0, "Couldn't get temp filename: error <<%s>>", pErrorBuf);
	}


	for (i=0; i < eaSize(&ppFolderNames); i++)
	{
		char *pReadFile;
		int iFileSize;
		char *pTemp;
		int iCurRevNum;

		sprintf(systemString, "%s info %s > %s", GetSvnExe(), ppFolderNames[i][1] == ':' ? GetJunctionNameFromFolderName(ppFolderNames[i]) : ppFolderNames[i], spTempFileName);
		if (system_w_timeout(systemString, NULL, iFailureTime) != 0)
		{
			bFailed = true;
		}


		pReadFile = fileAlloc(spTempFileName, &iFileSize);

		if (!pReadFile)
		{
			printf("command \"%s\" failed\n", systemString);
			return 0;
		}
	
		pTemp = strstr(pReadFile, "Revision: ");

		if (!pTemp)
		{
			printf("command \"%s\" produced badly formatted output\n", systemString);
			free(pReadFile);
			return 0;
		}

		iCurRevNum = 0;
		sscanf(pTemp, "Revision: %d", &iCurRevNum);

		if (iCurRevNum <= 0)
		{
			printf("command \"%s\" has unreadable rev num\n", systemString);
			free(pReadFile);
			return 0;
		}


		if (iCurRevNum > iHighestRevNum)
		{
			iHighestRevNum = iCurRevNum;
		}

		if (ppRepositoryURL)
		{
			estrClear(ppRepositoryURL);

			pTemp = strstr(pReadFile, "URL: ");
			if (!pTemp)
			{
				printf("command \"%s\" produced badly formatted output\n", systemString);
				free(pReadFile);
				return 0;
			}

			pTemp += 5;

			while (*pTemp != '\n' && *pTemp )
			{
				estrConcatChar(ppRepositoryURL, *pTemp);
				pTemp++;
			}

			estrTrimLeadingAndTrailingWhitespace(ppRepositoryURL);
		}
			
		free(pReadFile);
	}

	sprintf(systemString, "erase %s", spTempFileName);
	system_w_timeout(systemString, NULL, iFailureTime);

	return bFailed ? 0 : iHighestRevNum;
}

bool SVN_ReadCheckinsFromBuffer(char *pBuffer, CheckinInfo ***pppList, U32 iFlags)
{
	CheckinInfo *pInfo;
	char *pNextNewLine, *pNextPipe;
	char *pEndOfComment;
	int i;
	bool bAlreadyInList;

	while (1)
	{
		do
		{
			//hop forward until we find an 'r' at the beginning of a line
			pNextNewLine = strchr(pBuffer, '\n');

			if (!pNextNewLine)
			{
				return true;
			}

			pBuffer = pNextNewLine + 1;
		}
		while (*pBuffer != 'r');

		pInfo = StructCreate(parse_CheckinInfo);

		// Parse the re
		pBuffer += 1;

		while (isdigit(*pBuffer))
		{
			pInfo->iRevNum *= 10;
			pInfo->iRevNum += (*pBuffer - '0');
			pBuffer++;
		}

		if (!pInfo->iRevNum || *pBuffer != ' ')
		{
			StructDestroy(parse_CheckinInfo, pInfo);
			continue;
		}

		pBuffer++;
		if (*pBuffer != '|')
		{
			StructDestroy(parse_CheckinInfo, pInfo);
			continue;
		}

		pBuffer++;

		// Skip whitespace
		while(*pBuffer == ' ') ++pBuffer;

		pNextPipe = strchr(pBuffer, '|');
		if(!pNextPipe)
		{
			StructDestroy(parse_CheckinInfo, pInfo);
			continue;
		}
		// Trim whitespace at the end
		while(*(pNextPipe-1) == ' ') --pNextPipe;

		if (pNextPipe <= pBuffer)
		{
			StructDestroy(parse_CheckinInfo, pInfo);
			continue;
		}

		estrConcat(&pInfo->userName, pBuffer, pNextPipe-pBuffer);

		//skip over next |
		pNextPipe = strchr(pBuffer, '|');

		if (!pNextPipe)
		{
			StructDestroy(parse_CheckinInfo, pInfo);
			continue;
		}

		pBuffer = pNextPipe + 2;

		pInfo->iCheckinTimeSS2000 = timeGetSecondsSince2000FromLocalDateString(pBuffer);

		pNextNewLine = strchr(pBuffer, '\n');

		if (!pNextNewLine)
		{
			StructDestroy(parse_CheckinInfo, pInfo);
			continue;
		}

		pBuffer = pNextNewLine + 1;
		pNextNewLine = strchr(pBuffer, '\n');

		if (!pNextNewLine)
		{
			StructDestroy(parse_CheckinInfo, pInfo);
			continue;
		}
		pBuffer = pNextNewLine + 1;

		//now pBuffer points to the beginning of the checkin comment
		pEndOfComment = strstr(pBuffer, "------------------------------------------------------------------------");

		if (pEndOfComment)
		{
			estrConcat(&pInfo->checkinComment, pBuffer, pEndOfComment - pBuffer);
			pBuffer = pEndOfComment;
		}
		else
		{
			estrCopy2(&pInfo->checkinComment, pBuffer);
			pBuffer += strlen(pBuffer);
		}

		//replace $s in comments because they screw up the CB, which is the prime user of this code
		if (iFlags & SVNGETCHECKINS_FLAG_REPLACE_DOLLARSIGNS)
		{
			estrReplaceOccurrences(&pInfo->checkinComment, "$", "(DollarSign)");
		}

		bAlreadyInList = false;
		for (i=0; i < eaSize(pppList); i++)
		{
			if ((*pppList)[i]->iRevNum == pInfo->iRevNum)
			{
				bAlreadyInList = true;
				break;
			}
		}

		if (bAlreadyInList)
		{
			StructDestroy(parse_CheckinInfo, pInfo);
		}
		else
		{
			eaPush(pppList, pInfo);
		}
	}

	return true;
}

int SortCheckinInfosByTime(const CheckinInfo **pInfo1, const CheckinInfo **pInfo2)
{
	if ((*pInfo1)->iCheckinTimeSS2000 > (*pInfo2)->iCheckinTimeSS2000)
	{
		return -1;
	}
	else if ((*pInfo1)->iCheckinTimeSS2000 < (*pInfo2)->iCheckinTimeSS2000)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

bool SVN_GetCheckins(int iFromRev, int iToRev, char *pFolderNameString, char **ppFolderNameList, char *pRepository, CheckinInfo ***pppList, U32 iFailureTime, U32 iFlags)
{
	char **ppFolderNames = NULL;
	int i;
	char systemString[1024];

	bool bFailed = false;

	if (iFromRev >= iToRev)
	{
		return true;
	}


	if (!GetTempFileName_UTF8(".", "SPR", 0, &spTempFileName))
	{
		char *pErrorBuf = NULL;
		FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pErrorBuf, NULL);

		assertmsgf(0, "Couldn't get temp filename: error <<%s>>", pErrorBuf);
	}

	if (pRepository)
	{

		char *pReadFile;
		int iFileSize;
		bool bResult;

		sprintf(systemString, "%s log %s -r %d:%d > %s", GetSvnExe(), pRepository, iFromRev + 1, iToRev, spTempFileName);
		if (system_w_timeout(systemString, NULL, iFailureTime) != 0)
		{
			printf("timeout failure while executing %s", systemString);
			bFailed = true;
		}
		else
		{
			pReadFile = fileAlloc(spTempFileName, &iFileSize);

			if (!pReadFile)
			{
				printf("Failure while executing command \"%s\"\n", systemString);
				bFailed = true;
			}
			else
			{
				bResult = SVN_ReadCheckinsFromBuffer(pReadFile, pppList, iFlags);

				if (!bResult)
				{
					printf("Error parsing SVN log file (command %s)\n", systemString);
					bFailed = true;
				}

				free(pReadFile);
			}
		}
	}
	else
	{

		if (pFolderNameString)
		{
	
			DivideString(pFolderNameString, ";", &ppFolderNames, DIVIDESTRING_POSTPROCESS_FORWARDSLASHES);
		}
		else
		{
			ppFolderNames = ppFolderNameList;
		}


		for (i=0; i < eaSize(&ppFolderNames); i++)
		{
			char *pReadFile;
			int iFileSize;
			bool bResult;

			sprintf(systemString, "%s log -r %d:%d %s > %s", GetSvnExe(), iFromRev + 1, iToRev, GetJunctionNameFromFolderName(ppFolderNames[i]), spTempFileName);
			if (system_w_timeout(systemString, NULL, iFailureTime) != 0)
			{
				printf("timeout failure while executing %s", systemString);
				bFailed = true;
			}
			else
			{
				pReadFile = fileAlloc(spTempFileName, &iFileSize);

				if (!pReadFile)
				{
					printf("Failure while executing command \"%s\"\n", systemString);
					bFailed = true;
				}
				else
				{
					bResult = SVN_ReadCheckinsFromBuffer(pReadFile, pppList, iFlags);

					if (!bResult)
					{
						printf("Error parsing SVN log file (command %s)\n", systemString);
						bFailed = true;
					}

					free(pReadFile);
				}
			}

		}
	}

	sprintf(systemString, "erase %s", spTempFileName);
	system_w_timeout(systemString, NULL, iFailureTime);

	if (pFolderNameString)
	{
		eaDestroyEx(&ppFolderNames, NULL);
	}

	eaQSort(*pppList, SortCheckinInfosByTime);

	return !bFailed;
}

AUTO_COMMAND ACMD_NAME(FolderIsSVNRepository);
bool SVN_FolderIsSVNRepository(char *pFolderName)
{

	char tempFileName[MAX_PATH];
	char systemString[2048];
	char *pBuff;
	bool bRetVal = false;

	if (!dirExists(pFolderName))
	{
		return false;
	}


	if (!GetTempFileName_UTF8(".", "SPR", 0, &spTempFileName))
	{
		char *pError = NULL;
		FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pError, NULL);

		assertmsgf(0, "Couldn't get temp filename: error <<%s>>", pError);
	}

	sprintf(systemString, "%s stat %s --depth immediates > %s 2>&1", GetSvnExe(), GetJunctionNameFromFolderName(pFolderName), tempFileName);

	system_w_timeout(systemString, NULL, 120);

	pBuff = fileAlloc(tempFileName, NULL);

	if (pBuff)
	{
		ANALYSIS_ASSUME(pBuff);
		if (!strstri(pBuff, "is not a working copy"))
		{
			bRetVal = true;
		}
	}
	free(pBuff);

	sprintf(systemString, "erase %s", tempFileName);
	system(systemString);

	return bRetVal;
}

U32 SVN_GetSVNNumberWhenBranchWasCreated(char *pBranchDepositoryName)
{
	char tempFileName[MAX_PATH];
	char systemString[2048];
	char *pBuf;
	char *pTemp;
	U32 iRetVal = 0;

	if (!GetTempFileName_UTF8(".", "SPR", 0, &spTempFileName))
	{
		char *pError = NULL;
		FormatMessage_UTF8(FORMAT_MESSAGE_FROM_SYSTEM, NULL, GetLastError(), 0, &pError, NULL);

		assertmsgf(0, "Couldn't get temp filename: error <<%s>>", pError);
	}

	sprintf(systemString, "%s log --stop-on-copy %s > %s", GetSvnExe(), pBranchDepositoryName, tempFileName);
	system_w_timeout(systemString, NULL, 120);


	pBuf = fileAlloc(tempFileName, NULL);

	if (!pBuf)
	{
		return 0;
	}

	pTemp = strstri(pBuf, "created new branch from ");
	if (pTemp)
	{
		pTemp = strstri(pTemp, " rev ");
		if (pTemp)
		{
			pTemp += 5;
			iRetVal = atoi(pTemp);
		}
	}

	free(pBuf);
	sprintf(systemString, "erase %s", tempFileName);
	system_w_timeout(systemString, NULL, 120);

	return iRetVal;
}

char *DEFAULT_LATELINK_SVN_GetUserName(void)
{
	return "__FAKE__";
}

char *DEFAULT_LATELINK_SVN_GetPassword(void)
{
	return "__FAKE__";
}

#endif


