/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/
#include "net.h"

#include "textparserJSON.h"

#include "AuctionLot.h"
#include "AuctionLot_h_ast.h"
#include "tradeCommon.h"
#include "tradeCommon_h_ast.h"

#include "gslGatewaySession.h"
#include "gslGatewayContainerMapping.h"

#include "gslGatewayAuction.h"

#include "gslGatewayAuction_c_ast.h"

#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "NotifyCommon.h"
#include "GameStringFormat.h"
#include "Entity.h"
#include "StringCache.h"
#include "Player.h"
#include "gslAuction.h"
#include "AuctionLot_Transact.h"

#define MAX_SEARCH_RESULTS 100


extern void gslGateway_AuctionRequestSearch(Entity *pEnt, ParamsAuctionSearch *pParams);

/***************************************************************************/
AUTO_STRUCT;
typedef struct ParamsAuctionSearch
{
	int iMinLevel;         AST(NAME(MinLevel))
	int iMaxLevel;         AST(NAME(MaxLevel))
	int iQuality;          AST(NAME(Quality))
	char achSearch[128];   AST(NAME(Search))
	char achCategory[128]; AST(NAME(Category)) 
	char achClass[128];	   AST(NAME(Class))
	char achGemType[128];  AST(NAME(GemType))
	int iNumberOfSlots;	   AST(NAME(NumberOfSlots))
	char achStats[128];    AST(NAME(Stats))

	int iOwnedLotsOnly;    AST(NAME(OwnedLotsOnly))
	int iBidLotsOnly;      AST(NAME(BidLotsOnly))
} ParamsAuctionSearch;

AUTO_STRUCT;
typedef struct MappedAuctionSlot
{
	REF_TO(ItemDef) hItemDef;	AST(NAME(ItemDef))
	int iCount;				AST(NAME(Count))
}MappedAuctionSlot;

AUTO_STRUCT;
typedef struct MappedAuctionLot
{
	U32 uContainerID;		AST(NAME(ContainerID))
	int iIndex;				AST(NAME(Index))

	U32 uCreationTime;		AST(NAME(CreationTime) FORMATSTRING(JSON_SECS_TO_RFC822=1))
	U32 uExpireTime;		AST(NAME(ExpireTime) FORMATSTRING(JSON_SECS_TO_RFC822=1))
	U32 uPrice;				AST(NAME(Price))

	int iCurrentBid;		AST(NAME(CurrentBid))
	int iMinBid;			AST(NAME(MinimumBid))
	int iNumBids;			AST(NAME(NumBids))
	U32 uBiddingPlayerAccountID;	AST(NAME(BiddingPlayerAccountID))
	U32 uBiddingPlayerContainerID;	AST(NAME(BiddingPlayerContainerID))

	bool bWinning;			AST(NAME(Winning))
	bool bOwner;			AST(NAME(Owner))

	char *estrOwnerName;	AST(NAME(OwnerName) ESTRING)
	char *estrOwnerHandle;	AST(NAME(OwnerHandle) ESTRING)

	MappedAuctionSlot **ppSlots; AST(NAME(Slots))
}MappedAuctionLot;

AUTO_STRUCT;
typedef struct MappedAuctionList
{
	int iRealHitCount;		AST(NAME(RealHitCount))

	MappedAuctionLot **ppLots;	AST(NAME(AuctionLots))
}MappedAuctionList;

AUTO_STRUCT;
typedef struct MappedAuctionSearch
{
	char *estrID;            AST(NAME(ID) ESTRING)
	AuctionLotList *pList;   AST(NAME(AuctionList) NO_NETSEND)
	MappedAuctionList *pMappedList; AST(NAME(List))
} MappedAuctionSearch;

/***************************************************************************/

