#include "gslChat.h"
#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "EntityLib.h"
#include "gslTransactions.h"
#include "LoginCommon.h"
#include "NotifyEnum.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "gslSendToClient.h"
#include "GameServerLib.h"
#include "gslEntity.h"

#include "Autogen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "../Common/AutoGen/ObjectDB_autogen_RemoteFuncs.h"

extern ParseTable parse_AccountFlagUpdate[];
#define TYPE_parse_AccountFlagUpdate AccountFlagUpdate

typedef struct CSRSilenceStruct
{
	EntityRef userRef;

	char *characterName;
	U32 uDuration;
} CSRSilenceStruct;

// Blocks incoming private messages
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(deaf);
void ServerAdmin_CSRDeaf(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_csrdeaf(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
	}
}

// Stops blocking incoming private messages
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(undeaf);
void ServerAdmin_CSRUndeaf(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_csrundeaf(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
	}
}

void ServerAdmin_SilenceTransactionReturn(TransactionReturnVal *returnVal, CSRSilenceStruct *data)
{
	ContainerID returnID = 0;
	Entity *pEnt = entFromEntityRefAnyPartition(data->userRef);
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &returnID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && returnID && pEnt)
	{
		Entity *pOtherEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, returnID);
		if (pOtherEnt)
			RemoteCommand_csrSilence(NULL, GLOBALTYPE_CHATSERVER, 0, 
				pOtherEnt->pPlayer->accountID, data->uDuration);
	}
	else
	{
		char *error = NULL;
		entFormatGameMessageKey(pEnt, &error, "ChatServer_Local_UserDNE", STRFMT_STRING("User", data->characterName), STRFMT_END);
		if (!error) {
			estrCopy2(&error, "User does not exist or there are multiple users with the character name");
		}
	}
	if (data->characterName)
		free(data->characterName);
	free(data);
}
// Prevent the player from talking in chat
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(silenceuser);
void ServerAdmin_CSRSilenceByDisplayName(Entity *pEnt, ACMD_SENTENCE displayName)
{
	char *actualDisplayName = displayName && *displayName == '@' ? displayName+1 : displayName;
	RemoteCommand_userSilenceByDisplayName(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, 
		actualDisplayName, gProjectGameServerConfig.iSilenceTime * 60);
}
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(unsilenceuser);
void ServerAdmin_CSRUnsilenceByDisplayName(Entity *pEnt, ACMD_SENTENCE displayName)
{
	char *actualDisplayName = displayName && *displayName == '@' ? displayName+1 : displayName;
	RemoteCommand_userUnsilenceByDisplayName(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, actualDisplayName);
}


// Prevent the player from talking in chat
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(silence);
void ServerAdmin_CSRSilence(Entity *pEnt)
{

	RemoteCommand_csrSilence(NULL, GLOBALTYPE_CHATSERVER, 0, 
		pEnt->pPlayer->accountID, gProjectGameServerConfig.iSilenceTime * 60);
}

void ServerAdmin_UnsilenceTransactionReturn(TransactionReturnVal *returnVal, CSRSilenceStruct *data)
{
	ContainerID returnID = 0;
	Entity *pEnt = entFromEntityRefAnyPartition(data->userRef);
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &returnID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && pEnt && returnID)
	{
		Entity *pOtherEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, returnID);
		if (pOtherEnt)
			RemoteCommand_csrUnsilence(NULL, GLOBALTYPE_CHATSERVER, 0, pOtherEnt->pPlayer->accountID);
	}
	else
	{
		char *error = NULL;
		entFormatGameMessageKey(pEnt, &error, "ChatServer_Local_UserDNE", STRFMT_STRING("User", data->characterName), STRFMT_END);
		if (!error) {
			estrCopy2(&error, "User does not exist or there are multiple users with the character name");
		}
	}
	if (data->characterName)
		free(data->characterName);
	free(data);
}

