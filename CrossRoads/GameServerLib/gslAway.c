#include "EString.h"
#include "cmdparse.h"
#include "logging.h"

#include "Entity.h"
#include "GameStringFormat.h"
#include "chatCommon.h"
#include "gslChatConfig.h"
#include "gslCommandParse.h"
#include "gslLogSettings.h"
#include "gslTransactions.h"
#include "gslSendToClient.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "TextFilter.h"
#include "logging.h"
#include "gslSocial.h"
#include "file.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "Player_h_ast.h"

static S32 s_iAutoAwayIdleTime = 60 * 10;
static S32 s_iAwayKickWarnTime = 60 * 50;
static S32 s_iAwayKickTime = 60 * 60;
static S32 s_iAwayKickAccessLevel = ACCESS_USER;

static S32 s_iAwayKickWarnTime_ProdEditMode = 60 * 25;
static S32 s_iAwayKickTime_ProdEditMode = 60 * 30;

//__CATEGORY Settings that control player idle times that result in player going idle, getting warned, and getting kicked from the game.
// Change the autoaway idle time for the server, in seconds.
AUTO_CMD_INT(s_iAutoAwayIdleTime, AutoAwayIdleTime) ACMD_AUTO_SETTING(IdleTimes, GAMESERVER);

// Change the afk kick warning time for the server, in seconds.
AUTO_CMD_INT(s_iAwayKickWarnTime, AwayKickWarnTime) ACMD_AUTO_SETTING(IdleTimes, GAMESERVER);

// Change the afk kick time for the server, in seconds.
AUTO_CMD_INT(s_iAwayKickTime, AwayKickTime) ACMD_AUTO_SETTING(IdleTimes, GAMESERVER);

// Any players at this access level or below will be considered for away kicking.
AUTO_CMD_INT(s_iAwayKickAccessLevel, AwayKickAccessLevel) ACMD_AUTO_SETTING(IdleTimes, GAMESERVER);

// Change the afk kick warning time for production edit mode server, in seconds.
AUTO_CMD_INT(s_iAwayKickWarnTime_ProdEditMode, AwayKickWarnTime_ProdEditMode) ACMD_AUTO_SETTING(IdleTimes, GAMESERVER);

// Change the afk kick time for production edit mode server, in seconds.
AUTO_CMD_INT(s_iAwayKickTime_ProdEditMode, AwayKickTime_ProdEditMode) ACMD_AUTO_SETTING(IdleTimes, GAMESERVER);


// Mark yourself as away from the keyboard.
AUTO_COMMAND ACMD_NAME(away) ACMD_ACCESSLEVEL(0);
void gslAway(CmdContext *pContext, Entity *pEnt, ACMD_SENTENCE pchMessage)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig)
	{
		Vec3 vPos;
		char *pchDefaultMessage;
		pConfig->status |= USERSTATUS_AFK;
		pConfig->status &= ~USERSTATUS_DND;
		if (pchMessage && *pchMessage)
		{
			// Set the permanent status if it's passed in
			ReplaceAnyWordProfane(pchMessage);
			gslSetCurrentActivity(pEnt, pchMessage);
		}
		else
		{			
			// Always set the temp status
			estrStackCreate(&pchDefaultMessage);
			entFormatGameMessageKey(pEnt, &pchDefaultMessage, "Away_DefaultAwayMessage", STRFMT_PLAYER(pEnt), STRFMT_END);
			gslSetCurrentActivity(pEnt, pchDefaultMessage);
			estrDestroy(&pchDefaultMessage);
		}

		devassert(pEnt->pPlayer);
		entFormatGameMessageKey(pEnt, pContext->output_msg, "Away_AwayFeedback", STRFMT_PLAYER(pEnt), STRFMT_STRING("Reason", pEnt->pPlayer->pchActivityString), STRFMT_END);
		entGetPos(pEnt, vPos);
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_PLAYER, pEnt, "PlayerAfk", "Location <%d;%d;%d> Auto 0", (int)vPos[0], (int)vPos[1], (int)vPos[2]);
		}
		RemoteCommand_UserChangeStatus(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), 
			USERSTATUSCHANGE_AFK, gslGetCurrentActivity(pEnt));
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

