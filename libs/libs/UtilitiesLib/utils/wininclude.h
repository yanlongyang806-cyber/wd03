#ifndef WININCLUDE_H
#define WININCLUDE_H
#pragma once
#include "stdtypes.h"

#if _PS3

// some of win32 API wrapper types should go here

#define CONVERT_TO_STRING(x) #x
#define LINE_STR_INTERNAL(x) CONVERT_TO_STRING(x)
#define LINE_STR LINE_STR_INTERNAL(__LINE__)

#else

#include "windefinclude.h"

C_DECLARATIONS_BEGIN

#if !_XBOX

#include <winsock2.h>	//This grabs ALL of windows.h

#if ( defined(_DEBUG) || defined(PROFILE) ) && !defined(MEMALLOC_C) && !defined(TIMING_C) && !defined(XBOX_INCLUDE)

#undef MsgWaitForMultipleObjects
#undef MsgWaitForMultipleObjectsEx
#define MsgWaitForMultipleObjects	timed_MsgWaitForMultipleObjects
#define MsgWaitForMultipleObjectsEx	timed_MsgWaitForMultipleObjectsEx

U32 timed_MsgWaitForMultipleObjects(U32 count, HANDLE* handles, S32 waitForAll, U32 ms, U32 wakeMask);
U32 timed_MsgWaitForMultipleObjectsEx(U32 count, HANDLE* handles, U32 ms, U32 wakeMask, U32 flags);

#endif

#endif

C_DECLARATIONS_END

#endif
#endif
