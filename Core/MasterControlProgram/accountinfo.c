#include "mastercontrolprogram.h"
#include "estring.h"
#include "timing.h"
#include "accountnet.h"
#include "GlobalComm.h"
#include "winutil.h"
#include "accountInfo.h"
#include "accountnet_h_ast.h"

//if true, then the "login" button will be pressed immediately
bool gbLoginImmediately = false;
AUTO_CMD_INT(gbLoginImmediately, LoginImmediately) ACMD_CMDLINE;


#define LINKCREATED_FAILURETIME 15000
#define PACKETSENT_FAILURETIME 15000
#define FAILURESTATE_RESETTIME 3000



enumAccountServerState sAccountState = ACCOUNTSERVERSTATE_NONE;
NetLink *spAccountLink = NULL;
U64 siTimeEnteredAccountState = 0; //msecs since 2000
static bool sbAccountServerSupportsLoginFailedPacket = false;
char *spAccountStateString = NULL; //estring
char *gpTicket = NULL; //estring
U32 guAccountID = 0;
U32 guTicketID = 0;
static HWND sHWndForAccountScreen = 0;


//if true, then we read our hashed password out of the file and stuck a string made up of all
//question marks into the password and it has not been changed.
bool sbPasswordIsFakeFiller = false;
char sFakeFillerPassword[256] = "";

void AccountFSM_SetState(enumAccountServerState eState, const char *pStateString)
{
	siTimeEnteredAccountState = timeMsecsSince2000();
	if (eState != ACCOUNTSERVERSTATE_NONE)
	{
		estrPrintf(&spAccountStateString, "AccountServer status:\n%s",pStateString);
	}
	sAccountState = eState;
}

void AccountFSM_Fail(const char *pFailureString)
{
	AccountFSM_SetState(ACCOUNTSERVERSTATE_FAILED, pFailureString);
	ShowWindow(GetDlgItem(sHWndForAccountScreen, IDC_PROCEED_WITHOUT_AUTH), SW_SHOW);
}

U64 AccountFSM_TimeInState(void)
{
	return timeMsecsSince2000() - siTimeEnteredAccountState;
}


void AccountFSM_HandleInput(Packet* pak, int cmd, NetLink* link,void *user_data)
{
	switch (cmd)
	{
	case FROM_ACCOUNTSERVER_LOGIN:
		if (sAccountState == ACCOUNTSERVERSTATE_PACKET_SENT)
		{
			estrCopy2(&gpTicket, pktGetStringTemp(pak));
			guAccountID = guTicketID = 0;
			AccountFSM_SetState(ACCOUNTSERVERSTATE_SUCCEEDED, "Account verified");
			accountValidateCloseAccountServerLink();
			spAccountLink = NULL;
		}
		break;
	case FROM_ACCOUNTSERVER_LOGIN_NEW:
		if (sAccountState == ACCOUNTSERVERSTATE_PACKET_SENT)
		{
			guAccountID = pktGetU32(pak);
			guTicketID = pktGetU32(pak);
			estrDestroy(&gpTicket);

			AccountFSM_SetState(ACCOUNTSERVERSTATE_SUCCEEDED, "Account verified");
			accountValidateCloseAccountServerLink();
			spAccountLink = NULL;
		}
		break;
	case FROM_ACCOUNTSERVER_LOGIN_FAILED:
	case FROM_ACCOUNTSERVER_FAILED:
		if (cmd == FROM_ACCOUNTSERVER_FAILED && sbAccountServerSupportsLoginFailedPacket)
			break;
		else
			sbAccountServerSupportsLoginFailedPacket = true;
		if (sAccountState == ACCOUNTSERVERSTATE_PACKET_SENT)
		{
			if (cmd == FROM_ACCOUNTSERVER_FAILED)
				AccountFSM_Fail(pktGetStringTemp(pak));
			else
			{
				LoginFailureCode eCode = pktGetU32(pak);
				AccountFSM_Fail(accountValidatorGetFailureReasonByCode(NULL, eCode));
			}
		}
		break;
	default:
		break;
	}
}

void AccountFSM_Reset(void)
{
	if (spAccountLink)
	{
		accountValidateCloseAccountServerLink();
		spAccountLink = NULL;
	}
	AccountFSM_SetState(ACCOUNTSERVERSTATE_NONE, "");
}

