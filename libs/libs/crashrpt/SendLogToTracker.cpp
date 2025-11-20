extern "C"
{
#include "superassert.h"

#include "stdtypes.h"
#include "sysutil.h"
#include "timing.h"
#include "../serverlib/pub/serverlib.h"

#include "sendlogtotracker.h"
#include "sock.h"
#include "utils.h"
#include "cmdparse.h"

	char userName[64] = "";

}


CREXTERN void CREXPORT swSendLogToTracker(const char *pErrorString, const char *pCommandString, const char *pVersionString)
{


	U32 iTime;
	char *pExecutableName;

	NetLink *pLinkToControllerTracker;

	Packet *pPak;
	static NetComm *netComm = NULL;
	
	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	globCmdParse(pCommandString);

	iTime = timeSecondsSince2000();
	pExecutableName = getExecutableName();

	pLinkToControllerTracker = commConnect(netComm,LINK_FORCE_FLUSH,"CrashMachine",DEFAULT_CONTROLLERTRACKER_PORT,0,0,0,0);
	if (!linkConnectWait(pLinkToControllerTracker,2.f))
	{
		linkRemove(&pLinkToControllerTracker);
		return;
	}

	pPak = pktCreate(pLinkToControllerTracker, TO_CONTROLLERTRACKER_ERRORREPORT);

	pktSendString(pPak, pErrorString);
	pktSendString(pPak, pVersionString);
	pktSendString(pPak, userName[0] ? userName : getComputerName());
	pktSendString(pPak, pExecutableName);
	pktSendBits(pPak, 32, iTime);

	pktSend(&pPak);
	commMonitor(netComm);
	Sleep(1);
	linkRemove(&pLinkToControllerTracker);
}	
