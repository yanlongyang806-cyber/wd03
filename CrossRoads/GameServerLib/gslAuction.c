//This file contains pass through commands for Client->GameServer->AuctionServer.
// AuctionUI.c contains the originating client commands.

#include "Entity.h"
#include "Player.h"
#include "AuctionLot.h"
#include "gslAuction.h"
#include "gslMail_Old.h"

#include "chatCommon.h"
#include "chatCommonStructs.h"
#include "gslChat.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

#include "GameServerLib.h"

#include "AuctionCommon.h"
#include "AutoGen/AuctionCommon_h_ast.h"
#include "AutoGen/AuctionLot_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/Entity_h_ast.h"

#include "GameAccountDataCommon.h"
#include "contact_common.h"
#include "mission_common.h"
#include "GlobalTypes.h"
#include "globalTypes_h_ast.h"
#include "itemCommon.h"
#include "objContainer.h"
#include "CharacterClass.h"
#include "inventoryCommon.h"
#include "interaction_common.h"
#include "objTransactions.h"
#include "GameStringFormat.h"
#include "gslSendToClient.h"
#include "LoggedTransactions.h"
#include "AutoGen/inventoryCommon_h_ast.h"
#include "AutoGen/gslAuction_c_ast.h"
#include "Reward.h"
#include "Rand.h"
#include "AlgoItem.h"
#include "AutoTransDefs.h"
#include "NotifyCommon.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "utilitiesLib.h"
#include "SavedPetCommon.h"
#include "stringCache.h"
#include "gslSavedPet.h"
#include "SavedPetTransactions.h"
#include "logging.h"
#include "entCritter.h"
#include "GamePermissionsCommon.h"
#include "inventoryCommon.h"
#include "AutoGen/entCritter_h_ast.h"
#include "AuctionLot_Transact.h"
#include "AutoGen/AuctionLot_Transact_h_ast.h"
#include "EntityMailCommon.h"
#include "Character.h"
#include "AuctionBrokerCommon.h"
#include "file.h"
#include "Character.h"
#include "CharacterAttribs.h"
#include "Character_h_ast.h"

#include "gslGatewaySession.h"
#include "gslGatewayAuction.h"

#include "AutoGen/ChatServer_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"

#define MAX_SEARCH_RESULTS 100

extern StaticDefineInt CharClassTypesEnum[];
extern StaticDefineInt ContactFlagsEnum[];

#define AUCTION_REQUEST_TIMEOUT_PERIOD 30
#define AUCTION_REQUESTS_PER_PERIOD 6
#define AUCTION_EXPIRE_STALE_REQUESTS_TIME 50

AUTO_STRUCT;
typedef struct AuctionRequestCache
{
	GlobalType type;
	ContainerID id;

	U32 firstRequestedTime;
	U32 iNumRequests;

	AuctionSearchRequest searchRequest;

} AuctionRequestCache;

AuctionRequestCache **ppRequests;

AUTO_STRUCT;
typedef struct LotItemInfo
{
	char* pchItemDef;
	char** ppchUntranslatedNames;
	S32 iCount;
	bool bTranslateNames;
} LotItemInfo;

AUTO_STRUCT;
typedef struct LotSoldMailCBData
{
	ContainerID iAccountSellerID;
	S32			iLanguageID;
	U32			iPrice;
	U32			uSoldFee;
	char * eSellerAccountName;			AST( ESTRING )
	LotItemInfo** eaLotItems;

} LotSoldMailCBData;

AUTO_STRUCT;
typedef struct AuctionOutbidMailCBData
{
	ContainerID iOldBidderAccountID;
	char *estrOldBidderAccountName;			AST( ESTRING )
	S32			iLanguageID;
	U32			iOldBid;
	U32			iNewBid;	
	LotItemInfo** eaLotItems;
} AuctionOutbidMailCBData;


static bool sbAuctionsDisabled = false;

