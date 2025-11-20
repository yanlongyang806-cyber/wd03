#ifndef WINDEFINCLUDE_H
#define WINDEFINCLUDE_H

#include "stdtypes.h"

C_DECLARATIONS_BEGIN

#if _PS3
#else

#ifdef _XBOX

	#define NOD3D
	#include <xtl.h>
	#include <xbdm.h>

	#define IsDebuggerPresent DmIsDebuggerPresent

#else

	#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && defined(_M_IX86)
	#define _X86_
	#endif

	#if !defined(_68K_) && !defined(_MPPC_) && !defined(_X86_) && !defined(_IA64_) && !defined(_AMD64_) && defined(_M_AMD64)
	#define _AMD64_
	#endif


	#include <windef.h>
	#include <winbase.h>

#undef GetCommandLine
#define GetCommandLine GetCommandLineCryptic
const char *GetCommandLineCryptic(void);

#endif


#if ( defined(_DEBUG) || defined(PROFILE) ) && !defined(MEMALLOC_C) && !defined(TIMING_C) && !defined(XBOX_INCLUDE)
#undef EnterCriticalSection
#undef TryEnterCriticalSection
#undef LeaveCriticalSection
#undef Sleep
#undef SleepEx

#undef WaitForSingleObject
#undef WaitForSingleObjectEx
#undef WaitForMultipleObjects
#undef WaitForMultipleObjectsEx
#undef SetEvent
#undef ResetEvent

#define WaitForSingleObject(h, ms){															\
			static PERFINFO_TYPE* piStatic;													\
			timed_WaitForSingleObject((h), (ms), "WaitForSingleObject("#h")", &piStatic);	\
		}
#define WaitForSingleObjectWithReturn(h, ms, ret){													\
			static PERFINFO_TYPE* piStatic;															\
			(ret) = timed_WaitForSingleObject((h), (ms), "WaitForSingleObject("#h")", &piStatic);	\
		}

#define WaitForSingleObjectEx(h, ms, alertable, name){					\
			static PERFINFO_TYPE* piStatic;																		\
			timed_WaitForSingleObjectEx((h), (ms), (alertable), "WaitForSingleObjectEx("#h name")", &piStatic);\
		}
#define WaitForSingleObjectExWithReturn(h, ms, alertable, ret, name){													\
			static PERFINFO_TYPE* piStatic;																		\
			(ret) = timed_WaitForSingleObjectEx((h), (ms), (alertable), "WaitForSingleObjectEx("#h name")", &piStatic);\
		}

#define WaitForEvent(h, ms) WaitForSingleObject(h, ms) // Different funciton on PS3

#define WaitForMultipleObjects		timed_WaitForMultipleObjects
#define WaitForMultipleObjectsEx	timed_WaitForMultipleObjectsEx
#define SetEvent					timed_SetEvent
#define ResetEvent					timed_ResetEvent

#undef HeapAlloc
#undef HeapCreate
#undef HeapDestroy
#undef HeapReAlloc
#undef HeapFree

#define CONVERT_TO_STRING(x) #x
#define LINE_STR_INTERNAL(x) CONVERT_TO_STRING(x)
#define LINE_STR LINE_STR_INTERNAL(__LINE__)

#define EnterCriticalSection(cs)	{static PERFINFO_TYPE* pi;timed_EnterCriticalSection(cs, "EnterCriticalSection("#cs")", &pi);}
#ifdef PERFINFO_L2
#define TryEnterCriticalSection(cs)	timed_TryEnterCriticalSection(cs, #cs)
#define LeaveCriticalSection(cs)	{static PERFINFO_TYPE* pi;timed_LeaveCriticalSection(cs, #cs, &pi);}
#endif  // PERFINFO_L2
#define Sleep(ms)					{static PERFINFO_TYPE* pi_sleep;timed_Sleep(ms, "Sleep("#ms""__FILE__":"LINE_STR")", &pi_sleep);}
#define SleepEx(ms, alertable)		{static PERFINFO_TYPE* pi;timed_SleepEx(ms, alertable, "SleepEx("#ms""__FILE__":"LINE_STR")", &pi);}

#define HeapAlloc			timed_HeapAlloc
#define HeapCreate			timed_HeapCreate
#define HeapDestroy			timed_HeapDestroy
#define HeapReAlloc			timed_HeapReAlloc
#define HeapFree			timed_HeapFree

#define XPhysicalAlloc(size,address,alignment,protect) physicalmalloc_timed(size,address,alignment,protect,__FILE__ "-Physical", __FILE__, __LINE__)
#define XPhysicalFree(userData) physicalfree_timed(userData, __FILE__ "-Physical")
#endif

// These functions are in timing.c.

void timed_EnterCriticalSection(CRITICAL_SECTION* cs, const char* name, PERFINFO_TYPE** piStatic);
BOOL timed_TryEnterCriticalSection(CRITICAL_SECTION* cs, const char* name);
void timed_LeaveCriticalSection(CRITICAL_SECTION* cs, const char* name, PERFINFO_TYPE** piStatic);
void timed_Sleep(U32 ms, const char* name, PERFINFO_TYPE** piStatic);
void timed_SleepEx(U32 ms, BOOL alertable, const char* name, PERFINFO_TYPE** piStatic);

U32 timed_WaitForSingleObject(HANDLE h, U32 ms, const char* name, PERFINFO_TYPE** pi);
U32 timed_WaitForSingleObjectEx(HANDLE h, U32 ms, S32 alertable, const char* name, PERFINFO_TYPE** pi);
U32 timed_WaitForMultipleObjects(U32 count, HANDLE* handles, S32 waitForAll, U32 ms);
U32 timed_WaitForMultipleObjectsEx(U32 count, HANDLE* handles, S32 waitForAll, U32 ms, S32 isAlertable);
BOOL timed_SetEvent(HANDLE h);
BOOL timed_ResetEvent(HANDLE h);

LPVOID	timed_HeapAlloc(HANDLE hHeap, DWORD dwFlags, SIZE_T dwBytes);
HANDLE	timed_HeapCreate(DWORD flOptions, SIZE_T dwInitialSize, SIZE_T dwMaximumSize);
BOOL	timed_HeapDestroy(HANDLE hHeap);
LPVOID	timed_HeapReAlloc(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem, SIZE_T dwBytes);
BOOL	timed_HeapFree(HANDLE hHeap, DWORD dwFlags, LPVOID lpMem);


#endif

// Putting these here to avoid including stat.h to track down calls to non-64-bit stat
#define _S_IFMT         0xF000          /* file type mask */
#define _S_IFDIR        0x4000          /* directory */
#define _S_IFCHR        0x2000          /* character special */
#define _S_IFIFO        0x1000          /* pipe */
#define _S_IFREG        0x8000          /* regular */
#define _S_IREAD        0x0100          /* read permission, owner */
#define _S_IWRITE       0x0080          /* write permission, owner */
#define _S_IEXEC        0x0040          /* execute/search permission, owner */

C_DECLARATIONS_END

#endif