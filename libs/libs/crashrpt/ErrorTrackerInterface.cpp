#include "sendprogressdlg.h"

extern "C"
{
#include "superassert.h"

#include "stdtypes.h"
#include "sysutil.h"
#include "timing.h"
#include "../serverlib/pub/serverlib.h"

#include "errortrackerinterface.h"
#include "sock.h"
#include "utils.h"
#include "cmdparse.h"

#include "estring.h"
#include "textparserutils.h"

#include "file.h"
#include "errornet.h"
#include "autogen/errornet_h_ast.h"

	int sendLogToTrackerDumpFlags = 0;
}

// Wait 5000ms for the server to say what kind of dump we should be sending
#define WAIT_FOR_DUMPFLAGS_TIMEOUT 5000

// This must match the value in IncomingData.c!
// TODO: Move this into a header
#define DUMP_READ_BUFFER_SIZE 1024

static char szCrashMachineName[128] = "";

static unsigned char readBuffer[DUMP_READ_BUFFER_SIZE];
static char *pDumpFilename;

static int iErrorTrackerDumpFlags = 0;
static int iErrorTrackerDumpID = 0;
static bool bDumpFlagsReceived = false;

static NetComm *netComm = NULL;

static void ReceiveMsg(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch(cmd)
	{
	case FROM_ERRORTRACKER_DUMPFLAGS:
		{
			iErrorTrackerDumpFlags = pktGetU32(pkt);
			iErrorTrackerDumpID    = pktGetU32(pkt);
			bDumpFlagsReceived = true;
		}
		break;
	};
}

CREXTERN void CREXPORT swSetErrorTracker(const char *pErrorTracker)
{
	strcpy_s(SAFESTR(szCrashMachineName), pErrorTracker);
}

CREXTERN void CREXPORT swSendErrorToTracker(SendErrorToTrackerParams *pParams)
{
	U32 iTime;
	char *pExecutableName;

	if(szCrashMachineName[0] == 0)
	{
		return;
	}

	NetLink *pErrorTrackerLink;

	int iCmdLength = 0;
	char *pTempCmd = NULL;
	char *pCurrCmd = NULL;
	char *pTokenContext = NULL; // Yay, strtok_s()

	Packet *pPak;

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	iTime = timeSecondsSince2000();
	pExecutableName = getExecutableName();

	pErrorTrackerLink = commConnect(netComm,LINK_FORCE_FLUSH,szCrashMachineName,DEFAULT_ERRORTRACKER_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(pErrorTrackerLink,2.f))
	{
		linkRemove(&pErrorTrackerLink);
		return;
	}

	iErrorTrackerDumpFlags = 0;
	bDumpFlagsReceived = false;

	// ---------------------------------------------------------------------------------
	// Send Error Data!

	// Since pParams->errorData is coming across a DLL boundary, we cannot assume that
	// anything we added to the ErrorData struct is there. This avoids that nastiness.

	// NOTE: Hopefully this will all go away soon, along with the crash DLL.

	ErrorData tempErrorData;
	memset(&tempErrorData, 0, sizeof(ErrorData));

	// V1 Stuff - Guaranteed to be there
	tempErrorData.eType                     = pParams->errorData.eType;
	tempErrorData.pPlatformName             = pParams->errorData.pPlatformName;
	tempErrorData.pExecutableName           = pParams->errorData.pExecutableName;
	tempErrorData.pProductName              = pParams->errorData.pProductName;
	tempErrorData.pVersionString            = pParams->errorData.pVersionString;
	tempErrorData.pErrorString              = pParams->errorData.pErrorString;
	tempErrorData.pUserWhoGotIt             = pParams->errorData.pUserWhoGotIt;
	tempErrorData.pSourceFile               = pParams->errorData.pSourceFile;
	tempErrorData.iSourceFileLine           = pParams->errorData.iSourceFileLine;
	tempErrorData.pDataFile                 = pParams->errorData.pDataFile;
	tempErrorData.iDataFileModificationTime = pParams->errorData.iDataFileModificationTime;
	tempErrorData.iClientCount              = pParams->errorData.iClientCount;
	tempErrorData.pTrivia                   = pParams->errorData.pTrivia;

	// --- Added 06/06/07 ---
	if(pParams->paramsSize == sizeof(SendErrorToTrackerParams))
	{
		tempErrorData.iProductionMode = pParams->errorData.iProductionMode;
	}
	else
	{
		tempErrorData.iProductionMode = 0;
	}

	pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_ERROR);
	putErrorDataIntoPacket(pPak, &tempErrorData);
	pktSend(&pPak);
	commMonitor(netComm);

	// ---------------------------------------------------------------------------------
	// Wait for a few seconds to see if the server wants a dump from us

	DWORD start_tick = GetTickCount();
	while(!bDumpFlagsReceived)
	{
		Sleep(1);
		commMonitor(netComm);

		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_DUMPFLAGS_TIMEOUT))
		{
			break;
		}
	}

	commMonitor(netComm);
	Sleep(1500);
	linkRemove(&pErrorTrackerLink);

	if(pParams->pOutputFlags)
	{
		*pParams->pOutputFlags = iErrorTrackerDumpFlags;
	}
}

