#include "aslAuctionServer.h"
#include "aslPersistedStores.h"
#include "AppServerLib.h"
#include "ServerLib.h"
#include "UtilitiesLib.h"
#include "objContainer.h"
#include "objIndex.h"
#include "AutoStartupSupport.h"
#include "logging.h"
#include "earray.h"
#include "AutoTransDefs.h"
#include "StringCache.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "Player.h"
#include "chatCommonStructs.h"
#include "EntityMailCommon.h"
#include "ResourceManager.h"
#include "StringFormat.h"
#include "AuctionLot_Transact.h"
#include "AutoGen/AuctionLot_Transact_h_ast.h"
#include "LoggedTransactions.h"
#include "WorldGrid.h"
#include "AuctionBrokerCommon.h"
#include "AuctionCommon.h"
#include "AuctionCommon_h_ast.h"
#include "Alerts.h"
#include "ReferenceSystem_Internal.h"
#include "rand.h"
#include "qsortG.h"

#include "AutoGen/controller_autogen_remotefuncs.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"
#include "AutoGen/appserverlib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AuctionLot_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/ChatServer_autogen_remotefuncs.h"

static ObjectIndex *gidx_AuctionLotOwner;
static ObjectIndex *gidx_AuctionItemLevel;
static ObjectIndex *gidx_AuctionItemQuality;
static ObjectIndex *gidx_AuctionItemSortType;
static ObjectIndex *gidx_AuctionItemSortTypeCategory;
static ObjectIndex *gidx_AuctionItemId;
static ObjectIndex *gidx_AuctionMailID;
static ObjectIndex *gidx_AuctionUsageRestriction;
static ObjectIndex *gidx_AuctionExpireTime;
static ObjectIndex *gidx_AuctionItemName;
static ObjectIndex *gidx_AuctionItemPetLevel;
static ObjectIndex *gidx_AuctionItemNumGemSlots;

//
// Collect the IDs of closed auction lots here
//
static INT_EARRAY sClosedLotIDs = NULL;
static U32 sTotalClosedLotsOnStartup = 0;
static U32 sCountClosedLotsRemoved = 0;
static U32 sCountClosedLotsRemoveFailed = 0;
static bool sClosingLots = false;
static bool sContainerLoadingDone = false;
static bool s_bPriceHistoryLoadingDone = false;

static INT_EARRAY sInvFixupIDs = NULL;

// The time this server starts up.  Any auctionLots in New state that are older than this get removed.
static U32 sStartupTime = 0;

// control throttling of old auction lot cleanup
U32 gLotsRemovedPerTick = 300;
AUTO_CMD_INT(gLotsRemovedPerTick, LotsRemovedPerTick) ACMD_COMMANDLINE;

U32 gLotsExpireCheckPerTick = CHECK_AUCTIONLOTS_PER_TICK;
AUTO_CMD_INT(gLotsExpireCheckPerTick, LotsExpireCheckPerTick) ACMD_COMMANDLINE;

U32 g_ValidateAllItemsBelongToASortType = false;
AUTO_CMD_INT(g_ValidateAllItemsBelongToASortType, ItemSortTypeValidation) ACMD_COMMANDLINE;

static bool s_bFixupCanStart = false;
static bool s_bFixupDone = false;

static bool s_bDisablePriceHistories = false;
AUTO_CMD_INT(s_bDisablePriceHistories, DisablePriceHistories) ACMD_AUTO_SETTING(Auction, AUCTIONSERVER) ACMD_COMMANDLINE;

static NOCONST(AuctionPriceHistories)* s_pLocalPriceHistories = NULL;

static StashTable stPriceHistoryFastLookup;

static int s_iNextPriceSaveTickChunk = 0;
static int s_iNumPriceChunks = 0;
static U32 s_uSecondsPerPriceHistoryUpdateTick = SECONDS_PER_PRICE_HISTORY_TICK;
AUTO_CMD_INT(s_uSecondsPerPriceHistoryUpdateTick, SecondsPerPriceHistoryUpdateTick) ACMD_AUTO_SETTING(Auction, AUCTIONSERVER) ACMD_COMMANDLINE;

static void
	InvFixupLots()
{
	s_bFixupCanStart = true;
	printf("Starting fixup of auction %d auction lots.\n", eaiSize(&sInvFixupIDs));
}

static bool AuctionFixupsDone(void)
{
	return s_bFixupDone;
}

static void AuctionFixup_CB(TransactionReturnVal *pReturn, ContainerID *piLotID)
{
	AuctionLot *pLot = AuctionServer_GetLot(*piLotID);

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && pLot)
	{
		objIndexInsert(gidx_AuctionItemLevel, pLot);
		objIndexInsert(gidx_AuctionItemPetLevel, pLot);
		objIndexInsert(gidx_AuctionItemNumGemSlots, pLot);
		objIndexInsert(gidx_AuctionItemQuality, pLot);
		objIndexInsert(gidx_AuctionItemSortType, pLot);
		objIndexInsert(gidx_AuctionItemSortTypeCategory, pLot);
		objIndexInsert(gidx_AuctionUsageRestriction, pLot);
		objIndexInsert(gidx_AuctionExpireTime, pLot);
		objIndexInsert(gidx_AuctionItemName, pLot);
	}
	SAFE_FREE(piLotID);
}

static void DoAuctionLotFixups(void)
{
	U32 uFixupTime = 0;

	if(!s_bFixupCanStart)
	{
		return;
	}

	if(s_bFixupDone)
	{
		return;
	}
	else if(timeSecondsSince2000() >= uFixupTime)
	{
		S32 iCount = 0;
		printf(".");

		while(iCount < 5000 && ea32Size(&sInvFixupIDs) > 0)
		{
			U32 lotID = ea32Pop(&sInvFixupIDs);
			AuctionLot *pLot = AuctionServer_GetLot(lotID);
			S32 *piLotID = malloc(sizeof(ContainerID));
			TransactionReturnVal *pReturn;

			*piLotID = lotID;
			pReturn = objCreateManagedReturnVal(AuctionFixup_CB, piLotID);

			if(pLot)
			{
				objIndexRemove(gidx_AuctionItemLevel, pLot);
				objIndexRemove(gidx_AuctionItemPetLevel, pLot);
				objIndexRemove(gidx_AuctionItemNumGemSlots, pLot);
				objIndexRemove(gidx_AuctionItemQuality, pLot);
				objIndexRemove(gidx_AuctionItemSortType, pLot);
				objIndexRemove(gidx_AuctionItemSortTypeCategory, pLot);
				objIndexRemove(gidx_AuctionUsageRestriction, pLot);
				objIndexRemove(gidx_AuctionExpireTime, pLot);
				objIndexRemove(gidx_AuctionItemName, pLot);
			}

			AutoTrans_auction_tr_AuctionSlotItemFixup(pReturn, GLOBALTYPE_AUCTIONSERVER, GLOBALTYPE_AUCTIONLOT, lotID);
			++iCount;
		}

		if(ea32Size(&sInvFixupIDs) == 0)
		{
			s_bFixupDone = true;
			RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
			printf("\nAll auction lots fixed up\n");
			return;
		}
		uFixupTime = timeSecondsSince2000() + 2;	// 5000 lots per ~2 seconds
	}

	return;

}

static void
RemoveClosedLotsDone(void)
{
	log_printf(LOG_CONTAINER, "Removed closed auction lots completed. Success: %d, Failure: %d, Total: %d", sCountClosedLotsRemoved, sCountClosedLotsRemoveFailed, sTotalClosedLotsOnStartup);

	sClosingLots = false;
}

static void
RemoveClosedLotsBegin(void)
{
	sTotalClosedLotsOnStartup = ea32Size(&sClosedLotIDs);
	if ( sTotalClosedLotsOnStartup > 0 )
	{
		sClosingLots = true;
	}
	else 
		RemoveClosedLotsDone();
}