static void gslGateway_Auction_GetLotsSearch_CB(TransactionReturnVal *pReturn, void *pData)
{
	int i = 0;
	AuctionLotList *pList;	
	//I should really do something here

	GatewaySession *psess = wgsFindSessionForAccountId(((intptr_t)pData));

	if(RemoteCommandCheck_Auction_GetLots(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		ContainerTracker *pTracker = session_FindContainerTracker(psess,GW_GLOBALTYPE_AUCTION_SEARCH,"search");

		if(pTracker && pTracker->pAuctionLotList)
		{
			StructDestroy(parse_AuctionLotList,pTracker->pAuctionLotList);
			pTracker->bModified = true;
		}

		if(pTracker)
			pTracker->pAuctionLotList = pList;
		else
			StructDestroy(parse_AuctionLotList,pList);
	}
	else
	{
		Entity *pEnt = session_GetLoginEntity(psess);
		if(pEnt)
		{
			notify_NotifySend(pEnt, kNotifyType_AuctionFailed, entTranslateMessageKey(pEnt, "Auction.SearchFailed"), NULL, NULL);
		}
	}
}

//void gslGateway_AuctionRequestSearch(Entity *pEnt, int iMinLevel, int iMaxLevel, int iQuality, const char *pchSearchVal, const char *pchSearchCategory)
void gslGateway_AuctionRequestSearch(Entity *pEnt, ParamsAuctionSearch *pParams)
{
	AuctionSearchRequest *pSearchRequest = NULL;

	if(pEnt == NULL)
		return;

	pSearchRequest = StructCreate(parse_AuctionSearchRequest);

	pSearchRequest->minLevel = pParams->iMinLevel;
	pSearchRequest->maxLevel = pParams->iMaxLevel;
	pSearchRequest->itemQuality = pParams->iQuality;
	pSearchRequest->stringSearch = StructAllocString(pParams->achSearch);
	pSearchRequest->pchRequiredClass = StructAllocString(pParams->achClass);
	pSearchRequest->uiNumGemSlots = pParams->iNumberOfSlots;
	
	pSearchRequest->eRequiredGemType = StaticDefineIntGetInt(ItemGemTypeEnum,pParams->achGemType);
	if(pSearchRequest->eRequiredGemType == -1)
		pSearchRequest->eRequiredGemType = 0;

	if(pParams->achStats)
	{
		char** tokens = NULL;
		int i;

		estrTokenize(&tokens, "%", pParams->achStats);
		for(i=0;i<eaSize(&tokens);i++)
		{
			int iAttrib = StaticDefineIntGetInt(AttribTypeEnum,tokens[i]);

			if(iAttrib)
				ea32Push(&pSearchRequest->eaiAttribs,iAttrib);
		}

		eaDestroyEString(&tokens);
	}

	if (pParams->achCategory[0])
	{
		char** tokens = NULL;
		estrTokenize(&tokens, "%", pParams->achCategory);
		if(eaSize(&tokens))
		{
			pSearchRequest->pchItemSortTypeCategoryName = allocAddString(tokens[0]);
			if(eaSize(&tokens) == 2)
				pSearchRequest->sortType = atoi(tokens[1]);
		}
		eaDestroyEString(&tokens);
	}

	pSearchRequest->numResults = MAX_SEARCH_RESULTS;
	pSearchRequest->stringLanguage = entGetLanguage(pEnt);

	if (gAuctionConfig.bHideOwnAuctionsInSearch)
	{
		pSearchRequest->bExcludeOwner = true;
		if (pEnt)
		{
			pSearchRequest->ownerID = entGetContainerID(pEnt);
		}			
	}
	else
	{
		pSearchRequest->bExcludeOwner = false;
	}

	{	
		RemoteCommand_Auction_GetLots(objCreateManagedReturnVal(gslGateway_Auction_GetLotsSearch_CB, (void *)((intptr_t)entGetAccountID(pEnt))),GLOBALTYPE_AUCTIONSERVER, 0, pSearchRequest);
	}
}

void SubscribeAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker, ParamsAuctionSearch *pParams)
{
	Entity *pEnt = session_GetLoginEntity(psess);

	PERFINFO_AUTO_START_FUNC();

	if(pEnt && pParams)
	{
		if(ptracker->pAuctionLotList)
		{
			StructDestroy(parse_AuctionLotList, ptracker->pAuctionLotList);
			ptracker->pAuctionLotList = NULL;
			ptracker->bReady = false;
		}

		if(pParams->iBidLotsOnly)
		{
			gslAuction_GetLotsBidByPlayer(pEnt,pParams->achSearch);
		}
		else if(pParams->iOwnedLotsOnly)
		{
			gslAuction_GetLotsOwnedByPlayer(pEnt);
		}
		else
		{
			gslGateway_AuctionRequestSearch(pEnt, pParams);
		}
	}

	PERFINFO_AUTO_STOP();
}

bool IsModifiedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker)
{
	return false;
}

bool CheckModifiedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker)
{
	return false;
}

bool IsReadyAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker)
{
	return ptracker->pAuctionLotList != NULL;
}

