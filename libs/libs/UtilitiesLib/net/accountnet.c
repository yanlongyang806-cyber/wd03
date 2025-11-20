#include "accountnet.h"
#include "crypt.h"
#include "error.h"
#include "estring.h"
#include "file.h"
#include "GlobalComm.h"
#include "globalTypes.h"
#include "Money.h"
#include "net.h"
#include "Organization.h"
#include "ResourceInfo.h"
#include "StashTable.h"
#include "sysutil.h"
#include "TimedCallback.h"
#include "timing.h"
#include "StringUtil.h"
#include "structNet.h"
#include "utilitiesLib.h"
#include "ContinuousBuilderSupport.h"
#if _XBOX
#include "XUtil.h"
#endif
#include "accountnet_h_ast.h"
#include "AutoGen/GlobalComm_h_ast.h"
#include "wininclude.h"

//occasionally we turn this compile flag on and make a special crypticLauncher for QA that prints out extra stuff
#if 0
#define QA_LOG(str) printf("%s", str);
#else
#define QA_LOG(str)
#endif

#define LIVE_ACCOUNTSERVER ORGANIZATION_DOMAIN

static bool sbAccountServerWasSet = false;

static char szAccountServerName[128] = LIVE_ACCOUNTSERVER;

//static NetLink *gpAccountSaltLink = NULL;
NetLink *gpAccountLink = NULL;
static NetComm *spAccountServerComm = NULL;

// Global failure reason temporary storage.
static char *spFormattedFailureReason = NULL;

bool gbDoNotSendMachineID = false;
AUTO_CMD_INT(gbDoNotSendMachineID, DoNotSendMachineID) ACMD_CMDLINE ACMD_ACCESSLEVEL(0);

bool gbCompressPersistentAccountLinks = true;
AUTO_CMD_INT(gbCompressPersistentAccountLinks, CompressPersistentAccountLinks) ACMD_CMDLINE;

bool gbCompressDirectAccountLink = false;
AUTO_CMD_INT(gbCompressDirectAccountLink, CompressDirectAccountLink) ACMD_CMDLINE;

// Debugging - skip connecting to Account Server and load the specified file as a ticket
static char *spForceAccountTicket = NULL;
static char *spTicketText = NULL;
AUTO_COMMAND ACMD_CMDLINE ACMD_NAME(ForceAccountTicket);
void setForceAccountTicketFile(const char *filename)
{
	spForceAccountTicket = StructAllocString(filename);
}

NetComm *accountCommDefault(void)
{
	if (!spAccountServerComm)
		spAccountServerComm = commCreate(0, 1);
	return spAccountServerComm;
}

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void setAccountServer(const char *pAccountServer)
{
	sbAccountServerWasSet = true;
	strcpy(szAccountServerName, pAccountServer);
}

AUTO_COMMAND ACMD_NAME(SetAccountServer) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void setAccountServerAtRunTime(const char *pAccountServer)
{
	sbAccountServerWasSet = true;
	strcpy(szAccountServerName, pAccountServer);
}

bool accountServerWasSet(void)
{
	return sbAccountServerWasSet;
}

bool accountServerIsLive(void)
{
	return !stricmp(getAccountServer(), LIVE_ACCOUNTSERVER);
}

char *DEFAULT_LATELINK_getAccountServer(void)
{
	if (g_isContinuousBuilder)
	{
		return "localhost";
	}
	return szAccountServerName;
}

char * accountHashPassword_s (const char *password, char *buffer, size_t buffer_size)
{
	return cryptCalculateSHAHash(password, buffer, buffer_size);
}

char * accountPWHashPassword_s (const char *account_name, const char *password, char *buffer, size_t buffer_size)
{
	char *pModPassword = estrDup(account_name);
	_strlwr_s(pModPassword, estrLength(&pModPassword) + 1);
	estrConcatString(&pModPassword, password, (unsigned int)strlen(password));
	accountMD5HashPassword_s(pModPassword, buffer, buffer_size);
	estrDestroy(&pModPassword);
	return buffer;
}

char * accountPWHashPasswordFixedSalt_s (const char *salt, const char *password, char *buffer, size_t buffer_size)
{
	char *pModPassword = estrDup(salt);
	estrConcatString(&pModPassword, password, (unsigned int)strlen(password));
	accountMD5HashPassword_s(pModPassword, buffer, buffer_size);
	estrDestroy(&pModPassword);
	return buffer;
}

char *accountMD5HashPassword_s(const char *password, char *buffer, size_t buffer_size)
{
	U32 aiHash[4] = {0,0,0,0};

	cryptMD5((char*) password, (int) strlen(password), aiHash);
	//encodeBase64String( (unsigned char*) aiHash, sizeof(U32)*4, buffer, buffer_size);
	encodeHexString( (unsigned char*) aiHash, sizeof(U32)*4, buffer, buffer_size);

	return buffer;
}
char *accountSHA256HashPassword_s(const char *password, char *buffer, size_t buffer_size)
{
	return cryptCalculateSHAHash(password, buffer, buffer_size);
}

int accountConvertHexToBase64_s (const char *hexString, char *buffer, size_t buffer_size)
{
	int hexLen = hexString ? (int) strlen(hexString) : 0;
	char * rawData;
	int rawLen = hexLen / 2;
	int result;
	if (!hexString) return -1; // decode failed
	if (!rawLen) return -1;

	rawData = malloc(rawLen);

	if (decodeHexString(hexString, hexLen, rawData, rawLen) == -1)
	{
		free(rawData);
		return -1; // decode failed
	}

	result = encodeBase64String(rawData, rawLen, buffer, buffer_size);
	free(rawData);
	return result;
}

char * accountAddSaltToHashedPassword_s (const char *passwordHash, U32 salt, char *buffer, size_t buffer_size)
{
	if (salt)
	{
		char * saltChars = (char*) &salt;
		char saltString[5];
		int i;
		for (i=0; i<4; i++)
		{
#if _PS3
			if (saltChars[i] == 0)
				saltChars[i] = 4-i; // just make it non-zero so it won't terminate the string
			saltString[3-i] = saltChars[i];
#elif _XBOX
			// XBox has opposite endian-ess
			if (saltChars[i] == 0)
				saltChars[i] = 4-i; // just make it non-zero so it won't terminate the string
			saltString[3-i] = saltChars[i];
#else
            if (saltChars[i] == 0)
				saltChars[i] = i+1; // just make it non-zero so it won't terminate the string
			saltString[i] = saltChars[i];
#endif
		}
		saltString[4] = '\0';
		return cryptCalculateSHAHash(STACK_SPRINTF("%s%s", passwordHash, saltString), buffer, buffer_size);
	}
	strcpy_s(buffer, buffer_size, passwordHash);
	return buffer;
}

char * accountAddNewStyleSaltToHashedPassword_s (const char *passwordHash, U32 salt, char *buffer, size_t buffer_size)
{
	char tempString[16];
	sprintf(tempString, "%u", salt);
	return cryptAddSaltToHash(passwordHash, tempString, false, buffer, buffer_size);
}

char *accountAddAccountNameThenNewStyleSaltToHashedPassword_s(const char *passwordHash, const char *accountName, U32 iSalt, char *buffer, size_t buffer_size)
{
	char tempString[16];
	char *pTempBuffer = alloca(buffer_size);
	char *pTempRetVal = cryptAddSaltToHash(passwordHash, accountName, true, pTempBuffer, buffer_size);
	if (!pTempRetVal)
	{
		return NULL;
	}

	if (iSalt)
	{
		sprintf(tempString, "%u", iSalt);
		return cryptAddSaltToHash(pTempBuffer, tempString, false, buffer, buffer_size);
	}

	strcpy_s(buffer, buffer_size, pTempBuffer);
	return buffer;
}

AccountPermissionStruct* findGamePermissions(AccountTicket *pTicket, const char *pProductName)
{
	int i, size = eaSize(&pTicket->ppPermissions);
	for (i=0; i<size; i++)
	{
		if (stricmp(pTicket->ppPermissions[i]->pProductName, pProductName) == 0)
			return pTicket->ppPermissions[i];
	}
	return NULL;
}