static void
RemoveClosedLot_CB(TransactionReturnVal *pReturn, void *data)
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		sCountClosedLotsRemoved++;
	}
	else
	{
		sCountClosedLotsRemoveFailed++;
	}
	// check if we are done
	if ( ( sCountClosedLotsRemoved + sCountClosedLotsRemoveFailed ) >= sTotalClosedLotsOnStartup )
	{
		RemoveClosedLotsDone();
	}
}

static void
RemoveClosedLots()
{
	U32 n = ea32Size(&sClosedLotIDs);
	U32 i;
	int doneCount = 5;

	if ( n > gLotsRemovedPerTick )
	{
		n = gLotsRemovedPerTick;
	}

	// if we get called for enough ticks after removing the last lot, assume we are done
	if ( n == 0 )
	{
		doneCount--;
		if ( doneCount <= 0 )
		{
			RemoveClosedLotsDone();
		}
		return;
	}

	for ( i = 0; i < n; i++ )
	{
		U32 lotID = ea32Pop(&sClosedLotIDs);
		objRequestContainerDestroy(objCreateManagedReturnVal(RemoveClosedLot_CB, NULL), GLOBALTYPE_AUCTIONLOT, lotID, GLOBALTYPE_AUCTIONSERVER, 0);
	}

	log_printf(LOG_CONTAINER, "requested removal of %d closed auction lots\n", n);
}

static void
ContainerLoadingDone(void)
{
	printf("Container transfer complete\n");
	sContainerLoadingDone = true;
	InvFixupLots();
	RemoveClosedLotsBegin();
//	AuctionLotInvento
}

AuctionLot *AuctionServer_GetLot(ContainerID iLotID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_AUCTIONLOT, iLotID);
	if (pContainer) {
		return (AuctionLot *)pContainer->containerData;
	} else {
		return NULL;
	}
}

static U32 s_ExpiredLotBatchCount = 0;
static U32 s_ExpiredLotCompleteCount = 0;
static U32 s_ExpiredLotSkippedTickCount = 0;
static U32 s_TotalLotsExpired = 0;
static U32 s_TotalLotsExpiredSuccess = 0;
static U32 s_TotalSkippedTicks = 0;

static S32 *eaiDeadLot = NULL;

void auction_RemoveDeadLots(void)
{
	S32 i;
	for(i = 0; i < eaiSize(&eaiDeadLot); ++i)
	{
		AuctionLot *pLot = AuctionServer_GetLot(eaiDeadLot[i]);
		if(pLot)
		{
			objIndexRemove(gidx_AuctionExpireTime, pLot);
		}
	}

	eaiClear(&eaiDeadLot);
}