void gslAuctions_SetAuctionsDisabled(bool bDisabled)
{
	sbAuctionsDisabled = bDisabled;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(AuctionsDisabled);
bool gslAuction_AuctionsDisabled(void)
{
	return sbAuctionsDisabled;
}

AUTO_STARTUP(GslAuctionLoadConfig) ASTRT_DEPS(AS_CharacterAttribs);
void gslAuction_LoadAuctionConfig(void)
{
	AuctionConfig_Load();
}

static AuctionRequestCache *GetCacheForEnt(Entity *ent)
{
	GlobalType type;
	ContainerID id;
	int i;
	int size = eaSize(&ppRequests);

	if (!ent) return NULL;

	type = entGetType(ent);
	id = entGetContainerID(ent);

	for (i = 0; i < size; i++)
	{
		if (type == ppRequests[i]->type && id == ppRequests[i]->id)
			return ppRequests[i];
	}
	return NULL;
}

static bool RegisterRequestIfAble(Entity *ent, AuctionSearchRequest *pRequest, U32 timeDelay)
{
	bool bAllow = false;
	static U32 lastTime;
	U32 curTime = timeSecondsSince2000();
	AuctionRequestCache *pCache = GetCacheForEnt(ent);
	if (lastTime == 0)
	{
		lastTime = curTime;
	}
	if (!pCache)
	{
		pCache = StructCreate(parse_AuctionRequestCache);
		pCache->type = entGetType(ent);
		pCache->id = entGetContainerID(ent);
		eaPush(&ppRequests, pCache);
	}
	if (pCache)
	{
		if (pCache->firstRequestedTime + timeDelay <= curTime)
		{
			pCache->firstRequestedTime = curTime;
			pCache->iNumRequests = 1;
			bAllow = true;
		}
		else if (pCache->iNumRequests < AUCTION_REQUESTS_PER_PERIOD)
		{
			pCache->iNumRequests++;
			bAllow = true;
		}

		if (bAllow && pRequest)
		{			
			StructCopy(parse_AuctionSearchRequest, pRequest, &pCache->searchRequest, 0, 0, 0);
		}
	}
	if (lastTime + AUCTION_EXPIRE_STALE_REQUESTS_TIME <= curTime)
	{
		int i;
		int size = eaSize(&ppRequests);
		for (i = 0; i < size; i++)
		{
			pCache = ppRequests[i];
			if (pCache->firstRequestedTime + AUCTION_EXPIRE_STALE_REQUESTS_TIME <= curTime)
			{
				StructDestroy(parse_AuctionRequestCache, pCache);
				eaRemoveFast(&ppRequests, i);
				i--; size--;
			}
		}
		lastTime = curTime;
	}
	return bAllow;
}

static AuctionSearchRequest *GetLastRequest(Entity *ent)
{
	AuctionRequestCache *pCache = GetCacheForEnt(ent);
	if (pCache)
	{
		return &pCache->searchRequest;
	}
	return NULL;
}




typedef struct AuctionCallbackStructure
{
	AuctionLot *pLot; // Description of what the lot should be
	EntityRef eRef;
	ContainerID eContainerID;
	U64 uItemID;
	int iCount;
	ContainerID lotID;
	INT_EARRAY auctionPetContainerIDs;
	bool bRetryWithNoSeller;	// retry the auction purchase if a failure was caused by no seller container
	TransactionReturnVal* pContainerItemCallback;
} AuctionCallbackStructure;


static bool CanAccessAuctions(Entity *pEnt)
{
	ContactDef *pContactDef = NULL;

	if (!pEnt) {
		return false;
	}
	if (entGetVirtualShardID(pEnt) != 0) {
		return false;
	}
	if (entGetAccessLevel(pEnt) >= ACCESS_DEBUG) {
		return true;
	}
	if (SAFE_MEMBER2(pEnt->pPlayer, pInteractInfo, pContactDialog)) {
		pContactDef = GET_REF(pEnt->pPlayer->pInteractInfo->pContactDialog->hContactDef);
	}
	return (pContactDef && contact_IsMarket(pContactDef)) || GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER;
}

static void gslAuction_GetLotsBidByPlayer_CB(TransactionReturnVal *pReturn, void *pData)
{
	AuctionLotList *pList = NULL;		

	if (pReturn == NULL || RemoteCommandCheck_Auction_GetLots(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS) 
	{		
		Entity *pEnt = (Entity*)entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,(ContainerID)((intptr_t)pData));
		AuctionRequestCache *pCache = GetCacheForEnt(pEnt);

		if (pReturn == NULL)
		{
			pList = StructCreate(parse_AuctionLotList);
		}

		ANALYSIS_ASSUME(pList != NULL);
		pList->cooldownEnd = timeSecondsSince2000();
		if (pCache && pCache->iNumRequests >= AUCTION_REQUESTS_PER_PERIOD)
		{
			pList->cooldownEnd = pCache->firstRequestedTime + AUCTION_REQUEST_TIMEOUT_PERIOD;
		}

		if (ea32Size(&pList->eaiNonExistingAuctionLotContainerIDs) > 0)
		{
			// Some of the auctions no longer exists, do a clean up on the entity
			PlayerAuctionBidListCleanupData param = { 0 };
			ea32Copy(&param.eaiAuctionLotContainerIDsToRemove, &pList->eaiNonExistingAuctionLotContainerIDs);

			AutoTrans_auction_tr_RemoveExpiredAuctionsFromPlayerBidList(NULL, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, (ContainerID)((intptr_t)pData), &param);

			StructDeInit(parse_PlayerAuctionBidListCleanupData, &param);
		}

		ClientCmd_gclAuctionShowAuctionsBidByPlayer(pEnt, pList);

		if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
			session_updateAuctionLotList(wgsFindOwningSessionForDBContainer(GLOBALTYPE_ENTITYPLAYER,(intptr_t)pData),GATEWAYAUCTION_BIDS, pList);

		StructDestroy(parse_AuctionLotList, pList);
	}
}

static void gslAuction_GetLotsOwnedByPlayer_CB(TransactionReturnVal *pReturn, void *pData) {
	AuctionLotList *pList;
	if (RemoteCommandCheck_Auction_GetLots(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS) 
	{
		Entity *pEnt = (Entity*)entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,(ContainerID)((intptr_t)pData));
		AuctionRequestCache *pCache = GetCacheForEnt(pEnt);

		pList->cooldownEnd = timeSecondsSince2000();
		if (pCache && pCache->iNumRequests >= AUCTION_REQUESTS_PER_PERIOD)
		{
			pList->cooldownEnd = pCache->firstRequestedTime + AUCTION_REQUEST_TIMEOUT_PERIOD;
		}

		if(pEnt && pEnt->pPlayer)
		{
			pEnt->pPlayer->uLastLotsPostedByPlayer = eaSize(&pList->ppAuctionLots);
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}

		ClientCmd_gclAuctionShowOwnAuctions(pEnt, pList);

		if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
			session_updateAuctionLotList(wgsFindOwningSessionForDBContainer(GLOBALTYPE_ENTITYPLAYER, (intptr_t)pData), GATEWAYAUCTION_OWNED, pList);

		StructDestroy(parse_AuctionLotList, pList);
	}
}

// Gets an earray of all the containerIDs referenced by items of type kItemDefType_Container in pLot
int gslAuction_GetContainerItemPetsFromLot(AuctionLot* pLot, U32** peaiPetIDs) 
{
	int iAdded = 0;
	if(pLot && pLot->ppItemsV2)
	{
		int i;

		for(i = 0; i < eaSize(&pLot->ppItemsV2); i++)
		{
			Item* pItem = pLot->ppItemsV2[i]->slot.pItem;
			ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
			if(pItemDef && pItemDef->eType == kItemType_Container && pItem && pItem->pSpecialProps && pItem->pSpecialProps->pContainerInfo)
			{
				if(peaiPetIDs) {
					eaiPush(peaiPetIDs, StringToContainerID(REF_STRING_FROM_HANDLE(pItem->pSpecialProps->pContainerInfo->hSavedPet)));
				}
				iAdded++;
			}
		}
	}
	return iAdded;
}

bool gslAuction_LotHasUniqueItem(AuctionLot* pLot)
{
	if (pLot)
	{
		S32 i;
		for (i = eaSize(&pLot->ppItemsV2)-1; i >= 0; i--)
		{
			AuctionSlot* pSlot = pLot->ppItemsV2[i];
			ItemDef* pItemDef = GET_REF(pSlot->slot.pItem->hItem);
			if (pItemDef && (pItemDef->flags & kItemDefFlag_Unique) != 0)
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Auction) ACMD_PRIVATE;
void gslAuction_GetLotsBidByPlayer(Entity *pEnt, const char *pchItemName)
{
	AuctionSearchRequest request = {0};

	if (gslAuction_AuctionsDisabled() || !pEnt || entGetType(pEnt) != GLOBALTYPE_ENTITYPLAYER || (!CanAccessAuctions(pEnt) && entGetAccessLevel(pEnt) < 9))
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.NoResults"), NULL, NULL);
		return;
	}

	if (pEnt->pPlayer->pPlayerAuctionData == NULL || ea32Size(&pEnt->pPlayer->pPlayerAuctionData->eaiAuctionsBid) <= 0)
	{
		// There is nothing to search
		gslAuction_GetLotsBidByPlayer_CB(NULL, (void *)((intptr_t)entGetContainerID(pEnt)));
		return;
	}

	// Copy all auction lot IDs
	ea32Copy(&request.eaiAuctionLotContainerIDs, &pEnt->pPlayer->pPlayerAuctionData->eaiAuctionsBid);

	if (pchItemName && pchItemName[0])
	{
		// Set the search string and language
		request.stringSearch = StructAllocString(pchItemName);
		request.stringLanguage = entGetLanguage(pEnt);
	}

	request.numResults = 100000;
	request.itemQuality = -1;

	if (!RegisterRequestIfAble(pEnt, &request, 1))
	{
		return;
	}
	RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsBidByPlayer_CB, (void *)((intptr_t)entGetContainerID(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, &request);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Auction) ACMD_PRIVATE;
void gslAuction_GetLotsOwnedByPlayer(Entity *pEnt)
{
	AuctionSearchRequest request = {0};
	
	if (gslAuction_AuctionsDisabled() || !pEnt || entGetType(pEnt) != GLOBALTYPE_ENTITYPLAYER || (!CanAccessAuctions(pEnt) && entGetAccessLevel(pEnt) < 9))
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.NoResults"), NULL, NULL);
		return;
	}
	
	request.ownerID = entGetContainerID(pEnt);
	request.numResults = 100000;
	request.itemQuality = -1;
	
	if (!RegisterRequestIfAble(pEnt, NULL, 1))
	{
		return;
	}
	RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsOwnedByPlayer_CB, (void *)((intptr_t)entGetContainerID(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, &request);
}

static void gslAuction_GetLotsSearch_CB(TransactionReturnVal *pReturn, void *pData) {
	AuctionLotList *pList;
	Entity *pEnt = (Entity*)entFromEntityRefAnyPartition((EntityRef)((intptr_t)pData));

	if (RemoteCommandCheck_Auction_GetLots(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS) 
	{
		AuctionRequestCache *pCache = GetCacheForEnt(pEnt);

		pList->cooldownEnd = timeSecondsSince2000();
		if (pCache)
		{
			int i, j;

			if (pCache->iNumRequests >= AUCTION_REQUESTS_PER_PERIOD)
			{
				pList->cooldownEnd = pCache->firstRequestedTime + AUCTION_REQUEST_TIMEOUT_PERIOD;
			}
			for (i = 0; i < eaSize(&pList->ppAuctionLots); i++)
			{
				if (!pList->ppAuctionLots[i])
					continue;
				for (j = 0; j < eaSize(&pList->ppAuctionLots[i]->ppItemsV2); j++)
				{
					if (!pList->ppAuctionLots[i]->ppItemsV2[j] || !pList->ppAuctionLots[i]->ppItemsV2[j]->slot.pItem)
						continue;
					inv_FixupItem(pList->ppAuctionLots[i]->ppItemsV2[j]->slot.pItem);
				}
			}
		}

		ClientCmd_gclAuctionShowSearchResults(pEnt, pList);

		StructDestroy(parse_AuctionLotList, pList);
	}
	else
	{
		if(pEnt)
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.SearchFailed"), NULL, NULL);
			ClientCmd_gclAuctionShowSearchResults(pEnt, NULL);
		}
	}
}

static void gslAuction_GetBrokerLotsSearch_CB(TransactionReturnVal *pReturn, void *pData) {
	AuctionLotList *pList;
	Entity *pEnt = (Entity*)entFromEntityRefAnyPartition((EntityRef)((intptr_t)pData));

	if (RemoteCommandCheck_Auction_GetLots(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS) 
	{
		AuctionRequestCache *pCache = GetCacheForEnt(pEnt);

		pList->cooldownEnd = timeSecondsSince2000();
		if (pCache)
		{
			int i, j;

			if (pCache->iNumRequests >= AUCTION_REQUESTS_PER_PERIOD)
			{
				pList->cooldownEnd = pCache->firstRequestedTime + AUCTION_REQUEST_TIMEOUT_PERIOD;
			}
			for (i = 0; i < eaSize(&pList->ppAuctionLots); i++)
			{
				if (!pList->ppAuctionLots[i])
					continue;
				for (j = 0; j < eaSize(&pList->ppAuctionLots[i]->ppItemsV2); j++)
				{
					if (!pList->ppAuctionLots[i]->ppItemsV2[j] || !pList->ppAuctionLots[i]->ppItemsV2[j]->slot.pItem)
						continue;
					inv_FixupItem(pList->ppAuctionLots[i]->ppItemsV2[j]->slot.pItem);
				}
			}
		}

		ClientCmd_gclAuctionBrokerShowSearchResults(pEnt, pList);

		StructDestroy(parse_AuctionLotList, pList);
	}
	else
	{
		if(pEnt)
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.SearchFailed"), NULL, NULL);
			ClientCmd_gclAuctionBrokerShowSearchResults(pEnt, NULL);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_CATEGORY(Auction);
void gslAuction_GetAllLots(Entity *pEnt)
{
	AuctionSearchRequest request = {0};
	
	if (gslAuction_AuctionsDisabled() || !pEnt || entGetType(pEnt) != GLOBALTYPE_ENTITYPLAYER || (!CanAccessAuctions(pEnt) && entGetAccessLevel(pEnt) < 9))
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.NoResults"), NULL, NULL);
		return;
	}
	
	request.numResults = 100000;
	request.ownerID = entGetContainerID(pEnt);
	request.bExcludeOwner = true;
	request.itemQuality = -1;
	RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsSearch_CB, (void *)((intptr_t)entGetRef(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, &request);
}
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Auction);
void gslAuction_GetLotsForAuctionBroker(Entity *pEnt, S32 iSortColumn)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	AuctionBrokerDef *pAuctionBrokerDef = pDialog ? GET_REF(pDialog->hAuctionBrokerDef) : NULL;
	CharacterClass *pCharacterClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
	const AuctionBrokerClassInfo * pClassInfo;
	const AuctionBrokerLevelInfo * pLevelInfo;
	S32 iLevel;
	AuctionSearchRequest searchRequest;

	if (!pAuctionBrokerDef || !pCharacterClass)
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.NoResults"), NULL, NULL);
		ClientCmd_gclAuctionBrokerShowSearchResults(pEnt, NULL);
		return;
	}

	iLevel = entity_GetSavedExpLevel(pEnt);
	pClassInfo = AuctionBroker_GetClassInfo(pAuctionBrokerDef, pCharacterClass->pchName);
	pLevelInfo = pClassInfo ? AuctionBroker_GetLevelInfo(pClassInfo, iLevel) : NULL;
	if( pDialog->pAuctionBrokerLastUsedLevelInfo )
	{
		StructDestroy(parse_AuctionBrokerLevelInfo, pDialog->pAuctionBrokerLastUsedLevelInfo);
	}
	pDialog->pAuctionBrokerLastUsedLevelInfo = StructClone(parse_AuctionBrokerLevelInfo, pLevelInfo);

	if (!pLevelInfo || eaSize(&pLevelInfo->ppItemDropInfo) == 0)
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.NoResults"), NULL, NULL);
		ClientCmd_gclAuctionBrokerShowSearchResults(pEnt, NULL);
		return;
	}

	// Set up the search request
	StructInit(parse_AuctionSearchRequest, &searchRequest);
	searchRequest.bExcludeOwner = true;
	searchRequest.ownerID = entGetContainerID(pEnt);
	searchRequest.numResults = MAX_SEARCH_RESULTS;
	searchRequest.stringLanguage = entGetLanguage(pEnt);
	searchRequest.pAuctionBrokerSearchData = StructClone(parse_AuctionBrokerLevelInfo, pLevelInfo);
	searchRequest.itemQuality = -1;
	searchRequest.eSortColumn = iSortColumn;

	RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetBrokerLotsSearch_CB, (void *)((intptr_t)entGetRef(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, &searchRequest);

	StructReset(parse_AuctionSearchRequest, &searchRequest);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Auction);
void gslAuction_GetLotsForSearch(Entity *pEnt, AuctionSearchRequest *pRequest)
{
	int i = 0;
	
	if (gslAuction_AuctionsDisabled() || !pEnt || entGetType(pEnt) != GLOBALTYPE_ENTITYPLAYER || (!CanAccessAuctions(pEnt) && entGetAccessLevel(pEnt) < 9))
	{	
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.NoResults"), NULL, NULL);
		ClientCmd_gclAuctionShowSearchResults(pEnt, NULL);
		return;
	}
	if (!pRequest->stringSearch && 
		pRequest->itemQuality == -1 && 
		pRequest->sortType == 0 && 
		(pRequest->pchItemSortTypeCategoryName == NULL || pRequest->pchItemSortTypeCategoryName[0] == '\0') && 
		pRequest->minLevel == 0 && pRequest->maxLevel == 0 && pRequest->maxUsageRestrictionCategory == UsageRestrictionCategory_None)
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.MustChooseCriteria"), NULL, NULL);
		ClientCmd_gclAuctionShowSearchResults(pEnt, NULL);
		return;
	}

	if (gAuctionConfig.bRequiredCategoryForSearch && 
		pRequest->sortType == 0 && 
		(pRequest->pchItemSortTypeCategoryName == NULL || pRequest->pchItemSortTypeCategoryName[0] == '\0'))
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.MustChooseCategory"), NULL, NULL);
		ClientCmd_gclAuctionShowSearchResults(pEnt, NULL);
		return;
	}

	pRequest->numResults = MAX_SEARCH_RESULTS;
	pRequest->stringLanguage = entGetLanguage(pEnt);

	if (gAuctionConfig.bHideOwnAuctionsInSearch)
	{
		pRequest->bExcludeOwner = true;
		if (pEnt)
		{
			pRequest->ownerID = entGetContainerID(pEnt);
		}			
	}
	else
	{
		pRequest->bExcludeOwner = false;
	}

	if (!RegisterRequestIfAble(pEnt, pRequest, AUCTION_REQUEST_TIMEOUT_PERIOD))
	{
		ClientCmd_gclAuctionShowSearchResults(pEnt, NULL);
		return;
	}
	//for (i = 0; i < 100; i++)
	{	
		RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsSearch_CB, (void *)((intptr_t)entGetRef(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, pRequest);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void LotSoldMailCallback(LotSoldMailCBData *pData, CmdContext *pContext)
{
	Entity *pBuyer = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);

	if(pBuyer && pBuyer->pPlayer && pData)
	{
		S32 i;
		char* estrBody = NULL;
		char* estrItemName = NULL;
		char* estrNameTemp = NULL;
		ChatMailStruct mail = {0};

		estrStackCreate(&estrBody);
		estrStackCreate(&estrItemName);
		estrStackCreate(&estrNameTemp);
		
		langFormatMessageKey(pData->iLanguageID, &estrBody, "Auction_Sold_Body", 
			STRFMT_STRING("Name",((Entity *)pBuyer)->pSaved->savedName), 
			STRFMT_STRING("Account", pBuyer->pPlayer->publicAccountName),
			STRFMT_INT("Price", pData->iPrice),
			STRFMT_INT("PriceAfterFees", MAX(0, pData->iPrice - pData->uSoldFee)),
			STRFMT_INT("SoldFee", pData->uSoldFee), 
			STRFMT_END);
			
		estrConcatf(&estrBody,"\n\n");
		for (i = 0; i < eaSize(&pData->eaLotItems); i++)
		{
			LotItemInfo* pItemInfo = pData->eaLotItems[i];
			ItemDef* pItemDef = item_DefFromName(pItemInfo->pchItemDef);
			
			estrClear(&estrItemName);
			estrClear(&estrNameTemp);
			item_GetNameFromUntranslatedStrings(pData->iLanguageID, pItemInfo->bTranslateNames, pItemInfo->ppchUntranslatedNames, &estrNameTemp);
			
			if (item_ShouldGetGenderSpecificName(pItemDef))
			{
				entGetGenderNameFromString(pBuyer, pData->iLanguageID, estrNameTemp, &estrItemName);
			}
			else
			{
				estrPrintf(&estrItemName, "%s", estrNameTemp);
			}
			estrConcatf(&estrBody, "%d %s\n", pItemInfo->iCount, estrItemName);
		}

		gslMail_InitializeMailEx(&mail, NULL, // Seller entity isn't here, populate the fields manually
			langTranslateMessageKeyDefault(pData->iLanguageID, "Auction_Sold_Subject", "[UNTRANSLATED]Auction lot sold."), 
			estrBody, 
			langTranslateMessageKeyDefault(pData->iLanguageID, "Auction_Sold_From_Name", "[UNTRANSLATED]AuctionHouse"), 
			EMAIL_TYPE_NPC_NO_REPLY, 0, timeSecondsSince2000());
		mail.fromID = pData->iAccountSellerID;

		RemoteCommand_ChatServerSendMailByID_v2(GLOBALTYPE_CHATSERVER, 0, 
			 pData->iAccountSellerID, &mail);
		StructDeInit(parse_ChatMailStruct, &mail);

		estrDestroy(&estrBody);
		estrDestroy(&estrItemName);
		estrDestroy(&estrNameTemp);
	}
}

AUTO_TRANS_HELPER
ATR_LOCKS(pSeller, ".Pplayer.Accountid, .Pplayer.Langid, .Pplayer.Publicaccountname, .Psaved.Savedname, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics")
ATR_LOCKS(pAuctionLot, ".Price, .Usoldfee, .Ppitemsv2, .Upostingfee, .Uexpiretime, .Creationtime, .Pbiddinginfo.Icurrentbid");
void auction_trh_LotSoldMailCallback(ATR_ARGS, GlobalType iEntityType, ContainerID iContainerID, ATH_ARG NOCONST(Entity)* pSeller, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	// if pSeller->pPlayer is intentionally checked this way. If it is not checked this way it 
	// the entire transaction fails.
	if(NONNULL(pSeller) && NONNULL(pSeller->pPlayer))
	{
		LotSoldMailCBData *pData = NULL;
		S32 i;
		char* estrBody = NULL;
		char* estrItemNames = NULL;

		estrStackCreate(&estrBody);
		estrStackCreate(&estrItemNames);
		
		pData = StructCreate(parse_LotSoldMailCBData);
		estrPrintf(&pData->eSellerAccountName, "%s", pSeller->pPlayer->publicAccountName);
		pData->iAccountSellerID = pSeller->pPlayer->accountID;
		pData->iLanguageID = pSeller->pPlayer->langID;
		pData->iPrice = pAuctionLot->price;
		pData->uSoldFee = auction_trh_GetFinalSalesFee(ATR_PASS_ARGS, pSeller, pAuctionLot);

		for (i = 0; i < eaSize(&pAuctionLot->ppItemsV2); i++)
		{
			LotItemInfo* pItemInfo = StructCreate(parse_LotItemInfo);
			NOCONST(AuctionSlot) *pSlot = pAuctionLot->ppItemsV2[i];
			NOCONST(Item)* pItem = pSlot->slot.pItem;
			ItemDef* pItemDef = GET_REF(pItem->hItem);
			if (pItemDef)
			{
				if (estrItemNames && estrItemNames[0])
					estrAppend2(&estrItemNames, " ");
				estrAppend2(&estrItemNames, pItemDef->pchName);
				pItemInfo->pchItemDef = StructAllocString(pItemDef->pchName);
			}
			pItemInfo->iCount = pItem->count;
			item_trh_GetNameUntranslated(pItem, pSeller, &pItemInfo->ppchUntranslatedNames, &pItemInfo->bTranslateNames);
			eaPush(&pData->eaLotItems, pItemInfo);
		}

		// sends mail to seller
		QueueRemoteCommand_LotSoldMailCallback(ATR_RESULT_SUCCESS, iEntityType, iContainerID, pData);		
		
		// log information (on buyer) about the sale, includes seller account and price
		
		estrPrintf(&estrBody, "Auction sold by P[%d@%d %s@%s],  price %d: PFee %d: SFee %d: %s", pSeller->myContainerID, pData->iAccountSellerID,
			pSeller->pSaved->savedName, pData->eSellerAccountName, pAuctionLot->price, pAuctionLot->uPostingFee, pAuctionLot->uSoldFee, estrItemNames);
		TRANSACTION_APPEND_LOG_SUCCESS("%s", estrBody);
		
		estrDestroy(&estrBody);
		estrDestroy(&estrItemNames);
		for (i = eaSize(&pData->eaLotItems)-1; i >= 0; i--)
		{
			eaClear(&pData->eaLotItems[i]->ppchUntranslatedNames);
		}
		StructDestroy(parse_LotSoldMailCBData, pData);
	}

}

AUTO_TRANS_HELPER
	ATR_LOCKS(pBuyer, ".Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppuppetmaster.Curid, .Hallegiance, .Hsuballegiance, .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Langid, .Pplayer.Pugckillcreditlimit, .Psaved.Ppuppetmaster.Curtype, .Pchar.Ilevelexp, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .Pinventoryv2.Ppinventorybags, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Pplitebags, .Pplayer.Playertype, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Pptemppuppets")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .Pinventoryv2.Peaowneduniqueitems")
	ATR_LOCKS(pSeller, ".Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Hallegiance, .Hsuballegiance, .Pplayer.Accountid, .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Pplayer.Langid, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pAuctionLot, ".State, .Ownerid, .Ppitemsv2, .Price, .Usoldfee, .Pbiddinginfo.Icurrentbid, .Pbiddinginfo.Ibiddingplayercontainerid, .Upostingfee, .Ilangid, .Uexpiretime, .Creationtime")
	ATR_LOCKS(eaContainerItemPets, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Pplitebags, .Pinventoryv2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
bool auction_trh_PurchaseAuction(ATR_ARGS, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaPets, ATH_ARG NOCONST(Entity) *pSeller, ATH_ARG NOCONST(AuctionLot) *pAuctionLot, ATH_ARG CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, const ItemChangeReason *pBuyerReason, const ItemChangeReason *pSellerReason, GameAccountDataExtract *pBuyerExtract, GameAccountDataExtract *pSellerExtract)
{
	int i;
	NOCONST(Item)* pItem = NULL;
	U32 iSoldPriceAfterFees;
	const char* pchItemName = (NONNULL(pAuctionLot) && NONNULL(pAuctionLot->ppItemsV2) && NONNULL(pAuctionLot->ppItemsV2[0])) ? pAuctionLot->ppItemsV2[0]->pchItemName : NULL;
	F32 fPricePerUnit = pAuctionLot->price;

	if (NONNULL(pAuctionLot) && NONNULL(pAuctionLot->ppItemsV2) && NONNULL(pAuctionLot->ppItemsV2[0]) && NONNULL(pAuctionLot->ppItemsV2[0]->slot.pItem))
		fPricePerUnit /= pAuctionLot->ppItemsV2[0]->slot.pItem->count;


	if (!auction_trh_AcceptsBuyout(ATR_PASS_ARGS, pAuctionLot))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, auction cannot be purchased.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_BuyoutClosed", kNotifyType_AuctionFailed);
		return false;
	}

	if (gAuctionConfig.bPurchasedItemsAreMailed)
	{
		if (auction_trh_CloseAuction(ATR_PASS_ARGS, pSeller, pBuyer, pAuctionLot, true, pSellerReason, pBuyerReason))
		{
			QueueRemoteCommand_Auction_RecordSalePriceForItem(ATR_RESULT_SUCCESS, GLOBALTYPE_AUCTIONSERVER, 0, pchItemName, ceilf(fPricePerUnit));
			return true;
		}
	}

	if (pAuctionLot->state != ALS_Open)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, item already consumed.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_Closed", kNotifyType_AuctionFailed);
		return false;
	}

	if (pBuyer->myContainerID == pAuctionLot->ownerID)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, Can't be purchase auction from self.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_SameOwner", kNotifyType_AuctionFailed);
		return false;
	}

	if(pAuctionLot->price > Auction_MaximumSellPrice())
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, Sell price too high.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_SellPriceTooHigh", kNotifyType_AuctionFailed);
		return false;
	}

	pAuctionLot->state = ALS_Closed;

	for (i = 0; i < eaSize(&pAuctionLot->ppItemsV2); i++)
	{
		NOCONST(AuctionSlot) *pSlot = pAuctionLot->ppItemsV2[i];
		ItemDef *itemDef;
		InvBagIDs destBag = InvBagIDs_Inventory;
		pItem = pSlot->slot.pItem; // changed item rules: caller owns pointer.

		item_trh_ClearPowerIDs(pItem);
		item_trh_FixupAlgoProps(pItem);

		itemDef = GET_REF(pItem->hItem);
		if ( itemDef != NULL )
		{
			destBag = itemAcquireOverride_FromAuction(itemDef);
			if ( destBag == InvBagIDs_None )
			{
				destBag = InvBagIDs_Inventory;
			}
		} 
		if(itemDef && itemDef->eType == kItemType_Container)
		{
			AllegianceDef* pBuyerAllegiance = NONNULL(pBuyer) ? GET_REF(pBuyer->hAllegiance) : NULL;
			AllegianceDef* pSellerAllegiance =  NONNULL(pSeller) ? GET_REF(pSeller->hAllegiance) : NULL;

			if(ISNULL(pBuyerAllegiance) || ISNULL(pSellerAllegiance) || pBuyerAllegiance != pSellerAllegiance)
			{
				AllegianceDef* pBuyerSubAllegiance = NONNULL(pBuyer) ? GET_REF(pBuyer->hSubAllegiance) : NULL;
				AllegianceDef* pSellerSubAllegiance =  NONNULL(pSeller) ? GET_REF(pSeller->hSubAllegiance) : NULL;

				if ((ISNULL(pBuyerSubAllegiance) || pBuyerSubAllegiance != pSellerAllegiance) && (ISNULL(pSellerSubAllegiance) || pBuyerAllegiance != pSellerSubAllegiance))
				{
					TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed: Auction has container item(s) and buyer and seller belong to different allegiances");
					QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_WrongAllegiance", kNotifyType_AuctionFailed);
					return false;
				}
			}

			if(!trhAddSavedPetFromContainerItem(ATR_PASS_ARGS, pBuyer, pItem, eaContainerItemPets, 0, pBuyerReason, pBuyerExtract))
			{
				TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, saved pet from item failed");// %s", pBuyer->debugName );
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_Unknown", kNotifyType_AuctionFailed);
				return false;
			}
		}
		else if (inv_AddItem(ATR_PASS_ARGS, pBuyer, eaPets, destBag, -1, (Item*)pItem, itemDef->pchName, 0, pBuyerReason, pBuyerExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, item add failed");// %s", pBuyer->debugName );
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_ItemAdd", kNotifyType_AuctionFailed);
			return false;
		}
	}

	iSoldPriceAfterFees = pAuctionLot->price - auction_trh_GetFinalSalesFee(ATR_PASS_ARGS, pSeller, pAuctionLot);

	// send the seller an email to show that xx@yyy bought a lot for zzz
	// do it here before the items are destroyed
	auction_trh_LotSoldMailCallback(ATR_PASS_ARGS, pBuyer->myEntityType, pBuyer->myContainerID, pSeller, pAuctionLot);

	eaDestroyStructNoConst(&pAuctionLot->ppItemsV2, parse_AuctionSlot);

	if (pAuctionLot->price)
	{
		if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pBuyer, false, Auction_GetCurrencyNumeric(), -1 * (pAuctionLot->price), pBuyerReason))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, Removing resources failed");
			QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_PurchaseAuction_Unknown", kNotifyType_AuctionFailed);
			return false;
		}		

		if(NONNULL(pSeller))
		{
			// Pay the auction owner
			if (iSoldPriceAfterFees && !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pSeller, false, Auction_GetCurrencyNumeric(), iSoldPriceAfterFees, pSellerReason))
			{
				TRANSACTION_APPEND_LOG_FAILURE("auction_tr_PurchaseAuction failed, could not pay the owner[%d].", pSeller->myContainerID);
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pBuyer->myEntityType, pBuyer->myContainerID, "AuctionServer_Error_PurchaseAuction_Unknown", kNotifyType_AuctionFailed);
				return false;
			}	
		}
	}

	QueueRemoteCommand_Auction_RecordSalePriceForItem(ATR_RESULT_SUCCESS, GLOBALTYPE_AUCTIONSERVER, 0, pchItemName, ceilf(fPricePerUnit));

	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pBuyer, ".Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Langid, .Pplayer.Pugckillcreditlimit, .Psaved.Ppuppetmaster.Curtempid, .Pchar.Ilevelexp, .pInventoryV2.Pplitebags, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Pptemppuppets")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
