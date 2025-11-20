#pragma once
#include "cmdparse.h"
#include "ClusterController.h"

AUTO_ENUM;
typedef enum CommandResponseStatus
{
	COMMANDRESPONSE_NOT_RESPONDED,
	COMMANDRESPONSE_DISCONNECTED,
	COMMANDRESPONSE_RESPONDED,
	COMMANDRESPONSE_TIMED_OUT,
	COMMANDRESPONSE_FAILED,
	COMMANDRESPONSE_FILE_TRANSFER_FAILED,
} CommandResponseStatus;

typedef struct CommandResponse CommandResponse;

AUTO_STRUCT;
typedef struct PerShardCommandResponse
{
	CommandResponse *pParent; NO_AST
	U32 iUniqueID; //unique across all responses, used as a key for callbacks and such
	const char *pShardName; AST(POOL_STRING, KEY)
	CommandResponseStatus eStatus;
	char *pResponseString;
	float fExtraTime; //if the command might be timing out, but there are file transfers going on, keep giving it extra time
	bool bWaitForFileTransfers;
} PerShardCommandResponse;

AUTO_STRUCT;
typedef struct CommandResponse
{
	int iID; AST(KEY)
	char *pCommand;
	char *pFiles;
	U32 iTimeIssued; AST(FORMATSTRING(HTML_SECS_AGO=1))
	PerShardCommandResponse **ppPerShardResponses;
	char *pResponseSummary; AST(ESTRING)
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo; NO_AST
	int iTimeOut;
} CommandResponse;

//a command that was sent to a shard, and it either was disconnected or timed out, 
//so presumably might be re-sent when reconnection occurs
AUTO_STRUCT;
typedef struct MissedCommand
{
	int iID; AST(KEY)
	const char *pShardName; AST(POOL_STRING)
	U32 iSendTime; AST(FORMATSTRING(HTML_SECS_AGO=1))
	CommandResponseStatus eStatus;
	char *pCommandString;
	char *pFiles; AST(ESTRING, FORMATSTRING(HTML_CLASS_IFEXPR = "$ ; divWarning2"))
	AST_COMMAND("Clear", "ClearMissedCommand $FIELD(ShardName) $FIELD(ID)")
	AST_COMMAND("Retry", "RetryMissedCommand $FIELD(ShardName) $FIELD(ID)")
} MissedCommand;


bool CheckForCommandCompletion(CommandResponse *pCommand);
void ClusterControllerCommands_OncePerSecond(float timeSinceLastCallback);
void ClusterControllerCommands_ShardDisconnected(Shard *pShard);

void HandleCommandReturn(Packet *pPak, Shard *pShard);

extern StashTable gShardsByName;

void ClusterControllerCommands_ShardNewlyConnected(Shard *pShard);