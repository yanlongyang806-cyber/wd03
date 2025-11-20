/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AuctionLot_Transact.h"
#include "AutoGen/AuctionLot_Transact_h_ast.h"

#include "AutoTransDefs.h"
#include "StringFormat.h"

#include "AuctionLot.h"
#include "AutoGen/AuctionLot_h_ast.h"
#include "GamePermissionsCommon.h"

#include "Entity.h"
#include "AutoGen/Entity_h_ast.h"
#include "EntitySavedData.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "Player.h"
#include "AutoGen/Player_h_ast.h"
#include "mailCommon.h"
#include "mailCommon_h_ast.h"

#include "EntityMailCommon.h"

#ifndef GAMECLIENT
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"
#endif

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pplayerauctiondata.Eaiauctionsbid");
enumTransactionOutcome auction_tr_RemoveExpiredAuctionsFromPlayerBidList(ATR_ARGS, NOCONST(Entity) *pEnt, PlayerAuctionBidListCleanupData *pCleanupData)
{
	if (pCleanupData && ea32Size(&pCleanupData->eaiAuctionLotContainerIDsToRemove) > 0 &&
		NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pPlayerAuctionData))
	{
		S32 i;
		for (i = 0; i < ea32Size(&pCleanupData->eaiAuctionLotContainerIDsToRemove); i++)
		{
			S32 iFoundIndex = -1;
			if (ea32SortedFindIntOrPlace(&pEnt->pPlayer->pPlayerAuctionData->eaiAuctionsBid, pCleanupData->eaiAuctionLotContainerIDsToRemove[i], &iFoundIndex))
			{
				ea32Remove(&pEnt->pPlayer->pPlayerAuctionData->eaiAuctionsBid, iFoundIndex);
			}
		}
	}
	TRANSACTION_RETURN_LOG_SUCCESS("auction_tr_RemoveExpiredAuctionsFromPlayerBidList succeeded.");
}

// Returns the auction expiration time
AUTO_TRANS_HELPER;
U32 auction_trh_GetExpirationTime(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (NONNULL(pAuctionLot->pBiddingInfo))
	{
		return pAuctionLot->uExpireTime + (gAuctionConfig.iBidExpirationExtensionInSeconds * pAuctionLot->pBiddingInfo->iNumBids);
	}
	else
	{
		return pAuctionLot->uExpireTime;
	}
}

// Indicates whether the bids are still accepted for the given auction
AUTO_TRANS_HELPER;
bool auction_trh_AcceptsBids(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (gAuctionConfig.bBiddingEnabled && NONNULL(pAuctionLot) && NONNULL(pAuctionLot->pBiddingInfo) && pAuctionLot->pBiddingInfo->iMinimumBid > 0)
	{
		// Make sure the auction has not expired
		U32 iTimeNow = timeSecondsSince2000();
		U32 iExpireTime = auction_trh_GetExpirationTime(ATR_PASS_ARGS, pAuctionLot);

		if (iTimeNow > iExpireTime)
		{
			return false;
		}

		return true;
	}
	return false;
}

// Returns the minimum numeric value for the next bid
AUTO_TRANS_HELPER;
U32 auction_trh_GetMinimumNextBidValue(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (gAuctionConfig.bBiddingEnabled && NONNULL(pAuctionLot) && NONNULL(pAuctionLot->pBiddingInfo) && pAuctionLot->pBiddingInfo->iMinimumBid)
	{
		if (pAuctionLot->pBiddingInfo->iCurrentBid)
		{
			U32 iIncrementalValue = MAX(1, round((F32)pAuctionLot->pBiddingInfo->iCurrentBid * gAuctionConfig.fBiddingIncrementalMultiplier));
			return pAuctionLot->pBiddingInfo->iCurrentBid + iIncrementalValue;
		}
		else
		{
			return pAuctionLot->pBiddingInfo->iMinimumBid;
		}
		return pAuctionLot->pBiddingInfo->iCurrentBid == 0 ? pAuctionLot->pBiddingInfo->iMinimumBid : pAuctionLot->pBiddingInfo->iCurrentBid + 1;
	}

	return 0;
}

// Indicates whether the auction can be owned by paying the buyout price
AUTO_TRANS_HELPER;
bool auction_trh_AcceptsBuyout(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (pAuctionLot->price == 0)
	{
		// No price is specified
		return false;
	}

	if (ISNULL(pAuctionLot->pBiddingInfo))
	{
		// If there is no bidding information, the auction is only available for buyout.
		return true;
	}

	// Auction can be bought out as long as the current bid is less than the buyout price
	return pAuctionLot->pBiddingInfo->iCurrentBid < pAuctionLot->price;
}

