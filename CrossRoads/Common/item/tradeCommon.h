#pragma once
GCC_SYSTEM

#include "itemEnums.h"
#include "itemEnums_h_ast.h"

typedef struct Item Item;
typedef struct ItemDef ItemDef;
typedef struct GameAccountDataExtract GameAccountDataExtract;

AUTO_STRUCT;
typedef struct TradeSlot
{
	Item *pItem;
	int SrcBagId;
	int SrcSlot;
	int count;
	int tradeSlotPetID;
} TradeSlot;

// Just like a regular TradeSlot, but the Item is unowned. Useful for caching.
AUTO_STRUCT;
typedef struct TradeSlotLite
{
	Item *pItem; AST(UNOWNED)
	InvBagIDs SrcBagId;
	int count;
	int SrcSlot;
	U64 SrcItemId;
	int tradeSlotPetID;
} TradeSlotLite;

AUTO_STRUCT;
typedef struct TradeBag
{
	bool finished;				// flag indicating whether the owner accepts the trade as given
	TradeSlot **ppTradeSlots;	// earray of all the slots in this bag
} TradeBag;

AUTO_STRUCT;
typedef struct TradeBagLite
{
	TradeSlotLite **ppTradeSlots;
}TradeBagLite;

#if GAMESERVER || GAMECLIENT
SA_RET_OP_STR char *trade_OfferIsValid(Entity *pEnt1, GameAccountDataExtract *pExtract);
#endif

bool trade_IsPetBeingTraded( Entity* pPetEnt, Entity* pSrcEnt );
void Trade_GetTradeableItems(TradeSlotLite ***peaSlots, Entity* pEnt, S32 iBag, bool bIncludePets, bool bIncludeUnidentified);
int trade_GetNumericItemTradedCount(SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_STR const char* pItemDefName);

#include "AutoGen/tradeCommon_h_ast.h"