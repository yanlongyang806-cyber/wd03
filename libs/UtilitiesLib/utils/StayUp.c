#include "StayUp.h"
#include "EString.h"
#include "sysutil.h"
#include "UTF8.h"

#if !PLATFORM_CONSOLE

#include "windefinclude.h"
#include "winuser.h"
#include "process_util.h"

#define STAYUP_COMMANDLINE_OPTION "-StayUp"
#define STAYUP_MSG_SLEEP 4000
#define STAYUP_MAX_FAILS 3
#define STAYUP_FAIL_TIME_DIFF 10 * 60 * 1000
#define STAYUP_TICK_SLEEP 1000

static bool gbStartedByStayUp = false;
AUTO_CMD_INT(gbStartedByStayUp, StartedByStayUp) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

static int giStayUpCount = 0;
AUTO_CMD_INT(giStayUpCount, StayUpCount) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

static int siStayUpParentPid = 0;
AUTO_CMD_INT(siStayUpParentPid, StayUpParentPid) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;;

static char sStayUpParentExeName[128] = "";
AUTO_CMD_STRING(sStayUpParentExeName, StayUpParentExeName) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;;

/************************************************************************/
/* Command line processing                                              */
/************************************************************************/

// Returns true if it finds a command line option
static bool FoundCommandLineOption(int argc, const char * const *argv, SA_PARAM_NN_STR const char *option)
{
	int i;

	for (i = 1; i < argc; i++)
	{
		if (!stricmp(argv[i], option))
		{
			return true;
		}
	}

	return false;
}

// Get a safe version of a command line option
SA_RET_NN_STR static const char * GetSafeCommandLineOption(SA_PARAM_NN_STR const char *option)
{
	static char *pNewOption = NULL;

	if (strchr(option, ' '))
	{
		unsigned int i;

		estrClear(&pNewOption);
		estrConcatChar(&pNewOption, '"');
		for (i = 0; i < strlen(option); i++)
		{
			if (option[i] == '"')
				estrConcatChar(&pNewOption, '\\');
			estrConcatChar(&pNewOption, option[i]);
		}
		estrConcatChar(&pNewOption, '"');
	}
	else
	{
		estrCopy2(&pNewOption, option);
	}

	return pNewOption;
}

// Get command line for new process
SA_RET_NN_STR static char * GetCommandLineString(int argc, const char * const *argv)
{
	static char *pCmdLine = NULL;
	static int siCount = 1;
	int i;

	char *pExeName;
	char *pShortExeName = NULL;

	pExeName = getExecutableName();
	estrGetDirAndFileName(pExeName, NULL, &pShortExeName);


	estrPrintf(&pCmdLine, "%s -StartedByStayUp -StayupCount %d -StayUpParentPid %d -StayUpParentExeName %s", getExecutableName(), siCount, getpid(), pShortExeName);
	siCount++;

	for (i = 1; i < argc; i++)
	{
		if (stricmp(argv[i], STAYUP_COMMANDLINE_OPTION))
			estrConcatf(&pCmdLine, " %s", GetSafeCommandLineOption(argv[i]));
	}
	

	return pCmdLine;
}


/************************************************************************/
/* Process stuff                                                        */
/************************************************************************/

// Create the child process
HANDLE SpawnProcess(int argc, const char *const *argv)
{
	char *cmd = GetCommandLineString(argc, argv);
	STARTUPINFO si = {0};
	PROCESS_INFORMATION pi = {0};
	HANDLE hProcess = INVALID_HANDLE_VALUE;

	si.cb = sizeof(si);

	if (CreateProcess_UTF8(NULL, cmd,
		NULL, // process security attributes, cannot be inherited
		NULL, // thread security attributes, cannot be inherited
		TRUE, // do let this child inhert handles
		0,
		NULL, // inherit environment
		NULL, // inherit current directory
		&si,
		&pi))
	{
		CloseHandle(pi.hThread);
		hProcess = pi.hProcess;
	}

	return hProcess;
}

// Ensure the process is running
static bool EnsureProcessRunning(int argc, const char *const *argv, HANDLE *hProcess, StayUpFunc pFunc, void *pUserData)
{
	static int fails = 0;
	static U32 lastStart = 0;

	assert(hProcess);

	if (*hProcess != INVALID_HANDLE_VALUE)
	{
		DWORD dwExitCode;

		if (GetExitCodeProcess(*hProcess, &dwExitCode))
		{
			if (dwExitCode == STILL_ACTIVE)
			{
				if (GetTickCount() - STAYUP_FAIL_TIME_DIFF > lastStart)
					fails = 0;

				return true;
			}
			else if (lastStart)
			{
				fails++;
				if (fails > STAYUP_MAX_FAILS)
				{
					fprintf(stderr, "FAiled too many times, aborting STAYUP\n");
					return false;
				}
			}
		}
	}

	if (pFunc && !pFunc(pUserData))
		return true;

	if (*hProcess != INVALID_HANDLE_VALUE)
	{
		CloseHandle(*hProcess);
	}

	*hProcess = SpawnProcess(argc, argv);

	if (*hProcess != INVALID_HANDLE_VALUE)
	{
		DWORD dwProcessID = GetProcessId(*hProcess);

		printf("Started new child process [pid:%u]...\n\n", (unsigned int)dwProcessID);
	}
	else
	{
		fprintf(stderr, "Could not launch child process!\n");
		return false;
	}

	lastStart = GetTickCount();

	return true;
}

#endif

/************************************************************************/
/* Stay up                                                              */
/************************************************************************/

bool StayUp(int argc, const char *const *argv,
			StayUpFunc pSafeToStartFunc, void *pSafeToStartUserData,
			StayUpFunc pTickFunc, void *pTickUserData)
{
#if !PLATFORM_CONSOLE
	HANDLE hProcess = INVALID_HANDLE_VALUE;

	if (!FoundCommandLineOption(argc, argv, STAYUP_COMMANDLINE_OPTION))
	{
		return false;
	}

	while (true)
	{
		if (!EnsureProcessRunning(argc, argv, &hProcess, pSafeToStartFunc, pSafeToStartUserData))
		{
		
			Sleep(STAYUP_MSG_SLEEP);
			return true;
		}

		if (pTickFunc)
		{
			if (!pTickFunc(pTickUserData)) return true;
		}
		Sleep(STAYUP_TICK_SLEEP);
	}

	return true;

#else
	return false;
#endif
}

bool StartedByStayUp(void)
{
#if !PLATFORM_CONSOLE
	return gbStartedByStayUp;
#else
	return 0;
#endif
}

int StayUpCount(void)
{
	if (!gbStartedByStayUp)
	{
		return 0;
	}

	return giStayUpCount;
}

void CancelStayUp(void)
{
	if (siStayUpParentPid && sStayUpParentExeName[0])
	{
		if (ProcessNameMatch(siStayUpParentPid, sStayUpParentExeName, false))
		{
			kill(siStayUpParentPid);
		}
	}
}