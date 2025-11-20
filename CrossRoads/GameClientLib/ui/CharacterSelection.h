/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCL_CHARACTERSELECTION_H
#define GCL_CHARACTERSELECTION_H

#include "UICore.h"

typedef struct BasicTexture BasicTexture;
typedef struct Entity Entity;
typedef struct Login2CharacterChoice Login2CharacterChoice;
typedef U32 ContainerID;

const Login2CharacterChoice * GetRenamingCharacter(void);
bool CharacterSelection_IsVirtualShardUGCShard(ContainerID iVirtualShardID);

AUTO_STRUCT;
typedef struct CharacterSelectionSlot
{
	Login2CharacterChoice *pChoice; AST(UNOWNED)
	bool bCharacterSlot;
	bool bUnusedSlot;
	bool bPurchaseSlot;
	bool bCanUse;				// set to true if the player can use this type, will be false if it was a free characterpath that is no longer free
	bool bSuperPremium;			// this is a super premium slot
} CharacterSelectionSlot;

extern ContainerID g_CharacterSelectionPlayerId;

Entity *CharacterSelection_GetSubscribedEntity(void);
void LoginExpr_LobbyChooseCharacterByID(ContainerID id);
SA_RET_OP_VALID Login2CharacterChoice * CharacterSelection_GetChoiceByID(U32 iID);
BasicTexture* CharacterSelection_GetCharacterPortraitTexture(SA_PARAM_NN_VALID Login2CharacterChoice *pChoice, const char *pchBackground, F32 fWidth, F32 fHeight);

#endif
