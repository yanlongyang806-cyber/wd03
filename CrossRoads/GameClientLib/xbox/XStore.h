/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
#if _XBOX

typedef struct AccountProxyProduct AccountProxyProduct;

// Checks if the product is available in XBOX Marketplace
bool xStore_IsProductAvailableInMarketPlace(SA_PARAM_NN_VALID AccountProxyProduct *pProduct);

// Opens up the dialog for purchasing the product in XBOX Marketplace
bool xStore_OpenMarketPlacePurchaseDialog(U64 iOfferId);

void xStore_Init(void);

// Per tick processing for XStore
void xStore_Tick(void);

// Starts the process that notifies the server about the purchases
void xStore_BeginPurchaseNotification(void);

#endif