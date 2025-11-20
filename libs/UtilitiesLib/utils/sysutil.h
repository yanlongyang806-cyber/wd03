#pragma once
GCC_SYSTEM

C_DECLARATIONS_BEGIN

typedef struct HWND__ *HWND;

char* getComputerName(void);
char* getExecutableName(void);
char* getExecutableVersion(int dots);
char* getExecutableVersionEx(char* executableName, int dots);
int versionCompare(char* version1, char* version2);
char *getExecutableDir(char *buf);

char *getExecutableTopDir(void); //if "c:\startrek_server\startrek\controller.exe", returns "c:\startrek_server\"
char *getExecutableTopDirCropped(void); //if "c:\startrek_server\startrek\controller.exe", returns "startrek_server"

unsigned long getPhysicalMemoryEx(unsigned long *max, unsigned long *avail, unsigned long *availVirtual);
U64 getPhysicalMemory64Ex(U64 *max, U64 *avail, U64 *availVirtual);
unsigned long getPhysicalMemory(unsigned long *max, unsigned long *avail); // Returns max when NULLs are passed in
U64 getPhysicalMemory64(U64 *max, U64 *avail);
size_t getProcessPageFileUsage(void);
size_t getProcessImageSize(void);
size_t xboxGetSystemReservedSize(void);
U64 getVirtualAddressSize(void);

#if _PS3
#define winCopyFromClipboardUnicode() 0
#define winCopyToClipboard(s)
#define winCopyFromClipboard() 0
#define winCopyUTF8ToClipboard(s)
#define winCopyUTF8FromClipboard() 0
#else
const char *winCopyFromClipboardUnicode(void); //Returns the format data name
void winCopyToClipboard(const char* s);
const char *winCopyFromClipboard(void); // Returns pointer to static buffer
void winCopyUTF8ToClipboard(const char* s);
const char *winCopyUTF8FromClipboard(void); // Returns pointer to static buffer
#endif

#ifndef PLATFORM_CONSOLE
void hideConsoleWindow(void);

//might end up with the window appearing in the taskbar, but not actually being 
//a visible window (calls SW_SHOW internally)
void showConsoleWindow(void);
//calls SW_NORMAL internally
void showConsoleWindow_NoTaskbar(void);


bool IsConsoleWindowVisible(void);

void HideConsoleWindowByPID(U32 pid);
void ShowConsoleWindowByPID(U32 pid);

HWND FindHWNDFromPid(U32 pid);
#endif

HWND compatibleGetConsoleWindow(void);
void disableRtlHeapChecking(void *heap);
void preloadDLLs(int silent);


void PreloadSharedAddress(int silent);

extern bool gWaitedForDebugger;

#if PLATFORM_CONSOLE
    #define WAIT_FOR_DEBUGGER
    #define WAIT_FOR_DEBUGGER_LPCMDLINE
#else

void sharedMemoryProcessCommandLine();

// Check if -WaitForDebugger or -WaitForDebugger_Break has been requested, and if so, wait.
void checkWaitForDebugger(const char *lpCmdLine, int argc, char *const *argv);

#define WAIT_FOR_DEBUGGER checkWaitForDebugger(NULL, argc, argv);

#define WAIT_FOR_DEBUGGER_LPCMDLINE checkWaitForDebugger(lpCmdLine, 0, NULL);

//for debugging purposes, you might add this to the beginning of a function where something is going to go wrong,
//and you want to make sure you get attached before whatever it is happens
void waitForDebugger(bool wait_for_debugger_break);

#endif

// Print working set size parameters in a cross-platform safe way.
void printProcessWorkingSetSize(void);

// Return the current Wine version, or NULL if not running under Wine
const char *getWineVersion(void);
bool getWineVersionNumbers(int *piMajor, int *piMinor, int *piRevision);
bool getWineVersionOrLater(int iMajor, int iMinor, int iRevision);
bool getIsTransgaming(void);
const char *getTransgamingInfo(void);

#define XPERFDUMP_DIRECTORY_PATH "C:\\night\\tools\\xperf\\traces\\"
void xperfEnsureRunning(void);
void xperfDump(const char *filename); // filename should not be an absolute path
void xperfView(const char *filename); // filename should not be an absolute path

void autoPrintExecutableVersion(void);

//spawn handle.exe and dump a report about all handles for a given filename into a file in loggingdir, and generate
//a threadsafe alert
void RunHandleExeAndAlert(const char *pAlertKey, const char *pBadFile, char *pShortLogFileName, FORMAT_STR const char *pMessage, ...);

// Return data for a Windows resource.
void *allocResourceById(const char *type, int id, size_t *size);

// Sleep for a certain number of seconds.
void delay(U32 milliseconds);

//convert wide to utf8 argvs
char *UTF16_to_UTF8_CommandLine(const S16 *pIn);

#define ARGV_WIDE_TO_ARGV										\
	{															\
		int _i;													\
		argv = malloc(sizeof(char*) * argc);					\
		for (_i = 0 ; _i < argc; _i++)								\
		{														\
			argv[_i] = UTF16_to_UTF8_CommandLine(argv_wide[_i]);	\
		}														\
	}	


C_DECLARATIONS_END
