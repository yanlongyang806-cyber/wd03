#ifndef _XBOX
#pragma once

#include "wininclude.h"

// OkToAll functions return this if OkToAll was selected
#define IDOKTOALL 3

// IDs for the various dialog elements, for use in the generic dialog callback
#define IDD_MB_OKTOALL                  105
#define IDD_DLG_REQUEST_STRING          106
#define IDC_OKTOALL                     1029
#define IDC_OKTOALL_EDIT                1030
#define IDC_EDIT_REQUEST_STRING         1033
#define IDC_EDIT_INPUT_STRING           1034
#define IDC_OKTOALLCANCEL_EDIT			1035


// handles extended functionality for generic dialog boxes
typedef int(*GenericDialogCallback)(HWND, UINT, WPARAM, LPARAM); 

// returns IDOKTOALL if they hit OKTOALL, IDOK if they just hit OK, and IDCANCEL if they hit cancel
int okToAllCancelDialog(char *dialogText, char *caption);
int okToAllCancelDialogEx(char *dialogText, char *caption, GenericDialogCallback callback_proc);

// returns IDOKTOALL if they hit OKTOALL, or IDOK if they just hit OK
int okToAllDialog(char *dialogText, char *caption);
int okToAllDialogEx(char *dialogText, char *caption, GenericDialogCallback callback_proc);

// returns the input string of the dialog box
char *requestStringDialog(char *dialogText, char *caption);
char *requestStringDialogEx(char *dialogText, char *caption, GenericDialogCallback callback_proc);

// flash the toolbar of the given window
void flashWindow(HWND hWnd);

#endif