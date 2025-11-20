/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "Alerts.h"
#include "AutoStartupSupport.h"
#include "GlobalTypes.h"
#include "file.h"
#include "FolderCache.h"
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
#include "gimmeDLLWrapper.h"
#include "dirMonitor.h"
#include "../../libs/graphicsLib/pub/GfxTextureTools.h"
#include "AutoGen/Controller_autogen_remotefuncs.h"
#include "AutoGen/ServerLib_autogen_remotefuncs.h"

#define	PHYSX_SRC_FOLDER "../../3rdparty"

#include "PhysicsSDK.h"



AUTO_STARTUP(TextureServer) ASTRT_DEPS(GraphicsLibEarly );
void dummyStartup(void)
{

}


int main(int argc, char **argv)
{
	int maintimer = 0, frametimer = 0;
	F32 fClientStartTime = 0.0f;

	EXCEPTION_HANDLER_BEGIN;
	WAIT_FOR_DEBUGGER;

	gimmeDLLDisable(1);
	RegisterGenericGlobalTypes();
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_TEXTURESERVER);

	DO_AUTO_RUNS;

	dirMonSetBufferSize(NULL, 2*1024);
	FolderCacheChooseMode();
	FolderCacheEnableCallbacks(0);
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), 'T', 0x408040);
	serverLibStartup(argc, argv);

	loadstart_printf("Connecting TextureServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
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


	AutoStartup_SetTaskIsOn("TextureServer", 1);


	loadstart_printf("Running auto startup...");
	DoAutoStartup();
	loadend_printf("...done.");

	resFinishLoading();
	stringCacheFinalizeShared();

	maintimer = timerAlloc();
	timerStart(maintimer);
	frametimer = timerAlloc();
	timerStart(frametimer);




	{
		char buffer[256];
		sprintf(buffer, "%s %d", GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID());
		setConsoleTitle(buffer);
	}

	RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "ready");


	while(1)
	{
	

		autoTimerThreadFrameBegin("main");
		utilitiesLibOncePerFrame(timerElapsedAndStart(frametimer), 1.0f);
		serverLibOncePerFrame();
		commMonitor(commDefault());
	
		autoTimerThreadFrameEnd();
	}

	EXCEPTION_HANDLER_END
	return 0;
}

AUTO_COMMAND;
void SavePNG(char *pTexName)
{
	char fileName[CRYPTIC_MAX_PATH];
	sprintf(fileName, "c:\\temp\\%s.png", pTexName);
	gfxSaveTextureAsPNG(pTexName, fileName, false, NULL);
}

AUTO_COMMAND_REMOTE;
void GetTexture(char *pTexName, GlobalType eCallingServerType, ContainerID iCallingServerID, int iRequestID)
{
	TextParserBinaryBlock *pBlock;
	StuffBuff sb;

	initStuffBuff(&sb, 10*1024);

	if (!gfxSaveTextureAsPNG_StuffBuff(pTexName, &sb, false))
	{
		RemoteCommand_HereIsTextureFromTextureServer(eCallingServerType, iCallingServerID, iRequestID, NULL, "Unable to get PNG file");
		goto cleanup_and_exit;
	}

	pBlock = TextParserBinaryBlock_CreateFromMemory(sb.buff, sb.idx, false);
	if (!pBlock)
	{
		RemoteCommand_HereIsTextureFromTextureServer(eCallingServerType, iCallingServerID, iRequestID, NULL, 
			"Unable to create TPBinaryBlock");
		goto cleanup_and_exit;
	}
	
	RemoteCommand_HereIsTextureFromTextureServer(eCallingServerType, iCallingServerID, iRequestID, pBlock, "");

	StructDestroy(parse_TextParserBinaryBlock, pBlock);

cleanup_and_exit:
	freeStuffBuff(&sb);
}