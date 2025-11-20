#pragma once

#include "../../Core/Controller/Pub/ControllerPub.h"

typedef struct MissedCommand MissedCommand;
typedef struct ShardVariableContainer ShardVariableContainer;

AUTO_ENUM;
typedef enum ShardConnectionState
{
	SHARD_NEVER_CONNECTED,
	SHARD_CONNECTED,
	SHARD_DISCONNECTED,
} ShardConnectionState;



AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "State");
typedef struct Shard
{
	const char *pShardName; AST(KEY POOL_STRING)
	char *pMonitor; AST(FORMATSTRING(HTML=1))
	char *pVNC; AST(FORMATSTRING(HTML=1))
	char *pRestartBatchFile;  AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	char *pMachineName;
	ShardConnectionState eState; AST(FORMATSTRING(HTML_CLASS_IFEXPR = "\\q$\\q = \\qCONNECTED\\q ; divGreen ; \\q$\\q = \\qDISCONNECTED\\q ; divRed ; \\q$\\q = \\qNEVER_CONNECTED\\q ; divPurple"))
	U32 iLastConnectionTime; AST(FORMATSTRING(HTML_SECS_AGO=1))

	int iNextMissedCommandID; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	MissedCommand **ppMissedCommands; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))


	ControllerSummaryForClusterController summary; AST(EMBEDDED_FLAT) 
	NetLink *pLink; NO_AST
	char *pInternal; AST(FORMATSTRING(HTML=1))

	ControllerAutoSetting_Category **ppAutoSettingCategories; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))
	ShardVariableForClusterController_List *pShardVariableList; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))

	AST_COMMAND("Kill this shard", "KillShard $FIELD(ShardName) $INT(1 to use SentryServer) $STRING(Type this shard's name to kill it)")
	AST_COMMAND("Run this shard", "RunShard $FIELD(ShardName) $STRING(Type yes to run this shard)")
} Shard;

#define UNKNOWN_SHARD ((void*)(0x1))