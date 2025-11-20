
#include "AuctionLot.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "ResourceManager.h"
#include "GameBranch.h"
#include "logging.h"
#include "StringCache.h"
#include "error.h"
#include "timing.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "GamePermissionsCommon.h"
#include "microtransactions_common.h"
#include "GameAccountDataCommon.h"
#include "AutoTransDefs.h"
#include "EntityMailCommon.h"
#include "StringFormat.h"
#include "EntitySavedData.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "Player.h"
#include "AutoGen/Player_h_ast.h"

#include "AutoGen/AuctionLot_h_ast.h"
#include "AutoGen/AuctionLotEnums_h_ast.h"
#include "AuctionBrokerCommon.h"

AuctionConfig gAuctionConfig;

AUTO_RUN_LATE;
int RegisterAuctionLotContainer(void)
{
	objRegisterNativeSchema(GLOBALTYPE_AUCTIONLOT, parse_AuctionLot, NULL, NULL, NULL, NULL, NULL);
	return 1;
}

void AuctionConfig_Load(void)
{
	char *pcBinFile = NULL;
	char *pcBuffer = NULL;
	char *estrFile = NULL;

	loadstart_printf("Loading AuctionConfig...");

	ParserLoadFiles(NULL, 
		"defs/config/AuctionConfig.def", 
		"AuctionConfig.bin", 
		PARSER_OPTIONALFLAG, 
		parse_AuctionConfig,
		&gAuctionConfig);

	// Do basic validation
	if (gAuctionConfig.bAllowCustomAuctionDurations)
	{
		if (eaSize(&gAuctionConfig.eaDurationOptions) < 2)
		{
			ErrorFilenamef(estrFile, "You have to specify at least 2 custom duration options when the AllowCustomAuctionDurations flag is set in AuctionConfig.def.");
		}
		else
		{
			// Make sure all duration options are less than the default duration and there is only one default
			S32 iDefaultCount = 0;
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(gAuctionConfig.eaDurationOptions, AuctionDurationOption, pDurationOption)
			{
				if (pDurationOption->bUIDefaultDuration)
				{
					++iDefaultCount;
				}

				if (pDurationOption->iDuration > gAuctionConfig.uDefaultExpireTimeDays * SECONDS_PER_DAY)
				{
					ErrorFilenamef(estrFile, "One of the auction duration options is greater than the default expire time value of %u.", gAuctionConfig.uDefaultExpireTimeDays);
				}
			}
			FOR_EACH_END

			if (iDefaultCount == 0)
			{
				ErrorFilenamef(estrFile, "You must define a default duration for the UI in the auction config.");
			}
			else if (iDefaultCount > 1)
			{
				ErrorFilenamef(estrFile, "You can only define one default duration for the UI in the auction config.");
			}
		}
	}

	estrDestroy(&estrFile);

	loadend_printf("Done.");

	estrDestroy(&pcBuffer);
	estrDestroy(&pcBinFile);
}

U32 Auction_GetMaximumDuration(Entity *pEntity)
{
	U32 uExpireTime = AUCTIONLOT_EXPIRE_DAYS;

	if(gAuctionConfig.uDefaultExpireTimeDays > 0)
	{
		uExpireTime = gAuctionConfig.uDefaultExpireTimeDays;
	}

	// This is where account / character expire times should be set
	if(pEntity && gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEntity), GAME_PERMISSION_AUCTION_EXPIRE, false, &bFound);
		if(bFound)
		{
			uExpireTime = max((U32)iVal, uExpireTime);
		}
	}

	uExpireTime *= SECONDS_PER_DAY;

	return uExpireTime;
}

U32 Auction_GetExpireTime(Entity *pEntity)
{
	U32 uExpireTime = Auction_GetMaximumDuration(pEntity);

	uExpireTime += timeServerSecondsSince2000();

	return uExpireTime;
}

