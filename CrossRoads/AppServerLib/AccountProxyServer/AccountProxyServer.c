/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "aslAccountProxyServerInit.h"

AUTO_RUN;
int RegisterAccountProxyServer(void)
{
	aslRegisterApp(GLOBALTYPE_ACCOUNTPROXYSERVER, AccountProxyServerLibInit, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);

	return 1;
}