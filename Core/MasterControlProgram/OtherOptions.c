#include "mastercontrolprogram.h"
#include "utils.h"
#include "fileutil.h"

#include "fileutil2.h"
#include "error.h"

#define MAX_OTHER_OPTIONS 33
char sOtherOptionNames[MAX_OTHER_OPTIONS][128];
int iOtherOptionNamesCount=0;

U32 iOtherOptionButtonIDs[MAX_OTHER_OPTIONS] =
{
	IDC_BUTTON1,
	IDC_BUTTON2,
	IDC_BUTTON3,
	IDC_BUTTON4,
	IDC_BUTTON5,
	IDC_BUTTON6,
	IDC_BUTTON7,
	IDC_BUTTON8,
	IDC_BUTTON9,
	IDC_BUTTON10,
	IDC_BUTTON11,
	IDC_BUTTON12,
	IDC_BUTTON13,
	IDC_BUTTON14,
	IDC_BUTTON15,
	IDC_BUTTON16,
	IDC_BUTTON17,
	IDC_BUTTON18,
	IDC_BUTTON19,
	IDC_BUTTON20,
	IDC_BUTTON21,
	IDC_BUTTON22,
	IDC_BUTTON23,
	IDC_BUTTON24,
	IDC_BUTTON25,
	IDC_BUTTON26,
	IDC_BUTTON27,
	IDC_BUTTON28,
	IDC_BUTTON29,
	IDC_BUTTON30,
	IDC_BUTTON31,
	IDC_BUTTON32,
	IDC_BUTTON33
};

void PressOtherOptionButton(int i, bool bWait)
{
	char cwd[CRYPTIC_MAX_PATH];
	char batFile[CRYPTIC_MAX_PATH];
	char batFileDir[CRYPTIC_MAX_PATH];
	char batFileNoDir[CRYPTIC_MAX_PATH];
	char systemString[CRYPTIC_MAX_PATH+4];

	char dataDir[MAX_PATH];
	char coreDataDir[MAX_PATH];
	char exeDir[MAX_PATH];
	char coreExeDir[MAX_PATH];

	fileGetcwd(cwd, 1023);

	assert(i >= 0 && i < MAX_OTHER_OPTIONS);

	strcpy(dataDir, fileDataDir());
	strcpy(coreDataDir, fileCoreDataDir()?fileCoreDataDir():fileDataDir());
	strcpy(exeDir, fileExecutableDir());
	strcpy(coreExeDir, fileCoreExecutableDir());

	sprintf(batFile, "server/MCPButtons/%s.bat", sOtherOptionNames[i]);
	fileLocateWrite(batFile, batFile);
	strcpy(batFileDir, batFile);
	verify(chdir(getDirectoryName(batFileDir)) == 0);

	backSlashes(dataDir);
	backSlashes(coreDataDir);
	backSlashes(coreExeDir);
	backSlashes(exeDir);

	getFileNameNoDir(batFileNoDir, batFile);


	if (bWait)
	{
		sprintf(systemString, "cmd.exe /c \"\"%s\"\" %s %s %s %s", batFileNoDir, dataDir, coreDataDir, exeDir, coreExeDir);
		system_wait(systemString, false, false);
	}
	else
	{
		sprintf(systemString, "\"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", batFileNoDir, dataDir, coreDataDir, exeDir, coreExeDir);
		system_detach(systemString, false, false);
	}

	verify(chdir(cwd) == 0);
}

void PressOtherOptionButtonByName(char *pName, bool bWait)
{
	int i;
	for (i = 0; i < MAX_OTHER_OPTIONS; i++)
	{
		if (stricmp(pName, sOtherOptionNames[i]) == 0)
		{
			PressOtherOptionButton(i, bWait);
			return;
		}
	}

	Errorf("Couldn't find other option batch file %s", pName);

}

FileScanAction FindOptionBatchFilesProcessor(char* dir, struct _finddata32_t* data, void *pUserData)
{
	char path[CRYPTIC_MAX_PATH];
	char *s;
	char *pCommonDirString;
	if (iOtherOptionNamesCount >= ARRAY_SIZE(sOtherOptionNames))
		return FSA_STOP;
	if (!strEndsWith(data->name, ".bat"))
		return FSA_EXPLORE_DIRECTORY;
	sprintf(path, "%s/%s", dir, data->name);
	fileRelativePath(path, path);
	assert((pCommonDirString = strstr(path, "server/MCPButtons/")));

	strcpy(sOtherOptionNames[iOtherOptionNamesCount], pCommonDirString + strlen("server/MCPButtons/"));
	s = strrchr(sOtherOptionNames[iOtherOptionNamesCount], '.');
	if (s)
		*s = '\0'; // Remove .bat
	iOtherOptionNamesCount++;

	return FSA_EXPLORE_DIRECTORY;
}

void FindOptionBatchFiles(void)
{
	memset(sOtherOptionNames, 0, sizeof(sOtherOptionNames));
	iOtherOptionNamesCount = 0;

	fileScanAllDataDirs("server/MCPButtons", FindOptionBatchFilesProcessor, NULL);
}


BOOL OtherOptionsDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{
	int i;

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{
				
			for (i=0; i < MAX_OTHER_OPTIONS; i++)
			{
				if (sOtherOptionNames[i][0])
				{
					ShowWindow(GetDlgItem(hDlg, iOtherOptionButtonIDs[i]),  SW_SHOW);
					SetWindowText(GetDlgItem(hDlg, iOtherOptionButtonIDs[i]), sOtherOptionNames[i]);
				}
				else
				{
					ShowWindow(GetDlgItem(hDlg, iOtherOptionButtonIDs[i]),  SW_HIDE);
				}
			}
			return TRUE; 

		}
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL)
		{
			pWindow->bCloseRequested = true;

			return FALSE;



		}
			
		for (i=0; i < MAX_OTHER_OPTIONS; i++)
		{
			if (LOWORD (wParam) == iOtherOptionButtonIDs[i])
			{
				PressOtherOptionButton(i, false);
			}
		}	
		break;

	}
	
	return FALSE;
}
