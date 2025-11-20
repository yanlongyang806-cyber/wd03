#pragma once

#include "stashTable.h"

typedef struct LogParserUniqueCounterDefinition LogParserUniqueCounterDefinition;

AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Count");
typedef struct LogParserUniqueCounter
{
	char *pName; AST(KEY)
	LogParserUniqueCounterDefinition *pDefinition; AST(UNOWNED)

	int iCount;

	StashTable sFoundThings; NO_AST
	U32 *pFoundThingsArray;
	
	U32 iNextCompletionTime; AST(FORMATSTRING(HTML_SECS=1))

} LogParserUniqueCounter;

LogParserUniqueCounter *LogParserUniqueCounter_FindByName(char *pName);
void LogParserUniqueCounter_InitAllFromConfig(void);
void LogParserUniqueCounter_WriteOut(void);
void LogParserUniqueCounter_Tick(void);
