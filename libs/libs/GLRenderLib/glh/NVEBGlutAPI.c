////////////////////////////////////////////////////////////
//
// NVEBGlutAPI.c
//
// This file defines the entry points needed to initialize
// and deinitialize GLUT programs that are to be loaded as
// DLLs.
//
////////////////////////////////////////////////////////////


#define WINDOWS_LEAN_AND_MEAN
#include <wininclude.h>
#include <string>


////////////////////////////////////////////////////////////
// Internal types and namespaces

using namespace std;

typedef void (*LPATEXITNOTIFIER)(void*);
typedef void (*LPNVPARSENOTIFIER)(const char*, void*);


////////////////////////////////////////////////////////////
// Internal private helper functions

static bool nvebGlutInit();
static bool nvebGlutExit();
static void __cdecl nvebAtExit();


////////////////////////////////////////////////////////////
// Internal private data

static bool              nvebInstrumented          = false;
static LPATEXITNOTIFIER  nvebAtExitNotifierFunc    = NULL;
static void*             nvebAtExitNotifierCbData  = NULL;
static LPNVPARSENOTIFIER nvebNvParseNotifierFunc   = NULL;
static void*             nvebNvParseNotifierCbData = NULL;


////////////////////////////////////////////////////////////
// Effect entry point (to initialize)

extern "C"
__declspec(dllexport) bool __nvebEffectEntryInit(HINSTANCE hInst)
{
    // Do the C/C++ RT initializations
	if (!nvebGlutInit())
		return false;

    // Record that the program is running in instrumented mode
	nvebInstrumented = true;

    // Setup an atexit() handler to catch accidental calls to exit()
    atexit(&nvebAtExit);

    // Call main with our made up parameters...
	extern int main(int, char**);

	int argc = 1;
	char *argv[2];
	char *argv0 = "NVEffectsBrowser";
	char *argv1 = NULL;

	argv[0] = argv0;
	argv[1] = argv1;

	main(argc, argv);

	return true;
}


////////////////////////////////////////////////////////////
// Effect entry point (at exit)

extern "C"
__declspec(dllexport) bool __nvebEffectEntryExit(HINSTANCE hInst)
{
	if (!nvebGlutExit())
		return false;

	return true;
}


////////////////////////////////////////////////////////////
// Effect entry point (register effect notifier callbacks)

extern "C"
__declspec(dllexport) bool __nvebEffectRegisterAtExitNotifier(void *func, void *cbdata)
{
    nvebAtExitNotifierFunc   = (LPATEXITNOTIFIER) func;
    nvebAtExitNotifierCbData = cbdata;
    return true;
}

extern "C"
__declspec(dllexport) bool __nvebEffectRegisterNvParseNotifier(void *func, void *cbdata)
{
    nvebNvParseNotifierFunc   = (LPNVPARSENOTIFIER) func;
    nvebNvParseNotifierCbData = cbdata;
    return true;
}


////////////////////////////////////////////////////////////
// Effect custom exit() routine

extern "C"
void __cdecl __nvebExit(int res)
{
    extern void __cdecl exit(int);

    // I've changed the behavior here.
    //
    // Just ignoring calls to exit() certainly allows the app
    // to keep running, but that isn't necessarily a good thing.
    // Many apps are coded to reasaonably expect exit() to never
    // return.  When it does, lots of things may break.
    //
    // So instead of ignoring exit() calls, we catch them and
    // the Glut filter tries to do something reasonable (either
    // reset the DLL (if the exit() happens while running) or 
    // tell the browser that we can't run at all (if the exit()
    // occurs during initialization.))
    //
    // So we always, actually call exit().
    //
    exit(res);
}


////////////////////////////////////////////////////////////
// Effect custom nvparse() routine

#ifdef NVEB_USING_NVPARSE

extern "C"
void __cdecl __nvebNvParse(const char *lpProg, ...)
{
    extern void nvparse(const char*, ...);

    if (nvebInstrumented)
        if (nvebNvParseNotifierFunc)
            nvebNvParseNotifierFunc(lpProg, nvebNvParseNotifierCbData);

	if(!strncmp(lpProg, "!!VSP1.0", 8))	{

        va_list ap;
		va_start(ap, lpProg);
		int vpsid = va_arg(ap,int);
		va_end(ap);
		nvparse(lpProg,vpsid);

    } else {

        nvparse(lpProg);
    }
}

#endif


////////////////////////////////////////////////////////////
// Effect internal routines

static bool nvebGlutInit()
{
	return true;
}

static bool nvebGlutExit()
{
	return true;
}

static void __cdecl nvebAtExit()
{
    if (nvebAtExitNotifierFunc)
        nvebAtExitNotifierFunc(nvebAtExitNotifierCbData);
}