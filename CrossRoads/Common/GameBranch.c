#include "EString.h"
#include "GameBranch.h"
#include "stdtypes.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

char *g_pcGameBranch = NULL;

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_COMMANDLINE ACMD_NAME("SetGameBranch");
void cmdGameBranch_SetGameBranch(const char *pchBranch)
{
	if(g_pcGameBranch)
		free(g_pcGameBranch);
	if(pchBranch && pchBranch[0])
	{
		size_t len = strlen(pchBranch);
		g_pcGameBranch = malloc(len+1);
		strncpy_s(g_pcGameBranch, len+1, pchBranch, len);
	}
}

// Removed for now since Ben said there's no use for it at all anymore
//AUTO_EXPR_FUNC() ACMD_NAME(GetGameBranch);
//const char *exprGameBranch_GetGameBranch(void)
//{
	//return(NULL_TO_EMPTY(g_pcGameBranch));
//}

const char *GameBranch_GetDirectory(char **pcEstrOut, const char *pchDirectory)
{
	if(!pchDirectory || !g_pcGameBranch || !g_pcGameBranch[0])	
	{
		estrCopy2(pcEstrOut, pchDirectory);
	}
	else
	{
		// Suffix the game branch
		estrPrintf(pcEstrOut, "%s_%s",
			pchDirectory,
			g_pcGameBranch);
	}

	return(*pcEstrOut);
}

const char *GameBranch_GetFilename(char **pcEstrOut, const char *pchFilename)
{
	if(!pchFilename || !g_pcGameBranch || !g_pcGameBranch[0])
	{
		estrCopy2(pcEstrOut, pchFilename);
	}
	else
	{
		// Prefix the game branch
		estrPrintf(pcEstrOut, "%s_%s",
			g_pcGameBranch,
			pchFilename);
	}

	return(*pcEstrOut);
}


//Requires that the format of pchPath is DirBase/Dir/File.ext
// The fixup in this case with FixDirectory and FixFilename true would return pcEstrOut of
// "DirBase/Dir_GameBranch/GameBranch_File.ext"
const char *GameBranch_FixupPath(char **pcEstrOut, const char *pchPath, bool bFixDirectory, bool bFixFilename)
{
	if(!pchPath || !g_pcGameBranch || !g_pcGameBranch[0] || (!bFixDirectory && !bFixFilename))
	{
		estrCopy2(pcEstrOut, pchPath);
	}
	else
	{
		char *pPathDup = strdup(pchPath);
		char *pTok = pPathDup;
		char *context = NULL;
		char *pFile = NULL;
		char *pDir = NULL;
		int iPathLen = 0;

		estrClear(pcEstrOut);

		pTok = strtok_s(pTok, "/", &context);
		while(pTok != NULL)
		{
			if(pFile)
			{
				if(pDir)
					iPathLen += (int)strlen(pDir) + 1;
				pDir = pFile;
			}
			pFile = pTok;
			pTok = strtok_s(NULL, "/", &context);
		}
		if(pDir || pFile)
		{
			
			//If the last token wasn't a file with extension
			if(pFile && !strchr(pFile, '.'))
			{
				if(pDir)
					iPathLen += (int)strlen(pDir) + 1;
				pDir = pFile;
				pFile = NULL;
			}

			if(iPathLen)
				estrConcat(pcEstrOut, pchPath, iPathLen);

			if(pDir)
			{
				estrConcat(pcEstrOut, pDir, (int)strlen(pDir));
				if(bFixDirectory)
				{
					estrConcatChar(pcEstrOut, '_');
					estrConcat(pcEstrOut,	g_pcGameBranch, (int)strlen(g_pcGameBranch));
				}
			}

			if(pFile)
			{
				estrConcatChar(pcEstrOut, '/');
				if(bFixFilename)
				{
					estrConcat(pcEstrOut,	g_pcGameBranch, (int)strlen(g_pcGameBranch));
					estrConcatChar(pcEstrOut, '_');
				}

				estrConcat(pcEstrOut, pFile, (int)strlen(pFile));
			}
		}

		free(pPathDup);
	}

	return(*pcEstrOut);
}
