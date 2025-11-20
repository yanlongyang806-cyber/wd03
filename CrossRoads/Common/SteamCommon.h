#pragma once
GCC_SYSTEM

#define STEAM_CURRENT_APP -1
#define STEAM_PMID "SteamWallet"
#define STEAM_NOTIFY_LABEL "Steam"

AUTO_STRUCT;
typedef struct SteamMicroTxnAuthorizationResponse
{
	bool authorized;
	U64 order_id;
} SteamMicroTxnAuthorizationResponse;

C_DECLARATIONS_BEGIN

U32 ccSteamAppID(void);

C_DECLARATIONS_END