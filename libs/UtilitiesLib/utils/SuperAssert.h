#pragma once
#include "memcheck.h"
GCC_SYSTEM

#if _PS3
#if !SPU
#include <../../ppu/include/assert.h>
#endif

#else
#include <stdlib.h>
#include <excpt.h>
#include <process.h>
#endif

#include "stdtypes.h"
#include "fpmacros.h"

#ifdef __cplusplus
extern "C"{
#endif

typedef struct _EXCEPTION_POINTERS *PEXCEPTION_POINTERS;

const char *getErrorTracker();

void assertOverrideMiniDumpFilename(const char *pFilename);
void assertOverrideFullDumpFilename(const char *pFilename);


char * assertGetMiniDumpFilename();
char * assertGetFullDumpFilename();
S16 * assertGetMiniDumpFilename_Wide();
S16 * assertGetFullDumpFilename_Wide();

int ceSpawnXperf(const char *errortext, const char *filename);

#if !_PS3
typedef struct _EXCEPTION_POINTERS *PEXCEPTION_POINTERS;

bool assertWriteMiniDump(char* filename, PEXCEPTION_POINTERS info);
bool assertWriteMiniDumpSimple(PEXCEPTION_POINTERS info);
void assertWriteFullDump(char* filename, PEXCEPTION_POINTERS info);
bool assertWriteFullDumpSimple(PEXCEPTION_POINTERS info);
#endif

void assertDoNotFreezeThisThread(unsigned long threadID);

typedef void (*AssertCallback)(char* errMsg);

typedef void (*assertSndCB)(void);
void assertSetSndCB(assertSndCB cb);

typedef void (*AuxAssertCB)(const char* expr, const char* errormsg, const char* filename, unsigned lineno);

void SetAuxAssertCB(AuxAssertCB pCB);

#define ASSERTMODE_NODEBUGBUTTONS		(1 << 0)

#define ASSERTMODE_MINIDUMP			(1 << 2)

#define ASSERTMODE_DATEDMINIDUMPS	(1 << 4) // Name .mdmp and .dmp files based on date/time
#define ASSERTMODE_FULLDUMP			(1 << 5) // Perform a full dump (needs userdump.exe)

#define ASSERTMODE_ZIPPED			(1 << 7) // Zip .mdmp and .dmp files


#define ASSERTMODE_SENDCALLSTACK	(1 << 10) // Send the actual callstack (rather than just a text parse of it) to ErrorTracker
#define ASSERTMODE_NOERRORTRACKER	(1 << 11) // Don't send anything to the error tracker (for beaconizer - Adam)
#define ASSERTMODE_NOERRORSTACKWALK (1 << 12) // Don't run stackwalk on Errorf's 
#define ASSERTMODE_TEMPORARYDUMPS   (1 << 13) // Tries to write dumps to Windows temp directory, always adds date (no effect on XBox)
#define ASSERTMODE_ISEXTERNALAPP    (1 << 14) 
#define ASSERTMODE_FORCEFULLDUMPS   (1 << 15)
#define ASSERTMODE_USECRYPTICERROR  (1 << 16) // Executes CrypticError.exe instead of processing dump/error internally
#define ASSERTMODE_PASSPRODUCTIONMODE (1 << 17) // If production mode flag should be passed down to CrypticError


#if _DEBUG || PROFILE

    #define _DbgBreak() __debugbreak() // portable compiler intrinsic

	//#pragma message ("DEBUG ASSERT ###################################")
	bool stackdumpIsUseful(const char *pStackdump);
	int __cdecl superassert(const char* expr, const char* errormsg,  bool isFatalError, const char* filename, unsigned lineno);
	int __cdecl superassertf(const char* expr, FORMAT_STR const char* errormsg_fmt, bool isFatalError, const char* filename, unsigned lineno, ...);

#if _PS3
    #define assertIsExecutableInToolsBin() 0
#else
	int assertIsExecutableInToolsBin(void);
#endif

	void setAssertMode(int assertmode);
#define setDefaultAssertMode() setAssertMode((assertIsExecutableInToolsBin()? (ASSERTMODE_MINIDUMP|ASSERTMODE_TEMPORARYDUMPS) : 0) | ASSERTMODE_USECRYPTICERROR)
#define setProductionClientAssertMode() setAssertMode(ASSERTMODE_MINIDUMP|ASSERTMODE_SENDCALLSTACK|ASSERTMODE_TEMPORARYDUMPS|ASSERTMODE_NODEBUGBUTTONS|ASSERTMODE_ISEXTERNALAPP|ASSERTMODE_USECRYPTICERROR|ASSERTMODE_PASSPRODUCTIONMODE)
	int getAssertMode();
	void setAssertExtraInfo(const char* info);			// used for map id's - added to assert info
	void setAssertExtraInfo2(const char* info);			// used for driver versions, etc - added to assert info
	void setAssertProgramVersion(const char* versionName);	
	void setAssertCallback(AssertCallback func); // This callback will occur in a second thread, so be careful what you do!
	int programIsShuttingDown(void);
	void setProgramIsShuttingDown(int val, char *pFile, int iLine);

	void closeSockOnAssert(uintptr_t sock);

	// if submitting error reports, force an error to be caught instead of asserting 
	int assertSubmitErrorReports(void);
	extern int g_ignoredivzero;

	#define assertmsgfEx(exp, isFatalError, msg, ...)	(((exp) || ((superassertf(#exp, FORMAT_STRING_CHECKED(msg), isFatalError, __FILE__, __LINE__, __VA_ARGS__)==EXCEPTION_CONTINUE_SEARCH)?(__debugbreak(),0): 0)), __assume(exp), 0)
	#define assertmsgEx(exp, isFatalError, msg)			(((exp) || ((superassert(#exp, msg, isFatalError, __FILE__, __LINE__)==EXCEPTION_CONTINUE_SEARCH)?(__debugbreak(),0): 0)), __assume(exp), 0)

	#define assertmsgf(exp, msg, ...)	assertmsgfEx(exp, false, msg, __VA_ARGS__)
	#define assertmsg(exp, msg)			assertmsgEx(exp, false, msg)

#ifdef assert
#undef assert
#endif

    #define assert(exp)					assertmsg(exp, 0)
	#define ignorableAssertmsgf(exp, msg, ...)	assertmsgfEx(exp, false, msg, __VA_ARGS__)
	#define ignorableAssertmsg(exp, msg)		assertmsgEx(exp, false, msg)
	#define ignorableAssert(exp)				ignorableAssertmsg(exp, 0)
	
	#define fatalerrorAssertmsgf(exp, msg, ...)	assertmsgfEx(exp, true, msg, __VA_ARGS__)
	#define fatalerrorAssertmsg(exp, msg)       assertmsgEx(exp, true, msg)
	#define fatalerrorAssert(exp)               fatalerrorAssertmsg(exp, 0)


	int assertIsDevelopmentMode(void);
	void AssertErrorf(const char * file, int line, FORMAT_STR char const *fmt, ...);
	void AssertErrorFilenamef(const char *file, int line, const char *filename, FORMAT_STR char const *fmt, ...);

	// assert in development mode, error in production mode
	#define devassert(exp) ((exp) || (assertIsDevelopmentMode() ? assertmsg(exp, 0) : (AssertErrorf(__FILE__, __LINE__, "devassert: %s", #exp),0)))
	#define devassertmsg(exp, msg) ((exp) || (assertIsDevelopmentMode() ? assertmsg(exp, msg) : (AssertErrorf(__FILE__, __LINE__, "devassert: %s; %s", #exp, msg),0)))
	#define devassertmsgf(exp, msg, ...) ((exp) || (assertIsDevelopmentMode() ? assertmsgf(exp, msg, __VA_ARGS__) : (AssertErrorf(__FILE__, __LINE__, "devassert: %s;" msg, #exp, __VA_ARGS__),0)))

	// Like devassert(), but ask Error Tracker to not ignore the error string.
	// This causes asserts with different messages to be considered unique.
	#define devassertmsgunique(exp, msg) ((exp) || (assertIsDevelopmentMode() ? fatalerrorAssertmsg(exp, msg) : (AssertErrorf(__FILE__, __LINE__, "devassert: %s; %s", #exp, msg),0)))
	#define devassertmsguniquef(exp, msg, ...) ((exp) || (assertIsDevelopmentMode() ? fatalerrorAssertmsgf(exp, msg, ##__VA_ARGS__) : (AssertErrorf(__FILE__, __LINE__, "devassert: %s;" msg, #exp, __VA_ARGS__),0)))

	#define exit(ret)	  ( setProgramIsShuttingDown(1, __FILE__, __LINE__), exit(ret) )

	extern PEXCEPTION_POINTERS stackOverflowExceptionPointers;
	extern void *stackOverflowTib;
	extern void *stackOverflowBoundingFramePointer;

	// wrap around all threads to catch all exceptions
#if _PS3
	#define EXCEPTION_HANDLER_BEGIN
	#define EXCEPTION_HANDLER_END_EXPR
	#define EXCEPTION_HANDLER_END_RESPONSE
	#define EXCEPTION_HANDLER_END
#else
	char* GetExceptionName(unsigned long code);
	U32 assertGetCurrentThreadID(void);
	static __forceinline int stackoverflow(void) { while(1); return 1; } // Another thread asserts on the stack overflow
	int assertExcept(unsigned int code, PEXCEPTION_POINTERS info, void *tib, void *boundingFramePointer);
	void winAboutToExitMaybeCrashed(void);
	void stackwalkSetMainFunctionPointer(void *ptr);  // Set pointer near base of stack, to help stack unwinding
	void *GetCurrentThreadTib(void); // from winutil.h
	void *GetBoundingFramePointer(void); // from winutil.h

	#define EXCEPTION_HANDLER_BEGIN __try { int _cryptic_dummy_frame_marker; stackwalkSetMainFunctionPointer(&_cryptic_dummy_frame_marker); SET_FP_CONTROL_WORD_DEFAULT; {
	#define EXCEPTION_HANDLER_END_EXPR(exc_code, exc_info) ((exc_code) == 0xC00000FDL /* EXCEPTION_STACK_OVERFLOW */ ? ((stackOverflowExceptionPointers = (exc_info), stackOverflowTib = GetCurrentThreadTib(), stackOverflowBoundingFramePointer = GetBoundingFramePointer()), stackoverflow()) : assertExcept((exc_code), (exc_info), GetCurrentThreadTib(), GetBoundingFramePointer()))
	#define EXCEPTION_HANDLER_END_RESPONSE winAboutToExitMaybeCrashed(); exit(1); return 1;
	#define EXCEPTION_HANDLER_END } } \
		__except (EXCEPTION_HANDLER_END_EXPR(GetExceptionCode(), GetExceptionInformation())) \
		{ EXCEPTION_HANDLER_END_RESPONSE }
	#endif

	#define verify devassert

	// the counter guarantees uniqueness for each time used as a param.
	#define UNIQUEVAR2(CTR) do_not_use_uniquevar_foo_bar ## CTR
	#define UNIQUEVAR UNIQUEVAR2(__COUNTER__)

#else

#if _PS3
	#undef assert
#endif

	//#pragma message ("NON DEBUG ASSERT ###################################")
	#define setAssertMode(mode) ((void)0)
	#define getAssertMode() (0)
	#define setAssertExtraInfo(info) ((void)0)
	#define setAssertExtraInfo2(info) ((void)0)
	#define setAssertResponse(res)	((void)0)
	#define setAssertProgramVersion(ver)	((void)0)
	#define setAssertCallback(func)	((void)0)
	#define assert(exp)				((void)0)
	#define assertmsg(exp, msg)		((void)0)
	#define devassertmsg(exp, msg)	((void)0)
	#define devassert(exp)			((void)0)
	#define assertExcept(code, info, tib, bound) (EXCEPTION_CONTINUE_SEARCH)
	#define EXCEPTION_HANDLER_BEGIN
	#define EXCEPTION_HANDLER_END_EXPR
	#define EXCEPTION_HANDLER_END_RESPONSE
	#define EXCEPTION_HANDLER_END
	#define verify(exp) exp
	#define verifyFn(fn, exp, msg) exp

#endif

// Pointer to crashState()
typedef int (*crashStateFunc)(int set);

// Access the crash state directly, for overloading a crashState() function in another module.
// Use isCrashed() instead of this for just telling if we're crashed.
int crashState(int set);

// Override our crashState() with one from another module.
void setCrashState(crashStateFunc func);

// Return true if we've crashed.
int isCrashed(void);

int CompareUsefulVersionStrings(const char *s1, const char *s2);

void assertSetAccountName(const char *accountName);
char* assertGetAccountName(void);
void assertClearAccountName(void);

// Clues in CrypticError that this should be treated as a server. 
// Good for production game clients running on this side of the fence,
// such as no-gfxmode test clients and headshot servers.
void assertForceServerMode(int bForce); 

void runRemoteDebugger();

extern bool gbLeaveCrashesUpForever;

//if you need to call asserts in the future with TimedCallback (usually done when things are shutting down), 
//use this:
typedef struct TimedCallback TimedCallback;
void assertTimedCallback(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);

//if true, then on any assert, create a dump. Usually this is true in production mode, and for anything
//run by a CB, and false otherwise, but some weird types of server setups have their own more complicated logic.
LATELINK;
bool assertForceDumps(void);

#ifdef __cplusplus
}
#endif

