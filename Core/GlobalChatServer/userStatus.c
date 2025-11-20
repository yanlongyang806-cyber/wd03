#include "chatdb.h"
#include "chatCommon.h"
#include "chatGlobal.h"
#include "net.h"
#include "StringUtil.h"
#include "TextFilter.h"
#include "xmpp\XMPP_Chat.h"
#include "ChatServer\chatShared.h"

#include "AutoGen\chatdb_h_ast.h"

void userSendActivityStatus(ChatUser *user, NetLink *originator)
{
	U32 uChatServerID = GetLocalChatLinkID(chatServerGetCommandLink());
	PlayerInfoStruct *pStruct = findPlayerInfoByLocalChatServerID(user, uChatServerID);
	PlayerStatusChange statusChange = {0};
	char *cmdString = NULL;
	char *statusString = NULL;

	statusChange.uAccountID = user->id;
	statusChange.eStatus = user->online_status;
	statusChange.eaiAccountIDs = CONTAINER_NOCONST(ChatUser, user)->friends;
	if (pStruct)
	{
		statusChange.uChatServerID = uChatServerID;
		statusChange.pActivity = pStruct->playerActivity;
	}

	estrStackCreate(&cmdString);
	estrStackCreate(&statusString);
	ParserWriteTextEscaped(&statusString, parse_PlayerStatusChange, &statusChange, 0, 0, 0);
	estrPrintf(&cmdString, "ChatPlayerInfo_SendActivityStringUpdate %s", statusString);
	sendCommandToAllLocalMinusSender(originator, cmdString);
	estrDestroy(&cmdString);
	estrDestroy(&statusString);

	XMPPChat_RecvPresence(user, NULL);
}

AUTO_COMMAND_REMOTE ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Chat) ACMD_IFDEF(GAMESERVER);
int UserChangeStatus(ContainerID userID, UserStatusChange eStatusChange, const char *msg)
{
	ChatUser *user = userFindByContainerId(userID);
	bool bChanged = false;
	if (!user)
		return CHATRETURN_FWD_NONE;
	switch (eStatusChange)
	{
	case USERSTATUSCHANGE_AFK:
		bChanged = userChangeStatusAndActivity(user, USERSTATUS_AFK, USERSTATUS_DND, msg, true);
	xcase USERSTATUSCHANGE_DND:
		bChanged = userChangeStatusAndActivity(user, USERSTATUS_DND, USERSTATUS_AFK, msg, true);
	xcase USERSTATUSCHANGE_BACK:
		bChanged = userChangeStatusAndActivity(user, 0, USERSTATUS_DND | USERSTATUS_AFK, msg, true);
	}
	if (bChanged)
		userSendActivityStatus(user, chatServerGetCommandLink());
	return CHATRETURN_FWD_NONE;
}