// Indicates whether the auction can be canceled
AUTO_TRANS_HELPER;
bool auction_trh_CanBeCanceled(ATR_ARGS, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (NONNULL(pAuctionLot->pBiddingInfo))
	{
		return !pAuctionLot->pBiddingInfo->iNumBids;
	}
	return true;
}

// Calculates the final sales fee. This function already takes the posting fee paid into account.
AUTO_TRANS_HELPER;
U32 auction_trh_GetFinalSalesFee(ATR_ARGS, ATH_ARG NOCONST(Entity) *pAuctionOwnerEnt, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	U32 iSalesFee;
	U32 iFinalSalesPrice;
	AuctionDurationOption *pDurationOption;

	if(!gAuctionConfig.bAuctionsUseSoldFee)
	{
		// no fee
		return 0;
	}

	// Get the duration option to see if a custom duration option is used for the auction
	pDurationOption = Auction_GetDurationOption(pAuctionLot->uExpireTime - pAuctionLot->creationTime);

	// Set the final sales price	
	if (NONNULL(pAuctionLot->pBiddingInfo) && pAuctionLot->pBiddingInfo->iCurrentBid)
	{
		iFinalSalesPrice = pAuctionLot->pBiddingInfo->iCurrentBid;
	}
	else
	{
		iFinalSalesPrice = pAuctionLot->price;
	}

	// get the sold fee, rounding down intentionally
	if (pDurationOption && pDurationOption->fAuctionSoldFee)
	{
		iSalesFee = iFinalSalesPrice * pDurationOption->fAuctionSoldFee;
	}
	else
	{
		iSalesFee = iFinalSalesPrice * gAuctionConfig.fAuctionDefaultSoldFee;
	}	

	//
	// This is where account / character price changes should be set
	//
	if(NONNULL(pAuctionOwnerEnt) && gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(pAuctionOwnerEnt, GAME_PERMISSION_AUCTION_SOLD_PERCENT, false, &bFound);
		if(bFound)
		{
			F32 fPercent = ((float)iVal) / 100.0f ;
			iSalesFee = min(iFinalSalesPrice * fPercent, iSalesFee);
		}
	}

	// reduce by posting fee
	if(pAuctionLot->uPostingFee < iSalesFee)
	{
		iSalesFee -= pAuctionLot->uPostingFee;
	}
	else
	{
		// posting fee >= uSoldFee
		iSalesFee = 0; 
	}

	// Sales fee cannot be more than the sales price
	iSalesFee = min(iSalesFee, iFinalSalesPrice);

	return iSalesFee;
}

#ifndef GAMECLIENT