AUTO_COMMAND_REMOTE;
void Auction_RecordSalePriceForItem(const char* pchName, S32 iPricePerUnit)
{
	NOCONST(AuctionPriceHistory)* pHistory = NULL;


	pchName = allocFindString(pchName);

	if (!gAuctionConfig.bEnablePriceHistoryTracking || s_bDisablePriceHistories || !pchName)
		return;

	if (iPricePerUnit <= 0)
	{
		Errorf("Invalid price of '%d' passed to Auction_RecordSalePriceForItem for item %s.", iPricePerUnit, pchName);
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	stashFindPointer(stPriceHistoryFastLookup, pchName, &pHistory);

	devassertmsgf(pHistory, "Auction_RecordSalePriceForItem() couldn't find a stash table entry for item def \"%s\", which should have been created on startup...", pchName);

	//This will accumulate floating-point error over time, but it doesn't need to be super precise,
	//  and it's a lot faster than iterating over all the stored values to calculate a new average every time we receive a new price.
	pHistory->iCachedSum -= eaiGet(&pHistory->eaiLastSalePrices, pHistory->uNextWriteIndex);
	pHistory->iCachedSum += iPricePerUnit;
	eaiSet(&pHistory->eaiLastSalePrices, iPricePerUnit, pHistory->uNextWriteIndex);
	pHistory->uNextWriteIndex++;
	pHistory->uNextWriteIndex %= PERSISTED_PRICES_PER_ITEM;

	pHistory->pParentChunk->bDirty = true;
	PERFINFO_AUTO_STOP();
}

AUTO_TRANSACTION
	ATR_LOCKS(pPersistedHistories, ".eaPriceChunks");
enumTransactionOutcome Auction_tr_InitializePriceHistoryEarray(ATR_ARGS, NOCONST(AuctionPriceHistories)* pPersistedHistories)
{
	eaIndexedEnableNoConst(&pPersistedHistories->eaPriceChunks, parse_AuctionPriceHistoryChunk);
	return TRANSACTION_OUTCOME_SUCCESS;
}

//Runs every 10 seconds on a small subset of persisted prices.
AUTO_TRANSACTION
	ATR_LOCKS(pPersistedHistories, ".eaPriceChunks[]");
enumTransactionOutcome Auction_tr_PersistRecentSalePrices(ATR_ARGS, NOCONST(AuctionPriceHistories)* pPersistedHistories, NON_CONTAINER AuctionPriceHistoryChunk* pChunkUpdate, int iChunkID)
{
	NOCONST(AuctionPriceHistoryChunk)* pChunk = eaIndexedGetUsingInt(&pPersistedHistories->eaPriceChunks, iChunkID);
	
	PERFINFO_AUTO_START_FUNC();

	if (!pChunk)
	{
		pChunk = StructCreateNoConst(parse_AuctionPriceHistoryChunk);
		pChunk->iKey = iChunkID;
		eaIndexedPushUsingIntIfPossible(&pPersistedHistories->eaPriceChunks, iChunkID, pChunk);
	}

	eaCopyStructsDeConst(&pChunkUpdate->eaPrices, &pChunk->eaPrices, parse_AuctionPriceHistory);

	PERFINFO_AUTO_STOP();

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_COMMAND_REMOTE;
int aslAuction_CheckPriceHistoryForItem(const char* pchName)
{
	AuctionPriceHistory* pHistory = NULL;

	if (!gAuctionConfig.bEnablePriceHistoryTracking || s_bDisablePriceHistories)
		return 0;

	stashFindPointer(stPriceHistoryFastLookup, pchName, &pHistory);
	if (pHistory && eaiSize(&pHistory->eaiLastSalePrices) > 0)
		return pHistory->iCachedSum/eaiSize(&pHistory->eaiLastSalePrices);

	return 0;
}

static void ExpireAuction_CB(TransactionReturnVal *pReturn, ContainerID *piLotID)
{
	AuctionLot *pLot = AuctionServer_GetLot(*piLotID);
	s_ExpiredLotCompleteCount++;

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS && piLotID)
	{
		s_TotalLotsExpiredSuccess++;
		if(pLot)
		{
			if (pLot->pBiddingInfo && pLot->pBiddingInfo->iCurrentBid > 0 && pLot->pBiddingInfo->iBiddingPlayerContainerID > 0)
			{
				//This expiration was actually a sale
				Auction_RecordSalePriceForItem(pLot->ppItemsV2[0]->pchItemName, ceilf(((F32)pLot->pBiddingInfo->iCurrentBid)/pLot->ppItemsV2[0]->slot.pItem->count));
			}

//			RemoteCommand_ChatServerSendAuctionExpireMail(GLOBALTYPE_CHATSERVER, 0, pLot->recipientID, pLot->recipientID, GetShardNameFromShardInfoString(), pLot->iLangID, *piLotID);
			objRequestContainerDestroy(NULL, GLOBALTYPE_AUCTIONLOT, pLot->iContainerID, GLOBALTYPE_AUCTIONSERVER, 0);

			if(pLot->state == ALS_Cleanup_Mailed)
			{
				RemoteCommand_ChatServerDeleteMailWithLotID(GLOBALTYPE_CHATSERVER, 0,
					pLot->recipientID, pLot->iContainerID);
			}

		}
	}
	else if(pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE && pReturn && pLot)
	{
		bool bDestroyed = false;

		if (pLot->pBiddingInfo && pLot->pBiddingInfo->iNumBids)
		{
			pLot->pBiddingInfo->iNumFailedCloseOperations++;
		}

		if(pLot->state != ALS_Mailed)
		{
			// if the reason for the failure was there is no owner then destroy the lot
			// lots in mail (old expired lots) will not be deleted
			S32 i;
			for(i = 0; i < pReturn->iNumBaseTransactions; ++i)
			{
				if(pReturn->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE && pReturn->pBaseReturnVals[i].returnString &&
					strstri(OBJ_TRANS_CONTAINER_DOES_NOT_EXIST, pReturn->pBaseReturnVals[i].returnString))
				{
					objRequestContainerDestroy(NULL, GLOBALTYPE_AUCTIONLOT, pLot->iContainerID, GLOBALTYPE_AUCTIONSERVER, 0);
					bDestroyed = true;
					break;			
				}
			}
		}

		if(!bDestroyed && 
			(pLot->pBiddingInfo == NULL || pLot->pBiddingInfo->iNumBids == 0 || pLot->pBiddingInfo->iNumFailedCloseOperations >= 3))
		{
			if (pLot->pBiddingInfo && pLot->pBiddingInfo->iNumBids)
			{
				Errorf("Auction lot[%u] cannot be successfully closed. Please investigate this. Otherwise the last bidder will neither get the escrow money nor the item from the auction.", pLot->iContainerID);
			}
			// remove from auction expire list (use earray that is checked during next auction expire tick).
			eaiPush(&eaiDeadLot,*piLotID);
		}
	}
	SAFE_FREE(piLotID);
}

// This function is called on a regular tick to check for expired auctions
void ExpireAuctionLots(void)
{
	static U32 s_NextExpireTime = 0;
	ObjectIndexIterator iter;
	U32 iNumLotsChecked = 0;
	AuctionLot *pLot;
	ContainerID *piLotID;
	TransactionReturnVal *pReturn;
	U32 iCurTime = timeSecondsSince2000();
	ContainerStore *pStore;
	ObjectIndexKey createTimeKey = {0};

	// Don't do anything this tick if the last batch hasn't completed.  This is to prevent 
	//  making a bad situation worse when the transaction system is backed up.
	if ( s_ExpiredLotBatchCount > s_ExpiredLotCompleteCount )
	{
		s_ExpiredLotSkippedTickCount++;
		s_TotalSkippedTicks++;
		return;
	}

	auction_RemoveDeadLots();

	pStore = objFindContainerStoreFromType(GLOBALTYPE_AUCTIONLOT);
	objIndexInitKey_Int64(gidx_AuctionExpireTime, &createTimeKey, s_NextExpireTime);


	if ( s_ExpiredLotSkippedTickCount > 0 )
	{
		printf("Auction Expiration skipped %d ticks due to no-completion\n", s_ExpiredLotSkippedTickCount);
	}
	// Reset the counters
	s_ExpiredLotBatchCount = 0;
	s_ExpiredLotCompleteCount = 0;
	s_ExpiredLotSkippedTickCount = 0;

	objIndexObtainReadLock(gidx_AuctionExpireTime);

	objIndexGetIteratorFrom(gidx_AuctionExpireTime, &iter, ITERATE_FORWARD, &createTimeKey, 0);
	while (iNumLotsChecked < gLotsExpireCheckPerTick && (pLot = objIndexGetNextContainerData(&iter, pStore)))
	{
		if
			(
				(pLot->state == ALS_Open) ||
				(pLot->state == ALS_Mailed && pLot->price > 0)
			)
		{
			// If it's an open lot, check if it's expired, and if so, call the transaction that expires it
			U32 uExpireTime = Auction_GetExpirationTime(pLot);
			if(uExpireTime < 1)
			{
				uExpireTime = pLot->creationTime;
			}
			if(iCurTime > uExpireTime) 
			{
				const char *itemLogString = NULL;
				int itemCount = 0;
				ItemChangeReason buyerReason = {0};
				ItemChangeReason sellerReason = {0};

				// Set these statics so that the next attempt to expire auction lots will pick up where we left off.
				// This is to ensure that if there is transaction lag or some of the expiration transactions fail,
				//  that we continue making progress looking for expired lots rather than just getting stuck retrying
				//  a few bad ones over and over.

				piLotID = malloc(sizeof(ContainerID));
				*piLotID = pLot->iContainerID;
				pReturn = LoggedTransactions_CreateManagedReturnVal("AuctionLot", ExpireAuction_CB, piLotID);

				// format the item description for logging
				if ( pLot->ppItemsV2[0] != NULL )
				{
					Item *pItem = pItem = pLot->ppItemsV2[0]->slot.pItem;
					itemLogString = REF_STRING_FROM_HANDLE(pItem->hItem);
					itemCount = pLot->ppItemsV2[0]->slot.pItem->count;
				}

				if ( itemLogString == NULL )
				{
					itemLogString = "";
				}
				if(pLot->state == ALS_Mailed)
				{
					log_printf(LOG_AUCTION, "Expired lot moving to char, auction lot %d, owner=%d, itemCount=%d, item=%s", pLot->iContainerID, pLot->ownerID, itemCount, itemLogString);
				}
				else
				{
					log_printf(LOG_AUCTION, "Expiring auction lot %d, owner=%d, itemCount=%d, item=%s", pLot->iContainerID, pLot->ownerID, itemCount, itemLogString);
				}
				
				inv_FillItemChangeReason(&sellerReason, NULL, "Auction-Sell", itemLogString);
				inv_FillItemChangeReason(&buyerReason, NULL, "Auction-Buy", itemLogString);

				if (pLot->pBiddingInfo && pLot->pBiddingInfo->iBiddingPlayerContainerID > 0)
					AutoTrans_auction_tr_ExpireAuctionWithLastBidder(pReturn, GLOBALTYPE_AUCTIONSERVER, GLOBALTYPE_ENTITYPLAYER, pLot->ownerID, GLOBALTYPE_ENTITYPLAYER, pLot->pBiddingInfo->iBiddingPlayerContainerID, GLOBALTYPE_AUCTIONLOT, pLot->iContainerID, &buyerReason, &sellerReason);
				else
				{
					AuctionExpiredInfo auctionInfo = {0};

					estrPrintf(&auctionInfo.esAuctionExpiredFromName, "%s", langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Sold_From_Name", "Auction House"));
					estrPrintf(&auctionInfo.esAuctionExpiredSubject, "%s", langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Expired_Subject", "Auction lot expired"));
					estrPrintf(&auctionInfo.esAuctionExpiredBody, "%s", langTranslateMessageKeyDefault(pLot->iLangID, "Auction_Expired_Body", "This lot has expired"));

					AutoTrans_auction_tr_ExpireAuctionNoBids(pReturn, GLOBALTYPE_AUCTIONSERVER, GLOBALTYPE_ENTITYPLAYER, pLot->ownerID, GLOBALTYPE_AUCTIONLOT, pLot->iContainerID, &auctionInfo);

					StructDeInit(parse_AuctionExpiredInfo, &auctionInfo);
				}

				// Increment the counter for each transaction we initiate
				s_ExpiredLotBatchCount++;
				s_TotalLotsExpired++;
			}
			else
			{
				// If we've reached a lot that hasn't expired, we can stop searching.
				// The iteration is done in order, from the oldest lots to the newest.

				// Reset the static variables so that the next scan for expired lots starts at the beginning.
				s_NextExpireTime = 0;
				break;
			}
		}

		// Moved next time to while loop and numlots checked so it actually only checks N per tick
		s_NextExpireTime = pLot->uExpireTime;
		++iNumLotsChecked;
	}

	objIndexReleaseReadLock(gidx_AuctionExpireTime);
	objIndexDeinitKey_Int64(gidx_AuctionExpireTime, &createTimeKey);
}

void AddAuctionLotCB(Container *con, AuctionLot *pLot)
{	
	// Queue certain lots for deletion when they are acquired on startup.  This only applies to lots that are older 
	//  than the current server instance.
	// Closed lots are those that are left over from a previous bug that caused auction lots to never be deleted, 
	//  or from crashes that occur between the transaction that closes them and the container deletion.
	// New lots are most likely left over from a crash that occurred between the container creation and the transaction 
	//  that would have initialized them.
	if(pLot->uVersion != AUCTION_LOT_VERSION)
	{
		ea32Push(&sInvFixupIDs, pLot->iContainerID);
	}
	
	if ( ( ( pLot->creationTime != 0 ) && ( pLot->creationTime < sStartupTime ) ) && ( ( pLot->state == ALS_Closed ) || ( pLot->state == ALS_New ) ) )
	{
		// collect closed lots, so that they can be deleted
		ea32Push(&sClosedLotIDs, pLot->iContainerID);
	}
	else
	{
		objIndexInsert(gidx_AuctionLotOwner, pLot);
		objIndexInsert(gidx_AuctionMailID, pLot);
		
		if (pLot->state == ALS_Open || (pLot->state == ALS_Mailed && pLot->price > 0))
		{
			objIndexInsert(gidx_AuctionExpireTime, pLot);
		}

		if (pLot->state == ALS_Open)
		{
			objIndexInsert(gidx_AuctionItemLevel, pLot);
			objIndexInsert(gidx_AuctionItemPetLevel, pLot);
			objIndexInsert(gidx_AuctionItemNumGemSlots, pLot);
			objIndexInsert(gidx_AuctionItemQuality, pLot);
			objIndexInsert(gidx_AuctionItemSortType, pLot);
			objIndexInsert(gidx_AuctionItemSortTypeCategory, pLot);
			objIndexInsert(gidx_AuctionUsageRestriction, pLot);
            objIndexInsert(gidx_AuctionItemId, pLot);
			objIndexInsert(gidx_AuctionItemName, pLot);
		}
	}
}

void RemoveAuctionLotCB(Container *con, AuctionLot *pLot)
{
	objIndexRemove(gidx_AuctionItemId, pLot);
	objIndexRemove(gidx_AuctionLotOwner, pLot);
	objIndexRemove(gidx_AuctionMailID, pLot);

	objIndexRemove(gidx_AuctionItemLevel, pLot);
	objIndexRemove(gidx_AuctionItemPetLevel, pLot);
	objIndexRemove(gidx_AuctionItemNumGemSlots, pLot);
	objIndexRemove(gidx_AuctionItemQuality, pLot);
	objIndexRemove(gidx_AuctionItemSortType, pLot);
	objIndexRemove(gidx_AuctionItemSortTypeCategory, pLot);
	objIndexRemove(gidx_AuctionUsageRestriction, pLot);
	objIndexRemove(gidx_AuctionExpireTime, pLot);
	objIndexRemove(gidx_AuctionItemName, pLot);
}

void CommitAuctionLotStatePreCB(Container *con, ObjectPathOperation **operations)
{
	AuctionLot *pLot = (AuctionLot *)con->containerData;
	if (pLot->state == ALS_Open)
	{
		objIndexRemove(gidx_AuctionItemLevel, pLot);
		objIndexRemove(gidx_AuctionItemPetLevel, pLot);
		objIndexRemove(gidx_AuctionItemNumGemSlots, pLot);
		objIndexRemove(gidx_AuctionItemQuality, pLot);	
		objIndexRemove(gidx_AuctionItemSortType, pLot);
		objIndexRemove(gidx_AuctionItemSortTypeCategory, pLot);
		objIndexRemove(gidx_AuctionUsageRestriction, pLot);	
		objIndexRemove(gidx_AuctionExpireTime, pLot);
        objIndexRemove(gidx_AuctionItemId, pLot);
		objIndexRemove(gidx_AuctionItemName, pLot);
	}
}

void CommitAuctionLotStatePostCB(Container *con, ObjectPathOperation **operations)
{
	AuctionLot *pLot = (AuctionLot *)con->containerData;
	if (pLot->state == ALS_Open)
	{
		objIndexInsert(gidx_AuctionItemLevel, pLot);
		objIndexInsert(gidx_AuctionItemPetLevel, pLot);
		objIndexInsert(gidx_AuctionItemNumGemSlots, pLot);
		objIndexInsert(gidx_AuctionItemQuality, pLot);
		objIndexInsert(gidx_AuctionItemSortType, pLot);
		objIndexInsert(gidx_AuctionItemSortTypeCategory, pLot);
		objIndexInsert(gidx_AuctionUsageRestriction, pLot);
		objIndexInsert(gidx_AuctionExpireTime, pLot);
        objIndexInsert(gidx_AuctionItemId, pLot);
		objIndexInsert(gidx_AuctionItemName, pLot);
	}
}

void AuctionServer_InitIndices(void)
{
	//For updating indices.
	objRegisterContainerTypeAddCallback(GLOBALTYPE_AUCTIONLOT, AddAuctionLotCB);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_AUCTIONLOT, RemoveAuctionLotCB);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_AUCTIONLOT, CommitAuctionLotStatePreCB, ".state", true, false, true, NULL);
	objRegisterContainerTypeCommitCallback(GLOBALTYPE_AUCTIONLOT, CommitAuctionLotStatePostCB, ".state", true, false, false, NULL);

	gidx_AuctionLotOwner = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ownerID", ".iContainerID", NULL);
	gidx_AuctionItemLevel = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].uiCachedMinLevel", ".iContainerID", NULL);
	gidx_AuctionItemPetLevel = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].pItem.pSpecialProps.pSuperCritterPet.uLevel", ".iContainerID", NULL);
	gidx_AuctionItemNumGemSlots = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].uiNumGemSlots", ".iContainerID", NULL);
	gidx_AuctionItemQuality = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].pItem.pAlgoProps.Quality", ".iContainerID", NULL);
	gidx_AuctionItemSortType = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].iItemSortType", ".iContainerID", NULL);
	gidx_AuctionItemSortTypeCategory = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].pchItemSortTypeCategoryName", ".iContainerID", NULL);
	gidx_AuctionItemId = objIndexCreateWithStringPath(4, 0, parse_AuctionLot, ".iContainerID");
	gidx_AuctionMailID = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".recipientID", ".iContainerID", NULL);
	gidx_AuctionUsageRestriction = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].eUICategory", ".iContainerID", NULL);
	gidx_AuctionExpireTime = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".uExpireTime", ".iContainerID", NULL);
	gidx_AuctionItemName = objIndexCreateWithStringPaths(4, 0, parse_AuctionLot, ".ppItemsV2[0].pchItemName", ".iContainerID", NULL);
}