U32 Auction_GetPostingFee(Entity *pEntity, U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration, bool bPlayerIsAtAuctionHouse)
{
	U32 uPostingFee = 0;
	U32 uMinPostFee;
	F32 fPostingFeeMultiplier = gAuctionConfig.fAuctionDefaultPostingFee;
	F32 iPriceUsedForFee = gAuctionConfig.bBiddingEnabled && iStartingBid > 0 ? iStartingBid : iBuyoutPrice;

	if(!gAuctionConfig.bAuctionsUsePostingFee || (iBuyoutPrice == 0 && iStartingBid == 0))
	{
		// no fee
		return 0;
	}

	if (iAuctionDuration > 0)
	{
		AuctionDurationOption *pDurationOption = Auction_GetDurationOption(iAuctionDuration);

		devassertmsgf(pDurationOption, "Invalid duration option is passed to Auction_GetPostingFee. Duration: %u seconds", iAuctionDuration);

		if (pDurationOption && pDurationOption->fAuctionPostingFee > 0.f)
		{
			fPostingFeeMultiplier = pDurationOption->fAuctionPostingFee;
		}
	}

	// get the posting fee, rounding down intentionally
	uPostingFee = round(iPriceUsedForFee * fPostingFeeMultiplier * (!bPlayerIsAtAuctionHouse ? gAuctionConfig.fPlayerRoamingFeeMultiplier : 1.0));

	// This is where account / character price changes should be set
	if(pEntity && gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEntity), GAME_PERMISSION_AUCTION_POST_PERCENT, false, &bFound);
		if(bFound)
		{
			// We don't support per duration pricing for game permissions
			F32 fPercent = ((float)iVal) / 100.0f ;
			uPostingFee = min((U32)round(iPriceUsedForFee * fPercent), uPostingFee);
		}
	}
	
	uMinPostFee = gAuctionConfig.fAuctionMinimumPostingFee;
	if(pEntity && gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEntity), GAME_PERMISSION_AUCTION_MIN_POST_FEE, false, &bFound);
		if(bFound)
		{
			uMinPostFee = min((U32)iVal, uMinPostFee);
		}
	}
	
	if(uMinPostFee > uPostingFee)
	{
		uPostingFee = uMinPostFee;
	}
	
	return uPostingFee;
}

U32 Auction_GetSalesFee(Entity *pEnt, U32 iBuyoutPrice, U32 iStartingBid, U32 iAuctionDuration, bool bPlayerIsAtAuctionHouse)
{
	U32 iSalesFee;
	U32 iFinalSalesPrice;
	AuctionDurationOption *pDurationOption = Auction_GetDurationOption(iAuctionDuration);

	if(pEnt == NULL || !gAuctionConfig.bAuctionsUseSoldFee || (iStartingBid == 0 && iBuyoutPrice == 0))
	{
		// no fee
		return 0;
	}

	// Set the final sales price	
	if (iBuyoutPrice)
	{
		iFinalSalesPrice = iBuyoutPrice;
	}
	else
	{
		iFinalSalesPrice = iStartingBid;
	}

	// Get the sales fee
	if (pDurationOption && pDurationOption->fAuctionSoldFee)
	{
		iSalesFee = iFinalSalesPrice * pDurationOption->fAuctionSoldFee * (!bPlayerIsAtAuctionHouse ? gAuctionConfig.fPlayerRoamingFeeMultiplier : 1.0);
	}
	else
	{
		iSalesFee = iFinalSalesPrice * gAuctionConfig.fAuctionDefaultSoldFee * (!bPlayerIsAtAuctionHouse ? gAuctionConfig.fPlayerRoamingFeeMultiplier : 1);
	}	

	//
	// This is where account / character price changes should be set
	//
	if(gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEnt), GAME_PERMISSION_AUCTION_SOLD_PERCENT, false, &bFound);
		if(bFound)
		{
			F32 fPercent = ((float)iVal) / 100.0f ;
			iSalesFee = MIN(iFinalSalesPrice * fPercent * (!bPlayerIsAtAuctionHouse ? gAuctionConfig.fPlayerRoamingFeeMultiplier : 1.0), iSalesFee);
		}
	}

	// Sales fee cannot be more than the sales price
	iSalesFee = MIN(iSalesFee, iFinalSalesPrice);

	return iSalesFee;
}