// Called to close an auction (works for bids and buyouts)
AUTO_TRANS_HELPER;
bool auction_trh_CloseAuction(ATR_ARGS, 
	ATH_ARG NOCONST(Entity) *pOwner, 
	ATH_ARG NOCONST(Entity) *pBuyer,  
	ATH_ARG NOCONST(AuctionLot) *pLot,
	bool bBuyout,
	const ItemChangeReason *pSellerReason,
	const ItemChangeReason *pBuyerReason)
{
	char *estrAuctionWonBody = NULL;
	char *estrAuctionSoldBody = NULL;
	bool bWonEmailSent;
	bool bSoldEmailSent;
	U32 iSoldPrice;
	U32 iSoldPriceAfterFees;
	U32 iSoldFee;
	NOCONST(EmailV3Message)* pMessage = NULL;
	MailCharacterItems *pMailItems = NULL;

	if (pLot->state != ALS_Open)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, auction is no longer open");
		return false;
	}

	if (bBuyout)
	{
		// Make sure that the pBuyer is not the same player as the auction owner
		if (pBuyer->myContainerID == pLot->ownerID)
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, pBuyer[%d] is the auction owner.", pBuyer->myContainerID);
			return false;
		}

		iSoldPrice = pLot->price;

		// Set the state to clean up
		pLot->state = ALS_Cleanup;

		// Charge the buyer
		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pBuyer, false, Auction_GetCurrencyNumeric(), -((S32)pLot->price), pBuyerReason))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, cannot charge the buyer.");
			return false;
		}
	}
	else
	{
		if (ISNULL(pLot->pBiddingInfo))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, there is no bidding information.");
			return false;
		}

		// Make sure that the pBuyer is the same player as the last bidder
		if (pLot->pBiddingInfo->iBiddingPlayerContainerID != pBuyer->myContainerID)
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, pBuyer[%d] is different than the last bidder[%d].", 
				pBuyer->myContainerID,
				pLot->pBiddingInfo->iBiddingPlayerContainerID);
			return false;
		}

		iSoldPrice = pLot->pBiddingInfo->iCurrentBid;

		// Set the state to clean up
		pLot->state = ALS_Cleanup_BiddingClosed;
	}

	// Calculate the sold price after fees
	iSoldFee = auction_trh_GetFinalSalesFee(ATR_PASS_ARGS, pOwner, pLot);
	iSoldPriceAfterFees = iSoldPrice - iSoldFee;

	// Pay the auction owner
	if (iSoldPriceAfterFees)
	{
		if (!gAuctionConfig.bIncludeCurrencyInSoldEmail)
		{
			if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pOwner, false, Auction_GetCurrencyNumeric(), iSoldPriceAfterFees, pSellerReason))
			{
				TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, could not pay the owner[%d].", pOwner->myContainerID);
				return false;
			}
		}
		else
		{
			Item* pCurrency = CONTAINER_RECONST(Item, inv_ItemInstanceFromDefName(Auction_GetCurrencyNumeric(), 0, 0, NULL, NULL, NULL, false, NULL));
			pMailItems = CharacterMailAddItem(NULL, pCurrency, iSoldPriceAfterFees);
		}
	}
	langFormatMessageKey(pLot->iLangID, &estrAuctionSoldBody, "Auction_Sold_Body",
		STRFMT_STRING("Name", pBuyer->pSaved->savedName), 
		STRFMT_STRING("Account", pBuyer->pPlayer->publicAccountName),
		STRFMT_INT("Price", iSoldPrice),
		STRFMT_INT("PriceAfterFees", MAX(0, iSoldPriceAfterFees - pLot->uPostingFee)),
		STRFMT_INT("SoldFee", iSoldFee),
		STRFMT_END);
	
	bSoldEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pOwner,
		langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_From_Name", "[UNTRANSLATED]Auction House"),
		langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_Subject", "[UNTRANSLATED]Your auction is sold."), 
		estrAuctionSoldBody, 
		pMailItems,
		0,
		kNPCEmailType_Default);
	if (!bSoldEmailSent)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, \"Auction Sold\" email could not be sent to the auction owner.");
	}

	langFormatMessageKey(pBuyer->pPlayer->langID, &estrAuctionWonBody, "Auction_Won_Body",
		STRFMT_STRING("Name", pOwner->pSaved->savedName), 
		STRFMT_STRING("Account", pOwner->pPlayer->publicAccountName),
		STRFMT_INT("Price", iSoldPrice),
		STRFMT_END);

	// Send the "Auction Won" email
	pMailItems = CharacterMailAddItemsFromAuctionLot(NULL, pLot);
	bWonEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pBuyer,
		langTranslateMessageKeyDefault(pBuyer->pPlayer->langID, "Auction_Won_From", "[UNTRANSLATED]Auction House"),
		langTranslateMessageKeyDefault(pBuyer->pPlayer->langID, "Auction_Won_Subject", "[UNTRANSLATED]You have won an auction!"), 
		estrAuctionWonBody, 
		pMailItems,
		0,
		kNPCEmailType_Default);

	estrDestroy(&estrAuctionWonBody);

	if (!bWonEmailSent)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_trh_CloseAuction failed, \"Auction Won\" email could not be sent to the last bidder.");
	}

	return bWonEmailSent;
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Pplayer.Pemailv2.Mail, .Pplayer.Pemailv2.Ilastusedid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pLastBidder, ".Pplayer.Pemailv2.Mail, .Pplayer.Pemailv2.Ilastusedid, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, pInventoryV2.ppInventoryBags[], .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Langid, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pLot, ".State, .Price, .Ownerid, .Pbiddinginfo.Ibiddingplayercontainerid, .Ilangid, .Ppitemsv2, .Pbiddinginfo.Icurrentbid, .Upostingfee, .Uexpiretime, .Creationtime");
enumTransactionOutcome auction_tr_ExpireAuctionWithLastBidder(ATR_ARGS, NOCONST(Entity) *pOwner, NOCONST(Entity) *pLastBidder, NOCONST(AuctionLot) *pLot, const ItemChangeReason *pSellerReason, const ItemChangeReason *pBuyerReason)
{
	NOCONST(Item)* pItem = NULL;

	if(pLot->state == ALS_Mailed)
	{
		if(pLot->price == 0)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, mailed auction doesn't have price.");
		}
	}
	else if (pLot->state != ALS_Open) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, auction isn't active.");
	}

	if (ISNULL(pLastBidder))
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, called without a last bidder passed in.");
	}

	if (ISNULL(pOwner) || ISNULL(pOwner->pPlayer)) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, owner is NULL or not a player.");
	}

	if (pOwner->myContainerID != pLot->ownerID) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, wrong owner passed in.");
	}

	if (NONNULL(pLastBidder) && 
		NONNULL(pLot->pBiddingInfo) && 
		pLot->pBiddingInfo->iBiddingPlayerContainerID != pLastBidder->myContainerID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, wrong bidder passed in.");
	}

	if (NONNULL(pLastBidder) && pLot->state != ALS_Open)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionWithLastBidder failed, auction lot in wrong state.");
	}

	// There was at least one bid for this auction. The auction goes to the last bidder instead of returning to the owner
	if (auction_trh_CloseAuction(ATR_PASS_ARGS, pOwner, pLastBidder, pLot, false, pSellerReason, pBuyerReason))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Lot bidding is closed successfully.");
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("Lot bidding could not be closed properly.")
	}
}

