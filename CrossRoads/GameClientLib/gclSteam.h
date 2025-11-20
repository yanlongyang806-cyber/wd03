#pragma once
GCC_SYSTEM
#if !PLATFORM_CONSOLE
C_DECLARATIONS_BEGIN

void gclSteamInit(void);
void gclSteamOncePerFrame(void);
void gclSteamSetAchievement(const char *pchName);
void gclSteamReset(void);
U64 gclSteamID(void);
bool gclSteamIsSubscribedApp(U32 uAppID);
void gclSteamOnMicroTxnAuthorizationResponse(bool bAuthorized, U64 uOrderID);

//Returns the current currency to use for steam wallet
const char *gclSteamGetAccountCurrency(void);
void gclSteamCmd_UpdateCurrency(const char *pchCurrency);
void gclSteam_PurchaseActive(bool bActive);
bool gclSteam_CanAttemptPurchase();
void gclSteam_PurchaseFailed();

C_DECLARATIONS_END
#endif