#include "AccountServer.h"

#include "AccountLoginDecrypt.h"
#include "AccountEncryption.h"
#include "AccountIntegration.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "AccountReporting.h"
#include "AccountTransactionLog.h"
#include "AccountServerConfig.h"
#include "AccountServer_c_ast.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "AutoTransDefs.h"
#include "billing.h"
#include "crypt.h"
#include "Discount.h"
#include "fileutil.h"
#include "GcsInterface.h"
#include "GlobalComm.h"
#include "GlobalComm_h_ast.h"
#include "HashFunctions.h"
#include "hoglib.h"
#include "HttpLib.h"
#include "InternalSubs.h"
#include "KeyValues/KeyValueChain.h"
#include "KeyValues/KeyValues.h"
#include "KeyValues/VirtualCurrency.h"
#include "logcomm.h"
#include "mathutil.h"
#include "netipfilter.h"
#include "net/net.h"
#include "objBackupCache.h"
#include "objContainerIO.h"
#include "objMerger.h"
#include "objTransactionCommands.h"
#include "objTransactions.h"
#include "Organization.h"
#include "ProxyInterface/AccountProxy.h"
#include "qsortG.h"
#include "ServerLib.h"
#include "sock.h"
#include "StayUp.h"
#include "Steam.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "structNet.h"
#include "Subscription.h"
#include "SubscriptionHistory.h"
#include "sysutil.h"
#include "ThreadSafeMemoryPool.h"
#include "timing_profiler_interface.h"
#include "utilitiesLib.h"
#include "ValueLockList.h"
#include "WebInterface.h"
#include "wiCommon.h"
#include "WorkerThread.h"
#include "XMLInterface.h"
#include "XMLRPC.h"

#include "AutoGen/AccountIntegration_h_ast.h"
#include "AutoGen/RateLimit_h_ast.h"

static U32 gLastDefragDay = 0; // The last timestamp on which we performed a defrag
static bool gLastMergerWasDefrag = 0;

static bool sbForceLikeLive = false;
AUTO_CMD_INT(sbForceLikeLive, ForceLikeLive) ACMD_CMDLINE;

bool gAppendSnapshot = false;
AUTO_CMD_INT(gAppendSnapshot, AppendSnapshot) ACMD_CMDLINE;

bool gDefragAfterMerger = false;
AUTO_CMD_INT(gDefragAfterMerger, DefragAfterMerger) ACMD_CMDLINE;

static U32 gMergerLaunchFailuresBeforeAlert_Normal = 2;
AUTO_CMD_INT(gMergerLaunchFailuresBeforeAlert_Normal, MergerLaunchFailuresBeforeAlert_Normal) ACMD_CMDLINE;

static U32 gMergerLaunchFailuresBeforeAlert_Defrag = 5;
AUTO_CMD_INT(gMergerLaunchFailuresBeforeAlert_Defrag, MergerLaunchFailuresBeforeAlert_Defrag) ACMD_CMDLINE;

int giWebInterfacePort = 80;
AUTO_CMD_INT(giWebInterfacePort, httpPort) ACMD_CMDLINE;

bool gbNewWebInterface = true;
AUTO_CMD_INT(gbNewWebInterface, NewWebInterface) ACMD_CMDLINE;

char gAccountDataPath[MAX_PATH] = "server/AccountServer/";
AUTO_CMD_STRING(gAccountDataPath, DataPath) ACMD_CMDLINE;

char gAccountWebDirPath[MAX_PATH] = "server/AccountServer/WebRoot/";
AUTO_CMD_STRING(gAccountWebDirPath, WebDirectory) ACMD_CMDLINE;

int giLauncherPid = 0;
AUTO_CMD_INT(giLauncherPid, LauncherPID) ACMD_CMDLINE;

// Whether or not to prevent subscription creation while the user has a pending PayPal sub
static bool gbBlockOnPending = false;
AUTO_CMD_INT(gbBlockOnPending, BlockOnPending) ACMD_CMDLINE ACMD_CATEGORY(Account_Server_Billing);
 
// Wether or not accounts will be created automatically if they don't already exist
bool gbDisableAutoCreateLocal = true;
AUTO_CMD_INT(gbDisableAutoCreateLocal, DisableAutoCreate) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static bool gbRegenerateGUIDs = false;
AUTO_CMD_INT(gbRegenerateGUIDs, RegenerateGUIDs) ACMD_CMDLINE;

static bool gbShowDBSpam = true;
AUTO_CMD_INT(gbShowDBSpam, ShowDBSpam) ACMD_CMDLINE;


//when BGB shards are run, the local account server talks to the controller, so that it will
//know to kill itself when the controller goes away
bool gbConnectToController = false;
AUTO_CMD_INT(gbConnectToController, ConnectToController) ACMD_CMDLINE;

// Client timeout (in seconds)
static int giClientTimeout = 60*3; // Seconds
AUTO_CMD_INT(giClientTimeout, ClientTimeout) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// Fast snapshot
#define OPT_FASTSNAPSHOT_DEFAULT true
static bool gbFastSnapshot = OPT_FASTSNAPSHOT_DEFAULT;
AUTO_CMD_INT(gbFastSnapshot, FastSnapshot) ACMD_CMDLINE;

// Force snapshot
#define OPT_FORCEHOGGSNAPSHOT_DEFAULT false
static bool gbForceHoggSnapshot = OPT_FORCEHOGGSNAPSHOT_DEFAULT;
AUTO_CMD_INT(gbForceHoggSnapshot, ForceHoggSnapshot) ACMD_CMDLINE;

// Suppress merger.
#define OPT_NOMERGER_DEFAULT false
static bool gbNoMerger = OPT_NOMERGER_DEFAULT;
AUTO_CMD_INT(gbNoMerger, NoMerger);

// Whether accounts whose machine-locking enable status is not initialized are treated as locked or unlocked
bool gbMachineLockEnableDefault = false;
AUTO_CMD_INT(gbMachineLockEnableDefault, MachineLockDefault) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// Disable all machine-locking checks and treat all accounts as unlocked
bool gbMachineLockDisable = false;
AUTO_CMD_INT(gbMachineLockDisable, MachineLockDisable) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

bool gbMachineLockResetToUnknown = false;
AUTO_CMD_INT(gbMachineLockResetToUnknown, MachineLockResetToUnknown) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

bool gbDisallowCrypticAndPwLoginType = false;
AUTO_CMD_INT(gbDisallowCrypticAndPwLoginType, DisallowCrypticAndPwLoginType);

// Activate high-performance logging, which is dangerous because it can result in data loss on an Account Server crash
bool gbDangerousHighPerformanceLogMode = false;
AUTO_CMD_INT(gbDangerousHighPerformanceLogMode, DangerousHighPerformanceLogMode) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

extern char gUpdateSchemas[MAX_PATH];

static void dbEnableDBLogCacheHint(void);
static void dbDisableDBLogCacheHint(void);


/************************************************************************/
/* Account server access levels                                         */
/************************************************************************/

struct {
	AccountServerAccessLevel eLevel;
	U32 uPermissions;
} gASAccessMap[] = {
	// Level ---------- Permissions
	{ASAL_SuperAdmin,	ASAL_KEY_ACCESS|ASAL_GRANT_ACCESS},
	{ASAL_Admin,		ASAL_KEY_ACCESS},
	{ASAL_Normal,		0},
	{ASAL_Limited,		0},
};

// Translate a numeric access level into an account server access level
AccountServerAccessLevel ASGetAccessLevel(int iLevel)
{
	switch (iLevel)
	{
	case ASAL_SuperAdmin:
	case ASAL_Admin:
	case ASAL_Normal:
	case ASAL_Limited:
		return iLevel;
	}
	return ASAL_Invalid;
}

// Determine if an access level has the specified permissions
bool ASHasPermissions(AccountServerAccessLevel eAccessLevel, U32 uPermissions)
{
	if (!verify(ASGetAccessLevel(eAccessLevel) == eAccessLevel)) return false;
	if (eAccessLevel == ASAL_Invalid) return false;

	ARRAY_FOREACH_BEGIN(gASAccessMap, i);
	{
		if (gASAccessMap[i].eLevel == eAccessLevel) return (gASAccessMap[i].uPermissions & uPermissions) == uPermissions;
	}
	ARRAY_FOREACH_END;

	return false;
}


/************************************************************************/
/* Account server mode                                                  */
/************************************************************************/

// Do NOT use this variable to check the mode!
static char gsAccountServerModeString[256] = "";
AUTO_CMD_STRING(gsAccountServerModeString, Mode) ACMD_CMDLINE;

AccountServerMode getAccountServerMode(void)
{
	static AccountServerMode mode = ASM_Normal;
	static bool bFoundMode = false;

	if (!bFoundMode)
	{
		if (gUpdateSchemas && *gUpdateSchemas)
			mode = ASM_UpdateSchemas;
		else if (!gsAccountServerModeString || !*gsAccountServerModeString)
			mode = ASM_Normal;
		else
		{
			mode = StaticDefineIntGetInt(AccountServerModeEnum, gsAccountServerModeString);
			if (!devassertmsg(mode >= 0, "Invalid mode!"))
				mode = ASM_Normal;
			if (!devassertmsg(mode != ASM_UpdateSchemas, "Must use the UpdateSchemas command-line option."))
				mode = ASM_Normal;
		}

		bFoundMode = true;
	}

	return mode;
}

bool isAccountServerMode(AccountServerMode mode)
{
	if (getAccountServerMode() == mode) return true;
	return false;
}

// Return true if the Account Server is being run "like" the Live AS, and should therefore be treated as similarly as possible
// For now the criteria are: (1) running outside an MCP (2) in production mode
bool isAccountServerLikeLive(void)
{
	if (sbForceLikeLive)
	{
		return true;
	}

	if (ShardInfoStringWasSet())
	{
		return false;
	}

	if (!isProductionMode())
	{
		return false;
	}
	
	return true;
}

// Return true if this is actually the live Account Server, i.e. billing type "Official"
bool isAccountServerLive(void)
{
	return billingGetServerType() == BillingServerType_Official;
}

//The live AccountServer should not send dumps to ErrorTracker due to them containing customer data.
bool OVERRIDE_LATELINK_shouldSendDumps(void)
{
	return !isAccountServerLive();
}

// ---------------------------------------------------
// Display Name Change request
AUTO_STRUCT;
typedef struct DisplayNameMessageStruct
{
	U32 uAccountID;
	char *displayName;
	NetLink *link; NO_AST
} DisplayNameMessageStruct;

void trChangeNameMessage_CB(TransactionReturnVal *returnVal, DisplayNameMessageStruct *data)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		AccountInfo *account = data && data->displayName ? findAccountByDisplayName(data->displayName) : NULL;
		if (account)
		{
			if (data->link && linkConnected(data->link))
			{
				Packet *pkt = pktCreate(data->link, FROM_ACCOUNTSERVER_MODIFIED);
				pktSend(&pkt);
			}
			sendDisplayNameUpdates(account);
		}
	}
	else
	{
		if (data->link && linkConnected(data->link))
		{
			char *errorString = objAutoTransactionGetResult(returnVal);
			Packet *pkt = pktCreate(data->link, FROM_ACCOUNTSERVER_INVALID_NAME);
			if (errorString && errorString[0])
				pktSendString(pkt, errorString);
			else
				pktSendString(pkt, "Display Name already taken");
			pktSend(&pkt);
		}
	}
	if (data)
		StructDestroy(parse_DisplayNameMessageStruct, data);
}
static DisplayNameMessageStruct **sppDisplayNameChangeQueue = NULL;

// ---------------------------------------------------

const char * dbAccountDataDir(void)
{
	static char path[CRYPTIC_MAX_PATH] = {0};
	if (!path[0]) {
		fileSpecialDir("accountdb", SAFESTR(path));

		if (path[0] == '.')
		{
			char fullPath[CRYPTIC_MAX_PATH] = {0};
			fileGetcwd(SAFESTR(fullPath));
			strcat(fullPath, path);
			makeLongPathName(fullPath, path);
		}

		strcat(path, "/");
	}
	return path;
}

static int AccountServerConnectCallback(NetLink* link,AccountLink *accountLink)
{
	linkSetTimeout(link, giClientTimeout);
	//printf("Client connected.\n");
	accountLink->temporarySalt = 0;
	return 1;
}

static int AccountServerDisconnectCallback(NetLink *link,AccountLink *accountLink)
{
	int i = 0;

	for (i=eaSize(&sppDisplayNameChangeQueue)-1; i>=0; i--)
	{
		if (sppDisplayNameChangeQueue[i]->link == link)
		{
			sppDisplayNameChangeQueue[i]->link = NULL;
		}
	}

	memset(accountLink, 0, sizeof(*accountLink));

	return 1;
}

// ---------------------------------------------------
// Permission Utility Functions

int accountPermissionValueCmp(const AccountPermissionValue **pptr1, const AccountPermissionValue **pptr2)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	int iTypeCmp = stricmp((*pptr1)->pType, (*pptr2)->pType);

	if (iTypeCmp != 0)
		return iTypeCmp;
	return stricmp((*pptr1)->pValue, (*pptr2)->pValue);
}

void accountPermissionValueStringParse(const char *permissionString, char *** eaShards, AccountPermissionValue ***eaValues)
{
	int i, size; 
	char **ppPermissionParts = NULL;

	if (!eaShards && !eaValues)
		return;

	accountPermissionStringParse(permissionString, &ppPermissionParts);

	size = eaSize(&ppPermissionParts);
	for (i=0; i<size; i+=2)
	{
		if (stricmp(ppPermissionParts[i], "shard") == 0)
		{
			if (eaShards)
			{
				char *cur = ppPermissionParts[i+1];
				char *delim;
				while (delim = strchr(cur, ','))
				{
					char *pCopy;
					*delim = 0;

					pCopy = estrCreateFromStr(cur);
					estrTrimLeadingAndTrailingWhitespace(&pCopy);
					if (estrLength(&pCopy))
					{
						if (stricmp(pCopy, "all") == 0)
						{
							eaClearEString(eaShards);
							eaPush(eaShards, pCopy);
							cur = NULL;
							break;
						}
						eaPush(eaShards, pCopy);
					}
					else estrDestroy(&pCopy);

					cur = delim+1;
				}
				if (cur && *cur)
				{
					char *pCopy = estrCreateFromStr(cur);
					estrTrimLeadingAndTrailingWhitespace(&pCopy);
					if (estrLength(&pCopy))
					{
						if (stricmp(pCopy, "all") == 0)
						{
							eaClearEString(eaShards);
						}
						eaPush(eaShards, pCopy);
					}
					else estrDestroy(&pCopy);
				}
			}

			free(ppPermissionParts[i]);
			free(ppPermissionParts[i+1]);
		}
		else
		{
			if (eaValues)
			{
				char *cur = ppPermissionParts[i+1];
				char *delim;
				while (delim = strchr(cur, ','))
				{
					NOCONST(AccountPermissionValue) *pValue = StructCreateNoConst(parse_AccountPermissionValue);

					*delim = 0;
					pValue->pType = estrCreateFromStr(ppPermissionParts[i]);
					pValue->pValue = estrCreateFromStr(cur);

					estrTrimLeadingAndTrailingWhitespace(&pValue->pType);
					estrTrimLeadingAndTrailingWhitespace(&pValue->pValue);

					if (estrLength(&pValue->pType) && estrLength(&pValue->pValue))
						eaPush(eaValues, (AccountPermissionValue*) pValue);
					else
						StructDestroyNoConst(parse_AccountPermissionValue, pValue);
					cur = delim+1;
				}
				if (*cur)
				{
					NOCONST(AccountPermissionValue) *pValue = StructCreateNoConst(parse_AccountPermissionValue);
					pValue->pType = estrCreateFromStr(ppPermissionParts[i]);
					pValue->pValue = estrCreateFromStr(cur);

					estrTrimLeadingAndTrailingWhitespace(&pValue->pType);
					estrTrimLeadingAndTrailingWhitespace(&pValue->pValue);
					
					if (estrLength(&pValue->pType) && estrLength(&pValue->pValue))
						eaPush(eaValues, (AccountPermissionValue*) pValue);
					else
						StructDestroyNoConst(parse_AccountPermissionValue, pValue);
				}
			}

			free(ppPermissionParts[i]);
			free(ppPermissionParts[i+1]);
		}
	}
	if (eaShards)
		eaQSort(*eaShards, strCmp);
	if (eaValues)
		eaQSort(*eaValues, accountPermissionValueCmp);
	eaDestroy(&ppPermissionParts);
}

// Parse the permission struct's string into an earray of key/value's (eg. "shard", "playtest", "start", "1"....)
// Odd indices = key, even incides = value
void accountPermissionStringParse(const char *pPermissionString, char ***eaPermissionParts)
{
	const char *cur = pPermissionString;
	char *curKeyEnd = NULL, *curValueEnd = NULL;
	bool bHasShard = false;

	while (cur)
	{
		curKeyEnd = strstri(cur, ":");
		curValueEnd = strstri(cur, ";");

		if (curKeyEnd == NULL && curValueEnd == NULL)
		{
			if (!bHasShard)
			{
				eaPush(eaPermissionParts, strdup("shard"));
				eaPush(eaPermissionParts, strdup(cur));
				bHasShard = true;
			}
			cur = NULL;
		}
		else if (curKeyEnd == NULL)
		{
			if (!bHasShard)
			{
				int len = curValueEnd - cur;
				char *value = malloc (len + 1);

				eaPush(eaPermissionParts, strdup("shard"));
				memcpy(value, cur, len);
				value[len] = '\0';
				eaPush(eaPermissionParts, value);			
				bHasShard = true;
			}
			cur = NULL;
		}
		else if (curValueEnd == NULL)
		{
			int len = curKeyEnd - cur;
			char *key = malloc (len + 1);

			memcpy(key, cur, len);
			key[len] = '\0';
			eaPush(eaPermissionParts, key);	

			cur = curKeyEnd+1;
			while (*cur && IS_WHITESPACE(*cur))
				cur++; // trim leading whitespace, leave ending whitespace
			eaPush(eaPermissionParts, strdup(cur));
			cur = NULL;
		}
		else if (curValueEnd < curKeyEnd ) // eg. "<value>; <key>: ..."
		{ // treat value as shard string, but continue on
			if (!bHasShard)
			{
				int len = curValueEnd - cur;
				char *value = malloc (len + 1);

				eaPush(eaPermissionParts, strdup("shard"));
				memcpy(value, cur, len);
				value[len] = '\0';
				eaPush(eaPermissionParts, value);			
				bHasShard = true;
			}
			cur = curValueEnd+1;
			while (*cur && IS_WHITESPACE(*cur))
				cur++;
		}
		else  // "<key>: <value>; ..."
		{
			int len = curKeyEnd - cur;
			char *key = malloc (len + 1);
			char *value;

			memcpy(key, cur, len);
			key[len] = '\0';
			eaPush(eaPermissionParts, key);	

			cur = curKeyEnd+1;
			while (*cur && IS_WHITESPACE(*cur))
				cur++; // trim leading whitespace, leave ending whitespace
			len = curValueEnd - cur;
			value = malloc (len + 1);
			memcpy(value, cur, len);
			value[len] = '\0';
			eaPush(eaPermissionParts, value);

			if (stricmp(key, "shard") == 0)
				bHasShard = true;

			cur = curValueEnd+1;
			while (*cur && IS_WHITESPACE(*cur))
				cur++;
		}
	}
}

