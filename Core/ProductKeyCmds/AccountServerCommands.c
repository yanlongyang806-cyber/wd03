#include "AccountServerCommands.h"
#include "net/net.h"
#include "crypt.h"
#include "sysutil.h"
#include "cmdparse.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "earray.h"
#include "accountnet.h"
#include "accountCommon.h"
#include "textparser.h"
#include "StashTable.h"
#include "timing.h"
#include "error.h"

#include "accountCommon_h_ast.h"

extern CRITICAL_SECTION gCommAccess;
//extern ParseTable parse_AccountInfoStripped[];
static NetLink *spAccountLink = NULL;
static NetLink *spWebAccountLink = NULL;

static bool bReceivedLoginResponse = false;
static U32 maxTime = 0;

void ReceiveLoginResponse(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch(cmd)
	{
	case FROM_ACCOUNTSERVER_FAILED:
		{
		}
	xcase FROM_ACCOUNTSERVER_LOGIN:
		{
		}
	xcase FROM_ACCOUNTSERVER_ACCOUNTINFO:
		{
		}
	}
	bReceivedLoginResponse = true;
}
#define LOADTEST_TIMEOUT 5 // in seconds

int TestAccountLogin(const char *pAccountName, const char *pPassword)
{
	char passwordHash[128] = "";
	U32 start_time, end_time;
	NetLink *link = NULL;

	accountHashPassword(pPassword, passwordHash);

	bReceivedLoginResponse = false;
	start_time = timeSecondsSince2000();
	loadstart_printf("Starting login... ");
	link = accountValidateInitializeLink(pAccountName, passwordHash, ReceiveLoginResponse, 0, 0);
	if (!link || !linkConnectWait(link, 2.0f))
	{
		linkRemove(&link);
		return 1;
	}

	if (!accountValidateStartLoginProcess(false))
		return 2;

	while (!bReceivedLoginResponse)
	{
		end_time = timeSecondsSince2000();
		if (end_time - start_time > LOADTEST_TIMEOUT)
		{
			linkRemove(&link);
			return 3;
		}
		commMonitor(commDefault());
	}
	loadend_printf("Done.");

	end_time = timeSecondsSince2000();
	maxTime = max(maxTime, end_time-start_time);
	return bReceivedLoginResponse ? -1 : 0;
}

static void ReceiveMsg(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	switch (cmd)
	{
	case FROM_ACCOUNTSERVER_FAILED:
		{
			char *pResponseString = pktGetStringTemp(pkt);
			printf("\nFailure Response: %s\n> ", pResponseString ? pResponseString : "");
		}
	xcase FROM_ACCOUNTSERVER_CREATEDKEYS:
		{
			U32 uCount = pktGetU32(pkt);
			printf("\nCreated %d keys.\n> ", uCount);
		}
	xcase FROM_ACCOUNTSERVER_UNUSEDKEY:
		{
			char *pKey = pktGetStringTemp(pkt);
			printf("\nUnused key: %s\n> ", pKey);
		}
	xcase FROM_ACCOUNTSERVER_KEYACTIVATED:
		{
			printf("\nKey activated\n> ");
		}
	xcase FROM_ACCOUNTSERVER_KEYGROUPCREATED:
		{
			printf("\nKey Group created.\n> ");
		}
	xcase FROM_ACCOUNTSERVER_MODIFIED:
		{
			printf("\nPermissions modified.\n> ");
		}
	default:
		break;
	}
}

static int iAccountServerResponse;
static char *pAccountServerResponse;
static StashTable accountInfoStash = NULL;

#define WAIT_FOR_ACCOUNTSERVER_TIMEOUT 5000 // 5 seconds

AccountInfoStripped * WebInterfaceGetAccountInfo(const char *pAccountName)
{
	AccountInfoStripped *pInfo = NULL;
	if (pAccountName && accountInfoStash && stashFindPointer(accountInfoStash, pAccountName, &pInfo))
	{
		return pInfo;
	}
	return NULL;
}

