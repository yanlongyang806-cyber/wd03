/***************************************************************************



***************************************************************************/

#ifndef UI_MODALDIALOG_H
#define UI_MODALDIALOG_H
GCC_SYSTEM

#include "UICore.h"
#include "UIFileBrowser.h"

//////////////////////////////////////////////////////////////////////////
// This is a totally modal dialog -- it not only stops the UI, but the entire
// client main loop. If you just want to halt the UI, use a regular UIDialog,
// found in UIDialog.c/UIDialog.h.

typedef enum
{
	UIOk		= 1<<0,
	UIYes		= 1<<1,
	UINo		= 1<<2,
	UICancel	= 1<<3,
	UIEditFile	= 1<<4,
	UICopyToClipboard	= 1<<5,
	UICustomButton1 = 1<<6,
	UICustomButton2 = 1<<7,
	UICustomButton3 = 1<<8,
	UIOpenFolder	= 1<<9,
} UIDialogButtons;

typedef enum UIBrowserMode;
typedef enum UIBrowserType;
typedef void (*UIFilterFunc) (char*, int, const char*, UserData);

void ui_ModalDialogSetCustomButtons(SA_PARAM_OP_STR const char *button1, SA_PARAM_OP_STR const char *button2, SA_PARAM_OP_STR const char *button3);
void ui_ModalDialogSetCustomButton(int index, SA_PARAM_OP_STR const char *button);
void ui_ModalDialogSetCustomSkin(SA_PARAM_OP_VALID UISkin* skin);

UIDialogButtons ui_ModalDialog(SA_PARAM_NN_STR const char *title, SA_PARAM_NN_STR const char *message,
							   Color text_color, UIDialogButtons buttons);

UIDialogButtons ui_ModalFileBrowser(SA_PARAM_NN_STR const char *title,
									SA_PARAM_NN_STR const char *buttonText,
									UIBrowserMode mode,
									UIBrowserType type, 
									bool excludePrivateDirs,
									SA_PARAM_NN_STR const char *topDir,
									SA_PARAM_OP_STR const char *startDir,
									SA_PARAM_OP_STR const char *defaultExt,
									SA_PRE_VALID SA_POST_OP_STR char *dirOut, int dirOutLen,
									SA_PRE_VALID SA_POST_OP_STR char *fileOut, int fileOutLen, char *defaultText);
UIDialogButtons ui_ModalFileBrowserEx(
        SA_PARAM_NN_STR const char *title, SA_PARAM_NN_STR const char *buttonText,
        UIBrowserMode mode, UIBrowserType type, bool excludePrivateDirs,
		SA_PARAM_NN_STR const char **topDirs, SA_PARAM_OP_STR const char *startDir, SA_PARAM_OP_STR const char **defaultExts,
        SA_PRE_VALID SA_POST_OP_STR char *dirOut, int dirOutLen, SA_PRE_VALID SA_POST_OP_STR char *fileOut,
        int fileOutLen,
        UIFilterFunc filterF, UserData filterD, char *defaultText );

void ui_ModalShutdownEffect(void);

// For creating your own modal dialogs!
void ui_ModalDialogBeforeWidgetAdd(UIGlobalState* state);
void ui_ModalDialogLoop(void);
void ui_ModalDialogAfterWidgetDestroy(UIGlobalState* state);
void ui_ModalDialogLoopExit(void);

#endif // UI_MODALDIALOG_H

