#include "ContinuousBuilderSupport.h"
#include "globalComm.h"
#include "estring.h"
#include "net.h"
#include "winInclude.h"
#include "utils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc);) ;


bool g_isContinuousBuilder = false; //retroactive errors behave differently on continuous builders
AUTO_CMD_INT(g_isContinuousBuilder, IsContinuousBuilder) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

//send update strings to the CB that is running, but don't do any of the other g_isContinuousBuilder behaviors
//(used for patchclient during prod builds where we want timing info, but aren't really testing patchclient per se)
static bool sbSendUpdatesToContinuousBuilder = false;
AUTO_CMD_INT(sbSendUpdatesToContinuousBuilder, SendUpdatesToContinuousBuilder) ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);

static NetLink *spLinkToContinuousBuilder = NULL;
static NetComm *spCBComm = NULL;

//when comments are reported back up to the CB via the netlink, the CB needs to know which context they belong in
//note that this can have spaces in it
static char *spContextNameOnCB = NULL;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_COMMANDLINE;
void ContextNameOnCB(ACMD_SENTENCE pName)
{
	estrCopy2(&spContextNameOnCB, pName);
	printf("CB context name set to: %s\n", pName);
}

CRITICAL_SECTION sCBCriticalSection = {0};

AUTO_RUN;
void CBSupportInit(void)
{
	if (g_isContinuousBuilder)
	{
		InitializeCriticalSection(&sCBCriticalSection);
	}
}




static void AttemptCBConnection(void)
{
	if (!spCBComm)
	{
		spCBComm = commCreate(0, 0);
	}

	if (!spLinkToContinuousBuilder)
	{
		spLinkToContinuousBuilder = commConnect(spCBComm,LINKTYPE_UNSPEC, LINK_FORCE_FLUSH,
			"localhost", CONTINUOUS_BUILDER_PORT,
			0,0,0,0);

		if (!linkConnectWait(&spLinkToContinuousBuilder,2.f))
		{
			return;
		}
	}
}


void CBSupport_StopErrorTracking(void)
{
	Packet *pPacket;

	if (!g_isContinuousBuilder)
	{
		return;
	}

	EnterCriticalSection(&sCBCriticalSection);

	AttemptCBConnection();

	if (spLinkToContinuousBuilder)
	{
		pPacket = pktCreate(spLinkToContinuousBuilder, TO_CONTINUOUSBUILDER_STOP_ERRORTRACKING);
		pktSend(&pPacket);
		linkFlush(spLinkToContinuousBuilder);
	}

	LeaveCriticalSection(&sCBCriticalSection);
}

void CBSupport_StartErrorTracking(void)
{
	Packet *pPacket;

	if (!g_isContinuousBuilder)
	{
		return;
	}

	EnterCriticalSection(&sCBCriticalSection);

	AttemptCBConnection();

	if (spLinkToContinuousBuilder)
	{
		pPacket = pktCreate(spLinkToContinuousBuilder, TO_CONTINUOUSBUILDER_START_ERRORTRACKING);
		pktSend(&pPacket);
		linkFlush(spLinkToContinuousBuilder);
	}

	LeaveCriticalSection(&sCBCriticalSection);
}

void CBSupport_PauseTimeout(int iNumSeconds)
{
	Packet *pPacket;

	if (!g_isContinuousBuilder)
	{
		return;
	}

	EnterCriticalSection(&sCBCriticalSection);

	AttemptCBConnection();

	if (spLinkToContinuousBuilder)
	{
		pPacket = pktCreate(spLinkToContinuousBuilder, TO_CONTINUOUSBUILDER_PAUSE_TIMEOUT);
		pktSendBits(pPacket, 32, iNumSeconds);
		if (spContextNameOnCB)
		{
			pktSendString(pPacket, spContextNameOnCB);
		}
		pktSend(&pPacket);
		linkFlush(spLinkToContinuousBuilder);
	}

	LeaveCriticalSection(&sCBCriticalSection);
}

void SendStringToCB(CBStringType eType, char *pString, ...)
{
	char *pFullString = NULL;
	Packet *pPacket;
	int iPacketType;

	estrGetVarArgs(&pFullString, pString);

	consolePushColor();
	consoleSetColor(COLOR_GREEN | COLOR_BLUE | COLOR_HIGHLIGHT, 0);
	printf("Build comment: %s\n", pFullString);
	consolePopColor();

	if (!(g_isContinuousBuilder || sbSendUpdatesToContinuousBuilder))
	{
		estrDestroy(&pFullString);
		return;
	}

	EnterCriticalSection(&sCBCriticalSection);

	AttemptCBConnection();

	if (spLinkToContinuousBuilder)
	{
		switch (eType)
		{
		xcase CBSTRING_COMMENT:
			iPacketType = TO_CONTINUOUSBUILDER_COMMENT;
		xcase CBSTRING_SUBSTATE:
			iPacketType = TO_CONTINUOUSBUILDER_SUBSTATE;
		xcase CBSTRING_SUBSUBSTATE:
			iPacketType = TO_CONTINUOUSBUILDER_SUBSUBSTATE;
		xdefault:
			assert(0);
		}

		pPacket = pktCreate(spLinkToContinuousBuilder, iPacketType);
		pktSendString(pPacket, pFullString);
		if (spContextNameOnCB)
		{
			pktSendString(pPacket, spContextNameOnCB);
		}
		pktSend(&pPacket);
		linkFlush(spLinkToContinuousBuilder);
		estrDestroy(&pFullString);
	}

	LeaveCriticalSection(&sCBCriticalSection);

}

char *GetCBDataCorruptionComment(void)
{
	if (g_isContinuousBuilder)
	{
		return " Delete this file from the Continuous Builder to force it to rebuild.";
	}
	else
	{
		return "";
	}
}

char *CBSupport_GetSpawningCommandLine(void)
{
	static char *spRetVal = NULL;
	char *pContextNameSuperEsc = NULL;
	
	if (!g_isContinuousBuilder)
	{
		return "";
	}

	estrPrintf(&spRetVal, " -IsContinuousBuilder ");

	if (gbLeaveCrashesUpForever)
	{
		estrConcatf(&spRetVal, " -LeaveCrashesUpForever ");
	}

	if (spContextNameOnCB)
	{
		estrSuperEscapeString(&pContextNameSuperEsc, spContextNameOnCB);
		estrConcatf(&spRetVal, " -SuperEsc ContextNameOnCB %s ", pContextNameSuperEsc);
		estrDestroy(&pContextNameSuperEsc);
	}

	return spRetVal;
}