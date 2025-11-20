/* File StringTable.h
 *	A string table is basically a buffer space that holds tightly packed string.  The table
 *	grows in large memory chunks whenever it runs out of space to store more strings.
 *
 *	Create and using a string table:
 *		Call strTableCreate with the desired chunkSize.  This size tells
 *		the string table how much memory to allocate every time the string table
 *		runs out of space to hold the string being inserted.
 *
 *	Inserting strings into the string table:
 *		Call the strTableAddString function with a the handle of the table to add the string
 *		to and the string to be added.  The index of the string will be returned.
 *
 */

#ifndef STRINGTABLE_H
#define STRINGTABLE_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"

typedef struct StashTableImp *StashTable;

typedef struct StringTableImp StringTableImp;
typedef StringTableImp *StringTable;
typedef int (*StringProcessor)(char*);

/* Enum StringTableMode
 *	Defines several possible StashTable operation modes.
 *	
 *	Default:
 *		Makes the table non-indexable.
 *
 *	Indexable:
 *		Allow all the strings in the table to be accessed via some index.
 *		The index reflects the order of string insertion into the table and
 *		will be static over the lifetime of the StringTable.
 *
 *		Making the strings indexable requires that the string table use 
 *		additional memory to keep track of the index-to-string relationship.  
 *		Currently, this overhead is at 4 bytes per string.
 */
typedef enum
{
	StrTableDefault =		0,
	Indexable =				(1 << 0),
	InSharedHeap =			(1 << 2),
	StrTableVirtualAlloc =	(1 << 3),
} StringTableMode;


// Constructor + Destructor
void destroyStringTable(StringTable table);
StringTable strTableCreateEx(StringTableMode mode, unsigned int chunkSize MEM_DBG_PARMS);
#define strTableCreate(mode, chunkSize) strTableCreateEx(mode, chunkSize MEM_DBG_PARMS_INIT)
StringTable strTableCheckForSharedCopy(const char* pcStringSharedHashKey, bool* bFirstCaller);
StringTable strTableCopyIntoSharedHeap(StringTable table, const char* pcStringSharedHashKey, bool bAlreadyAreFirstCaller );
size_t strTableGetCopyTargetAllocSize(StringTable table);
StringTable strTableCopyEmptyToAllocatedSpace(StringTable table, void* pAllocatedSpace, size_t uiTotalSize );
StringTable strTableCopyToAllocatedSpace(StringTable table, void* pAllocatedSpace, size_t uiTotalSize );

void* strTableAddString(StringTable table, const void* str);
int strTableAddStringGetIndex(StringTable table, const void* str);
void strTableClear(StringTable table);

// String enumeration
int strTableGetStringCount(StringTable table);
char* strTableGetString(StringTable table, int i);
StashTable strTableCreateStringToIndexMap(StringTable table);
const char* strTableGetConstString(StringTable table, int i);
size_t strTableMemUsage(StringTable table);

// StringTable mode query/alteration
void strTableForEachString(StringTable table, StringProcessor processor);
StringTableMode strTableGetMode(StringTable table);

void strTableLock(StringTable table);

// We can't remove strings, but we can record how many we would have removed and track how much memory is wasted because of it.
void strTableLogRemovalRequest(StringTable table, const char* pcStringToRemove);

void printStringTableMemUsage(void);

#endif