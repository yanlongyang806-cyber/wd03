/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GlobalTypes.h"
#include "sysutil.h"

#include "AppServerLib.h"
#include "winutil.h"

int main(int argc, char** argv)
{
	EXCEPTION_HANDLER_BEGIN
	WAIT_FOR_DEBUGGER

	RegisterGenericGlobalTypes(); // We need to call these now, so the parsing works
	parseGlobalTypeArgc(argc, argv, GLOBALTYPE_NONE);

	DO_AUTO_RUNS;

	assertmsg(GetAppGlobalType() != GLOBALTYPE_NONE, "Didn't find -ContainerType in AppServer");

	setConsoleTitle(GlobalTypeToName(GetAppGlobalType()));
	setWindowIconColoredLetter(compatibleGetConsoleWindow(), (GlobalTypeToName(GetAppGlobalType()))[0], 0x8080ff);


	// First, call the universal setup stuff
	aslPreMain(GetProjectName(), argc, argv);

	//calls app-specific app-init
	aslStartApp();


	aslMain();

	EXCEPTION_HANDLER_END
	return 0;
}
