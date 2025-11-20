/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "CostumeCommon.h"
#include "Entity.h"
#include "Expression.h"
#include "gslCostume.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// --------------------------------------------------------------------------
//  Costume Expression Functions
// --------------------------------------------------------------------------

AUTO_EXPR_FUNC(ai) ACMD_NAME(SetCostume);
void exprFuncSetCostume(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_DICT(PlayerCostume) char *costume)
{
	if(e)
	{
		entSetActive(e);
		costumeEntity_SetCostumeByName(e, costume);
	}
	
}

// Currently be used for testing purposes in Creatures for doing the transfomration costume swapping
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetActiveCostume);
void exprFuncSetActiveCostume(ACMD_EXPR_SELF Entity *e, S32 index)
{
	if(e)
	{
		costumetransaction_SetPlayerActiveCostume(e, kPCCostumeStorageType_Primary, index);
	}
}



AUTO_EXPR_FUNC(ai) ACMD_NAME(AddCostumeFx);
void exprFuncAddCostumeFx(ACMD_EXPR_SELF Entity *e, const char *fxName)
{
	const char *poolFXName = allocAddString(fxName);
	PCFXNoPersist *fx;
	int i;

	for(i=0; i<eaSize(&e->costumeRef.eaAdditionalFX); i++)
		if(e->costumeRef.eaAdditionalFX[i]->pcName==poolFXName)
			return;
	
	fx = StructAlloc(parse_PCFXNoPersist);

	fx->pcName = poolFXName;
	fx->fHue = 0;

	eaPush(&e->costumeRef.eaAdditionalFX, fx);
	costumeEntity_SetCostumeRefDirty(e);
}


AUTO_EXPR_FUNC(ai) ACMD_NAME(RemoveCostumeFx);
void exprFuncRemoveCostumeFx(ACMD_EXPR_SELF Entity *e, const char *fxName)
{
	const char *poolFXName = allocAddString(fxName);
	int i;

	for(i=eaSize(&e->costumeRef.eaAdditionalFX)-1; i>=0; i--)
	{
		PCFXNoPersist *fx = e->costumeRef.eaAdditionalFX[i];
		if(fx->pcName==fxName)
		{
			StructDestroySafe(parse_PCFXNoPersist, &fx);
			eaRemoveFast(&e->costumeRef.eaAdditionalFX, i);
			costumeEntity_SetCostumeRefDirty(e);
		}
	}
}

