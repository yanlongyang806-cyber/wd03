#pragma once
GCC_SYSTEM

#define ACCOUNT_TICKET_LIFESPAN 300 // 5 minutes in seconds
#define CONFLICT_TICKET_LIFESPAN ACCOUNT_TICKET_LIFESPAN

#define ACCOUNT_PERMISSION_SHARD_PREFIX "shard:"
#define ACCOUNT_PERMISSION_SHARD_PREFIX_LEN 6
#define ACCOUNT_PERMISSION_TOKEN_PREFIX "token:"
#define ACCOUNT_PERMISSION_TOKEN_PREFIX_LEN 6
#define ACCOUNT_PERMISSION_ALL_BASIC_ZONES "AllBasicZones"
#define ACCOUNT_PERMISSION_ALL_BASIC_LEVELS "AllBasicLevels"
#define ACCOUNT_PERMISSION_ALL_TRADE "AllTrade"
#define ACCOUNT_PERMISSION_ALL_SOCIAL "Allsocial"
#define ACCOUNT_PERMISSION_XMPP "XMPPAccess"
#define ACCOUNT_PERMISSION_LIFETIME_SUB "LifetimeSub"
#define ACCOUNT_PERMISSION_PRESS "Press"
#define ACCOUNT_PERMISSION_PREMIUM "Premium"
#define ACCOUNT_PERMISSION_STANDARD "Standard"
#define ACCOUNT_PERMISSION_IGNORE_QUEUE "IgnoreQueue"
#define ACCOUNT_PERMISSION_VIP_QUEUE "VIPQueue"
#define ACCOUNT_PERMISSION_UGC_ALLOWED "UGCAllowed"

#define MAX_ACCOUNTNAME 128

// the longest valid e-mail address is actually 254 UTF-8 characters 
// which can each by 4 bytes long
#define MAX_EMAIL 1024

#define MAX_PASSWORD 128
#define MAX_PASSWORD_PLAINTEXT 127
#define MAX_PASSWORD_ENCRYPTED 128 // Must be the size of the key, in bytes
#define MAX_PASSWORD_ENCRYPTED_BASE64 ((MAX_PASSWORD_ENCRYPTED + 2) / 3 * 4 + 1)
#define MAX_GUID 64
#define MAX_LOGIN_FIELD MAX(MAX_ACCOUNTNAME, MAX_EMAIL)
#define MAX_FIXED_SALT MAX_LOGIN_FIELD

typedef struct AccountPermissionStruct AccountPermissionStruct;
typedef struct AccountTicket AccountTicket;
typedef struct AccountTicketSigned AccountTicketSigned;

AUTO_STRUCT;
typedef struct AccountInfoStripped
{
	char accountName[MAX_ACCOUNTNAME];
	AccountPermissionStruct **ppPermissions;
	char **ppProductKeys;
	char *email;
	char *displayName;
	U32 uTimeReceived; NO_AST
} AccountInfoStripped;

#ifndef GAMECLIENT

void permissionsParseTokenString (SA_PARAM_NN_VALID char ***eaTokenList, AccountPermissionStruct *permissions);

bool permissionsCheckStartTime(AccountPermissionStruct *permissions, U32 time);
bool permissionsCheckEndTime(AccountPermissionStruct *permissions, U32 time);
bool permissionsGame(AccountPermissionStruct *permissions, const char *pPermission);

// Given a string in the form 'bar', '@bar', or 'foo@bar', return 'bar'.
const char *accountGetHandle(const char *pchName);

#endif

