#include "net/net.h"
#include "crypt.h"
#include "sysutil.h"
#include "cmdparse.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "earray.h"
#include "accountnet.h"
#include "WebInterface.h"
#include "AccountServerCommands.h"
#include "sock.h"
#include "timing.h"

#define MAX_COMMAND_LEN 256

CRITICAL_SECTION gCommAccess;
void initCommAccessCriticalSection(void)
{
	InitializeCriticalSection(&gCommAccess);
}

static bool spQuitComm = false;

static DWORD WINAPI CommMonitor(LPVOID lpParam)
{
	while (1)
	{
		EnterCriticalSection(&gCommAccess);
		commMonitor(commDefault());
		LeaveCriticalSection(&gCommAccess);
		if (spQuitComm)
			break;
		Sleep(1);
	}
	return 0;
}

AUTO_COMMAND ACMD_NAME(exit,quit);
void exitAccountTest(void)
{
	spQuitComm = true;
	exit(0);
}

void promptUserInput(void)
{
	while(1)
	{
		char buffer[MAX_COMMAND_LEN];
		printf("> ");
		gets(buffer);
		printf("Running: %s\n", buffer);
		globCmdParse(buffer);
	}
}

static char szCommandString[256] = "";
AUTO_COMMAND ACMD_NAME(run,setCmdString) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setCmdString(char *pCmd)
{
	strcpy(szCommandString, pCmd);
}

static bool sbPauseOnExit = true;
AUTO_COMMAND ACMD_NAME(np) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setNoExitPause(int i)
{
	sbPauseOnExit = i;
}

static bool sbRunWebInterface = false;
AUTO_COMMAND ACMD_NAME(web) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setWebInterface(int i)
{
	sbRunWebInterface = i;
}

bool gbRunLoadTest = false;
AUTO_CMD_INT(gbRunLoadTest,loadtest) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

int giLoadCount = 10000;
AUTO_CMD_INT(giLoadCount,loadcount) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

int giLoadTestPort = 6999;
AUTO_CMD_INT(giLoadTestPort,loadport) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

char gpLoadTestParentAddress[128] = "";
AUTO_CMD_STRING(gpLoadTestParentAddress, loadaddress) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

bool gbLoadTestChildProcess = false;
AUTO_CMD_INT(gbLoadTestChildProcess,loadchild) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

U32 giLoadTestRunTime = 600; // default = 10 minutes
AUTO_CMD_INT(giLoadTestRunTime,loadruntime) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

char gAccountName[128] = "default";
char gPassword[128] = "";
AUTO_CMD_STRING(gAccountName,accountname) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;
AUTO_CMD_STRING(gPassword,password) ACMD_ACCESSLEVEL(0) ACMD_EARLYCOMMANDLINE;

static int siChildProcessCount = 0;
static int siChildProcessFailCount = 0;
#define ACCOUNTSERVER_LOADTEST_FAILURE 0
#define ACCOUNTSERVER_LOADTEST_SUCCESS 1

static int aiFailures[4] = {0,0,0,0};

void ReceiveLoadChildFailure(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	//U32 maxTime = pktGetU32(pkt);
	U32 iFailureCode = pktGetU32(pkt);
	if (cmd == ACCOUNTSERVER_LOADTEST_FAILURE)
	{
		siChildProcessFailCount++;
		if (iFailureCode >= 0 && iFailureCode < 4)
			aiFailures[iFailureCode]++;
	}
	siChildProcessCount--;
	{
		Packet* resp = pktCreate(link, 0);
		pktSend(&resp);
	}
}

void ReceiveLoadTestResponseToResponse(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	exit(0);
}