CREXTERN void CREXPORT swSendDumpToTracker(SendDumpToTrackerParams *pParams)
{
	// ---------------------------------------------------------------------------------
	// Send dump!

	CSendProgressDlg dlg;
	MSG msg;

	NetLink *pErrorTrackerLink;

	Packet *pPak;

	if(szCrashMachineName[0] == 0)
	{
		return;
	}

	if (!netComm)
		netComm = commCreate(0,0); // Must be non-threaded!

	pErrorTrackerLink = commConnect(netComm,LINK_FORCE_FLUSH,szCrashMachineName,DEFAULT_ERRORTRACKER_PORT, ReceiveMsg,0,0,0);
	if (!linkConnectWait(pErrorTrackerLink,2.f))
	{
		linkRemove(&pErrorTrackerLink);
		return;
	}

	// Mini or full?
	if(iErrorTrackerDumpFlags & DUMPFLAGS_FULLDUMP)
	{
		pDumpFilename = assertGetFullDumpFilename();
	}
	else if(iErrorTrackerDumpFlags & DUMPFLAGS_MINIDUMP)
	{
		pDumpFilename = assertGetMiniDumpFilename();
	}

	if(pDumpFilename && fileExists(pDumpFilename))
	{
		int iSentBytes = 0;
		int iTotalBytes;

		// ** Can't do this ... potentially causes all kinds of mayhem if 
		// ** fileLoadDataDirs() has to be called
		// iTotalBytes = fileSize(pDumpFilename);

		FILE *pDumpFile = (FILE*)fopen(pDumpFilename, "rb");
		if(pDumpFile != NULL)
		{
			fseek(pDumpFile, 0, SEEK_END);
			iTotalBytes = ftell(pDumpFile);
			fseek(pDumpFile, 0, SEEK_SET);

			if(iTotalBytes > 0)
			{
				dlg.Create(NULL, 0);
				dlg.ShowWindow(SW_SHOW);
				dlg.UpdateWindow();

				pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_START);
				pktSendU32(pPak, iErrorTrackerDumpFlags);
				pktSend(&pPak);
				commMonitor(netComm);

				while(!dlg.isDone)
				{
					// Pump some messages for our progress dialog
					while(PeekMessage(&msg,NULL,0,0,PM_NOREMOVE))
					{
						PeekMessage( &msg, NULL,0,0,PM_REMOVE);
						TranslateMessage( &msg);
						DispatchMessage( &msg);
					}

					int iAmountLeft   = iTotalBytes - iSentBytes;
					int iAmountToRead = min(DUMP_READ_BUFFER_SIZE, iAmountLeft);

					if(iAmountToRead > 0)
					{
						// Read in a bit more of the chunk
						int iReadBytes = fread(readBuffer, 1, DUMP_READ_BUFFER_SIZE, pDumpFile);

						if(iReadBytes > 0)
						{
							iSentBytes += iReadBytes;

							// update the progress control
							dlg.UpdateStatus(iSentBytes, iTotalBytes);

							// and send it
							pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_CONTINUE);
							pktSendU32(pPak, iReadBytes); // How many bytes in the packet?
							pktSendU32(pPak, iSentBytes); // How many bytes should be in the file now? (sanity check)
							pktSendBytes(pPak, iReadBytes, readBuffer);
							pktSend(&pPak);
							commMonitor(netComm);
						}
						else
						{
							// Nothing left to read!
							break;
						}
					}
					else
					{
						// Bail out, somehow the file read is failing.
						break;
					}
				}

				dlg.DestroyWindow();

				if(iSentBytes == iTotalBytes)
				{
					pPak = pktCreate(pErrorTrackerLink, TO_ERRORTRACKER_DUMP_FINISH);
					pktSendU32(pPak, iTotalBytes);         // How many bytes should be in the final file?
					pktSendU32(pPak, iErrorTrackerDumpID); // Maps this dump to a particular crash
					pktSend(&pPak);
					commMonitor(netComm);
				}

				fclose(pDumpFile);
			}
		}
	}
	// ---------------------------------------------------------------------------------

	commMonitor(netComm);
	Sleep(1500);
	linkRemove(&pErrorTrackerLink);
}	