AccountPermissionStruct* findGamePermissionsByShard(AccountTicket *pTicket, const char *pProductName, const char *pShardName)
{
	int i, size = eaSize(&pTicket->ppPermissions);
	for (i=0; i<size; i++)
	{
		if (stricmp(pTicket->ppPermissions[i]->pProductName, pProductName) == 0)
		{
			AccountPermissionStruct *permissions = pTicket->ppPermissions[i];
			if (!pShardName || !pShardName[0])
				return permissions;
			else
			{
				char *shardNames = strstri(permissions->pPermissionString, ACCOUNT_PERMISSION_SHARD_PREFIX);
				if (shardNames)
				{
					char * endShardNames = strstri(shardNames, ";");
					char * shardList, *curShard;
					char * context = NULL;
					int len = endShardNames ? 
						endShardNames - shardNames - ACCOUNT_PERMISSION_SHARD_PREFIX_LEN : 
					(int) strlen(shardNames) - ACCOUNT_PERMISSION_SHARD_PREFIX_LEN;
					shardNames += ACCOUNT_PERMISSION_SHARD_PREFIX_LEN; // advance past the "shard:"

					shardList = malloc(len + 1);
					shardList[len] = '\0'; // NULL terminate this
					strncpy_s(shardList, len+1, shardNames, len);

					curShard = strtok_s(shardList, " ,;", &context);
					while (curShard)
					{
						if (stricmp(curShard, pShardName) == 0)
						{
							free(shardList);
							return permissions;
						}
						curShard = strtok_s(NULL, " ,;", &context);
					}

					free(shardList);
				}
				else // TODO temporary - assume entire string is shard category filter
				{
					if (stricmp(permissions->pPermissionString, pShardName) == 0)
					{
						return permissions;
					}
				}
			}
		}
	}
	return NULL;
}

void accountSendSaltRequest(NetLink * pLink, const char * pLoginField)
{
	Packet * pkt = pktCreate(pLink, TO_ACCOUNTSERVER_REQUEST_LOGINSALT);
	AccountSaltRequest request = {0};

	StructInit(parse_AccountSaltRequest, &request);
	strcpy(request.pLoginField, pLoginField);
	ParserSendStructAsCheckedNameValuePairs(pkt, parse_AccountSaltRequest, &request);
	StructDeInit(parse_AccountSaltRequest, &request);

	pktSend(&pkt);
}

const char * accountNetGetPubKey(AccountServerEncryptionKeyVersion eKeyVersion)
{
	switch (eKeyVersion)
	{
	case ASKEY_dev_1:
		return "AAAAB3NzaC1yc2EAAAABJQAAAIB5OyjrNKpShHMnrLoLMOvHMOAHj/dRPmgV++ej"
			   "EdsmtZlUMp1Nh4UMu7lT/YSrI+5uko6ddFTfD+vAivdUcEyYq0QtwImk67W+SMhl"
			   "PF+0wj68WIs/uB6DyDDP2nVqM+rGbp90i+iTT+/yMLedfZ4vy6GKHGsvCHP7oWDx"
			   "KoinXw==";
	case ASKEY_prod_1:
		return "AAAAB3NzaC1yc2EAAAABJQAAAIEAgDNPjixZgLaR6ViSOCBAgvhNiYARO2CjEDFH"
			   "pfI3pLBl7x+b5zJz1/wQnsEBZeSz/u09J9ezfnOUfmy4e7gr/6VKrgyAjfNp9Nkb"
			   "RqR7WHKHYJLpMmV39t+CpfwqKWiiQmlsEcKO3rxpfrVVIJi1ifaIyio9TsRXPfLe"
			   "EoS4E5c=";
	}
	assert(0);
	return NULL;
}

void accountSendLoginPacket(
	NetLink *link, const char * pLoginField, const char * pPassword, bool bPasswordHashed,
	U32 iSalt, const char * pFixedSalt, bool bMachineID, const char * pPreEncryptedPassword)
{
	AccountNetStruct_ToAccountServerLogin sendStruct = {0};
	Packet *response = NULL;
	char hashedCrypticPass[MAX_PASSWORD] = {0};
	char hashedPWEPass[MAX_PASSWORD] = {0};
	char hashedPWEPassFixedSalt[MAX_PASSWORD] = {0};

	if (bPasswordHashed)
	{
		// If it's a PWE hash, it must be a SALTED one
		strcpy(hashedPWEPass, pPassword);
		strcpy(hashedPWEPassFixedSalt, pPassword);

		// If it's a Cryptic hash, it must be an UNSALTED one
		strcpy(hashedCrypticPass, pPassword);
	}
	else
	{
		sendStruct.eKeyVersion = accountServerIsLive() ? ASKEY_prod_1 : ASKEY_dev_1;

		accountHashPassword(pPassword, hashedCrypticPass);
		accountPWHashPasswordFixedSalt(pFixedSalt, pPassword, hashedPWEPassFixedSalt);
		accountPWHashPassword(pLoginField, pPassword, hashedPWEPass);

		if (!nullStr(pPreEncryptedPassword))
		{
			strcpy(sendStruct.encryptedPassword, pPreEncryptedPassword);
		}
		else
		{
			char encryptedPassword[MAX_PASSWORD_ENCRYPTED] = {0};
			EncryptionState * encryptionState = cryptRSACreateState();

			cryptLoadRSAPublicKey(encryptionState, accountNetGetPubKey(sendStruct.eKeyVersion));
			cryptRSAEncrypt(encryptionState, pPassword, strlen(pPassword), SAFESTR(encryptedPassword));
			encodeBase64String(SAFESTR(encryptedPassword), SAFESTR(sendStruct.encryptedPassword));
			cryptRSADestroyState(encryptionState);
		}
	}

	accountAddSaltToHashedPassword(hashedPWEPass, iSalt, sendStruct.hashedPWPassword);
	accountAddSaltToHashedPassword(hashedPWEPassFixedSalt, iSalt, sendStruct.hashedPWPasswordFixedSalt);
	accountAddAccountNameThenNewStyleSaltToHashedPassword(hashedCrypticPass,
		pLoginField, iSalt, sendStruct.hashedCrypticPassword);

	strncpy(sendStruct.accountName, pLoginField, ARRAY_SIZE_CHECKED(sendStruct.accountName) - 1);
	sendStruct.accountName[ARRAY_SIZE_CHECKED(sendStruct.accountName) - 1] = '\0';

	sendStruct.eLoginType = ACCOUNTLOGINTYPE_CrypticAndPW;
	if (bMachineID) strcpy(sendStruct.machineID, getMachineID());

	// The following could be the server's locale if this is coming from
	// a server monitor page, but the important part is that it do the right
	// thing for customers.
	sendStruct.localeID = getCurrentLocale();

	response = pktCreate(link, TO_ACCOUNTSERVER_NVPSTRUCT_LOGIN);
	ParserSendStructAsCheckedNameValuePairs(response, parse_AccountNetStruct_ToAccountServerLogin, &sendStruct);
	pktSend(&response);

	QA_LOG("Sent new-style login response\n");
}

void accountSendLoginValidatePacket(NetLink * pLink, U32 uAccountID, U32 uTicketID)
{
	Packet * pkt = pktCreate(pLink, TO_ACCOUNTSERVER_LOGINVALIDATE);
	pktSendU32(pkt, uAccountID);
	pktSendU32(pkt, uTicketID);
	pktSend(&pkt);
}

void accountSendGenerateOneTimeCode(NetLink * pLink, U32 uAccountID, U32 uIP, const char * pMachineID)
{
	Packet * pkt = pktCreate(pLink, TO_ACCOUNTSERVER_GENERATE_ONETIMECODE);
	pktSendU32(pkt, uAccountID);
	pktSendString(pkt, pMachineID);
	pktSendU32(pkt, uIP);
	pktSend(&pkt);
}

void accountSendSaveNextMachine(NetLink * pLink, U32 uAccountID, const char * pMachineID, const char * pMachineName, U32 uIP)
{
	Packet * pkt = pktCreate(pLink, TO_ACCOUNTSERVER_SAVENEXTMACHINE);
	pktSendU32(pkt, uAccountID);
	pktSendString(pkt, pMachineID);
	pktSendString(pkt, pMachineName);
	pktSendU32(pkt, uIP);
	pktSend(&pkt);
}

void accountSendOneTimeCode(NetLink * pLink, U32 uAccountID, const char * pMachineID, const char * pOneTimeCode, const char * pMachineName, U32 uIP)
{
	Packet * pkt = pktCreate(pLink, TO_ACCOUNTSERVER_ONETIMECODE);
	pktSendU32(pkt, uAccountID);
	pktSendString(pkt, pMachineID);
	pktSendString(pkt, pOneTimeCode);
	pktSendString(pkt, pMachineName ? pMachineName : "");
	pktSendU32(pkt, uIP);
	pktSend(&pkt);
}

typedef struct AccountLoginLink {
	char *pLoginField; // Whatever the user typed in (account name or e-mail address)
	char *pPassword; // Plain-text
	bool bPasswordHashed;
	AccountLoginType eLoginType;
	U32 salt;

	// Data for callbacks
	FailedLoginCallback failed_cb;
	PacketCallback* login_cb;
	void *userData;
} AccountLoginLink;

typedef struct AccountCreateLink {
	char *pAccountName;
	char *pPassword;
} AccountCreateLink;

static void accountLinkDisconnect(NetLink* link,void *user_data)
{
	AccountLoginLink *linkData = (AccountLoginLink*) user_data;
	if (linkData)
	{
		if (linkData->failed_cb)
		{
			linkData->failed_cb("Lost connection to Account Server", linkData->userData);
		}
		SAFE_FREE(linkData->pLoginField);
		SAFE_FREE(linkData->pPassword);
	}
	gpAccountLink = NULL;
}