void AccountFSM_Update(void)
{
	switch (sAccountState)
	{
	case ACCOUNTSERVERSTATE_LINKCREATED:
		if (linkConnected(spAccountLink))
		{
			accountValidateStartLoginProcess(gGlobalDynamicSettings.accountName);
			AccountFSM_SetState(ACCOUNTSERVERSTATE_PACKET_SENT, "Attempting to contact AccountSever");
		}
		else if (AccountFSM_TimeInState() > LINKCREATED_FAILURETIME)
		{
			AccountFSM_Fail("Timeout while waiting for AccountServer connection");
		}
		break;

	case ACCOUNTSERVERSTATE_PACKET_SENT:
		if (AccountFSM_TimeInState() > PACKETSENT_FAILURETIME)
		{
			AccountFSM_Fail("Timeout while waiting for AccountServer authentication packet");
		}
		break;

	case ACCOUNTSERVERSTATE_FAILED:
		if (AccountFSM_TimeInState() > FAILURESTATE_RESETTIME)
		{
			AccountFSM_Reset();
		}
		break;
	}
}

void AccountFSM_BeginContact(void)
{	
	AccountValidateData validateData = {0};
	validateData.pPassword = gGlobalDynamicSettings.password;
	validateData.eLoginType = ACCOUNTLOGINTYPE_CrypticAndPW;
	validateData.login_cb = AccountFSM_HandleInput;
	validateData.pLoginField = gGlobalDynamicSettings.accountName;

	if (sAccountState != ACCOUNTSERVERSTATE_NONE)
	{
		return;
	}

	spAccountLink = accountValidateInitializeLinkEx(&validateData);

	if (spAccountLink)
	{
		AccountFSM_SetState(ACCOUNTSERVERSTATE_LINKCREATED, "Contacting AccountServer");
	}
	else
	{
		AccountFSM_Fail("Couldn't even attempt AccountServer connection");
	}

}






bool CheckAccountNameIsValid(char *pAccountName, char **ppErrorMessage)
{
	/*int iLen = (int)strlen(pAccountName);
	int ret;

	if (iLen == 0)
	{
		estrPrintf(ppErrorMessage, "Please enter a username");
		return false;
	}

	if (iLen > 64)
	{
		estrPrintf(ppErrorMessage, "Username too long (max 63 characters)");
		return false;
	}

	if (ret = StringIsValidName(pAccountName))
	{
		if (ret == STRINGERR_CHARACTERS)
			estrPrintf(ppErrorMessage, "Username contains invalid characters");
		else if (ret == STRINGERR_MIN_LENGTH)
			estrPrintf(ppErrorMessage, "Username is too short");
		else if (ret == STRINGERR_MAX_LENGTH)
			estrPrintf(ppErrorMessage, "Username is too long");
		return false;
	}*/

	return true;
}

bool CheckPasswordIsValid(char *pPassword, char **ppErrorMessage)
{
	//int iLen = (int)strlen(pPassword);
	////int i;

	//if (iLen > 64)
	//{
	//	estrPrintf(ppErrorMessage, "Password too long (max 63 characters)");
	//	return false;
	//}

	//if (!(sbPasswordIsFakeFiller && strcmp(pPassword, sFakeFillerPassword) == 0))
	//{
	//	/*for (i=0; i < iLen; i++)
	//	{
	//		if (!isalnum(pPassword[i]))
	//		{
	//			estrPrintf(ppErrorMessage, "Password must consist solely of letters and numbers");
	//			return false;
	//		}
	//	}*/
	//	/*if (!StringIsValidPassword(pPassword))
	//	{
	//		estrPrintf(ppErrorMessage, "Password contains invalid characters");
	//		return false;
	//	}*/
	//}

	return true;
}


