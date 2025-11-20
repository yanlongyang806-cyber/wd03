#pragma once
GCC_SYSTEM;

#include "UIGen.h"

#define UI_GEN_MAX_WINDOWS	256

AUTO_STRUCT;
typedef struct UIGenWindowDef {
	const char *pchName; AST(POOL_STRING STRUCTPARAM KEY REQUIRED)

	// If this UIGenWindowDef is clonable, then this variable
	// limits how many clones can be made.  There is a overall
	// cap of 255 possible clones.  If Clones is set to 0, then
	// the window is not clonable.
	//
	// The default is 0.
	S32 iClones;

	// HideInState allows you to specify a list of global gen
	// states that if set, the window will automatically be
	// hidden and will prevent the window from opening.
	//
	// The default is not to be hidden in any state.
	UIGenState *eaiHideStates; AST(NAME(HideInState))

	// RequireInState allows you to specify a list of global gen
	// states that the window requires before it will be opened.  All
	// global states are required.  An empty list will cause it to be
	// allowed in all states.
	//
	// The default is to be allowed in all states.
	UIGenState *eaiRequireStates; AST(NAME(RequireInState))

	// AllowInState allows you to specify a list of global gen states
	// that the window requires before it will be opened.  Just one
	// state is required.  An empty list will cause it to be allowed
	// in all states.
	//
	// The default is to be allowed in all states.
	UIGenState *eaiAllowStates; AST(NAME(AllowInState))

	// If the Modal flag is set, then the window is added to the
	// modal window layer instead of the window layer.
	//
	// The default is 0.
	bool bModal;

	// If the PersistOpen flag is set, then the window manager
	// will remember which windows are open, and will attempt to
	// reopen them when possible.
	//
	// The default is 0.
	bool bPersistOpen;

	// An action that is run when the window is added to the
	// window manager. This will only run once for the lifetime
	// of the window, and it will be run regardless of the
	// window will be displayed or not.
	UIGenAction *pOnWindowAdded;

	// An action that is run when the window is removed from the
	// window manager. This will only run once for the lifetime
	// of the window, and it will be run regardless of the
	// window will be displayed or not.
	UIGenAction *pOnWindowRemoved;

	// The file that this UIGenWindowDef was loaded from.
	const char *pchFilename; AST(CURRENTFILE)
} UIGenWindowDef;

AUTO_STRUCT;
typedef struct UIGenWindow {
	// The WindowDef that this window was added with.
	REF_TO(UIGenWindowDef) hDef;

	// The UIGen that this window represents.
	REF_TO(UIGen) hTemplate;

	// If this window represents a cloned window, then this
	// is the cloned instance of the template.
	UIGen *pInstance;

	// If this window represents a cloned window, then this
	// is the clone number of the window.
	U8 chClone;

	bool bVisible;
	bool bSetPosition;
} UIGenWindow;

typedef struct UIGenWindowManager {
	UI_INHERIT_FROM(UI_WIDGET_TYPE);
	U32 bfStates[UI_GEN_STATE_BITFIELD];
	UIGenWindow **eaActiveWindows;
} UIGenWindowManager;

extern UIGenWindowManager *g_ui_pGenWindowManager;

UIGen *ui_GenWindowGetGen(UIGenWindow *pWindow);
UIGenWindow *ui_GenWindowManagerFindWindow(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen, U8 chClone);
bool ui_GenWindowManagerAddWindow(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow);
bool ui_GenWindowManagerRemoveWindow(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow);

UIGenWindow *ui_GenWindowManagerCreateClonedWindow(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen, U8 chClone);
UIGenWindow *ui_GenWindowManagerCreateWindow(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen);
S32 ui_GenWindowManagerGetNextClone(UIGenWindowManager *pWindowManager, UIGen *pGen);

void ui_GenWindowManagerPersistWindows(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen);
void ui_GenWindowManagerOpenPersisted(UIGenWindowManager *pWindowManager);

UIGenWindow *ui_GenWindowManagerGetWindow(UIGenWindowManager *pWindowManager, UIGen *pGen);

bool ui_GenWindowManagerAdd(UIGenWindowManager *pWindowManager, UIGenWindowDef *pDef, UIGen *pGen, S32 iWindowClone);
bool ui_GenWindowManagerAddPos(UIGenWindowManager *pWindowManager, UIGenWindowDef *pWindowDef, UIGen *pGen, S32 iWindowClone, float fPercentX, float fPercentY);
bool ui_GenWindowManagerRemove(UIGenWindowManager *pWindowManager, UIGen *pGen, bool bForce);

bool ui_GenWindowManagerIsVisible(UIGenWindowManager *pWindowManager, UIGenWindowDef *pDef);
void ui_GenWindowManagerForceTick();
bool ui_GenWindowManagerShow(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow);
bool ui_GenWindowManagerHide(UIGenWindowManager *pWindowManager, UIGenWindow *pWindow, bool bForce);
void ui_GenWindowManagerOncePerFrameInput(void);

void ui_GenWindowManagerLoad(void);