static void accountSaltResponseCallback(Packet* pak, int cmd, NetLink* link,void *user_data)
{
	AccountLoginLink *linkData = (AccountLoginLink*) user_data;

	if (!linkData)
	{
		return;
	}

	switch (cmd)
	{		
	case FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT:
		{
			AccountNetStruct_FromAccountServerLoginSalt recvStruct = {0};
			Packet *response = NULL;
			char * fixedSalt = NULL;

			QA_LOG("Received salted login request\n");

			if (!ParserReceiveStructAsCheckedNameValuePairs(pak, parse_AccountNetStruct_FromAccountServerLoginSalt, &recvStruct))
			{
				Errorf("Receive failure in FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT");
				return;
			}

			if (!recvStruct.eSaltType && recvStruct.iSalt)
			{
				Errorf("Got invalid data in FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT");
				return;
			}

			linkData->salt = recvStruct.iSalt;

			switch (recvStruct.eSaltType)
			{
			xcase LOGINSALTTYPE_SALT_WITH_ACNTNAME_THEN_SHORT_TERM_SALT:
				fixedSalt = linkData->pLoginField;
			xcase LOGINSALTTYPE_SALT_WITH_FIXED_THEN_SHORT_TERM_SALT:
				fixedSalt = recvStruct.pFixedSalt;
			xdefault:
				Errorf("Got unknown LOGINSALTTYPE_SALT in FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT");
				return;
			}

			accountSendLoginPacket(link,
				linkData->pLoginField, linkData->pPassword, linkData->bPasswordHashed, linkData->salt, fixedSalt,
				true, NULL);
		}
	break;

	xcase FROM_ACCOUNTSERVER_LOGIN:	
	case FROM_ACCOUNTSERVER_LOGIN_NEW:
	case FROM_ACCOUNTSERVER_FAILED:
	case FROM_ACCOUNTSERVER_LOGIN_FAILED:
	case FROM_ACCOUNTSERVER_LOGINVALIDATE_FAILED:
	case FROM_ACCOUNTSERVER_LOGIN_XBOX:
	case FROM_ACCOUNTSERVER_LOGIN_FAILED_XBOX:
		
		if (linkData->login_cb && (FROM_ACCOUNTSERVER_LOGIN_XBOX == cmd || FROM_ACCOUNTSERVER_LOGIN_FAILED_XBOX == cmd || linkData->salt)) // We don't have the salt for XUID logins
		{
			linkData->login_cb(pak, cmd, link, linkData->userData);
		}
		if (linkGetUserData(link)) // might be freed in callback
		{
			linkData->salt = 0;
			linkData->failed_cb = NULL; // set this to NULL so that failed_cb doesn't get called when disconnected
		}
	xdefault:
		break;
	}
}

static void accountClearCreateLinkData(void)
{
	if (gpAccountLink)
	{
		AccountCreateLink *createData = linkGetUserData(gpAccountLink);
		if (createData)
		{
			if (createData->pAccountName)
				free(createData->pAccountName);
			if (createData->pPassword)
				free(createData->pPassword);
			free(createData);
			linkSetUserData(gpAccountLink, NULL);
		}
	}
}

static void accountClearLoginLinkData(void)
{
	if (gpAccountLink)
	{
		AccountLoginLink *linkData = linkGetUserData(gpAccountLink);
		if (linkData)
		{
			SAFE_FREE(linkData->pLoginField);
			SAFE_FREE(linkData->pPassword);
			free(linkData);
			linkSetUserData(gpAccountLink, NULL);
		}
	}
}

void accountValidateCloseAccountServerLink(void)
{
	accountClearLoginLinkData();
	if (gpAccountLink)
		linkRemove(&gpAccountLink);
}

void accountCancelLogin(void)
{
	accountClearLoginLinkData();
	linkRemove(&gpAccountLink);
}

U32 getDirectAccountConnectionFlags(void)
{
	U32 uFlags = LINK_FORCE_FLUSH;

	if (!gbCompressDirectAccountLink)
		uFlags |= LINK_NO_COMPRESS;

	return uFlags;
}

U32 getPersistentAccountConnectionFlags(void)
{
	U32 uFlags = LINK_FORCE_FLUSH;

	if (!gbCompressPersistentAccountLinks)
		uFlags |= LINK_NO_COMPRESS;

	return uFlags;
}

static void accountValidateAddLinkInfo(SA_PARAM_NN_VALID AccountValidateData *pValidateData)
{
	AccountLoginLink * loginInfo = calloc(1, sizeof(AccountLoginLink));
	loginInfo->pLoginField = strdup(pValidateData->pLoginField);
	loginInfo->pPassword = StructAllocString(pValidateData->pPassword);
	loginInfo->bPasswordHashed = pValidateData->bPasswordHashed;
	loginInfo->salt = 0;
	loginInfo->login_cb = pValidateData->login_cb;
	loginInfo->failed_cb = pValidateData->failed_cb;
	loginInfo->userData = pValidateData->userData;
	loginInfo->eLoginType = pValidateData->eLoginType;
	linkSetUserData(gpAccountLink, loginInfo);
}

// ACCOUNTLOGINTYPE_CrypticAndPW - main hash = Cryptic, backup hash = Perfect World
NetLink * accountValidateInitializeLinkEx(AccountValidateData *pValidateData)
{
	accountClearLoginLinkData();
	linkRemove(&gpAccountLink);
	
	// TODO: These should both do better checking. <NPK 2009-02-13>
	if(!pValidateData->pLoginField || !pValidateData->pLoginField[0])
	{
		if (pValidateData->failed_cb)
			pValidateData->failed_cb("Invalid username", pValidateData->userData);
		return NULL;
	}
	else if(!pValidateData->pPassword)
	{
		if (pValidateData->failed_cb)
			pValidateData->failed_cb("Invalid password", pValidateData->userData);
		return NULL;
	}

	gpAccountLink = commConnect(commDefault(), LINKTYPE_UNSPEC, getDirectAccountConnectionFlags(), getAccountServer(),
		DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, accountSaltResponseCallback, pValidateData->connect_cb, accountLinkDisconnect, 0);

	if (gpAccountLink)
	{
		linkSetKeepAlive(gpAccountLink);
	}
	else
	{
		if (pValidateData->failed_cb)
			pValidateData->failed_cb("Couldn't even attempt account server connection", pValidateData->userData);
		return NULL;
	}

	accountValidateAddLinkInfo(pValidateData);
	return gpAccountLink;
}

// Really really stupid function to hack in support for the horrible pointer copying we're doing with accountValidateInitializeLink
// The link passed in here MUST be a pointer to gpAccountLink
bool accountValidateWaitForLink(NetLink **pLink, F32 timeout)
{
	bool bResult;

	if(!pLink)
	{
		return false;
	}

	assert(*pLink == gpAccountLink);
	bResult = linkConnectWait(pLink, timeout);

	if(!bResult)
	{
		gpAccountLink = NULL;
	}

	return bResult;
}

int accountValidateStartLoginProcess(const char * pLoginField)
{
	if (linkConnected(gpAccountLink))
	{
		accountSendSaltRequest(gpAccountLink, pLoginField);
		return 1;
	}
	return 0;
}

// ---------------------------------------------------------------------------------------------

static void validatorPacketCallback (Packet* pak, int cmd, NetLink* link, void *user_data);
static void AccountServerResponseCallback (Packet* pak, int cmd, NetLink* link, void *user_data)
{
	switch (cmd)
	{	
	xcase FROM_ACCOUNTSERVER_LOGINSALT:
	case FROM_ACCOUNTSERVER_LOGIN_NEW:
	case FROM_ACCOUNTSERVER_LOGIN:	
	case FROM_ACCOUNTSERVER_FAILED:
	case FROM_ACCOUNTSERVER_LOGIN_FAILED:
	case FROM_ACCOUNTSERVER_LOGINVALIDATE_FAILED:
	case FROM_ACCOUNTSERVER_LOGIN_XBOX:
	case FROM_ACCOUNTSERVER_LOGIN_FAILED_XBOX:
	case FROM_ACCOUNTSERVER_ONETIMECODEVALIDATE_RESPONSE:
	case FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT:
		validatorPacketCallback(pak, cmd, link, user_data);
	}
}

static void validatorDisconnectCallback(NetLink* link, void *user_data);
static void AccountServerDisconnectCallback(NetLink *link, void *user_data)
{
	validatorDisconnectCallback(link, user_data);
}

typedef enum AccountValidatorState
{
	ACCOUNTVALIDATORSTATE_DISCONNECTED = 0,
	ACCOUNTVALIDATORSTATE_CONNECTING,
	ACCOUNTVALIDATORSTATE_CONNECTED_NEEDS_SALT,
	ACCOUNTVALIDATORSTATE_CONNECTED_SENT_SALTED_PASS,

	ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE, // Connected to AS, sent packet, and is expecting a final response for callbacks
	ACCOUNTVALIDATORSTATE_CONNECTED_DONE, // Conncted to AS, sent packet, and does not expect a response (is done)

	ACCOUNTVALIDATORSTATE_COUNT
} AccountValidatorState;

