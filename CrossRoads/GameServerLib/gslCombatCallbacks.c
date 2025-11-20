/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatCallbacks.h"

#include "EntityIterator.h"

#include "Character.h"
#include "Entity.h"
#include "GlobalTypes.h"
#include "NotifyCommon.h"
#include "StringFormat.h"
#include "PowersAutoDesc.h"
#include "CharacterAttribs.h"
#include "Player.h"

static void gslccbCharacterPowersChanged(Character *pchar)
{
	if(pchar && pchar->pEntParent)
	{
		if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
		{
//			ClientCmd_PowersResetArray(pchar->pEntParent,0);
			pchar->dirtyID++;
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			if(!pchar->dirtyID) pchar->dirtyID++;
		}
		else if(entIsPrimaryPet(pchar->pEntParent))
		{
// 			Entity *eOwner = entGetOwner(pchar->pEntParent);
// 			if(eOwner && entCheckFlag(eOwner,ENTITYFLAG_IS_PLAYER))
// 			{
// 				ClientCmd_PowersResetArray(eOwner,entGetRef(pchar->pEntParent));
// 			}
			pchar->dirtyID++;
			entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
			if(!pchar->dirtyID) pchar->dirtyID++;
		}
	}
}

void gslInitCombatCallbacks(void)
{
	combatcbCharacterPowersChanged = gslccbCharacterPowersChanged;
}