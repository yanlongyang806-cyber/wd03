#include "earray.h"
#include "LogParserAggregator.h"
#include "LogParserAggregatorCommands_c_ast.h"
#include "LogParsing.h"
#include "LogParsing_h_ast.h"
#include "NameValuePair.h"
#include "StashTable.h"
#include "StringCache.h"
#include "timing.h"

// A temporary variable that can be used to hold an EString result value. (NOT THREAD SAFE)
static char *sResult = NULL;

// Aggregator userdata for EmitLastValueForEachGenericArrayString().
AUTO_STRUCT;
typedef struct EmitLastValueForEachGenericArrayStringData
{
	StashTable ststr2intCategories;				// key: deep copy string (category name), value: int (count sum)
} EmitLastValueForEachGenericArrayStringData;

// Custom parser for Patch Server-generated patcher GenericArray.
AUTO_COMMAND ACMD_CATEGORY(LogParserAggregator);
char *EmitLastValueForEachGenericArrayString(LPTimedAggregatorData *data)
{
	GenericArray **array;
	static StashTable ststr2intServerAndCategories = NULL;	// key: deep copy string (serverid : category name), value: int (count sum) (NOT THREAD SAFE)
	const ParsedLog *const *logs = data->logs;
	EmitLastValueForEachGenericArrayStringData *userdata;
	GenericArray **output = NULL;
	GenericArray output_outer = {0};

	// Allocate user data, if necessary.
	if (data->userdata)
		userdata = data->userdata;
	else
	{
		data->usertable = parse_EmitLastValueForEachGenericArrayStringData;
		data->userdata = StructCreateVoid(data->usertable);
		userdata = data->userdata;
		userdata->ststr2intCategories = stashTableCreateWithStringKeys(128, StashDefault);
	}

	// The flag for each category in the list of categories previously aggregated.
	FOR_EACH_IN_STASHTABLE2(userdata->ststr2intCategories, elem)
	{
		stashElementSetInt(elem, 0);
	}
	FOR_EACH_END;

	// Reduce logs into the last category emitted by each server.
	if (!ststr2intServerAndCategories)
		ststr2intServerAndCategories = stashTableCreateWithStringKeys(128, StashDefault);
	EARRAY_CONST_FOREACH_BEGIN(logs, i, n);
	{
		const ParsedLog *log = logs[i];

		// If there's no array, skip it.
		array = log->pObjInfo->pGenericArray.array;
		if (!array || !eaSize(&array))
			continue;

		// Overwrite each category.
		EARRAY_CONST_FOREACH_BEGIN(array, j, m);
		{
			GenericArray *category = array[j];
			const char *name = category->string;
			int count = category->integer;
			char key[256];
			snprintf(key, sizeof(key), "%u:%s", log->iServerID, name);
			stashAddInt(ststr2intServerAndCategories, allocAddString(key), count, true);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	// Further reduce by accumulating counts from all servers by category.
	FOR_EACH_IN_STASHTABLE2(ststr2intServerAndCategories, elem)
	{
		char *key = stashElementGetStringKey(elem);
		int count = stashElementGetInt(elem);
		char *name_unpooled = strchr(key, ':') + 1;
		const char *name = allocAddString(name_unpooled);
		int sum = 0;
		stashFindInt(userdata->ststr2intCategories, name, &sum);
		sum += count;
		stashAddInt(userdata->ststr2intCategories, name, sum, true);

	}
	EARRAY_FOREACH_END;
	stashTableClear(ststr2intServerAndCategories);

	// Write reduced categories to array.
	FOR_EACH_IN_STASHTABLE2(userdata->ststr2intCategories, elem)
	{
		GenericArray *entry = StructCreate(parse_GenericArray);
		entry->string = stashElementGetStringKey(elem);
		entry->integer = stashElementGetInt(elem);
		eaPush(&output, entry);
		if (!entry->integer)
			stashRemovePointer(userdata->ststr2intCategories, stashElementGetStringKey(elem), NULL);
	}
	FOR_EACH_END;

	// Add no longer existent categories to the output array.
	FOR_EACH_IN_STASHTABLE2(userdata->ststr2intCategories, elem)
	{
		int flag = stashElementGetInt(elem);
		if (!flag)
		{
			GenericArray *entry = StructCreate(parse_GenericArray);
			entry->string = stashElementGetStringKey(elem);
			entry->integer = 0;
			eaPush(&output, entry);
		}
	}
	FOR_EACH_END;

	// Write log line.
	output_outer.array = output;
	estrClear(&sResult);
	ParserWriteText(&sResult, parse_GenericArray, &output_outer, 0, 0, 0);
	eaDestroyEx(&output, NULL);

	return sResult;
}

// Sum each pair value over the time range, and calculate relative percents.
AUTO_COMMAND ACMD_CATEGORY(LogParserAggregator);
char *PairSumPercents(LPTimedAggregatorData *data)
{
	const ParsedLog *const *logs = data->logs;
	StashTable pairs = stashTableCreateWithStringKeys(0, StashDeepCopyKeys);
	float total = 0;

	// Loop over each pair in each log.
	EARRAY_CONST_FOREACH_BEGIN(logs, i, n);
	{
		const ParsedLog *log = logs[i];
		if (log->ppPairs)
		{
			EARRAY_CONST_FOREACH_BEGIN(log->ppPairs, j, m);
			{
				NameValuePair *pair = log->ppPairs[j];
				if (pair && pair->pName && pair->pValue)
				{
					float value = atof(pair->pValue);
					float *sum;

					// Don't accumulate unless it's positive.
					if (value <= 0)
						continue;

					// Find pair.
					stashFindPointer(pairs, pair->pName, &sum);
					if (!sum)
					{
						bool success;
						sum = calloc(1, sizeof(float));
						success = stashAddPointer(pairs, pair->pName, sum, false);
						devassert(success);
					}

					// Accumulate value.
					*sum += value;
					total += value;
				}
			}
			EARRAY_FOREACH_END;
		}
	}
	EARRAY_FOREACH_END;

	// If we didn't accumulate anything, return.
	if (!total)
		return "";

	// Create percent name-value pair string.
	estrClear(&sResult);
	FOR_EACH_IN_STASHTABLE2(pairs, elem)
	{
		const char *name = stashElementGetStringKey(elem);
		float *sum = stashElementGetPointer(elem);
		if (estrLength(&sResult))
			estrConcatChar(&sResult, ' ');
		estrConcatf(&sResult, "%s %f", name, *sum/total);
		free(sum);
	}
	FOR_EACH_END;
	stashTableDestroy(pairs);

	return sResult;
}

#include "LogParserAggregatorCommands_c_ast.c"