// Mark yourself as back at the keyboard.
AUTO_COMMAND ACMD_NAME(unaway, back) ACMD_ACCESSLEVEL(0);
void gslUnaway(CmdContext *pContext, Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig && (pConfig->status & (USERSTATUS_AFK | USERSTATUS_AUTOAFK | USERSTATUS_DND)) != 0)
	{		
		// Clear automatic status
		gslSetCurrentActivity(pEnt, NULL);
		if (pConfig->status & USERSTATUS_DND)
			entFormatGameMessageKey(pEnt, pContext->output_msg, "DND_ReturnFeedback", STRFMT_PLAYER(pEnt), STRFMT_END);
		else
			entFormatGameMessageKey(pEnt, pContext->output_msg, "Away_ReturnFeedback", STRFMT_PLAYER(pEnt), STRFMT_END);
		pConfig->status &= ~USERSTATUS_AFK;
		pConfig->status &= ~USERSTATUS_DND;
		devassert(pEnt->pPlayer);
		RemoteCommand_UserChangeStatus(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), 
			USERSTATUSCHANGE_BACK, NULL);
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_NAME(AutoAFK) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gslAutoAFK(CmdContext *pContext, Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig && (pConfig->status & USERSTATUS_AUTOAFK) == 0)
	{
		Vec3 vPos;
		devassert(pEnt->pPlayer);
		// This always shows auto-AFK message
		if (!(pConfig->status & USERSTATUS_AFK) && !(pConfig->status & USERSTATUS_DND))
		{
			gslSetCurrentActivity(pEnt, entTranslateMessageKey(pEnt, "Away_AutoAwayReason"));
			RemoteCommand_UserChangeStatus(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), 
				USERSTATUSCHANGE_AUTOAFK, gslGetCurrentActivity(pEnt));
		}
		pConfig->status |= USERSTATUS_AUTOAFK;
		entFormatGameMessageKey(pEnt, pContext->output_msg, "Away_AwayFeedback", STRFMT_PLAYER(pEnt), STRFMT_STRING("Reason",  entTranslateMessageKey(pEnt, "Away_AutoAwayReason")), STRFMT_END);
		entGetPos(pEnt, vPos);
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_PLAYER, pEnt, "PlayerAfk", "Location <%d;%d;%d> Auto 1", (int)vPos[0], (int)vPos[1], (int)vPos[2]);
		}
		
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_NAME(AutoAFKReturn) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gslClearAutoAFK(CmdContext *pContext, Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig && (pConfig->status & USERSTATUS_AUTOAFK) != 0)
	{
		devassert(pEnt->pPlayer);
		pConfig->status &= (~USERSTATUS_AUTOAFK);
		if (!(pConfig->status & USERSTATUS_AFK) && !(pConfig->status & USERSTATUS_DND))
		{
			gslSetCurrentActivity(pEnt, NULL);
			RemoteCommand_UserChangeStatus(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), 
				USERSTATUSCHANGE_REMOVE_AUTOAFK, gslGetCurrentActivity(pEnt));
		}
		entFormatGameMessageKey(pEnt, pContext->output_msg, "Away_AutoAwayReturn", STRFMT_PLAYER(pEnt), STRFMT_END);

		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);		
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(away);
void gslAwayToggle(CmdContext *pContext, Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig && (pConfig->status & (USERSTATUS_AFK | USERSTATUS_AUTOAFK)) != 0)
		gslUnaway(pContext, pEnt);
	else
		gslAway(pContext, pEnt, "");
}

