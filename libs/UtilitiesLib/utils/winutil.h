#pragma once
GCC_SYSTEM

#if _PS3

// title & fault optional
void errorDialog_(char *str, char* title, char* fault, int highlight);
#define errorDialog(hwnd, str, title, fault, highlight) errorDialog_(str, title, fault, highlight) 

void msgAlert_(const char *str);
#define msgAlert(hwnd, str) msgAlert_(str)

#define setErrorDialogCallback(callback, userdata)
#define pushErrorDialogCallback(callback, userdata)
#define popErrorDialogCallback()

#define winSetHInstance(h) 
#define winGetHInstance() 0

bool checkForRequiredClientFiles(void);

#else

#include "wininclude.h"
#include "multiMon.h"

// Use with setSafeCloseAction().
typedef void (*SafeCloseActionType)(DWORD fdwCtrlType);

// Helper function to align all of the elements in a dialog.
// Call once with the initial width and heigh, and then after that
//  call it with the new width/height (from WM_SIZE), and an ID of two controls:
//		idAlignMe:	Everything to the right of the left of this control will translate horizontally upon resize
//					Everything below the top of this control will translate vertically upon resize
//		idUpperLeft:	Everything whose top aligns with the top of this control will stretch vertically upon resize
//						Everything whose left aligns with the left of this control will stretch horizontally upon resize
void doDialogOnResize(HWND hDlg, WORD w, WORD h, int idAlignMe, int idUpperLeft);
void setDialogMinSize(HWND hDlg, WORD minw, WORD minh);

int NumLines(char *text);
int LongestWord(char *text); 
void OffsetWindow(HWND hDlg, HWND hWnd, int xdelta, int ydelta);

void resizeControl(HWND hDlgParent, HWND hDlg, int dx, int dy, int dw, int dh);

// Disable the console close button.
void disableConsoleCloseButton(void);

// Use the "safe" Ctrl-C handler that requires Ctrl-C to be pressed three times.
void useSafeCloseHandler(void);

// Set the action that useSafeCloseHandler() takes when Ctrl-C is pressed.  Returns the previous one.
SafeCloseActionType setSafeCloseAction(SafeCloseActionType SafeCloseActionFptr);

void errorDialog(HWND hwnd, char *str, char* title, char* fault, int highlight); // title & fault optional
void msgAlert(HWND hwnd, const char *str);

HICON getIconColoredLetter(char letter, U32 colorRGB);
void setWindowIconColoredLetter(HWND hwnd, char letter, U32 colorRGB);

void winRegisterMe(const char *commandName, const char *extension); // Registers the current executable to handle files of the given extension
void winRegisterMeEx(const char *commandName, const char *extension, const char *params); // Registers the current executable to handle files of the given extension
char *winGetFileName_s(HWND hwnd, const char *fileMask, char *fileName, size_t fileName_size, bool save);

void winSetHInstance(HINSTANCE hInstance);
HINSTANCE winGetHInstance(void);

void winAddToPath(const char *path, int prefix);
bool winExistsInRegPath(const char *path);
bool winExistsInEnvPath(const char *path);


typedef void (*ErrorDialogCallback)(HWND hwnd, char *str, char* title, char* fault, int highlight, void *userdata);
typedef void (*MsgAlertCallback)(HWND hwnd, const char *str);

#if !_XBOX
void errorDialogInternal(HWND hwnd, char *str, char* title, char* fault, int highlight);
#endif


void setErrorDialogCallback(ErrorDialogCallback callback, void *userdata);
void pushErrorDialogCallback(ErrorDialogCallback callback, void *userdata);
void popErrorDialogCallback(void);
void setMsgAlertCallback(MsgAlertCallback callback);

bool isOSShuttingDown(void);

//if you have a dialog box with multiple controls inside it which you want to have proportionally scale
//when the parent scales, you can use a MultiControlScaleManager to simplify that process

//this enum defines the different behaviors that a control can have when the parent resizes...
//a control has one of these for horizontal behavior and one for vertical behavior
typedef enum enumScaleBehavior
{
	SCALE_FULLRESIZE, //the position and size of the window all scale proportionally
	SCALE_MOVE_NORESIZE, //the position scales proportionally, but the size is fixed
	
	SCALE_LOCK_LEFT, //the size remains the same, and the offset from the top/left remains fixed
	SCALE_LOCK_TOP,

	SCALE_LOCK_RIGHT, //the size remains the same, and the offset from the bottom/right remains fixed
	SCALE_LOCK_BOTTOM,

	SCALE_LOCK_BOTH, //the offset from all sides remains fixed, so the size changes
} enumScaleBehavior;