//Excludes items of type Title, Token, Numeric, Lore and ItemValue.
void aslAuctionServerValidateItemSortTypeMembership()
{
	RefDictIterator iter;
	ItemDef* pDef = NULL;
	STRING_EARRAY eaItemNames = NULL;
	int iCount = 0;
	int i;
	char fileName[MAX_PATH];
	FILE *pOutFile;
	sprintf(fileName, "%s/AuctionHouseUnsearchableItems.txt", fileTempDir());
	mkdirtree(fileName);

	pOutFile = fopen(fileName, "wt");
	
	RefSystem_InitRefDictIterator(g_hItemDict, &iter);

	while(pDef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (!pDef->iSortID && 
			pDef->eType != kItemType_Title &&
			pDef->eType != kItemType_Token &&
			pDef->eType != kItemType_Numeric &&
			pDef->eType != kItemType_Lore &&
			pDef->eType != kItemType_ItemValue &&
			!itemdef_IsUnidentified(pDef) &&
			!(pDef->flags & (kItemDefFlag_BindOnPickup | kItemDefFlag_BindToAccountOnPickup)))
		{
			eaPush(&eaItemNames, (char*)pDef->pchName);
			iCount++;
		}
	}

	eaQSort(eaItemNames, strCmp);

	for (i = 0; i < iCount; i++)
	{
		fprintf(pOutFile, "%s\n", eaItemNames[i]);
	}

	eaDestroy(&eaItemNames);

	fclose(pOutFile);

	if (iCount > 0)
		Errorf("%i non-BoP items found that do not belong to an ItemSortType. It is currently impossible to search for these items on the auction house. See tmp/UnsearchableItems.txt for the list.", iCount);
}

