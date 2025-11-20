#pragma once

#define WEBREQUEST_PARTITION_INDEX (1)

typedef struct Entity Entity;
typedef struct GameAccountData GameAccountData;
typedef struct WebDiscountRequest WebDiscountRequest;
typedef struct PurchaseRequestData PurchaseRequestData;

void wrCSub_AddDiscountRequest(WebDiscountRequest *pDiscount);
void wrCSub_AddPurchaseRequest(PurchaseRequestData *pPurchase);

void wrCSub_RemoveRequest(U32 uSubRequestID);
#define wrCSub_RemoveDiscountRequest(pDiscount) wrCSub_RemoveRequest(pDiscount->uSubRequestID)
#define wrCSub_RemovePurchaseRequest(pPurchase) wrCSub_RemoveRequest(pPurchase->uSubRequestID)
void wrCSub_MarkPurchaseCompleted(PurchaseRequestData *pPurchase);

// Dictionary Subscription Callbacks
void wrCSub_EntitySubscribed(Entity *pEnt);
void wrCSub_GADSubscribed(GameAccountData *pGAD);

void wrCSub_Tick(void);
bool wrCSub_VerifyVersion(SA_PARAM_NN_VALID Entity *pEnt);