typedef struct MultiControlScaleManager MultiControlScaleManager;

MultiControlScaleManager *BeginMultiControlScaling(HWND hParent);
void ReInitMultiControlScaling(MultiControlScaleManager *pManager, HWND hNewParent);
void MultiControlScaling_AddChild(MultiControlScaleManager *pManager, int iChildID, enumScaleBehavior eHorizBehavior, enumScaleBehavior eVertBehavior);
void MultiControlScaling_Update(MultiControlScaleManager *pManager);


//thes function is a big hack written by someone who doesn't know what he's doing. It's full of fixed size buffers
//and doesn't work with newlines. If we need a better version, it should be rewritten from scratch
void stringToJpeg(char *pInString, char *pFileName);
//returns a malloced buffer of U8s
SA_RET_OP_VALID U8* stringToBuffer(char *pInString, int *pOutXSize, int *pOutYSize);

// Fixes \ns to be \r\n pairs as needed
void SetWindowTextCleanedup(HWND hWnd, const char *text);

bool checkForRequiredClientFiles(void);

// Must call this before creating the console, or it will not take effect until next time.
// Palette entries are in the format 0x00bbggrr
// Does not work if we're running in the context of someone else's console (e.g. run cmd.exe, then run a console app from within there)
// Default palette:
// U32 palette[16] = {
// 	0x000000,
// 	0x800000,
// 	0x008000,
// 	0x808000,
// 	0x000080,
// 	0x800080,
// 	0x008080,
// 	0xc0c0c0,
// 	0x808080,
// 	0xff0000,
// 	0x00ff00,
// 	0xffff00,
// 	0x0000ff,
// 	0xff00ff,
// 	0x00ffff,
// 	0xffffff,
// };

void consoleSetPalette(U32 palette[16]);

bool SetTextFast(HWND hWnd, const char *text);
bool SetTextFastf(HWND hWnd, FORMAT_STR const char *text, ...);

void estrGetWindowText(char **ppEstr, HWND hWnd);

#endif


typedef enum CheckWhen
{
	CheckWhen_Startup,
	CheckWhen_Shutdown,
	CheckWhen_RunTimeInactive,
	CheckWhen_RunTimeActive,
} CheckWhen;
void winCheckAccessibilityShortcuts(CheckWhen when);
void winAboutToExitMaybeCrashed(void);


/*-----------------------------------------------------
Dialog scrolling code from here:
http://www.codeproject.com/KB/dialog/scroll_dialog.aspx
-------------------------------------------------*/

//NOTE NOTE NOTE: due to the static s_prevX and s_prevY variables, this
//can only be used for one window at a time... if you want two windows open both
//scrolling, you'll have to make it a bit more sophisticated, presumably via
//sticking those values into SimpleWindowManager window structs
BOOL SD_OnInitDialog(HWND hwnd);
void SD_OnSize(HWND hwnd, UINT state, int cx, int cy);
void SD_OnHScroll(HWND hwnd, UINT code);
void SD_OnVScroll(HWND hwnd, UINT code);
BOOL SD_OnInitDialog_ForceSize(HWND hwnd, int xSize, int ySize);


//adds the given directory to the path environment variable
//NOTE NOTE NOTE this is not heavily tested
void AddDirToPathEnivronmentVariable(const char *pDirName);

// Get the Windows Thread Information Block for the current thread, fs:[0] on x86.
void *GetCurrentThreadTib(void);

// Get a value that bounds all lower frames on the stack.
void *GetBoundingFramePointer(void);

//when someone presses CTRL-C thrice to kill a "safe" app, call this. If it returns true, then
//do NOT shut down
LATELINK;
bool TripleControlCOverride(void);

//finds any/all windows associated with a given PID, sets their console titles (may only
//work dependably on Windows 7, and only if the process is one that you yourself created)
void SetWindowTitleByPID(U32 iPID, char *pText);

HANDLE CreateFileMappingSafe( DWORD lpProtect, int size, const char* handleName, int silent);
HANDLE OpenFileMappingSafe(DWORD dwDesiredAccess, bool bInheritHandle, const char* handleName, int silent );
LPVOID MapViewOfFileExSafe(HANDLE handle, const char* handleName, void* desiredAddress, int silent );
DWORD WaitForEventInfiniteSafe(HANDLE hEvent);

// A wrapper to allow using GetWorkingSetSizeEx on XP.
// Note: Flags will always be returned as 0.
BOOL compatibleGetProcessWorkingSetSizeEx(__in HANDLE hProcess,
	__out  PSIZE_T lpMinimumWorkingSetSize,
	__out  PSIZE_T lpMaximumWorkingSetSize,
	__out  PDWORD Flags);
