/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "HeadshotServer.h"
#include "Alerts.h"
#include "AutoStartupSupport.h"
#include "ClientControllerLib.h"
#include "DirMonitor.h"
#include "gimmeDLLWrapper.h"
#include "GameClientLib.h"
#include "GlobalTypes.h"
#include "file.h"
#include "FolderCache.h"
#include "mathutil.h"
#include "MemReport.h"
#include "objTransactions.h"
#include "ResourceManager.h"
#include "serverlib.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "sysutil.h"
#include "timing.h"
#include "timing_profiler_interface.h"
#include "url.h"
#include "UtilitiesLib.h"
#include "winutil.h"
#include "headshotUtils.h"

#include "HeadShotServer_c_ast.h"

#include "AutoGen/Controller_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_commands_autogen_CommandFuncs.h"
#include "AutoGen/headshotUtils_h_ast.h"

static StashTable sHeadshotHandlesByID = NULL;
static StashTable sTextureViewerRequestHandlesByID = NULL;
static bool gbCanAcceptRequests = false;

int giClientStartTimeout = 300;
AUTO_CMD_INT(giClientStartTimeout, ClientStartTimeout);

bool gbGetCostumesFromClone = false;
AUTO_CMD_INT(gbGetCostumesFromClone, GetCostumesFromClone);

AUTO_RUN_SECOND;
void HeadshotServer_InitStringCache(void)
{
	if(isProductionMode())
	{
		stringCacheSetInitialSize(8*1024);
	}
	else
	{
		stringCacheSetInitialSize(800*1024);
	}
}

AUTO_STARTUP(HeadshotServer, 1) ASTRT_DEPS(AS_Messages);
void HeadshotServer_AutoStartup(void)
{
	// do nothing
}

int main(int argc, char **argv)
{
	int maintimer = 0, frametimer = 0;
	F32 fClientStartTime = 0.0f;

	EXCEPTION_HANDLER_BEGIN;
	WAIT_FOR_DEBUGGER;

	gimmeDLLDisable(1);
	RegisterGenericGlobalTypes();
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_HEADSHOTSERVER);

	DO_AUTO_RUNS;

	dirMonSetBufferSize(NULL, 2*1024);
	FolderCacheChooseMode();
	FolderCacheEnableCallbacks(0);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'C', 0x808080);
	serverLibStartup(argc, argv);
	ClientController_InitLib();

	loadstart_printf("Connecting HeadshotServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	while (!InitObjectTransactionManager(GetAppGlobalType(), gServerLibState.containerID, gServerLibState.transactionServerHost, gServerLibState.transactionServerPort, gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}
	loadend_printf("connected.");

	loadstart_printf("Running auto startup...");
	DoAutoStartup();
	loadend_printf("...done.");

	resFinishLoading();
	stringCacheFinalizeShared();

	maintimer = timerAlloc();
	timerStart(maintimer);
	frametimer = timerAlloc();
	timerStart(frametimer);

	if(!ClientController_MonitorCrashBeganEvents(HEADSHOT_CRASHBEGAN_EVENT) || !ClientController_MonitorCrashCompletedEvents(HEADSHOT_CRASHCOMPLETED_EVENT))
	{
		ErrorOrAlert("HEADSHOT_EVENTMONITORING_FAILED", "Failed to establish Windows events to monitor Headshot Client. Might not get proper reporting of Headshot Client crashes.");
	}

	{
		char buffer[256];
		sprintf(buffer, "%s %d", GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID());
		setConsoleTitle(buffer);
	}

	while(1)
	{
		static bool bWasConnected = false;
		static bool bWasCrashed = false;
		ClientControllerState eState;

		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsedAndStart(frametimer), 1.0f);
		serverLibOncePerFrame();
		commMonitor(commDefault());
		eState = ClientController_MonitorState();

		switch(eState)
		{
		case CC_NOT_RUNNING:
			ClientController_StartClient(true, true, "-settestingmode -maxfps 60 -maxinactivefps 60 -forceloadcostumes -notimeout -noaudio -minimalrendering -windowed -gfxheadshotservermode");
			fClientStartTime = timerElapsed(maintimer);
			RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientStarting");
		xcase CC_RUNNING:
			if(bWasConnected)
			{
				// Client was connected but isn't anymore, it may have gone away in a very bad way
				// This would be superseded by the "crash began" event if the client had crashed, since CE wouldn't kill the exe's links and there's no link timeout
				ClientController_KillClient();
				bWasConnected = false;
				RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientDisconnected");
				TriggerAlertf("HEADSHOTCLIENT_DISCONNECT", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, "Headshot Client has disconnected inexplicably! Headshot service will be temporarily interrupted while the client is restarted. Please check %s for a client dump and contact Vinay to investigate.", getHostName());
			}
			else if(timerElapsed(maintimer) - fClientStartTime > giClientStartTimeout)
			{
				ClientController_KillClient();
				RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientNeverStarted");
				TriggerAlertf("HEADSHOTCLIENT_NEVERSTARTED", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, "Headshot Client failed to start after %d seconds! There might be something wrong with the machine or the executable. Killing it and trying again.", giClientStartTimeout);
			}
		xcase CC_CONNECTED:
			// Client is connected, everything is set, we don't need to do anything magical here
			if(!bWasConnected)
			{
				bWasConnected = true;
				gbCanAcceptRequests = true;
				printf("Headshot Client now connected.\n");
				RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientIsReady");
			}
		xcase CC_CRASHED:
			if(!bWasCrashed)
			{
				bWasCrashed = true;
				bWasConnected = false;
				gbCanAcceptRequests = false;
				RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ClientCrashed");
				TriggerAlertf("HEADSHOTCLIENT_CRASH", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(), 0, "Headshot Client has crashed! Headshot service will be temporarily interrupted while CrypticError processes the client dump. Please verify that the Headshot Client restarts on %s within 5 minutes - if it doesn't, contact Vinay to investigate.", getHostName());
			}
		xcase CC_CRASH_COMPLETE:
			bWasCrashed = false;
			ClientController_KillClient();
		}
		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END
	return 0;
}