AUTO_STARTUP(AuctionServer) ASTRT_DEPS(	Items, 
										CurrencyExchangeSchema, 
										CurrencyExchangeConfig,
										AutoStart_AuctionBroker,
										AuctionServerLoad,
										AS_TextFilter);
void aslAuctionServerStartup(void)
{
	if (g_ValidateAllItemsBelongToASortType)
	{
		aslAuctionServerValidateItemSortTypeMembership();
	}
}

int AuctionServerLibInit(void)
{
	AutoStartup_SetTaskIsOn("AuctionServer", 1);
	AutoStartup_RemoveAllDependenciesOn("WorldLib");

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	resFinishLoading();

	objLoadAllGenericSchemas();
	
	stringCacheFinalizeShared();
	
	assertmsg(GetAppGlobalType() == GLOBALTYPE_AUCTIONSERVER, "Auction server type not set");
	
	loadstart_printf("Connecting AuctionServer to TransactionServer (%s)... ", gServerLibState.transactionServerHost);
	
	while (!InitObjectTransactionManager(GetAppGlobalType(),
			gServerLibState.containerID,
			gServerLibState.transactionServerHost,
			gServerLibState.transactionServerPort,
			gServerLibState.bUseMultiplexerForTransactions, NULL)) {
		Sleep(1000);
	}
	if (!objLocalManager()) {
		loadend_printf("Failed.");
		return 0;
	}
	
	loadend_printf("Connected.");

	gAppServer->oncePerFrame = AuctionServerLibOncePerFrame;
	
	AuctionServer_InitIndices();


	return 1;
}

static void aslAuction_InitLocalPriceHistory(AuctionPriceHistories* pSrc)
{
	int iChunk, j, k;
	ReferenceDictionary *pDictionary = RefDictionaryFromNameOrHandle(g_hItemDict);
	StashTable stAllItemDefs = stashTableCreateWithStringKeys(pDictionary->iNumberOfReferents, StashCaseInsensitive);
	RefDictIterator iter;
	ItemDef* pDef;
	StashTableIterator stIter;
	StashElement elem;
	RefSystem_InitRefDictIterator(g_hItemDict, &iter);
	while(pDef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		stashAddPointer(stAllItemDefs, pDef->pchName, pDef, true);
	}
	//copy into our mutable runtime price histories
	eaCopyStructsDeConst(&pSrc->eaPriceChunks, &s_pLocalPriceHistories->eaPriceChunks, parse_AuctionPriceHistoryChunk);
	
	if (!s_pLocalPriceHistories->eaPriceChunks)
		eaIndexedEnableNoConst(&s_pLocalPriceHistories->eaPriceChunks, parse_AuctionPriceHistoryChunk);
	
	for (iChunk = 0; iChunk < eaSize(&s_pLocalPriceHistories->eaPriceChunks); iChunk++)
	{
		for (j = 0; j < eaSize(&s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices); j++)
		{
			int iNumPrices = eaiSize(&s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices[j]->eaiLastSalePrices);
			
			s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices[j]->pParentChunk = CONTAINER_RECONST(AuctionPriceHistoryChunk, s_pLocalPriceHistories->eaPriceChunks[iChunk]);
			
			stashAddPointer(stPriceHistoryFastLookup, s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices[j]->pchItemDefName, s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices[j], false);
			stashRemovePointer(stAllItemDefs, s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices[j]->pchItemDefName, NULL);
			
			if (iNumPrices > 0)
			{
				for (k = 0; k < iNumPrices; k++)
				{
					s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices[j]->iCachedSum += s_pLocalPriceHistories->eaPriceChunks[iChunk]->eaPrices[j]->eaiLastSalePrices[k];
				}
			}
		}
	}
	
	//stAllItemDefs now contains all itemdefs that weren't already in our database of prices
	
	if (stashGetCount(stAllItemDefs) > 0)
	{
		//indexed earray keys start at 1
		int iLastChunkID = eaSize(&s_pLocalPriceHistories->eaPriceChunks);

		stashGetIterator(stAllItemDefs, &stIter);
		while (stashGetNextElement(&stIter, &elem))
		{
			int idx = eaIndexedFindUsingInt(&s_pLocalPriceHistories->eaPriceChunks, iLastChunkID);
			NOCONST(AuctionPriceHistory)* pHistory = StructCreateNoConst(parse_AuctionPriceHistory);
			if (idx == -1 || eaSize(&s_pLocalPriceHistories->eaPriceChunks[idx]->eaPrices) >= PRICE_HISTORIES_PER_CHUNK)
			{
				NOCONST(AuctionPriceHistoryChunk)* pChunk = StructCreateNoConst(parse_AuctionPriceHistoryChunk);
				pChunk->iKey = ++iLastChunkID;
				eaIndexedPushUsingIntIfPossible(&s_pLocalPriceHistories->eaPriceChunks, iLastChunkID, pChunk);
				idx = eaIndexedFindUsingInt(&s_pLocalPriceHistories->eaPriceChunks, iLastChunkID);
			}
			s_pLocalPriceHistories->eaPriceChunks[idx]->bDirty = true;
			pHistory->pchItemDefName = allocFindString(stashElementGetStringKey(elem));
			pHistory->pParentChunk = CONTAINER_RECONST(AuctionPriceHistoryChunk, s_pLocalPriceHistories->eaPriceChunks[idx]);
			eaPush(&s_pLocalPriceHistories->eaPriceChunks[idx]->eaPrices, pHistory);
			stashAddPointer(stPriceHistoryFastLookup, pHistory->pchItemDefName, pHistory, false);
		}
	}
	stashTableDestroy(stAllItemDefs);
	s_bPriceHistoryLoadingDone = true;
}

static void AuctionServerLibCreatePriceHistoryContainer_CB(TransactionReturnVal *pReturn, void *pUnused)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		Container* pContainer = objGetContainer(GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER, PRICE_HISTORY_CONTAINER_ID);
		if (pContainer && pContainer->containerData)
		{
			AutoTrans_Auction_tr_InitializePriceHistoryEarray(NULL, GetAppGlobalType(), GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER, PRICE_HISTORY_CONTAINER_ID);
			aslAuction_InitLocalPriceHistory(pContainer->containerData);
			return;
		}
	}
	TriggerAlert("AuctionServer_EVENT_CONTAINER_ERROR", "At launch time, could not find or create a price history container. Auction prices will not be tracked.", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
}

static void CreatePriceHistoryContainer()
{
	// Manual transaction to create the container if it doesn't exist, and to make
	//  sure that it gets the correct ID.

	TransactionRequest *request = objCreateTransactionRequest();

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"VerifyContainer containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER),
		PRICE_HISTORY_CONTAINER_ID);

	// Move the container to the auction server
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
		"MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER), GlobalTypeToName(GLOBALTYPE_AUCTIONSERVER), 0);

	objAddToTransactionRequestf(request, GLOBALTYPE_AUCTIONSERVER, 0, "containerVar containerIDVar ContainerVarBinary", 
		"ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
		GlobalTypeToName(GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER));

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
		objCreateManagedReturnVal(AuctionServerLibCreatePriceHistoryContainer_CB, NULL), "CreatePriceHistoryContainer", request);
	objDestroyTransactionRequest(request);
}