MappedAuctionSearch *CreateMappedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker, MappedAuctionSearch *psearch)
{
	int i,n;
	psearch = StructCreate(parse_MappedAuctionSearch);

	PERFINFO_AUTO_START_FUNC();

	estrCopy(&psearch->estrID, &ptracker->estrID);
	psearch->pList = StructClone(parse_AuctionLotList, ptracker->pAuctionLotList);
	psearch->pMappedList = StructCreate(parse_MappedAuctionList);

	psearch->pMappedList->iRealHitCount = psearch->pList->realHitcount;

	for(i=0;i<eaSize(&psearch->pList->ppAuctionLots);i++)
	{
		MappedAuctionLot *pMappedLot = StructCreate(parse_MappedAuctionLot);
		AuctionLot *pLot = psearch->pList->ppAuctionLots[i];

		eaPush(&psearch->pMappedList->ppLots,pMappedLot);

		pMappedLot->uContainerID = pLot->iContainerID;
		pMappedLot->iIndex = i;

		pMappedLot->uCreationTime = pLot->creationTime;
		pMappedLot->uExpireTime = pLot->uExpireTime;
		pMappedLot->uPrice = pLot->price;

		if(pLot->pBiddingInfo)
		{
			pMappedLot->iCurrentBid = MAX(pLot->pBiddingInfo->iMinimumBid,pLot->pBiddingInfo->iCurrentBid);
			pMappedLot->iMinBid = Auction_GetMinimumNextBidValue(pLot);
			pMappedLot->iNumBids = pLot->pBiddingInfo->iNumBids;
			pMappedLot->uBiddingPlayerAccountID = pLot->pBiddingInfo->iBiddingPlayerAccountID;
			pMappedLot->uBiddingPlayerContainerID = pLot->pBiddingInfo->iBiddingPlayerContainerID;
			pMappedLot->bWinning = pLot->pBiddingInfo->iBiddingPlayerAccountID == psess->idAccount;
		}
		
		if(pLot->pOptionalData)
		{
			estrCopy2(&pMappedLot->estrOwnerHandle,pLot->pOptionalData->pchOwnerHandle);
			estrCopy2(&pMappedLot->estrOwnerName,pLot->pOptionalData->pchOwnerName);
		}

		pMappedLot->bOwner = pLot->OwnerAccountID == psess->idAccount;
		
		for(n=0;n<eaSize(&pLot->ppItemsV2);n++)
		{
			MappedAuctionSlot *pSlot = StructCreate(parse_MappedAuctionSlot);

			COPY_HANDLE(pSlot->hItemDef,pLot->ppItemsV2[n]->slot.pItem->hItem);
			pSlot->iCount = pLot->ppItemsV2[n]->slot.pItem->count;

			eaPush(&pMappedLot->ppSlots,pSlot);
		}
	}

	PERFINFO_AUTO_STOP();
	return psearch;
}

void DestroyMappedAuctionSearch(GatewaySession *psess, ContainerTracker *ptracker, MappedAuctionSearch *psearch)
{
	if(psearch)
	{
		StructDestroy(parse_MappedAuctionSearch, psearch);
	}
}

/***************************************************************************/

void session_SendIntegerUpdate(GatewaySession *psess, const char *pchUpdateMessage, U32 iInt)
{
	Packet *pkt;


	SESSION_PKTCREATE(pkt, psess, "Server_UpdateInteger");

	pktSendString(pkt, pchUpdateMessage);
	pktSendU32(pkt, iInt);

	pktSend(&pkt);
}

void session_SendTradeBagResult(GatewaySession *psess, TradeBagLite *pBag)
{
	Packet *pkt;
	char *estr = NULL;

	SESSION_PKTCREATE(pkt, psess, "Server_TradeBagResult");

	ParserWriteJSON(&estr, parse_TradeBagLite, pBag,
		0,
		0,
		TOK_SERVER_ONLY|TOK_EDIT_ONLY|TOK_NO_NETSEND);

	pktSendString(pkt, estr);
	pktSend(&pkt);
}

void session_updateAuctionLotList(GatewaySession *psess, char *pchLotID, AuctionLotList *pList)
{
	ContainerTracker *pTracker = psess ? session_FindContainerTracker(psess,GW_GLOBALTYPE_AUCTION_SEARCH,pchLotID) : NULL;

	if(!pTracker)
		return;

	if(pTracker->pAuctionLotList)
	{
		StructDestroy(parse_AuctionLotList,pTracker->pAuctionLotList);
	}

	pTracker->pAuctionLotList = StructClone(parse_AuctionLotList,pList);

	pTracker->bModified = true;
}

#include "gslGatewayAuction_c_ast.c"


/* End of File */
