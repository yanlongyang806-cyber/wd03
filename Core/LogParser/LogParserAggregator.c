#include "cmdparse.h"
#include "StashTable.h"
#include "LogParser.h"
#include "LogParserAggregator.h"
#include "LogParserAggregator_c_ast.h"
#include "LogParserAggregator_h_ast.h"
#include "LogParsing.h"
#include "LogParsing_h_ast.h"
#include "timing.h"

// Aggregated data for a LPTimedAggregator
AUTO_STRUCT;
typedef struct LPTimedAggregatorInstance
{
	size_t log_bytes;						// Total number of bytes in all log lines; user can't change the raw log line length!
	LPTimedAggregatorData data;				// Container for logs
} LPTimedAggregatorInstance;

// Active TimedAggregators
static StashTable stTimedAggregators = NULL;


//
AUTO_FIXUPFUNC;
TextParserResult FixupLPTimedAggregatorData(LPTimedAggregatorData *data, enumTextParserFixupType type, void *pExtraData)
{
	if (type == FIXUPTYPE_DESTRUCTOR)
	{
		if (data->userdata)
		{
			if (data->destructor)
				data->destructor(data);
			if (data->usertable)
				StructDestroyVoid(data->usertable, data->userdata);
		}
			
	}
	return PARSERESULT_SUCCESS;
}

// Return false if adding another log line would push us over our safety caps.
static bool LPTimedAggregator_IsAddingLogSafe(LPTimedAggregator *pTimedAggregator, LPTimedAggregatorInstance *instance, size_t size)
{
	return instance->log_bytes + size <= pTimedAggregator->iMaxLogBytes
		&& (size_t)eaSize(&instance->data.logs) + 1 <= pTimedAggregator->iMaxLogs;
}

// Get the earliest relevant time.
static U32 LPTimedAggregator_GetLogFloor(LPTimedAggregator *pTimedAggregator, U32 latest_time)
{
	U32 current_period = LogParserRoundTime(latest_time, pTimedAggregator->iLength, pTimedAggregator->iOffset);
	U32 floor_period = current_period - pTimedAggregator->iPeriods*pTimedAggregator->iLength;
	return floor_period;
}

// Return false if this log is too early to be useful to us.
// In most cases, logs are supposed to be sorted and processed in order, but various conditions
// might make this not always the case.  For instance, a major timer desynch, a long stall, or a bug might cause
// a log to arrive apparently too late.
static bool LPTimedAggregator_IsLogTimeRelevant(U32 floor_time, const ParsedLog *pLog)
{
	return pLog->iTime >= floor_time;
}

// Prune logs which no longer belong.
static void LPTimedAggregator_PruneLogs(LPTimedAggregator *pTimedAggregator, LPTimedAggregatorInstance *instance, U32 floor_time)
{
	int i;
	for (i = 0; i < eaSize(&instance->data.logs) && !LPTimedAggregator_IsLogTimeRelevant(floor_time, instance->data.logs[i]); ++i)
	{
		instance->log_bytes -= estrLength(&instance->data.logs[i]->pRawLogLine);
		StructDestroy(parse_ParsedLog, instance->data.logs[i]);
	}
	eaRemoveRange(&instance->data.logs, 0, i);
}

// Compare pointers to ParsedLogs based on iTime.
static int ParsedLogTimeLess(const void *lhs_ptr, const void *rhs_ptr)
{
	const ParsedLog *const *lhs = lhs_ptr, *const *rhs = rhs_ptr;
	return (*lhs)->iTime - (*rhs)->iTime;
}

// Add log to aggregator instance.  Return true if the log was actually added.
static bool LPTimedAggregator_AddLog(LPTimedAggregator *pTimedAggregator, LPTimedAggregatorInstance *instance, const ParsedLog *pLog)
{
	U32 latest_time;
	U32 floor_time;
	ParsedLog *saved_log = NULL;
	int index;

	// Make sure this log is within a relevant time window.
	if (eaSize(&instance->data.logs))
		latest_time = MAX(pLog->iTime, eaTail(&instance->data.logs)->iTime);
	else
		latest_time = pLog->iTime;
	floor_time = LPTimedAggregator_GetLogFloor(pTimedAggregator, latest_time);
	if (!LPTimedAggregator_IsLogTimeRelevant(floor_time, pLog))
		return false;

	// Prune logs which we no longer need.
	LPTimedAggregator_PruneLogs(pTimedAggregator, instance, floor_time);

	// Check safety caps.
	if (!LPTimedAggregator_IsAddingLogSafe(pTimedAggregator, instance, estrLength(&pLog->pRawLogLine)))
		return false;

	// Copy log line.
	saved_log = StructClone(parse_ParsedLog, pLog);

	// Find last possible place to put this log in the array, thus making it stable on order.
	index = (int)eaBFind(instance->data.logs, ParsedLogTimeLess, saved_log);
	while (index < eaSize(&instance->data.logs) && saved_log->iTime == instance->data.logs[index]->iTime)
		++index;

	// Insert the log line.
	eaInsert(&instance->data.logs, saved_log, index);
	instance->log_bytes += estrLength(&saved_log->pRawLogLine);

	return true;
}