typedef enum AccountValidatorRequestType
{
	ACCOUNTVALIDATORTYPE_ValidateTicket = 0,
	ACCOUNTVALIDATORTYPE_FullLogin,
	ACCOUNTVALIDATORTYPE_GenerateOneTimeCode,
	ACCOUNTVALIDATORTYPE_ValidateOneTimeCode,
	ACCOUNTVALIDATORTYPE_SaveNextMachine,
} AccountValidatorRequestType;

typedef struct AccountValidator
{
	AccountValidatorState  state;
	AccountValidatorResult result;
	char *failureReason;
	char *formattedFailureReason;					// Temporary storage for a free-form failure reason
	LoginFailureCode failureCode;
	NetLink *link;
	char *loginField; // Account name or e-mail; whatever they typed in
	char *plaintextPassword;
	char *ticket;
	bool accountServerSupportsLoginFailedPacket;	// If true, use FROM_ACCOUNTSERVER_LOGIN_FAILED instead of FROM_ACCOUNTSERVER_FAILED

	U32 uAccountID;
	U32 uTicketID;
	char *pMachineID;
	char *pOneTimeCode;
	char *pMachineName;
	U32 uIP; // Client's IP
	AccountValidatorRequestType eRequestType;

	bool bPersistLink;
	DWORD uTimerStart;
	AccountValidator **ppTicketRequests;
	AccountValidator *pParent;
} AccountValidator;

#define ACCOUNT_VALIDATOR_CONNECT_TIMEOUT 5000
#define ACCOUNT_VALIDATOR_WAIT_TIMEOUT 30000

AccountValidator * accountValidatorCreate()
{
	AccountValidator *pValidator = calloc(1, sizeof(AccountValidator));
	pValidator->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
	pValidator->result = ACCOUNTVALIDATORRESULT_IDLE;
	return pValidator;
}

void accountValidatorDestroyLink(AccountValidator *pValidator)
{
	int i;
	if (pValidator->pParent)
	{
		accountValidatorDestroyLink(pValidator->pParent);
		return;
	}

	if(pValidator->link)
		linkRemove(&pValidator->link);
	pValidator->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;

	for (i=eaSize(&pValidator->ppTicketRequests)-1; i>=0; i--)
	{
		pValidator->ppTicketRequests[i]->link = NULL;
		pValidator->ppTicketRequests[i]->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
		switch(pValidator->ppTicketRequests[i]->state)
		{
		case ACCOUNTVALIDATORSTATE_CONNECTING:
		case ACCOUNTVALIDATORSTATE_CONNECTED_NEEDS_SALT:
		case ACCOUNTVALIDATORSTATE_CONNECTED_SENT_SALTED_PASS:
		case ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE:
			{
				pValidator->ppTicketRequests[i]->result = ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT;
			}
		};
	}
}
void accountValidatorDestroy(AccountValidator *pValidator)
{
	int i;
	if (!pValidator)
		return;
	if(pValidator->link && !pValidator->bPersistLink)
		linkRemove(&pValidator->link); // persisted links need to be manually destroyed
	estrDestroy(&pValidator->loginField);
	estrDestroy(&pValidator->plaintextPassword);
	estrDestroy(&pValidator->ticket);

	for (i=eaSize(&pValidator->ppTicketRequests)-1; i>=0; i--)
		free(pValidator->ppTicketRequests[i]);
	eaDestroy(&pValidator->ppTicketRequests);
	free(pValidator->failureReason);

	if (pValidator->pParent)
	{
		eaFindAndRemove(&pValidator->pParent->ppTicketRequests, pValidator);
	}

	free(pValidator);
}

bool accountValidatorIsReady(AccountValidator *pValidator)
{
	return (accountValidatorGetResult(pValidator) != ACCOUNTVALIDATORRESULT_STILL_PROCESSING);
}

AccountValidatorResult accountValidatorGetResult(AccountValidator *pValidator)
{
	return pValidator->result;
}

const char *accountValidatorGetFailureReason(AccountValidator *pValidator)
{
	// If there's a free-form login failure string, return it.
	if (pValidator->failureReason)
		return pValidator->failureReason;

	return accountValidatorGetFailureReasonByCode(pValidator, pValidator->failureCode);
}

const char *accountValidatorGetFailureReasonByCode(AccountValidator *pValidator, LoginFailureCode code)
{

#define LOGIN_GENERIC_FAILURE "Invalid username or password."

	static const char *const table[] = {
		/* LoginFailureCode_Unknown */					NULL,
		/* LoginFailureCode_Ok */						NULL,
		/* LoginFailureCode_NotFound */					LOGIN_GENERIC_FAILURE,
		/* LoginFailureCode_BadPassword */				LOGIN_GENERIC_FAILURE,
		/* LoginFailureCode_RateLimit */				"Too many attempts; please try again later.",
		/* LoginFailureCode_Disabled */					"This account has been disabled.",
		/* LoginFailureCode_UnlinkedPWCommonAccount */	"No Cryptic account linked to Perfect World credentials.",
		/* LoginFailureCode_InvalidTicket */			"Invalid login ticket.",
		/* LoginFailureCode_DisabledLinked */			"Please log in using your Perfect World account name.",
		/* LoginFailureCode_InvalidLoginType */         NULL,
		/* LoginFailureCode_Banned */                   "This account has been banned.",
		/* LoginFailureCode_NewMachineID */             LOGIN_GENERIC_FAILURE, // Unsupported by systems that use this function
		/* LoginFailureCode_CrypticDisabled */			"This account must be linked to a Perfect World account to log in.",
	};

#undef LOGIN_GENERIC_FAILURE

	if ((size_t)code >= sizeof(table)/sizeof(*table) || !table[code])
	{
		char **buffer;
		char *formatting = NULL;
		if (pValidator)
			buffer = &pValidator->formattedFailureReason;
		else
			buffer = &spFormattedFailureReason;
		SAFE_FREE(*buffer);
		estrStackCreate(&formatting);
		estrPrintf(&formatting, "Unknown failure code: %u", (unsigned)code);
		*buffer = strdup(formatting);
		estrDestroy(&formatting);
		return *buffer;
	}

	return table[code];
}

LoginFailureCode accountValidatorGetFailureCode(AccountValidator *pValidator)
{
	return pValidator->failureCode;
}

void accountValidatorTick(AccountValidator *pValidator)
{
	int i;
	DWORD curTick = GetTickCount();
	bool bFailedConnect = false;

	if (spForceAccountTicket && *spForceAccountTicket)
	{
		if (!spTicketText)
		{
			int size;
			spTicketText = fileAlloc(spForceAccountTicket, &size);
			assert(spTicketText);
		}
		if (*spTicketText)
		{
			estrCopy2(&pValidator->ticket, spTicketText);
			pValidator->result = ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_SUCCESS;
		}
		else
		{
			pValidator->result = ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_FAILED;
			pValidator->failureCode = LoginFailureCode_NotFound;
			pValidator->failureReason = strdup("Test login failure");
		}
		pValidator->state  = ACCOUNTVALIDATORSTATE_DISCONNECTED;
		if (!pValidator->bPersistLink)
			linkRemove(&pValidator->link);
		for (i=eaSize(&pValidator->ppTicketRequests)-1; i>=0; i--)
			accountValidatorTick(pValidator->ppTicketRequests[i]);
	}

	switch(pValidator->state)
	{
	xcase ACCOUNTVALIDATORSTATE_CONNECTING:
		{
			if(linkConnected(pValidator->link))
			{
				switch (pValidator->eRequestType)
				{
				case ACCOUNTVALIDATORTYPE_FullLogin: // Always the first packet to the AS
					accountSendSaltRequest(pValidator->link, pValidator->loginField);
					pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTED_NEEDS_SALT;
				xcase ACCOUNTVALIDATORTYPE_ValidateTicket: // Always the second packet; contains the password
					accountSendLoginValidatePacket(pValidator->link, pValidator->uAccountID, pValidator->uTicketID);
					pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE;
				xcase ACCOUNTVALIDATORTYPE_GenerateOneTimeCode: // If we want Account Guard for a new machine, send this
					accountSendGenerateOneTimeCode(pValidator->link, pValidator->uAccountID, pValidator->uIP, pValidator->pMachineID);
					pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTED_DONE;
					pValidator->result = ACCOUNTVALIDATORRESULT_OTC_GENERATED;
				xcase ACCOUNTVALIDATORTYPE_SaveNextMachine: // If we want Account Guard for a new machine and we were told to bypass it, send this
					accountSendSaveNextMachine(pValidator->link, pValidator->uAccountID, pValidator->pMachineID, pValidator->pMachineName, pValidator->uIP);
					pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTED_DONE;
					pValidator->result = ACCOUNTVALIDATORRESULT_SAVENEXTMACHINE_SUCCESS;
				xcase ACCOUNTVALIDATORTYPE_ValidateOneTimeCode: // Contains the actual one time code sent to the user via e-mail
					accountSendOneTimeCode(pValidator->link, pValidator->uAccountID,
						pValidator->pMachineID, pValidator->pOneTimeCode, pValidator->pMachineName, pValidator->uIP);
					pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE;
				}
			}
			else if (pValidator->uTimerStart)
			{
				if (curTick - pValidator->uTimerStart > ACCOUNT_VALIDATOR_CONNECT_TIMEOUT)
				{
					pValidator->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
					pValidator->result = ACCOUNTVALIDATORRESULT_FAILED_CONN_TIMEOUT;
					bFailedConnect = true;
				}
			}
		}
	xdefault:
		if (pValidator->uTimerStart)
		{
			if (curTick - pValidator->uTimerStart > ACCOUNT_VALIDATOR_WAIT_TIMEOUT)
			{
					pValidator->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
					pValidator->result = ACCOUNTVALIDATORRESULT_FAILED_GENERIC;
					bFailedConnect = true;
			}
		}
	};
	
	if (bFailedConnect)
	{
		if (!pValidator->bPersistLink)
			accountValidatorDestroyLink(pValidator);
	}
	else
	{
		for (i=eaSize(&pValidator->ppTicketRequests)-1; i>=0; i--)
			accountValidatorTick(pValidator->ppTicketRequests[i]);
	}
}