U32 Auction_GetSoldFee(Entity *pEntity, U32 uAskingPrice, U32 uPostingFee, bool bPlayerIsAtAuctionHouse)
{
	U32 uSoldFee = 0;

	if(!gAuctionConfig.bAuctionsUseSoldFee || !uAskingPrice)
	{
		// no fee
		return 0;
	}

	// get the sold fee, rounding down intentionally
	uSoldFee = uAskingPrice * gAuctionConfig.fAuctionDefaultSoldFee * (!bPlayerIsAtAuctionHouse ? gAuctionConfig.fPlayerRoamingFeeMultiplier : 1.0);

	//
	// This is where account / character price changes should be set
	//
	if(pEntity && gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEntity), GAME_PERMISSION_AUCTION_SOLD_PERCENT, false, &bFound);
		if(bFound)
		{
			F32 fPercent = ((float)iVal) / 100.0f ;
			uSoldFee = min(uAskingPrice * fPercent, uSoldFee);
		}
	}
	
	// reduce by posting fee
	if(uPostingFee < uSoldFee)
	{
		uSoldFee -= uPostingFee;
	}
	else
	{
		// posting fee >= uSoldFee
		uSoldFee = 0; 
	}

	// can cost more than the item worth
	uSoldFee = min(uSoldFee, uAskingPrice);

	return uSoldFee;
}

// get the maximum number of auctions that this character can place
U32 Auction_GetMaximumPostedAuctions(Entity *pEntity)
{
	U32 uMaxAuctions = 0;
	NOCONST(GameAccountData) *pData;

	if(!pEntity || !pEntity->pPlayer)
	{
		return 0;
	}
	
	if(gAuctionConfig.uDefaultAuctionPostsMaximum > 0)
	{
		uMaxAuctions = gAuctionConfig.uDefaultAuctionPostsMaximum;
	}
	else
	{
		uMaxAuctions = DEFAULT_MAXIMUM_AUCTIONS_POSTS;
	}
	
	// game permission limits
	if(pEntity && gamePermission_Enabled())
	{
		bool bFound;
		S32 iVal = GamePermissions_trh_GetCachedMaxNumericEx(CONTAINER_NOCONST(Entity, pEntity), GAME_PERMISSION_MAX_AUCTIONS, false, &bFound);
		if(bFound)
		{
			uMaxAuctions = max((U32)iVal, uMaxAuctions);
		}
	}
	
	//Add the number of numerics you have
	uMaxAuctions += inv_trh_GetNumericValue(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEntity), MicroTrans_GetExtraMaxAuctionsKeyID());

	pData = CONTAINER_NOCONST(GameAccountData, entity_GetGameAccount(pEntity));
	if(pData)
	{
		uMaxAuctions += MAX(0, gad_trh_GetAttribInt(pData, MicroTrans_GetExtraMaxAuctionsGADKey()));
	}
	
	return uMaxAuctions;
}

AuctionDurationOption * Auction_GetDurationOption(U32 iAuctionDuration)
{
	S32 iFoundIndex;
	if (iAuctionDuration > 0 && (iFoundIndex = eaIndexedFindUsingInt(&gAuctionConfig.eaDurationOptions, iAuctionDuration)) >= 0)
	{
		return gAuctionConfig.eaDurationOptions[iFoundIndex];
	}
	return NULL;
}

// This sets basic data for an auction lot, this is to set initial data and should only be called outside of a transaction
void AuctionLotInit(NOCONST(AuctionLot) *pAuctionLot)
{
	if(pAuctionLot)
	{
		pAuctionLot->uVersion = AUCTION_LOT_VERSION;
	}
}

// the maximum sell price 
U32 Auction_MaximumSellPrice(void)
{
	U32 uMaxPrice = 1<<31;

	if(gAuctionConfig.uMaximumAuctionPrice > 0 && gAuctionConfig.uMaximumAuctionPrice < uMaxPrice)
	{
		return gAuctionConfig.uMaximumAuctionPrice;
	}

	return uMaxPrice;
}

// Returns the numeric used for the auction house currency
const char * Auction_GetCurrencyNumeric(void)
{
	return gAuctionConfig.pchCurrencyNumeric && gAuctionConfig.pchCurrencyNumeric[0] ? gAuctionConfig.pchCurrencyNumeric : "Resources";
}

#include "AutoGen/AuctionLot_h_ast.c"
#include "AutoGen/AuctionLotEnums_h_ast.c"