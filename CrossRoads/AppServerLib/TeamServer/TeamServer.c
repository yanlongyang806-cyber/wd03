/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "TeamServer.h"
#include "AppServerLib.h"
#include "aslTeamServerInit.h"


AUTO_RUN;
int RegisterTeamServer(void)
{
	aslRegisterApp(GLOBALTYPE_TEAMSERVER, TeamServerLibInit, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);

	return 1;
}