static AccountValidator *accountValidatorFindTicketRequest (AccountValidator *pBaseValidator, U32 uAccountID);
static void validatorPacketCallback(Packet* pak, int cmd, NetLink* link, void *user_data)
{
	AccountValidator *pValidator = (AccountValidator *)user_data;

	switch (cmd)
	{	
	xcase FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT:
		{
			AccountNetStruct_FromAccountServerLoginSalt recvStruct = {0};
			const char * pFixedSalt = NULL;

			QA_LOG("Received new-style login request\n");

			if (!ParserReceiveStructAsCheckedNameValuePairs(pak, parse_AccountNetStruct_FromAccountServerLoginSalt, &recvStruct))
			{
				Errorf("Receive failure in FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT");
				return;
			}

			if (!recvStruct.eSaltType && recvStruct.iSalt)
			{
				Errorf("Got invalid data in FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT");
				return;
			}
			
			switch (recvStruct.eSaltType)
			{
			xcase LOGINSALTTYPE_SALT_WITH_ACNTNAME_THEN_SHORT_TERM_SALT:
				pFixedSalt = pValidator->loginField;
			xcase LOGINSALTTYPE_SALT_WITH_FIXED_THEN_SHORT_TERM_SALT:
				pFixedSalt = recvStruct.pFixedSalt;
			xdefault:
				Errorf("Got unknown LOGINSALTTYPE_SALT in FROM_ACCOUNTSERVER_NVPSTRUCT_LOGIN_SALT");
				return;
			}

			accountSendLoginPacket(link,
				pValidator->loginField, pValidator->plaintextPassword, false, recvStruct.iSalt, pFixedSalt,
				false, NULL);

			pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTED_SENT_SALTED_PASS;
		}
	xcase FROM_ACCOUNTSERVER_LOGIN_NEW:
	case FROM_ACCOUNTSERVER_LOGIN_XBOX:
		{
			Packet *response = NULL;
			U32 uAccountID = pktGetU32(pak);
			U32 uTicketID = pktGetU32(pak);

			pValidator->uAccountID = uAccountID;
			pValidator->uTicketID = uTicketID;

			accountSendLoginValidatePacket(link, uAccountID, uTicketID);
			pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE;
		}
	xcase FROM_ACCOUNTSERVER_LOGIN:	
		{
			char *tempString = pktGetStringTemp(pak);
			if (pktCheckRemaining(pak, sizeof(U32)))
			{
				U32 uAccountID = pktGetU32(pak);
				pValidator = accountValidatorFindTicketRequest(pValidator, uAccountID);
				if (!pValidator)
					return;
			}
			estrCopy2(&pValidator->ticket, tempString);

			pValidator->result = ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_SUCCESS;
			pValidator->state  = ACCOUNTVALIDATORSTATE_DISCONNECTED;
			if (!pValidator->bPersistLink)
				linkRemove(&pValidator->link);
		}
	xcase FROM_ACCOUNTSERVER_FAILED:
	acase FROM_ACCOUNTSERVER_LOGIN_FAILED:
	acase FROM_ACCOUNTSERVER_LOGINVALIDATE_FAILED:
	acase FROM_ACCOUNTSERVER_LOGIN_FAILED_XBOX:
		{
			char *msg = NULL;
			LoginFailureCode code = LoginFailureCode_Unknown;

			// Ticket verification failure
			if (cmd == FROM_ACCOUNTSERVER_LOGINVALIDATE_FAILED && pktCheckRemaining(pak, sizeof(U32)))
			{
				pValidator->accountServerSupportsLoginFailedPacket = true;
				code = pktGetU32(pak);
			}

			// Legacy ticket verification failure
			else if (cmd == FROM_ACCOUNTSERVER_FAILED && pValidator->accountServerSupportsLoginFailedPacket && pktCheckRemaining(pak, sizeof(U32)))
			{
				U32 uAccountID;
				msg = pktGetStringTemp(pak);
				uAccountID = pktGetU32(pak);
				pValidator = accountValidatorFindTicketRequest(pValidator, uAccountID);
				if (!pValidator)
					return;
			}

			// Login failure, with code
			else if (cmd == FROM_ACCOUNTSERVER_LOGIN_FAILED)
			{
				pValidator->accountServerSupportsLoginFailedPacket = true;
				code = pktGetU32(pak);
			}

			// Legacy login failure, no code
			else if (!pValidator->accountServerSupportsLoginFailedPacket)
				msg = pktGetStringTemp(pak);

			// Save result.
			pValidator->result = ACCOUNTVALIDATORRESULT_SUCCESS_AUTH_FAILED;
			pValidator->state  = ACCOUNTVALIDATORSTATE_DISCONNECTED;
			pValidator->failureCode = code;
			pValidator->failureReason = msg ? strdup(msg) : NULL;
			if (!pValidator->bPersistLink)
				linkRemove(&pValidator->link);
		}
	xcase FROM_ACCOUNTSERVER_ONETIMECODEVALIDATE_RESPONSE:
		{
			LoginFailureCode failureCode = pktGetU32(pak);
			U32 uAccountID = pktGetU32(pak);
			pValidator = accountValidatorFindTicketRequest(pValidator, uAccountID);
			if (!pValidator)
				return;
			if (failureCode == LoginFailureCode_Ok)
				pValidator->result = ACCOUNTVALIDATORRESULT_OTC_SUCCESS;
			else
				pValidator->result = ACCOUNTVALIDATORRESULT_OTC_FAILED;
			if (!pValidator->bPersistLink)
				linkRemove(&pValidator->link);
		}
	}
}

static void validatorDisconnectCallback(NetLink* link,void *user_data)
{
	AccountValidator *pValidator = (AccountValidator *)user_data;
	int i;

	if(pValidator->link)
	{
		linkRemove(&pValidator->link);
	}
	for (i=eaSize(&pValidator->ppTicketRequests)-1; i>=0; i--)
		pValidator->ppTicketRequests[i]->link = NULL;

	switch(pValidator->state)
	{
	case ACCOUNTVALIDATORSTATE_CONNECTING:
	case ACCOUNTVALIDATORSTATE_CONNECTED_NEEDS_SALT:
	case ACCOUNTVALIDATORSTATE_CONNECTED_SENT_SALTED_PASS:
	case ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE:
		{
			pValidator->result = ACCOUNTVALIDATORRESULT_FAILED_GENERIC;
		}
	};

	pValidator->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
	
	for (i=eaSize(&pValidator->ppTicketRequests)-1; i>=0; i--)
	{
		pValidator->ppTicketRequests[i]->link = NULL;
		switch(pValidator->ppTicketRequests[i]->state)
		{
		case ACCOUNTVALIDATORSTATE_CONNECTING:
		case ACCOUNTVALIDATORSTATE_CONNECTED_NEEDS_SALT:
		case ACCOUNTVALIDATORSTATE_CONNECTED_SENT_SALTED_PASS:
		case ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE:
			{
				pValidator->ppTicketRequests[i]->result = ACCOUNTVALIDATORRESULT_FAILED_GENERIC;
			}
		};
		pValidator->ppTicketRequests[i]->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
	}
}

