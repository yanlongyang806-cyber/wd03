#ifndef INVENTORY_UIEXPR_H
#define INVENTORY_UIEXPR_H

typedef Item Item;

void Item_ItemPowersAutoDesc(Item *pItem, char **pestrDesc, const char *pchPowerMessageKey, const char *pchAttribMessageKey, S32 eActiveGemSlotType);
S32 Item_IsBeingTraded(Entity* pEnt, Item* pItem);

#endif INVENTORY_UIEXPR_H