// Mark yourself as away from the keyboard.
AUTO_COMMAND ACMD_NAME(afk) ACMD_ACCESSLEVEL(0);
void gslAFK(CmdContext *pContext, Entity *pEnt, ACMD_SENTENCE pchMessage)
{
	// Implemented like this so I can use the ERROR_FUNCTION_FOR below, since
	// that doesn't support multiple command names.
	gslAway(pContext, pEnt, pchMessage);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(afk);
void gslAFKToggle(CmdContext *pContext, Entity *pEnt)
{
	gslAwayToggle(pContext, pEnt);
}

// Similar to gslAutoAwayCheck(), but specifically to handle entity player containers that have been transferred to
//  the gameserver but are flagged as ENTITYFLAG_IGNORE.
// This is to enforce idle kick time when either the client never connected to the gameserver, or it is sitting at
//  the "press any key" prompt.
void gslIgnoredPlayerIdleCheck(Entity *pEnt)
{
    U32 uiTime = timeSecondsSince2000();
    int iAwayKickTimeToUse = isProductionEditMode() ? s_iAwayKickTime_ProdEditMode : s_iAwayKickTime;

    if (entGetAccessLevel(pEnt) <= s_iAwayKickAccessLevel && 
        ( pEnt->pPlayer->pUI->uiLastClientPoke == 0 || ( pEnt->pPlayer->pUI->uiLastClientPoke + iAwayKickTimeToUse < uiTime ) ) && 
        pEnt->pPlayer->uiContainerArrivalTime + iAwayKickTimeToUse < uiTime)
    {
		// Always logged.  Not disabled behind gbEnableGamePlayLogging flag
		entLog(LOG_PLAYER, pEnt, "AwayKick", "Logging out player container because client never connected, container arrival time was at %d and time is now %d",
			pEnt->pPlayer->uiContainerArrivalTime, uiTime);
        gslLogOutEntity(pEnt, 0, 0);
    }
}

// Auto-AFK does not override status messages set manually, and does not propagate to GCS?
void gslAutoAwayCheck(Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	U32 uiTime = timeSecondsSince2000();

	int iAwayKickTimeToUse = isProductionEditMode() ? s_iAwayKickTime_ProdEditMode : s_iAwayKickTime;
	int iAwayWarnTimeToUse = isProductionEditMode() ? s_iAwayKickWarnTime_ProdEditMode : s_iAwayKickWarnTime;

	PERFINFO_AUTO_START_FUNC();
	
	if (pConfig && pEnt->pPlayer->pUI)
	{
		if (pEnt->pPlayer->pUI->uiLastClientPoke + s_iAutoAwayIdleTime < uiTime && (pConfig->status & USERSTATUS_AUTOAFK) == 0)
		{
			GameServerParsePublic("AutoAFK", 0, pEnt, NULL, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
		}
		else if (pEnt->pPlayer->pUI->uiLastClientPoke + s_iAutoAwayIdleTime >= uiTime && (pConfig->status & USERSTATUS_AUTOAFK) != 0)
		{
			GameServerParsePublic("AutoAFKReturn", 0, pEnt, NULL, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
		}

		if (entGetAccessLevel(pEnt) <= s_iAwayKickAccessLevel && pEnt->pPlayer->pUI->uiLastClientPoke + iAwayKickTimeToUse < uiTime)
		{
			char *pch = NULL;
			entFormatGameMessageKey(pEnt, &pch, "Away_Kicked",
				STRFMT_TIMER("WarnTimer", iAwayWarnTimeToUse),
				STRFMT_TIMER("KickTimer", iAwayKickTimeToUse),
				STRFMT_END);
			if (entGetClientLink(pEnt))
				gslSendForceLogout(entGetClientLink(pEnt), pch);
			// Always logged. Not disabled behind gbEnableGamePlayDataLogging flag.
			entLog(LOG_PLAYER, pEnt, "AwayKick", "Sending an away KICK, last poke was at %d and time is now %d",
				pEnt->pPlayer->pUI->uiLastClientPoke, uiTime);
			gslLogOutEntity(pEnt, 0, 0);
			estrDestroy(&pch);
		}
		else if (entGetAccessLevel(pEnt) <= s_iAwayKickAccessLevel && pEnt->pPlayer->pUI->uiLastClientPoke + iAwayWarnTimeToUse < uiTime && !pEnt->pPlayer->pUI->bIdleKickWarned)
		{
			char *pch = NULL;
			pEnt->pPlayer->pUI->bIdleKickWarned = true;
			entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			entFormatGameMessageKey(pEnt, &pch, "Away_KickWarning",
				STRFMT_TIMER("WarnTimer", iAwayWarnTimeToUse),
				STRFMT_TIMER("KickTimer", iAwayKickTimeToUse),
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_AwayKickWarning, pch, NULL, NULL);
			if (gbEnableGamePlayDataLogging) {
				entLog(LOG_PLAYER, pEnt, "AwayKick", "Sending an away warning, last poke was at %d and time is now %d",
					pEnt->pPlayer->pUI->uiLastClientPoke, uiTime);
			}
			estrDestroy(&pch);
		}
	}

	PERFINFO_AUTO_STOP();
}

// Poke the server so it doesn't mark this entity as AFK.
// Not ACMD_PRIVATE because then the gslUnaway return string doesn't
// get sent back down to the client.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void gslAwayPoke(CmdContext *pContext, Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
		pEnt->pPlayer->pUI->uiLastClientPoke = timeSecondsSince2000();
		pEnt->pPlayer->pUI->bIdleKickWarned = false;
		if (pConfig && (pConfig->status & USERSTATUS_AUTOAFK) != 0)
		{
			GameServerParsePublic("AutoAFKReturn", 0, pEnt, NULL, -1, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
		}
	}
}

// Mark yourself as "Do Not Disturb".
AUTO_COMMAND ACMD_NAME(dnd) ACMD_ACCESSLEVEL(0);
void gslDND(CmdContext *pContext, Entity *pEnt, ACMD_SENTENCE pchMessage)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig)
	{
		Vec3 vPos;
		char *pchDefaultMessage;
		pConfig->status |= USERSTATUS_DND;
		pConfig->status &= ~USERSTATUS_AFK;
		if (pchMessage && *pchMessage)
		{
			// Set the permanent status if it's passed in
			ReplaceAnyWordProfane(pchMessage);
			gslSetCurrentActivity(pEnt, pchMessage);
		}
		else
		{		
			estrStackCreate(&pchDefaultMessage);
			entFormatGameMessageKey(pEnt, &pchDefaultMessage, "DND_DefaultMessage", STRFMT_PLAYER(pEnt), STRFMT_END);
			gslSetCurrentActivity(pEnt, pchDefaultMessage);
			estrDestroy(&pchDefaultMessage);
		}

		devassert(pEnt->pPlayer);
		entFormatGameMessageKey(pEnt, pContext->output_msg, "DND_Feedback", STRFMT_PLAYER(pEnt), STRFMT_STRING("Reason", pEnt->pPlayer->pchActivityString), STRFMT_END);
		entGetPos(pEnt, vPos);
		if (gbEnableGamePlayDataLogging) {
			entLog(LOG_PLAYER, pEnt, "PlayerDND", "Location <%d;%d;%d> Auto 0", (int)vPos[0], (int)vPos[1], (int)vPos[2]);
		}
		RemoteCommand_UserChangeStatus(GLOBALTYPE_CHATSERVER, 0, entGetAccountID(pEnt), 
			USERSTATUSCHANGE_DND, gslGetCurrentActivity(pEnt));
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dnd);
void gslDNDToggle(CmdContext *pContext, Entity *pEnt)
{
	NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);
	if (pConfig && (pConfig->status & USERSTATUS_DND) != 0)
		gslUnaway(pContext, pEnt);
	else
		gslDND(pContext, pEnt, "");
}
