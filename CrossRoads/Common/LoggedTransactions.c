/***************************************************************************
 *     Copyright (c) 2008, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "MemoryPool.h"
#include "estring.h"
#include "logging.h"

#include "GlobalTypes.h"
#include "objSchema.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "EntityLib.h"

typedef struct LoggedCallbackData
{
	const char *pchPrefix;
	TransactionReturnCallback func;
	void *pvUserData;

	GlobalType type;
	ContainerID id;

    const char *debugName;
    const char *gameSpecificLogInfo;
} LoggedCallbackData;

MP_DEFINE(LoggedCallbackData);

CmdList gLoggedCallbackCmdList = {1,0,0}; // Internal list

AUTO_COMMAND ACMD_NAME(Log) ACMD_LIST(gLoggedCallbackCmdList);
void WriteLog(ACMD_SENTENCE message, CmdContext *context)
{	
	char *logString = NULL;
	Entity *pent = NULL;	
	LoggedCallbackData *plog = context->data;

	estrStackCreate(&logString);
	estrAppendUnescaped(&logString, message);
	pent = entFromContainerIDAnyPartition(plog->type, plog->id);
	
	if(pent)
	{
		entLog(LOG_LOGGED_TRANSACTIONS, pent, plog->pchPrefix, "%s", logString);
	}
    else if ( plog->debugName )
    {
        objLog(LOG_LOGGED_TRANSACTIONS, plog->type, plog->id, 0, plog->debugName, NULL, NULL, plog->pchPrefix, plog->gameSpecificLogInfo, "%s", logString);
    }
	else
	{
		objLog(LOG_LOGGED_TRANSACTIONS, plog->type, plog->id, 0, "", NULL, NULL, plog->pchPrefix, NULL, "%s", logString);
	}

	estrDestroy(&logString);

}

AUTO_COMMAND ACMD_NAME(LogToCategory) ACMD_LIST(gLoggedCallbackCmdList);
void WriteLogToCategory(int eCategory, ACMD_SENTENCE message, CmdContext *context)
{	
	char *logString = NULL;
	Entity *pent = NULL;	
	LoggedCallbackData *plog = context->data;

	estrStackCreate(&logString);
	estrAppendUnescaped(&logString, message);
	pent = entFromContainerIDAnyPartition(plog->type, plog->id);
	
	if(pent)
	{
		entLog(eCategory, pent, plog->pchPrefix, "%s", logString);
	}
    else if ( plog->debugName )
    {
        objLog(eCategory, plog->type, plog->id, 0, plog->debugName, NULL, NULL, plog->pchPrefix, plog->gameSpecificLogInfo, "%s", logString);
    }
	else
	{
		objLog(eCategory, plog->type, plog->id, 0, "", NULL, NULL, plog->pchPrefix, NULL, "%s", logString);
	}

	estrDestroy(&logString);

}

AUTO_COMMAND ACMD_NAME(LogToCategoryWithName) ACMD_LIST(gLoggedCallbackCmdList);
void WriteLogToCategoryWithName(int eCategory, const char *actionname, ACMD_SENTENCE message, CmdContext *context)
{	
	char *logString = NULL;
	Entity *pent = NULL;	
	LoggedCallbackData *plog = context->data;

	estrStackCreate(&logString);
	estrAppendUnescaped(&logString, message);
	pent = entFromContainerIDAnyPartition(plog->type, plog->id);
	
	if(pent)
	{
		entLog(eCategory, pent, actionname ? actionname : plog->pchPrefix, "%s", logString);
	}
	else if ( plog->debugName )
    {
        objLog(eCategory, plog->type, plog->id, 0, plog->debugName, NULL, NULL, actionname ? actionname : plog->pchPrefix, plog->gameSpecificLogInfo, "%s", logString);
    }
    else
	{
		objLog(eCategory, plog->type, plog->id, 0, "", NULL, NULL, actionname ? actionname : plog->pchPrefix, NULL, "%s", logString);
	}

	estrDestroy(&logString);

}

AUTO_COMMAND ACMD_NAME(RemoteCommand) ACMD_LIST(gLoggedCallbackCmdList);
void RunRemoteCommand(GlobalType type, ContainerID id, ACMD_SENTENCE message, CmdContext *context)
{	
	Entity *pent = NULL;	
	LoggedCallbackData *plog = context->data;

	if (type == 0 && id == 0)
	{
		type = plog->type;
		id = plog->id;
	}

	// Logic copied from automatic remote commands
	objRequestTransactionSimplef(NULL, type, id, "RunRemoteCommand", "remotecommand %s", message);
}



// Not recursive/thread safe
bool RunLoggedCallbackCommand(const char *cmd, LoggedCallbackData *plog)
{
	int result;
	char *output = NULL;
	static CmdContext context = {0};
	InitCmdOutput(context, output);

	context.access_level = 9;
	context.data = plog;
	
	result = cmdParseAndExecute(&gLoggedCallbackCmdList,cmd,&context);

	CleanupCmdOutput(context);

	return result;
}


static void LoggedTransactionCB(TransactionReturnVal* retval, LoggedCallbackData *plog)
{
	if(plog)
	{
		if(retval)
		{
			int i= 0;
			char *pch = objAutoTransactionGetResult(retval);
			char *end = pch;		

			for (i = 0; i < retval->iNumBaseTransactions; i++)
			{
				if (retval->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					log_printf(LOG_CONTAINER, "Transaction with prefix %s on %s %d failed on step %d with string '%s'", plog->pchPrefix, GlobalTypeToName(plog->type), plog->id, i, retval->pBaseReturnVals[i].returnString);
				}
			}
			while(pch)
			{
				end = strchr(pch, '\n');
				if(end)
				{
					*end++ = '\0';
				}

				if(*pch)
				{
					RunLoggedCallbackCommand(pch, plog);
				}

				pch = end;
			}
		}

		if(plog->func)
		{
			plog->func(retval, plog->pvUserData);
		}

        if ( plog->debugName )
        {
            free((char *)plog->debugName);
        }
        if ( plog->gameSpecificLogInfo )
        {
            free((char *)plog->gameSpecificLogInfo);
        }
		MP_FREE(LoggedCallbackData, plog);
	}
}

TransactionReturnVal* LoggedTransactions_CreateManagedReturnValEx(const char *pchPrefix, Entity *pent, GlobalType type, ContainerID id, const char *debugName, const char *gameSpecificLogInfo, TransactionReturnCallback func, void *pvUserData)
{
	TransactionReturnVal *retval;
	LoggedCallbackData *plog;

	MP_CREATE(LoggedCallbackData, 4);

	plog = MP_ALLOC(LoggedCallbackData);
	plog->pchPrefix = pchPrefix;
	plog->func = func;
	plog->pvUserData = pvUserData;

	if(pent)
	{
		plog->type = entGetType(pent);
		plog->id = entGetContainerID(pent);
	}
	else
	{
		plog->type = type;
		plog->id = id;
	}

	retval = objCreateManagedReturnVal(LoggedTransactionCB, plog);

	if(!retval)
	{
		MP_FREE(LoggedCallbackData, plog);
	}
	else
	{
		retval->eFlags |= TRANSACTIONRETURN_FLAG_LOGGED_RETURN;
	}

    if ( debugName )
    {
        plog->debugName = strdup(debugName);
    }

    if ( gameSpecificLogInfo )
    {
        plog->gameSpecificLogInfo = strdup(gameSpecificLogInfo);
    }

	return retval;
}

TransactionReturnVal* LoggedTransactions_CreateManagedReturnVal(const char *pchPrefix, TransactionReturnCallback func, void *pvUserData)
{
	return LoggedTransactions_CreateManagedReturnValEx(pchPrefix, NULL, GLOBALTYPE_NONE, 0, NULL, NULL, func, pvUserData);
}

TransactionReturnVal* LoggedTransactions_CreateManagedReturnValEnt(const char *pchPrefix, Entity *pent, TransactionReturnCallback func, void *pvUserData)
{
	return LoggedTransactions_CreateManagedReturnValEx(pchPrefix, pent, GLOBALTYPE_ENTITY, 0, NULL, NULL, func, pvUserData);
}

TransactionReturnVal* LoggedTransactions_CreateManagedReturnValObj(const char *pchPrefix, GlobalType type, ContainerID id, TransactionReturnCallback func, void *pvUserData)
{
	return LoggedTransactions_CreateManagedReturnValEx(pchPrefix, NULL, type, id, NULL, NULL, func, pvUserData);
}

TransactionReturnVal* LoggedTransactions_CreateManagedReturnValEntInfo(const char *pchPrefix, GlobalType entType, ContainerID entID, const char *debugName, const char *gameSpecificLogInfo, TransactionReturnCallback func, void *pvUserData)
{
    return LoggedTransactions_CreateManagedReturnValEx(pchPrefix, NULL, entType, entID, debugName, gameSpecificLogInfo, func, pvUserData); 
}

/* End of File */