// Allow the player to talk in chat again
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(unsilence);
void ServerAdmin_CSRUnsilence(Entity *pEnt)
{
	RemoteCommand_csrUnsilence(NULL, GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
}

// Send a shard-wide announcement
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(announce);
void ServerAdmin_CSRAnnounce(Entity *pEnt, ACMD_SENTENCE msg)
{
	// TODO
	if (pEnt && pEnt->pPlayer && msg && *msg)
	{
		RemoteCommand_csrAnnounce(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID, msg);
	}
}

// Send a zone-wide broadcast
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(broadcast);
void ServerAdmin_CSRBroadcast(Entity *pEnt, ACMD_SENTENCE msg)
{
	// TODO figure out how to route this through ChatServer and back down?
	ServerChat_BroadcastMessage(msg, kNotifyType_ServerAnnounce);
}

void ServerAdmin_SpyTransactionReturn(TransactionReturnVal *returnVal, int *accountID)
{
	ContainerID returnID = 0;
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &returnID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && returnID)
	{
		Entity *pOtherEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, returnID);
		if (pOtherEnt)
			RemoteCommand_csrSpyByID(GLOBALTYPE_CHATSERVER, 0, *accountID, pOtherEnt->pPlayer->accountID);
	}
	else
	{
		// TODO
	}
	free(accountID);
}
void ServerAdmin_SpyStopTransactionReturn(TransactionReturnVal *returnVal, int *accountID)
{
	ContainerID returnID = 0;
	enumTransactionOutcome eOutcome = gslGetPlayerIDFromNameWithRestoreReturn(returnVal, &returnID);

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && returnID)
	{
		Entity *pOtherEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, returnID);
		if (pOtherEnt)
			RemoteCommand_csrStopSpyingByID(GLOBALTYPE_CHATSERVER, 0, *accountID, pOtherEnt->pPlayer->accountID);
	}
	else
	{
		// TODO
	}
	free(accountID);
}

// Spy on another player's chat
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(spy);
void ServerAdmin_CSRSpy(Entity *pEnt, char *handleOrCharacterName)
{
	if (pEnt && pEnt->pPlayer && handleOrCharacterName && *handleOrCharacterName)
	{
		if (*handleOrCharacterName == '@')
		{
			// Send by handle
			RemoteCommand_csrSpyByHandle(GLOBALTYPE_CHATSERVER, 0, 
				pEnt->pPlayer->accountID, handleOrCharacterName+1); // remove the '@' from the sending
		}
		else
		{
			int *accountID = malloc(sizeof(int));
			*accountID = pEnt->pPlayer->accountID;
			gslGetPlayerIDFromNameWithRestore(handleOrCharacterName, entGetVirtualShardID(pEnt), ServerAdmin_SpyTransactionReturn, accountID);
		}
	}
}

// Stop spying on another player's chat
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(spystop);
void ServerAdmin_CSRSpyStop(Entity *pEnt, char *handleOrCharacterName)
{
	if (pEnt && pEnt->pPlayer && handleOrCharacterName && *handleOrCharacterName)
	{
		if (*handleOrCharacterName == '@')
		{
			// Send by handle
			RemoteCommand_csrStopSpyingByHandle(GLOBALTYPE_CHATSERVER, 0, 
				pEnt->pPlayer->accountID, handleOrCharacterName+1); // remove the '@' from the sending
		}
		else
		{
			int *accountID = malloc(sizeof(int));
			*accountID = pEnt->pPlayer->accountID;
			gslGetPlayerIDFromNameWithRestore(handleOrCharacterName, entGetVirtualShardID(pEnt), ServerAdmin_SpyStopTransactionReturn, accountID);
		}
	}
}

// List all the people you're spying on
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(ChatAdmin, CSR) ACMD_NAME(spylist);
void ServerAdmin_CSRSpyList(Entity *pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		RemoteCommand_csrSpyList(GLOBALTYPE_CHATSERVER, 0, pEnt->pPlayer->accountID);
	}
}

// Boots a player from the game
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(CSR) ACMD_NAME(BootPlayer);
void ServerAdmin_CSRBootPlayer(Entity *pEnt) {
	ClientLink *link = entGetClientLink(pEnt);
	if(link) {
		gslSendForceLogout(link, "Booted from server");
	}
}

// Make yourself invisible to other players
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(SetInvisible);
void ServerAdmin_CSRSetInvisible(Entity* e, S32 on)
{
	gslEntitySetInvisibleTransient(e, on);
	
	if(!on){
		gslEntitySetInvisiblePersistent(e, 0);
	}
}

// Make yourself invisible to other players
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_CATEGORY(CSR) ACMD_NAME(SetInvisiblePersist);
void ServerAdmin_CSRSetInvisiblePersist(Entity* e, S32 on)
{
	gslEntitySetInvisibleTransient(e, on);
	gslEntitySetInvisiblePersistent(e, on);
}