// Process a log line.
static void LPTimedAggregator_Log(void *userdata, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	LPTimedAggregator *pTimedAggregator = userdata;
	LPTimedAggregatorInstance *instance = NULL;
	bool success;
	MultiVal **parameters = NULL;
	char *output = NULL;
	CmdContext cmd_context = {0};
	char *line = NULL;
	ParsedLog *pOutputLog;
	static LogParsingRestrictions no_restrictions = {0};

	PERFINFO_AUTO_START_FUNC();

	// Create table, if necessary.
	if (!stTimedAggregators)
		stTimedAggregators = stashTableCreateAddress(0);

	// Find or create an instance of the aggregator data.
	devassert(pTimedAggregator);
	success = stashAddressFindPointer(stTimedAggregators, pTimedAggregator, &instance);
	if (!success)
	{
		instance = StructCreate(parse_LPTimedAggregatorInstance);
		success = stashAddressAddPointer(stTimedAggregators, pTimedAggregator, instance, false);
		devassert(success);
	}

	// Add this log to the aggregator.
	success = LPTimedAggregator_AddLog(pTimedAggregator, instance, pLog);
	if (!success)
	{
		ErrorFilenamef(pTimedAggregator->pFilename, "%s: Failed to add log line, action %s", pTimedAggregator->pCommand, pLog->pObjInfo ? NULL_TO_EMPTY(pLog->pObjInfo->pAction) : "");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Set up command context.
	cmd_context.eHowCalled = CMD_CONTEXT_HOWCALLED_LOGPARSER;
	estrStackCreate(&output);
	cmd_context.output_msg = &output;
	cmd_context.access_level = 9;
	eaPush(&cmd_context.categories, "LogParserAggregator");

	// Set up parameters.
	eaPush(&parameters, MultiValCreate());
	parameters[0]->type = MULTI_NP_POINTER;
	parameters[0]->ptr_noconst = &instance->data;

	// Run the command.
	success = cmdExecuteWithMultiVals(&gGlobalCmdList, pTimedAggregator->pCommand, &cmd_context, &parameters);
	eaDestroy(&cmd_context.categories);
	MultiValDestroy(parameters[0]);
	eaDestroy(&parameters);
	if (!success)
		ErrorFilenamef(pTimedAggregator->pFilename, "%s: Unable to execute command: %s", pTimedAggregator->pCommand, output);

	// If no output, we're done.
	if (!success || !estrLength(&output))
	{
		estrDestroy(&output);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Parse result of command as log fragment.
	estrStackCreate(&line);
	if (pTimedAggregator->pOutputActionName)
	{
		estrPrintf(&line, "%s %6d %s[%d]: : %s(", timeGetLogDateStringFromSecondsSince2000(pLog->iTime), INT_MAX, GlobalTypeToShortName(GLOBALTYPE_PROCEDURAL), LONG_MAX,
			pTimedAggregator->pOutputActionName);
		estrAppendEscaped(&line, output);
		estrConcatChar(&line, ')');
	}
	else
		estrPrintf(&line, "%s %6d %s[%d]: : %s", timeGetLogDateStringFromSecondsSince2000(pLog->iTime), INT_MAX, GlobalTypeToShortName(GLOBALTYPE_PROCEDURAL), LONG_MAX,
			NULL_TO_EMPTY(output));
	pOutputLog = StructCreate(parse_ParsedLog);
	success = ReadLineIntoParsedLog(pOutputLog, line, estrLength(&line), &no_restrictions, LogParserPostProcessCB, 0, NULL);
	estrDestroy(&line);
	if (!success)
	{
		ErrorDetailsf("fragment %s", output);
		ErrorFilenamef(pTimedAggregator->pFilename, "%s returned malformed log fragment: %s", pTimedAggregator->pCommand, output);
		estrDestroy(&output);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	estrDestroy(&line);
	
	// Add procedural log.
	if (pTimedAggregator->pOutputActionName)
		LogParser_AddProceduralLog(pOutputLog, output);
	else
		LogParser_AddProceduralLog(pOutputLog, "timedaggregator");
	estrDestroy(&output);

	PERFINFO_AUTO_STOP_FUNC();
}

// Initialize and register aggregators.
void LPAggregator_InitSystem()
{
	int i;
	for (i=0; i < eaSize(&gLogParserConfig.ppLPTimedAggregator); i++)
	{
		LPTimedAggregator *pTimedAggregator = gLogParserConfig.ppLPTimedAggregator[i];
		RegisterActionSpecificCallback(pTimedAggregator->pActionName, NULL, pTimedAggregator->pCommand, LPTimedAggregator_Log,
			pTimedAggregator, &pTimedAggregator->useThisLog, ASC_AGGREGATOR);
	}
}

// Stub
void LPAggregator_DumpStatusFile()
{
}

// Stub
void LPAggregator_LoadFromStatusFile()
{
}

// Reset aggregators.
void LPAggregator_ResetSystem()
{
	stashTableDestroyStruct(stTimedAggregators, NULL, parse_LPTimedAggregatorInstance);
	stTimedAggregators = NULL;
}

// Clear aggregators and reinitialize.
void LPAggregator_FullyResetSystem()
{
	LPAggregator_ResetSystem();
	LPAggregator_InitSystem();
}

#include "LogParserAggregator_c_ast.c"
#include "LogParserAggregator_h_ast.c"