ATR_LOCKS(pSeller, ".Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Hallegiance, .Hsuballegiance, .Pplayer.Accountid, .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Pugckillcreditlimit, .Pplayer.Langid, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pAuctionLot, ".State, .Ownerid, .Ppitemsv2, .Price, .Usoldfee, .Pbiddinginfo.Icurrentbid, .Pbiddinginfo.Ibiddingplayercontainerid, .Upostingfee, .Ilangid, .Uexpiretime, .Creationtime")
ATR_LOCKS(eaContainerItemPets, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
enumTransactionOutcome auction_tr_PurchaseAuction(ATR_ARGS, NOCONST(Entity) *pBuyer, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, NOCONST(Entity) *pSeller,
												NOCONST(AuctionLot) *pAuctionLot, CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, 
												const ItemChangeReason *pBuyerReason, const ItemChangeReason *pSellerReason,
												GameAccountDataExtract *pBuyerExtract, GameAccountDataExtract *pSellerExtract)
{
	if(!auction_trh_PurchaseAuction(ATR_PASS_ARGS, pBuyer, eaPets, pSeller, pAuctionLot, eaContainerItemPets, pBuyerReason, pSellerReason, pBuyerExtract, pSellerExtract))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Items not purchased.");
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Items purchased.");
}

AUTO_TRANSACTION
	ATR_LOCKS(pBuyer, ".Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppuppetmaster.Curtype, .Hallegiance, .Hsuballegiance, .Psaved.Savedname, .Pplayer.Publicaccountname, .Pplayer.Langid, .Pplayer.Pugckillcreditlimit, .Psaved.Ppuppetmaster.Curtempid, .Pchar.Ilevelexp, .pInventoryV2.Pplitebags, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Playertype, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, , .Psaved.Ppuppetmaster.Pptemppuppets")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pAuctionLot, ".State, .Ownerid, .Ppitemsv2, .Price, .Usoldfee, .Pbiddinginfo.Icurrentbid, .Pbiddinginfo.Ibiddingplayercontainerid, .Upostingfee, .Ilangid, .Uexpiretime, .Creationtime")
	ATR_LOCKS(eaContainerItemPets, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
enumTransactionOutcome auction_tr_PurchaseAuctionNoSeller(ATR_ARGS, NOCONST(Entity) *pBuyer, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, 
													NOCONST(AuctionLot) *pAuctionLot, CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, 
													const ItemChangeReason *pBuyerReason, GameAccountDataExtract *pExtract)
{
	if(!auction_trh_PurchaseAuction(ATR_PASS_ARGS, pBuyer, eaPets, NULL, pAuctionLot, eaContainerItemPets, pBuyerReason, NULL, pExtract, NULL))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Items not purchased.");
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Items purchased.");
}

void Auction_FreeCallbackStructure(AuctionCallbackStructure *pData)
{
	if(pData)
	{
		if(pData->pLot)
		{
			StructDestroy(parse_AuctionLot, pData->pLot);
		}
		
		ea32Destroy(&pData->auctionPetContainerIDs);
		free(pData);
	}
}

static void LotPurchase_CB(TransactionReturnVal *pReturn, AuctionCallbackStructure *pData) 
{	
	Entity *pEnt = entFromEntityRefAnyPartition(pData->eRef);

	if(!pEnt && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		pEnt = entForClientCmd(pData->eContainerID,pEnt);

	if (pEnt)
	{	
		if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) 
		{
			S32 i; // for iterating over outcomes

			// check to see if the transaction failed because of no container
			// if that is the case then the seller is gone and we want to try purchasing with no seller
			if(pData->bRetryWithNoSeller)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

				for(i = 0; i < pReturn->iNumBaseTransactions; ++i)
				{
					if(pReturn->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE && pReturn->pBaseReturnVals[i].returnString && 
						strstri(pReturn->pBaseReturnVals[i].returnString, OBJ_TRANS_CONTAINER_DOES_NOT_EXIST) && strstri(pReturn->pBaseReturnVals[i].returnString, "GlobalType=EntityPlayer"))
					{
						U32* eaPets = NULL;
						// do another transaction but with no seller as his container is gone
						AuctionCallbackStructure *pStruct = calloc(sizeof(AuctionCallbackStructure),1);
						U32* eaiContainerItemPets = NULL;
						ItemChangeReason reason = {0};

						pStruct->eRef = entGetRef(pEnt);
						pStruct->pLot = StructClone(parse_AuctionLot, pData->pLot);
						gslAuction_GetContainerItemPetsFromLot(pData->pLot, &eaiContainerItemPets);
						ea32Create(&eaPets);
						if (gslAuction_LotHasUniqueItem(pData->pLot))
						{
							Entity_GetPetIDList(pEnt, &eaPets);
						}

						inv_FillItemChangeReason(&reason, pEnt, "Auction:Purchase", "NoSeller");

						AutoTrans_auction_tr_PurchaseAuctionNoSeller(LoggedTransactions_CreateManagedReturnValEnt("AuctionNoSeller", pEnt, LotPurchase_CB, pStruct), GLOBALTYPE_GAMESERVER, 
								entGetType(pEnt), entGetContainerID(pEnt), 
								GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
								GLOBALTYPE_AUCTIONLOT, pData->pLot->iContainerID, 
								GLOBALTYPE_ENTITYSAVEDPET, &eaiContainerItemPets, 
								&reason, pExtract);
						eaiDestroy(&eaiContainerItemPets);
						ea32Destroy(&eaPets);
						return;
					}
				}
			}

			for(i = 0; i < pReturn->iNumBaseTransactions; ++i)
			{
				if(pReturn->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE && pReturn->pBaseReturnVals[i].returnString && 
					strstr(pReturn->pBaseReturnVals[i].returnString, "log Unable to add item") != 0)
				{
					notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Inventory.InventoryFull"), NULL, NULL);
					return;
				}
			}

			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.AuctionPurchasedFail"), NULL, NULL);
		}
		else
		{
			AuctionSearchRequest *pRequest;
			objRequestContainerDestroy(LoggedTransactions_CreateManagedReturnValEnt("Auction",pEnt,NULL, NULL), GLOBALTYPE_AUCTIONLOT, pData->pLot->iContainerID, GLOBALTYPE_AUCTIONSERVER, 0);

			pRequest = GetLastRequest(pEnt);

			if (pRequest)
			{
				if (ea32Size(&pRequest->eaiAuctionLotContainerIDs) > 0)
				{
					ClientCmd_gclAuctionClearPlayerBidResults(pEnt);
					RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsBidByPlayer_CB, (void *)((intptr_t)entGetContainerID(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, pRequest);
				}
				else
				{
					ClientCmd_gclAuctionClearSearchResults(pEnt);
					RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsSearch_CB, (void *)((intptr_t)entGetRef(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, pRequest);
				}
			}
			
			notify_NotifySend(pEnt, kNotifyType_AuctionSuccess, entTranslateMessageKey(pEnt, "Auction.AuctionPurchased"), NULL, NULL);
		}
	}

	Auction_FreeCallbackStructure(pData);
}


AUTO_TRANSACTION
ATR_LOCKS(pBuyer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Publicaccountname, .pInventoryV2.Pplitebags, .Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Pptemppuppets")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
ATR_LOCKS(pAuctionLot, ".State, .Ownerid, .Ppitemsv2, .Pbiddinginfo.Inumbids")
ATR_LOCKS(eaContainerItemPets, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
enumTransactionOutcome auction_tr_CancelAuction(ATR_ARGS, NOCONST(Entity) *pBuyer, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, 
											NOCONST(AuctionLot) *pAuctionLot, CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, 
											const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;
	NOCONST(Item)* pItem = NULL;
	if (pAuctionLot->state != ALS_Open)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_CancelAuction failed, item already consumed.");
	}

	if (pBuyer->myContainerID != pAuctionLot->ownerID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_CancelAuction failed, Can't be cancelled by someone else.");
	}

	if (!auction_trh_CanBeCanceled(ATR_PASS_ARGS, pAuctionLot))
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_CancelAuction failed, Can't be cancelled because there are already bids for the auction.");
	}

	pAuctionLot->state = ALS_Closed;

	for (i = 0; i < eaSize(&pAuctionLot->ppItemsV2); i++)
	{
		NOCONST(AuctionSlot) *pSlot = pAuctionLot->ppItemsV2[i];
		ItemDef *itemDef;
		InvBagIDs destBag = InvBagIDs_Inventory;
		pItem = pSlot->slot.pItem;

		item_trh_ClearPowerIDs(pItem);
		pItem->id = 0; //Reset the id, so we don't have an id conflict with older items / stacks

		itemDef = GET_REF(pItem->hItem);
		if ( itemDef != NULL )
		{
			destBag = itemAcquireOverride_FromAuction(itemDef);
			if ( destBag == InvBagIDs_None )
			{
				destBag = InvBagIDs_Inventory;
			}
		}
		if(itemDef && itemDef->eType == kItemType_Container)
		{
			if(!trhAddSavedPetFromContainerItem(ATR_PASS_ARGS, pBuyer, pItem, eaContainerItemPets, 0, pReason, pExtract))
			{
				TRANSACTION_APPEND_LOG_FAILURE("auction_tr_CancelAuction failed, saved pet from item failed");// %s", pBuyer->debugName );
				return false;
			}
		}
		else if (inv_AddItem(ATR_PASS_ARGS, pBuyer, eaPets, destBag, -1, (Item*)pItem, itemDef->pchName, 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_CancelAuction failed, item add failed");// %s", pBuyer->debugName );
		}
	}
	
	eaDestroyStructNoConst(&pAuctionLot->ppItemsV2, parse_AuctionSlot);

	TRANSACTION_RETURN_LOG_SUCCESS("Items transfered.");
}

// Cancel a mail auction. Should not be accessible to players, used to roll back
// in case we make an auction but the mailing fails for some reason.
AUTO_TRANSACTION
	ATR_LOCKS(pBuyer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Publicaccountname, .pInventoryV2.Pplitebags, .Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Pptemppuppets")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pAuctionLot, ".State, .Ownerid, .Ppitemsv2")
	ATR_LOCKS(eaContainerItemPets, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
enumTransactionOutcome auction_tr_CancelMail(ATR_ARGS, NOCONST(Entity) *pBuyer, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, 
											NOCONST(AuctionLot) *pAuctionLot, CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, 
											const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;
	NOCONST(Item)* pItem = NULL;
	if (pAuctionLot->state != ALS_Mailed)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_CancelMail failed, item is not mailed.");
	}

	if (pBuyer->myContainerID != pAuctionLot->ownerID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_CancelMail failed, Can't be cancelled by someone else.");
	}

	pAuctionLot->state = ALS_Closed;

	for (i = 0; i < eaSize(&pAuctionLot->ppItemsV2); i++)
	{
		NOCONST(AuctionSlot) *pSlot = pAuctionLot->ppItemsV2[i];
		ItemDef *itemDef;
		InvBagIDs destBag = InvBagIDs_Inventory;
		pItem = pSlot->slot.pItem;

		item_trh_ClearPowerIDs(pItem);

		itemDef = GET_REF(pItem->hItem);
		if ( itemDef != NULL )
		{
			destBag = itemAcquireOverride_FromMail(itemDef);
			if ( destBag == InvBagIDs_None )
			{
				destBag = InvBagIDs_Inventory;
			}
		}
		if(itemDef && itemDef->eType == kItemType_Container)
		{
			if(!trhAddSavedPetFromContainerItem(ATR_PASS_ARGS, pBuyer, pItem, eaContainerItemPets, 0, pReason, pExtract))
			{
				TRANSACTION_APPEND_LOG_FAILURE("auction_tr_CancelMail failed, saved pet from item failed");// %s", pBuyer->debugName );
				return false;
			}
		}
		else if (inv_AddItem(ATR_PASS_ARGS, pBuyer, eaPets, destBag, -1, (Item*)pItem, itemDef->pchName,  0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_CancelMail failed, item add failed");// %s", pBuyer->debugName );
		}
	}
	eaDestroyStructNoConst(&pAuctionLot->ppItemsV2, parse_AuctionSlot);

	TRANSACTION_RETURN_LOG_SUCCESS("Items transfered.");
}

static S32 *eaCanceledLots;			// the lot id
static U32 *eaCanceledLotsTm;		// time the lot was canceled

static void LotCancel_CB(TransactionReturnVal *pReturn, AuctionCallbackStructure *pData) 
{	
	Entity *pEnt = entFromEntityRefAnyPartition(pData->eRef);
	if (pEnt)
	{	
		if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) 
		{		
			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.AuctionCancelledFail"), NULL, NULL);
		}
		else
		{
			objRequestContainerDestroy(LoggedTransactions_CreateManagedReturnValEnt("Auction",pEnt,NULL, NULL), GLOBALTYPE_AUCTIONLOT, pData->pLot->iContainerID, GLOBALTYPE_AUCTIONSERVER, 0);			
			gslAuction_GetLotsOwnedByPlayer(pEnt);
			notify_NotifySend(pEnt, kNotifyType_AuctionSuccess, entTranslateMessageKey(pEnt, "Auction.AuctionCancelled"), NULL, NULL);
		}
	}

	Auction_FreeCallbackStructure(pData);
}

bool gslAuction_ValidatePermissions(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot,
	SA_PARAM_NN_VALID U32 **peaiContainerItemPets,
	const char *pchAuctionsDisabledErrorMessage,
	const char *pchTrialErrorMessage)
{
	if (gslAuction_AuctionsDisabled() || !pEnt || pEnt->myEntityType != GLOBALTYPE_ENTITYPLAYER || (!CanAccessAuctions(pEnt) && entGetAccessLevel(pEnt) < 9))
	{
		notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, pchAuctionsDisabledErrorMessage), NULL, NULL);
		return false;
	}
	else
	{
		GameAccountDataExtract *pExtract;
		int i;

		if( gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_USE_MARKET))
		{
			ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, pchTrialErrorMessage), false);
			return false;
		}

		gslAuction_GetContainerItemPetsFromLot(pCopiedLot, peaiContainerItemPets);

		pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		if(*peaiContainerItemPets)
		{
			int iPartitionIdx = entGetPartitionIdx(pEnt);
			for(i=0; i<eaiSize(peaiContainerItemPets); i++)
			{
				char* estrError = NULL;
				Entity* pPetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYSAVEDPET, (*peaiContainerItemPets)[i]);
				estrCreate(&estrError);
				if(pPetEnt && !Entity_CanAcceptPetTransfer(pEnt, pPetEnt, pExtract, &estrError))
				{
					ClientCmd_gclAuctionPostComplete(pEnt, estrError, false);
					return false;
				}
				estrDestroy(&estrError);
			}
		}

		return true;
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CATEGORY(Auction, Inventory);
void gslAuction_PurchaseAuction(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot, bool bPurchasing)
{
	U32* eaiContainerItemPets = NULL;
	if (gslAuction_ValidatePermissions(pEnt, pCopiedLot, &eaiContainerItemPets, "Auction.AuctionPurchasedFail", "Auction.NoTrialAuctionPurchases"))
	{
		U32* eaPets = NULL;
		AuctionCallbackStructure *pStruct;
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

		pStruct = calloc(sizeof(AuctionCallbackStructure),1);
		pStruct->eRef = entGetRef(pEnt);
		pStruct->pLot = StructClone(parse_AuctionLot, pCopiedLot);
		pStruct->eContainerID = entGetContainerID(pEnt);

		ea32Create(&eaPets);
		if (gslAuction_LotHasUniqueItem(pCopiedLot))
		{
			Entity_GetPetIDList(pEnt, &eaPets);
		}

		if (pCopiedLot->ownerID != entGetContainerID(pEnt))
		{
			GameAccountDataExtract *pSellerExtract = NULL; // Don't have seller entity at this time.
			ItemChangeReason reasonBuyer = {0}, reasonSeller = {0};
            static char *sellerDetail = NULL;
            static char *buyerDetail = NULL;
            const char *itemName = "";
            const char *sellerName = "";
            const char *sellerHandle = "";

            // Extract the item name for logging.
            if ( pCopiedLot->ppItemsV2 && pCopiedLot->ppItemsV2[0] && pCopiedLot->ppItemsV2[0]->slot.pItem )
            {
                ItemDef *itemDef = GET_REF(pCopiedLot->ppItemsV2[0]->slot.pItem->hItem);
                if ( itemDef )
                {
                    itemName = itemDef->pchName;
                }
            }

            // Extract the seller name for logging.
            if ( pCopiedLot->pOptionalData )
            {
                if ( pCopiedLot->pOptionalData->pchOwnerName )
                {
                    sellerName = pCopiedLot->pOptionalData->pchOwnerName;
                }
                if ( pCopiedLot->pOptionalData->pchOwnerHandle )
                {
                    sellerHandle = pCopiedLot->pOptionalData->pchOwnerHandle;
                }
            }

            estrClear(&sellerDetail);
            estrClear(&buyerDetail);

            estrConcatf(&buyerDetail, "buyer=%s, item=%s", pEnt->debugName, itemName);
            estrConcatf(&sellerDetail, "seller=%s@%s playerID=%u, item=%s", sellerName, sellerHandle, pCopiedLot->ownerID, itemName);

			pStruct->bRetryWithNoSeller = !gAuctionConfig.bPurchasedItemsAreMailed; // When items are mailed we need a seller

			inv_FillItemChangeReason(&reasonBuyer, pEnt, "Auction:Purchase:Buyer", buyerDetail);
			inv_FillItemChangeReason(&reasonSeller, NULL, "Auction:Purchase:Seller", sellerDetail);

			AutoTrans_auction_tr_PurchaseAuction(LoggedTransactions_CreateManagedReturnValEnt("Auction",pEnt,LotPurchase_CB,pStruct), GetAppGlobalType(), 
				entGetType(pEnt), entGetContainerID(pEnt),
				GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
				GLOBALTYPE_ENTITYPLAYER, pCopiedLot->ownerID,
				GLOBALTYPE_AUCTIONLOT, pCopiedLot->iContainerID,
				GLOBALTYPE_ENTITYSAVEDPET, &eaiContainerItemPets,
				&reasonBuyer, &reasonSeller,
				pExtract, pSellerExtract);

		}
		else
		{
			bool bDoCancel = true;

			if(eaiFind(&eaCanceledLots, pCopiedLot->iContainerID) != -1)
			{
				// already have this lot, don't cancel it again
				bDoCancel = false;
			}

			if(bDoCancel)
			{
				ItemChangeReason reason = {0};
				S32 i;
				U32 uTm = timeSecondsSince2000();

				inv_FillItemChangeReason(&reason, pEnt, "Auction:Cancel", NULL);
				
				for(i = eaiSize(&eaCanceledLotsTm) - 1; (i >=0 && i < eaiSize(&eaCanceledLotsTm)); --i)
				{
					// cancels older than 30 seconds are removed
					if(uTm > eaCanceledLotsTm[i])	
					{
						eaiPop(&eaCanceledLotsTm);
						eaiPop(&eaCanceledLots);
					}
					else
					{
						break;
					}
				}

				// record so we don't keep running same cancel transaction over and over again
				eaiInsert(&eaCanceledLots, pCopiedLot->iContainerID,0);
				eaiInsert(&eaCanceledLotsTm, uTm + 30,0);

				AutoTrans_auction_tr_CancelAuction(LoggedTransactions_CreateManagedReturnValEnt("Auction",pEnt,bPurchasing ? LotPurchase_CB : LotCancel_CB,pStruct), GLOBALTYPE_GAMESERVER, 
					entGetType(pEnt), entGetContainerID(pEnt), GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
					GLOBALTYPE_AUCTIONLOT, pCopiedLot->iContainerID,
					GLOBALTYPE_ENTITYSAVEDPET, &eaiContainerItemPets,
					&reason, pExtract);

			}
		}

		ea32Destroy(&eaPets);
	}
	eaiDestroy(&eaiContainerItemPets);
}