static void WebInterfaceReceiveMsg(Packet *pkt,int cmd,NetLink* link,void *user_data)
{
	iAccountServerResponse = cmd;
	switch (cmd)
	{
	case FROM_ACCOUNTSERVER_FAILED:
		{
			char *pResponseString = pktGetStringTemp(pkt);
			if (pResponseString)
			{
				pAccountServerResponse = strdup(pResponseString);
				printf("%s\n", pResponseString);
			}
		}
	xcase FROM_ACCOUNTSERVER_LOGIN:
		{
		}
	xcase FROM_ACCOUNTSERVER_ACCOUNTINFO:
		{
			char *pAccountInfoString = pktGetStringTemp(pkt);
			if (pAccountInfoString)
			{
				AccountInfoStripped *pAccountInfo = StructCreate(parse_AccountInfoStripped);
				ParserReadText(pAccountInfoString, parse_AccountInfoStripped, pAccountInfo, 0);
				if (pAccountInfo->accountName[0])
				{
					AccountInfoStripped *pAccountInfoOld = NULL;
					if (!accountInfoStash)
					{
						accountInfoStash = stashTableCreateWithStringKeys(200, StashDefault);
					}
					if (stashRemovePointer(accountInfoStash, pAccountInfo->accountName, &pAccountInfoOld))
					{
						// destroy old info for key
						StructDestroy(parse_AccountInfoStripped, pAccountInfoOld);
					}
					pAccountInfo->uTimeReceived = timeSecondsSince2000();
					stashAddPointer(accountInfoStash, pAccountInfo->accountName, pAccountInfo, true);
				}
				else
				{
					StructDestroy(parse_AccountInfoStripped, pAccountInfo);
				}
			}
		}
	xcase FROM_ACCOUNTSERVER_CREATED:
		{
			printf("Account Created\n");
		}
	xcase FROM_ACCOUNTSERVER_KEYACTIVATED:
		{
			printf("Key Activated\n");
		}
	default:
		break;
	}
}

bool webConnectAccountServer(void)
{
	EnterCriticalSection(&gCommAccess);

	if (!spWebAccountLink || !linkConnected(spWebAccountLink))
	{
		linkRemove(&spWebAccountLink);
		spWebAccountLink = commConnect(commDefault(), LINK_NO_COMPRESS|LINK_FORCE_FLUSH, getAccountServer(), 
			DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, WebInterfaceReceiveMsg, 0, 0, 0);
	}
	if (!linkConnectWait(spWebAccountLink,2.f))
	{
		linkRemove(&spWebAccountLink);
		return false;
	}
	return true;
}

void webDisconnectAccountServer(void)
{
	linkRemove(&spWebAccountLink);
	LeaveCriticalSection(&gCommAccess);
}

char * confirmLogin(const char *pAccountName, const char *pPassword)
{
	Packet *pkt;
	DWORD start_tick;

	if (!webConnectAccountServer())
		return "Could not connect to Account Server";

	iAccountServerResponse = 0;
	if (pAccountServerResponse)
	{
		free(pAccountServerResponse);
		pAccountServerResponse = NULL;
	}
	pkt = pktCreate(spWebAccountLink, TO_ACCOUNTSERVER_LOGIN_CONFIRM);
	pktSendString(pkt, pAccountName);
	pktSendString(pkt, pPassword);
	pktSend(&pkt);

	start_tick = GetTickCount();
	while (iAccountServerResponse == 0)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_ACCOUNTSERVER_TIMEOUT))
		{
			return "Timed out";
		}

		Sleep(1);
		commMonitor(commDefault());
	}
	if (iAccountServerResponse == FROM_ACCOUNTSERVER_FAILED)
	{
		return pAccountServerResponse ? pAccountServerResponse : "Unspecified error in logging in";
	}

	webDisconnectAccountServer();
	return NULL;
}

