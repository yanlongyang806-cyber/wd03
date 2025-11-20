#ifndef GSLITEMUPGRADE_H__
#define GSLITEMUPGRADE_H__

void itemUpgrade_BeginStack(Entity *pEnt, int iStackAmount, InvBagIDs eSrcBagID, int SrcSlotIdx, U64 uSrcItemID, InvBagIDs eModBagID, int ModSlotIdx, U64 uModItemID);
void itemUpgrade_Tick(Entity *pEnt, F32 fTick);

#endif