static void gslAuction_BidCB(TransactionReturnVal *pReturn, AuctionCallbackStructure *pData) 
{	
	Entity *pEnt = entFromEntityRefAnyPartition(pData->eRef);

	if(!pEnt && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		pEnt = entForClientCmd(pData->eContainerID,pEnt);

	if (pEnt)
	{	
		AuctionSearchRequest *pRequest = GetLastRequest(pEnt);

		if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) 
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.AuctionBidFailed"), NULL, NULL);
		}
		else
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionSuccess, entTranslateMessageKey(pEnt, "Auction.AuctionBidSuccessful"), NULL, NULL);
		}

		if (pRequest)
		{
			if (ea32Size(&pRequest->eaiAuctionLotContainerIDs) > 0)
			{
				ClientCmd_gclAuctionClearPlayerBidResults(pEnt);
				RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsBidByPlayer_CB, (void *)((intptr_t)entGetContainerID(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, pRequest);
			}
			else
			{
				ClientCmd_gclAuctionClearSearchResults(pEnt);
				RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslAuction_GetLotsSearch_CB, (void *)((intptr_t)entGetRef(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, pRequest);
			}
		}
	}

	Auction_FreeCallbackStructure(pData);
}

bool gslAuction_ValidateBidOnAuction(Entity *pEnt, AuctionLot *pLot, U32 iBidValue)
{
	// Make sure this auction is not closed to bidding
	if (!Auction_AcceptsBids(pLot))
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.BiddingClosed"), false);
		return false;
	}

	// Make sure the bid amount is valid
	if (iBidValue < Auction_GetMinimumNextBidValue(pLot))
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.BidNotHighEnough"), false);
		return false;
	}

	// Make sure the bidder has enough money
	if (inv_GetNumericItemValue(pEnt, Auction_GetCurrencyNumeric()) < (S32)iBidValue)
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.NotEnoughResourcesForBid"), false);
		return false;
	}

	return true;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CATEGORY(Auction, Inventory);
void gslAuction_BidOnAuction(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot, U32 iBidValue)
{
	U32* eaiContainerItemPets = NULL;
	if (gslAuction_ValidatePermissions(pEnt, pCopiedLot, &eaiContainerItemPets, "Auction.AuctionBiddingFailed", "Auction.NoTrialAuctionBids"))
	{
		AuctionCallbackStructure *pStruct;
		ItemChangeReason reason = {0};

		// If the bidder can already buyout the auction with this bid make it so
		if (Auction_AcceptsBuyout(pCopiedLot) && iBidValue >= pCopiedLot->price)
		{
			gslAuction_PurchaseAuction(pEnt, pCopiedLot, true);
			eaiDestroy(&eaiContainerItemPets);
			return;
		}

		gslAuction_ValidateBidOnAuction(pEnt,pCopiedLot,iBidValue);

		pStruct = calloc(sizeof(AuctionCallbackStructure),1);
		pStruct->eRef = entGetRef(pEnt);
		pStruct->pLot = StructClone(parse_AuctionLot, pCopiedLot);
		pStruct->eContainerID = entGetContainerID(pEnt);

		inv_FillItemChangeReason(&reason, pEnt, "Auction:Bid", pCopiedLot->description);

		AutoTrans_auction_tr_BidOnAuction(LoggedTransactions_CreateManagedReturnValEnt("Auction", pEnt, gslAuction_BidCB, pStruct), GetAppGlobalType(),
			GLOBALTYPE_ENTITYPLAYER, pCopiedLot->pBiddingInfo->iBiddingPlayerContainerID,
			entGetType(pEnt), entGetContainerID(pEnt),
			GLOBALTYPE_AUCTIONLOT, pCopiedLot->iContainerID,
			iBidValue, &reason);
	}
	eaiDestroy(&eaiContainerItemPets);
}

