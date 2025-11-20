/***************************************************************************
 *     Copyright (c) 2012-, Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 *
 ***************************************************************************/
#include "gslGatewaySession.h"
#include "gslGatewayAuction.h"

#include "entity.h"
#include "player.h"
#include "GameAccountDataCommon.h"

#include "AuctionLot.h"
#include "gslAuction.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "AuctionLot_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

#include "tradeCommon.h"
#include "tradeCommon_h_ast.h"
#include "NotifyCommon.h"
#include "AuctionLot_Transact.h"
#include "GameStringFormat.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"


typedef struct AuctionCallbackStructure
{
	AuctionLot *pLot; // Description of what the lot should be
	ContainerID accountID;
} AuctionCallbackStructure;

static void gslGateway_Auction_CancelRequest_CB(TransactionReturnVal *pReturn, void *pData)
{
	AuctionCallbackStructure *pStruct = (AuctionCallbackStructure*)pData;
	GatewaySession *psess = wgsFindSessionForAccountId(pStruct->accountID);
	Entity *pEnt = session_GetLoginEntity(psess);

	if(psess)
	{
		if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionSuccess, entTranslateMessageKey(pEnt, "Auction.AuctionCancelled"), NULL, NULL);
		}
		else
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.AuctionCancelledFail"), NULL, NULL);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayAuction_RequestCancel) ACMD_LIST(gGatewayCmdList);
void GatewayAuction_RequestCancel(Entity *pEnt, char *pchTrackerID, ContainerID auctionLotID)
{
	GatewaySession *psess = wgsFindSessionForAccountId(entGetAccountID(pEnt));
	ContainerTracker *ptracker = psess ? session_FindContainerTracker(psess,GW_GLOBALTYPE_AUCTION_SEARCH,pchTrackerID) : NULL;
	AuctionLot *pLot = NULL;

	if(ptracker && ptracker->pAuctionLotList)
	{
		int i;

		for(i=0;i<eaSize(&ptracker->pAuctionLotList->ppAuctionLots);i++)
		{
			if(ptracker->pAuctionLotList->ppAuctionLots[i]->iContainerID == auctionLotID)
			{
				pLot = ptracker->pAuctionLotList->ppAuctionLots[i];
				break;
			}
		}
	}

	if(pLot && pLot->ownerID == entGetContainerID(pEnt) && Auction_CanBeCanceled(pLot))
	{
		U32* eaiContainerItemPets = NULL;
		if (gslAuction_ValidatePermissions(pEnt, pLot, &eaiContainerItemPets, "Auction.AuctionCancelledFail", "Auction.AuctionCancelledFail"))
		{
			U32* eaPets = NULL;
			AuctionCallbackStructure *pStruct;
			ItemChangeReason reason = {0};
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

			inv_FillItemChangeReason(&reason, pEnt, "Auction:Cancel", pLot->description);

			pStruct = calloc(sizeof(AuctionCallbackStructure),1);

			pStruct->pLot = StructClone(parse_AuctionLot,pLot);
			pStruct->accountID = entGetAccountID(pEnt);

			ea32Create(&eaPets);
			if (gslAuction_LotHasUniqueItem(pLot))
			{
				Entity_GetPetIDList(pEnt, &eaPets);
			}

			AutoTrans_auction_tr_CancelAuction(objCreateManagedReturnVal(gslGateway_Auction_CancelRequest_CB, pStruct),
				GLOBALTYPE_GATEWAYSERVER, GLOBALTYPE_ENTITYPLAYER,entGetContainerID(pEnt),GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
				GLOBALTYPE_AUCTIONLOT,pLot->iContainerID,
				GLOBALTYPE_ENTITYSAVEDPET,&eaiContainerItemPets,
				&reason, pExtract);
		}
		
	}
	else
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.AuctionCancelledFail"), NULL, NULL);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(AuctionRequestPurchase) ACMD_LIST(gGatewayCmdList);
void exprAuctionRequestPurchase(Entity *pEnt, char *pchTrackerID, ContainerID auctionLotID)
{
	GatewaySession *psess = wgsFindSessionForAccountId(entGetAccountID(pEnt));
	ContainerTracker *ptracker = session_FindContainerTracker(psess,GW_GLOBALTYPE_AUCTION_SEARCH,pchTrackerID);
	AuctionLot *pLot = NULL;

	if(ptracker && ptracker->pAuctionLotList)
	{
		int i;

		for(i=0;i<eaSize(&ptracker->pAuctionLotList->ppAuctionLots);i++)
		{
			if(ptracker->pAuctionLotList->ppAuctionLots[i]->iContainerID == auctionLotID)
			{
				pLot = ptracker->pAuctionLotList->ppAuctionLots[i];
				break;
			}
		}
	}

	if(pLot)
	{
		gslAuction_PurchaseAuction(pEnt,pLot,true);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(AuctionRequestBid) ACMD_LIST(gGatewayCmdList);
void exprAuctionRequestBid(Entity *pEnt, char *pchTrackerID, ContainerID auctionLotID, U32 iBidValue)
{
	GatewaySession *psess = wgsFindSessionForAccountId(entGetAccountID(pEnt));
	ContainerTracker *ptracker = session_FindContainerTracker(psess,GW_GLOBALTYPE_AUCTION_SEARCH, pchTrackerID);
	AuctionLot *pLot = NULL;

	if(ptracker && ptracker->pAuctionLotList)
	{
		int i;

		for(i=0;i<eaSize(&ptracker->pAuctionLotList->ppAuctionLots);i++)
		{
			if(ptracker->pAuctionLotList->ppAuctionLots[i]->iContainerID == auctionLotID)
			{
				pLot = ptracker->pAuctionLotList->ppAuctionLots[i];
				break;
			}
		}
	}

	if(pLot)
	{
		gslAuction_BidOnAuction(pEnt,pLot,iBidValue);
	}
	
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(GatewayAuction_CreateAuction) ACMD_LIST(gGatewayCmdList);
void gslGatewayAuction_CreateAuction(Entity *pEnt, U64 iItemID, U32 iCount, U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration)
{
	Item *pItem = NULL;

	if(pEnt)
	{
		GatewaySession *psess = wgsFindSessionForAccountId(entGetAccountID(pEnt));
		pEnt = session_GetLoginEntityOfflineCopy(psess);
	}

	if(!pEnt)
	{
		return;
	}

	pItem = inv_GetItemByID(pEnt,iItemID);

	if (gAuctionConfig.bBiddingEnabled)
	{
		if (iBuyoutPrice == 0 && iStartingBid == 0)
		{
			notify_NotifySend(NULL, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.MustSetBuyoutPriceOrStartingBid"), NULL, NULL);
			return;
		}
		if (iStartingBid && iBuyoutPrice && iBuyoutPrice <= iStartingBid)
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.BuyoutPriceMustBeGreaterThanStartingBid"), NULL, NULL);
			return;
		}
	}
	else if (iBuyoutPrice <= 0)
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.MustSetPrice"), NULL, NULL);
		return;
	}

	if (pItem)
	{
		NOCONST(AuctionLot) *pAuctionLot = StructCreateNoConst(parse_AuctionLot);
		NOCONST(AuctionSlot) *pAuctionItem = StructCreateNoConst(parse_AuctionSlot);

		pAuctionItem->slot.pItem = StructCloneDeConst(parse_Item, pItem);
		pAuctionItem->slot.pItem->count = iCount;

		eaPush(&pAuctionLot->ppItemsV2,pAuctionItem);

		if (gAuctionConfig.bBiddingEnabled && iStartingBid)
		{
			pAuctionLot->pBiddingInfo = StructCreateNoConst(parse_AuctionBiddingInfo);
			pAuctionLot->pBiddingInfo->iMinimumBid = iStartingBid;
		}
		pAuctionLot->price = iBuyoutPrice;

		ea32Push(&pAuctionLot->auctionPetContainerIDs, 0);

		gslAuction_CreateLotFromDescription(pEnt, (AuctionLot *)pAuctionLot, iAuctionDuration);
	}
	else
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, TranslateMessageKey("Auction.MustSetItem"), NULL, NULL);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(GatewayAuction_GetPostingFee) ACMD_LIST(gGatewayCmdList);
void gslGateway_AuctionGetPostingFee(Entity *pEnt, U32 iBuyoutPrice, U32 iStartingBid, U32 iDuration)
{
	GatewaySession *psess = wgsFindSessionForAccountId(entGetAccountID(pEnt));
	U32 iPostFee = Auction_GetPostingFee(pEnt,iBuyoutPrice,iStartingBid,iDuration, true);
	U32 iSaleFee = Auction_GetSalesFee(pEnt,iBuyoutPrice,iStartingBid,iDuration, true);

	session_SendIntegerUpdate(psess,"PostingFee",iPostFee);
	session_SendIntegerUpdate(psess,"SaleFee",iSaleFee);
}

/* End of File */
