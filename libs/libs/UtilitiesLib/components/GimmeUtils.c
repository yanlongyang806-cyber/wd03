#include "wininclude.h"
#include "fileutil.h"
#include "timing.h"
#include "estring.h"
#include "stringutil.h"
#include "simpleparser.h"
#include "Trivia.h"
#include "GimmeUtils.h"
#include "SVNUtils.h"
#include "SVNUtils_h_ast.h"
#include "GimmeUtils_c_ast.h"
#include "StringCache.h"
#include "sysUtil.h"

//gimme branch numbers are currently broken, setting a kludge so the CBs can keep working
static int siOverrideGimmeBranchNum = -1;
AUTO_CMD_INT(siOverrideGimmeBranchNum, OverrideGimmeBranchNum);


AUTO_STRUCT;
typedef struct GimmeEmailAlias
{
	char *pGimmeName; AST(STRUCTPARAM, POOL_STRING)
	char *pEmailName; AST(STRUCTPARAM, POOL_STRING)
} GimmeEmailAlias;

AUTO_STRUCT;
typedef struct GimmeEmailAliasList
{
	GimmeEmailAlias **ppAlias;
} GimmeEmailAliasList;


AUTO_STRUCT;
typedef struct DidTheyClickYesEntry
{
	U32 iDate;
	char *pUserName; AST(ESTRING)
	const char **ppFileNames; AST(POOL_STRING)
} DidTheyClickYesEntry;


#if !PLATFORM_CONSOLE

bool Gimme_UpdateFileToTime(U32 iTime, char *pFileName, U32 iFailureTime)
{
	char systemString[1024];
	int iRetVal;
	QueryableProcessHandle *pHandle;

	U32 iStartingTime = timeSecondsSince2000_ForceRecalc();

	if (!iTime)
	{
		sprintf(systemString, "gimme -glvfile %s", pFileName);
	}
	else
	{
		sprintf(systemString, "gimme -getbydateSS2000 %u %s", iTime, pFileName);
	}

	if (!(pHandle = StartQueryableProcess(systemString, NULL, true, false, false, NULL)))
	{
	
		return false;
	}

	while (1)
	{

		if (QueryableProcessComplete(&pHandle, &iRetVal))
		{
			break;
		}

		if (iFailureTime && timeSecondsSince2000_ForceRecalc() - iStartingTime > iFailureTime)
		{
		
			KillQueryableProcess(&pHandle);
			return false;
		}

		Sleep(1);
	}

	return (iRetVal == 0);
}


bool Gimme_UpdateFoldersToTime(U32 iTime, char *pFolderNameString, char **ppFolderNameList, U32 iFailureTime, char *pExtraCommandLine)
{
	char **ppLocalFolderNames = NULL;
	char **ppFolderNames = NULL;
	int i;
	static char *pSystemString = NULL;
	U32 iStartingTime = timeSecondsSince2000_ForceRecalc();
	int iRetVal = 0;
	QueryableProcessHandle *pHandle;


	if (pFolderNameString)
	{
		DivideString(pFolderNameString, ";", &ppLocalFolderNames, DIVIDESTRING_POSTPROCESS_BACKSLASHES);
		ppFolderNames = ppLocalFolderNames;
	}
	else
	{
		ppFolderNames = ppFolderNameList;
	}

	for (i=0; i < eaSize(&ppFolderNames); i++)
	{
		estrPrintf(&pSystemString, "gimme %s -getbydateSS2000 %u %s", 
			pExtraCommandLine ? pExtraCommandLine : "",
			iTime, ppFolderNames[i]);

		if (!(pHandle = StartQueryableProcess(pSystemString, NULL, true, false, false, NULL)))
		{
			
			eaDestroyEx(&ppLocalFolderNames, NULL);
					
			return false;
		}

		while (1)
		{

			if (QueryableProcessComplete(&pHandle, &iRetVal))
			{
				break;
			}

			if (iFailureTime && timeSecondsSince2000_ForceRecalc() - iStartingTime > iFailureTime)
			{
				
				eaDestroyEx(&ppLocalFolderNames, NULL);
							
			
				KillQueryableProcess(&pHandle);
				return false;
			}

			Sleep(1);
		}

	}


	eaDestroyEx(&ppLocalFolderNames, NULL);
	

	return (iRetVal == 0);
}