AUTO_TRANSACTION
	ATR_LOCKS(pBuyer, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Accountid, .Psaved.Ppuppetmaster.Curtype, .Pplayer.Publicaccountname, .Psaved.Ppuppetmaster.Curtempid, .pInventoryV2.Pplitebags, .Psaved.Ppownedcontainers, .Hallegiance, .Hsuballegiance, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppallowedcritterpets, .Psaved.Ppalwayspropslots, .Psaved.Ppuppetmaster.Pppuppetrequests, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pugckillcreditlimit, .Pplayer.Playertype, .Itemidmax, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Pptemppuppets")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems")
	ATR_LOCKS(pAuctionLot, ".Icontainerid, .State, .Recipientid, .Ppitemsv2")
	ATR_LOCKS(eaContainerItemPets, ".Psaved.Pscpdata, .Pchar, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Psaved.Ipetid, .Pcritter.Petdef, .Psaved.Savedname, .pInventoryV2.Ppinventorybags, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Curtempid, .Psaved.Ppallowedcritterpets, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Hallegiance, .Hsuballegiance, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pplayer.Playertype");
enumTransactionOutcome auction_tr_AcceptMailedAuction(ATR_ARGS, NOCONST(Entity) *pBuyer, CONST_EARRAY_OF(NOCONST(Entity)) eaPets, 
													NOCONST(AuctionLot) *pAuctionLot, CONST_EARRAY_OF(NOCONST(Entity)) eaContainerItemPets, 
													const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;
	NOCONST(Item)* pItem = NULL;
	S32 iAuctionLot = pAuctionLot->iContainerID;
	if (pAuctionLot->state != ALS_Mailed)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AcceptMailedAuction failed, auction lot %d already consumed.", iAuctionLot);
	}

	if (ISNULL(pBuyer->pPlayer) || pAuctionLot->recipientID != pBuyer->pPlayer->accountID)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Entity's account does not own mail lot %d.", iAuctionLot);
	}


	pAuctionLot->state = ALS_Closed;

	for (i = 0; i < eaSize(&pAuctionLot->ppItemsV2); i++)
	{
		InvBagIDs destBag = InvBagIDs_Inventory;
		ItemDef *itemDef;
		NOCONST(AuctionSlot) *pSlot = pAuctionLot->ppItemsV2[i];
		pItem = pSlot->slot.pItem;

		itemDef = GET_REF(pItem->hItem);
		if(ISNULL(itemDef))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_tr_AcceptMailedAuction failed, Null itemdef");
			return TRANSACTION_OUTCOME_FAILURE;
		}

		item_trh_ClearPowerIDs(pItem);

		destBag = itemAcquireOverride_FromMail(itemDef);
		if ( destBag == InvBagIDs_None )
		{
			destBag = InvBagIDs_Inventory;
		}

		if(itemDef->eType == kItemType_Container)
		{
			if(!trhAddSavedPetFromContainerItem(ATR_PASS_ARGS, pBuyer, pItem, eaContainerItemPets, 0, pReason, pExtract))
			{
				TRANSACTION_APPEND_LOG_FAILURE("auction_tr_AcceptMailedAuction failed, saved pet from item failed");// %s", pBuyer->debugName );
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
		else if (inv_AddItem(ATR_PASS_ARGS, pBuyer, eaPets, destBag, -1, (Item*)pItem, itemDef->pchName, 0, pReason, pExtract) != TRANSACTION_OUTCOME_SUCCESS)
		{
			TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AcceptMailedAuction failed, item add failed");// %s", pBuyer->debugName );
		}
	}
	eaDestroyStructNoConst(&pAuctionLot->ppItemsV2, parse_AuctionSlot);

	TRANSACTION_RETURN_LOG_SUCCESS("Items transfered from lot %d", iAuctionLot);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pEnt, ".Psaved.Ppuppetmaster, .Pplayer.Publicaccountname, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Pppreferredpetids")
ATR_LOCKS(pPetEnt, ".Pcritter.Petdef, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Hclass")
ATR_LOCKS(pAuctionLot, ".State");
bool auction_trh_AddContainerItemToAuction(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(Entity)* pPetEnt, ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	ContainerID iPetID = 0;
	PetDef* pPetDef = NULL;
	ItemDef* pItemDef = NULL;

	if (pAuctionLot->state != ALS_New)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_trh_AddContainerItemToAuction failed, items already added.");
		return false;
	}


	if(NONNULL(pPetEnt)) {
		//This item represents an owned container on the entity
		int iPet;

		// Determine if the entity is capable of saved pet auctions
		if(ISNULL(pEnt->pSaved) || ISNULL(pPetEnt) || ISNULL(pPetEnt->pSaved))
		{
			TRANSACTION_APPEND_LOG_FAILURE("error: Source entity %s@%s is not capable of saved pet auctions",
				pEnt->debugName,pEnt->pPlayer->publicAccountName);
			return false;
		}

		pPetDef = NONNULL(pPetEnt->pCritter) ? GET_REF(pPetEnt->pCritter->petDef) : NULL;
		pItemDef = NONNULL(pPetDef) ? GET_REF(pPetDef->hTradableItem) : NULL;

		if(!pItemDef)
		{
			TRANSACTION_APPEND_LOG_FAILURE("error: Pet %s is not tradeable (no tradeable item in pet def)",
				pPetEnt->debugName);
			return false;
		}

		trhRemoveSavedPet(ATR_PASS_ARGS, pEnt, pPetEnt);

		// Remove the puppet if this pet is a puppet
		if(pEnt->pSaved->pPuppetMaster)
		{
			for(iPet=eaSize(&pEnt->pSaved->pPuppetMaster->ppPuppets)-1;iPet>=0;iPet--)
			{
				if(pEnt->pSaved->pPuppetMaster->ppPuppets[iPet]->curID == iPetID)
				{
					NOCONST(PuppetEntity) *pPuppet = pEnt->pSaved->pPuppetMaster->ppPuppets[iPet];

					if(pPuppet->eState == PUPPETSTATE_ACTIVE)
					{
						TRANSACTION_APPEND_LOG_FAILURE("error: cannot remove active puppet (%d) in a trade %s@%s",iPetID,pEnt->debugName,pEnt->pPlayer->publicAccountName);
						return false;
					}

					eaRemove(&pEnt->pSaved->pPuppetMaster->ppPuppets,iPet);
					StructDestroyNoConst(parse_PuppetEntity,pPuppet);
				}
			}
		}
	} 
	else 
	{
		TRANSACTION_APPEND_LOG_FAILURE("Cannot add item to auction: Invalid pet entity");
		return false;
	}

	TRANSACTION_APPEND_LOG_SUCCESS("Successfully added item to auction: Saved pet removed.");
	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags[], .Psaved.Ppuppetmaster, .Pplayer.Pugckillcreditlimit, .Pplayer.Publicaccountname, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Ppownedcontainers, .Psaved.Ppalwayspropslots, .Psaved.Pipetidsremovedfixup, .Psaved.Pppreferredpetids, .Hallegiance, .Hsuballegiance, pInventoryV2.ppLiteBags[]")
ATR_LOCKS(pPetEnts, ".Pcritter.Petdef, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Hclass")
ATR_LOCKS(pAuctionLot, ".Upostingfee, .State");
enumTransactionOutcome auction_tr_AddContainerItemsToAuction(ATR_ARGS, NOCONST(Entity)* pEnt, CONST_EARRAY_OF(NOCONST(Entity)) pPetEnts, 
															NOCONST(AuctionLot) *pAuctionLot, int newState, const ItemChangeReason *pReason)
{
	int i;

	if(!gConf.bAllowContainerItemsInAuction)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Creating Auctions with Container Items is currently disabled.");
	}


	for(i = 0; i < eaSize(&pPetEnts); i++)
	{
		if(!auction_trh_AddContainerItemToAuction(ATR_PASS_ARGS, pEnt, pPetEnts[i], pAuctionLot))
		{
			TRANSACTION_RETURN_LOG_FAILURE("Failed to add container items to auction.  Item add failed");
		}
	}

	if(pAuctionLot->uPostingFee > 0)
	{
		if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, Auction_GetCurrencyNumeric(), -1 * (pAuctionLot->uPostingFee), pReason))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_tr_AddContainerItemsToAuction failed, unable to subtract posting fee resources.");
			return false;
		}		
	}


	pAuctionLot->state = newState;
	TRANSACTION_RETURN_LOG_SUCCESS("Container Items successfully added to auction");
}


AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pinventoryv2.Ppinventorybags, .Pinventoryv2.Peaowneduniqueitems, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pinventoryv2.Pplitebags, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype")
	ATR_LOCKS(pAuctionLot, ".State, .Ppitemsv2, .Upostingfee, .Auctionpetcontainerids")
	ATR_LOCKS(eaPets, ".Psaved.Conowner.Containerid, .Pinventoryv2.Ppinventorybags, .Psaved.Conowner.Containertype, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Pinventoryv2.Peaowneduniqueitems, .Pinventoryv2.Pplitebags");
enumTransactionOutcome auction_tr_AddItemsToAuction(ATR_ARGS, NOCONST(Entity)* pEnt, NOCONST(AuctionLot) *pAuctionLot, 
													CONST_EARRAY_OF(NOCONST(Entity)) eaPets, int newState, U32 bChargePostingFee, 
													const ItemChangeReason *pReason, GameAccountDataExtract *pExtract)
{
	int i;
	if (pAuctionLot->state != ALS_New)
	{
		TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AddItemsToAuction failed, items already added.");
	}

	pAuctionLot->state = newState;

	for (i = 0; i < eaSize(&pAuctionLot->ppItemsV2); i++)
	{
		NOCONST(AuctionSlot) *pSlot = pAuctionLot->ppItemsV2[i];
		ContainerID containerID = ea32Get(&pAuctionLot->auctionPetContainerIDs,i);
		int pet = 0;
		ItemDef* pItemDef = NULL;
		pItemDef = NONNULL(pSlot->slot.pItem) ? GET_REF(pSlot->slot.pItem->hItem) : NULL;

		if(NONNULL(pItemDef) && pItemDef->eType != kItemType_Container) 
		{
			InvBagIDs eBagID;
			int slotNum;
			Item *pRemovedItem = NULL;

			if (containerID)
			{
				for (pet = eaSize(&eaPets)-1; pet >= 0; pet--)
				{
					if (eaPets[pet]->myContainerID == containerID)
						break;
				}
				if (pet < 0)
				{
					TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AddItemsToAuction failed, item remove failed");// %s", pEnt->debugName );
				}

				// don't allow them to list an item from the buyback bag
				inv_trh_GetItemByID(ATR_PASS_ARGS, pEnt, pSlot->slot.pItem->id, &eBagID, &slotNum, InvGetFlag_NoBuyBackBag);
				if ( eBagID == InvBagIDs_Buyback )
				{
					TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AddItemsToAuction failed, selling items in buyback bag not allowed");
				}

				pRemovedItem = inv_RemoveItemByID(ATR_PASS_ARGS, eaPets[pet], pSlot->slot.pItem->id, pSlot->slot.pItem->count, 0, pReason, pExtract);
				if(!pRemovedItem)
				{
					TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AddItemsToAuction failed, item remove failed");// %s", pPet->debugName );
				}
			}
			else 
			{
				// don't allow them to list an item from the buyback bag
				inv_trh_GetItemByID(ATR_PASS_ARGS, pEnt, pSlot->slot.pItem->id, &eBagID, &slotNum, InvGetFlag_NoBuyBackBag);
				if ( eBagID == InvBagIDs_Buyback )
				{
					TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AddItemsToAuction failed, selling items in buyback bag not allowed");
				}

				pRemovedItem = inv_RemoveItemByID(ATR_PASS_ARGS, pEnt, pSlot->slot.pItem->id, pSlot->slot.pItem->count, 0, pReason, pExtract);
				if(!pRemovedItem)
				{
					TRANSACTION_RETURN_LOG_FAILURE("auction_tr_AddItemsToAuction failed, item remove failed");// %s", pEnt->debugName );
				}
			}
			StructDestroySafe(parse_Item, &pRemovedItem);
		}
 	}
 	
	if(pAuctionLot->uPostingFee > 0 && bChargePostingFee)
	{
		if(!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, Auction_GetCurrencyNumeric(), -1 * (pAuctionLot->uPostingFee), pReason))
		{
			TRANSACTION_APPEND_LOG_FAILURE("auction_tr_AddItemsToAuction failed, unable to subtract posting fee resources.");
			return false;
		}		
	}
 	
	TRANSACTION_RETURN_LOG_SUCCESS("Items removed.");
}


static void LotAddItem_CB(TransactionReturnVal *pReturn, AuctionCallbackStructure *pData) 
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->eRef);
	if (pEnt || pData->eContainerID)
	{
		if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) 
		{
			ClientCmd_gclAuctionPostComplete(entForClientCmd(pData->eContainerID,pEnt), entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		}
		else
		{
			pEnt = entForClientCmd(pData->eContainerID,pEnt);
			gslAuction_GetLotsOwnedByPlayer(pEnt);
			ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPosted"), true);
		}
	}

	Auction_FreeCallbackStructure(pData);
}

static void LotAddContainerItem_CB(TransactionReturnVal *pReturn, AuctionCallbackStructure *pData) 
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->eRef);
	if (pEnt || pData->eContainerID)
	{
		if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS) 
		{
			ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		}
		else
		{
			if(pData && pData->pLot)
			if(gslAuction_GetContainerItemPetsFromLot(pData->pLot, NULL) < eaSize(&pData->pLot->ppItemsV2))
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
				ItemChangeReason reason = {0};

				inv_FillItemChangeReason(&reason, pEnt, "Auction:Add", pData->pLot->description);

				AutoTrans_auction_tr_AddItemsToAuction(pData->pContainerItemCallback, GLOBALTYPE_GAMESERVER, 
					entGetType(pEnt), entGetContainerID(pEnt), 
					GLOBALTYPE_AUCTIONLOT, pData->lotID, 
					GLOBALTYPE_ENTITYSAVEDPET, &(U32*)pData->auctionPetContainerIDs, 
					ALS_Open, false, &reason, pExtract);
			} else {
				gslAuction_GetLotsOwnedByPlayer(pEnt);
				ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPosted"), true);
			}
		}
	}

	Auction_FreeCallbackStructure(pData);
}