HeadshotRequestHandle *HeadshotServer_GetHeadshotRequestHandle(char *pName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	static int siNextRequestID = 1;
	U32 iContainerID;
	HeadshotRequestHandle *pHandle;
	HeadshotRequestHandle tempHandle = {0};
	char *pTemp = NULL;
	char *pExt = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(!gbCanAcceptRequests)
	{
		pCB(NULL, 0, 0, "Headshot client not ready", pUserData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}
	
	pExt = strchr(pName, '.');

	if((pTemp = strchr(pName, '_')))
	{
		*pTemp = 0;
		*pExt = 0;
		++pTemp;
		tempHandle.eType = NameToGlobalType(pTemp);
		--pTemp;
		*pTemp = '_';
		*pExt = '.';
	}

	if(!tempHandle.eType)
	{
		tempHandle.eType = GLOBALTYPE_ENTITYPLAYER;
	}

	if (!StringToUint(pName, &iContainerID) || !iContainerID)
	{
		pCB(NULL, 0, 0, "Bad headshot request syntax", pUserData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	tempHandle.iImageSizeX = 256;
	if (urlFindBoundedInt(pArgList, "jpgSizeX", &tempHandle.iImageSizeX, 32, 1024) == -1)
	{
		pCB(NULL, 0, 0, "bad jpgSizeX syntax", pUserData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	tempHandle.iImageSizeY = 256;
	if (urlFindBoundedInt(pArgList, "jpgSizeY", &tempHandle.iImageSizeY, 32, 1024) == -1)
	{
		pCB(NULL, 0, 0, "bad jpgSizeY syntax", pUserData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	switch(urlFindVec3(pArgList, "jpgCamPos", tempHandle.camPos))
	{
	case 1:
		tempHandle.bGotCamPos = true;
		break;
	case -1:
		pCB(NULL, 0, 0, "bad jpgCamPos syntax", pUserData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	switch(urlFindVec3(pArgList, "jpgCamDir", tempHandle.camDir))
	{
	case 1:
		tempHandle.bGotCamDir = true;
		break;
	case -1:
		pCB(NULL, 0, 0, "bad jpgCamDir syntax", pUserData);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	strcpy(tempHandle.bgTexName, urlFindSafeValue(pArgList, "jpgTexName"));

	strcpy(tempHandle.achStyle, urlFindSafeValue(pArgList, "jpgStyle"));
	strcpy(tempHandle.achFrame, urlFindSafeValue(pArgList, "jpgFrame"));
	strcpy(tempHandle.pPoseInfo, urlFindSafeValue(pArgList, "jpgPose"));
	pTemp = strdup(urlFindSafeValue(pArgList, "animDelta"));
	if(strcmp(pTemp, "") == 0)
	{
		tempHandle.fAnimDelta = 0.1;
	}
	else
	{
		tempHandle.fAnimDelta = max(0.1, atof(pTemp));
	}
	free(pTemp);

	pTemp = strdup(urlFindSafeValue(pArgList, "jpgCamFrame"));
	if(strstri(pTemp, "body") != NULL)
	{
		tempHandle.bForceBodyshot = true;
	}
	else
	{
		tempHandle.bForceBodyshot = false;
	}
	free(pTemp);

	pTemp = strdup(urlFindSafeValue(pArgList, "transparent"));
	if(strcmp(pTemp, "") == 0)
	{
		tempHandle.bTransparent = false;
	}
	else
	{
		tempHandle.bTransparent = (bool)atoi(pTemp);
	}

	pHandle = calloc(sizeof(HeadshotRequestHandle), 1);

	memcpy(pHandle, &tempHandle, sizeof(HeadshotRequestHandle));

	pHandle->iRequestID = siNextRequestID++;
	pHandle->iContainerID = iContainerID;
	sprintf(pHandle->fileName, "%s/HeadShot_%d%s", fileTempDir(), pHandle->iRequestID, pExt);
	mkdirtree(pHandle->fileName);

	pHandle->pJpegCB = pCB;
	pHandle->pJpegUserData = pUserData;

	if (!sHeadshotHandlesByID)
	{
		sHeadshotHandlesByID = stashTableCreateInt(16);
	}

	stashIntAddPointer(sHeadshotHandlesByID, pHandle->iRequestID, pHandle, true);
	PERFINFO_AUTO_STOP();
	return pHandle;
}

void HeadshotServer_GetHeadshotJpegCB(char *pName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	HeadshotRequestHandle *pHandle = HeadshotServer_GetHeadshotRequestHandle(pName, pArgList, pCB, pUserData);
	if (pHandle)
		RemoteCommand_RequestPlayerCostumeString(gbGetCostumesFromClone ? GLOBALTYPE_CLONEOBJECTDB : GLOBALTYPE_OBJECTDB, 0, pHandle->eType, pHandle->iContainerID, GetAppGlobalType(), GetAppGlobalID(), pHandle->iRequestID);
}

void HeadshotServer_ReturnPlayerCostumeString(int iRequestID, const char *pchCostume, int iVersion);

void HeadshotServer_GetHeadshotJpeg(char *pName, UrlArgumentList *pArgList, const char* pchCostume, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	HeadshotRequestHandle *pHandle = HeadshotServer_GetHeadshotRequestHandle(pName, pArgList, pCB, pUserData);
	if (pHandle)
		HeadshotServer_ReturnPlayerCostumeString(pHandle->iRequestID, pchCostume, 5); // Not quite sure what 5 means. 
}

HeadshotRequestHandle *GetHandleFromID(int iRequestID)
{
	HeadshotRequestHandle *pRetVal;

	if (!sHeadshotHandlesByID)
	{
		return NULL;
	}

	if (stashIntFindPointer(sHeadshotHandlesByID, iRequestID, &pRetVal))
	{
		return pRetVal;
	}

	return NULL;
}

AUTO_COMMAND_REMOTE ACMD_NAME(ReturnPlayerCostumeString);
void HeadshotServer_ReturnPlayerCostumeString(int iRequestID, const char *pchCostume, int iVersion)
{
	HeadshotRequestHandle *pHandle = GetHandleFromID(iRequestID);

	PERFINFO_AUTO_START_FUNC();

	if (pHandle)
	{
		if (!pchCostume || *pchCostume == '\0')
		{
			pHandle->pJpegCB(NULL, 0, 0, "Couldn't get costume", pHandle->pJpegUserData);
			stashIntRemovePointer(sHeadshotHandlesByID, iRequestID, NULL);
			free(pHandle);
		}
		else
		{
			Vec3 size = { pHandle->iImageSizeX, pHandle->iImageSizeY, 0 };
			HeadshotRequestParams params = { 0 };
			char *estrParams = NULL;
			char *estrEscapedCostume = NULL;
			estrStackCreate(&estrEscapedCostume);
			estrAppendEscaped(&estrEscapedCostume, pchCostume);

			params.pFileName = pHandle->fileName;
			params.iRequestID = iRequestID;
			params.eContainerType = pHandle->eType;
			params.iContainerID = pHandle->iContainerID;
			copyVec3(size, params.size);
			params.bForceBodyshot = pHandle->bForceBodyshot;
			params.bTransparent = pHandle->bTransparent;
			params.bCostumeV0 = iVersion < 5;
			if(*pHandle->achStyle)
			{
				params.pchStyle = pHandle->achStyle;
			}
			else
			{
				if (*pHandle->achFrame)
				{
					params.pchFrame = pHandle->achFrame;
				}
				else
				{
					if (pHandle->bGotCamPos)
						copyVec3(pHandle->camPos, params.camPos);
					if (pHandle->bGotCamDir)
						copyVec3(pHandle->camDir, params.camDir);
				}
				params.pBGTextureName = pHandle->bgTexName;
				params.pPoseString = pHandle->pPoseInfo;
				params.animDelta = pHandle->fAnimDelta;
			}

			ParserWriteTextEscaped(&estrParams, parse_HeadshotRequestParams, &params, 0, 0, 0);
			cmd_WriteHeadshotForHeadshotServer(estrParams, estrEscapedCostume);
			estrDestroy(&estrParams);
			estrDestroy(&estrEscapedCostume);
		}
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(HeadshotComplete);
void HeadshotServer_HeadshotComplete(int iRequestID)
{
	HeadshotRequestHandle *pHandle = GetHandleFromID(iRequestID);
	
	PERFINFO_AUTO_START_FUNC();

	if (pHandle)
	{
		int iJpegDataSize;
		void *pJpegData = fileAlloc(pHandle->fileName, &iJpegDataSize);
		if (pJpegData)
		{
			pHandle->pJpegCB(pJpegData, iJpegDataSize, 300, NULL, pHandle->pJpegUserData);
			free(pJpegData);
		}
		else
		{
			pHandle->pJpegCB(NULL, 0, 0, "Mysterious headshot failure", pHandle->pJpegUserData);
		}

		stashIntRemovePointer(sHeadshotHandlesByID, iRequestID, NULL);
		free(pHandle);
	}

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(HeadshotFailed);
void HeadshotServer_HeadshotFailed(int iRequestID, ACMD_SENTENCE errorString)
{
	HeadshotRequestHandle *pHandle = GetHandleFromID(iRequestID);

	PERFINFO_AUTO_START_FUNC();

	if (pHandle)
	{
		pHandle->pJpegCB(NULL, 0, 0, errorString, pHandle->pJpegUserData);

		stashIntRemovePointer(sHeadshotHandlesByID, iRequestID, NULL);
		free(pHandle);
	}

	PERFINFO_AUTO_STOP();
}

void HeadshotServer_GetTexturePngCB(char *pName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	TextureViewerRequestHandle *pHandle;
	static int siNextRequestID = 1;

	PERFINFO_AUTO_START_FUNC();

	if(!gbCanAcceptRequests)
	{
		pCB(NULL, 0, 0, "Headshot client not ready", pUserData);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!strEndsWith(pName, ".png"))
	{
		pCB(NULL, 0, 0, "texture requests must be PNG", pUserData);
		PERFINFO_AUTO_STOP();
		return;
	}

	pHandle = calloc(sizeof(HeadshotRequestHandle), 1);

	pHandle->iRequestID = siNextRequestID++;
	sprintf(pHandle->fileName, "%s/TextureView_%d.png", fileTempDir(), pHandle->iRequestID);
	mkdirtree(pHandle->fileName);

	pHandle->pJpegCB = pCB;
	pHandle->pJpegUserData = pUserData;
	estrCopy2(&pHandle->pShortName, pName);
	estrSetSize(&pHandle->pShortName, estrLength(&pHandle->pShortName) - 4);

	if (!sTextureViewerRequestHandlesByID)
	{
		sTextureViewerRequestHandlesByID = stashTableCreateInt(16);
	}

	stashIntAddPointer(sTextureViewerRequestHandlesByID, pHandle->iRequestID, pHandle, true);

	cmd_WriteTextureForHeadshotServer(pHandle->pShortName, pHandle->iRequestID, pHandle->fileName);

	PERFINFO_AUTO_STOP();
}

AUTO_COMMAND ACMD_NAME(TextureComplete);
void HeadshotServer_TextureComplete(int iRequestID, int iSuccess, ACMD_SENTENCE errorString)
{
	TextureViewerRequestHandle *pHandle;

	PERFINFO_AUTO_START_FUNC();

	if (stashIntFindPointer(sTextureViewerRequestHandlesByID, iRequestID, &pHandle))
	{
		if (iSuccess)
		{
			int iJpegDataSize;
			void *pJpegData = fileAlloc(pHandle->fileName, &iJpegDataSize);
			if (pJpegData)
			{
				pHandle->pJpegCB(pJpegData, iJpegDataSize, 1000000000, NULL, pHandle->pJpegUserData);
				free(pJpegData);
			}
			else
			{
				iSuccess = false;
				errorString = "Could not load file that should exist";
			}
		}

		if (!iSuccess)
		{
			pHandle->pJpegCB(NULL, 0, 0, errorString, pHandle->pJpegUserData);
		}

		stashIntRemovePointer(sTextureViewerRequestHandlesByID, iRequestID, NULL);
		estrDestroy(&pHandle->pShortName);
		free(pHandle);
	}

	PERFINFO_AUTO_STOP();
}

AUTO_RUN;
void HeadshotServer_InitJpegLibrary(void)
{
	JpegLibrary_RegisterCB("HEADSHOT", HeadshotServer_GetHeadshotJpegCB);
	JpegLibrary_RegisterCB("TEXTURE", HeadshotServer_GetTexturePngCB);
}

AUTO_STRUCT;
typedef struct RemoteHeadShotUserData
{
	GlobalType eCallingServerType;
	ContainerID iCallingServerID;
	U32 iUserData;
} RemoteHeadShotUserData;

void GetHeadShotRemote_ReturnJpegCB(char *pData, int iDataSize, int iLifeSpan, char *pMessage, RemoteHeadShotUserData *pUserData)
{
	TextParserBinaryBlock *pBlock = NULL;

	if (pData)
	{
		pBlock = TextParserBinaryBlock_CreateFromMemory(pData, iDataSize, false);
	}

	RemoteCommand_GetHeadShot_Return(pUserData->eCallingServerType, pUserData->iCallingServerID, pBlock, pMessage, pUserData->iUserData);

	StructDestroySafe(parse_TextParserBinaryBlock, &pBlock);
	StructDestroy(parse_RemoteHeadShotUserData, pUserData);
}

//pFileName should be "17.jpg" to get the headshot for player 17, or "234_ENTITYSAVEDPET.jpg" to get the 
//headshot for pet 234
AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void GetHeadshot(char *pFileName, UrlArgumentList *pArgList, const char* pchCostume, GlobalType eCallingServerType, ContainerID iCallingServerID, U32 iUserData)
{
	RemoteHeadShotUserData *pUserData  = StructCreate(parse_RemoteHeadShotUserData);
	pUserData->eCallingServerType = eCallingServerType;
	pUserData->iCallingServerID = iCallingServerID;
	pUserData->iUserData = iUserData;

	HeadshotServer_GetHeadshotJpeg(pFileName, pArgList, pchCostume, GetHeadShotRemote_ReturnJpegCB, pUserData);
}

#include "HeadShotServer_c_ast.c"
