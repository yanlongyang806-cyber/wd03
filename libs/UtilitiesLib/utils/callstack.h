#ifndef CALLSTACK_H
#define CALLSTACK_H
#pragma once
GCC_SYSTEM

#include "guiddef.h"

// Make sure to update LineContentHeaders in callstack.c to match order and count!
typedef enum LineContents
{
	LINECONTENTS_IGNORE = 0,

	LINECONTENTS_MODULES_START,
	LINECONTENTS_MODULE_NAME,
	LINECONTENTS_MODULE_PDB,
	LINECONTENTS_MODULE_BASE_ADDRESS,
	LINECONTENTS_MODULE_SIZE,
	LINECONTENTS_MODULE_TIME,
	LINECONTENTS_MODULE_GUID,
	LINECONTENTS_MODULE_AGE,
	LINECONTENTS_MODULE_METHOD,
	LINECONTENTS_MODULES_END,

	LINECONTENTS_CALLSTACK_START,
	LINECONTENTS_CALLSTACK_ADDRESS,
	LINECONTENTS_CALLSTACK_END,

	LINECONTENTS_COUNT
} LineContents;

// Label strings
extern const char *LineContentHeaders[];

// ----------------------------------------------------------------------------
// "Report" - Human readable text listing all possible module/stack information
//            that can be known without debugging information. 

// Generates Report into s from current state, to be used mid-crash
bool callstackWriteTextReport(char *s, int iMaxLength);

// ----------------------------------------------------------------------------
// GUID helper functions

bool GUIDFromString(const char *pStr, GUID *pGUID);

// ----------------------------------------------------------------------------
// CallStack data - Typically generated from Reports (as defined above)

AUTO_STRUCT;
typedef struct CallStackModule
{
	char *pModuleName;
	char *pPDBName;
	char *pGuid;
	U64 uBaseAddress;
	U32 uSize;
	U32 uTime;
	U32 uAge;
} CallStackModule;

AUTO_STRUCT;
typedef struct CallStackEntry
{
	U64 uAddress;
} CallStackEntry;

AUTO_STRUCT;
typedef struct CallStack
{
	CallStackModule **ppModules;
	CallStackEntry **ppEntries;
} CallStack;

CallStack * callstackCreateFromTextReport(const char *pReport);

#endif