int main(int argc, char** argv)
{
	DWORD dummy;
	WAIT_FOR_DEBUGGER
	EXCEPTION_HANDLER_BEGIN

	DO_AUTO_RUNS;

	// First, call the universal setup stuff
	memCheckInit();

	setDefaultAssertMode();
	cryptAdler32Init();
	initCommAccessCriticalSection();

	if (gbLoadTestChildProcess)
	{
		U32 end_time = timeSecondsSince2000() + giLoadTestRunTime;
		bool bFailedTest = true;
		int iFailureCode = -1;
		printf("Test time: %d\n", giLoadTestRunTime);
		while ((iFailureCode = TestAccountLogin(gAccountName, gPassword)) < 0) 
		{
			if (timeSecondsSince2000() > end_time)
			{
				bFailedTest = false;
				break;
			}			
		}
		{
			NetLink *link = commConnect(commDefault(), LINK_FORCE_FLUSH, gpLoadTestParentAddress, giLoadTestPort, 
				ReceiveLoadTestResponseToResponse,0,0,0);
			if (link && linkConnectWait(link, 2.0f))
			{
				Packet *pkt = pktCreate(link, bFailedTest ? ACCOUNTSERVER_LOADTEST_FAILURE : ACCOUNTSERVER_LOADTEST_SUCCESS);
				pktSendU32(pkt, iFailureCode);
				pktSend(&pkt);
				printf("Sent %s\n", bFailedTest ? "Failure" : "Success");
			}
			else
			{
				pushDontReportErrorsToErrorTracker(true);
				Errorf(0);
				popDontReportErrorsToErrorTracker();
			}
			while(1)
			{
				commMonitor(commDefault());
				Sleep(1);
			}
		}
	}
	else if (gbRunLoadTest)
	{
		/*printf("Account Server: %s\n", getAccountServer());
		while(1)
		{
			TestAccountLogin(gAccountName, gPassword);
		}*/
		int i;
		U32 start_time = timeSecondsSince2000();
		U32 last_time = start_time;
		NetListen *listen = commListen(commDefault(), LINK_FORCE_FLUSH, giLoadTestPort, ReceiveLoadChildFailure, 0, 0, 0);
		char localAddress[128] = "";

		assert(listen);
		CreateThread(0, 0, CommMonitor, 0, 0, &dummy);
		GetIpStr(getHostLocalIp(), localAddress, ARRAY_SIZE_CHECKED(localAddress));

		printf("Starting Load Test of %d parallel logins of %d seconds each... \n", giLoadCount, giLoadTestRunTime);
		for (i=0; i<giLoadCount; i++)
		{
			// TODO spawn child process
			// -loadport %d -loadaddress %s -loadchild
			siChildProcessCount++;
			system_detach(STACK_SPRINTF("ProductKeyCmds.exe -setAccountServer %s -loadport %d -loadaddress %s -loadruntime %d -loadchild", 
				getAccountServer(), giLoadTestPort, localAddress, giLoadTestRunTime), 0, 1);
		}
		printf("Started processes.\n");

		while(siChildProcessCount)
		{
			U32 cur_time = timeSecondsSince2000();
			if (cur_time > last_time + 10)
			{
				printf("Checkpoint: %d seconds, %d left\n", cur_time - start_time, siChildProcessCount);
				last_time = cur_time;
			}
			Sleep(5);
		}
		printf("\nFailed count: %d out of %d\n", siChildProcessFailCount, giLoadCount);
		printf("Failure Summary:\n  Unknown: %d\n  Connection: %d\n  Packet Send: %d\n  Time out: %d\n",
			aiFailures[0], aiFailures[1], aiFailures[2], aiFailures[3]);
		while(1)
		{
			promptUserInput();
		}
	}
	else if (sbRunWebInterface)
	{
		initWebInterface();
		while (1)
		{
			commMonitor(getWebComm());
			updateWebInterface();
		}
	}
	else if (szCommandString[0])
	{
		CreateThread(0, 0, CommMonitor, 0, 0, &dummy);
		globCmdParse(szCommandString);
		if (sbPauseOnExit)
			system("pause");
	}
	else
	{
		CreateThread(0, 0, CommMonitor, 0, 0, &dummy);
		newConsoleWindow();
		showConsoleWindow();

		while (1)
		{
			promptUserInput();
		}
	}

	spQuitComm = true;

	EXCEPTION_HANDLER_END
	return 0;
}