void Gimme_ApproveFoldersByTime(U32 iRevNum, char *pFolderNames, U32 iFailureTime)
{
	/*char **ppFolderNames = NULL;
	int i;
	char systemString[1024];
	SYSTEMTIME t;

	timerSystemTimeFromSecondsSince2000(&t,iRevNum);

	DivideString(pFolderNames, ";", &ppFolderNames, DIVIDESTRING_POSTPROCESS_BACKSLASHES);

	for (i=0; i < eaSize(&ppFolderNames); i++)
	{
		sprintf(systemString, "gimme -approvebydate %02d%02d%02d%02d:%02d:%02d %s", 
			t.wMonth, t.wDay, t.wYear % 100, t.wHour, t.wMinute, t.wSecond, ppFolderNames[i]);
		system_w_timeout(systemString, iFailureTime);
	}

	eaDestroyEx(&ppFolderNames, NULL);*/
}



//FIXME sort this out better
//copied from patchClientLib - 

#define TRIVIAFILE_PATCH	"patch_trivia.txt"
#define PATCH_DIR					".patch"
#define TRIVIAFILE_PATCH_OLD	".bak"
#define TRIVIAFILE_PATCH_NEW	".new"


static bool gimmeUtils_getPatchTriviaList(char *triviapath, int triviapath_size, char *rootpath, int rootpath_size, const char *file)
{
	char *slash;

	if(!rootpath)
	{
		rootpath_size = MAX_PATH;
		rootpath = _alloca(rootpath_size);
	}

	if(file)
	{
		if(file[0] == '\0')
			fileGetcwd(rootpath, rootpath_size);
		else
			strcpy_s(rootpath, rootpath_size, file);
		forwardSlashes(rootpath);
		if(strEndsWith(rootpath, "/")){
			rootpath[strlen(rootpath) - 1] = 0;
		}
	}
	else
	{
		strcpy_s(rootpath, rootpath_size, getExecutableName());
		forwardSlashes(rootpath);
		slash = strrchr(rootpath,'/');
		if(slash)
			*slash = '\0';
	}

	if (strchr(rootpath, ':') != strrchr(rootpath, ':')) // invalid path, will cause assert in file layer (thinks it's a pigged path)
		return false;

	for(;;)
	{
		sprintf_s(SAFESTR2(triviapath), "%s/%s/%s", rootpath, PATCH_DIR, TRIVIAFILE_PATCH); // Secret knowledge
		if(fileExists(triviapath))
			return true;
		else 
		{
			strcat_s(SAFESTR2(triviapath), TRIVIAFILE_PATCH_OLD);
			if(fileExists(triviapath))
				return true;
			else {
				sprintf_s(SAFESTR2(triviapath), "%s/%s/", rootpath, PATCH_DIR);
				if(dirExists(triviapath)) {
					strcat_s(SAFESTR2(triviapath), TRIVIAFILE_PATCH);
					return true;
				}
			}
		}

		slash = strrchr(rootpath, '/');
		if(!slash)
		{
			rootpath[0] = '\0';
			return false;
		}
		*slash = '\0';
	}
}




static bool gimmeUtils_triviaGetPatchTriviaForFile(char *buf, int buf_size, const char *file, const char *key)
{
	char trivia_file[MAX_PATH];
	bool bFound = false;

	if(buf_size > 0)
		buf[0] = '\0';
	if(gimmeUtils_getPatchTriviaList(SAFESTR(trivia_file), NULL, 0, file))
	{
		TriviaMutex mutex = triviaAcquireDumbMutex(trivia_file);
		TriviaList *list = triviaListCreateFromFile(trivia_file);
		triviaReleaseDumbMutex(mutex);
		if(list)
		{
			const char *val = triviaListGetValue(list, key);
			if(val)
			{
				strcpy_s(buf, buf_size, val);
				bFound = true;
			}
			triviaListDestroy(&list);
		}
		else
		{
			FatalErrorf("Could not open patch trivia %s", trivia_file);
		}
	}

	return bFound;
}

//returns -1 on failure
int Gimme_GetBranchNum(const char *pFolderName)
{
	char outBuf[256];
	int iRetVal;

	if (!gimmeUtils_triviaGetPatchTriviaForFile(SAFESTR(outBuf), pFolderName, "PatchBranch"))
	{
		return -1;
	}

	if (!StringToInt(outBuf, &iRetVal))
	{
		iRetVal = -1;
	}

	return iRetVal;
}