char * assignKeyWithPassword(const char *pAccountName, const char *pPassword, const char *pKey, bool bNewAccount)
{
	Packet *pkt;
	DWORD start_tick;

	if (!webConnectAccountServer())
	{
		return "Could not connect to Account Server";
	}

	// Clear response values
	iAccountServerResponse = 0;
	if (pAccountServerResponse)
	{
		free(pAccountServerResponse);
		pAccountServerResponse = NULL;
	}

	// Send validation
	if (bNewAccount)
	{
		pkt = pktCreate(spWebAccountLink, TO_ACCOUNTSERVER_CREATEACCOUNT);
	}
	else
	{
		pkt = pktCreate(spWebAccountLink, TO_ACCOUNTSERVER_LOGIN_CONFIRM);
	}
	pktSendString(pkt, pAccountName);
	pktSendString(pkt, pPassword);
	pktSend(&pkt);

	start_tick = GetTickCount();
	while (iAccountServerResponse == 0)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_ACCOUNTSERVER_TIMEOUT))
		{
			return "Timed out";
		}

		Sleep(1);
		commMonitor(commDefault());
	}

	if (iAccountServerResponse == FROM_ACCOUNTSERVER_FAILED)
	{
		return pAccountServerResponse ? pAccountServerResponse : "Unspecified error in authenticating";
	}
	if (bNewAccount && iAccountServerResponse != FROM_ACCOUNTSERVER_CREATED || 
		!bNewAccount && iAccountServerResponse != FROM_ACCOUNTSERVER_ACCOUNTINFO)
	{
		return "Async error";
	}

	if (!pKey || !pKey[0])
	{
		if (bNewAccount)
		{
			return "Account created.";
		}
		return "No key!";
	}

	iAccountServerResponse = 0;
	assert (pAccountServerResponse == NULL);
	// should never need to free pAccountServerResponse here
	pkt = pktCreate(spWebAccountLink, TO_ACCOUNTSERVER_ACTIVATEKEY);
	pktSendString(pkt, pAccountName);
	pktSendString(pkt, pKey);
	pktSend(&pkt);

	start_tick = GetTickCount();
	while (iAccountServerResponse == 0)
	{
		DWORD tick = GetTickCount();
		if(tick > (start_tick + WAIT_FOR_ACCOUNTSERVER_TIMEOUT))
		{
			return "Timed out";
		}

		Sleep(1);
		commMonitor(commDefault());
	}
	if (iAccountServerResponse == FROM_ACCOUNTSERVER_FAILED)
	{
		return pAccountServerResponse ? pAccountServerResponse : "Unspecified error in activating key";
	}

	webDisconnectAccountServer();
	return "Key activation successful!";
}

static bool EnterConnection(void)
{
	EnterCriticalSection(&gCommAccess);

	if (!spAccountLink || !linkConnected(spAccountLink))
	{
		linkRemove(&spAccountLink);
		spAccountLink = commConnect(commDefault(), LINK_NO_COMPRESS|LINK_FORCE_FLUSH, getAccountServer(),
			DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, ReceiveMsg, 0, 0, 0);
	}
	if (!linkConnectWait(spAccountLink,2.f))
	{
		linkRemove(&spAccountLink);
		printf("No connection!\n");
		return false;
	}
	return true;
}

static void LeaveConnection(void)
{
	LeaveCriticalSection(&gCommAccess);
}

AUTO_COMMAND ACMD_CATEGORY(ProductKey);
void createKeyGroup (const char * prefix, const char * productName, const char * defaultPermissionString, int defaultAccessLevel)
{
	Packet *pkt;
	if (strlen(prefix) > 5)
	{
		printf("Product prefixes are no more than 5 characters long!\n");
		return;
	}

	if (!EnterConnection())
	{
		LeaveConnection();
		return;
	}

	pkt = pktCreate(spAccountLink, TO_ACCOUNTSERVER_CREATEKEYGROUP);
	pktSendString(pkt, prefix);
	pktSendString(pkt, productName);
	pktSendString(pkt, defaultPermissionString);
	pktSendU32(pkt, defaultAccessLevel);
	pktSend(&pkt);

	LeaveConnection();
}

// Attempt to create a specified number of new product keys
AUTO_COMMAND ACMD_CATEGORY(ProductKey);
void createKeys(const char * prefix, int keyCount)
{
	Packet *pkt;

	if (strlen(prefix) > 5)
	{
		printf("Product prefixes are no more than 5 characters long!\n");
		return;
	}

	if (!EnterConnection())
	{
		LeaveConnection();
		return;
	}

	pkt = pktCreate(spAccountLink, TO_ACCOUNTSERVER_GENERATEKEYS);
	pktSendString(pkt, prefix);
	pktSendU32(pkt, (U32) keyCount);
	pktSend(&pkt);

	LeaveConnection();
}

