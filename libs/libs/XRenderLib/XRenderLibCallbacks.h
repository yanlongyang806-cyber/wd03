#ifndef _XRENDERLIBCALLBACKS_H_
#define _XRENDERLIBCALLBACKS_H_

#include "xdevice.h"

#if !_PS3
#define IGNORE_CALLBACK_RETURN	0xdeadbeef

typedef LRESULT (*MainWndProc_Callback)(RdrDeviceDX *pDevice, HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void Set_MainWndProc_Callback( MainWndProc_Callback pCallbackRtn );

typedef void (*rxbxCreateDirect_Callback)(RdrDeviceDX *pDevice);
void Set_rxbxCreateDirect_Callback( rxbxCreateDirect_Callback pCallbackRtn );

typedef void (*rxbxPresentDirect_Callback)(RdrDeviceDX *pDevice);
void Set_rxbxPresentDirect_Callback( rxbxPresentDirect_Callback pCallbackRtn );
#endif

#endif //_XRENDERLIBCALLBACKS_H_


