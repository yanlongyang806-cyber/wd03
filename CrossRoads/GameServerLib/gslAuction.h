/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef AuctionLot AuctionLot;

// Gets the containerIDs for all saved pets stored on container items in the specified auction lot
// Returns the number of containerIDs added
int gslAuction_GetContainerItemPetsFromLot(AuctionLot* pLot, U32** peaiPetIDs);
bool gslAuction_LotHasUniqueItem(AuctionLot* pLot);

void gslAuctions_SetAuctionsDisabled(bool bDisabled);
bool gslAuction_AuctionsDisabled(void);
bool gslAuction_ValidateBidOnAuction(Entity *pEnt, AuctionLot *pLot, U32 iBidValue);
bool gslAuction_ValidatePermissions(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot, SA_PARAM_NN_VALID U32 **peaiContainerItemPets, const char *pchAuctionsDisabledErrorMessage, const char *pchTrialErrorMessage);

void gslAuction_GetLotsOwnedByPlayer(Entity *pEnt);
void gslAuction_GetLotsBidByPlayer(Entity *pEnt, const char *pchItemName);

void gslAuction_CreateLotFromDescription(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot, U32 iAuctionDuration);
void gslAuction_BidOnAuction(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot, U32 iBidValue);
void gslAuction_PurchaseAuction(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot, bool bPurchasing);