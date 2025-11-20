/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "cmdXBox.h"
#include "xbox\XSession.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void xBoxSessionCreate(U32 iTeamId)
{
#if _XBOX
	xSession_CreateSession(iTeamId);
#endif
}