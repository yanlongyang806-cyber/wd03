#ifndef CRYPTIC_LOGPARSER_AGGREGATOR_H
#define CRYPTIC_LOGPARSER_AGGREGATOR_H

#include "LogParser.h"

// Aggregate logs based on a certain number of periods of a certain length
AUTO_STRUCT;
typedef struct LPTimedAggregator
{
	// Source
	char *pFilename;						AST(CURRENTFILE)				// Filename this object was loaded from

	// Match criteria.
	const char *pActionName;				AST(POOL_STRING)				// The action name to look for in a log
	UseThisLogCheckingStruct useThisLog;	AST(EMBEDDED_FLAT)

	// Aggregation command.
	char *pCommand;															// Run this auto command
	const char *pOutputActionName;			AST(POOL_STRING)				// Optional action name to output

	// Aggregation period
	U32 iLength;							AST(DEFAULT(1))					// Length, in seconds, of each period
	U32 iOffset;															// Rounding offset of each period
	U32 iPeriods;							AST(DEFAULT(1))					// Number of periods to aggregate
	size_t iMaxLogs;						AST(INT DEFAULT(1024))				// Safety cap on number of logs to aggregate
	size_t iMaxLogBytes;					AST(INT DEFAULT(20*1024*1024))		// Safety cap on total bytes of log lines to aggregate
} LPTimedAggregator;

typedef struct LPTimedAggregatorData LPTimedAggregatorData;

// Optional destructor for LPTimedAggregatorData.
typedef void (*LPTimedAggregatorDataDestructor)(LPTimedAggregatorData *data);

// Aggregator state
AUTO_STRUCT;
typedef struct LPTimedAggregatorData
{
	ParsedLog **logs;									// Log lines for current periods
	void *userdata;								NO_AST	// Pointer owned by the aggregator
	LPTimedAggregatorDataDestructor destructor;	NO_AST	// If non-null, called to destroy object
	ParseTable *usertable;						NO_AST	// If non-null, ParseTable for userdata, needed to destroy it
} LPTimedAggregatorData;

// Initialize and register aggregators.
void LPAggregator_InitSystem(void);

// Stub
void LPAggregator_DumpStatusFile(void);

// Stub
void LPAggregator_LoadFromStatusFile(void);

// Clear aggregators.
void LPAggregator_ResetSystem(void);

// Clear aggregators and reinitialize.
void LPAggregator_FullyResetSystem(void);

#endif  // CRYPTIC_LOGPARSER_AGGREGATOR_H
