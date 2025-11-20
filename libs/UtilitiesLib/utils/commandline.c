#include "commandline.h"
#include "winInclude.h"
#include "estring.h"
#include "utils.h"
#include "file.h"
#include "superassert.h"
#include "UTF8.h"

static char *spCommandLineFromFile = NULL;

char *GetCommandLineFromFile(void)
{
	return spCommandLineFromFile;
}

//fake command
AUTO_COMMAND ACMD_COMMANDLINE;
void CommandLineFile(char *pStr)
{
}

//insert this into CommandLineFromFile so that if that ever gets cmdparsed twice we will know
AUTO_COMMAND ACMD_COMMANDLINE;
void CLDupTest(int iFake)
{
	static bool bOnce = false;
	assertmsgf(!bOnce, "CLDupTest called twice, this means that the command line from the special command line file is being cmdparsed twice. Talk to Alex.");
	bOnce = true;
}

#undef FILE
#undef fopen
#undef fclose
#undef fseek
#undef ftell
#undef fread

const char *GetCommandLineCryptic(void)
{
	static char *pCommandLineFromWindows = NULL;
	static char *pCommandLineToUse = NULL;

	char *pFoundCmd;

	if (pCommandLineToUse)
	{
		return pCommandLineToUse;
	}

	if (!pCommandLineFromWindows)
	{
		pCommandLineFromWindows = UTF16_to_UTF8_CommandLine(GetCommandLineW());
	}

	if ((pFoundCmd = strstri(pCommandLineFromWindows, "-CommandLineFile")))
	{
		char *pFileName = NULL;
		FILE *pFile;
		int iFileSize;

		if (pFoundCmd)
		{
			pFoundCmd += strlen("-CommandLineFile");
		}

		while (*pFoundCmd && (*pFoundCmd == ' ' || *pFoundCmd == '"'))
		{
			pFoundCmd++;
		}

		while (*pFoundCmd && *pFoundCmd != ' ' && *pFoundCmd != '"')
		{
			estrConcat(&pFileName, pFoundCmd, 1);
			pFoundCmd++;
		}

		backSlashes(pFileName);
		ANALYSIS_ASSUME(pFileName != NULL); // I'm fairly sure this is correct
		fopen_s(&pFile, pFileName, "rt");
		assertmsgf(pFile, "Someone told us to load our command line from %s, but we can't", pFileName);

		fseek(pFile, 0, SEEK_END);
		iFileSize = ftell(pFile);
		fseek(pFile, 0, SEEK_SET);

		estrSetSize(&spCommandLineFromFile, iFileSize);
		fread(spCommandLineFromFile, iFileSize, 1, pFile);
	
		estrConcatf(&spCommandLineFromFile, " -CLDupTest");

		fclose(pFile);

		estrPrintf(&pCommandLineToUse, "%s %s", pCommandLineFromWindows, spCommandLineFromFile);
	}
	else
	{
		pCommandLineToUse = pCommandLineFromWindows;
	}

	return pCommandLineToUse;
}
//NOTE NOTE NOTE NOTE do not put anything after this, as all the cryptic FILE stuff is un-#defined