// Semi-deprecated function only used by XMPP_Login and Server Monitor
void accountValidatorRequestTicket(AccountValidator *pValidator, const char *pLogin, const char *pPassword)
{
	switch(pValidator->state)
	{
	case ACCOUNTVALIDATORSTATE_CONNECTING:
	case ACCOUNTVALIDATORSTATE_CONNECTED_NEEDS_SALT:
	case ACCOUNTVALIDATORSTATE_CONNECTED_SENT_SALTED_PASS:
	case ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE:
		{
			if (!pValidator->bPersistLink)
				linkRemove(&pValidator->link);
			pValidator->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
		}
	};

	pValidator->result = ACCOUNTVALIDATORRESULT_STILL_PROCESSING;

	pValidator->link = commConnect(accountCommDefault(), LINKTYPE_UNSPEC, getDirectAccountConnectionFlags(), getAccountServer(),
		DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, AccountServerResponseCallback, 0, AccountServerDisconnectCallback, 0);

	if (!pValidator->link)
	{
		pValidator->state =  ACCOUNTVALIDATORSTATE_DISCONNECTED;
		pValidator->result = ACCOUNTVALIDATORRESULT_FAILED_GENERIC;
		return;
	}

	linkSetUserData(pValidator->link, pValidator);
	linkSetKeepAliveSeconds(pValidator->link, 5.0f);

	estrCopy2(&pValidator->loginField, pLogin);
	estrCopy2(&pValidator->plaintextPassword, pPassword);
	pValidator->eRequestType = ACCOUNTVALIDATORTYPE_FullLogin;
	pValidator->uTimerStart = GetTickCount();

	pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTING;
}

void accountValidatorTicketValidate(AccountValidator *pValidator, U32 uAccountID, U32 uTicketID)
{
	switch(pValidator->state)
	{
	case ACCOUNTVALIDATORSTATE_CONNECTING:
	case ACCOUNTVALIDATORSTATE_CONNECTED_NEEDS_SALT:
	case ACCOUNTVALIDATORSTATE_CONNECTED_SENT_SALTED_PASS:
	case ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE:
		{
			if (!pValidator->bPersistLink)
				linkRemove(&pValidator->link);
			pValidator->state = ACCOUNTVALIDATORSTATE_DISCONNECTED;
		}
	};

	pValidator->result = ACCOUNTVALIDATORRESULT_STILL_PROCESSING;

	if (!pValidator->bPersistLink)
	{
		pValidator->link = commConnect(accountCommDefault(), LINKTYPE_UNSPEC, getDirectAccountConnectionFlags(), getAccountServer(),
			DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, AccountServerResponseCallback, 0, AccountServerDisconnectCallback, 0);
	}

	if (!pValidator->link)
	{
		pValidator->state =  ACCOUNTVALIDATORSTATE_DISCONNECTED;
		pValidator->result = ACCOUNTVALIDATORRESULT_FAILED_GENERIC;
		return;
	}

	if (!pValidator->bPersistLink)
	{
		linkSetUserData(pValidator->link, pValidator);
		linkSetKeepAliveSeconds(pValidator->link, 5.0f);
	}

	pValidator->uAccountID = uAccountID;
	pValidator->uTicketID = uTicketID;
	pValidator->eRequestType = ACCOUNTVALIDATORTYPE_ValidateTicket;
	pValidator->uTimerStart = GetTickCount();

	pValidator->state = ACCOUNTVALIDATORSTATE_CONNECTING;
}

void accountValidatorRestartValidation(AccountValidator *pValidator)
{
	if (pValidator->pParent && pValidator->bPersistLink)
	{
		if (!pValidator->pParent->link)
		{
			pValidator->pParent->link = commConnect(accountCommDefault(), LINKTYPE_UNSPEC, getPersistentAccountConnectionFlags(), getAccountServer(),
				DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, AccountServerResponseCallback, 0, AccountServerDisconnectCallback, 0);
			if (pValidator->pParent->link)
			{
				linkSetUserData(pValidator->pParent->link, pValidator->pParent);
				linkSetKeepAliveSeconds(pValidator->pParent->link, 5.0f);
			}
			// No link case will fail in accountValidatorTicketValidate
		}
		pValidator->link = pValidator->pParent->link;
	}
	accountValidatorTicketValidate(pValidator, pValidator->uAccountID, pValidator->uTicketID);
}

bool accountValidatorGetTicket(AccountValidator *pValidator, char **estr)
{
	if(pValidator->ticket)
	{
		estrCopy2(estr, pValidator->ticket);
		return true;
	}

	return false;
}

void accountSetValidatorPersistLink (AccountValidator *pValidator, bool bPersistLink)
{
	pValidator->bPersistLink = bPersistLink;
	if (bPersistLink)
	{
		if (!pValidator->link)
		{
			pValidator->link = commConnect(accountCommDefault(), LINKTYPE_UNSPEC, getPersistentAccountConnectionFlags(), getAccountServer(),
				DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, AccountServerResponseCallback, 0, AccountServerDisconnectCallback, 0);
		}
		if(pValidator->link)
		{
			linkSetUserData(pValidator->link, pValidator);
			linkSetKeepAliveSeconds(pValidator->link, 5.0f);
			linkConnectWait(&pValidator->link, 5.0f); // wait 5 seconds for connection, TODO in failure?
		}
		// No error if commConnect failed
	}
}

AccountValidator *** accountValidatorGetValidateRequests(AccountValidator *pBaseValidator)
{
	return &pBaseValidator->ppTicketRequests;
}

void accountValidatorRemoveValidateRequest(AccountValidator *pBaseValidator, AccountValidator *pChildValidator)
{
	eaFindAndRemove(&pBaseValidator->ppTicketRequests, pChildValidator);
}

static AccountValidator *accountValidatorCreateChild(AccountValidator *pBaseValidator)
{
	AccountValidator *pChildValidator;
	assertmsg(pBaseValidator->bPersistLink, "Base AccountValidator must have PersistLink set");

	pChildValidator = calloc(1, sizeof(AccountValidator));
	pChildValidator->bPersistLink = true;

	if (!pBaseValidator->link)
	{
		pBaseValidator->link = commConnect(accountCommDefault(), LINKTYPE_UNSPEC, getPersistentAccountConnectionFlags(), getAccountServer(),
			DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, AccountServerResponseCallback, 0, AccountServerDisconnectCallback, 0);
		if (!pBaseValidator->link)
		{
			free(pChildValidator);
			return NULL;
		}
		linkSetUserData(pBaseValidator->link, pBaseValidator);
		linkSetKeepAliveSeconds(pBaseValidator->link, 5.0f);
	}
	pChildValidator->link = pBaseValidator->link;
	pChildValidator->pParent = pBaseValidator;
	eaPush(&pBaseValidator->ppTicketRequests, pChildValidator);
	return pChildValidator;
}
AccountValidator *accountValidatorAddValidateRequest(AccountValidator *pBaseValidator, U32 uAccountID, U32 uTicketID)
{
	AccountValidator *pChildValidator = accountValidatorCreateChild(pBaseValidator);
	if (!pChildValidator)
		return NULL;
	accountValidatorTicketValidate(pChildValidator, uAccountID, uTicketID);
	return pChildValidator;
}

AccountValidator *accountValidatorGenerateOneTimeCode(AccountValidator *pBaseValidator, U32 uAccountID, const char *pMachineID, U32 uClientIP)
{
	AccountValidator *pChildValidator = accountValidatorCreateChild(pBaseValidator);
	if (!pChildValidator)
		return NULL;	
	pChildValidator->result = ACCOUNTVALIDATORRESULT_STILL_PROCESSING;
	pChildValidator->uAccountID = uAccountID;
	pChildValidator->pMachineID = StructAllocString(pMachineID);
	pChildValidator->uIP = uClientIP;
	pChildValidator->eRequestType = ACCOUNTVALIDATORTYPE_GenerateOneTimeCode;
	pChildValidator->uTimerStart = GetTickCount();
	pChildValidator->state = ACCOUNTVALIDATORSTATE_CONNECTING;
	return pChildValidator;
}

AccountValidator *accountValidatorSaveNextMachine(AccountValidator *pBaseValidator, U32 uAccountID, SA_PARAM_NN_STR const char *pMachineID, 
	SA_PARAM_NN_STR const char *pMachineName, U32 uClientIP)
{
	AccountValidator *pChildValidator = accountValidatorCreateChild(pBaseValidator);
	if (!pChildValidator)
		return NULL;	
	pChildValidator->result = ACCOUNTVALIDATORRESULT_STILL_PROCESSING;
	pChildValidator->uAccountID = uAccountID;
	pChildValidator->pMachineID = StructAllocString(pMachineID);
	pChildValidator->pMachineName = StructAllocString(pMachineName);
	pChildValidator->uIP = uClientIP;
	pChildValidator->eRequestType = ACCOUNTVALIDATORTYPE_SaveNextMachine;
	pChildValidator->uTimerStart = GetTickCount();
	pChildValidator->state = ACCOUNTVALIDATORSTATE_CONNECTING;
	return pChildValidator;
}