bool Gimme_GetRevisionFileName(char outName[MAX_PATH], char *pFolderName, int iBranchNum, char *pLoadedCfgFile)
{
	char *pReadHead= pLoadedCfgFile;
	int iFolderNameLen = (int)strlen(pFolderName);

	while (1)
	{
		char *pNextNewLine = strchr(pReadHead, '\n');
		if (strStartsWith(pReadHead, pFolderName) && (IS_WHITESPACE(pReadHead[iFolderNameLen]) || pReadHead[iFolderNameLen] == 0))
		{
			char tempBuffer[MAX_PATH];
			if (!pNextNewLine)
			{
				return false;
			}

			pReadHead = pNextNewLine + 1;
			pNextNewLine = strchr(pReadHead, '\n');

			if (pNextNewLine)
			{
				*pNextNewLine = 0;
			}

			strcpy(tempBuffer, pReadHead);
			removeTrailingWhiteSpaces(tempBuffer);
			
			

			snprintf_s(outName, MAX_PATH, "%s\\comments%da.txt", tempBuffer, iBranchNum);

			if (pNextNewLine)
			{
				*pNextNewLine = '\n';
			}

			return true;
		}

		if (!pNextNewLine)
		{
			return false;
		}

		pReadHead = pNextNewLine + 1;
	}
}




bool Gimme_GetCheckinsBetweenTimes_ForceBranch(U32 iFromTime, U32 iToTime, char *pFolderNameString, char **ppFolderNameList, 
	enumGimmeGetCheckinsFlags eFlags, CheckinInfo ***pppList, U32 iFailureTime, int iForcedBranchNum, int iForcedCoreBranchNum)
{
	char **ppFolderNames = NULL;
	int iFolderNum;



	char listFileName[CRYPTIC_MAX_PATH];

	sprintf(listFileName, "%s/gimmeCheckins.txt", fileTempDir());
	
	mkdirtree(listFileName);
	
	if (pFolderNameString)
	{
		DivideString(pFolderNameString, ";", &ppFolderNames, DIVIDESTRING_POSTPROCESS_NONE);
	}
	else
	{
		ppFolderNames = ppFolderNameList;
	}

	for (iFolderNum=0; iFolderNum < eaSize(&ppFolderNames); iFolderNum++)
	{
		int iActualBranchNum = Gimme_GetBranchNum(ppFolderNames[iFolderNum]);
		int iStartingBranchNum = (iActualBranchNum == 0 ? 0 : iActualBranchNum - 1);
		int iEndingBranchNum = iActualBranchNum;
		int iBranchNumToUse;

		if (iActualBranchNum == -1)
		{
			if (pFolderNameString)
			{
				eaDestroyEx(&ppFolderNames, NULL);
			}
			return false;
		}

		if(strstri(ppFolderNames[iFolderNum], "core")!=0)
		{
			if(iForcedCoreBranchNum != -1)
			{
				iStartingBranchNum = iEndingBranchNum = iForcedCoreBranchNum;
			}
		}
		else if (iForcedBranchNum != -1)
		{
			iStartingBranchNum = iEndingBranchNum = iForcedBranchNum;
		}

		for (iBranchNumToUse = iStartingBranchNum; iBranchNumToUse <= iEndingBranchNum; iBranchNumToUse++)
		{
		
			int i;
		

			CheckinList checkinList = {0};
			char systemString[1024];
			sprintf(systemString, "gimme -overridebranch %d -getcheckinsbetweentimesSS2000 %s %u %u %s",
				iBranchNumToUse, listFileName, iFromTime, iToTime, ppFolderNames[iFolderNum]);

			if (system_w_timeout(systemString, NULL, iFailureTime))
			{
				if (pFolderNameString)
				{
					eaDestroyEx(&ppFolderNames, NULL);
				}			
				return false;
			}

			ParserReadTextFile(listFileName, parse_CheckinList, &checkinList, 0);

			for (i=0; i < eaSize(&checkinList.checkins); i++)
			{
				int j;

				if (eFlags & GIMMEGETCHECKINS_FLAG_NO_CHECKINS_FROM_CBS)
				{
					if (strStartsWith(checkinList.checkins[i]->userName, "CB_")
						|| strStartsWith(checkinList.checkins[i]->userName, "CB-"))
					{
						StructDestroy(parse_CheckinInfo, checkinList.checkins[i]);
						continue;
					}
				}

				if (eFlags & GIMMEGETCHECKINS_FLAG_NO_BLANK_COMMENTS)
				{
					if (estrLength(&checkinList.checkins[i]->checkinComment) == 0)
					{
						StructDestroy(parse_CheckinInfo, checkinList.checkins[i]);
						continue;
					}
				}

				for (j=0; j < eaSize(pppList); j++)
				{
					if (StructCompare(parse_CheckinInfo, (*pppList)[j], checkinList.checkins[i], 0, 0, 0) == 0)
					{
						StructDestroySafe(parse_CheckinInfo, &checkinList.checkins[i]);
						break;
					}
				}

				if (eFlags & GIMMEGETCHECKINS_FLAG_REPLACE_DOLLARSIGNS)
				{
					estrReplaceOccurrences(&checkinList.checkins[i]->checkinComment, "$", "(DollarSign)");
				}


				if (checkinList.checkins[i])
				{
					eaPush(pppList, checkinList.checkins[i]);
				}
			}

			eaDestroy(&checkinList.checkins);
			
		}
		
	}


	eaQSort(*pppList, SortCheckinInfosByTime);

	if (pFolderNameString)
	{
		eaDestroyEx(&ppFolderNames, NULL);
	}

	return true;
}