static void	AuctionServerLibEnsurePriceHistoryContainerExists(void)
{
	Container* pContainer = objGetContainer(GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER, PRICE_HISTORY_CONTAINER_ID);
	if (!pContainer || !pContainer->containerData)
	{
		CreatePriceHistoryContainer();
	}
	else
	{
		aslAuction_InitLocalPriceHistory(pContainer->containerData);
	}
}

int AuctionServerLibOncePerFrame(F32 fElapsed)
{
	static bool bOnce = false;
	static U32 iLastDeleteTime = 0;
	static U32 iLastExpireTime = 0;
	static U32 iLastPersistPriceHistoryTime = 0;
	if(!bOnce) {
		sStartupTime = timeSecondsSince2000();
		iLastPersistPriceHistoryTime = timeSecondsSince2000();
		aslPersistedStores_Load();

		aslAcquireContainerOwnership(GLOBALTYPE_AUCTIONLOT, ContainerLoadingDone);
		
		if (gAuctionConfig.bEnablePriceHistoryTracking)
		{
			ReferenceDictionary *pDictionary;

			pDictionary = RefDictionaryFromNameOrHandle(g_hItemDict);
			s_pLocalPriceHistories = StructCreateNoConst(parse_AuctionPriceHistories);
			
			s_iNumPriceChunks = ceil(pDictionary->iNumberOfReferents/PRICE_HISTORIES_PER_CHUNK);
			
			//Initial size: One entry per itemdef. Slightly excessive due to inclusion of numerics/tokens/etc, but those are a small percentage of all items.
			stPriceHistoryFastLookup = stashTableCreateWithStringKeys(pDictionary->iNumberOfReferents, StashCaseInsensitive);
			aslAcquireContainerOwnership(GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER, AuctionServerLibEnsurePriceHistoryContainerExists);
		}
		else
			s_bPriceHistoryLoadingDone = true;

//		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		ATR_DoLateInitialization();
		bOnce = true;

		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "LoadingContainers");

	}


	if(sContainerLoadingDone && s_bPriceHistoryLoadingDone)
	{
		if(AuctionFixupsDone())
		{
			// Expire any auction lots that have gone over time
			// Don't start expiring until all the containers are loaded
			if(timeSecondsSince2000() - iLastExpireTime >= SECONDS_PER_AUCTION_EXPIRE_TICK)
			{
				iLastExpireTime = timeSecondsSince2000();

				if ( gLotsExpireCheckPerTick != 0 )
				{
					ExpireAuctionLots();
				}
			}
			//At set intervals, persist a single chunk of prices.
			if(gAuctionConfig.bEnablePriceHistoryTracking && !s_bDisablePriceHistories && (timeSecondsSince2000() - iLastPersistPriceHistoryTime >= s_uSecondsPerPriceHistoryUpdateTick))
			{
				int idx = eaIndexedFindUsingInt(&s_pLocalPriceHistories->eaPriceChunks, s_iNextPriceSaveTickChunk+1);
				iLastPersistPriceHistoryTime = timeSecondsSince2000();

				if (s_pLocalPriceHistories->eaPriceChunks[idx]->bDirty)
				{
					AutoTrans_Auction_tr_PersistRecentSalePrices(NULL, GetAppGlobalType(), GLOBALTYPE_AUCTIONPRICEHISTORYCONTAINER, PRICE_HISTORY_CONTAINER_ID, CONTAINER_RECONST(AuctionPriceHistoryChunk, s_pLocalPriceHistories->eaPriceChunks[idx]), s_iNextPriceSaveTickChunk+1);
				
					s_pLocalPriceHistories->eaPriceChunks[idx]->bDirty = false;
				}

				if ((++s_iNextPriceSaveTickChunk) >= s_iNumPriceChunks)
					s_iNextPriceSaveTickChunk = 0;
			}
	
			// if there are any closed auction lots that need to be cleaned up, then call the cleanup function
			if (sClosingLots && timeSecondsSince2000() - iLastDeleteTime >= SECONDS_PER_AUCTION_REMOVE_TICK)
			{
				iLastDeleteTime = timeSecondsSince2000();
				RemoveClosedLots();
			}

			// Run ticks for persisted stores
			aslPersistedStores_Tick();

		}
		else
		{
			DoAuctionLotFixups();
		}
	}

	return 1;
}

//Don't even think about using this on a live shard. I am dead serious.
AUTO_COMMAND ACMD_CATEGORY(Auction, Debug) ACMD_ACCESSLEVEL(9);
void AuctionPriceHistoryLoadTesting(int iNumPricesPerItemDef, int minPrice, int maxPrice)
{
	RefDictIterator iter;
	ItemDef* pDef;
	RefSystem_InitRefDictIterator(g_hItemDict, &iter);
	while(pDef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		int i;
		for (i = 0; i < iNumPricesPerItemDef; i++)
			Auction_RecordSalePriceForItem(pDef->pchName, randomIntRange(minPrice, maxPrice));
	}
}

AUTO_COMMAND ACMD_CATEGORY(Auction, Debug);
void PrintIndexState(void)
{
	char *estr = 0;
	estrStackCreate(&estr);
	printObjectIndexStructure(gidx_AuctionLotOwner, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionItemLevel, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionItemPetLevel, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionItemNumGemSlots, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionItemQuality, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionItemSortType, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionItemSortTypeCategory, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionUsageRestriction, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionExpireTime, &estr);
	printf("%s\n", estr);

	estrClear(&estr);
	printObjectIndexStructure(gidx_AuctionItemName, &estr);
	printf("%s\n", estr);

	estrDestroy(&estr);

}

extern int gMaxElementsToSearch;

static bool AuctionServer_GetIter(ObjectIndex *pIndex, ObjectIndexIterator *pIter, ObjectIndexKey *pKey)
{
	if (pKey->val.type) {
		return objIndexGetIteratorFrom(pIndex, pIter, ITERATE_FORWARD, pKey, 0);
	} else {
		return objIndexGetIterator(pIndex, pIter, ITERATE_FORWARD);
	}
}

static AuctionLot *AuctionServer_GetNextMatch(ObjectIndexIterator *pIter, ObjectIndexKey *pKey, ContainerStore *pStore)
{
	if (pKey->val.type) {
		return objIndexGetNextMatchContainerData(pIter, pKey, OIM_LTE, pStore);
	} else {
		return objIndexGetNextContainerData(pIter, pStore);
	}
}

static S64 AuctionServer_SearchLotsByIndexRange(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, ObjectIndex *pIndex,
												ObjectIndexKey *pStartKey, ObjectIndexKey *pEndKey, AuctionFilter_CB pFilterCallback)
{
	ObjectIndexIterator iter;
	AuctionLot *pLot = NULL;
	ContainerStore *pStore = objFindContainerStoreFromType(GLOBALTYPE_AUCTIONLOT);

	PERFINFO_AUTO_START_FUNC();
	objIndexObtainReadLock(pIndex);
	if (AuctionServer_GetIter(pIndex, &iter, pStartKey)) {
		while (pLot = AuctionServer_GetNextMatch(&iter, pEndKey, pStore)) {
			pLotList->hitcount++;
			if (!pFilterCallback || pFilterCallback(pLot, pRequest)) {
				eaPush(&pLotList->ppAuctionLots, pLot);
			}
			if (pLotList->hitcount > gMaxElementsToSearch) {
				pLotList->result = ASR_SearchCap;
				break;
			}
		}
	}
	objIndexReleaseReadLock(pIndex);
	PERFINFO_AUTO_STOP();

	pLotList->realHitcount = eaSize(&pLotList->ppAuctionLots);
	return pLotList->realHitcount;
}

