/***************************************************************************



***************************************************************************/

/* This file contains the methods and data needed to set up and use the input library */

#ifndef _INPUTLIB_H
#define _INPUTLIB_H
GCC_SYSTEM

#include "inputData.h"
#include "windefinclude.h"

typedef struct RdrDevice RdrDevice;
typedef struct ExprContext ExprContext;

// Creates an input device that is bound to the given render device and instance, using the
// given callback functions. This also does input startup.
InputDevice *inpCreateInputDevice(SA_PARAM_NN_VALID RdrDevice *rdr, HINSTANCE hInstance, KeyBindExec bind, bool bUnicodeRendererWindow);
// Destroy an input device
void inpDestroyInputDevice(InputDevice *inp);

// Does all the per-frame input update handling. This needs to be done in two steps,
// with some UI running in between. This is because the UI needs to be able to change
// the keybind stack based on e.g. mouse input, and have that reflected immediately.
// If keybinds run before the UI, you get clickthrough; if the rest of input runs
// after the UI, you get a frame of input delay.
void inpUpdateEarly(InputDevice *inp);
void inpUpdateLate(InputDevice *inp);

// Clears the keyboard buffer
void inpClearBuffer(InputDevice *inp);
// Clears the current input state
void inpClear(void);
bool inpIsIgnoringInput();
void inpBeginIgnoringInput();
void inpStopIgnoringInput();

// Gets and sets the active input device
InputDevice *inpGetActive(void);
void inpSetActive(InputDevice *inp);

// Mark input for this frame as "handled" -- future input checks this
// frame will return false.
extern int g_inputTrace;
void inpHandledEx(const char* name, const char* file, int line, const char* reason);
#define inpHandled() inpHandledEx(NULL, __FILE__, __LINE__, "manualCall")
void inpScrollHandled(void);
bool inpCheckHandled(void);
bool inpCheckScrollHandled(void);

// Does this window currently have focus?
int inpFocus(void);

void inpDisableAltTab(void);
void inpEnableAltTab(void);

// Various input querying functions
int inpLevel(int idx);
int inpEdge(int idx);
int inpLevelPeek(int idx);
int inpEdgePeek(int idx);

// Capture a key so nobody else can use it this frame
void inpCapture(int idx);
bool inpIsCaptured(int idx);
// Returns rather something changed with the given key this turn
bool inpEdgeThisFrame(void);
// Did anything change this frame?
void inpClearEdge( int idx );
// Remove this id from the buffer


// inpIsInactiveApp: inaccurate if there are multiple threads with Win32 windows, use gfxIsInactiveApp instead
bool inpIsInactiveApp(InputDevice *inp);
bool inpIsInactiveWindow(InputDevice *inp);

typedef void (*ConsoleInputProcessor)(void);
void inpSetConsoleInputProcessor(ConsoleInputProcessor processor);

bool inpHasKeyboard(void);
bool inpHasGamepad(void);

//#define INPUT_EXPR_TAG "Input"

// Call this every frame to update MouseX/Y/Z variables in an ExprContext.
// Call it once with bAddConstVariables to add many variables (all key names)
// to your context.
void inpUpdateExpressionContext(ExprContext *pContext, bool bAddConstVariables);

U32 inpDeltaTimeToLastInputEdge(void);
U32 inpLastInputEdgeTime(void);

// Return true if the mouse moved, a key was pressed, or one of the
// joystick analog controls is in a non-neutral position.
bool inpDidAnything(void);

bool ClientWindowIsBeingMovedOrResized();

void inpGetDeviceScreenSize(int * size);

#endif
