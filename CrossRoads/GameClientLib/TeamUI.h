#pragma once
GCC_SYSTEM

typedef Entity Entity;

void teamHideMapTransferChoice( Entity *pEnt );
S32 TeamUp_GetMembers(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pPlayer, int iGroupIdx, int includePets, bool bExcludePlayer);
S32 TeamUp_GetGroups(UIGen *pGen, Entity *pPlayer, bool bIncludePlayer, bool bAddEmptyGroup);