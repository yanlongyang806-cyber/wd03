#pragma once

#include "globaltypes.h"
#include "net/net.h"



#include "process_util.h"
#include "resource_CrypticLauncher.h"


#include "SimpleWindowManager.h"
#include "..\..\core\NewControllerTracker\pub\NewControllerTracker_pub.h"


typedef struct CrypticLauncherWindow CrypticLauncherWindow;


//returns true if the text changed
bool SetTextFast(HWND hWnd, const char *text);



//prototypes for dlg and tick funcs for all window types

bool MCPStartTickFunc(SimpleWindow *pWindow);
BOOL MCPStartDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);
BOOL MCPPreDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);




//a little struct which is for a simple FSM of the status of attempting to connect
//to the controller tracker
typedef struct ControllerTrackerConnectionStatusStruct
{
	U32 iOverallBeginTime; //if 0, then this is our first attempt, or our first attempt since connection failed
	U32 iCurBeginTime; //every 30 seconds, we kill our current link and attempt to connect again
} ControllerTrackerConnectionStatusStruct;

//returns true if connected
bool UpdateControllerTrackerConnection(ControllerTrackerConnectionStatusStruct *pStatus, char **ppResultEString);

// Set or reset the launcher's base URL.
void SetLauncherBaseUrl(CrypticLauncherWindow *launcher);
