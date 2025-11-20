#pragma once
GCC_SYSTEM

#include "AppLocale.h"

typedef void(*ccSteamPurchaseProductCallback)(bool success, const char *msg, void *userdata);

void ccSteamPurchaseProduct(U32 account_id, Language lang, U64 steam_id, SA_PARAM_OP_STR const char *ip, SA_PARAM_NN_STR const char *category, U32 product_id, SA_PARAM_NN_STR const char *currency, ccSteamPurchaseProductCallback cb, void *userdata);
void ccSteamOnMicroTxnAuthorizationResponse(bool bAuthed, U32 uAccountID, U64 uOrderID, Language eLanguage, ccSteamPurchaseProductCallback pCallback, void *pUserData);