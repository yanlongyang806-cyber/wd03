/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "estring.h"
#include "gslShardVariable.h"
#include "gslWorldVariable.h"
#include "timing.h"
#include "WorldGrid.h"
#include "WorldVariable.h"
#include "ShardVariableCommon.h"


// ----------------------------------------------------------------------------------
// Debug Commands
// ----------------------------------------------------------------------------------

// Gets a shard variable
AUTO_COMMAND ACMD_NAME(GetShardVariable) ACMD_ACCESSLEVEL(9);
char *shardvariable_CmdGetVariable(const char *pcName)
{
	static char *estrBuf = NULL;
	ShardVariable *pShardVar;

	estrClear(&estrBuf);
	pShardVar = shardvariable_GetByName(pcName);
	if (pShardVar) {
		worldVariableToEString(pShardVar->pVariable, &estrBuf);
	} else {
		shardvariable_ErrorOnNotFound(pcName, &estrBuf);
	}
	return estrBuf;
}


// Sets a shard variable
AUTO_COMMAND ACMD_NAME(SetShardVariable) ACMD_ACCESSLEVEL(9);
char *shardvariable_CmdSetVariable(const char *pcName, const char *pcValue)
{
	static char *estrBuf = NULL;
	ShardVariable *pShardVar;

	estrClear(&estrBuf);
	pShardVar = shardvariable_GetByName(pcName);
	if (pShardVar) {
		WorldVariable *pWorldVar = StructClone(parse_WorldVariable, pShardVar->pVariable);
		assert(pWorldVar);
		worldVariableFromString(pWorldVar, pcValue, &estrBuf);
		shardvariable_SetVariable(pWorldVar, &estrBuf);
		StructDestroy(parse_WorldVariable, pWorldVar);
		if (!estrBuf) {
			estrPrintf(&estrBuf, "Variable '%s' set to '%s'", pcName, pcValue);
		}
	} else {
		shardvariable_ErrorOnNotFound(pcName, &estrBuf);
	}
	return estrBuf;
}

// Sets a shard variable to the current time
AUTO_COMMAND ACMD_NAME(SetShardVariableToSS2000) ACMD_ACCESSLEVEL(9);
char* shardvariable_CmdSetVariableToSS2000(const char* pcName)
{
	static char *estr = NULL;

	estrPrintf(&estr, "%d", timeSecondsSince2000());
	return shardvariable_CmdSetVariable(pcName, estr);
}

// Increment a shard variable
AUTO_COMMAND ACMD_NAME(IncrementShardVariableInt) ACMD_ACCESSLEVEL(9);
char *shardvariable_CmdIncrementIntVariable(const char *pcName, int iValue)
{
	static char *estrBuf = NULL;
	estrClear(&estrBuf);
	shardvariable_IncrementIntVariable(pcName, iValue, &estrBuf);
	if (!estrBuf) {
		estrPrintf(&estrBuf, "Variable '%s' incremented by '%d'", pcName, iValue);
	}
	return estrBuf;
}


// Increment a shard variable
AUTO_COMMAND ACMD_NAME(IncrementShardVariableFloat) ACMD_ACCESSLEVEL(9);
char *shardvariable_CmdIncrementFloatVariable(const char *pcName, F32 fValue)
{
	static char *estrBuf = NULL;
	estrClear(&estrBuf);
	shardvariable_IncrementFloatVariable(pcName, fValue, &estrBuf);
	if (!estrBuf) {
		estrPrintf(&estrBuf, "Variable '%s' incremented by '%g'", pcName, fValue);
	}
	return estrBuf;
}


// Resets a shard variable
AUTO_COMMAND ACMD_NAME(ResetShardVariable) ACMD_ACCESSLEVEL(9);
char *shardvariable_CmdResetVariable(const char *pcName)
{
	static char *estrBuf = NULL;
	estrClear(&estrBuf);
	shardvariable_ResetVariable(pcName, &estrBuf);
	if (!estrBuf) {
		estrPrintf(&estrBuf, "Variable '%s' reset", pcName);
	}
	return estrBuf;
}


// Resets all shard variables. Resets even if the map does not subscribe to MapRequested vars
AUTO_COMMAND ACMD_NAME(ResetAllShardVariables) ACMD_ACCESSLEVEL(9);
char *shardvariable_CmdResetAllVariables(void)
{
	static char *estrBuf = NULL;
	estrClear(&estrBuf);
	shardvariable_ResetAllVariables(&estrBuf);
	if (!estrBuf) {
		estrPrintf(&estrBuf, "All shard variables were reset");
	}
	return estrBuf;
}

AUTO_COMMAND_REMOTE;
void ResetShardVariable(char *pName)
{
	shardvariable_CmdResetVariable(pName);
}

AUTO_COMMAND_REMOTE;
void SetShardVariable(char *pName, char *pNewValue)
{
	shardvariable_CmdSetVariable(pName, pNewValue);
}


