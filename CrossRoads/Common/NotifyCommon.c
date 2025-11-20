/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CostumeCommon.h"
#include "NotifyCommon.h"
#include "NotifyCommon_h_ast.h"
#include "NotifyCommon_h_ast.c"
#include "NotifyEnum_h_ast.h"

#if defined(GAMECLIENT) || defined(GAMESERVER)
#include "Entity.h"
#include "Expression.h"
#include "mission_common.h"

#ifndef GAMECLIENT
#include "GameServerLib.h"
#include "gslLogSettings.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#else
#include "gclNotify.h"
#include "gclEntity.h"
// Run budget mapping if gameclient
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#endif

// ----------------------------------------------------------------------------------------------------------------------
static void notfiy_LogActivity(	Entity *pEnt, 
								NotifyType eType, 
								const char *pchDisplayString, 
								const char *pcLogicalString, 
								const char *pcSound)
{
#ifndef GAMECLIENT
	if (gbEnableGamePlayEventLogging
		&& pEnt->pPlayer
		&& eType != kNotifyType_ExperienceReceived 
		&& eType != kNotifyType_NumericReceived)
	{
		char achType[1000];
		char ach[20];
		itoa(entGetContainerID(pEnt), ach, 10);
		sprintf(achType, "game-%s", StaticDefineIntRevLookup(NotifyTypeEnum, eType));
		logActivity(pEnt->pPlayer, achType, "character_ent", ach, "subject", pcLogicalString, "message", pchDisplayString, "sound", pcSound, NULL);
	}
#endif
}

// ----------------------------------------------------------------------------------------------------------------------
void notify_NotifySend(	Entity *pEnt, 
						NotifyType eType, 
						const char *pchDisplayString, 
						const char *pchLogicalString, 
						const char *pchTexture) 
{
	if (!pchDisplayString || !*pchDisplayString) 
		return;
		
#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySend(pEnt, eType, pchDisplayString, pchLogicalString, pchTexture);
		notfiy_LogActivity(pEnt, eType, pchDisplayString, pchLogicalString, NULL);
	}
#else 

	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyReceive(eType, pchDisplayString, pchLogicalString, pchTexture);

#endif
}

void notify_NotifySendMessageStruct( Entity *pEnt, 
						NotifyType eType, 
						MessageStruct *pFmt) 
{
	if (!pFmt || !pFmt->pchKey || !*pFmt->pchKey) 
		return;
		
#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySendStruct(pEnt, eType, pFmt);
		notfiy_LogActivity(pEnt, eType, pFmt->pchKey, NULL, NULL);
	}
#else 

	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyReceiveMessageStruct(eType, pFmt);

#endif
}


// ----------------------------------------------------------------------------------------------------------------------
void notify_NotifySendWithTag(	Entity *pEnt, 
								NotifyType eType, 
								const char *pchDisplayString, 
								const char *pchLogicalString, 
								const char *pchTexture, 
								const char * pchTag)
{
	if (!pchDisplayString || !*pchDisplayString) 
		return;

#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySendWithTag(pEnt, eType, pchDisplayString, pchLogicalString, pchTexture, pchTag);
		notfiy_LogActivity(pEnt, eType, pchDisplayString, pchLogicalString, NULL);
	}
#else 

	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyReceiveWithTag(eType, pchDisplayString, pchLogicalString, pchTexture, pchTag);

#endif
}


// ----------------------------------------------------------------------------------------------------------------------
void notify_NotifySendAudio(Entity *pEnt, 
							NotifyType eType, 
							const char *pchDisplayString, 
							const char *pchLogicalString, 
							const char *pchSound, 
							const char *pchTexture) 
{
	if (!pchDisplayString || !*pchDisplayString) 
		return;
#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySendAudio(pEnt, eType, pchDisplayString, pchLogicalString, pchSound, pchTexture);
		notfiy_LogActivity(pEnt, eType, pchDisplayString, pchLogicalString, NULL);
	}
#else 
	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyReceiveAudio(eType, pchDisplayString, pchLogicalString, pchSound, pchTexture);
#endif
}


// ----------------------------------------------------------------------------------------------------------------------
void notify_NotifySendWithData(	Entity *pEnt, 
								NotifyType eType, 
								const char *pchDisplayString, 
								const char *pchLogicalString, 
								const char *pchSound, 
								const char *pchTexture, 
								const ChatData *pChatData)
{
	if (!pchDisplayString || !*pchDisplayString) 
		return;
#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySendWithData(pEnt, eType, pchDisplayString, pchLogicalString, pchSound, pchTexture, pChatData);
		notfiy_LogActivity(pEnt, eType, pchDisplayString, pchLogicalString, NULL);
	}
#else 
	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	
	gclNotifyReceiveWithData(eType, pchDisplayString, pchLogicalString, pchSound, pchTexture, pChatData);

#endif

}


// ----------------------------------------------------------------------------------------------------------------------
void notify_NotifySendWithHeadshot(	Entity *pEnt, 
									NotifyType eType, 
									const char *pchDisplayString, 
									const char *pchLogicalString, 
									const char *pchSound, 
									ContactHeadshotData *pHeadshotData)
{
	if (!pchDisplayString || !*pchDisplayString) 
		return;
#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySendWithHeadshot(pEnt, eType, pchDisplayString, pchLogicalString, pchSound, pHeadshotData);
		notfiy_LogActivity(pEnt, eType, pchDisplayString, pchLogicalString, pchSound);
	}
#else
	
	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyReceiveWithHeadshot(eType, pchDisplayString, pchLogicalString, pchSound, pHeadshotData);

#endif

}

// ----------------------------------------------------------------------------------------------------------------------
void notify_NotifySendWithOrigin(	Entity *pEnt, 
									NotifyType eType, 
									SA_PARAM_OP_STR const char *pchDisplayString, 
									SA_PARAM_OP_STR const char *pchLogicalString, 
									SA_PARAM_OP_STR const char *pchTag, 
									S32 iValue,
									const Vec3 vOrigin)
{
	if (!pchDisplayString || !*pchDisplayString) 
		return;
#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySendWithOrigin(pEnt, eType, pchDisplayString, pchLogicalString, pchTag, iValue, vOrigin);
		notfiy_LogActivity(pEnt, eType, pchDisplayString, pchLogicalString, NULL);
	}
#else 
	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyReceiveWithOrigin(eType, pchDisplayString, pchLogicalString, pchTag, iValue, vOrigin);
#endif
}

void notify_NotifySendWithItemID(	Entity *pEnt, 
									NotifyType eType, 
									SA_PARAM_OP_STR const char *pchDisplayString, 
									SA_PARAM_OP_STR const char *pchLogicalString, 
									SA_PARAM_OP_STR const char *pchTexture,
									U64 itemID,
									S32 iCount)
{
	if (!pchDisplayString || !*pchDisplayString) 
		return;
#ifndef GAMECLIENT
	if (pEnt)
	{
		ClientCmd_NotifySendWithItemID(pEnt, eType, pchDisplayString, pchLogicalString, pchTexture, itemID, iCount);
		notfiy_LogActivity(pEnt, eType, pchDisplayString, pchLogicalString, NULL);
	}
#else 
	assertmsg(!pEnt || pEnt == entActivePlayerPtr(), "Only the local entity can be notified from the client.");
	gclNotifyReceiveWithItemID(eType, pchDisplayString, pchLogicalString, pchTexture, itemID, iCount);
#endif

}

#endif

#include "NotifyEnum_h_ast.c"
