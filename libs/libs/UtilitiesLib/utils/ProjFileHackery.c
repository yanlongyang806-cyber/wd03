#include "estring.h"
#include "earray.h"
#include "utils.h"
#include "fileutil2.h"

//pConfigString should start with <fileConfiguration> and end with </fileConfiguration>, but can have multiple configurations

void SetConfigForMultipleFilesInProjFile(char *pConfigString, char ***pppSuffixes, char *pProjFileName)
{
	char *pFullFile = fileAlloc(pProjFileName, NULL);

	char *pFullFileEString = NULL;
	int iReadHeadOffset = 0;
	char *pNextRelativePathToken;
	int iConfigStrLen = (int)strlen(pConfigString);
	FILE *pOutFile;

	estrCopy2(&pFullFileEString, pFullFile);

	free(pFullFile);

	while ((pNextRelativePathToken = strstr(pFullFileEString + iReadHeadOffset, "RelativePath=\"")))
	{
		char *pFirstQuote;
		char *pSecondQuote;
		char fileName[CRYPTIC_MAX_PATH];
		int i;
		bool bMatch = false;

		iReadHeadOffset = (pNextRelativePathToken - pFullFileEString) + 1;
		
		pFirstQuote = pNextRelativePathToken + 13;
		pSecondQuote = strchr(pFirstQuote + 1, '"');

		*pSecondQuote = 0;
		strcpy(fileName, pFirstQuote+1);
		*pSecondQuote = '"';

		for (i=0; i < eaSize(pppSuffixes); i++)
		{
			if (strEndsWith(fileName, (*pppSuffixes)[i]))
			{
				bMatch = true;
				break;
			}
		}

		if (bMatch)
		{
			int iCloseBracketOffset = strchr(pSecondQuote, '>') - pFullFileEString;
			int iEndFileOffset = strstr(pFullFileEString + iCloseBracketOffset, "</File>") - pFullFileEString;

			estrRemove(&pFullFileEString, iCloseBracketOffset + 1, iEndFileOffset - iCloseBracketOffset - 1);
			estrInsert(&pFullFileEString, iCloseBracketOffset + 1, pConfigString, iConfigStrLen);
		}
	}

	pOutFile = fopen(pProjFileName, "wb");
	fwrite(pFullFileEString, 1, estrLength(&pFullFileEString), pOutFile);
	fclose(pOutFile);

	estrDestroy(&pFullFileEString);
}




void SetConfigForMultipleFilesAcrossProjects(char *pConfigString, char ***pppSuffixes, char *pRootDirForVCProjFiles)
{
	char **ppFileList = fileScanDirFolders(pRootDirForVCProjFiles, FSF_FILES);
	int iCount = eaSize( &ppFileList );
	int i;

	for (i = 0 ; i < iCount; i++)
	{
		if (strEndsWith(ppFileList[i], ".vcproj"))
		{
			SetConfigForMultipleFilesInProjFile(pConfigString, pppSuffixes, ppFileList[i]);
		}

	}
	
	fileScanDirFreeNames(ppFileList);

}

/*
AUTO_RUN_FILE;
void ProjFileHackeryTest(void)
{
	char **ppSuffixes = NULL;
	char *pConfig = fileAlloc("c:\\testconfig.txt", NULL);

	eaPush(&ppSuffixes, "autogen_1.c");
	eaPush(&ppSuffixes, "autogen_2.c");


	SetConfigForMultipleFilesAcrossProjects(pConfig, &ppSuffixes, "c:\\src");
}*/