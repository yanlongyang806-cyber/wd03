#pragma once

typedef void NetLinkPrintfCB(char *pLinkName, int iLinkID, char *pString, int iFGColor, int iBGColor);

void RegisterToReceiveNetLinkPrintfs(int iPortNum, int iMaxConnected, NetLinkPrintfCB *pReceivePrintfCB);
void AttemptToConnectNetLinkForPrintf(char *pServerName, int iPortNum, int iId, char *pName);

void ReceiveNetLinkPrints_Monitor(void);