static S64 AuctionServer_SearchLotsByIndex(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, ObjectIndex *pIndex,
										   ObjectIndexKey *pKey, AuctionFilter_CB pFilterCallback)
{
	return AuctionServer_SearchLotsByIndexRange(pLotList, pRequest, pIndex, pKey, pKey, pFilterCallback);
}

S64 AuctionServer_SearchAllLots(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey *key = calloc(1, sizeof(ObjectIndexKey));
	S64 iNumLots;
	key->val.type = MULTI_NONE;
	iNumLots = AuctionServer_SearchLotsByIndex(pLotList, pRequest, gidx_AuctionItemId, key, filter);
	objIndexDestroyKey_Int(gidx_AuctionItemId, &key);
	return iNumLots;
}

S64 AuctionServer_SearchLotsByIDs(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	S32 i;

	for (i = 0; i < ea32Size(&pRequest->eaiAuctionLotContainerIDs); i++)
	{
		Container *pContainer = objGetContainer(GLOBALTYPE_AUCTIONLOT, pRequest->eaiAuctionLotContainerIDs[i]);

		if (pContainer)
		{
			AuctionLot *pAuctionLot = (AuctionLot *)pContainer->containerData;
			pLotList->hitcount++;

			if (!filter || filter(pAuctionLot, pRequest)) 
			{
				eaPush(&pLotList->ppAuctionLots, pAuctionLot);
			}

			if (pLotList->hitcount > gMaxElementsToSearch) 
			{
				pLotList->result = ASR_SearchCap;
				break;
			}
		}
		else
		{
			// Auction Lot is no longer valid
			ea32Push(&pLotList->eaiNonExistingAuctionLotContainerIDs, pRequest->eaiAuctionLotContainerIDs[i]);
		}
	}

	pLotList->realHitcount = eaSize(&pLotList->ppAuctionLots);
	return pLotList->realHitcount;
}

S64 AuctionServer_SearchLotsForOwner(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key = {0};
	S64 iNumLots = 0;

	objIndexInitKey_Int(gidx_AuctionLotOwner, &key, pRequest->ownerID);
	iNumLots = AuctionServer_SearchLotsByIndex(pLotList, pRequest, gidx_AuctionLotOwner, &key, filter);
	objIndexDeinitKey_Int(gidx_AuctionLotOwner, &key);

	return iNumLots;
}

S64 AuctionServer_SearchItemsLevelRange(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key1 = {0};
	ObjectIndexKey key2 = {0};
	S64 iNumLots = 0;

	objIndexInitKey_Int(gidx_AuctionItemLevel, &key1, pRequest->minLevel);
	objIndexInitKey_Int(gidx_AuctionItemLevel, &key2, pRequest->maxLevel ? pRequest->maxLevel+1 : 10000);
	iNumLots = AuctionServer_SearchLotsByIndexRange(pLotList, pRequest, gidx_AuctionItemLevel, &key1, &key2, filter);
	objIndexDeinitKey_Int(gidx_AuctionItemLevel, &key1);
	objIndexDeinitKey_Int(gidx_AuctionItemLevel, &key2);

	return iNumLots;
}

S64 AuctionServer_SearchItemsPetLevelRange(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key1 = {0};
	ObjectIndexKey key2 = {0};
	S64 iNumLots = 0;

	objIndexInitKey_Int(gidx_AuctionItemPetLevel, &key1, pRequest->uiPetMinLevel);
	objIndexInitKey_Int(gidx_AuctionItemPetLevel, &key2, pRequest->uiPetMaxLevel ? pRequest->uiPetMaxLevel+1 : 10000);
	iNumLots = AuctionServer_SearchLotsByIndexRange(pLotList, pRequest, gidx_AuctionItemPetLevel, &key1, &key2, filter);
	objIndexDeinitKey_Int(gidx_AuctionItemPetLevel, &key1);
	objIndexDeinitKey_Int(gidx_AuctionItemPetLevel, &key2);

	return iNumLots;
}

S64 AuctionServer_SearchItemsNumGemSlotsRange(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key1 = {0};
	ObjectIndexKey key2 = {0};
	S64 iNumLots = 0;

	objIndexInitKey_Int(gidx_AuctionItemNumGemSlots, &key1, pRequest->uiNumGemSlots);
	objIndexInitKey_Int(gidx_AuctionItemNumGemSlots, &key2, 10000);
	iNumLots = AuctionServer_SearchLotsByIndexRange(pLotList, pRequest, gidx_AuctionItemNumGemSlots, &key1, &key2, filter);
	objIndexDeinitKey_Int(gidx_AuctionItemNumGemSlots, &key1);
	objIndexDeinitKey_Int(gidx_AuctionItemNumGemSlots, &key2);

	return iNumLots;
}

S64 AuctionServer_SearchItemsQuality(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key = {0};
	S64 iNumLots = 0;

	objIndexInitKey_Int(gidx_AuctionItemQuality, &key, pRequest->itemQuality);
	iNumLots = AuctionServer_SearchLotsByIndex(pLotList, pRequest, gidx_AuctionItemQuality, &key, filter);
	objIndexDeinitKey_Int(gidx_AuctionItemQuality, &key);

	return iNumLots;
}

S64 AuctionServer_SearchItemsSortType(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key = {0};
	S64 iNumLots = 0;

	objIndexInitKey_Int(gidx_AuctionItemSortType, &key, pRequest->sortType);
	iNumLots = AuctionServer_SearchLotsByIndex(pLotList, pRequest, gidx_AuctionItemSortType, &key, filter);
	objIndexDeinitKey_Int(gidx_AuctionItemSortType, &key);

	return iNumLots;
}

S64 AuctionServer_SearchItemsSortTypeCategory(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key = {0};
	S64 iNumLots = 0;

	objIndexInitKey_String(gidx_AuctionItemSortTypeCategory, &key, pRequest->pchItemSortTypeCategoryName);
	iNumLots = AuctionServer_SearchLotsByIndex(pLotList, pRequest, gidx_AuctionItemSortTypeCategory, &key, filter);
	objIndexDeinitKey_String(gidx_AuctionItemSortTypeCategory, &key);

	return iNumLots;
}

static S64 AuctionServer_FindCheapestForAuctionBroker(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, ObjectIndex *pIndex,
	ObjectIndexKey *pKey, AuctionFilter_CB pFilterCallback)
{
	ObjectIndexIterator iter;
	AuctionLot *pLot = NULL;
	AuctionLot *pCheapestLot = NULL;
	ContainerStore *pStore = objFindContainerStoreFromType(GLOBALTYPE_AUCTIONLOT);
	S64 iReturnVal = 0;
	U32 iNumMatchesFound = 0;

	PERFINFO_AUTO_START_FUNC();
	objIndexObtainReadLock(pIndex);
	if (AuctionServer_GetIter(pIndex, &iter, pKey)) 
	{
		while (pLot = AuctionServer_GetNextMatch(&iter, pKey, pStore)) 
		{
			pLotList->hitcount++;

			if (pLotList->hitcount > gMaxElementsToSearch) 
			{
				pLotList->result = ASR_SearchCap;
				break;
			}

			if (!Auction_AcceptsBuyout(pLot) || (pFilterCallback && !pFilterCallback(pLot, pRequest))) 
			{
				continue;				
			}

			++iNumMatchesFound;

			if (pCheapestLot == NULL || pLot->price < pCheapestLot->price)
			{
				pCheapestLot = pLot;
			}
		}
	}
	objIndexReleaseReadLock(pIndex);
	PERFINFO_AUTO_STOP();

	if (pCheapestLot)
	{
		eaPush(&pLotList->ppAuctionLots, pCheapestLot);
		iReturnVal = 1;
		ea32Push(&pLotList->piAuctionLotsCounts, iNumMatchesFound);
	}

	pLotList->realHitcount += iReturnVal;
	return iReturnVal;
}