static void CreateLotFromDescription_CB(TransactionReturnVal *pReturn, AuctionCallbackStructure *pData)
{
	Entity *pEnt = entFromEntityRefAnyPartition(pData->eRef);
	U32 newLotID;
	
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		newLotID = atoi(pReturn->pBaseReturnVals->returnString);
		if (pData && pData->pLot && eaSize(&pData->pLot->ppItemsV2))
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			U32* eaPetEntIDs = NULL;
			int iContainerItems = gslAuction_GetContainerItemPetsFromLot(pData->pLot, &eaPetEntIDs);
			ItemChangeReason reason = {0};

			pData->lotID = newLotID;

			inv_FillItemChangeReason(&reason, pEnt, "Auction:CreateLot", pData->pLot->description);

			if(iContainerItems > 0)
			{
				pData->pContainerItemCallback = LoggedTransactions_CreateManagedReturnValEntInfo("Auction", GLOBALTYPE_ENTITYPLAYER, pData->eContainerID, NULL, NULL, LotAddItem_CB,pData);

				AutoTrans_auction_tr_AddContainerItemsToAuction(LoggedTransactions_CreateManagedReturnValEntInfo("Auction", GLOBALTYPE_ENTITYPLAYER, pData->eContainerID, NULL, NULL, LotAddContainerItem_CB,pData), GetAppGlobalType(), 
					GLOBALTYPE_ENTITYPLAYER, pData->eContainerID, 
					GLOBALTYPE_ENTITYSAVEDPET, &eaPetEntIDs, 
					GLOBALTYPE_AUCTIONLOT, pData->lotID, 
					ALS_Open, &reason);				
			}

			if(iContainerItems < eaSize(&pData->pLot->ppItemsV2)) {
				AutoTrans_auction_tr_AddItemsToAuction(LoggedTransactions_CreateManagedReturnValEntInfo("Auction", GLOBALTYPE_ENTITYPLAYER, pData->eContainerID, NULL, NULL, LotAddItem_CB,pData), GetAppGlobalType(), 
					GLOBALTYPE_ENTITYPLAYER, pData->eContainerID, 
					GLOBALTYPE_AUCTIONLOT, pData->lotID, 
					GLOBALTYPE_ENTITYSAVEDPET, &(U32*)pData->auctionPetContainerIDs, 
					ALS_Open, true, &reason, pExtract);
			}

			eaiDestroy(&eaPetEntIDs);
			return;
		}
	}
	else
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
	}

	Auction_FreeCallbackStructure(pData);
}

void AuctionSlot_CacheSearchableAttribMagnitudes(Entity* pEnt, NOCONST(AuctionSlot)* pSlot)
{
	NOCONST(Item)* pItem = SAFE_MEMBER(pSlot, slot.pItem);
	Item* pConstItem = CONTAINER_RECONST(Item, pItem);
	ItemDef* pDef = SAFE_GET_REF(pItem, hItem);
	int iPower, iNumPowers = item_trh_GetNumItemPowerDefs(pItem, true);
	CharacterClass* pClass = SAFE_GET_REF2(pEnt, pChar, hClass);

	if (pItem)
	{
		for(iPower = 0; iPower < iNumPowers; ++iPower)
		{
			PowerDef *pPowerDef = item_GetPowerDef(pConstItem, iPower);

			if(pPowerDef && pPowerDef->eType==kPowerType_Innate)
			{	// calculate the attribs for this power
				F32 fItemPowerScale = item_GetItemPowerScale(pConstItem, iPower);

				FOR_EACH_IN_EARRAY_FORWARDS(pPowerDef->ppOrderedMods, AttribModDef, pModDef)
				{
					if(!pModDef->bDerivedInternally)
					{
						if (pModDef->offAspect == kAttribAspect_BasicAbs && eaiFind(&gAuctionConfig.eaiSearchableAttribs, pModDef->offAttrib) >= 0)
						{
							int iAttr = eaIndexedFindUsingInt(&pSlot->ppSearchableAttribMagnitudes, pModDef->offAttrib);
							F32 fMag = mod_GetInnateMagnitude(entGetPartitionIdx(pEnt), pModDef, pEnt->pChar, pClass, pDef->iLevel, 1);
							fMag *= fItemPowerScale;
							if (iAttr > 0)
								pSlot->ppSearchableAttribMagnitudes[iAttr]->fMag += fMag;
							else
							{
								NOCONST(AuctionLotSearchableStat)* pStat = StructCreateNoConst(parse_AuctionLotSearchableStat);
								pStat->eType = pModDef->offAttrib;
								pStat->fMag = fMag;
								eaIndexedPushUsingIntIfPossible(&pSlot->ppSearchableAttribMagnitudes, pModDef->offAttrib, pStat);
							}
						}
					}
				}
				FOR_EACH_END
			}

		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_CATEGORY(Auction, Inventory);
void gslAuction_CreateLotFromDescription(SA_PARAM_NN_VALID Entity* pEnt, AuctionLot *pCopiedLot, U32 iAuctionDuration)
{
	NOCONST(AuctionLot) *pNewLot = CONTAINER_NOCONST(AuctionLot, pCopiedLot);
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	bool bPlayerIsAtAuctionHouse = (pDialog && pDialog->screenType == ContactScreenType_Market);
	int i;
	U32 iStartingBid = SAFE_MEMBER2(pNewLot, pBiddingInfo, iMinimumBid);
	U32 uNumPostedAuctions = pEnt->pPlayer->uLastLotsPostedByPlayer;
	if (gAuctionConfig.bExpiredAuctionsCountTowardMaximum)
		uNumPostedAuctions += EmailV3_GetNumNPCMessagesByType(pEnt, kNPCEmailType_ExpiredAuction, true);
	
	// Validate the lot
	if (gslAuction_AuctionsDisabled() || !pEnt || pEnt->myEntityType != GLOBALTYPE_ENTITYPLAYER)
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		return;
	}
	
	if( gamePermission_Enabled() && !GamePermission_EntHasToken(pEnt, GAME_PERMISSION_CAN_USE_MARKET))
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.NoTrialAuctions"), false);
		return;
	}
	
	if (!CanAccessAuctions(pEnt) && entGetAccessLevel(pEnt) < 9)
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		return;
	}
	
	if (!pCopiedLot || eaSize(&pCopiedLot->ppItemsV2) == 0)
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		return;
	}
	
	if (gAuctionConfig.bBiddingEnabled)
	{
		if ((iStartingBid == 0 && pCopiedLot->pBiddingInfo) || // Bidding Info sub struct should only be created when there is a starting bid
			(pCopiedLot->price == 0 && iStartingBid == 0) ||
			(iStartingBid && pCopiedLot->price && pCopiedLot->price <= iStartingBid))
		{
			ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		}
	}
	else if (!gAuctionConfig.bBiddingEnabled && pCopiedLot->price < 1)
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		return;
	}

	if (eaSize(&pCopiedLot->ppItemsV2) != ea32Size(&pCopiedLot->auctionPetContainerIDs))
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		return;
	}
	
	if(uNumPostedAuctions >= Auction_GetMaximumPostedAuctions(pEnt))
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
		return;
	}

	if(pCopiedLot->price > Auction_MaximumSellPrice())
	{
		ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFailPriceToHigh"), false);
		return;
	}

	for (i = 0; i < eaSize(&pCopiedLot->ppItemsV2); i++)
	{
		NOCONST(AuctionSlot) *pAuctionItem = CONTAINER_NOCONST(AuctionSlot, pCopiedLot->ppItemsV2[i]);
		ContainerID containerID = pCopiedLot->auctionPetContainerIDs[i];
		U64 uItemID = pAuctionItem->slot.pItem->id;
		int iCount = pAuctionItem->slot.pItem->count;
		Entity *pPet = NULL;
		ItemDef *pAuctionItemDef = GET_REF(pAuctionItem->slot.pItem->hItem);

		InvBagIDs eBagID = InvBagIDs_None;
		int iSlotIdx = 0;
		Item *pItem;
		InventoryBag *pBag;
		InventorySlot *pSlot;

		if (gAuctionConfig.bPersistItemNameWithAuction && pAuctionItemDef)
		{
			pAuctionItem->pchItemName = allocAddString(pAuctionItemDef->pchName);
		}

		if (!pAuctionItemDef || resNamespaceIsUGC(pAuctionItemDef->pchName))
		{
			//Can't post items without a def or UGC items.
			ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
			return;
		}

		if (containerID)
		{
			pPet = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, GLOBALTYPE_ENTITYSAVEDPET, containerID);

			if (!pPet)
			{
				ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
				return;
			}

			if (!Entity_CanModifyPuppet(pEnt, pPet))
			{
				ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
				return;
			}
		}

		if (pPet)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			pItem = inv_GetItemAndSlotsByID(pPet, uItemID, &eBagID, &iSlotIdx);
			pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pPet), eBagID, pExtract);
		}
		else if(pAuctionItemDef && pAuctionItemDef->eType == kItemType_Container)
		{
			Entity* pPetEnt = NULL;
			char* estrError = NULL;

			pItem = CONTAINER_RECONST(Item, pAuctionItem->slot.pItem);
			pBag = NULL;
			pPetEnt = pItem && pItem->pSpecialProps && pItem->pSpecialProps->pContainerInfo ? GET_REF(pItem->pSpecialProps->pContainerInfo->hSavedPet) : NULL;

			estrCreate(&estrError);

			if(pPetEnt && !Entity_CanInitiatePetTransfer(pEnt, pPetEnt, &estrError))
			{
				ClientCmd_NotifySend(pEnt, kNotifyType_AuctionFailed, estrError, NULL, NULL);
				estrDestroy(&estrError);
				return;
			}
			estrDestroy(&estrError);
		} 
		else 
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			pItem = inv_GetItemAndSlotsByID(pEnt, uItemID, &eBagID, &iSlotIdx);
			pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), eBagID, pExtract);

			if (!item_ItemMoveValidTemporaryPuppetCheck(pEnt, pBag))
			{
				// Don't allow the player to create an auction lot from an item on a temporary puppet
				ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
			}
		}

		if ( ( pBag != NULL ) && ( pBag->BagID == InvBagIDs_Buyback ) )
		{
			// don't allow the player to post an item from their buyback bag
			ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
			return;
		}

		pSlot = pBag ? inv_GetSlotPtr(pBag, iSlotIdx) : NULL;

		if (pItem 
			&& (pSlot || (pAuctionItemDef && pAuctionItemDef->eType == kItemType_Container)) 
			&& !(pItem->flags & kItemFlag_Bound)
			&& !(pItem->flags & kItemFlag_BoundToAccount)
			&& !item_IsUnidentified(pItem))
		{
			ItemDef *pItemDef = GET_REF(pItem->hItem);
			
			if(pSlot) {
				iCount = min(pItem->count, iCount);
			}
			if (pItemDef->eType != kItemType_Numeric &&
				pItemDef->eType != kItemType_Token &&
				pItemDef->eType != kItemType_Title &&
				pItemDef->eType != kItemType_Lore &&
				pItemDef->eType != kItemType_ItemValue &&
				iCount > 0)
			{
				int localeID;
				const ItemSortTypeCategory *pItemSortTypeCategory = item_GetSortTypeCategoryBySortTypeID(pItemDef->iSortID);
				pAuctionItem->iItemSortType = pItemDef->iSortID;

				if (pItemSortTypeCategory)
				{
					pAuctionItem->pchItemSortTypeCategoryName = allocAddString(pItemSortTypeCategory->pchName);
				}

				if ( pItemDef->pRestriction != NULL ) 
				{
					pAuctionItem->eUICategory = pItemDef->pRestriction->eUICategory;
				}
				for (localeID = 0; localeID < locGetMaxLocaleCount(); localeID++)
				{
					int langID = locGetLanguage(localeID);
					if (locIsImplemented(localeID))
					{						
						eaSet(&pAuctionItem->ppTranslatedNames, (char *)allocAddString(item_GetNameLang(pItem, langID, pEnt)), langID);
						// This was slower than just searching the full string and does NOT do the right thing in other locales so the data in the DB was invalid -BZ
						// item_GetTokensLang(pItem, langID, &pAuctionItem->ppTranslatedTokens); 
					}
					else
					{
						eaSet(&pAuctionItem->ppTranslatedNames, (char *)allocAddString(""), langID);
					}

				}

				pAuctionItem->uiNumGemSlots = eaSize(&pItemDef->ppItemGemSlots);
				pAuctionItem->uiCachedMinLevel = item_GetMinLevel(pItem);

				AuctionSlot_CacheSearchableAttribMagnitudes(pEnt, pAuctionItem);

				estrClear(&pItem->pchDisplayName);
			}
			else
			{
				ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
				return;
			}
		}
		else
		{
			ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
			return;
		}
	}

	{
		// Seems to be good
		AuctionCallbackStructure *pStruct = calloc(sizeof(AuctionCallbackStructure),1);		

		pStruct->eRef = entGetRef(pEnt);
		pStruct->pLot = StructClone(parse_AuctionLot, pCopiedLot);
		pStruct->auctionPetContainerIDs = NULL;
		for (i = 0; i < ea32Size(&pCopiedLot->auctionPetContainerIDs); ++i)
		{
			if (pCopiedLot->auctionPetContainerIDs[i])
			{
				Entity *e = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, GLOBALTYPE_ENTITYSAVEDPET, pCopiedLot->auctionPetContainerIDs[i]);
				if (e)
				{
					ea32PushUnique(&pStruct->auctionPetContainerIDs, pCopiedLot->auctionPetContainerIDs[i]);
				}
				else
				{
					Auction_FreeCallbackStructure(pStruct);
					ClientCmd_gclAuctionPostComplete(pEnt, entTranslateMessageKey(pEnt, "Auction.AuctionPostedFail"), false);
					return;
				}
			}
		}

		pNewLot->ownerID = entGetContainerID(pEnt);
		pNewLot->OwnerAccountID = entGetAccountID(pEnt);

		if (gAuctionConfig.bPersistOwnerNameAndHandleWithAuction)
		{
			if (pNewLot->pOptionalData == NULL)
			{
				pNewLot->pOptionalData = StructCreateNoConst(parse_AuctionLotOptionalData);
			}

			// Copy the character name and handle
			strcpy_s(pNewLot->pOptionalData->pchOwnerName, sizeof(pNewLot->pOptionalData->pchOwnerName), pEnt->pSaved->savedName);
			strcpy_s(pNewLot->pOptionalData->pchOwnerHandle, sizeof(pNewLot->pOptionalData->pchOwnerHandle), pEnt->pPlayer->publicAccountName);
		}

		if (pNewLot->pBiddingInfo)
		{
			pNewLot->pBiddingInfo->iOwnerAccountID = entGetAccountID(pEnt);
		}
		if(pEnt->pPlayer)
		{
			// save account ID of the seller in case we need it
			pNewLot->recipientID = pEnt->pPlayer->accountID;
			pNewLot->iLangID = pEnt->pPlayer->langID;
		}

		AuctionLotInit(pNewLot);
		pNewLot->state = ALS_New;
		pNewLot->creationTime = timeSecondsSince2000();
		pNewLot->modifiedTime = 0;
		pNewLot->uExpireTime = pNewLot->creationTime + Auction_GetMaximumDuration(pEnt);
		if (iAuctionDuration > 0)
		{
			if (gAuctionConfig.bAllowCustomAuctionDurations)
			{
				// Make sure the auction duration is valid
				if (Auction_GetDurationOption(iAuctionDuration))
				{
					U32 iNewExpireTime = pNewLot->creationTime + iAuctionDuration;

					// Make sure the custom defined expiration time is no longer than maximum allowed
					if (iNewExpireTime < pNewLot->uExpireTime)
					{
						pNewLot->uExpireTime = iNewExpireTime;
					}
				}
				else
				{
					Errorf("An invalid duration has been passed to the gslAuction_CreateLotFromDescription function");
				}
			}
			else
			{
				Errorf("You can only specify custom auction durations if the AllowCustomAuctionDurations flag is set in AuctionConfig.def");
			}
		}

		pNewLot->uPostingFee = Auction_GetPostingFee(pEnt, pNewLot->price, iStartingBid, iAuctionDuration, bPlayerIsAtAuctionHouse);
		pNewLot->uSoldFee = Auction_GetSoldFee(pEnt, pNewLot->price, pNewLot->uPostingFee, bPlayerIsAtAuctionHouse);

		pStruct->eContainerID = entGetContainerID(pEnt);

		objRequestContainerCreate(LoggedTransactions_CreateManagedReturnValEnt("Auction", pEnt, CreateLotFromDescription_CB, pStruct), 
			GLOBALTYPE_AUCTIONLOT, (AuctionLot *)pNewLot, GLOBALTYPE_AUCTIONSERVER, 0); // Give ownership to Auction Server
	}
	
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(1) ACMD_CATEGORY(Auction, Inventory);
void gslAuction_FindLostLots(SA_PARAM_NN_VALID Entity* pEnt)
{
	if(pEnt->pPlayer)
	{
		// Get and forward all mail lots to the auction server for this character	
		RemoteCommand_Auction_ForwardMailAuctionLotsToChat(/* objCreateManagedReturnVal(SyncNPCEMail_CB, pEntity)  NULL, */
			GLOBALTYPE_AUCTIONSERVER, 0, pEnt->myContainerID, pEnt->pPlayer->accountID);
	}
}


