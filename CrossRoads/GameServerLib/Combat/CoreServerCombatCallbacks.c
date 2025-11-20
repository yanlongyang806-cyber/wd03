/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CoreServerCombatCallbacks.h"

#include "Character.h"
#include "Entity.h"

#include "AttribCurveImp.h"
#include "CharacterAttribs.h"
#include "CombatCallbacks.h"

/*
static bool csccbCharacterCanPerceive(Character *pchar, Character *pcharTarget)
{
	bool bPerceive = true;

	//Check hidden targets list
	if(pchar->perHidden && ea32Size(&pchar->perHidden) > 0)
	{
		if(ea32Find(&pchar->perHidden,pcharTarget->pEntParent->myRef) > -1)
			return false; //Cannot perceive, no matter the characters perception
	}
	if(pchar!=pcharTarget)
	{
		F32 fDist;
		F32 fPerception = pchar->pattrBasic->fPerception;
		Vec3 vec,vecTarget;
		if(entCheckFlag(pchar->pEntParent,ENTITYFLAG_IS_PLAYER))
		{
			fPerception -= pcharTarget->pattrBasic->fPerceptionStealth;
		}
		else
		{
			fPerception -= pcharTarget->pattrBasic->fAggroStealth;
		}
		fPerception = MAX(fPerception,0);
		entGetPos(pchar->pEntParent,vec);
		entGetPos(pcharTarget->pEntParent,vecTarget);
		fDist = distance3Squared(vec,vecTarget);
		bPerceive = (fDist <= (fPerception*fPerception));
	}
	return bPerceive;
}
*/

static F32 csccbPredictAttribModDef(AttribModDef *pdef)
{
	if(moddef_IsPredictedAttrib(pdef))
	{
		return 0.2f;
	}
	return 0.0f;
}

void coreserverInitCombatCallbacks(void)
{
	//combatcbCharacterCanPerceive = csccbCharacterCanPerceive;
	combatcbPredictAttribModDef = csccbPredictAttribModDef;
}