// Get a product key that has not been registered with any account
AUTO_COMMAND ACMD_NAME(getKey) ACMD_CATEGORY(ProductKey);
void getUnassignedKey(const char * prefix)
{
	Packet *pkt;
	
	if (strlen(prefix) > 5)
	{
		printf("Product prefixes are no more than 5 characters long!\n");
		return;
	}
	
	if (!EnterConnection())
	{
		LeaveConnection();
		return;
	}

	pkt = pktCreate(spAccountLink, TO_ACCOUNTSERVER_GETUNUSEDKEY);
	pktSendString(pkt, prefix);
	pktSend(&pkt);

	LeaveConnection();
}

// Assign an available product key to the specified account
AUTO_COMMAND ACMD_CATEGORY(ProductKey);
void assignKey(char *pAccountName, char *pKey)
{
	Packet *pkt;
	if (!EnterConnection())
	{
		LeaveConnection();
		return;
	}

	pkt = pktCreate(spAccountLink, TO_ACCOUNTSERVER_ACTIVATEKEY);
	pktSendString(pkt, pAccountName);
	pktSendString(pkt, pKey);
	pktSend(&pkt);

	LeaveConnection();
}

AUTO_COMMAND ACMD_CATEGORY(ProductKey);
void assignPermissions (char *pAccountName, char *pProductName, char *pPermissionString, int accessLevel)
{
	Packet *pkt;
	if (!EnterConnection())
	{
		LeaveConnection();
		return;
	}

	pkt = pktCreate(spAccountLink, TO_ACCOUNTSERVER_GIVEPERMISSIONS);
	pktSendString(pkt, pAccountName);
	pktSendString(pkt, pProductName);
	pktSendString(pkt, pPermissionString);
	pktSendU32(pkt, accessLevel);
	pktSend(&pkt);

	LeaveConnection();
}

// Print usage for commands
AUTO_COMMAND ACMD_NAME(help) ACMD_CATEGORY(ProductKey);
void printCmd (char *cmdString)
{
	char buffer[1024] = ""; // for storing cmd print usage info

	if (cmdString && cmdString[0])
	{
		Cmd *cmd = cmdListFind(&gGlobalCmdList, cmdString);
		if (cmd)
		{
			cmdPrintUsage(cmd, buffer, ARRAY_SIZE_CHECKED(buffer));
			printf("%s\n", buffer);
		}
		else
		{
			printf("Invalid command.\n");
		}
	}
	else
	{
		printf("Invalid command.\n");
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(help);
void printAllCmds(void)
{
	int i;
	char buffer[1024] = ""; // for storing cmd print usage info

	for (i=0; i<eaSize(&gGlobalCmdList.cmds); i++)
	{
		if (gGlobalCmdList.cmds[i]->categories && strstri(gGlobalCmdList.cmds[i]->categories, "ProductKey"))
		{
			cmdPrintUsage(gGlobalCmdList.cmds[i], buffer, ARRAY_SIZE_CHECKED(buffer));
			printf("\n%s\n", buffer);
		}
	}
}

// Reset connection to Account Server
AUTO_COMMAND ACMD_CATEGORY(ProductKey);
void reconnect(void)
{
	EnterCriticalSection(&gCommAccess);

	linkRemove(&spAccountLink);
	spAccountLink = commConnect(commDefault(), LINK_NO_COMPRESS|LINK_FORCE_FLUSH, getAccountServer(),
		DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, ReceiveMsg, 0, 0, 0);

	if (!linkConnectWait(spAccountLink,2.f))
	{
		linkRemove(&spAccountLink);
		printf("No connection!\n");
	}
	else
	{
		printf("Connected to Account Server\n");
	}
	LeaveCriticalSection(&gCommAccess);
}

AUTO_COMMAND ACMD_CATEGORY(ProductKey);
void testMD5Hash(char * password)
{
	char md5Hash[128] = "";
	printf("MD5 Hash: %s\n", accountMD5HashPassword(password, md5Hash));
}
