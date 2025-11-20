#pragma once
BOOL MCPAccountInfoDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow);

bool MCPAccountInfoTickFunc(SimpleWindow *pWindow);



void AccountFSM_Reset(void);
void AccountFSM_Update(void);
void AccountFSM_BeginContact(void);


typedef enum
{
	ACCOUNTSERVERSTATE_NONE,
	ACCOUNTSERVERSTATE_LINKCREATED,
	ACCOUNTSERVERSTATE_PACKET_SENT,
	ACCOUNTSERVERSTATE_SUCCEEDED,
	ACCOUNTSERVERSTATE_FAILED,
	ACCOUNTSERVERSTATE_PROCEEDINGWITHOUTAUTH,
} enumAccountServerState;
extern enumAccountServerState sAccountState;