S64 AuctionServer_SearchLotsByAuctionBrokerDef(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key = {0};
	S64 iNumLots = 0;

	devassert(pRequest->pAuctionBrokerSearchData);

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pRequest->pAuctionBrokerSearchData->ppItemDropInfo, AuctionBrokerItemDropInfo, pItemDropInfo)
	{
		ItemDef *pItemDef = GET_REF(pItemDropInfo->itemDefRef.hDef);

		if (pItemDef)
		{
			objIndexInitKey_String(gidx_AuctionItemName, &key, pItemDef->pchName);
			iNumLots += AuctionServer_FindCheapestForAuctionBroker(pLotList, pRequest, gidx_AuctionItemName, &key, filter);
			objIndexDeinitKey_String(gidx_AuctionItemName, &key);
		}
	}
	FOR_EACH_END

	return iNumLots;
}

S64 AuctionServer_SearchItemsUsageRestriction(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB filter)
{
	ObjectIndexKey key1 = {0};
	ObjectIndexKey key2 = {0};
	S64 iNumLots = 0;

	objIndexInitKey_Int(gidx_AuctionUsageRestriction, &key1, pRequest->minUsageRestrictionCategory);
	objIndexInitKey_Int(gidx_AuctionUsageRestriction, &key2, pRequest->maxUsageRestrictionCategory + 1);
	iNumLots = AuctionServer_SearchLotsByIndexRange(pLotList, pRequest, gidx_AuctionUsageRestriction, &key1, &key2, filter);
	objIndexDeinitKey_Int(gidx_AuctionUsageRestriction, &key1);
	objIndexDeinitKey_Int(gidx_AuctionUsageRestriction, &key2);

	return iNumLots;
}

S64 AuctionServer_SearchLots(AuctionLotList *pLotList, AuctionSearchRequest *pRequest, AuctionFilter_CB pFilterCallback, char **estrSearchType)
{
	if (pRequest->pAuctionBrokerSearchData)
	{
		estrConcatf(estrSearchType, "%s", "auctionsBroker");
		return AuctionServer_SearchLotsByAuctionBrokerDef(pLotList, pRequest, pFilterCallback);
	}
	else if (ea32Size(&pRequest->eaiAuctionLotContainerIDs) > 0)
	{
		estrConcatf(estrSearchType, "%s", "auctionsBid");
		return AuctionServer_SearchLotsByIDs(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->ownerID && !pRequest->bExcludeOwner) 
	{
		estrConcatf(estrSearchType, "%s", "owner");
		return AuctionServer_SearchLotsForOwner(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->maxLevel > 0 || pRequest->minLevel > 0)
	{
		estrConcatf(estrSearchType, "%s", "level");
		return AuctionServer_SearchItemsLevelRange(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->sortType > 0)
	{
		estrConcatf(estrSearchType, "%s", "sortType");
		return AuctionServer_SearchItemsSortType(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->uiNumGemSlots > 0)
	{
		estrConcatf(estrSearchType, "%s", "numGemSlots");
		return AuctionServer_SearchItemsNumGemSlotsRange(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->pchItemSortTypeCategoryName && pRequest->pchItemSortTypeCategoryName[0]) 
	{
		estrConcatf(estrSearchType, "%s", "sortTypeCategory");
		return AuctionServer_SearchItemsSortTypeCategory(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->maxUsageRestrictionCategory != UsageRestrictionCategory_None)
	{
		estrConcatf(estrSearchType, "%s", "usage");
		return AuctionServer_SearchItemsUsageRestriction(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->itemQuality > -1)
	{
		estrConcatf(estrSearchType, "%s", "quality");
		return AuctionServer_SearchItemsQuality(pLotList, pRequest, pFilterCallback);
	}
	else if (pRequest->uiPetMaxLevel > 0 || pRequest->uiPetMinLevel > 0)
	{
		estrConcatf(estrSearchType, "%s", "petLevel");
		return AuctionServer_SearchItemsPetLevelRange(pLotList, pRequest, pFilterCallback);
	}
	else 
	{
		estrConcatf(estrSearchType, "%s", "all");
		return AuctionServer_SearchAllLots(pLotList, pRequest, pFilterCallback);
	}
}

#define MAXIMUM_MAIL_LOTS 1000

S64 AuctionServer_SearchLotsMailID(const ContainerID iAccountID, U32 **ppLotIDs)
{
	ObjectIndexKey key = {0};
	ObjectIndexIterator iter;
	S64 count = 0;
	AuctionLot *lot = NULL;
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_AUCTIONLOT);
	
	if(iAccountID == 0 || ppLotIDs == NULL)
	{
		return 0;
	}
	
	PERFINFO_AUTO_START_FUNC();

	objIndexInitKey_Int(gidx_AuctionMailID, &key, iAccountID);
	objIndexObtainReadLock(gidx_AuctionMailID);
	objIndexGetIteratorFrom(gidx_AuctionMailID, &iter, ITERATE_FORWARD, &key, 0);

	while (lot = objIndexGetNextMatchContainerData(&iter, &key, OIM_EQ, store))
	{
		if(lot->state == ALS_Mailed)
		{
			eaiPush(ppLotIDs, lot->iContainerID);
			++count;
			if(count >= MAXIMUM_MAIL_LOTS)
			{
				break;
			}
		}
	}

	objIndexReleaseReadLock(gidx_AuctionMailID);
	objIndexDeinitKey_Int(gidx_AuctionMailID, &key);

	PERFINFO_AUTO_STOP();
	return count;
}

//
// Debugging command to print the number of lots in each state
//
AUTO_COMMAND ACMD_CATEGORY(Auction, Debug);
void PrintStateCounts(void)
{
	ContainerIterator iter;
	AuctionLot *pLot;
	S32 *counts = NULL;
	int i;

	objInitContainerIteratorFromType(GLOBALTYPE_AUCTIONLOT, &iter);
	while ( pLot = objGetNextObjectFromIterator(&iter) )
	{
		while ( ea32Size(&counts) <= pLot->state )
		{
			ea32Push(&counts, 0);
		}
		if ( counts != NULL )
		{
			counts[pLot->state]++;
		}
	}
	objClearContainerIterator(&iter);

	for ( i = 0; i < ea32Size(&counts); i++ )
	{
		printf("state %d: %d\n", i, counts[i]);
	}

	ea32Destroy(&counts);
}

//
// Debugging command to print the number of lots in each state
//
AUTO_COMMAND ACMD_CATEGORY(Auction, Debug);
void PrintExpireStats(void)
{
	printf("\nTotal Expires: %d\nTotal Successful Expires: %d\nTotal Skipped Ticks: %d\n", s_TotalLotsExpired, s_TotalLotsExpiredSuccess, s_TotalSkippedTicks);
}
// delete lots in expire order, used for testing.
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslAuctionDeleteLots(U32 uNumToDelete)
{
	if(!isProductionMode())
	{
		U32 iCount = 0;
		AuctionLot *pLot;
		ContainerIterator iter;

		uNumToDelete = min(uNumToDelete, 10000);

		objInitContainerIteratorFromType(GLOBALTYPE_AUCTIONLOT, &iter);
		while( (pLot = objGetNextObjectFromIterator(&iter)) && iCount < uNumToDelete)
		{
			objRequestContainerDestroy(NULL, GLOBALTYPE_AUCTIONLOT, pLot->iContainerID, GLOBALTYPE_AUCTIONSERVER, 0);
			++iCount;
		}
		objClearContainerIterator(&iter);
	}
}