AUTO_TRANSACTION
	ATR_LOCKS(pOwner, ".Pplayer.Pemailv2.Mail, .Pplayer.Pemailv2.Ilastusedid, .Pplayer.Accountid, .Pplayer.Langid")
	ATR_LOCKS(pLot, ".State, .Price, .Ownerid, .Pbiddinginfo.Ibiddingplayercontainerid, .Recipientid, .Ilangid, .Ppitemsv2");
enumTransactionOutcome auction_tr_ExpireAuctionNoBids(ATR_ARGS, NOCONST(Entity) *pOwner, NOCONST(AuctionLot) *pLot, AuctionExpiredInfo *pAuctionExpiredInfo)
{
	NOCONST(Item)* pItem = NULL;

	bool bExpiredEmailSent;
	MailCharacterItems* pMailItems;

	if(pLot->state == ALS_Mailed)
	{
		if(pLot->price == 0)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, mailed auction doesn't have price.");
		}
	}
	else if (pLot->state != ALS_Open) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, auction isn't active.");
	}

	if (ISNULL(pOwner) || ISNULL(pOwner->pPlayer)) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, owner is NULL or not a player.");
	}

	if (pOwner->myContainerID != pLot->ownerID) {
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, wrong owner passed in.");
	}

	if (NONNULL(pLot->pBiddingInfo) && 
		pLot->pBiddingInfo->iBiddingPlayerContainerID != 0)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_ExpireAuctionNoBids failed, the auction had a last bidder set. Use auction_tr_ExpireAuctionWithLastBidder() instead for this case.");
	}

	if(pLot->state == ALS_Mailed)
	{
		pLot->state = ALS_Cleanup_Mailed;
	}
	else
	{
		pLot->state = ALS_Cleanup;
	}
	pLot->recipientID = pOwner->pPlayer->accountID;
	pLot->iLangID = pOwner->pPlayer->langID;

	pMailItems = CharacterMailAddItemsFromAuctionLot(NULL, pLot);

	bExpiredEmailSent = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pOwner,
		pAuctionExpiredInfo->esAuctionExpiredFromName,
		pAuctionExpiredInfo->esAuctionExpiredSubject,
		pAuctionExpiredInfo->esAuctionExpiredBody, 
		pMailItems,
		0,
		kNPCEmailType_ExpiredAuction);

	if (!bExpiredEmailSent)
	{
		TRANSACTION_RETURN_LOG_FAILURE(
			"Failed:  Expired auction not lot added to character mail.");
	}

	TRANSACTION_RETURN_LOG_SUCCESS("Lot expired.");
}

AUTO_TRANSACTION
ATR_LOCKS(pLot, ".Ppitemsv1_Deprecated, .Uversion, .Ppitemsv2");
enumTransactionOutcome auction_tr_AuctionSlotItemFixup(ATR_ARGS, NOCONST(AuctionLot) *pLot)
{
	if(NONNULL(pLot->ppItemsV1_Deprecated))
	{
		NOCONST(Item)* pItem = NULL;
		int i = 0;
		for (i = 0; i < eaSize(&pLot->ppItemsV1_Deprecated); i++)
		{
			NOCONST(AuctionSlotV1)* pOldSlot = pLot->ppItemsV1_Deprecated[i];
			NOCONST(AuctionSlot)* pNewSlot = StructCreateNoConst(parse_AuctionSlot);
			pNewSlot->eUICategory = pOldSlot->eUICategory;
			pNewSlot->iItemSortType = pOldSlot->iItemSortType;
			pNewSlot->pchItemSortTypeCategoryName = pOldSlot->pchItemSortTypeCategoryName;
			pNewSlot->ppTranslatedNames = pOldSlot->ppTranslatedNames;
			pOldSlot->ppTranslatedNames = NULL;
			inv_trh_ent_MigrateSlotV1ToV2(ATR_PASS_ARGS, (NOCONST(InventorySlotV1)*)pOldSlot, (NOCONST(InventorySlot)*)pNewSlot);
			eaPush(&pLot->ppItemsV2, pNewSlot);
		}

		eaDestroyStructNoConst(&pLot->ppItemsV1_Deprecated, parse_AuctionSlotV1);
		pLot->ppItemsV1_Deprecated = NULL;
	}

	pLot->uVersion = AUCTION_LOT_VERSION;

	TRANSACTION_RETURN_LOG_SUCCESS("Lot Fixup Successful.");
}
#endif

#include "AutoGen/AuctionLot_Transact_h_ast.c"