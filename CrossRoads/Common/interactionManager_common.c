/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "interactionManager_common.h"

#include "Character.h"
#include "CharacterClass.h"
#include "CombatEval.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityInteraction.h"
#include "Player.h"
#include "WorldGrid.h"

static F32 CharacterPickUpMass(Character *pchar)
{
	static S32 s_eAttrib = INT_MIN;
	F32 fMass = 0;
	if(pchar && pchar->pattrBasic)
	{
		// This is a hardcoded copy of the current FC pickup rules.  It REALLY needs to be either
		//  implemented in data, or be a per-project callback, but for now we'll leave it hardcoded.
		F32 fStrength = 0;
		if(s_eAttrib==INT_MIN)
			s_eAttrib = StaticDefineIntGetInt(AttribTypeEnum,"StatStrength");
		if(s_eAttrib >= 0 && IS_NORMAL_ATTRIB(s_eAttrib))
		{
			fStrength = *F32PTR_OF_ATTRIB(pchar->pattrBasic,s_eAttrib);
		}
		if (fStrength) {
			fMass = 2 + log(fStrength/10)/log(2);
		} else {
			fMass = 2;
		}
	}
	return fMass;
}

bool im_EntityCanThrowObject(Entity *pEnt, WorldInteractionNode *pNode, F32 fOverrrideStrength)
{
	WorldInteractionEntry *pEntry;
	CritterDef *pCritterDef;
	CritterOverrideDef *pCritterOverrideDef;
	F32 fObjectMass=0, fPickUpMass=0;

	if(!pNode || !pEnt || !pEnt->pChar)
		return false;

	pEntry = wlInteractionNodeGetEntry(pNode);
	if(!pEntry || !pEntry->full_interaction_properties)
		return false;

	pCritterDef = wlInteractionGetDestructibleCritterDef(pEntry->full_interaction_properties);
	pCritterOverrideDef = wlInteractionGetDestructibleCritterOverride(pEntry->full_interaction_properties);

	fObjectMass = im_GetMass(pNode);
	if(fOverrrideStrength > 0.0f)
	{
		fPickUpMass = fOverrrideStrength;
	}
	else
	{
		fPickUpMass = CharacterPickUpMass(pEnt->pChar);
	}

	if(pCritterOverrideDef) 
	{
		if(pCritterOverrideDef->eFlags && pCritterOverrideDef->eFlags & kCritterOverrideFlag_Throwable)
		{	
			if (fPickUpMass>=fObjectMass)
				return true;
		}
		else if(pCritterOverrideDef->eFlags)
			return false; 
	}

	if(pCritterDef)
	{
		if(pCritterDef->eInteractionFlags && pCritterDef->eInteractionFlags & kCritterOverrideFlag_Throwable)
		{
			if (fPickUpMass>=fObjectMass)
				return true;
		}
	}

	return false;
}

bool im_IsNotDestructible(WorldInteractionNode *pNode)
{
	static int iMask = 0;

	if(!pNode)
		return false;

	if (!iMask) 
		iMask = wlInteractionClassNameToBitMask("Destructible");

	if(!wlInteractionCheckClass(pNode,iMask))
		return true;

	return false;
}

bool im_IsNotDestructibleOrCanThrowObject(Entity* pEnt, WorldInteractionNode *pNode, UserData *pData)
{
	if(im_IsNotDestructible(pNode))
		return true;
	else if(im_EntityCanThrowObject(pEnt, pNode, 0.0f))
		return true;

	return false;
}

S32 im_GetDeathPowerDefs(WorldInteractionNode *pNode, PowerDef ***pppDefs)
{
	WorldInteractionEntry *pEntry;
	CritterOverrideDef *pCritterOverrideDef;
	PowerDef *pdef;

	if(!pNode)
		return 0;

	pEntry = wlInteractionNodeGetEntry(pNode);
	if(!pEntry || !pEntry->full_interaction_properties)
		return 0;

	pdef = wlInteractionGetDestructibleDeathPower(pEntry->full_interaction_properties);
	if(pdef)
	{
		eaPush(pppDefs,pdef);
	}
	else
	{
		pCritterOverrideDef = wlInteractionGetDestructibleCritterOverride(pEntry->full_interaction_properties);
		if(pCritterOverrideDef)
		{
			int i,s=eaSize(&pCritterOverrideDef->ppOnDeathPowers);
			for(i=0; i<s; i++)
			{
				pdef = GET_REF(pCritterOverrideDef->ppOnDeathPowers[i]->hPowerDef);
				if(pdef)
				{
					eaPush(pppDefs,pdef);
				}
			}
		}
	}

	return eaSize(pppDefs);
}

F32 im_GetMass(WorldInteractionNode *pNode)
{
	WorldInteractionEntry *pEntry;
	CritterDef *pCritterDef;
	CritterOverrideDef *pCritterOverrideDef;

	if(!pNode)
		return 0;

	pEntry = wlInteractionNodeGetEntry(pNode);
	if(!pEntry || !pEntry->full_interaction_properties)
		return 0;

	pCritterOverrideDef = wlInteractionGetDestructibleCritterOverride(pEntry->full_interaction_properties);
	if(pCritterOverrideDef)
	{
		return pCritterOverrideDef->fMass;
	}

	pCritterDef = wlInteractionGetDestructibleCritterDef(pEntry->full_interaction_properties);
	if(pCritterDef)
	{
		return pCritterDef->fMass;
	}

	return 0;
}

CharacterClass *im_GetCharacterClass(WorldInteractionNode *pNode)
{
	CharacterClass *pclass = NULL;

	WorldInteractionEntry *pEntry;
	CritterDef *pCritterDef;

	if(!pNode)
		return pclass;

	pEntry = wlInteractionNodeGetEntry(pNode);
	if(!pEntry || !pEntry->full_interaction_properties)
		return pclass;

	pCritterDef = wlInteractionGetDestructibleCritterDef(pEntry->full_interaction_properties);
	if(pCritterDef)
	{
		pclass = characterclass_GetAdjustedClass(pCritterDef->pchClass,1,0,pCritterDef);
	}

	return pclass;
}

S32 im_GetLevel(WorldInteractionNode *pNode)
{
	S32 iLevel = 1;
	WorldInteractionEntry *pEntry;

	if(!pNode)
		return iLevel;

	pEntry = wlInteractionNodeGetEntry(pNode);
	if(!pEntry || !pEntry->full_interaction_properties)
		return iLevel;

	MAX1(iLevel,(S32)wlInteractionGetDestructibleCritterLevel(pEntry->full_interaction_properties));

	return iLevel;
}

void im_MapUnload(void)
{
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
	Entity *pPlayerEnt;

	while ((pPlayerEnt = EntityIteratorGetNext(iter)))
	{
		if (pPlayerEnt && pPlayerEnt->pPlayer) 
		{
			eaDestroyStruct(&pPlayerEnt->pPlayer->InteractStatus.ppTargetableNodesLast, parse_TargetableNode);
		}
	}
	EntityIteratorRelease(iter);
}