AccountValidator *accountValidatorAddOneTimeCodeValidation(AccountValidator *pBaseValidator, U32 uAccountID, 
	SA_PARAM_NN_STR const char *pMachineID, SA_PARAM_NN_STR const char *pOneTimeCode, const char *pMachineName, 
	U32 uClientIP)
{
	AccountValidator *pChildValidator = accountValidatorCreateChild(pBaseValidator);
	if (!pChildValidator)
		return NULL;	
	pChildValidator->result = ACCOUNTVALIDATORRESULT_STILL_PROCESSING;
	pChildValidator->uAccountID = uAccountID;
	pChildValidator->pMachineID = StructAllocString(pMachineID);
	pChildValidator->pOneTimeCode = StructAllocString(pOneTimeCode);
	pChildValidator->pMachineName = StructAllocString(pMachineName);
	pChildValidator->uIP = uClientIP;
	pChildValidator->eRequestType = ACCOUNTVALIDATORTYPE_ValidateOneTimeCode;
	pChildValidator->uTimerStart = GetTickCount();
	pChildValidator->state = ACCOUNTVALIDATORSTATE_CONNECTING;
	return pChildValidator;
}

AccountValidator *accountValidatorFindTicketRequest (AccountValidator *pBaseValidator, U32 uAccountID)
{
	int i;

	if(pBaseValidator->uAccountID == uAccountID && pBaseValidator->state == ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE)
		return pBaseValidator;

	for (i=eaSize(&pBaseValidator->ppTicketRequests)-1; i>=0; i--)
	{
		if (pBaseValidator->ppTicketRequests[i]->uAccountID == uAccountID && 
			pBaseValidator->ppTicketRequests[i]->state == ACCOUNTVALIDATORSTATE_CONNECTED_AWAITING_RESPONSE)
			return pBaseValidator->ppTicketRequests[i];
	}
	return NULL;
}

NetLink *accountValidatorGetAccountServerLink (AccountValidator *pBaseValidator)
{
	assertmsg(pBaseValidator->bPersistLink, "Base AccountValidator must have PersistLink set");

	if (!pBaseValidator->link)
	{
		pBaseValidator->link = commConnect(accountCommDefault(), LINKTYPE_UNSPEC, getPersistentAccountConnectionFlags(), getAccountServer(),
			DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, validatorPacketCallback, 0, AccountServerDisconnectCallback, 0);
		if (pBaseValidator->link)
		{
			linkSetUserData(pBaseValidator->link, pBaseValidator);
			linkSetKeepAliveSeconds(pBaseValidator->link, 5.0f);
		}
	}
	return pBaseValidator->link;
}

static AccountValidator * sAccountValidator = NULL;
AccountValidator *accountValidatorGetPersistent(void)
{
	if (!sAccountValidator)
	{
		sAccountValidator = accountValidatorCreate();
		accountSetValidatorPersistLink(sAccountValidator, true);
	}
	return sAccountValidator;
}


/************************************************************************/
/* Caching                                                              */
/************************************************************************/

bool stashReplaceStruct(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_VALID void *pValue, SA_PARAM_NN_VALID ParseTable pt[])
{
	void *oldValue;
	char *keyOut;

	if (stashGetKey(pTable, pKey, &keyOut))
	{
		// Remove the pointer and destroy the struct
		if (stashRemovePointer(pTable, pKey, &oldValue) && oldValue)
			StructDestroyVoid(pt, oldValue);

		// Re-use the same estring
		return stashAddPointer(pTable, keyOut, pValue, true);
	}

	return stashAddPointer(pTable, estrDup(pKey), pValue, true);
}

bool stashRemoveStruct(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID ParseTable pt[])
{
	void *oldValue;
	char *keyOut;

	if (stashGetKey(pTable, pKey, &keyOut))
	{
		// Remove the pointer and destroy the struct
		if (stashRemovePointer(pTable, pKey, &oldValue) && oldValue)
			StructDestroyVoid(pt, oldValue);

		estrDestroy(&keyOut);

		return true;
	}

	return false;
}

void stashClearStruct(SA_PARAM_NN_VALID StashTable pTable, SA_PARAM_NN_VALID ParseTable pt[])
{
	void *pValue;
	STRING_EARRAY keys = NULL;

	FOR_EACH_IN_STASHTABLE2(pTable, it);
		pValue = stashElementGetPointer(it);
		if (pValue)
			StructDestroyVoid(pt, pValue);

		eaPush(&keys, stashElementGetKey(it));
	FOR_EACH_END;

	stashTableClear(pTable);

	eaClearEString(&keys);
}

SA_RET_NN_STR const char *AccountGetShardProxyName(void)
{
	static char *name = NULL;

	if (!name)
		estrPrintf(&name, "%s%s", GetShardNameFromShardInfoString(), GetShortProductName());

	return name;
}

SA_RET_NN_VALID static StashTable getKeyValueListStashTable(void)
{
	static StashTable table = NULL;

	if (!table) table = stashTableCreateWithStringKeys(1, StashDefault);
	return table;
}

void AccountProxyClearKeyValueList(void)
{
	stashClearStruct(getKeyValueListStashTable(), parse_AccountProxyKeyValueInfo);
}

bool AccountProxyReplaceKeyValue(SA_PARAM_NN_VALID AccountProxyKeyValueInfo *pItem)
{
	return stashReplaceStruct(getKeyValueListStashTable(), pItem->pKey, pItem, parse_AccountProxyKeyValueInfo);
}

bool AccountProxySetKeyValueList(SA_PARAM_OP_VALID AccountProxyKeyValueInfoList *pList)
{
	AccountProxyClearKeyValueList();

	if (!pList) return true;

	EARRAY_CONST_FOREACH_BEGIN(pList->ppList, i, s);
		AccountProxyKeyValueInfo *pInfo = pList->ppList[i];

		if (pInfo && pInfo->pKey)
		{
			if (!AccountProxyReplaceKeyValue(pInfo))
			{
				AccountProxyClearKeyValueList();
				return false;
			}
		}
	EARRAY_FOREACH_END;

	return true;
}

bool AccountProxyGetKeyValue(SA_PARAM_NN_STR const char *pKey, SA_PRE_NN_FREE SA_POST_NN_VALID AccountProxyKeyValueInfo **pItem)
{
	return stashFindPointer(getKeyValueListStashTable(), pKey, pItem);
}

bool AccountProxyRemoveKeyValue(SA_PARAM_NN_STR const char *pKey)
{
	return stashRemoveStruct(getKeyValueListStashTable(), pKey, parse_AccountProxyKeyValueInfo);
}

char *AccountProxyFindValueFromKey(SA_PARAM_OP_VALID CONST_EARRAY_OF(AccountProxyKeyValueInfo) keyValues, SA_PARAM_NN_STR const char *pKey)
{
	int i;

	for (i=0; i < eaSize(&keyValues); i++)
	{
		if (stricmp(keyValues[i]->pKey, pKey) == 0)
		{
			return keyValues[i]->pValue;
		}
	}

	return NULL;
}

char *AccountProxyFindValueFromKeyContainer(SA_PARAM_OP_VALID CONST_EARRAY_OF(AccountProxyKeyValueInfoContainer) keyValues, SA_PARAM_NN_STR const char *pKey)
{
    return AccountProxyFindValueFromKey((CONST_EARRAY_OF(AccountProxyKeyValueInfo))keyValues, pKey);
}

char *AccountProxyFindValueFromKeyInList(SA_PARAM_OP_VALID const AccountProxyKeyValueInfoList *pList, SA_PARAM_NN_STR const char *pKey)
{
    if ( pList )
    {
        return AccountProxyFindValueFromKey(pList->ppList, pKey);
    }
    return NULL;
}

#define RPN_GET_TRUTH(x) (( (x)[0] && !((x)[0] == '0' && (x)[1] == 0) ))

__forceinline static void rpnPushTruth(STRING_EARRAY *stack, bool x)
{
	eaPush(stack, x ? estrDup("1") : estrDup("0"));
}

__forceinline static void rpnPushU32(STRING_EARRAY *stack, U32 x)
{
	char *temp = NULL;
	estrPrintf(&temp, "%d", x);
	eaPush(stack, temp);
}

__forceinline static bool getTwoStringArgs(STRING_EARRAY *stack, char **pArg1, char **pArg2)
{
	*pArg2 = eaPop(stack);
	*pArg1 = eaPop(stack);
	if (!(*pArg2)) return false;
	if (!(*pArg1)) {
		estrDestroy(pArg2);
		return false;
	}
	return true;
}

__forceinline static bool getTwoU32Args(STRING_EARRAY *stack, U32 *uArg1, U32 *uArg2)
{
	char *pArg1, *pArg2;
	if (!getTwoStringArgs(stack, &pArg1, &pArg2)) return false;
	*uArg1 = atoi(pArg1);
	*uArg2 = atoi(pArg2);
	estrDestroy(&pArg1);
	estrDestroy(&pArg2);
	return true;
}