BOOL MCPAccountInfoDlgFunc(HWND hDlg, UINT iMsg, WPARAM wParam, LPARAM lParam, SimpleWindow *pWindow)
{

	switch (iMsg)
	{

	case WM_INITDIALOG:
		{

			ShowWindow(GetDlgItem(sHWndForAccountScreen, IDC_PROCEED_WITHOUT_AUTH), SW_SHOW);

			sHWndForAccountScreen = hDlg;
			AccountFSM_Reset();	

			if (gGlobalDynamicSettings.accountName[0])
			{
				SetTextFast(GetDlgItem(hDlg, IDC_USERNAME), gGlobalDynamicSettings.accountName);
			

				if (gGlobalDynamicSettings.iPasswordLength && gGlobalDynamicSettings.iPasswordLength < sizeof(gGlobalDynamicSettings.password) - 1)
				{
					int i;

					assert(sizeof(gGlobalDynamicSettings.password) == sizeof(sFakeFillerPassword));

					for (i=0 ; i < gGlobalDynamicSettings.iPasswordLength; i++)
					{
						sFakeFillerPassword[i] = gGlobalDynamicSettings.password[i] = '?';
					}

					sFakeFillerPassword[i] = gGlobalDynamicSettings.password[i]  = 0;
					sbPasswordIsFakeFiller = true;
					SetTextFast(GetDlgItem(hDlg, IDC_PASSWORD), gGlobalDynamicSettings.password);
				}
			}
			else
			{
#if !STANDALONE
				SetTextFast(GetDlgItem(hDlg, IDC_USERNAME), getHostName());
				SetTextFast(GetDlgItem(hDlg, IDC_PASSWORD), getHostName());
#endif
			}
		}
		break;



	case WM_COMMAND:
		if (LOWORD(wParam) == IDCANCEL)
		{
			pWindow->bCloseRequested = true;
		
			return FALSE;

		}

		if (LOWORD(wParam) == IDC_USERNAME || LOWORD(wParam) == IDC_PASSWORD)
		{
			char userName[256];
			char password[256];
			char *pErrorMessage = NULL;
			bool bValid;

			if (sAccountState != ACCOUNTSERVERSTATE_NONE)
			{
				EnableWindow(GetDlgItem(hDlg, IDOK), false);
				break;
			}

			estrStackCreate(&pErrorMessage);

			GetWindowText(GetDlgItem(hDlg, IDC_USERNAME), userName, sizeof(userName));			
			GetWindowText(GetDlgItem(hDlg, IDC_PASSWORD), password, sizeof(password));

			if (sbPasswordIsFakeFiller && strcmp(password, sFakeFillerPassword) != 0)
			{
				sbPasswordIsFakeFiller = false;
			}


			bValid = CheckAccountNameIsValid(userName, &pErrorMessage);

			if (bValid)
			{
				bValid = CheckPasswordIsValid(password, &pErrorMessage);
			}

			if (bValid)
			{
				EnableWindow(GetDlgItem(hDlg, IDOK), true);
				SetTextFast(GetDlgItem(hDlg, IDC_ACCOUNT_INFO_MESSAGE), "");
			}
			else
			{
				EnableWindow(GetDlgItem(hDlg, IDOK), false);
				SetTextFast(GetDlgItem(hDlg, IDC_ACCOUNT_INFO_MESSAGE), pErrorMessage);
			}

			estrDestroy(&pErrorMessage);

		}
		
		if (LOWORD(wParam) == IDOK)
		{
			GetWindowText(GetDlgItem(hDlg, IDC_USERNAME), gGlobalDynamicSettings.accountName, sizeof(gGlobalDynamicSettings.accountName));			
			GetWindowText(GetDlgItem(hDlg, IDC_PASSWORD), gGlobalDynamicSettings.password, sizeof(gGlobalDynamicSettings.password));
			
			gGlobalDynamicSettings.iPasswordLength = (int)strlen(gGlobalDynamicSettings.password);
			WriteSettingsToFile();

			AccountFSM_BeginContact();


		}

		if (LOWORD(wParam) == IDC_PROCEED_WITHOUT_AUTH)
		{
			AccountFSM_SetState(ACCOUNTSERVERSTATE_PROCEEDINGWITHOUTAUTH, "Proceeding without authentication");
		}
		break;

	}
	
	return FALSE;
}
bool MCPAccountInfoTickFunc(SimpleWindow *pWindow)
{

	if (sAccountState == ACCOUNTSERVERSTATE_SUCCEEDED || sAccountState == ACCOUNTSERVERSTATE_PROCEEDINGWITHOUTAUTH)
	{
		pWindow->bCloseRequested = true;
		SimpleWindowManager_AddOrActivateWindow(MCPWINDOW_STARTING, 0, 
#if STANDALONE
			IDD_MCP_START_STANDALONE, 
#else
			IDD_MCP_START, 
#endif
			true, MCPStartDlgFunc, MCPStartTickFunc, NULL);
	}
	else
	{
		AccountFSM_Update();
		SetTextFast(GetDlgItem(pWindow->hWnd, IDC_APPSERVER_STATUS), spAccountStateString);

		if (sAccountState == ACCOUNTSERVERSTATE_NONE && gbLoginImmediately)
		{
			static bool bOnce = false;

			if (!bOnce)
			{
				bOnce = true;

				GetWindowText(GetDlgItem(pWindow->hWnd, IDC_USERNAME), gGlobalDynamicSettings.accountName, sizeof(gGlobalDynamicSettings.accountName));			
				GetWindowText(GetDlgItem(pWindow->hWnd, IDC_PASSWORD), gGlobalDynamicSettings.password, sizeof(gGlobalDynamicSettings.password));
				
				gGlobalDynamicSettings.iPasswordLength = (int)strlen(gGlobalDynamicSettings.password);
				WriteSettingsToFile();

				if (CheckAccountNameIsValid(gGlobalDynamicSettings.accountName, NULL) && (sbPasswordIsFakeFiller || CheckPasswordIsValid(gGlobalDynamicSettings.password, NULL)))
				{
					AccountFSM_BeginContact();
				}
			}
		}
	}
	return true;
}