bool Gimme_IsReasonableGimmeName(char *pName)
{
	if (!pName || !pName[0])
	{
		return false;
	}

	while (*pName)
	{
		if (!(isalnum(*pName) || *pName == '_' || *pName == '-'))
		{
			return false;
		}

		pName++;
	}

	return true;
}


static GimmeEmailAliasList sGimmeEmailAliasList = {0};

const char *Gimme_GetEmailNameFromGimmeName(char *pName)
{
	int i;
	const char *pPooledName = allocAddString(pName);

	for (i=0; i < eaSize(&sGimmeEmailAliasList.ppAlias); i++)
	{
		if (pPooledName == sGimmeEmailAliasList.ppAlias[i]->pGimmeName)
		{
			return sGimmeEmailAliasList.ppAlias[i]->pEmailName;
		}
	}

	return pPooledName;

}

void Gimme_LoadEmailAliases(void)
{
	StructDeInit(parse_GimmeEmailAliasList, &sGimmeEmailAliasList);
	ParserReadTextFile("n:/gimme/EmailAliases.txt", parse_GimmeEmailAliasList, &sGimmeEmailAliasList, 0);
}


DidTheyClickYesEntry **ppDidTheyClickYesEntries = NULL;

void Gimme_LoadDidTheyClickYesEntries(void)
{
	char *pBuffer;
	char *pReadHead;
	int iSize;

	eaDestroyStruct(&ppDidTheyClickYesEntries, parse_DidTheyClickYesEntry);

	pBuffer = fileAlloc("n:\\gimme\\logs\\TheyClickedYes.log", &iSize);

	if (!pBuffer)
	{
		return;
	}

	pReadHead = pBuffer;

	while (1)
	{
		char *pNextNewLine;
		DidTheyClickYesEntry *pEntry;
		U32 iNextTime = timeGetSecondsSince2000FromLocalDateString(pReadHead);
		if (!iNextTime)
		{
			break;
		}
	
		pEntry = StructCreate(parse_DidTheyClickYesEntry);
		pEntry->iDate = iNextTime;
		pReadHead += 20;

		while (!IS_WHITESPACE(*pReadHead))
		{
			estrConcatChar(&pEntry->pUserName, *pReadHead);
			pReadHead++;
		}

		pReadHead += 3;

		pNextNewLine = strchr(pReadHead, '\n');
		*pNextNewLine = 0;


		while (1)
		{
			char *pNextLeftParens = strchr(pReadHead, '(');
			char *pNextComma;
			if (!pNextLeftParens)
			{
				//bad formatting?
				break;
			}

			*(pNextLeftParens - 1) = 0;
			eaPush(&pEntry->ppFileNames, allocAddString(pReadHead));
			pReadHead = pNextLeftParens;
			pNextComma = strchr(pReadHead, ',');
			if (!pNextComma)
			{
				break;
			}

			pReadHead = pNextComma + 2;
		}
		
		eaPush(&ppDidTheyClickYesEntries, pEntry);

		if (!pNextNewLine)
		{
			break;
		}

		pReadHead = pNextNewLine + 1;
	} 

	free(pBuffer);
}

bool Gimme_DidTheyClickYes(const char *pUserName, const char *pFileName, U32 iStartTime, U32 iEndTime)
{
	int i;

	for (i=0; i < eaSize(&ppDidTheyClickYesEntries); i++)
	{
		DidTheyClickYesEntry *pEntry = ppDidTheyClickYesEntries[i];

		if (pEntry->iDate >=  iStartTime && pEntry->iDate <= iEndTime && stricmp(pEntry->pUserName, pUserName) == 0)
		{
			int j;
			
			for (j=0; j < eaSize(&pEntry->ppFileNames); j++)
			{
				if (stricmp(pEntry->ppFileNames[j],pFileName)==0)
				{
					return true;
				}
			}
		}
	}

	return false;
}

#endif

#include "GimmeUtils_c_ast.c"

