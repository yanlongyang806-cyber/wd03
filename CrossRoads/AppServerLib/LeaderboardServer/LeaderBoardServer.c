/***************************************************************************
*     Copyright (c) 2005-2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "aslLeaderboardServerInit.h"

AUTO_RUN;
int RegisterLeaderboardServer(void)
{
	aslRegisterApp(GLOBALTYPE_LEADERBOARDSERVER, LeaderboardServerLibInit, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);

	return 1;
}