static bool AccountHasTicketID(AccountInfo *account, U32 uTicketID)
{
	int i;

	assert (account);
	for (i=eaSize(&account->ppTickets)-1; i>=0; i--)
	{
		if (account->ppTickets[i]->uRandomID == uTicketID)
			return true;
	}
	return false;
}
int AccountCreateTicket(AccountLoginType eLoginType, SA_PARAM_NN_VALID AccountInfo *account, const char *pMachineID, U32 uIp, bool bIgnoreMachineID)
{
	static char ipStr[4*4];
	int i = 0;
	AccountTicketCache *ticket = NULL;
	U32 uCurTime = 0;
	
	PERFINFO_AUTO_START_FUNC();

	uCurTime = timeSecondsSince2000();
	bIgnoreMachineID = bIgnoreMachineID || machineLockingIsIPWhitelisted(uIp);

	for (i=eaSize(&account->ppTickets)-1; i>=0; i--)
	{
		// tickets are always appended, so newest ticket is at the end
		if (account->ppTickets[i]->uExpireTime < uCurTime)
		{
			int j;
			for (j=0; j<=i; j++)
				StructDestroy(parse_AccountTicketCache, account->ppTickets[j]);
			eaRemoveRange(&account->ppTickets, 0, i+1);
			break;
		}
	}

	GetIpStr(uIp, ipStr, 4*4);

	ticket = StructCreate(parse_AccountTicketCache);
	// Tickets are only ever created by Cryptic Clients (Launcher, Game Shards)
	ticket->bMachineRestricted = !bIgnoreMachineID && !accountIsMachineIDAllowed(account, pMachineID, MachineType_CrypticClient, ipStr, NULL);
	ticket->machineID = StructAllocString(pMachineID);
	ticket->ticket = createTicket(eLoginType, account, ticket->bMachineRestricted);
	ticket->uIp = uIp;

	do
	{
		ticket->uRandomID = cryptSecureRand();
	} while (AccountHasTicketID(account, ticket->uRandomID));
	ticket->uExpireTime = ticket->ticket->uExpirationTime;
	eaPush(&account->ppTickets, ticket);

	PERFINFO_AUTO_STOP_FUNC();

	return ticket->uRandomID;
}

AccountTicketCache *AccountFindTicketByID(AccountInfo *account, U32 uTicketID)
{
	int i;
	AccountTicketCache *ticket = NULL;
	U32 uCurTime = timeSecondsSince2000();

	if (!account)
		return NULL;
	for (i=eaSize(&account->ppTickets)-1; i>=0; i--)
	{
		// tickets are always appended, so newest ticket is at the end
		if (account->ppTickets[i]->uExpireTime < uCurTime)
		{
			int j;
			for (j=0; j<=i; j++)
			{
				if (account->ppTickets[j]->uRandomID == uTicketID)
					ticket = account->ppTickets[j];
				else
					StructDestroy(parse_AccountTicketCache, account->ppTickets[j]);
			}
			eaRemoveRange(&account->ppTickets, 0, i+1);
			break;
		}
		else if (account->ppTickets[i]->uRandomID == uTicketID)
		{
			ticket = account->ppTickets[i];
			eaRemove(&account->ppTickets, i); // remove ticket once it's been found (and presumably used)
		}
	}
	return ticket;
}

// Pass in true for bLocalIp to ignore/bypass the InternalUseLogin check
LoginFailureCode accountIsAllowedToLogin(AccountInfo *account, bool bLocalIp)
{
	// No such account
	if(!account)
		return LoginFailureCode_NotFound;

	// Internal accounts only work on local IPs
	if(!bLocalIp && account->bInternalUseLogin)
		return LoginFailureCode_NotFound;

	// Disabled accounts are never allowed to login.
	if (account->bLoginDisabled)
		return LoginFailureCode_Disabled;

	if (!nullStr(account->pPWAccountName))
	{
		PWCommonAccount *pPWaccount = findPWCommonAccountbyName(account->pPWAccountName);
		if (pPWaccount && pPWaccount->bBanned)
			return LoginFailureCode_Banned;
	}

	return LoginFailureCode_Ok;
}

const char *accountGetEmail(const AccountInfo *account)
{
	if (!account)
		return NULL;
	if (account->pPWAccountName)
	{
		PWCommonAccount * pwAccount = findPWCommonAccountbyName(account->pPWAccountName);
		if (pwAccount)
			return pwAccount->pEmail;
		return account->personalInfo.email;
	}
	return account->personalInfo.email;
}

// Append a log pair string, for accountLogLoginAttempt().
static void accountLogLoginAttempt_AppendPairString(SA_PRE_NN_NN_STR char **estrLogLine, SA_PARAM_NN_VALID const char *name, SA_PARAM_OP_VALID const char *string)
{
	if (string && *string)
		logAppendPairs(estrLogLine, logPair(name, "%s", string), NULL);
}

// Report this login attempt.
void accountLogLoginAttempt(AccountLoginAttemptData *pData)
{
	char *logline = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Add basic information to log line.
	estrStackCreate(&logline);
	logAppendPairs(&logline,
		logPair("attempted_private", "%s", pData->pPrivateAccountName),
		logPair("login_successful", "%d", !!pData->bLoginSuccessful),
		logPair("rejected_apriori", "%d", !!pData->bRejectedAPriori),
		logPair("good_account", "%d", !!pData->bGoodAccount),
		logPair("good_password", "%d", !!pData->bGoodPassword),
		logPair("login_allowed", "%d", !!pData->bLoginAllowed),
		logPair("blocked_ip", "%d", !!pData->bIpBlocked),
		logPair("link_blocked", "%d", !!pData->bLinkBlocked),
		logPair("internal", "%d", !!pData->bInternal),
		logPair("protocol", "%s", StaticDefineIntRevLookup(LoginProtocolEnum, pData->protocol)),
		logPair("login_type", "%s", StaticDefineIntRevLookup(AccountLoginTypeEnum, pData->eLoginType)),
		logPair("machine_locked", "%d", !!pData->bMachineLocked),
		logPair("machine_id", "%s", pData->pMachineID),
		logPair("password_decrypted", "%d", !!pData->bPasswordDecrypted),
		logPair("encryption_key", "%s", StaticDefineIntRevLookup(AccountServerEncryptionKeyVersionEnum, pData->eKeyVersion)),
		logPair("attempted_private_is_email", "%d", !!pData->bAccountNameIsEmail),
		NULL);

	// Add IP information, if present.
	EARRAY_CONST_FOREACH_BEGIN(pData->ips, i, n);
	{
		char name[2 + 10 + 1];
		sprintf(name, "ip%d", i);
		logAppendPairs(&logline, logPair(name, "%s", pData->ips[i] ? pData->ips[i] : ""), NULL);
	}
	EARRAY_FOREACH_END;

	// Add optional information.
	accountLogLoginAttempt_AppendPairString(&logline, "apriori_reason", pData->aPrioriReason);
	accountLogLoginAttempt_AppendPairString(&logline, "location", pData->location);
	accountLogLoginAttempt_AppendPairString(&logline, "referrer", pData->referrer);
	accountLogLoginAttempt_AppendPairString(&logline, "client_version", pData->clientVersion);
	accountLogLoginAttempt_AppendPairString(&logline, "note", pData->note);
	accountLogLoginAttempt_AppendPairString(&logline, "peer_ip", pData->peerIp);

	// Generate log line.
	objLog(LOG_LOGIN, GLOBALTYPE_ACCOUNT, pData->uMatchingAccountId, 0, pData->matchingDisplayName, NULL, NULL, "LoginAttempt", NULL, "%s", logline);
	estrDestroy(&logline);

	PERFINFO_AUTO_STOP();
}

