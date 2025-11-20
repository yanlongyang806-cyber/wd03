#pragma once
GCC_SYSTEM

typedef struct SendErrorToTrackerParams
{
	U32 paramsSize; // Must be set!
	U32 *pOutputFlags;
	ErrorData errorData;
	
} SendErrorToTrackerParams;

typedef struct SendDumpToTrackerParams
{
	U32 paramsSize; // Must be set!
	U32 dumpFlags;

} SendDumpToTrackerParams;

