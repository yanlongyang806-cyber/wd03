#ifndef UI_DIALOG_H
#define UI_DIALOG_H
GCC_SYSTEM

//////////////////////////////////////////////////////////////////////////
// This is a generic widget to throw up a dialog box and, if you want, get
// a canned response (as an integer response ID).
//
// Sample Easy Use:
//   ui_DialogPopup("You can't save this right now; you don't have it checked out.")
//
// Sample Harder Use:
//   UIDialog *pDialog = ui_DialogCreateEx("Verify Delete",
//                     "Are you sure you want to delete this?",
//                     MyDeleteCallback, pMyDeleteData,
//                     "Delete", kUIDialogButton_Yes, 
//                     "Cancel", kUIDialogButton_No,
//                     NULL);
//   ui_WindowShow(UI_WINDOW(pDialog));
// Note especially the NULL at the end to terminate the button list.
// MyDeleteCallback is a function that gets called with the response ID when
// the user finishes interacting with the dialog. The response value might
// also be kUIDialogButton_Close, which means the window was closed.
//
// (If you care about your users, you'll also remember to use labels more
// appropriate than than "OK"/"Cancel" or "Yes"/"No"...)

#include "UIWindow.h"

typedef struct UIDialog UIDialog;

typedef enum
{
	kUIDialogButton_End,
	kUIDialogButton_Close,
	kUIDialogButton_Ok,
	kUIDialogButton_Yes,
	kUIDialogButton_No,
	kUIDialogButton_Cancel,
	kUIDialogButton_Max,
} UIDialogButton;

typedef bool (*UIDialogResponseCallback)(UIDialog *pDialog, UIDialogButton eButton, UserData pResponseData);

typedef struct UIDialog
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE UI_WINDOW_TYPE);
	UIDialogResponseCallback cbResponse;
	UserData pResponseData;
	UIWidget *pExtraWidget;
} UIDialog;

#define ui_DialogPopup(pchTitle, pchText) ui_WindowShow(UI_WINDOW(ui_DialogCreate(pchTitle, pchText, NULL, NULL)))

#define ui_DialogCreate(pchTitle, pchText, cbResponse, pResponseData) ui_DialogCreateEx(pchTitle, pchText, cbResponse, pResponseData, NULL, NULL)

UIDialog *ui_DialogCreateEx(const char *pchTitle, const char *pchText, UIDialogResponseCallback cbResponse, UserData pResponseData, UIWidget *pExtraWidget, ...);
UIDialog *ui_DialogCreateMsgEx(const char *pchTitle, const char *pchText, UIDialogResponseCallback cbResponse, UserData pResponseData, UIWidget *pExtraWidget, ...);

#endif