static int siTestAuctionsConcurrent = 0;
static int siTestAuctionsToCreate = 0;
int giTestAuctionsPerFrame = 50;
AUTO_CMD_INT(giTestAuctionsPerFrame, AuctionRaidsPerFrame);

static void CreateTestLotFromDescription_CB(TransactionReturnVal *pReturn, AuctionCallbackStructure *pData)
{
	--siTestAuctionsConcurrent;

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		//printf("Created Lot\n");
	}
	else
	{
		printf("Failed to Create Lot\n");
	}
}

static void AuctionRaider_CB(TimedCallback *callback, F32 timeSinceLastCallback, void *userData)
{
	NOCONST(AuctionLot) *pNewLot = StructCreateNoConst(parse_AuctionLot);
	NOCONST(AuctionSlot) *pSlot;
	int i;
	Item *pItem;
	RewardContext *pLocalContext = NULL;
	static U32 iSeed = 0;
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)((intptr_t)userData));

	if(!pEnt)
	{
		return;
	}

	if(!iSeed) {
		iSeed = randomU32();
	}

	for(i = 0; i < giTestAuctionsPerFrame; i++)
	{
		if(!siTestAuctionsToCreate)
		{
			break;
		}
		
		if(siTestAuctionsConcurrent >= 32768)
		{
			break;
		}
		
		StructResetNoConst(parse_AuctionLot, pNewLot);
		
		pLocalContext = Reward_CreateOrResetRewardContext(NULL);

		if (pLocalContext)
		{
			pLocalContext->KillerLevel = randomIntRange(1,39);
			pLocalContext->RewardLevel = pLocalContext->KillerLevel;
			
			iSeed = randomIntSeeded(&iSeed, RandType_LCG);
			pItem = algoitem_generate_quality(entGetPartitionIdx(pEnt), pLocalContext, randomIntRange(0,4), &iSeed);
			
			if (pItem)
			{
				int localeID;
				ItemDef *pItemDef = GET_REF(pItem->hItem);
				if (pItemDef)
				{
					++siTestAuctionsConcurrent;
					--siTestAuctionsToCreate;

					AuctionLotInit(pNewLot);
					pNewLot->ownerID = entGetContainerID(pEnt);
					pNewLot->OwnerAccountID = entGetAccountID(pEnt);
					pNewLot->state = ALS_Open;
					pNewLot->creationTime = timeSecondsSince2000();
					pNewLot->modifiedTime = 0;
					pNewLot->uExpireTime = Auction_GetExpireTime(pEnt);
					pNewLot->price = randomIntRange(1, 100000);

					pSlot = StructCreateNoConst(parse_AuctionSlot);
					pSlot->slot.pItem = CONTAINER_NOCONST(Item, pItem);

					item_trh_FixupPowers(CONTAINER_NOCONST(Item, pItem));

					pSlot->iItemSortType = pItemDef->iSortID;
					if ( pItemDef->pRestriction != NULL ) 
					{
						pSlot->eUICategory = pItemDef->pRestriction->eUICategory;
					}
					for (localeID = 0; localeID < locGetMaxLocaleCount(); localeID++)
					{
						int langID = locGetLanguage(localeID);
						if (locIsImplemented(localeID))
						{					
							eaSet(&pSlot->ppTranslatedNames, (char *)allocAddString(item_GetNameLang(pItem, langID, pEnt)), langID);
							//item_GetTokensLang(pItem, langID, &pSlot->ppTranslatedTokens);
						}
						else
						{
							eaSet(&pSlot->ppTranslatedNames, (char *)allocAddString(""), langID);
						}
					}
					estrClear(&pItem->pchDisplayName);

					eaPush(&pNewLot->ppItemsV2, pSlot);

					objRequestContainerCreate(LoggedTransactions_CreateManagedReturnValEnt("Auction", pEnt, CreateTestLotFromDescription_CB, NULL), 
						GLOBALTYPE_AUCTIONLOT, (AuctionLot *)pNewLot, GLOBALTYPE_AUCTIONSERVER, 0); // Give ownership to Auction Server
				}
			}

			// Destroy the local context
			StructDestroy(parse_RewardContext, pLocalContext);
		}
	}
	StructDestroyNoConst(parse_AuctionLot, pNewLot);
	if(siTestAuctionsToCreate)
	{
		TimedCallback_Run(AuctionRaider_CB, userData, 0.0f);
	}
}

// Add 1000 algo items

AUTO_COMMAND;
void AuctionRaider(SA_PARAM_NN_VALID Entity* pEnt, int iAuctionsToAdd)
{
	if(!siTestAuctionsToCreate)
	{
		siTestAuctionsToCreate = iAuctionsToAdd;
		TimedCallback_Run(AuctionRaider_CB, (void *)((intptr_t)entGetRef(pEnt)), 0.0f);
	}
	else
	{
		siTestAuctionsToCreate += iAuctionsToAdd;
	}
}

static const char* s_pchTableName = NULL;

static void AuctionRaiderRewardTable_CB(TimedCallback *callback, F32 timeSinceLastCallback, void *userData)
{
	NOCONST(AuctionLot) *pNewLot = StructCreateNoConst(parse_AuctionLot);
	NOCONST(AuctionSlot) *pSlot;
	int i, j, k;
	RewardContext *pLocalContext = NULL;
	RewardTable* pTable = RefSystem_ReferentFromString(g_hRewardTableDict, s_pchTableName);
	InventoryBag** eaBags = NULL;
	static U32 iSeed = 0;
	Entity *pEnt = entFromEntityRefAnyPartition((EntityRef)((intptr_t)userData));

	if(!pEnt)
	{
		return;
	}

	if(!iSeed) {
		iSeed = randomU32();
	}

	for(i = 0; i < giTestAuctionsPerFrame; i++)
	{
		if(!siTestAuctionsToCreate)
		{
			break;
		}

		if(siTestAuctionsConcurrent >= 32768)
		{
			break;
		}

		StructResetNoConst(parse_AuctionLot, pNewLot);

		pLocalContext = Reward_CreateOrResetRewardContext(NULL);

		if (pLocalContext)
		{
			pLocalContext->KillerLevel = randomIntRange(1,NUM_PLAYER_LEVELS);
			pLocalContext->RewardLevel = pLocalContext->KillerLevel;

			iSeed = randomIntSeeded(&iSeed, RandType_LCG);
			reward_generateEx(entGetPartitionIdx(pEnt), pEnt, pLocalContext, pTable, &eaBags, NULL, &iSeed, false, NULL, NULL);

			for (j = 0; j < eaSize(&eaBags); j++)
			{
				for (k = 0; k < eaSize(&eaBags[j]->ppIndexedInventorySlots); k++)
				{
					if (eaBags[j]->ppIndexedInventorySlots[0]->pItem)
					{
						Item* pItem = StructClone(parse_Item, eaBags[j]->ppIndexedInventorySlots[0]->pItem);
						int localeID;
						ItemDef *pItemDef = GET_REF(pItem->hItem);
						if (pItemDef)
						{
							const ItemSortTypeCategory *pItemSortTypeCategory = item_GetSortTypeCategoryBySortTypeID(pItemDef->iSortID);

							++siTestAuctionsConcurrent;
							--siTestAuctionsToCreate;

							AuctionLotInit(pNewLot);
							pNewLot->ownerID = entGetContainerID(pEnt);
							pNewLot->OwnerAccountID = entGetAccountID(pEnt);
							pNewLot->state = ALS_Open;
							pNewLot->creationTime = timeSecondsSince2000();
							pNewLot->modifiedTime = 0;
							pNewLot->uExpireTime = Auction_GetExpireTime(pEnt);
							pNewLot->price = randomIntRange(1, 100000);

							pSlot = StructCreateNoConst(parse_AuctionSlot);
							pSlot->slot.pItem = CONTAINER_NOCONST(Item, pItem);

							item_trh_FixupPowers(CONTAINER_NOCONST(Item, pItem));

							pSlot->iItemSortType = pItemDef->iSortID;

							if (pItemSortTypeCategory)
								pSlot->pchItemSortTypeCategoryName = allocAddString(pItemSortTypeCategory->pchName);

							if ( pItemDef->pRestriction != NULL ) 
							{
								pSlot->eUICategory = pItemDef->pRestriction->eUICategory;
							}
							for (localeID = 0; localeID < locGetMaxLocaleCount(); localeID++)
							{
								int langID = locGetLanguage(localeID);
								if (locIsImplemented(localeID))
								{					
									eaSet(&pSlot->ppTranslatedNames, (char *)allocAddString(item_GetNameLang(pItem, langID, pEnt)), langID);
									//item_GetTokensLang(pItem, langID, &pSlot->ppTranslatedTokens);
								}
								else
								{
									eaSet(&pSlot->ppTranslatedNames, (char *)allocAddString(""), langID);
								}
							}
							estrClear(&pItem->pchDisplayName);

							eaPush(&pNewLot->ppItemsV2, pSlot);

							objRequestContainerCreate(LoggedTransactions_CreateManagedReturnValEnt("Auction", pEnt, CreateTestLotFromDescription_CB, NULL), 
								GLOBALTYPE_AUCTIONLOT, (AuctionLot *)pNewLot, GLOBALTYPE_AUCTIONSERVER, 0); // Give ownership to Auction Server
						}
					}
				}
			}

			eaDestroyStruct(&eaBags, parse_InventoryBag);


			// Destroy the local context
			StructDestroy(parse_RewardContext, pLocalContext);
		}
	}
	StructDestroyNoConst(parse_AuctionLot, pNewLot);
	if(siTestAuctionsToCreate)
	{
		TimedCallback_Run(AuctionRaiderRewardTable_CB, userData, 0.0f);
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void AuctionRaiderRewardTable(SA_PARAM_NN_VALID Entity* pEnt, const char* pchTableName, int iAuctionsToAdd)
{
	RewardTable* pTable = RefSystem_ReferentFromString(g_hRewardTableDict, pchTableName);
	if(pTable && !siTestAuctionsToCreate)
	{
		siTestAuctionsToCreate = iAuctionsToAdd;
		s_pchTableName = pTable->pchName;
		TimedCallback_Run(AuctionRaiderRewardTable_CB, (void *)((intptr_t)entGetRef(pEnt)), 0.0f);
	}
	else
	{
		Errorf("Wait for the current batch of auction to finish before queueing more.");
	}
}

static AuctionSearchRequest *debugRequest = NULL;
static TimingHistory *searchHistory = NULL;
static int searchesPerSecond = 1;
AUTO_CMD_INT(searchesPerSecond, AuctionRaiderSearchesPerSecond);

AUTO_COMMAND;
void AuctionRaiderResults(Entity *pEnt)
{
	gslSendPrintf(pEnt, "Most searches in a second: %d\nAverage searches per second: %0.2f\n", timingHistoryMostInInterval(searchHistory, 1.0), timingHistoryAverageInInterval(searchHistory, 1.0));
}

void LotsOfSearches_CB(TransactionReturnVal *returnVal, void *userData)
{
	timingHistoryPush(searchHistory);
}

void LotsOfSearchesDispatch()
{
	RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(LotsOfSearches_CB, NULL), GLOBALTYPE_AUCTIONSERVER, 0, debugRequest);

	if(debugRequest->stringSearch)
	{
	/*	++debugRequest->stringSearch[0];
		if(debugRequest->stringSearch[0] > 'z')
		{
			debugRequest->stringSearch[0] = 'a';
		}*/
	}
	else if(debugRequest->itemQuality > -1)
	{
		++debugRequest->itemQuality;
		if(debugRequest->itemQuality > 4)
		{
			debugRequest->itemQuality = 0;
		}
	}
	else if(debugRequest->sortType > -1)
	{
		/*++debugRequest->sortType;*/
	}
	else if(debugRequest->minLevel > 0)
	{
		++debugRequest->maxLevel;

		if(debugRequest->maxLevel > 40)
		{
			++debugRequest->minLevel;
			
			if(debugRequest->minLevel > 40)
			{
				debugRequest->minLevel = 1;
			}

			debugRequest->maxLevel = debugRequest->minLevel;
		}
	}
}

void LotsOfSearchesDebug(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i = 0;

	if(!debugRequest)
	{
		return;
	}

	for(i = 0; i < searchesPerSecond; ++i)
	{
		LotsOfSearchesDispatch();
	}

	TimedCallback_Run(LotsOfSearchesDebug, NULL, 1.0f);
}

AUTO_COMMAND;
void AuctionRaiderNameSearch(Entity *pEnt, char *name)
{
	if(!debugRequest)
	{
		debugRequest = StructCreate(parse_AuctionSearchRequest);
		searchHistory = timingHistoryCreate(50000);
	}
	else
	{
		StructReset(parse_AuctionSearchRequest, debugRequest);
	}

	debugRequest->stringSearch = strdup(name);
	debugRequest->itemQuality = -1;
	debugRequest->numResults = MAX_SEARCH_RESULTS;
	debugRequest->stringLanguage = entGetLanguage(pEnt);
	debugRequest->bExcludeOwner = true;

	TimedCallback_Run(LotsOfSearchesDebug, NULL, 0.0f);
}

AUTO_COMMAND;
void AuctionRaiderQualitySearch(Entity *pEnt)
{
	if(!debugRequest)
	{
		debugRequest = StructCreate(parse_AuctionSearchRequest);
		searchHistory = timingHistoryCreate(50000);
	}
	else
	{
		StructReset(parse_AuctionSearchRequest, debugRequest);
	}

	debugRequest->itemQuality = 0;
	debugRequest->numResults = MAX_SEARCH_RESULTS;
	debugRequest->stringLanguage = entGetLanguage(pEnt);
	debugRequest->bExcludeOwner = true;

	TimedCallback_Run(LotsOfSearchesDebug, NULL, 0.0f);
}

//AUTO_COMMAND;
//void SearchAuctionSortTypeDebug(Entity *pEnt)
//{
//	if(!debugRequest)
//	{
//		debugRequest = StructCreate(parse_AuctionSearchRequest);
//		searchHistory = timingHistoryCreate(50000);
//	}
//	else
//	{
//		StructReset(parse_AuctionSearchRequest, debugRequest);
//	}
//
//	debugRequest->sortType = 1;
//	debugRequest->numResults = MAX_SEARCH_RESULTS;
//	debugRequest->stringLanguage = entGetLanguage(pEnt);
//	debugRequest->bExcludeOwner = true;
//
//	TimedCallback_Run(LotsOfSearchesDebug, NULL, 0.0f);
//}

AUTO_COMMAND;
void AuctionRaiderLevelSearch(Entity *pEnt)
{
	if(!debugRequest)
	{
		debugRequest = StructCreate(parse_AuctionSearchRequest);

		if(searchHistory)
		{
			timingHistoryDestroy(searchHistory);
		}

		searchHistory = timingHistoryCreate(50000);
	}
	else
	{
		StructReset(parse_AuctionSearchRequest, debugRequest);
	}

	debugRequest->minLevel = 1;
	debugRequest->maxLevel = 1;
	debugRequest->numResults = MAX_SEARCH_RESULTS;
	debugRequest->stringLanguage = entGetLanguage(pEnt);
	debugRequest->bExcludeOwner = true;

	TimedCallback_Run(LotsOfSearchesDebug, NULL, 0.0f);
}

AUTO_COMMAND;
void AuctionRaiderStop()
{
	StructDestroy(parse_AuctionSearchRequest, debugRequest);
	debugRequest = NULL;
}

AUTO_TRANS_HELPER;
bool auction_trh_SendOutbidMail(ATR_ARGS, ATH_ARG NOCONST(Entity)* pOldBidder, 
	ATH_ARG NOCONST(Entity)* pNewBidder, 
	ATH_ARG NOCONST(AuctionLot) *pAuctionLot, 
	U32 iOldBid)
{
	if(NONNULL(pOldBidder->pPlayer) && NONNULL(pNewBidder->pPlayer))
	{
		bool bReturnValue = true;
		char* estrItemName = NULL;
		ChatMailStruct mail = {0};
		char *estrBody = NULL;
		NOCONST(EmailV3Message)* pMessage = NULL;

		estrStackCreate(&estrItemName);

		langFormatMessageKey(pAuctionLot->pBiddingInfo->iBiddingPlayerLangID, &estrBody, "Auction_Outbid_Body", 
			STRFMT_STRING("Name", pNewBidder->pSaved->savedName), 
			STRFMT_STRING("Account", pNewBidder->pPlayer->publicAccountName),
			STRFMT_INT("NewBid", pAuctionLot->pBiddingInfo->iCurrentBid), 
			STRFMT_INT("OldBid", iOldBid), 
			STRFMT_END);

		estrAppend2(&estrBody, "\n\n");
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pAuctionLot->ppItemsV2, NOCONST(AuctionSlot), pAuctionSlot)
		{
			S32 iTranslatedNameCount = eaSize(&pAuctionSlot->ppTranslatedNames);
			estrClear(&estrItemName);
			if (iTranslatedNameCount > 0)
			{
				S32 iLangID = pAuctionLot->pBiddingInfo->iBiddingPlayerLangID;
				if (iLangID >= iTranslatedNameCount)
				{
					iLangID = 0;
				}
				estrCopy2(&estrItemName, pAuctionSlot->ppTranslatedNames[iLangID]);
			}
			else
			{
				estrCopy2(&estrItemName, "[Untranslated Item]");
			}

			estrConcatf(&estrBody, "%d %s\n", pAuctionSlot->slot.pItem->count, estrItemName);
		}
		FOR_EACH_END

		bReturnValue = EntityMail_trh_NPCAddMail(ATR_PASS_ARGS, pOldBidder,
			langTranslateMessageKeyDefault(pAuctionLot->pBiddingInfo->iBiddingPlayerLangID, "Auction_Outbid_From_Name", "[UNTRANSLATED]Auction House"),
			langTranslateMessageKeyDefault(pAuctionLot->pBiddingInfo->iBiddingPlayerLangID, "Auction_Outbid_Subject", "[UNTRANSLATED]Auction lot outbid."), 
			estrBody, 
			NULL,
			0,
			kNPCEmailType_Default);

		estrDestroy(&estrBody);
		estrDestroy(&estrItemName);

		return bReturnValue;
	}
	return false;
}