__forceinline static bool getOneStringArg(STRING_EARRAY *stack, char **pArg1)
{
	*pArg1 = eaPop(stack);
	if (!(*pArg1)) return false;
	return true;
}

__forceinline static bool getOneU32Arg(STRING_EARRAY *stack, U32 *uArg1)
{
	char *pArg1;
	if (!getOneStringArg(stack, &pArg1)) return false;
	*uArg1 = atoi(pArg1);
	estrDestroy(&pArg1);
	return true;
}

const char *AccountProxySubstituteKeyTokens(const char *pKey, const char *pProxy, const char *pCluster, const char *pEnvironment)
{
	static char *pSubbedKey = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!pSubbedKey)
	{
		estrHeapCreate(&pSubbedKey, 512, 0);
	}

	estrClear(&pSubbedKey);

	// If the key name starts with '$', it indicates a substitution token
	if (pKey[0] == '$')
	{
		const char *pSubToken = pKey + 1;
		const char *pSubTokenEnd = NULL;

		// The token should be terminated by ':' - if not, ignore it
		if (pSubTokenEnd = strchr(pSubToken, ':'))
		{
			// Move the key name pointer after the token
			pKey = pSubTokenEnd + 1;

			// Look for particular tokens that we know how to substitute, and do it
			if (!strnicmp(pSubToken, "PROXY", pSubTokenEnd-pSubToken))
			{
				if (pProxy && *pProxy)
				{
					estrAppend2(&pSubbedKey, pProxy);
					estrConcatChar(&pSubbedKey, ':');
				}
			}
			else if (!strnicmp(pSubToken, "CLUSTER", pSubTokenEnd-pSubToken))
			{
				if (pCluster && *pCluster)
				{
					estrAppend2(&pSubbedKey, pCluster);
					estrConcatChar(&pSubbedKey, ':');
				}
				else if (pProxy && *pProxy)
				{
					estrAppend2(&pSubbedKey, pProxy);
					estrConcatChar(&pSubbedKey, ':');
				}
			}
			else if (!strnicmp(pSubToken, "ENV", pSubTokenEnd-pSubToken))
			{
				if (pEnvironment && *pEnvironment)
				{
					estrAppend2(&pSubbedKey, pEnvironment);
					estrConcatChar(&pSubbedKey, ':');
				}
			}
		}
	}

	estrAppend2(&pSubbedKey, pKey);
	PERFINFO_AUTO_STOP();

	return pSubbedKey;
}

bool AccountProxyKeysMeetRequirements(const AccountProxyKeyValueInfoList *pList,
									  const char * const * pRequirementStack,
									  const char *pProxy,
									  const char *pCluster,
									  const char *pEnvironment,
									  int *pError,
									  char *** pppKeysUsed)
{
	STRING_EARRAY stack = NULL;
	bool ret = false;
	char *last = NULL;
	int i;
	char *pArg1, *pArg2;
	U32 uArg1, uArg2;

	if (!pRequirementStack || !eaSize(&pRequirementStack)) return true;
	if (pError) *pError = false;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&pRequirementStack); i++)
	{
		if (!strcmp(pRequirementStack[i], "|"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 | uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "||"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushTruth(&stack, uArg1 || uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "&"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 & uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "&&"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushTruth(&stack, uArg1 && uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "^"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 ^ uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "=="))
		{
			if (!getTwoStringArgs(&stack, &pArg1, &pArg2)) break;
			rpnPushTruth(&stack, !strcmpi(pArg1, pArg2));
			estrDestroy(&pArg1);
			estrDestroy(&pArg2);
		}
		else if (!strcmp(pRequirementStack[i], "<"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushTruth(&stack, uArg1 < uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "<="))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushTruth(&stack, uArg1 <= uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "<<"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 << uArg2);
		}
		else if (!strcmp(pRequirementStack[i], ">"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushTruth(&stack, uArg1 > uArg2);
		}
		else if (!strcmp(pRequirementStack[i], ">="))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushTruth(&stack, uArg1 >= uArg2);
		}
		else if (!strcmp(pRequirementStack[i], ">>"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 >> uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "+"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 + uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "-"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 - uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "*"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 * uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "/"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 / uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "%"))
		{
			if (!getTwoU32Args(&stack, &uArg1, &uArg2)) break;
			rpnPushU32(&stack, uArg1 % uArg2);
		}
		else if (!strcmp(pRequirementStack[i], "!"))
		{
			if (!getOneU32Arg(&stack, &uArg1)) break;
			rpnPushTruth(&stack, !uArg1);
		}
		else if (!strcmp(pRequirementStack[i], "!="))
		{
			if (!getTwoStringArgs(&stack, &pArg1, &pArg2)) break;
			rpnPushTruth(&stack, !!strcmpi(pArg1, pArg2));
			estrDestroy(&pArg1);
			estrDestroy(&pArg2);
		}
		else if (!strcmp(pRequirementStack[i], "~"))
		{
			if (!getOneU32Arg(&stack, &uArg1)) break;
			rpnPushU32(&stack, ~uArg1);
		}
		else if (!strcmp(pRequirementStack[i], "$"))
		{
			const char *pKey = NULL;
			if (!getOneStringArg(&stack, &pArg1)) break;
			pKey = AccountProxySubstituteKeyTokens(pArg1, pProxy, pCluster, pEnvironment);
			if (pList)
			{
				char *value = AccountProxyFindValueFromKeyInList(pList, pKey);
				eaPush(&stack, estrDup(value ? value : "0"));
			}
			else
			{
				eaPush(&stack, estrDup("0"));
			}
			if (pppKeysUsed) eaPush(pppKeysUsed, estrDup(pKey));
			estrDestroy(&pArg1);
		}
		else
		{
			eaPush(&stack, estrDup(pRequirementStack[i]));
		}
	}

	last = eaPop(&stack);
	if (!last) {
		eaDestroy(&stack);
		if (pError) *pError = true;
		PERFINFO_AUTO_STOP();
		return false;
	}

	ret = RPN_GET_TRUTH(last);

	estrDestroy(&last);
	last = eaPop(&stack);
	if (last) {
		eaDestroyEString(&stack);
		estrDestroy(&last);
		if (pError) *pError = true;
		PERFINFO_AUTO_STOP();
		return false;
	}

	eaDestroyEString(&stack);
	PERFINFO_AUTO_STOP();
	return ret;
}

// Get a unique ID for a new proxy request (unqiue only within one proxy)
ProxyRequestID getNewProxyRequestID(void)
{
	// Only needs to be unique within one proxy
	static ProxyRequestID id = 0;
	return id++;
}

char *GetAccountBannedKey(void)
{
	static char retVal[16] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.Banned", GetShortProductName());
	}

	return retVal;
}

char *GetAccountSuspendedKey(void)
{
	static char retVal[16] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.Suspended", GetShortProductName());
	}

	return retVal;
}

char *GetAccountGMKey(void)
{
	static char retVal[32] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.GM", GetShortProductName());
	}

	return retVal;
}

char *GetAccountTutorialDoneKey(void)
{
	static char retVal[32] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.TutorialDone", GetShortProductName());
	}

	return retVal;
}

char *GetAccountUgcEditBanKey(void)
{
		static char retVal[24] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.UgcEditBanned", GetShortProductName());
	}

	return retVal;
}

char *GetAccountUgcPublishBanKey(void)
{
	static char retVal[24] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.UgcPublishBanned", GetShortProductName());
	}

	return retVal;
}

char *GetAccountUgcReviewerKey(void)
{
	static char retVal[24] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.UgcReviewer", GetShortProductName());
	}

	return retVal;
}

char *GetAccountUgcCreateProjectEULAKey(void)
{
	static char retVal[24] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.UgcCreateProjectEULA", GetShortProductName());
	}

	return retVal;
}

char *GetAccountUgcProjectSearchEULAKey(void)
{
	static char retVal[24] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.UgcProjectSearchEULA", GetShortProductName());
	}

	return retVal;
}

char *GetAccountUgcProjectExtraSlotsKey(void)
{
	static char retVal[24] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.UgcExtraSlots", GetShortProductName());
	}

	return retVal;
}

char *GetAccountUgcProjectSeriesExtraSlotsKey(void)
{
	static char retVal[24] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.UgcExtraSeriesSlots", GetShortProductName());
	}

	return retVal;
}

char *GetAccountVShardAllowedKey(U32 iVShardContainerID)
{
	static char retVal[28] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.VShardAllowed_%u", GetShortProductName(), iVShardContainerID);
	}
	return retVal;

}
char *GetAccountVShardNotAllowedKey(U32 iVShardContainerID)
{
	static char retVal[32] = "";
	if (!retVal[0])
	{
		sprintf(retVal, "%s.VShardNotAllowed_%u", GetShortProductName(), iVShardContainerID);
	}
	return retVal;

}

#include "accountnet_h_ast.c"