void accountAddExpectedSub(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubInternalName)
{
	if (!verify(pAccount)) return;
	if (!verify(pSubInternalName && *pSubInternalName)) return;

	PERFINFO_AUTO_START_FUNC();

	if (!accountExpectsSub(pAccount, pSubInternalName))
	{
		ExpectedSubscription *pExpected = StructCreate(parse_ExpectedSubscription);

		if (!pAccount->eaExpectedSubs)
		{
			eaIndexedEnable(&pAccount->eaExpectedSubs, parse_ExpectedSubscription);
		}

		pExpected->pInternalName = strdup(pSubInternalName);
		pExpected->uExpectedSinceSS2000 = timeSecondsSince2000();

		eaIndexedAdd(&pAccount->eaExpectedSubs, pExpected);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

U32 accountExpectsSub(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubInternalName)
{
	U32 uExpectedSince = 0;

	if (!verify(pAccount)) return 0;
	if (!verify(pSubInternalName && *pSubInternalName)) return 0;

	PERFINFO_AUTO_START_FUNC();

	// See if we expect a sub to be in our cache that isn't.
	if (pAccount->eaExpectedSubs)
	{
		int iIndex = eaIndexedFindUsingString(&pAccount->eaExpectedSubs, pSubInternalName);

		if (iIndex > -1)
		{
			uExpectedSince = pAccount->eaExpectedSubs[iIndex]->uExpectedSinceSS2000;
		}
	}

	if (gbBlockOnPending)
	{
		// See if we have a pending PayPal sub we expect to change to a different state.
		if (!uExpectedSince && pAccount->pCachedSubscriptionList)
		{
			EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, iCurSub, iNumSubs);
			{
				const CachedAccountSubscription *pCachedSub = pAccount->pCachedSubscriptionList->ppList[iCurSub];

				if (!devassert(pCachedSub)) continue;

				if (!stricmp_safe(pCachedSub->internalName, pSubInternalName) && pCachedSub->vindiciaStatus == SUBSCRIPTIONSTATUS_PENDINGCUSTOMER)
				{
					uExpectedSince = pCachedSub->estimatedCreationTimeSS2000;
					break;
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();

	return uExpectedSince;
}

void accountRemoveExpectedSub(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pSubInternalName)
{
	if (!verify(pAccount)) return;
	if (!verify(pSubInternalName && *pSubInternalName)) return;

	PERFINFO_AUTO_START_FUNC();

	if (pAccount->eaExpectedSubs)
	{
		int iIndex = eaIndexedFindUsingString(&pAccount->eaExpectedSubs, pSubInternalName);

		if (iIndex > -1)
		{
			StructDestroy(parse_ExpectedSubscription, eaRemove(&pAccount->eaExpectedSubs, iIndex));
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Determine if a link should be trusted for the global chat server
static bool LoginServerLinkIsSecure(SA_PARAM_NN_VALID NetLink* link)
{
	// Theo says this needs to be trusted because of DMZ issues (it was local)
	if (ipfIsTrustedIp(linkGetIp(link)))
		return true;

	return false;
}

// Auto-create an account with defaults reasonable for development use.
static AccountInfo *AutoCreateAccount(const char *pAccount, SA_PRE_NN_NN_STR const char *const *ips)
{
	char hashedPassword[MAX_PASSWORD] = "";
	static char *email = NULL;
	AccountInfo *account = NULL;

	PERFINFO_AUTO_START_FUNC();

	accountHashPassword(pAccount, hashedPassword);
	estrPrintf(&email, "%s@" ORGANIZATION_DOMAIN, pAccount);
	if (createFullAccount(pAccount, hashedPassword, pAccount, 
		email, NULL, NULL, NULL, NULL, NULL, ips, false, false, 9))
	{
		account = findAccountByName(pAccount);
		devassert(account);
	}

	PERFINFO_AUTO_STOP_FUNC();

	return account;
}

static bool IPAllowedAccountAutoCreate(U32 uIP)
{
	if (gbDisableAutoCreateLocal) return false;

	if (ipfIsTrustedIp(uIP)) return true;

	return false;
}

static bool IPNeedsPassword(U32 uIP)
{
	if (ipfIsTrustedIp(uIP)) return false;

	return true;
}

// Return false if the account name or password is too long.
bool confirmLoginInfoLength(SA_PARAM_OP_STR const char *pLoginField, SA_PARAM_OP_STR const char *pPassword, SA_PARAM_OP_VALID STRING_EARRAY ips, char **estrFailureReason)
{
	bool bAccountTooLong = strlen(pLoginField) >= MAX_LOGIN_FIELD;
	bool bPasswordTooLong = strlen(pPassword) >= MAX_PASSWORD;

	if (bAccountTooLong ||
		bPasswordTooLong )
	{
		AccountLoginAttemptData loginData = {0};
		estrPrintf(estrFailureReason, "%s length exceeded", bAccountTooLong ? bPasswordTooLong ? "account name and password" : "account name" : "password");
		loginData.pPrivateAccountName = pLoginField;
		loginData.bRejectedAPriori = true;
		loginData.aPrioriReason = *estrFailureReason;
		loginData.protocol = LoginProtocol_AccountNet;
		loginData.ips = ips;
		accountLogLoginAttempt(&loginData);
		return false;
	}

	return true;
}

// Fold similar failure codes, for security.
static LoginFailureCode FoldFailureCode(LoginFailureCode code)
{
	static const LoginFailureCode table[] = {
		/* LoginFailureCode_Unknown */					LoginFailureCode_NotFound,
		/* LoginFailureCode_Ok */						LoginFailureCode_NotFound,
		/* LoginFailureCode_NotFound */					LoginFailureCode_NotFound,
		/* LoginFailureCode_BadPassword */				LoginFailureCode_NotFound,
		/* LoginFailureCode_RateLimit */				LoginFailureCode_RateLimit,
		/* LoginFailureCode_Disabled */					LoginFailureCode_Disabled,
		/* LoginFailureCode_UnlinkedPWCommonAccount */	LoginFailureCode_UnlinkedPWCommonAccount,
		/* LoginFailureCode_InvalidTicket */			LoginFailureCode_NotFound,					// Should never happen, not used here
		/* LoginFailureCode_DisabledLinked */			LoginFailureCode_DisabledLinked,
		/* LoginFailureCode_InvalidLoginType */         LoginFailureCode_NotFound,
		/* LoginFailureCode_Banned */                   LoginFailureCode_Banned,
		/* LoginFailureCode_NewMachineID */             LoginFailureCode_NewMachineID,
		/* LoginFailureCode_CrypticDisabled */          LoginFailureCode_CrypticDisabled,

	};

	devassert(code != LoginFailureCode_InvalidTicket);
	if ((size_t)code >= sizeof(table)/sizeof(*table))
	{
		devassert(0);
		return LoginFailureCode_NotFound;
	}

	return table[code];
}

// Get the legacy failure text for a particular failure type.
static const char *LegacyFailureText(LoginFailureCode code)
{

#define LOGIN_GENERIC_FAILURE "Invalid username or password."

	static const char *const table[] = {
		/* LoginFailureCode_Unknown */					LOGIN_GENERIC_FAILURE,
		/* LoginFailureCode_Ok */						NULL,
		/* LoginFailureCode_NotFound */					LOGIN_GENERIC_FAILURE,
		/* LoginFailureCode_BadPassword */				LOGIN_GENERIC_FAILURE,
		/* LoginFailureCode_RateLimit */				"Too many attempts; please try again later.",
		/* LoginFailureCode_Disabled */					"This account has been disabled.",
		/* LoginFailureCode_UnlinkedPWCommonAccount */	"No Cryptic account linked to Perfect World credentials.",
		/* LoginFailureCode_InvalidTicket */			LOGIN_GENERIC_FAILURE,										// Should never happen, not used here
		/* LoginFailureCode_DisabledLinked */			"Please log in using your Perfect World account name.",
		/* LoginFailureCode_InvalidLoginType */         LOGIN_GENERIC_FAILURE,
		/* LoginFailureCode_Banned */                   "This account has been banned.",
		/* LoginFailureCode_NewMachineID */             LOGIN_GENERIC_FAILURE, // This feature is unsupported by legacy systems
		/* LoginFailureCode_CrypticDisabled */          "This account must be linked to a Perfect World account to log in.",
	};

	devassert(code != LoginFailureCode_InvalidTicket);
	if ((size_t)code >= sizeof(table)/sizeof(*table))
	{
		devassert(0);
		return LOGIN_GENERIC_FAILURE;
	}

#undef LOGIN_GENERIC_FAILURE

	return table[code];
}

// Send a login failure packet to the link.
static void SendLoginFailurePacket(NetLink *link, LoginFailureCode reason, U32 conflictTicketId)
{
	Packet *pak;
	LoginFailureCode code = FoldFailureCode(reason);

	// Send new failure packet, without flushing, so the next packet will be sent with it.
	pak = pktCreate(link, FROM_ACCOUNTSERVER_LOGIN_FAILED);
	pktSendU32(pak, code);
	if (reason == LoginFailureCode_UnlinkedPWCommonAccount || reason == LoginFailureCode_CrypticDisabled)
	{
		devassert(conflictTicketId);
		pktSendU32(pak, conflictTicketId);
	}
	else
		devassert(conflictTicketId == 0);
	pktSendNoFlush(&pak);

	// Send legacy packet, and flush.
	pak = pktCreate(link, FROM_ACCOUNTSERVER_FAILED);
	pktSendString(pak, LegacyFailureText(code));
	pktSend(&pak);

}

// Send a validate failure packet to the link.
static void SendLoginValidateFailurePacket(NetLink *link, LoginFailureCode failureCode, U32 uID)
{
	Packet *pak;

	PERFINFO_AUTO_START_FUNC();
	// Send new failure packet, without flushing, so the next packet will be sent with it.
	pak = pktCreate(link, FROM_ACCOUNTSERVER_LOGINVALIDATE_FAILED);
	pktSendU32(pak, failureCode);
	pktSendU32(pak, uID);
	pktSendNoFlush(&pak);

	// Send legacy packet, and flush.
	pak = pktCreate(link, FROM_ACCOUNTSERVER_FAILED);
	pktSendString(pak, "Invalid login.");
	pktSendU32(pak, uID);
	pktSend(&pak);
	PERFINFO_AUTO_STOP();

}

static U32 guLoginDataUpdateSeconds = 10;
AUTO_CMD_INT(guLoginDataUpdateSeconds, LoginDataUpdateSeconds) ACMD_CMDLINE;

static void CreateAccountLoginTicket(SA_PARAM_NN_VALID NetLink *link, AccountLoginType eLoginType, SA_PARAM_NN_VALID AccountInfo *account, const char *pMachineID, U32 uIP)
{
	Packet *response = NULL;
	U32 uTicketID = 0;
	TransactionRequest *pTransRequest = NULL;
	U32 uNow = 0;

	// Login is successful: create ticket.
	PERFINFO_AUTO_START_FUNC();
	uTicketID = AccountCreateTicket(eLoginType, account, pMachineID, uIP, false);

	// Send success response.
	response = pktCreate(link, FROM_ACCOUNTSERVER_LOGIN_NEW);
	pktSendU32(response, account->uID);
	pktSendU32(response, uTicketID);
	pktSendU32(response, eLoginType); // Send the final account type
	pktSend(&response);

	uNow = timeSecondsSince2000();
	if (account->loginData.uIP != uIP || uNow - account->loginData.uTime > guLoginDataUpdateSeconds)
	{
		// Record login information.
		pTransRequest = objCreateTransactionRequest();

		if (account->loginData.uIP != uIP)
		{
			objAddToTransactionRequestf(pTransRequest, GLOBALTYPE_ACCOUNT, account->uID, NULL, "set loginData.uIP = \"%d\"", uIP);
		}

		objAddToTransactionRequestf(pTransRequest, GLOBALTYPE_ACCOUNT, account->uID, NULL, "set loginData.uTime = \"%d\"", uNow);

		objRequestTransaction(NULL, "SetLoginData", pTransRequest);
		objDestroyTransactionRequest(pTransRequest);
		pTransRequest = NULL;
	}

	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_STRUCT;
typedef struct HandleLoginData
{
	AccountLoginType eLoginType;
	char * pMachineID;
	U32 uIP;
	bool bLocalIP;
	char * pFirstIP;
	U32 uLinkID;
} HandleLoginData;

static void HandleLogin_CommonCallback(AccountInfo * pAccount,
	LoginFailureCode eFailureCode, U32 uTicketID, void * userData)
{
	HandleLoginData * pData = userData;
	NetLink * pLink = linkFindByID(pData->uLinkID);

	if (pLink)
	{
		if (pAccount)
		{
			// Login is successful: create ticket.
			CreateAccountLoginTicket(pLink, pData->eLoginType, pAccount, pData->pMachineID, pData->uIP);

			servLog(LOG_ACCOUNT_SERVER_GENERAL, pData->bLocalIP ? "InternalLogin" : "ExternalLogin",
				"accountname %s accountid %lu ip %s", pAccount->accountName, pAccount->uID,
				pData->pFirstIP);  // Obsolete log line
		}
		else
		{
			SendLoginFailurePacket(pLink, eFailureCode, uTicketID);
		}
	}

	StructDestroy(parse_HandleLoginData, pData);
}

// Handle login packet.
static void HandleLogin_Common(Packet* pak, NetLink* link, AccountLink *accountLink,
	const char *pAccount,
	const char *pPassword,
	const char *pPasswordMD5,
	const char *pNewStyleSaltedCrypticPassword,
	const char *pPasswordFixedSalt,
	AccountServerEncryptionKeyVersion eKeyVersion,
	const char *pEncryptedPassword,
	const char *pFixedSalt,
	const char *pMachineID)
{
	U32 uIP = 0;
	static char **ips = NULL;			// Array reused between calls: see below.
	static const int ip_str_size = 4*4;
	bool bLocalIp = false;
	char *pFailureReason = NULL;
	AccountLoginAttemptData loginData = {0};
	bool bIsServerLogin = false;
	HandleLoginData * pData = NULL;

	PERFINFO_AUTO_START_FUNC();

	uIP = accountLink->ipRequest ? accountLink->ipRequest : linkGetIp(link);
	bLocalIp = ipfIsLocalIp(uIP);
	bIsServerLogin = ipfIsIpInGroup("CrypticServers", uIP);

	// Format the string IP.
	if (!ips)
	{
		eaSetSize(&ips, 1);
		estrSetSize(&ips[0], ip_str_size);
	}
	GetIpStr(uIP, ips[0], ip_str_size);

	// Check account and password length, so that they're safe to store on the accountLink.
	estrStackCreate(&pFailureReason);

	if (pPassword)
	{
		if (!confirmLoginInfoLength(pAccount, pPassword, ips, &pFailureReason))
		{
			SendLoginFailurePacket(link, LoginFailureCode_NotFound, 0);
			estrDestroy(&pFailureReason);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}

	if (pPasswordMD5)
	{
		if (!confirmLoginInfoLength(pAccount, pPasswordMD5, ips, &pFailureReason))
		{
			SendLoginFailurePacket(link, LoginFailureCode_NotFound, 0);
			estrDestroy(&pFailureReason);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}

	if (pNewStyleSaltedCrypticPassword)
	{
		if (!confirmLoginInfoLength(pAccount, pNewStyleSaltedCrypticPassword, ips, &pFailureReason))
		{
			SendLoginFailurePacket(link, LoginFailureCode_NotFound, 0);
			estrDestroy(&pFailureReason);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}

	if (!nullStr(pPasswordFixedSalt))
	{
		if (nullStr(pFixedSalt) || !confirmLoginInfoLength(pAccount, pPasswordFixedSalt, ips, &pFailureReason))
		{
			SendLoginFailurePacket(link, LoginFailureCode_NotFound, 0);
			estrDestroy(&pFailureReason);
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
	}

	estrDestroy(&pFailureReason);

	// Save account name and password.
	if (nullStr(accountLink->loginField))
	{
		strcpy(accountLink->loginField, pAccount);
	}
	
	// Perfect World Logins - setting the password hash to the correct pointer
	if (accountLink->eLoginType == ACCOUNTLOGINTYPE_PerfectWorld && pPassword && pPassword[0] && !(pPasswordMD5 && pPasswordMD5[0]))
	{
		pPasswordMD5 = pPassword;
		pPassword = NULL;
	}

	// Populate loginData
	loginData.pPrivateAccountName = pAccount;
	loginData.pSHA256Password = pPassword;
	loginData.pMD5Password = pPasswordMD5;
	loginData.pCrypticPasswordWithAccountNameAndNewStyleSalt = pNewStyleSaltedCrypticPassword;
	loginData.pPasswordFixedSalt = pPasswordFixedSalt;
	loginData.pFixedSalt = pFixedSalt;
	loginData.salt = accountLink->temporarySalt;
	loginData.eLoginType = accountLink->eLoginType;
	loginData.pMachineID = pMachineID;
	loginData.bIpNeedsPassword = IPNeedsPassword(uIP);
	loginData.bIpAllowedAutocreate = IPAllowedAccountAutoCreate(uIP);
	loginData.ips = ips;
	loginData.protocol = LoginProtocol_AccountNet;
	loginData.bInternal = bLocalIp;
	loginData.eKeyVersion = eKeyVersion;
	loginData.pPasswordEncrypted = pEncryptedPassword;

	// Attempt login.
	pData = StructCreate(parse_HandleLoginData);
	pData->eLoginType = accountLink->eLoginType;
	pData->pMachineID = pMachineID ? strdup(pMachineID) : NULL;
	pData->uIP = uIP;
	pData->bLocalIP = bLocalIp;
	pData->pFirstIP = ips[0] ? strdup(ips[0]) : NULL;
	pData->uLinkID = linkID(link);
	confirmLogin(&loginData, true, HandleLogin_CommonCallback, pData);

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleLoginWithNVPStruct(Packet *pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	AccountNetStruct_ToAccountServerLogin recvStruct = {0};
	const char * pLoginField = NULL;

	if (!ParserReceiveStructAsCheckedNameValuePairs(pak, parse_AccountNetStruct_ToAccountServerLogin, &recvStruct))
	{
		//malformed data of some sort... do nothing
		return;
	}

	//no point in picking one of the two, just use whichever password we have
	if (recvStruct.hashedCrypticPassword[0] && recvStruct.hashedPWPassword[0])
	{
		accountLink->eLoginType = ACCOUNTLOGINTYPE_CrypticAndPW;
	}
	else if (recvStruct.hashedPWPassword[0])
	{
		accountLink->eLoginType = ACCOUNTLOGINTYPE_PerfectWorld;
	}
	else
	{
		accountLink->eLoginType = ACCOUNTLOGINTYPE_Cryptic;
	}

	if (!nullStr(accountLink->loginField))
	{
		pLoginField = accountLink->loginField;
	}
	else
	{
		pLoginField = recvStruct.accountName;
	}

	HandleLogin_Common(pak, link, accountLink, pLoginField, NULL,
		recvStruct.hashedPWPassword, recvStruct.hashedCrypticPassword, recvStruct.hashedPWPasswordFixedSalt,
		recvStruct.eKeyVersion, recvStruct.encryptedPassword,
		accountLink->fixedSalt, recvStruct.machineID);
}

static void HandleLoginMachineID(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	bool bPacketOK = false;
	char * pAccount = NULL;
	char * pPassword = NULL, * pPasswordMD5 = NULL;
	char * pMachineID = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Get login information.
	PKT_CHECK_STR(pak, packetfailed);
	pAccount = pktGetStringTemp(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pPassword = pktGetStringTemp(pak);
	PKT_CHECK_BYTES(4, pak, packetfailed);
	accountLink->eLoginType = pktGetU32(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pMachineID = pktGetStringTemp(pak);
	bPacketOK = true;

	// If the packet is malformed, just drop the request.
	packetfailed:
	if (!bPacketOK)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (accountLink->eLoginType == ACCOUNTLOGINTYPE_CrypticAndPW) // Dual-mode login
	{
		// First password is Cryptic-hashed, this additional string is the PW password hash
		if (pktCheckNullTerm(pak))
			pPasswordMD5 = pktGetStringTemp(pak);
	}
	HandleLogin_Common(pak, link, accountLink, pAccount, pPassword, pPasswordMD5, NULL, NULL, 0, NULL, NULL, pMachineID);
	PERFINFO_AUTO_STOP_FUNC();

}

#define RATELIMIT_APRIORI_REASON "Rate limit hit"

static bool accountLogin_RateLimitAuthBlocked(enum LoginFailureCode *failureCode, SA_PARAM_NN_VALID AccountLoginAttemptData *pLoginAttemptData)
{
	if (!IPRateLimit(pLoginAttemptData->ips, IPRLA_Authentication))
	{
		IPRateLimit(pLoginAttemptData->ips, IPRLA_AuthenticationFailure);
		*failureCode = LoginFailureCode_RateLimit;
		pLoginAttemptData->bRejectedAPriori = true;
		pLoginAttemptData->aPrioriReason = RATELIMIT_APRIORI_REASON;
		pLoginAttemptData->bIpBlocked = true;
		accountLogLoginAttempt(pLoginAttemptData);
		return true;
	}
	return false;
}
static bool accountLogin_RateLimit(IPRateLimitActivity eActivity, enum LoginFailureCode *failureCode, SA_PARAM_NN_VALID AccountLoginAttemptData *pLoginAttemptData)
{
	if (!IPRateLimit(pLoginAttemptData->ips, eActivity))
	{
		*failureCode = LoginFailureCode_RateLimit;
		pLoginAttemptData->bIpBlocked = true;
		accountLogLoginAttempt(pLoginAttemptData);
		return true;
	}
	return false;
}

static bool gbPreventLinkedCrypticLogin = true;
AUTO_CMD_INT(gbPreventLinkedCrypticLogin, PreventLinkedCrypticLogin) ACMD_CMDLINE;

static bool gbDontAllowEmailLogin = false;
AUTO_CMD_INT(gbDontAllowEmailLogin, DontAllowEmailLogin) ACMD_CMDLINE;

typedef struct ConfirmLoginData
{
	AccountLoginAttemptData * pLoginData;
	bool bCreateConflictTicket;
	ConfirmLoginCallback pCallback;
	void * pUserData;
} ConfirmLoginData;

void confirmLogin_CB(const char * pPlaintextPassword, void * pUserData)
{
	ConfirmLoginData * pData = pUserData;
	PWCommonAccount *pPWEAccount = NULL;
	AccountInfo * pAccount = NULL;
	bool bFound = false;
	
	PERFINFO_AUTO_START_FUNC();

	pData->pLoginData->bPasswordDecrypted = true;

	pPWEAccount = findPWCommonAccountByEmail(pData->pLoginData->pPrivateAccountName);
	if (!pPWEAccount && pData->pLoginData->eKeyVersion == ASKEY_identity)
	{
		pPWEAccount = findPWCommonAccountbyName(pData->pLoginData->pPrivateAccountName);
	}

	if (pPWEAccount)
	{
		char hashedPWEPass[MAX_PASSWORD] = {0};
		char hashedPWEPassFixedSalt[MAX_PASSWORD] = {0};
		const char * pFixedSalt = pData->pLoginData->pFixedSalt;
		
		accountPWHashPassword(pPWEAccount->pAccountName, pPlaintextPassword, hashedPWEPass);

		if (!nullStr(pPWEAccount->pFixedSalt))
		{
			pFixedSalt = pPWEAccount->pFixedSalt;
		}

		accountPWHashPasswordFixedSalt(pFixedSalt, pPlaintextPassword, hashedPWEPassFixedSalt);

		SAFE_FREE(pData->pLoginData->pMD5Password);
		SAFE_FREE(pData->pLoginData->pPasswordFixedSalt);
		pData->pLoginData->pMD5Password = strdup(hashedPWEPass);
		pData->pLoginData->pPasswordFixedSalt = strdup(hashedPWEPassFixedSalt);

		bFound = true;
	}

	pAccount = findAccountByName(pData->pLoginData->pPrivateAccountName);
	if (pAccount)
	{
		char hashedCrypticPass[MAX_PASSWORD] = {0};

		accountHashPassword(pPlaintextPassword, hashedCrypticPass);

		SAFE_FREE(pData->pLoginData->pSHA256Password);
		pData->pLoginData->pSHA256Password = strdup(hashedCrypticPass);

		bFound = true;
	}

	if (bFound)
	{
		pData->pLoginData->salt = 0;

		// Let's try again...
		confirmLogin(pData->pLoginData, pData->bCreateConflictTicket, pData->pCallback, pData->pUserData);
	}
	else
	{
		accountLogLoginAttempt(pData->pLoginData);
		pData->pCallback(NULL, LoginFailureCode_NotFound, 0, pData->pUserData);
	}

	StructDestroy(parse_AccountLoginAttemptData, pData->pLoginData);
	free(pData);
	PERFINFO_AUTO_STOP();
}

// Confirm login credentials and log in an account.
// See accountLogLoginAttempt() for an explanation of logging parameters.
void confirmLogin(AccountLoginAttemptData *pLoginData,
	bool bCreateConflictTicket,
	ConfirmLoginCallback callback,
	void * userData)
{
	AccountInfo *pCrypticAccount = NULL;
	const char *pDisplayName = NULL;
	AccountLoginType eType = pLoginData->eLoginType; // Cache this so it doesn't get changed for logging
	LoginFailureCode eFailureCode = LoginFailureCode_Unknown;
	bool bFixedSaltStored = false;
	bool bPWEAccountFound = false;

	PERFINFO_AUTO_START_FUNC();

	if (eType == ACCOUNTLOGINTYPE_Default)
	{
		eType = ACCOUNTLOGINTYPE_Cryptic;
	}

	if (!pLoginData->pPrivateAccountName)
	{
		callback(NULL, LoginFailureCode_Unknown, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	if (eType != ACCOUNTLOGINTYPE_Cryptic &&
		eType != ACCOUNTLOGINTYPE_PerfectWorld &&
		eType != ACCOUNTLOGINTYPE_CrypticAndPW)
	{
		callback(NULL, LoginFailureCode_InvalidLoginType, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Rate limiting early-out
	if (accountLogin_RateLimitAuthBlocked(&eFailureCode, pLoginData))
	{
		callback(NULL, eFailureCode, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Find the existing account
	if (eType == ACCOUNTLOGINTYPE_PerfectWorld || eType == ACCOUNTLOGINTYPE_CrypticAndPW)
	{
		PWCommonAccount *pPWEAccount = findPWCommonAccountbyName(pLoginData->pPrivateAccountName);
		if (!pPWEAccount)
		{
			pPWEAccount = findPWCommonAccountByEmail(pLoginData->pPrivateAccountName);
			if (pPWEAccount)
			{
				pLoginData->bAccountNameIsEmail = true;
			}
		}

		if (pPWEAccount)
		{
			bFixedSaltStored = !nullStr(pPWEAccount->pFixedSalt);
			bPWEAccountFound = true;
		}

		if (pPWEAccount && confirmPerfectWorldLoginPassword(pPWEAccount,
			pLoginData->pMD5Password, pLoginData->pPasswordFixedSalt,
			pLoginData->pFixedSalt, pLoginData->salt, pLoginData->bAccountNameIsEmail))
		{
			pDisplayName = pPWEAccount->pForumName;
			eType = ACCOUNTLOGINTYPE_PerfectWorld; // Set it to this so that dual-mode login knows it's found a PW account

			if (pPWEAccount->uLinkedID)
			{
				pCrypticAccount = findAccountByID(pPWEAccount->uLinkedID);
			}
			else
			{
				U32 uConflictTicket = 0;
				if (bCreateConflictTicket)
					uConflictTicket = createConflictTicket(pPWEAccount);
				pLoginData->matchingDisplayName = pDisplayName;
				accountLogLoginAttempt(pLoginData);
				pLoginData->matchingDisplayName = NULL; // don't leave memory we set, as we don't own the struct
				callback(NULL, LoginFailureCode_UnlinkedPWCommonAccount, uConflictTicket, userData);
				PERFINFO_AUTO_STOP_FUNC();
				return;
			}
		}

		// By now, the only way we don't have a pCrypticAccount is if
		// the username or password is wrong
	}

	if (eType == ACCOUNTLOGINTYPE_Cryptic || eType == ACCOUNTLOGINTYPE_CrypticAndPW) // Cryptic
	{
		pCrypticAccount = findAccountByName(pLoginData->pPrivateAccountName);
		if (pCrypticAccount)
		{
			pLoginData->bAccountNameIsEmail = false;
		}

		if (pCrypticAccount && confirmLoginPassword(pCrypticAccount,
			pLoginData->pSHA256Password, pLoginData->pMD5Password,
			pLoginData->pCrypticPasswordWithAccountNameAndNewStyleSalt,
			pLoginData->salt))
		{
			pDisplayName = pCrypticAccount->displayName;
		}
		else
		{
			pCrypticAccount = NULL;
		}
	}

	if (!pCrypticAccount &&									// We still haven't logged in
		!pLoginData->bPasswordDecrypted &&					// We haven't tried to decrypt the password yet
		!nullStr(pLoginData->pPasswordEncrypted) &&			// There is a password to decrypt
		(	pLoginData->eKeyVersion == ASKEY_identity ||		// ... and it's free to decrypt
			(	pLoginData->eKeyVersion != ASKEY_none &&		// Or the key type is set
				bPWEAccountFound &&								// ... and there's a PWE account to be found
				!bFixedSaltStored &&							// ... and the PWE account doesn't have a fixed salt yet
				pLoginData->bAccountNameIsEmail	&&				// ... and the account name is an e-mail address
				!gbDontAllowEmailLogin)))						// ... and we allow e-mail log-in
	{
		ConfirmLoginData * pData = callocStruct(ConfirmLoginData);

		pData->pCallback = callback;
		pData->bCreateConflictTicket = bCreateConflictTicket;
		pData->pUserData = userData;
		pData->pLoginData = StructClone(parse_AccountLoginAttemptData, pLoginData);

		ALDGetPassword(pLoginData->eKeyVersion, pLoginData->pPasswordEncrypted, confirmLogin_CB, pData);

		// no callback here; will be called later
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (eType == ACCOUNTLOGINTYPE_CrypticAndPW)
	{
		// No PW account was found - tell it to check for a Cryptic account and obey all Cryptic procedures
		// Including local IP checking for passwords and/or auto-account creation
		eType = ACCOUNTLOGINTYPE_Cryptic;
	}

	// Rate limiting for failed logins
	if (!pCrypticAccount && accountLogin_RateLimit(IPRLA_AuthenticationFailure, &eFailureCode, pLoginData))
	{
		callback(NULL, eFailureCode, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	
	if (eType == ACCOUNTLOGINTYPE_Cryptic)
	{
		if (!pCrypticAccount && !pLoginData->bIpNeedsPassword)
			pCrypticAccount = findAccountByName(pLoginData->pPrivateAccountName);
		if (!pCrypticAccount && pLoginData->bIpAllowedAutocreate)
			pCrypticAccount = AutoCreateAccount(pLoginData->pPrivateAccountName, pLoginData->ips);
	}

	// Reject accounts which don't authenticate.
	if (!pCrypticAccount)
	{
		bool bGoodPrivateName = false;
		U32 uContainerID = 0;

		if (eType == ACCOUNTLOGINTYPE_Cryptic)
		{
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Command: Login, Account: %s - %s Failed\n",
				pLoginData->pPrivateAccountName, pLoginData->bInternal ? "Internal" : "External");  // Obsolete log line
		}

		if (eType == ACCOUNTLOGINTYPE_PerfectWorld)
		{
			bGoodPrivateName = findPWCommonAccountByLoginField(pLoginData->pPrivateAccountName) ? true : false;
		}
		else
		{
			pCrypticAccount = findAccountByName(pLoginData->pPrivateAccountName);
			if (pCrypticAccount)
			{
				uContainerID = pCrypticAccount->uID;
				bGoodPrivateName = true;
			}
		}

		if (bGoodPrivateName)
		{
			pLoginData->matchingDisplayName = pDisplayName;
			pLoginData->uMatchingAccountId = uContainerID;
			pLoginData->bGoodAccount = true;
			eFailureCode = LoginFailureCode_BadPassword;
			accountLogLoginAttempt(pLoginData);
			pLoginData->matchingDisplayName = NULL; // don't leave memory we set, as we don't own the struct
		}
		else
		{
			eFailureCode = LoginFailureCode_NotFound;
			accountLogLoginAttempt(pLoginData);
		}

		devassert(eFailureCode != LoginFailureCode_Unknown);
		callback(NULL, eFailureCode, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	
	pLoginData->bGoodPassword = true;
	pLoginData->bGoodAccount = true;
	pLoginData->matchingDisplayName = pDisplayName;
	pLoginData->uMatchingAccountId = pCrypticAccount->uID;

	// Login failures past this point still trigger rate limiting, but do NOT log as rate limited

	// Reject accounts for which login is disabled.
	eFailureCode = accountIsAllowedToLogin(pCrypticAccount, pLoginData->bInternal);
	if (eFailureCode != LoginFailureCode_Ok ||
		(eType == ACCOUNTLOGINTYPE_Cryptic && pCrypticAccount->bPWAutoCreated))
	{
		if (eFailureCode == LoginFailureCode_Ok)
			eFailureCode = LoginFailureCode_Disabled;
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Command: Login, Account: %s - Account disabled\n", pLoginData->pPrivateAccountName);  // Obsolete log line
		accountLogLoginAttempt(pLoginData);
		pLoginData->matchingDisplayName = NULL; // don't leave memory we set, as we don't own the struct

		IPRateLimit(pLoginData->ips, IPRLA_AuthenticationFailure);
		callback(NULL, eFailureCode, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If the account has been linked, we don't allow login by Cryptic private account name.
	if (gbPreventLinkedCrypticLogin && eType == ACCOUNTLOGINTYPE_Cryptic && pCrypticAccount->pPWAccountName)
	{
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Command: Login, Account: %s - Cryptic account linked\n", pLoginData->pPrivateAccountName);  // Obsolete log line
		pLoginData->bLinkBlocked = true;
		accountLogLoginAttempt(pLoginData);
		pLoginData->matchingDisplayName = NULL; // don't leave memory we set, as we don't own the struct

		IPRateLimit(pLoginData->ips, IPRLA_AuthenticationFailure);
		callback(NULL, LoginFailureCode_DisabledLinked, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}	
	// Disable of unlinked Cryptic Account logins, except for LoginProtocol_Xmlrpc
	else if (gbDisallowCrypticAndPwLoginType && pLoginData->protocol != LoginProtocol_Xmlrpc && eType == ACCOUNTLOGINTYPE_Cryptic && !pCrypticAccount->pPWAccountName)
	{
		U32 uConflictTicket = 0;
		if (bCreateConflictTicket)
			uConflictTicket = createCrypticConflictTicket(pCrypticAccount);
		accountLogLoginAttempt(pLoginData);
		pLoginData->matchingDisplayName = NULL; // don't leave memory we set, as we don't own the struct
		callback(NULL, LoginFailureCode_CrypticDisabled, uConflictTicket, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (pCrypticAccount && accountMachineLockingIsEnabled(pCrypticAccount) && !machineIDIsValid(pLoginData->pMachineID))
	{
		accountLogLoginAttempt(pLoginData);
		pLoginData->matchingDisplayName = NULL; // don't leave memory we set, as we don't own the struct
		callback(NULL, LoginFailureCode_NotFound, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	pLoginData->bLoginAllowed = true;
	pLoginData->bMachineLocked = !accountIsMachineIDAllowed(pCrypticAccount, pLoginData->pMachineID, 
		pLoginData->protocol == LoginProtocol_AccountNet ? MachineType_CrypticClient : MachineType_WebBrowser, 
		NULL, NULL);

	// Rate limiting for successful logins
	if (accountLogin_RateLimit(IPRLA_AuthenticationSuccess, &eFailureCode, pLoginData))
	{
		callback(NULL, eFailureCode, 0, userData);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Log success.
	pLoginData->bLoginSuccessful = true;
	pLoginData->eLoginType = eType; // Set to the type of the found account
	accountLogLoginAttempt(pLoginData);
	pLoginData->matchingDisplayName = NULL; // don't leave memory we set, as we don't own the struct

	// Login successful
	callback(pCrypticAccount, LoginFailureCode_Ok, 0, userData);
	PERFINFO_AUTO_STOP_FUNC();
}

// Checks Account Guard status for the account and machine ID; also updates access record if 'ip' is non-null
bool accountIsMachineIDAllowed(AccountInfo *pAccount, const char *pMachineID, MachineType eType, const char *ip, LoginFailureCode *failureCode)
{
	if (!pAccount)
		return false;
	if (!accountMachineLockingIsEnabled(pAccount))
		return true;
	if (nullStr(pMachineID))
	{
		if (failureCode)
			*failureCode = LoginFailureCode_BadPassword;
		return false;
	}
	if (!accountIsMachineIDSaved(pAccount, pMachineID, eType, ip))
	{
		if (failureCode)
			*failureCode = LoginFailureCode_NewMachineID;
		return false;
	}
	return true;
}

static void accountLogOneTimeCodeValidation(AccountInfo *pAccount, AccountLoginAttemptData *pData, const char *pOTC, const char *pMachineName)
{
	char *logline = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Add basic information to log line.
	estrStackCreate(&logline);
	logAppendPairs(&logline,
		logPair("machine_id", "%s", pData->pMachineID),
		logPair("onetimecode", "%s", pOTC),
		logPair("machine_name", "%s", pMachineName),
		logPair("success", "%d", !!pData->bLoginSuccessful),
		logPair("good_account", "%d", !!pData->bGoodAccount),
		logPair("login_type", "%s", StaticDefineIntRevLookup(AccountLoginTypeEnum, pData->eLoginType)),
		NULL);

	// Add IP information, if present.
	EARRAY_CONST_FOREACH_BEGIN(pData->ips, i, n);
	{
		char name[2 + 10 + 1];
		sprintf(name, "ip%d", i);
		logAppendPairs(&logline, logPair(name, "%s", pData->ips[i] ? pData->ips[i] : ""), NULL);
	}
	EARRAY_FOREACH_END;

	// Add optional information.
	accountLogLoginAttempt_AppendPairString(&logline, "apriori_reason", pData->aPrioriReason);
	accountLogLoginAttempt_AppendPairString(&logline, "location", pData->location);
	accountLogLoginAttempt_AppendPairString(&logline, "referrer", pData->referrer);
	accountLogLoginAttempt_AppendPairString(&logline, "client_version", pData->clientVersion);
	accountLogLoginAttempt_AppendPairString(&logline, "note", pData->note);
	accountLogLoginAttempt_AppendPairString(&logline, "peer_ip", pData->peerIp);

	// Generate log line.
	objLog(LOG_LOGIN, GLOBALTYPE_ACCOUNT, pAccount ? pAccount->uID : 0, 0, pData->matchingDisplayName, NULL, NULL, "OneTimeCodeValidation", NULL, "%s", logline);
	estrDestroy(&logline);
	PERFINFO_AUTO_STOP();
}

bool processOneTimeCode (AccountInfo *pAccount, AccountLoginAttemptData *pLoginData, 
	const char *pOneTimeCode, const char *pMachineName, LoginFailureCode *failureCode)
{
	MachineType eType;
	const char *ip;
	if (!pAccount || nullStr(pLoginData->pMachineID) || nullStr(pOneTimeCode))
	{
		*failureCode = LoginFailureCode_NotFound;
		return false;
	}

	// TODO(Theo) should this rate limit after generation of one-time code, or only for additional logins?
	/*if (!IPRateLimit(pLoginData->ips, IPRLA_OneTimeCode))
	{
		IPRateLimit(pLoginData->ips, IPRLA_OneTimeCodeFailure);
		*failureCode = LoginFailureCode_RateLimit;
		pLoginData->bRejectedAPriori = true;
		pLoginData->aPrioriReason = RATELIMIT_APRIORI_REASON;
		pLoginData->bIpBlocked = true;
		accountLogOneTimeCodeValidation(pAccount, pLoginData, pOneTimeCode);
		return false;
	}*/

	if (pLoginData->protocol == LoginProtocol_Xmlrpc)
		eType = MachineType_WebBrowser;
	else
		eType = MachineType_CrypticClient;
	pLoginData->matchingDisplayName = pAccount->displayName;
	pLoginData->bGoodAccount = true;
	ip = eaSize(&pLoginData->ips) ?  pLoginData->ips[0] : "";
	ANALYSIS_ASSUME(pLoginData->ips); // hopefully this is right
	if (accountMachineValidateOneTimeCode(pAccount, pLoginData->pMachineID, eType, pOneTimeCode, pMachineName, ip))
	{
		pLoginData->bLoginSuccessful = true;
		*failureCode = LoginFailureCode_Ok;
		IPRateLimit(pLoginData->ips, IPRLA_OneTimeCodeSuccess);
		accountLogOneTimeCodeValidation(pAccount, pLoginData, pOneTimeCode, pMachineName ? pMachineName : "");
		return true;
	}
	else
	{
		*failureCode = LoginFailureCode_BadPassword;
		IPRateLimit(pLoginData->ips, IPRLA_OneTimeCodeFailure);
		accountLogOneTimeCodeValidation(pAccount, pLoginData, pOneTimeCode, pMachineName ? pMachineName : "");
		return false;
	}
}

static void HandleOneTimeCode(Packet *pak, NetLink *link, AccountLink *accountLink)
{
	U32 uAccountID;
	char *pMachineID;
	char *pOTC;
	char *pMachineName = NULL;
	LoginFailureCode failureCode = LoginFailureCode_Ok;
	AccountLoginAttemptData loginData = {0};
	AccountInfo *pAccount;
	U32 uIP;
	static char **ips = NULL;			// Array reused between calls
	const int ip_str_size = 4*4;

	PERFINFO_AUTO_START_FUNC();
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uAccountID = pktGetU32(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pMachineID = pktGetStringTemp(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pOTC = pktGetStringTemp(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pMachineName = pktGetStringTemp(pak);
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uIP = pktGetU32(pak);

	// Format the string IP.
	if (!ips)
	{
		eaSetSize(&ips, 1);
		estrSetSize(&ips[0], ip_str_size);
	}
	GetIpStr(uIP, ips[0], ip_str_size);

	// Populate loginData
	loginData.protocol = LoginProtocol_AccountNet;
	loginData.pMachineID = pMachineID;
	loginData.ips = ips;

	pAccount = findAccountByID(uAccountID);
	if (!pAccount)
		failureCode = LoginFailureCode_InvalidTicket;
	else
		processOneTimeCode(pAccount, &loginData, pOTC, pMachineName, &failureCode);

	{
		Packet *response;
		PERFINFO_AUTO_START("OneTimeCodeResponse", 1);
		response = pktCreate(link, FROM_ACCOUNTSERVER_ONETIMECODEVALIDATE_RESPONSE);
		pktSendU32(response, failureCode);
		pktSendU32(response, uAccountID);
		pktSend(&response);
		PERFINFO_AUTO_STOP();
	}

packetfailed:
	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleSaveNextMachineInfo(Packet *pak, NetLink *link, AccountLink *accountLink)
{
	U32 uAccountID;
	char *pMachineID;
	char *pMachineName = NULL;
	LoginFailureCode failureCode = LoginFailureCode_Ok;
	AccountInfo *pAccount;
	U32 uIP;
	static char ip[4*4];

	PERFINFO_AUTO_START_FUNC();
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uAccountID = pktGetU32(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pMachineID = pktGetStringTemp(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pMachineName = pktGetStringTemp(pak);
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uIP = pktGetU32(pak);

	GetIpStr(uIP, ip, 16);

	pAccount = findAccountByID(uAccountID);
	if (pAccount && accountMachineSaveNextClient(pAccount, timeSecondsSince2000()))
	{
		accountMachineProcessAutosave(pAccount, pMachineID, pMachineName, MachineType_CrypticClient, ip);
	}
	// doesn't send a response

packetfailed:
	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleTicketValidateRequest(Packet* pak, NetLink* link, AccountLink *accountLink)
{
	U32 uID;
	U32 uTicketID;
	AccountInfo *account;
	AccountTicketCache *ticket = NULL;

	PERFINFO_AUTO_START_FUNC();
	// Get packet arguments.
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uID = pktGetU32(pak);
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uTicketID = pktGetU32(pak);

	// Look up account and ticket
	account = findAccountByID(uID);
	ticket = AccountFindTicketByID(account, uTicketID);

	if (account && ticket && ticket->uExpireTime >= timeSecondsSince2000())
	{
		char ipBuf[17];
		char *pTicketText = NULL;
		Packet *response;

		PERFINFO_AUTO_START("Send Ticket", 1);
		// Send successful login response.
		ParserWriteText(&pTicketText, parse_AccountTicketSigned, ticket->ticket, 0, 0, 0);
		response = pktCreate(link, FROM_ACCOUNTSERVER_LOGIN);
		pktSendString(response, pTicketText);
		pktSendU32(response, account->uID);
		pktSend(&response);
		estrDestroy(&pTicketText);
		PERFINFO_AUTO_STOP();

		// Record this login.
		servLog(LOG_ACCOUNT_SERVER_GENERAL, "LoginValidate", "accountname %s accountid %lu ip %s", account->accountName,
			account->uID, linkGetIpStr(link, ipBuf, sizeof(ipBuf)));
		accountReportLogin(account, ticket->uIp);
	}
	else
		SendLoginValidateFailurePacket(link, LoginFailureCode_InvalidTicket, uID);
	if (ticket)
		StructDestroy(parse_AccountTicketCache, ticket); // destroy used ticket

packetfailed:
	PERFINFO_AUTO_STOP_FUNC();
}

void generateOneTimeCode(AccountInfo *account, const char *pMachineID, char **ips, LoginProtocol eLoginProtocol)
{
	const char *ip;

	ip = eaSize(&ips) ? ips[0] : "";
	if (accountMachineGenerateOneTimeCode(account, pMachineID, ip))
	{
		char *logline = NULL;
		IPRateLimit(ips, IPRLA_OneTimeCodeGenerate);

		// Add basic information to log line.
		estrStackCreate(&logline);
		logAppendPairs(&logline,
			logPair("machine_id", "%s", pMachineID),
			logPair("login_protocol", "%s", StaticDefineIntRevLookup(AccountLoginTypeEnum, eLoginProtocol)),
			NULL);

		// Add IP information, if present.
		EARRAY_CONST_FOREACH_BEGIN(ips, i, n);
		{
			char name[2 + 10 + 1];
			sprintf(name, "ip%d", i);
			logAppendPairs(&logline, logPair(name, "%s", ips[i] ? ips[i] : ""), NULL);
		}
		EARRAY_FOREACH_END;

		// Generate log line.
		objLog(LOG_LOGIN, GLOBALTYPE_ACCOUNT, account->uID, 0, account->displayName, NULL, NULL, "OneTimeCodeGenerate", NULL, "%s", logline);
		estrDestroy(&logline);
	}
}

static void HandleGenerateOneTimeCode(Packet* pak, NetLink* link, AccountLink *accountLink)
{
	U32 uID;
	const char *pMachineID;
	AccountInfo *account;
	AccountTicketCache *ticket = NULL;
	U32 uIP;
	static char **ips = NULL;			// Array reused between calls
	const int ip_str_size = 4*4;

	PERFINFO_AUTO_START_FUNC();
	// Get packet arguments.
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uID = pktGetU32(pak);
	PKT_CHECK_STR(pak, packetfailed);
	pMachineID = pktGetStringTemp(pak);
	PKT_CHECK_BYTES(4, pak, packetfailed);
	uIP = pktGetU32(pak);

	// Format the string IP.
	if (!ips)
	{
		eaSetSize(&ips, 1);
		estrSetSize(&ips[0], ip_str_size);
	}
	GetIpStr(uIP, ips[0], ip_str_size);

	// Look up account and ticket
	account = findAccountByID(uID);
	if (devassert(account))
		generateOneTimeCode(account, pMachineID, ips, LoginProtocol_AccountNet);

packetfailed:
	PERFINFO_AUTO_STOP_FUNC();
}

// Generate a fixed salt for a given login field
static void GenerateFixedSalt(SA_PARAM_NN_STR const char * pLoginField,
							  SA_PARAM_NN_STR char * pOutFixedSalt, size_t pOutFixedSaltLength)
{
	// This function is meant to generate a security code that looks indistinguishable from
	// those generated by PWE. The output should always be the same for a given login field.

	static const char pSymbolTable[] = "ABCDEFGHIGKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	static const size_t szSymbolTableLength = sizeof(pSymbolTable) - 1;
	U32 uHash = MurmurHash2CaseInsensitive(pLoginField, (U32)strlen(pLoginField), 0xf00ba0);
	int i = 0;

	memset(pOutFixedSalt, 0, pOutFixedSaltLength);

	// srand/rand use TLS with the multi-threaded CRT in Windows
	// in posix, rand_r should be used
	srand(uHash);
	for (i = 0; i < 6; i++)
	{
		pOutFixedSalt[i] = pSymbolTable[rand()%szSymbolTableLength];
	}
}

// Return a fixed salt for a given login field (always the same; possibly bogus)
static void GetFixedSalt(SA_PARAM_NN_STR const char * pLoginField,
						 SA_PARAM_NN_STR char * pOutFixedSalt, size_t pOutFixedSaltLength)
{
	PWCommonAccount * pPWStub = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	pPWStub = findPWCommonAccountByLoginField(pLoginField);
	if (pPWStub && !nullStr(pPWStub->pFixedSalt))
	{
		strcpy_s(pOutFixedSalt, pOutFixedSaltLength, pPWStub->pFixedSalt);
	}
	else
	{
		GenerateFixedSalt(pLoginField, pOutFixedSalt, pOutFixedSaltLength);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void HandleRequestLoginSalt(Packet * pak, NetLink * link, AccountLink * accountLink)
{
	Packet *response = NULL;
	AccountNetStruct_FromAccountServerLoginSalt sendStruct = {0};

	if (!pktEnd(pak))
	{
		AccountSaltRequest request = {0};

		if (ParserReceiveStructAsCheckedNameValuePairs(pak, parse_AccountSaltRequest, &request))
		{
			strcpy(accountLink->loginField, request.pLoginField);
			GetFixedSalt(request.pLoginField, accountLink->fixedSalt, ARRAY_SIZE(accountLink->fixedSalt));
			StructDeInit(parse_AccountSaltRequest, &request);
		}
	}

	accountLink->temporarySalt = cryptSecureRand();

	sendStruct.iSalt = accountLink->temporarySalt;

	if (!nullStr(accountLink->fixedSalt))
	{
		sendStruct.eSaltType = LOGINSALTTYPE_SALT_WITH_FIXED_THEN_SHORT_TERM_SALT;
		strcpy(sendStruct.pFixedSalt, accountLink->fixedSalt);
	}
	else
	{
		sendStruct.eSaltType = LOGINSALTTYPE_SALT_WITH_ACNTNAME_THEN_SHORT_TERM_SALT;
		sendStruct.pFixedSalt[0] = '\0';
	}

	response = pktCreate(link, FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT);
	ParserSendStructAsCheckedNameValuePairs(response, parse_AccountNetStruct_FromAccountServerLoginSalt, &sendStruct);
	pktSend(&response);
}

static void AccountServerHandleMessageSwitch(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	switch(cmd)
	{
		xcase TO_ACCOUNTSERVER_LOGINVALIDATE:
			HandleTicketValidateRequest(pak, link, accountLink);
		xcase TO_ACCOUNTSERVER_GENERATE_ONETIMECODE:
			HandleGenerateOneTimeCode(pak, link, accountLink);
		xcase TO_ACCOUNTSERVER_LOGIN_MACHINEID:
			HandleLoginMachineID(pak, cmd, link, accountLink);
		xcase TO_ACCOUNTSERVER_ONETIMECODE:
			HandleOneTimeCode(pak, link, accountLink);
		xcase TO_ACCOUNTSERVER_SAVENEXTMACHINE:
			HandleSaveNextMachineInfo(pak, link, accountLink);
		xcase TO_ACCOUNTSERVER_REQUEST_LOGINSALT:
			HandleRequestLoginSalt(pak, link, accountLink);
			break;

		// Product Key Generation commands
		case TO_ACCOUNTSERVER_GET_PKGROUPS:
		case TO_ACCOUNTSERVER_CREATE_KEYBATCH:
		case TO_ACCOUNTSERVER_ADDKEYS:
			if (ipfIsLocalIp(linkGetIp(link)))
				HandleMessageFromKeyGen(pak, cmd, link, accountLink);
			break;

		xcase TO_ACCOUNTSERVER_DISPLAYNAME_CHANGE:
			{
				if (LoginServerLinkIsSecure(link))
				{
					U32 uAccountID;
					AccountInfo *account;
					char *displayName;
					DisplayNameMessageStruct *data;

					PKT_CHECK_BYTES(4, pak, packetfailed);
					uAccountID = pktGetU32(pak);
					PKT_CHECK_STR(pak, packetfailed);
					displayName = pktGetStringTemp(pak);

					account = findAccountByID(uAccountID);

					data = StructCreate (parse_DisplayNameMessageStruct);
					data->displayName = StructAllocString(displayName);
					data->uAccountID = uAccountID;
					data->link = link;

					if (changeAccountDisplayName(account, displayName, trChangeNameMessage_CB, data))
					{
						eaPush(&sppDisplayNameChangeQueue, data);
					}
					else
					{
						if (linkConnected(link))
						{
							Packet *response = pktCreate(link, FROM_ACCOUNTSERVER_INVALID_NAME);
							pktSendString(response, "Unknown error");
							pktSend(&response);
						}
						StructDestroy(parse_DisplayNameMessageStruct, data);
					}
				}
			}

		// Logging of system spects
		xcase TO_ACCOUNTSERVER_LOGSPECS:
			{
				U32 uAccountID;
				char *systemSpecs;
				AccountInfo *account;

				PKT_CHECK_BYTES(4, pak, packetfailed);
				uAccountID = pktGetU32(pak);
				PKT_CHECK_STR(pak, packetfailed);
				systemSpecs = pktGetStringTemp(pak);

				account = findAccountByID(uAccountID);
				if(account)
					objLog(LOG_CLIENT_PERF, GLOBALTYPE_NONE, 0, 0, account->accountName, NULL, NULL, "SystemSpecs", NULL, "%s", systemSpecs);
			}

		// Request Account Server version
		xcase TO_ACCOUNTSERVER_VERSION:
			if (ipfIsLocalIp(linkGetIp(link)) || ipfIsTrustedIp(linkGetIp(link)) && linkConnected(link))
			{
				Packet *response = pktCreate(link, FROM_ACCOUNTSERVER_VERSION);
				pktSendString(response, GetUsefulVersionString());
				pktSend(&response);
			}
		xcase TO_ACCOUNTSERVER_NVPSTRUCT_LOGIN  :
			HandleLoginWithNVPStruct(pak, cmd, link, accountLink);
		xdefault:
			break;
	}

packetfailed:
	return;
}

static void AccountServerHandleMessage(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	bool bHandled = false;

	static StaticCmdPerf cmdPerf[TO_ACCOUNTSERVER_MAX];

	// Record packet processing performance.
	PERFINFO_AUTO_START_FUNC();
	if (cmd >= 0 && cmd < ARRAY_SIZE(cmdPerf))
	{
		if(!cmdPerf[cmd].name)
		{
			char buffer[100];
			sprintf(buffer, "Cmd:%s (%d)", StaticDefineIntRevLookupNonNull(AccountServerCmdEnum, cmd), cmd);
			cmdPerf[cmd].name = strdup(buffer);
		}
		PERFINFO_AUTO_START_STATIC(cmdPerf[cmd].name, &cmdPerf[cmd].pi, 1);
	}
	else
		PERFINFO_AUTO_START("Cmd:Unknown", 1);

	// Throw away excessively-large packets.
	if (pktGetSize(pak) > 50*1024*1024)
	{
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}

	AccountServerHandleMessageSwitch(pak, cmd, link, accountLink);

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

static void AccountServerHandleTrustedMessageSwitch(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	bool bHandled = false;

	switch(cmd)
	{
		case TO_ACCOUNTSERVER_TRUSTED_SET_REQUEST_IP:
			accountLink->ipRequest = pktGetU32(pak);
			bHandled = true;
			break;
		case TO_ACCOUNTSERVER_TRUSTED_SET_SALT:
			accountLink->temporarySalt = pktGetU32(pak);
			bHandled = true;
			break;
	}

	if(!bHandled)
	{
		AccountServerHandleMessageSwitch(pak, cmd, link, accountLink);
	}
}

static void AccountServerHandleTrustedMessage(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	bool bHandled = false;

	static StaticCmdPerf cmdPerf[TO_ACCOUNTSERVER_MAX];

	// Record packet processing performance.
	PERFINFO_AUTO_START_FUNC();
	if (cmd >= 0 && cmd < ARRAY_SIZE(cmdPerf))
	{
		if(!cmdPerf[cmd].name)
		{
			char buffer[100];
			sprintf(buffer, "Cmd:%s (%d)", StaticDefineIntRevLookupNonNull(AccountServerCmdEnum, cmd), cmd);
			cmdPerf[cmd].name = strdup(buffer);
		}
		PERFINFO_AUTO_START_STATIC(cmdPerf[cmd].name, &cmdPerf[cmd].pi, 1);
	}
	else
		PERFINFO_AUTO_START("Cmd:Unknown", 1);

	// Throw away excessively-large packets.
	if (pktGetSize(pak) > 50*1024*1024)
	{
		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}

	AccountServerHandleTrustedMessageSwitch(pak, cmd, link, accountLink);

	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}


static char sImportFilePath[MAX_PATH] = "";
// Import a database file;
// File should be a CSV file of "username,email,password hash" with no empty values and no table header
// Entries with usernames or emails that have already been taken will be discarded
AUTO_COMMAND ACMD_NAME(import) ACMD_CMDLINE;
void importCSVDatabase(const char *filePath)
{
	strcpy(sImportFilePath, filePath);
}

void runImportCSVDatabase(const char *filePath)
{
	char *fullPath = NULL;
	char *mem, *s = NULL, *data;
	int failcount = 0;

	loadstart_printf("Importing Database <%s>... \n", filePath);
	if (!fileIsAbsolutePath(filePath))
	{
		estrConcatf(&fullPath, "%s%s", dbAccountDataDir(), filePath);
	}
	else
	{
		estrCopy2(&fullPath, filePath);
	}

	mem = fileAlloc(fullPath, 0);
	if (!mem)
	{
		Errorf("Failed to load account data file");
		loadend_printf("Failed.");
		return;
	}
	data = strtok_s(mem, "\r\n", &s);

	while (data)
	{
		char *curLine = NULL;
		char *name, *password_md5, *email;
		AccountInfo *account;
		int len;

		name = strtok_s(data, ",", &curLine);
		email = strtok_s(NULL, ",", &curLine);
		password_md5 = strtok_s(NULL, ",", &curLine);

		if (name)
		{
			if (*name == '"')
				name++;
			len = (int)strlen(name);
			if (len > 0 && *(name+len-1) == '"')
				*(name+len-1) = 0;
		}
		if (email)
		{
			if (*email == '"')
				email++;
			len = (int)strlen(email);
			if (len > 0 && *(email+len-1) == '"')
				*(email+len-1) = 0;
		}
		if (password_md5)
		{
			if (*password_md5 == '"')
				password_md5++;
			len = (int)strlen(password_md5);
			if (len > 0 && *(password_md5+len-1) == '"')
				*(password_md5+len-1) = 0;
		}
		

		if (name && password_md5 && email)
		{
			char ipBuf[17];
			STRING_EARRAY ips = NULL;
			ANALYSIS_ASSUME(password_md5);
			ANALYSIS_ASSUME(name);
			ANALYSIS_ASSUME(email);
			eaPush(&ips, GetIpStr(getHostLocalIp(), ipBuf, sizeof(ipBuf)));
			if (createFullAccount(name, password_md5, name, email, NULL, NULL, NULL, NULL, NULL, ips, false, false, 9))
			{
				// TODO?
				account = findAccountByName(name);
			}
			else
				account = NULL;
			eaDestroy(&ips);
			if (!account)
			{
				log_printf(LOG_ACCOUNT_SERVER_GENERAL, "DB Import Entry Failed, Account Name: %s, Email: %s\n", name, email);
				printf ("\t%s,%s,%s failed\n", name, password_md5, email);
				failcount++;
			}
		}
		data = strtok_s(NULL, "\r\n", &s);
	}
	printf("\tTotal Failed: %d\n", failcount);

	free(mem);
	loadend_printf("Done.");
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppdeprecateddistributedproductkeys, .Ppdistributedkeys");
enumTransactionOutcome trAccountMigrateDistributedKeys(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->ppDeprecatedDistributedProductKeys, iCurDeprecated, iNumDeprecated);
	{
		char *pKey = pAccount->ppDeprecatedDistributedProductKeys[iCurDeprecated];
		NOCONST(DistributedKeyContainer) *pDistributedKey = NULL;

		if (!devassert(pKey && *pKey)) continue;

		pDistributedKey = StructCreateNoConst(parse_DistributedKeyContainer);

		if (!devassert(pDistributedKey)) continue;

		pDistributedKey->pActivationKey = strdup(pKey);
		pDistributedKey->uDistributedTimeSS2000 = 0; // We don't know what it should be.

		eaPush(&pAccount->ppDistributedKeys, pDistributedKey);
	}
	EARRAY_FOREACH_END;

	// Leave the deprecated ones there in case of a DB revert.
	//eaDestroy(&pAccount->ppDeprecatedDistributedProductKeys);
	//pAccount->ppDeprecatedDistributedProductKeys = NULL;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppproducts");
enumTransactionOutcome trAccountScrubInvalidProducts(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;

	EARRAY_FOREACH_REVERSE_BEGIN(pAccount->ppProducts, iCurProduct);
	{
		NOCONST(AccountProductSub) *pProductSub = pAccount->ppProducts[iCurProduct];
		const ProductContainer *pProduct = NULL;

		if (!pProductSub)
		{
			eaRemove(&pAccount->ppProducts, iCurProduct);
			continue;
		}

		pProduct = findProductByName(pProductSub->name);

		if (!pProduct)
		{
			StructDestroyNoConst(parse_AccountProductSub, eaRemove(&pAccount->ppProducts, iCurProduct));
		}
	}
	EARRAY_FOREACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pcachedsubscriptionlist");
enumTransactionOutcome trAccountMarkInvalidCancelled(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;

	if (!pAccount->pCachedSubscriptionList) return TRANSACTION_OUTCOME_SUCCESS;

	EARRAY_CONST_FOREACH_BEGIN(pAccount->pCachedSubscriptionList->ppList, iCurCachedSub, iNumCachedSubs);
	{
		NOCONST(CachedAccountSubscription) *pCachedSub = pAccount->pCachedSubscriptionList->ppList[iCurCachedSub];

		if (!devassert(pCachedSub)) continue;

		if (pCachedSub->vindiciaStatus == SUBSCRIPTIONSTATUS_INVALID)
		{
			pCachedSub->vindiciaStatus = SUBSCRIPTIONSTATUS_CANCELLED;
		}
	}
	EARRAY_FOREACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

bool gbClearPlayTimes = 0;
AUTO_CMD_INT(gbClearPlayTimes, ClearPlayTime) ACMD_CMDLINE;

bool gbScrubInvalidProductsFromAccounts = false;
AUTO_CMD_INT(gbScrubInvalidProductsFromAccounts, ScrubInvalidProductsFromAccounts) ACMD_CMDLINE;

bool gbMarkInvalidCancelled = false;
AUTO_CMD_INT(gbMarkInvalidCancelled, MarkInvalidCancelled) ACMD_CMDLINE;

bool gbFixSubHistory = false;
AUTO_CMD_INT(gbFixSubHistory, FixSubHistory) ACMD_CMDLINE;

// Force all accounts to rebucket their activity logs
bool gbForceRebucketLogs = false;
AUTO_CMD_INT(gbForceRebucketLogs, ForceRebucketLogs) ACMD_CMDLINE;

// Perform a one-time lock of all accounts created before a specified timestamp - BE SURE TO ONLY USE ONCE
U32 giOneTimeLockAccountsBeforeCutoff = 0;
AUTO_CMD_INT(giOneTimeLockAccountsBeforeCutoff, OneTimeLockAccountsBeforeCutoff) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// Perform a mass change of passwords with a provided file of password hashes
char gcMassPasswordChangeFile[CRYPTIC_MAX_PATH] = {0};
AUTO_CMD_STRING(gcMassPasswordChangeFile, MassPasswordChange) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// ONE TIME FIXUP - activate Zen conversion
bool gbDoZenConversion = false;
AUTO_CMD_INT(gbDoZenConversion, DoZenConversion) ACMD_CMDLINE;

U32 giPrecalculatePermissionsThresholdDays = 30;
AUTO_CMD_INT(giPrecalculatePermissionsThresholdDays, PrecalculatePermissionsThresholdDays) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

static void scanAccounts(void)
{
	U32 uTotalAccountsUpdated = 0;
	FILE *fLockedAccounts = NULL;
	StashTable sLockAccountNameExclusions = NULL;
	char lockExcludeFile[CRYPTIC_MAX_PATH];

	// For mass password change feature
	bool bDoingMassPasswordChange = false;
	StashTable sMassPasswordChanges = NULL;

	// ONE TIME FIXUP declarations
	StashTable *psZenFixupAccountLists = NULL;
	CONST_EARRAY_OF(ZenKeyConversion) eaConversions = GetZenKeyConversions();

	PERFINFO_AUTO_START_FUNC();

	loadstart_printf("Scanning accounts... ");

	dbEnableDBLogCacheHint();

	if (giOneTimeLockAccountsBeforeCutoff)
	{
		fLockedAccounts = fopen(STACK_SPRINTF("%s%s", dbAccountDataDir(), "lockedAccounts.csv"), "wt");
		sprintf(lockExcludeFile, "%s%s", dbAccountDataDir(), "lockExclude.txt");

		if (fileExists(lockExcludeFile))
		{
			char **ppNames = NULL;
			char *pRawText = NULL;
			int len = 0;

			sLockAccountNameExclusions = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);

			pRawText = fileAlloc(lockExcludeFile, &len);
			DivideString(pRawText, "\n", &ppNames, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_ESTRINGS);
			fileFree(pRawText);

			EARRAY_FOREACH_BEGIN(ppNames, i);
			{
				char *pName = ppNames[i];
				stashAddInt(sLockAccountNameExclusions, pName, 1, false);
				estrDestroy(&pName);
			}
			EARRAY_FOREACH_END;

			eaDestroy(&ppNames);
		}
	}

	if (gcMassPasswordChangeFile[0] && fileExists(gcMassPasswordChangeFile))
	{
		char **ppLines = NULL;
		char *pRawText = NULL;
		int len = 0;

		bDoingMassPasswordChange = true;
		sMassPasswordChanges = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);

		pRawText = fileAlloc(gcMassPasswordChangeFile, &len);
		DivideString(pRawText, "\n", &ppLines, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_ESTRINGS);
		fileFree(pRawText);

		EARRAY_FOREACH_BEGIN(ppLines, i);
		{
			char **ppFields = NULL;
			DivideString(ppLines[i], ",", &ppFields, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_ESTRINGS);

			if (!devassertmsgf(eaSize(&ppFields) == 2, "Wrong number of fields in password change file, line %d (fields: %d)", i, eaSize(&ppFields)))
			{
				eaDestroyEString(&ppFields);
				continue;
			}

			stashAddPointer(sMassPasswordChanges, ppFields[0], ppFields[1], true);
			estrDestroy(&ppFields[0]);
			eaDestroy(&ppFields); // Specifically destroying the array without freeing, because ppFields[1] is in use in the StashTable; it will be freed later
		}
		EARRAY_FOREACH_END;

		eaDestroyEString(&ppLines);
	}

/*
	if (gbDoZenConversion)
	{
		psZenFixupAccountLists = calloc(eaSize(&eaConversions), sizeof(StashTable));

		EARRAY_CONST_FOREACH_BEGIN(eaConversions, iConversion, iNumConversions);
		{
			if (eaConversions[iConversion]->pAccountList)
			{
				char **ppNames = NULL;
				char *pListText = NULL;
				int len = 0;

				pListText = fileAlloc(eaConversions[iConversion]->pAccountList, &len);
				if (!pListText) continue;

				DivideString(pListText, "\r\n", &ppNames, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_ESTRINGS);
				fileFree(pListText);
				psZenFixupAccountLists[iConversion] = stashTableCreateWithStringKeys(8, StashDeepCopyKeys);

				EARRAY_FOREACH_BEGIN(ppNames, i);
				{
					char *pName = ppNames[i];
					stashAddInt(psZenFixupAccountLists[iConversion], pName, 1, false);
					estrDestroy(&pName);
				}
				EARRAY_FOREACH_END;

				eaDestroy(&ppNames);
			}
		}
		EARRAY_FOREACH_END;
	}
*/

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNT, accountContainer);
	{
		AccountInfo *account = (AccountInfo *) accountContainer->containerData;
		bool bUpdated = false;

		if (!devassert(account)) continue;

		autoTimerThreadFrameBegin(__FUNCTION__);

		if (!account->displayName)
		{
			changeAccountDisplayName(account, account->accountName, NULL, NULL);
			bUpdated = true;
		}

		if (gbRegenerateGUIDs || !account->globallyUniqueID || !*account->globallyUniqueID)
		{
			accountRegenerateGUID(account->uID);
			bUpdated = true;
		}

		if (account->ppDeprecatedDistributedProductKeys && eaSize(&account->ppDeprecatedDistributedProductKeys) &&
			!eaSize(&account->ppDistributedKeys))
		{
			AutoTrans_trAccountMigrateDistributedKeys(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID);
			bUpdated = true;
		}

		if (gbClearPlayTimes)
		{
			objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, account->uID, "DestroyPlaytimes", "destroy playtimes");
			bUpdated = true;
		}

		if (gbScrubInvalidProductsFromAccounts)
		{
			int iNumProductsBefore = eaSize(&account->ppProducts);

			AutoTrans_trAccountScrubInvalidProducts(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID);

			if (iNumProductsBefore != eaSize(&account->ppProducts))
				bUpdated = true;
		}

		if (gbMarkInvalidCancelled)
		{
			AutoTrans_trAccountMarkInvalidCancelled(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID);
			bUpdated = true;
		}

		if (account->ppKeyValuePairs)
		{
			EARRAY_FOREACH_REVERSE_BEGIN(account->ppKeyValuePairs, iCurKeyValue);
			{
				AccountKeyValuePair *pPair = account->ppKeyValuePairs[iCurKeyValue];

				if (!devassert(pPair)) continue;

				if (pPair->sValue_Obsolete)
				{
					AccountKeyValue_MigrateLegacyStorage(account);
					bUpdated = true;
					break;
				}
			}
			EARRAY_FOREACH_END;
		}

		if (gbFixSubHistory)
		{
			accountRecalculateAllArchivedSubHistory(account->uID);
			bUpdated = true;
		}

		if (((gbForceRebucketLogs && StayUpCount() <= 1) || !isAccountUsingIndexedLogs(account)) && (account->flags & ACCOUNT_FLAG_LOGS_REBUCKETED))
		{
			AutoTrans_trAccountClearFlags(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID, ACCOUNT_FLAG_LOGS_REBUCKETED);
			bUpdated = true;
		}

		if (!(account->flags & ACCOUNT_FLAG_LOGS_REBUCKETED))
		{
			queueAccountForRebucketing(account);
		}

		if (!account->uCreatedTime)
		{
			U32 uTimestamp = 0;

			if (eaiSize(&account->eauIndexedLogBatchIDs) > 0)
			{
				uTimestamp = getFirstAccountLogTimestampFromBatch(account->eauIndexedLogBatchIDs[0]);
			}

			if (eaiSize(&account->eauLogBatchIDs) > 0)
			{
				uTimestamp = getFirstAccountLogTimestampFromBatch(account->eauLogBatchIDs[0]);
			}

			if (!uTimestamp && eaSize(&account->ppLogEntries) > 0)
			{
				uTimestamp = account->ppLogEntries[0]->uSecondsSince2000;
			}

			if (!uTimestamp && eaSize(&account->ppLogEntriesBuffer) > 0)
			{
				uTimestamp = account->ppLogEntriesBuffer[0]->uSecondsSince2000;
			}

			if (uTimestamp)
			{
				objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, account->uID, "SetCreatedTime", "set uCreatedTime = \"%d\"", uTimestamp);
			}
			else
			{
				ErrorOrCriticalAlert("ACCT_NO_CREATION_TIMESTAMP", "Account %u couldn't have its created timestamp set, because it had no activity logs!", account->uID);
			}

			bUpdated = true;
		}

		if (bDoingMassPasswordChange)
		{
			char *pNewHash = NULL;

			if (stashRemovePointer(sMassPasswordChanges, account->accountName, &pNewHash))
			{
				AutoTrans_trForceSetPasswordHash(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, account->uID, pNewHash);
				account->passwordInfo->password_ForRAM[0] = 0;
				estrDestroy(&pNewHash);
				bUpdated = true;
			}
		}

		if (giOneTimeLockAccountsBeforeCutoff &&
			(account->uCreatedTime && account->uCreatedTime < giOneTimeLockAccountsBeforeCutoff) &&
			account->uPasswordChangeTime < giOneTimeLockAccountsBeforeCutoff &&
			(!sLockAccountNameExclusions || !stashFindInt(sLockAccountNameExclusions, account->accountName, NULL)))
		{
			const char *lockPassChars = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
			const int numPassChars = 62;
			int i, iChar = 0;
			char newPass[24] = {0};
			char newHash[MAX_PASSWORD] = {0};

			for (i = 0; i < 24; ++i)
			{
				iChar = randInt(numPassChars);
				newPass[i] = lockPassChars[iChar];
			}

			accountHashPassword(newPass, newHash);
			requestPasswordChangeTransaction(account, newHash);
			if (fLockedAccounts)
				fprintf(fLockedAccounts, "\"%s\",\"%s\",\"%s\"\n", account->accountName, account->displayName, account->personalInfo.email);

			bUpdated = true;
		}

/*
		if (gbDoZenConversion)
		{
			EARRAY_CONST_FOREACH_BEGIN(eaConversions, iConversion, iNumConversions);
			{
				ZenKeyConversion *pConversion = eaConversions[iConversion];

				if (getKeyValueChain(pConversion->pOldKey) || getKeyValueChain(pConversion->pNewKey)) return;
				if (pConversion->pAccountList && !stashFindInt(psZenFixupAccountLists[iConversion], account->accountName, NULL)) continue;

				if (accountConvertKeyToZen(account, pConversion->pOldKey, pConversion->pNewKey))
					bUpdated = true;
			}
			EARRAY_FOREACH_END;
		}
*/

		if (timeSecondsSince2000() - account->loginData.uTime <= giPrecalculatePermissionsThresholdDays * SECONDS_PER_DAY)
		{
			accountConstructPermissions(account);
		}

		if (gbMachineLockResetToUnknown && account->eMachineLockState != AMLS_Unknown)
		{
			objRequestTransactionSimplef(NULL, GLOBALTYPE_ACCOUNT, account->uID, "SetMachineLockState", "set eMachineLockState = \"%d\"", AMLS_Unknown);
			bUpdated = true;
		}

		if (bUpdated)
		{
			uTotalAccountsUpdated++;
			UpdateObjectTransactionManager();
		}

		autoTimerThreadFrameEnd();
	}
	CONTAINER_FOREACH_END;

	dbDisableDBLogCacheHint();

	if (fLockedAccounts)
		fclose(fLockedAccounts);

	if (sLockAccountNameExclusions)
		stashTableDestroy(sLockAccountNameExclusions);

/*
	if (gbDoZenConversion)
	{
		EARRAY_CONST_FOREACH_BEGIN(eaConversions, i, n);
		{
			if (psZenFixupAccountLists[i])
				stashTableDestroy(psZenFixupAccountLists[i]);
		}
		EARRAY_FOREACH_END;

		free(psZenFixupAccountLists);
	}
*/

	if (bDoingMassPasswordChange)
	{
		if (stashGetCount(sMassPasswordChanges))
		{
			FILE *fMissedPasswordChanges = fopen(STACK_SPRINTF("%s%s", dbAccountDataDir(), "missedPasswordChanges.csv"), "wt");
			FOR_EACH_IN_STASHTABLE2(sMassPasswordChanges, pElement)
			{
				char *pKey = stashElementGetStringKey(pElement);
				char *pValue = stashElementGetPointer(pElement);
				fprintf(fMissedPasswordChanges, "\"%s\",\"%s\"", pKey, pValue);
				estrDestroy(&pValue);
			}
			FOR_EACH_END

			fclose(fMissedPasswordChanges);
			AssertOrAlert("MASS_PASSWORD_CHANGE_OMISSIONS", "During mass password fixup, some keys in the fixup CSV were not used. See accountdb\\missedPasswordChanges.csv for which rows were skipped.");
		}

		stashTableDestroy(sMassPasswordChanges);
		AutoTrans_trSetPasswordVersion(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, eCurPasswordVersion);
		UpdateObjectTransactionManager();                                                                 
	}

	loadend_printf("%u accounts updated.", uTotalAccountsUpdated);

	PERFINFO_AUTO_STOP_FUNC();
}

static char gAccountDBLogFilename[MAX_PATH];
static bool gbCacheDBLog = false;
static char * gpCachedDBLog = NULL;

static void dbSetLogFileName(void)
{
	if (!gAccountDBLogFilename[0])
	{
		sprintf(gAccountDBLogFilename,"%s/DB.log",dbAccountDataDir());			
		logSetFileOptions_Filename(gAccountDBLogFilename,true,0,0,1);
	}
}

static void dbWriteDBLogCache(void)
{
	dbSetLogFileName();
	logDirectWrite(gAccountDBLogFilename, gpCachedDBLog);
	logFlushFile(gAccountDBLogFilename);
	estrClear(&gpCachedDBLog);
}

static void dbLogRotateCB(char *baseHog, char *oldIncrementalHog, char *newIncrementalHog)
{
	char *fname_orig = newIncrementalHog;
	char fname_final[MAX_PATH];
	char *dotStart = FindExtensionFromFilename(fname_orig);

	if(gAccountDBLogFilename[0])
	{
		char buf[MAX_PATH];
		char *ext = NULL;

		if(gbCacheDBLog)
		{
			dbWriteDBLogCache();
		}
		else
		{
			logFlushFile(gAccountDBLogFilename);
		}

		//rename the file to .log
		strcpy(buf, gAccountDBLogFilename);
		ext = FindExtensionFromFilename(buf);
		ext[0] = '\0';
		strcat(buf, ".log");

		logFlushAndRenameFile(gAccountDBLogFilename, buf);	
	}

	strncpy(fname_final,fname_orig,dotStart - fname_orig);
	strcat(fname_final,".lcg");

	strcpy(gAccountDBLogFilename, fname_final);
	logSetFileOptions_Filename(gAccountDBLogFilename,true,0,0,1);
}

static bool gbAllowDBLogCache = true;
AUTO_CMD_INT(gbAllowDBLogCache, AllowDBLogCache) ACMD_CMDLINE;

static void dbEnableDBLogCacheHint(void)
{
	PERFINFO_AUTO_START_FUNC();

	gbCacheDBLog = gbAllowDBLogCache;
	if(gpCachedDBLog)
	{
		estrClear(&gpCachedDBLog);
	}
	else if(gbCacheDBLog)
	{
		estrCreate(&gpCachedDBLog);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void dbDisableDBLogCacheHint(void)
{
	PERFINFO_AUTO_START_FUNC();

	gbCacheDBLog = false;
	if (estrLength(&gpCachedDBLog) > 0)
	{
		dbWriteDBLogCache();
	}

	PERFINFO_AUTO_STOP_FUNC();
}

static void dbLogTransaction(const char *commitString, U64 trSeq, U32 timestamp)
{
	static char *logString;
	static char timeString[128];
	size_t size = strlen(commitString);

	PERFINFO_AUTO_START("accountdbLogTransaction",1);

	estrClear(&logString);

	timeMakeDateStringFromSecondsSince2000(timeString,timestamp);
	estrPrintf(&logString,"%"FORM_LL"u %s: %s\n",trSeq, timeString,commitString);

	if (gbCacheDBLog)
	{
		estrAppend(&gpCachedDBLog, &logString);
		if (estrLength(&gpCachedDBLog) > 1024 * 1024) {
			dbWriteDBLogCache();
		}
	}
	else
	{
		dbSetLogFileName();
		logDirectWrite(gAccountDBLogFilename,logString);
	}

	PERFINFO_AUTO_STOP();
}
CmdList gAccountdbUpdateCmdList = {1,0,0};
static void accountdbHandleDatabaseUpdateStringEx(const char *cmd_orig, U64 sequence, U32 timestamp, bool replay)
{
	int result = 0;
	static CmdContext context = {0};
	static char *message;
	static char *cmd_start;
	char *lineBuffer, *cmd;
	PERFINFO_AUTO_START("accountdbHandleDatabaseUpdateString",1);

	context.output_msg = &message;
	context.access_level = 9;
	context.multi_line = true;

	estrCopy2(&cmd_start, cmd_orig);
	cmd = cmd_start;

	if (!timestamp)
	{
		timestamp = timeSecondsSince2000();
	}
	if (!sequence)
	{
		sequence = objContainerGetSequence() + 1;
	}
	objContainerSetSequence(sequence);
	objContainerSetTimestamp(timestamp);

	if (!replay) dbLogTransaction(cmd, sequence, timestamp);
	while (cmd)
	{
		lineBuffer = cmdReadNextLine(&cmd);
		result = cmdParseAndExecute(&gAccountdbUpdateCmdList,lineBuffer,&context);
		if (result)
		{
			S64 val;
			bool valid = false;
			val = MultiValGetInt(&context.return_val,&valid);
			if (val && valid)
			{
				result = 1;
			}
			else
			{
				result = 0;
			}
		}
		if (!result)
		{
			AssertOrAlert("ACCOUNTDB_APPLY", "Error \"%s\" while executing DBUpdateCommand: %s",message,lineBuffer);
		}
	}
	PERFINFO_AUTO_STOP();
}
static void accountdbUpdateCB(TransDataBlock ***pppBlocks, void *pUserData)
{
	int i;
	for (i=0; i < eaSize(pppBlocks); i++)
	{
		char *pString = (*pppBlocks)[i]->pString1;
		if (pString)
		{
			accountdbHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}

		pString =  (*pppBlocks)[i]->pString2;
		if (pString)
		{
			accountdbHandleDatabaseUpdateStringEx(pString, 0, 0, false);
		}

	}
}

AUTO_COMMAND ACMD_LIST(gAccountdbUpdateCmdList);
int dbUpdateContainer(U32 containerType, U32 containerID, ACMD_SENTENCE diffString)
{
	Container *pObject;
	if (!containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbUpdateContainer",1);
	verbose_printf("Updating data of container %s[%d] using diff %s\n",GlobalTypeToName(containerType),containerID,diffString);

	pObject = objGetContainer(containerType,containerID);

	if (pObject)
	{
		objModifyContainer(pObject,diffString);
	}
	else
	{
		if (!objAddToRepositoryFromString(containerType,containerID,diffString))
		{
			Errorf("Couldn't add to repository");
			PERFINFO_AUTO_STOP();
			return 0;
		}
	}
	PERFINFO_AUTO_STOP();
	return 1;
}
// Permanently destroys a container
AUTO_COMMAND ACMD_LIST(gAccountdbUpdateCmdList);
int dbDestroyContainer(char *containerTypeName, U32 containerID)
{
	GlobalType containerType = NameToGlobalType(containerTypeName);
	if (!containerType)
	{
		return 0;
	}
	PERFINFO_AUTO_START("dbDestroyContainer",1);

	verbose_printf("Permanently destroying container %s[%d]\n",containerTypeName,containerID);
	objRemoveContainerFromRepository(containerType,containerID);

	PERFINFO_AUTO_STOP();
	return 1;
}

void accountdbHandleDatabaseReplayString(const char *cmd_orig, U64 sequence, U32 timestamp)
{
	accountdbHandleDatabaseUpdateStringEx(cmd_orig, sequence, timestamp, true);
}

#define ACCOUNTSERVER_HOG_BUFFER_SIZE 1024*1024*1024

const DatabaseConfig *GetDatabaseConfig(void)
{
	static DatabaseConfig *pDatabaseConfig = NULL;
	char gDBConfigFileName[CRYPTIC_MAX_PATH] = "server/AccountServer/DBConfig.txt";

	if (pDatabaseConfig)
	{
		return pDatabaseConfig;
	}

	pDatabaseConfig = StructCreate(parse_DatabaseConfig);

	//load the config
	if (fileExists(gDBConfigFileName))
	{
		ParserReadTextFile(gDBConfigFileName, parse_DatabaseConfig, pDatabaseConfig, 0);

		pDatabaseConfig->IOConfig = objGetContainerIOConfig(pDatabaseConfig->IOConfig);
	}

	//clean up config defaults
	if (!pDatabaseConfig->iSnapshotInterval) pDatabaseConfig->iSnapshotInterval = 60; //Default snapshots to an hour.
	if (!pDatabaseConfig->iIncrementalInterval && !pDatabaseConfig->bNoHogs) pDatabaseConfig->iIncrementalInterval = 5; //Default incrementals to 5min.
	if (!pDatabaseConfig->iIncrementalHoursToKeep && !pDatabaseConfig->bNoHogs) pDatabaseConfig->iIncrementalHoursToKeep = 4;

	return pDatabaseConfig;
}

// Save the current version to a file.
static void SaveVersion()
{
	char filename[] = "ASVersionHistory.txt";
	char *outPath = NULL;
	FILE *out;
	int result;

	// Format output path.
	estrStackCreate(&outPath);
	estrPrintf(&outPath, "%s%s", dbAccountDataDir(), filename);

	// Open output file.
	out = fopen(outPath, "a");
	if (!out)
	{
		AssertOrAlert("ACCOUNT_SERVER_VERSION_HISTORY", "Unable to open %s\n", outPath);
	}

	// Write version.
	if (out)
	{
		result = fprintf(out, "%s\t%s\n", timeGetLocalDateString(), GetUsefulVersionString());
		if (result < 0)
			AssertOrAlert("ACCOUNT_SERVER_VERSION_HISTORY", "Unable to write to %s\n", outPath);
	}

	// Close file.
	if (out)
	{
		result = fclose(out);
		if (result)
			AssertOrAlert("ACCOUNT_SERVER_VERSION_HISTORY", "Unable to close %s\n", outPath);
	}
	estrDestroy(&outPath);
}

// Fast Local Copy stuff
static void *AccountGetObject(GlobalType globalType, U32 uID)
{
	Container *con = objGetContainer(globalType, uID);
	if (con)
		return con->containerData;
	return NULL;
}

static void *AccountGetBackup(GlobalType type, ContainerID id)
{
	Container *con = objGetContainer(type, id);
	if (!con)
		return NULL;
	return BackupCacheGet(con);
}

static int giBackupCacheSize = 10000;
AUTO_CMD_INT(giBackupCacheSize, BackupCacheSize) ACMD_CMDLINE;

int AccountInit_2(void)
{
	NetListen * local_link = NULL, * public_link = NULL;
	int val;
	const DatabaseConfig *pDatabaseConfig = GetDatabaseConfig();

	PERFINFO_AUTO_START_FUNC();
	
	// Create accountdb directory.
	mkdirtree_const(dbAccountDataDir());

	// Save Account Server version.
	if (isAccountServerMode(ASM_Normal))
	{
		SaveVersion();
	}

	// Register schema for Account containers.
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNT, parse_AccountInfo, NULL, NULL, NULL, NULL, NULL);
	BackupCache_RegisterType(GLOBALTYPE_ACCOUNT, BACKUPCACHE_LRU, giBackupCacheSize);
	RegisterFastLocalCopyCB(GLOBALTYPE_ACCOUNT, AccountGetObject, AccountGetBackup);

	objRegisterContainerTypeAddCallback(GLOBALTYPE_ACCOUNT, objAccountAddToContainerStore_CB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_ACCOUNT, objAccountRemoveFromContainerStore_CB);

	BackupCache_RegisterType(GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, BACKUPCACHE_STASH, 2);
	RegisterFastLocalCopyCB(GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, AccountGetObject, AccountGetBackup);

	// Register schema for activity log batches
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, parse_AccountLogBatch, NULL, NULL, NULL, NULL, NULL);
	objCreateContainerStoreLazyLoad(objFindContainerSchema(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH), true);

	InitObjectLocalTransactionManager(GLOBALTYPE_ACCOUNTSERVER, NULL);
	RegisterDBUpdateDataCallback(objLocalManager(), &accountdbUpdateCB);

	objSetSnapshotMode(isAccountServerMode(ASM_Merger));

	// TODO: VAS 062512
	// Remove because it is not necessary to directly set this -- the default is HogSafeAgainstAppCrash
	// Without this call, we can override the hog open mode by command line if desired
	hogSetGlobalOpenMode(HogSafeAgainstAppCrash);
	hogSetMaxBufferSize(ACCOUNTSERVER_HOG_BUFFER_SIZE);
	
	objSetIncrementalHoursToKeep(pDatabaseConfig->iIncrementalHoursToKeep);
	objSetCommandReplayCallback(accountdbHandleDatabaseReplayString);

	if(isAccountServerMode(ASM_UpdateSchemas))
	{
		printf("Updating Schemas [%d] in '%s'.\n", objNumberOfContainerSchemas(), gUpdateSchemas);
		log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Updating Schemas [%d] in '%s'.", objNumberOfContainerSchemas(), gUpdateSchemas);
		objUpdateAllSchemas(gUpdateSchemas);
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	objSetContainerSourceToHogFile(STACK_SPRINTF("%saccountserver.hogg", dbAccountDataDir()),
		pDatabaseConfig->iIncrementalInterval, dbLogRotateCB, NULL);

	if (isAccountServerMode(ASM_Merger))
	{

		// Make sure no other mergers are running.
		bool success = LockMerger(ACCOUNT_SERVER_MERGER_NAME);
		if (!success)
		{
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Merger already running");
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}

		objSetContainerForceSnapshot(gbForceHoggSnapshot);
		if (gbFastSnapshot)
		{
			objMergeIncrementalHogs(pDatabaseConfig->bBackupSnapshot, gAppendSnapshot, false);
			if(gDefragAfterMerger)
			{
				objDefragLatestSnapshot();
			}
		}
		else
		{
			objSetContainerSourceToDirectory(dbAccountDataDir());
			objLoadAllContainers();
			objSetContainerSourceToHogFile(STACK_SPRINTF("%saccountserver.hogg", dbAccountDataDir()),
				pDatabaseConfig->iIncrementalInterval, dbLogRotateCB, NULL);
			objLoadAllContainers();
		}

		svrLogFlush(true);

		// Release merger.
		UnlockMerger();
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	loadstart_printf("Loading containers...\n");
	if (gbShowDBSpam)
	{
		objLoadAllContainers();
	}
	else
	{
		REDIRECT_STDOUT_START("DBStartupSpam.txt");
		{
			objLoadAllContainers();
		}
		REDIRECT_STDOUT_END;
	}
	loadend_printf("Loading containers...done.");

	LocalTransactionsTakeOwnership();
	objContainerLoadingFinished();

	if (!isAccountServerMode(ASM_ExportUserList))
	{
		deleteLockedList();
		scanProductKeys();

		activateStaleContainerCleanup();
		ScanAccountTransactionLog();
		scanAccounts();
		scanProducts();

		if (sImportFilePath[0])
			runImportCSVDatabase(sImportFilePath);
		assertmsg(GetAppGlobalType() == GLOBALTYPE_ACCOUNTSERVER, "Account server type not set");

		val = commListenBoth(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, AccountServerHandleMessage, AccountServerConnectCallback,
			AccountServerDisconnectCallback, sizeof(AccountLink), &local_link, &public_link);
		assertmsg(val, STACK_SPRINTF("Could not start server: Port %d already in use.", DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT));

		val = commListenBoth(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH|LINK_NO_COMPRESS, DEFAULT_ACCOUNTSERVER_TRUSTED_PORT, AccountServerHandleTrustedMessage, AccountServerConnectCallback,
			AccountServerDisconnectCallback, sizeof(AccountLink), &local_link, &public_link);
		assertmsg(val, STACK_SPRINTF("Could not start trusted server: Port %d already in use.", DEFAULT_ACCOUNTSERVER_TRUSTED_PORT));

		// Initialize port for GCS packets.
		GcsInterfaceInit();
	}

	local_link = public_link = NULL;
	PERFINFO_AUTO_STOP_FUNC();
	return 1;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(ObjectDB);
void StartMerger(bool hideMerger, bool defrag)
{
	char *estr = NULL;
	static U32 pokeCount = 0;
	bool mutexLocked;
	int iMergerPID;

	// Don't start merger if it has been suppressed.
	if (gbNoMerger)
		return;

	// Check to see if a merger is still running
	mutexLocked = IsMergerRunning(ACCOUNT_SERVER_MERGER_NAME);
	if (mutexLocked)
	{
		pokeCount++;
		if (mutexLocked)
		{
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Could not start merger because one is already running. [mutex locked]");
			printf("Could not start merger because one is already running. [mutex locked]\n");
		}
		if (pokeCount > gMergerLaunchFailuresBeforeAlert_Normal && !gLastMergerWasDefrag)
			AssertOrAlert("ACCOUNTSERVER_MERGER_RUNNING", "Attempted to start account server merger but failed %d times because one was already running!", pokeCount);
		else if (pokeCount > gMergerLaunchFailuresBeforeAlert_Defrag && gLastMergerWasDefrag)
			AssertOrAlert("ACCOUNTSERVER_MERGER_RUNNING", "Attempted to start account server merger but failed %d times because a defrag merger was already running!", pokeCount);
		return;
	}
	pokeCount = 0;

	estrStackCreate(&estr);
	estrPrintf(&estr, "%s -Mode Merger", getExecutableName());
	estrConcatf(&estr, " -SetProductName %s %s", GetProductName(), GetShortProductName());
	estrConcatf(&estr, " -SetErrorTracker %s", getErrorTracker());
	estrConcatf(&estr, " -LogServer %s", gServerLibState.logServerHost);

	if(GetDatabaseConfig()->iDaysBetweenDefrags)
	{
		estrConcatf(&estr, " -AppendSnapshot");
	}

	if(defrag)
	{
		U32 iTime = timeSecondsSince2000();
		gLastDefragDay = GetDefragDay();
		estrAppend2(&estr, " -DefragAfterMerger");
		servLog(LOG_MISC, "DefragMergerLaunch", "Time %d", iTime);
		gLastMergerWasDefrag = true;
	}
	else
	{
		gLastMergerWasDefrag = false;
	}

	if (isProductionMode())
		estrConcatf(&estr, " -ProductionMode 1");

	// Database options
	if (gbFastSnapshot != OPT_FASTSNAPSHOT_DEFAULT)
		estrConcatf(&estr, " -FastSnapshot %d", (int)gbFastSnapshot);
	if (gbForceHoggSnapshot != OPT_FORCEHOGGSNAPSHOT_DEFAULT)
		estrConcatf(&estr, " -ForceHoggSnapshot %d", (int)gbForceHoggSnapshot);

	// Run the merger.
	iMergerPID = system_detach(estr, 0, hideMerger);

	printf("AccountServer Snapshot Merger detached. [pid:%d]\n", iMergerPID);
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "AccountServer Snapshot Merger detached. [pid:%d]\n%s", iMergerPID, estr);

	if (!hideMerger)
	{
		printf("%s\n\n", estr);
	}

	estrDestroy(&estr);
}

// Check if it's time to start a new merger.
static void MergerTick(bool bShowSnapshots, U32 iSnapshotInterval)
{
	static U32 suLastSnapshotTime = 0;
	U32 currentTime;
	const DatabaseConfig *dbconfig = GetDatabaseConfig();

	if (suLastSnapshotTime == 0)
		suLastSnapshotTime = timeSecondsSince2000();
	currentTime = timeSecondsSince2000();
	if (currentTime > suLastSnapshotTime + iSnapshotInterval)
	{
		StartMerger(!bShowSnapshots, TimeToDefrag(dbconfig->iDaysBetweenDefrags, gLastDefragDay, dbconfig->iTargetDefragWindowStart, dbconfig->iTargetDefragWindowDuration));
		suLastSnapshotTime = currentTime;
	}
}

int AccountServerOncePerFrame(F32 fTotalElapsed, F32 fElapsed)
{
	const DatabaseConfig *pDatabaseConfig = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	coarseTimerAddInstance(NULL, __FUNCTION__);

	pDatabaseConfig = GetDatabaseConfig();

	if (giLauncherPid)
	{
		HANDLE hProcess = 0;
		DWORD exitCode = 0;

		hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, false, giLauncherPid);
		
		if (!hProcess || (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE))
		{
			exit(0);
		}
	}
	
	svrLogFlush(0);
	billingOncePerFrame();
	objContainerSaveTick();
	ReportingTick();
	productKeyTick();
	MergerTick(pDatabaseConfig->bShowSnapshots, pDatabaseConfig->iSnapshotInterval*60);
	accountLogRebucketingTick();
	ProcessSteamQueue();
	AccountIntegration_Tick();
	oneTimeCodeTick();
	PurchaseLogMigrationTick();

	AccountEncryption_FixupSomeAccounts();

	ALDProcessQueue();

	coarseTimerStopInstance(NULL, __FUNCTION__);
	PERFINFO_AUTO_STOP();

	return 1;
}

static void AccountServer_WriteUsersToDisk(void)
{
			// TODO write out all users
	FILE * file = NULL;
	char filepath[MAX_PATH];

	sprintf(filepath, "%s%s", dbAccountDataDir(), ACCOUNTUSER_EXPORT_FILENAME);

	file = fopen(filepath, "w");
	if (file)
	{
		Container *con;
		ContainerIterator iter = {0};
		AccountTicket ticket = {0};
		char *ticketString = NULL;

		fwrite("{\n", sizeof(char), 2, file);
		objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
		con = objGetNextContainerFromIterator(&iter);
		while (con)
		{
			AccountInfo *account= (AccountInfo*) con->containerData;

			estrClear(&ticketString);
			ticket.accountID = account->uID;
			strcpy(ticket.accountName, account->accountName);
			if (account->displayName)
				strcpy(ticket.displayName, account->displayName);
			else
				strcpy(ticket.displayName, account->accountName);
			
			ParserWriteText(&ticketString, parse_AccountTicket, &ticket, 0, 0, 0);
			fprintf(file, "Accounts%s\n", ticketString);

			con = objGetNextContainerFromIterator(&iter);
		}
		objClearContainerIterator(&iter);
		fwrite("}", sizeof(char), 1, file);
		fclose(file);
	}
}

extern void TextFilterLoad(void);

static bool gbUseXMLRPCErrorHandler = false;
AUTO_CMD_INT(gbUseXMLRPCErrorHandler, UseXMLRPCErrorHandler) ACMD_CMDLINE;

TSMP_DEFINE(AccountPermission);
TSMP_DEFINE(AccountPermissionValue);

static void initAccountServerMemPools(void)
{
	TSMP_SMART_CREATE(AccountPermission, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_AccountPermission, &TSMP_NAME(AccountPermission));

	TSMP_SMART_CREATE(AccountPermissionValue, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_AccountPermissionValue, &TSMP_NAME(AccountPermissionValue));
}

int AccountServerInit(void)
{
	AccountInfo *account = NULL;
	bool bDeleted = false;

	PERFINFO_AUTO_START_FUNC();

	if (gbDangerousHighPerformanceLogMode)
		logEnableHighPerformance();

	logSetDir(dbAccountDataDir());
	XMLRPC_UseErrorHandler(gbUseXMLRPCErrorHandler);

	if (isAccountServerMode(ASM_KeyGenerating))
	{
		initializeProductKeyCreation();

		if (gbNewWebInterface)
		{
			accountServerHttpInit(giWebInterfacePort);
		}
		else
		{
			initWebInterface();
		}
	}
	else
	{
		// Don't record any stats if we're in some special mode.
		if (!isAccountServerMode(ASM_Normal))
		{
			SetReportingStatus(false);
		}
		
		initAccountServerMemPools();
		LoadAccountServerConfig();
		initializeAccountIntegration();
		initializeProducts();
		initializeKeyValueChains();
		VirtualCurrency_Init();
		initializeDiscounts();
		initializeSubscriptions();
		initializeInternalSubscriptions();
		initializeLockedList();
		asgRegisterSchema();
		registerProductKeySchemas();
		initAccountLogEntryMempool();
		InitializePurchaseLog();
		InitializeAccountTransactionLog();
		AccountProxyServerInit();
		initAccountStashTables();

		if (!isAccountServerMode(ASM_Merger) && !isAccountServerMode(ASM_ExportUserList) && !isAccountServerMode(ASM_UpdateSchemas))
		{
			if (gbNewWebInterface)
			{
				accountServerHttpInit(giWebInterfacePort);
			}
			else
			{
				initWebInterface();
			}
		}
		AccountInit_2();
		if (isAccountServerMode(ASM_ExportUserList))
		{
			AccountServer_WriteUsersToDisk();
			PERFINFO_AUTO_STOP_FUNC();
			return 1; // skip the rest
		}
		if (isAccountServerMode(ASM_Merger))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return 1; // skip the rest
		}

		if(isAccountServerMode(ASM_UpdateSchemas))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return 1; // skip the rest
		}

		TextFilterLoad();

		if(!billingInit())
		{
			PERFINFO_AUTO_STOP_FUNC();
			return 0;
		}

		asgInitialize();
		
		AccountEncryption_Startup();
		ALDInit();

		XmlInterfaceInit();

		ATR_DoLateInitialization();
	}

	PERFINFO_AUTO_STOP_FUNC();
	return 1;
}

void AccountServerShutdown(void)
{
	if (isAccountServerMode(ASM_Normal))
	{
		ALDDeinit();
	}
	if (!isAccountServerMode(ASM_Merger) && !isAccountServerMode(ASM_ExportUserList) && !isAccountServerMode(ASM_UpdateSchemas))
	{
		billingShutdown();
		shutdownWebInterface();
	}
}


//////////////////////////////////////
// Account and Display Name cleanup

static bool DisplayName_RemoveInvalidCharacters(char **estrDisplayName)
{
	int curIndex = 0;
	const char *cur = *estrDisplayName;
	char pchLastPunc = 0;
	char *displayCopy = strdup(*estrDisplayName);
	bool bModified = false;

	while (*cur)
	{
		if (*cur & 0x80)
		{
			bModified = true;
		}
		else if (!isalnum(*cur))
		{
			if (*cur == '-' || *cur == '.' || *cur == '_')
			{
				if ( pchLastPunc == 0) 
				{
					displayCopy[curIndex++] = *cur;
				} 
				else // otherwise ignore and skip this
					bModified = true;
				pchLastPunc = *cur;
			}
			else if (isspace((unsigned char)*cur))
			{
				bModified = true;
				if (pchLastPunc == 0)
				{
					displayCopy[curIndex++] = '_';
					pchLastPunc = *cur;
				}
				// otherwise ignore and skip since there's already a punctuation
			}
			else // ignore and skip all other characters
				bModified = true;
		}
		else
		{
			displayCopy[curIndex++] = *cur;
			pchLastPunc = 0;
		}
		cur++;
	}
	if (bModified)
		estrCopy2(estrDisplayName, displayCopy);
	free(displayCopy);
	return bModified;
}

AUTO_COMMAND ACMD_CATEGORY(Accounts) ACMD_ACCESSLEVEL(9);
void AccountServer_CleanupDisplayNames (void)
{
	Container *con;
	ContainerIterator iter = {0};

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		AccountInfo *account = (AccountInfo*) con->containerData;
		char *pDisplayCopy = NULL;
		bool bModified = false;
		bool bUnfixable = false; // un-fixable error
		int iLastError = STRINGERR_NONE;
		int error;

		if (account->flags & ACCOUNT_FLAG_INVALID_DISPLAYNAME)
		{
			con = objGetNextContainerFromIterator(&iter);
			continue; // already flagged
		}

		estrCopy2(&pDisplayCopy, account->displayName);
		while (error = StringIsInvalidDisplayName(pDisplayCopy, 0))
		{
			bool bExit = false;
			switch (error)
			{
			case STRINGERR_MIN_LENGTH:
			case STRINGERR_MAX_LENGTH:
				bUnfixable = true;
			xcase STRINGERR_WHITESPACE:
				estrTrimLeadingAndTrailingWhitespace(&pDisplayCopy);
				bModified = true;
			xcase STRINGERR_PROFANITY:
				bUnfixable = true;
			xcase STRINGERR_RESTRICTED:
				if (account->bInternalUseLogin)
				{ // not an error!
					bExit = true;
				}
				else
					bUnfixable = true;
			xcase STRINGERR_CHARACTERS:
				if (iLastError == STRINGERR_CHARACTERS)
				{
					log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Command: AutoDisplayCleanup, Account: %s - Failed cleanup: '%s', '%s'\n", 
						account->accountName, account->displayName, pDisplayCopy);
					bUnfixable = true;
				}
				else if (DisplayName_RemoveInvalidCharacters(&pDisplayCopy))
					bModified = true;
			xcase STRINGERR_NONE:
				bExit = true;
				break;
			}

			iLastError  = error;
			if (bUnfixable || bExit)
				break;
		}

		if (bUnfixable)
		{
			log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Command: AutoDisplayCleanup, Account: %s - Invalid Display Name '%s', Error %d\n", 
				account->accountName, account->displayName, iLastError);
			accountFlagInvalidDisplayName(account, true);
		}
		else if (bModified)
		{
			if (!findAccountContainerByName(pDisplayCopy) || stricmp(account->accountName, pDisplayCopy) == 0)
			{
				log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Command: AutoDisplayCleanup, Account: %s - Changing '%s' to '%s'\n", 
					account->accountName, account->displayName, pDisplayCopy);
				changeAccountDisplayName(account, pDisplayCopy, NULL, NULL);
			}
			else
			{
				log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Command: AutoDisplayCleanup, Account: %s - Changing '%s' to '%s' - already taken\n", 
					account->accountName, account->displayName, pDisplayCopy);
			}
		}

		estrDestroy(&pDisplayCopy);
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

static void estrConcatCompressedNumber(SA_PRE_NN_NN_STR char **estr, U64 uValue, SA_PARAM_NN_STR const char *pTable)
{
	while (uValue > 0)
	{
		estrConcatChar(estr, pTable[uValue % strlen(pTable)]);
		uValue /= strlen(pTable);
	}
}

static U64 uncompressNumber(SA_PARAM_NN_STR const char * pStr, SA_PARAM_NN_STR const char * pTable)
{
	U64 uValue = 0;
	int iCurChar = 0;

	if (!pStr || !*pStr) return 0;

	for (iCurChar = (int)strlen(pStr) - 1; iCurChar >= 0; iCurChar--)
	{
		uValue *= strlen(pTable);
		uValue += strchr(pTable, pStr[iCurChar]) - pTable;
	}

	return uValue;
}

static const U64 guSubtractMicroseconds = 12887145790099688; // No use worrying about time in the past

SA_RET_NN_STR char * createShortUniqueString(unsigned int uMaxLen, SA_PARAM_NN_STR const char *pTable)
{
	U64 uTime = microsecondsSince1601() - guSubtractMicroseconds;
	static U64 uEverIncrementingID = 0;
	unsigned int tableLen = (unsigned int)strlen(pTable);
	unsigned int uVersion = 1;
	char *output = NULL;

	if (!uEverIncrementingID) uEverIncrementingID = rand();

	estrPrintf(&output, "%c%c%c%c%c%c",
		pTable[uEverIncrementingID % tableLen],
		pTable[(uEverIncrementingID / tableLen) % tableLen],
		pTable[uVersion],
		pTable[(uEverIncrementingID / (tableLen * 2)) % tableLen],
		pTable[(uEverIncrementingID / (tableLen * 3)) % tableLen],
		pTable[(uEverIncrementingID / (tableLen * 4)) % tableLen]);
	estrConcatCompressedNumber(&output, uTime, pTable);
	if (uMaxLen && estrLength(&output) > uMaxLen)
	{
		AssertOrAlert("ACCOUNTSERVER_UNIQUE_STRING_LENGTH", "Unique string exceeds maximum length of %d.", uMaxLen);
	}
	if (uMaxLen)
		estrSetSize(&output, uMaxLen);

	uEverIncrementingID++;

	return output;
}

AUTO_ENUM;
typedef enum GUIDType
{
	GT_Account,
	GT_Transaction,
} GUIDType;

AUTO_COMMAND ACMD_CATEGORY(Account_Server);
SA_RET_NN_STR const char * DecodeGUID(SA_PARAM_NN_STR const char * GUID,
	SA_PARAM_NN_STR ACMD_NAMELIST(GUIDTypeEnum, STATICDEFINE) const char * Type)
{
	GUIDType eType = StaticDefineIntGetInt(GUIDTypeEnum, Type);
	static char * pResult = NULL;
	unsigned int uParts[5] = {0};
	unsigned int uVersion = 0;
	U64 uIncrementing = 0;
	U64 uTime = 0;
	const char * pTable = NULL;
	char pTime[256] = {0};

	if (strlen(GUID) < 6) return "Invalid GUID";

	switch (eType)
	{
	case GT_Account: pTable = ALPHA_NUMERIC_CAPS; break;
	case GT_Transaction: pTable = ALPHA_NUMERIC_CAPS_READABLE; break;
	default: return "Invalid type";
	}

	uParts[0] = strchr(pTable, GUID[0]) - pTable;
	uParts[1] = strchr(pTable, GUID[1]) - pTable;
	uVersion = strchr(pTable, GUID[2]) - pTable;
	uParts[2] = strchr(pTable, GUID[3]) - pTable;
	if (uVersion)
	{
		int i = 0;
		uParts[3] = strchr(pTable, GUID[4]) - pTable;
		uParts[4] = strchr(pTable, GUID[5]) - pTable;
		for (i = 4; i >= 0; i--) {
			uIncrementing *= strlen(pTable);
			uIncrementing += strchr(pTable, uParts[i]) - pTable;
		}
	}
	uTime = uncompressNumber(GUID + (uVersion ? 6 : 4), pTable) + guSubtractMicroseconds;

	if (!uVersion)
	{
		estrPrintf(&pResult, "Version: 0\nRandom: %u\nRandom2: %u\nMachine: %u\nIncrementing: %u\nTime number: %llu\n",
			uParts[0], uParts[1], uVersion, uParts[2], uTime);
	}
	else
	{
		estrPrintf(&pResult, "Version: %u\nIncrementing: %llu\nTime: %llu\n",
			uVersion, uIncrementing, uTime);
	}

	timeMakeDateStringFromSecondsSince2000(pTime, (uTime*10-timeY2KOffset())/WINTICKSPERSEC);
	estrConcatf(&pResult, "Time: %s UTC", pTime);

	return pResult;
}

int getAccountServerID(void)
{
	return 0; // In the future, there might be child account servers, with different IDs.
}

// Generic managed return value handler that reports transaction failures.
static void astrRequireSuccessReturn(TransactionReturnVal *returnVal, void *userData)
{
	if (returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		char *result = NULL;
		int i;
		estrStackCreate(&result);
		estrPrintf(&result, "Outcome %d", returnVal->eOutcome);
		for (i = 0; i < returnVal->iNumBaseTransactions; ++i)
			estrConcatf(&result, "; outcome %d, return \"%s\"", returnVal->pBaseReturnVals[i].eOutcome,
			NULL_TO_EMPTY(returnVal->pBaseReturnVals[i].returnString));
		AssertOrAlert("ACCOUNTSERVER_TRANSACTION", "Unsuccessful transaction %s (%s): %s",
			returnVal->pTransactionName, NULL_TO_EMPTY((char *)userData), result);
		estrDestroy(&result);
	}
}

// Generic managed return value that alerts on transaction failures.
TransactionReturnVal * astrRequireSuccess(SA_PARAM_OP_VALID const char *pExtraInformation)
{
	return objCreateManagedReturnVal(astrRequireSuccessReturn, (void *)pExtraInformation);
}

SubscriptionStatus getCachedSubscriptionStatus(SA_PARAM_NN_VALID const CachedAccountSubscription *pCachedSub)
{
	U32 uEndTime = 0;

	if (!verify(pCachedSub)) return SUBSCRIPTIONSTATUS_INVALID;

	// The entitlement end time should always be after the next billing date because of the 9
	// day grace period Vindicia offers.  However, the entitlement end time is sometimes 0.
	// To be safe, use the greater of the two dates/times as if it were the entitlement end time.
	uEndTime = MAX(pCachedSub->entitlementEndTimeSS2000, pCachedSub->nextBillingDateSS2000);

	// Vindicia subscriptions sometimes get stuck in the active state, even after
	// they are no longer entitled, so we should check the end time to be sure.
	if (pCachedSub->vindiciaStatus == SUBSCRIPTIONSTATUS_ACTIVE &&
		timeSecondsSince2000() > uEndTime)
	{
		return SUBSCRIPTIONSTATUS_SUSPENDED;
	}

	return pCachedSub->vindiciaStatus;
}


/************************************************************************/
/* IP address rate limit                                                */
/************************************************************************/

static RateLimit *gpIPRateLimit = NULL;

bool IPRateLimit(const char * const * easzIP, IPRateLimitActivity eActivity)
{
	bool bAllowed = true;

	PERFINFO_AUTO_START_FUNC();

	if (!gpIPRateLimit)
	{
		RateLimitConfig rateLimitConfig = {0};

		StructInit(parse_RateLimitConfig, &rateLimitConfig);
		rateLimitConfig.bEnabled = false; // Disabled until a file says otherwise
		RLAutoLoadConfigFromFile(&gpIPRateLimit, "server/AccountServer/ipRateLimit.txt",
			&rateLimitConfig, "IP_RATE_LIMIT", IPRateLimitActivityEnum);
		StructDeInit(parse_RateLimitConfig, &rateLimitConfig);
	}

	if (!devassert(gpIPRateLimit))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	EARRAY_CONST_FOREACH_BEGIN(easzIP, iCurIP, iNumIPs);
	{
		const char * szIP = easzIP[iCurIP];
		U32 uIP = 0;

		if (!szIP || !*szIP)
		{
			bAllowed = false;
			continue; // Go ahead and check the rest; don't early out
		}

		uIP = ipFromString(szIP);
		if (!uIP)
		{
			bAllowed = false;
			continue;
		}

		if (!ipfIsTrustedIp(uIP) && !ipfIsIpInGroup("ASRateLimitWhitelist", uIP))
		{
			// We don't actually want to early out
			bAllowed &= RLCheck(gpIPRateLimit, szIP, eActivity);
		}
	}
	EARRAY_FOREACH_END;

	PERFINFO_AUTO_STOP();

	return bAllowed;
}

// Returns SS2000 for when an IP will be unblocked (or 0 if it is not blocked)
AUTO_COMMAND ACMD_CATEGORY(Account_Server);
U32 IPBlockedUntil(SA_PARAM_NN_STR const char * szIP)
{
	if (!gpIPRateLimit)
	{
		return 0;
	}

	return RLBlockedUntil(gpIPRateLimit, szIP);
}

SA_RET_OP_VALID RateLimitBlockedIter * IPBlockedIterCreate(void)
{
	if (!gpIPRateLimit)
	{
		return NULL;
	}

	return RLBlockedIterCreate(gpIPRateLimit);
}

// Test an IP against the rate limiting
AUTO_COMMAND ACMD_CATEGORY(Account_Server);
SA_RET_NN_STR const char * TestIPAgainstRateLimit(SA_PARAM_NN_STR const char * IP,
	SA_PARAM_NN_STR ACMD_NAMELIST(IPRateLimitActivityEnum, STATICDEFINE) const char * ActivityType)
{
	bool bAllowed = false;
	const char ** easzIPs = NULL;
	eaPush(&easzIPs, IP);
	bAllowed = IPRateLimit(easzIPs, StaticDefineIntGetInt(IPRateLimitActivityEnum, ActivityType));
	eaDestroy(&easzIPs);
	return bAllowed ? "Allowed" : "Blocked"; 
}


/************************************************************************/
/* Database replacement used by platform test cases                     */
/************************************************************************/

void AccountServer_ReplaceDatabase(EARRAY_OF(AccountInfo) eaAccounts, 
	EARRAY_OF(ProductContainer) eaProducts, 
	EARRAY_OF(PWCommonAccount) eaPWAccounts,
	EARRAY_OF(SubscriptionCreateData) eaSubscriptions,
	EARRAY_OF(ProductKeyGroup) eaPKGroups, 
	EARRAY_OF(ProductKeyBatch) eaPKBatches)
{
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_DISCOUNT);
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_PURCHASE_LOG);
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_LOCKS);
	//objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA);

	// Clearing stashes
	Subscription_DestroyContainers();
	AccountManagement_DestroyContainers();
	InternalSubs_DestroyContainers();
	ProductKey_DestroyContainers();
	AccountIntegration_DestroyContainers();
	// Ignoring billing stuff

	EARRAY_CONST_FOREACH_BEGIN(eaAccounts, i, s);
	{
		createAccountFromStruct(eaAccounts[i]);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(eaProducts, i, s);
	{
		createProductFromStruct(eaProducts[i]);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(eaPWAccounts, i, s);
	{
		AccountIntegration_CreatePWFromStruct(eaPWAccounts[i]);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(eaSubscriptions, i, s);
	{
		if (eaSubscriptions[i])
			subscriptionAddStruct(eaSubscriptions[i]);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(eaPKGroups, i, s);
	{
		if (eaPKGroups[i])
			productKeyGroupAddStruct(eaPKGroups[i]);
	}
	EARRAY_FOREACH_END;

	EARRAY_CONST_FOREACH_BEGIN(eaPKBatches, i, s);
	{
		if (eaPKBatches[i])
			productKeyBatchAddFromStruct(eaPKBatches[i]);
	}
	EARRAY_FOREACH_END;	
}

extern ParseTable parse_AccountInfo[];
#define TYPE_parse_AccountInfo AccountInfo
extern ParseTable parse_ProductContainer[];
#define TYPE_parse_ProductContainer ProductContainer
AUTO_COMMAND ACMD_CATEGORY(AccountServer_Test);
void AccountServer_TestReplace(void)
{
	NOCONST(AccountInfo) *account = StructCreateNoConst(parse_AccountInfo);
	NOCONST(ProductContainer) *product = StructCreateNoConst(parse_ProductContainer);
	NOCONST(PWCommonAccount) *pwAccount = StructCreateNoConst(parse_PWCommonAccount);
	SubscriptionCreateData *subscription = StructCreate(parse_SubscriptionCreateData);
	NOCONST(SubscriptionContainer) *pSub = CONTAINER_NOCONST(SubscriptionContainer, &subscription->subscription);
	NOCONST(ProductKeyGroup) *pKeyGroup =  StructCreateNoConst(parse_ProductKeyGroup);
	NOCONST(ProductKeyBatch) *pKeyBatch =  StructCreateNoConst(parse_ProductKeyBatch);

	EARRAY_OF(AccountInfo) ppAccounts = NULL;
	EARRAY_OF(ProductContainer) ppProducts= NULL;
	EARRAY_OF(PWCommonAccount) ppPWAccounts= NULL;
	EARRAY_OF(SubscriptionCreateData) ppSubscriptions = NULL;
	EARRAY_OF(ProductKeyGroup) ppPKGroups = NULL;
	EARRAY_OF(ProductKeyBatch) ppPKBatches = NULL;

	account->uID = 100;
	strcpy(account->accountName, "test");
	estrCopy2(&account->displayName, "test2");
	strcpy(account->password_obsolete, "none123");
	account->pPWAccountName = StructAllocString("testPW");

	product->uID = 30;
	estrCopy2(&product->pName, "Test Product");
	estrCopy2(&product->pInternalName, "testprod");

	pwAccount->pAccountName = StructAllocString("testPW");
	pwAccount->pForumName = StructAllocString("testPW2_forum");
	pwAccount->pEmail = StructAllocString("rawr@rawr.rawr");
	pwAccount->uLinkedID = 100;
	
	pSub->uID = 55;
	estrCopy2(&pSub->pName, "SubName"); 
	estrCopy2(&pSub->pInternalName, "SubInternalName");
	estrCopy2(&pSub->pDescription, "Sub Description");
	estrCopy2(&pSub->pProductName, "testprod");
	subscription->pPriceString = strdup("20 DNE, 10 USD ");
	subscription->pCategoryString = strdup("testcat1,testcat2, testcat3");
	
	eaPush(&ppAccounts, (AccountInfo*) account);
	eaPush(&ppProducts, (ProductContainer*) product);
	eaPush(&ppPWAccounts, (PWCommonAccount*) pwAccount);
	eaPush(&ppSubscriptions, subscription);

	pKeyGroup->uID = pKeyBatch->uID = 1;
	eaiPush(&pKeyGroup->keyBatches, 1);
	eaPush(&pKeyGroup->ppProducts, estrDup("Test Product"));
	strcpy(pKeyGroup->productPrefix, "AAAAA");

	pKeyBatch->uKeySize = PRODUCT_KEY_SIZE;
	estrCopy2(&pKeyBatch->pBatchName, "KeyBatch1");
	estrCopy2(&pKeyBatch->pBatchDescription, "This is a key batch");
	eaPush(&pKeyBatch->ppBatchKeys, strdup("AAAAABBBBBCCCCCDDDDDEEEEE"));
	eaPush(&pKeyBatch->ppBatchKeys, strdup("AAAAABBBBBCCCCCDDDDDZZZZZ"));

	eaPush(&ppPKGroups, CONTAINER_RECONST(ProductKeyGroup, pKeyGroup));
	eaPush(&ppPKBatches, CONTAINER_RECONST(ProductKeyBatch, pKeyBatch));

	AccountServer_ReplaceDatabase(ppAccounts, ppProducts, ppPWAccounts, ppSubscriptions, ppPKGroups, ppPKBatches);
	eaDestroyStruct(&ppAccounts, parse_AccountInfo);
	eaDestroyStruct(&ppProducts, parse_ProductContainer);
	eaDestroyStruct(&ppPWAccounts, parse_PWCommonAccount);
	eaDestroyStruct(&ppSubscriptions, parse_SubscriptionCreateData);
	eaDestroyStruct(&ppPKGroups, parse_ProductKeyGroup);
	eaDestroyStruct(&ppPKBatches, parse_ProductKeyBatch);
}

const char *GetPasswordVersionName(enumPasswordVersion eVersion)
{
	static char **sppNames = NULL;

	if (!sppNames)
	{
		int i;

		for (i = 0; i < PASSWORDVERSION_COUNT; i++)
		{
			char *pTemp = NULL;
			estrPrintf(&pTemp, "%s(%d)", StaticDefineInt_FastIntToString(enumPasswordVersionEnum, i), i);
			eaPush(&sppNames, pTemp);
		}
	}

	if (eVersion < 0 || eVersion >= PASSWORDVERSION_COUNT)
	{
		char temp[128];
		sprintf(temp, "UNKNOWN(%d)", eVersion);
		return allocAddString(temp);
	}

	return sppNames[eVersion];
}

enumPasswordVersion eCurPasswordVersion = PASSWORDVERSION_HASHED_SALTED_WITH_ACTNAME;

AUTO_COMMAND ACMD_COMMANDLINE;
void PasswordVersion(int iVersion, CmdContext *pContext)
{
	if (pContext->eHowCalled != CMD_CONTEXT_HOWCALLED_COMMANDLINE)
	{
		CRITICAL_NETOPS_ALERT("BAD_PW_VERSION_SET", "Someone tried to execute the command PasswordVersion via %s. This command may only be executed from the command line",
			GetContextHowString(pContext));
		return;
	}

	assertmsgf(iVersion >= 0 && iVersion < PASSWORDVERSION_COUNT, "Trying to set PasswordVersion to %d. Legal values are 0...%d",
		iVersion, PASSWORDVERSION_COUNT - 1);

	eCurPasswordVersion = iVersion;
}

void StartupFail(FORMAT_STR const char* format, ...)
{
	// moved from AccountEncryption.c
	char *pFullError = NULL;
	estrGetVarArgs(&pFullError, format);
	consoleSetColor(0, COLOR_RED | COLOR_BRIGHT);
	printf("%s\n", pFullError);

	if (!isAccountServerLikeLive())
		return;

	CancelStayUp();

	while (1)
	{
		static U32 siLastAlertTime = 0;
		U32 iCurTime = timeSecondsSince2000();
		if (siLastAlertTime < iCurTime - 60)
		{
			CRITICAL_NETOPS_ALERT("AS_STARTUP_FAIL", "%s", pFullError);
			siLastAlertTime = iCurTime;
		}
		commMonitor(commDefault());
		utilitiesLibOncePerFrame(REAL_TIME);	
	}

	exit(-1);
}


#include "AccountServer_h_ast.c"
#include "AccountServer_c_ast.c"