AUTO_TRANS_HELPER;
void auction_trh_AddAuctionToBidListOnPlayer(ATR_ARGS, 
	ATH_ARG NOCONST(Entity) *pBidder,
	ATH_ARG NOCONST(AuctionLot) *pAuctionLot)
{
	if (ISNULL(pBidder->pPlayer->pPlayerAuctionData))
	{
		pBidder->pPlayer->pPlayerAuctionData = StructCreateNoConst(parse_PlayerAuctionData);

		ea32Push(&pBidder->pPlayer->pPlayerAuctionData->eaiAuctionsBid, pAuctionLot->iContainerID);
	}
	else
	{
		S32 iFoundIndex = 0;
		if (!ea32SortedFindIntOrPlace(&pBidder->pPlayer->pPlayerAuctionData->eaiAuctionsBid, pAuctionLot->iContainerID, &iFoundIndex))
		{
			// Insert in the correct position to keep the array sorted
			ea32Insert(&pBidder->pPlayer->pPlayerAuctionData->eaiAuctionsBid, pAuctionLot->iContainerID, iFoundIndex);
		}
	}
}

AUTO_TRANS_HELPER;
bool auction_trh_BidOnAuction(ATR_ARGS, 
	ATH_ARG NOCONST(Entity) *pOldBidder,
	ATH_ARG NOCONST(Entity) *pNewBidder,
	ATH_ARG NOCONST(AuctionLot) *pAuctionLot, 
	U32 iBidValue,
	const ItemChangeReason *pReason)
{
	// Make sure there is a valid new bidder
	if (ISNULL(pAuctionLot->pBiddingInfo))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, pBiddingInfo is NULL.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_Unknown", kNotifyType_AuctionFailed);
		return false;
	}

	// Make sure there is a valid new bidder
	if (ISNULL(pNewBidder) || ISNULL(pNewBidder->pPlayer))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, new bidder is NULL.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_Unknown", kNotifyType_AuctionFailed);
		return false;
	}

	// Make sure the bidder is different than the auction owner
	if (pNewBidder->myContainerID == pAuctionLot->ownerID)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, new bidder is the auction owner.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_SameOwner", kNotifyType_AuctionFailed);
		return false;
	}

	// Make sure that the auction owner does not bid on this auction with a different character
	if (pNewBidder->pPlayer->accountID == pAuctionLot->pBiddingInfo->iOwnerAccountID)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, auction owner is bidding on their own auction with a different character.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_SameOwner", kNotifyType_AuctionFailed);
		return false;
	}

	// Make sure this auction is not closed to bidding
	if (!auction_trh_AcceptsBids(ATR_PASS_ARGS, pAuctionLot))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, auction is closed to bids.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_BiddingClosed", kNotifyType_AuctionFailed);
		return false;
	}

	// Make sure the bid amount is valid
	if (iBidValue < auction_trh_GetMinimumNextBidValue(ATR_PASS_ARGS, pAuctionLot))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, bid is too small.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_BidTooLow", kNotifyType_AuctionFailed);
		return false;
	}

	// Make sure the bidder has enough money
	if (inv_trh_GetNumericValue(ATR_PASS_ARGS, pNewBidder, Auction_GetCurrencyNumeric()) < (S32)iBidValue)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, bidder does not have enough resources.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_InsufficientFunds", kNotifyType_AuctionFailed);
		return false;
	}	

	if (pAuctionLot->state != ALS_Open)
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, auction is closed.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_Closed", kNotifyType_AuctionFailed);
		return false;
	}

	// Make sure the old bidder entity is still valid
	if ((NONNULL(pOldBidder) && pOldBidder->myContainerID != pAuctionLot->pBiddingInfo->iBiddingPlayerContainerID) ||
		(ISNULL(pOldBidder) && pAuctionLot->pBiddingInfo->iBiddingPlayerContainerID))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, old bidder passed to the function is no longer valid.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_OldBidderFailed", kNotifyType_AuctionFailed);
		return false;
	}

	// Give the escrow money back to the old bidder
	if (NONNULL(pOldBidder) && !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pOldBidder, false, Auction_GetCurrencyNumeric(), pAuctionLot->pBiddingInfo->iCurrentBid, pReason))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, old bidder's escrow money could not be refunded.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_OldBidderFailed", kNotifyType_AuctionFailed);
		return false;
	}

	// Get the escrow money from the new bidder
	if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pNewBidder, false, Auction_GetCurrencyNumeric(), -((S32)iBidValue), pReason))
	{
		TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, new bidder's escrow money could not be charged.");
		QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_Unknown", kNotifyType_AuctionFailed);
		return false;
	}

	{
		U32 iOldBid = pAuctionLot->pBiddingInfo->iCurrentBid;
		// Update the bidding information
		++pAuctionLot->pBiddingInfo->iNumBids;
		pAuctionLot->pBiddingInfo->iCurrentBid = iBidValue;
		pAuctionLot->pBiddingInfo->iBiddingPlayerContainerID = pNewBidder->myContainerID;
		pAuctionLot->pBiddingInfo->iBiddingPlayerAccountID = NONNULL(pNewBidder->pPlayer) ? pNewBidder->pPlayer->accountID : 0;
		pAuctionLot->pBiddingInfo->iBiddingPlayerLangID = pNewBidder->pPlayer->langID;

		// Send the old bidder an email to indicate that they are outbid by the new bidder
		if (NONNULL(pOldBidder) && pOldBidder->myContainerID != pNewBidder->myContainerID)
		{
			if (!auction_trh_SendOutbidMail(ATR_PASS_ARGS, pOldBidder, pNewBidder, pAuctionLot, iOldBid))
			{
				TRANSACTION_APPEND_LOG_FAILURE("auction_tr_BidOnAuction failed, outbid mail could not be send.");
				QueueRemoteCommand_notify_RemoteSendNotification(ATR_RESULT_FAIL, pNewBidder->myEntityType, pNewBidder->myContainerID, "AuctionServer_Error_OldBidderFailed", kNotifyType_AuctionFailed);
				return false;
			}
		}
	}

	// Store the auction lot ID in the player
	auction_trh_AddAuctionToBidListOnPlayer(ATR_PASS_ARGS, pNewBidder, pAuctionLot);

	return true;
}

AUTO_TRANSACTION
	ATR_LOCKS(pOldBidder, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, pInventoryV2.ppLiteBags[], .Pplayer.Pemailv2.Ilastusedid, .Pplayer.Pemailv2.Mail")
	ATR_LOCKS(pNewBidder, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.ppInventoryBags[], .Pplayer.Pplayerauctiondata, .Pplayer.Accountid, .Pplayer.Langid, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Psaved.Savedname, .Pplayer.Publicaccountname, pInventoryV2.ppLiteBags[]")
	ATR_LOCKS(pAuctionLot, ".Pbiddinginfo.Ibiddingplayeraccountid, .Ownerid, .Pbiddinginfo.Iowneraccountid, .State, .Pbiddinginfo.Ibiddingplayercontainerid, .Pbiddinginfo.Icurrentbid, .Pbiddinginfo.Inumbids, .Pbiddinginfo.Ibiddingplayerlangid, .Pbiddinginfo.Iminimumbid, .Uexpiretime, .Ppitemsv2, .Icontainerid");
enumTransactionOutcome auction_tr_BidOnAuction(ATR_ARGS, 
	NOCONST(Entity) *pOldBidder, // Old bidder, can be NULL
	NOCONST(Entity) *pNewBidder, // New bidder
	NOCONST(AuctionLot) *pAuctionLot,
	U32 iBidValue,
	const ItemChangeReason *pReason)
{
	if(!auction_trh_BidOnAuction(ATR_PASS_ARGS, pOldBidder, pNewBidder, pAuctionLot, iBidValue, pReason))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Auction bidding has failed.");
	}
	TRANSACTION_RETURN_LOG_SUCCESS("Auction bidding was successful.");
}

// Delete lots in expire order, used for testing
AUTO_COMMAND ACMD_CATEGORY(Auction) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void AuctionDeleteLots(U32 uNumToDelete)
{
	if(!isProductionMode())
	{
		RemoteCommand_aslAuctionDeleteLots(GLOBALTYPE_AUCTIONSERVER, 0, uNumToDelete);
	}
}

typedef struct CheckPriceHistoryCBData
{
	ContainerID entID;
	const char* pchItemName;	AST(POOL_STRING)
} CheckPriceHistoryCBData;

// Fake a sale to test the price-history feature
AUTO_COMMAND ACMD_CATEGORY(Auction) ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD;
void AuctionTestRecordPrice(const char* pchName, S32 price)
{
	if(!isProductionMode())
	{
		RemoteCommand_Auction_RecordSalePriceForItem(GLOBALTYPE_AUCTIONSERVER, 0, pchName, price);
	}
}

void gslAuction_CheckPriceHistoryForItem_CB(TransactionReturnVal *pReturn, void *pData)
{
	int iPrice = 0;
	if (pReturn == NULL || RemoteCommandCheck_aslAuction_CheckPriceHistoryForItem(pReturn, &iPrice) == TRANSACTION_OUTCOME_SUCCESS) 
	{	
		CheckPriceHistoryCBData* pCBData = (CheckPriceHistoryCBData*)pData;
		Entity *pEnt = (Entity*)entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,pCBData->entID);
		
		ClientCmd_gclAuctionUI_ReceivePriceHistory(pEnt, pCBData->pchItemName, iPrice);
	}
	if (pData)
		free(pData);
}

// Query the AuctionServer for an item's average recent sale price.
AUTO_COMMAND ACMD_CATEGORY(Auction) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslAuction_CheckPriceHistoryForItem(Entity* pEnt, const char* pchName)
{
	CheckPriceHistoryCBData* pData = calloc(1, sizeof(CheckPriceHistoryCBData));
	pData->entID = pEnt->myContainerID;
	pData->pchItemName = allocFindString(pchName);
	RemoteCommand_aslAuction_CheckPriceHistoryForItem(objCreateManagedReturnVal(gslAuction_CheckPriceHistoryForItem_CB, pData), GLOBALTYPE_AUCTIONSERVER, 0, pchName);
}
#include "AutoGen/gslAuction_c_ast.c"
