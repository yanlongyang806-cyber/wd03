/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Skills_DD.h"

#include "BlockEarray.h"
#include "estring.h"
#include "GameStringFormat.h"
#include "Expression.h"
#include "ExpressionPrivate.h"
#include "Character.h"
#include "CharacterAttribs.h"

#include "NotifyCommon.h"

#include "Entity.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "stringcache.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

typedef struct Expression Expression;

static void gameaction_SendFloaterMsgToEnt(Entity *pEnt, Message *pMessage, int r, int g, int b);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void NNOParseSkillsForTooltip(Expression* pExpr, const char** ppchConditionInfoOut)
{
	int ii,s=beaSize(&pExpr->postfixEArray);
	static MultiVal **s_ppStack = NULL;
	for(ii=0; ii<s; ii++)
	{
		MultiVal *pVal = pExpr->postfixEArray + ii;
		if(pVal->type==MULTIOP_PAREN_OPEN || pVal->type==MULTIOP_PAREN_CLOSE)
			continue;
		if(pVal->type==MULTIOP_FUNCTIONCALL)
		{
			const char *pchFunction = pVal->str;
			if(!strncmp(pchFunction,"Ddcharacter", 11) || !strncmp(pchFunction,"Ddentity", 8))
			{
				MultiVal* pAttribVal = NULL;
				if (!strstr(pchFunction, "hasskill"))
	 				eaPop(&s_ppStack);
				pAttribVal = eaPop(&s_ppStack);
				*ppchConditionInfoOut = allocFindString(MultiValGetString(pAttribVal,NULL));

			}
		}
		eaPush(&s_ppStack,pVal);
	}

	eaClear(&s_ppStack);
}


AUTO_EXPR_FUNC(player, CEFuncsCharacter);
bool DDCharacterHasSkill(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
	const char *skillName)
{
	return !!character_FindPowerByName(pChar, skillName);
}

AUTO_EXPR_FUNC(entityutil);
bool DDEntityHasSkill(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt,
	const char *skillName)
{
	return pEnt && character_FindPowerByName(pEnt->pChar, skillName);
}

//Returns the magnitude of the given character's bonus for a specified skill name.
AUTO_EXPR_FUNC(player, CEFuncsCharacter); 
F32 DDCharacterGetSkillBonus(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
	const char *skillName)
{
	return DDCharacterHasSkill(pContext, pChar, skillName) ? 9999 : 0;
}

//Returns the magnitude of the given character's bonus for a specified skill name.
AUTO_EXPR_FUNC(player, Entity, util); 
F32 DDEntGetSkillBonus(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt,
	const char *skillName)
{
	return pEnt && DDEntityHasSkill(pContext, pEnt, skillName) ? 9999 : 0;
}

//Returns the magnitude of the given character's bonus for a specified skill name.
AUTO_EXPR_FUNC(player, CEFuncsCharacter);
F32 DDCharacterPassiveRollSkill(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
	const char *skillName)
{
	return DDCharacterHasSkill(pContext, pChar, skillName) ? 9999 : 0;
}

//Evaluates a skill check on a character with a specific DC, returning true on success and false on failure (incl. critical failures.)
AUTO_EXPR_FUNC(player, CEFuncsCharacter); 
bool DDCharacterSkillCheck(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
						   const char *skillName,
						   S32 DC)
{
	return !!DDCharacterHasSkill(pContext, pChar, skillName);
}

//Rolls a skill check on a character, returning the result. (or 1 on a critical failure)
AUTO_EXPR_FUNC(player, CEFuncsCharacter); 
S32 DDCharacterRollSkill(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
						 const char *skillName)
{
	return DDCharacterHasSkill(pContext, pChar, skillName) ? 9999 : 0;
}

//Rolls a skill check on a party, returning the highest result.
AUTO_EXPR_FUNC(player, CEFuncsCharacter); 
bool DDPartySkillCheck(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN  peaEntsIn,
					   const char *skillName, S32 DC)
{
	int i;
	for (i = 0; i < eaSize(peaEntsIn); i++)
	{
		if(!(*peaEntsIn)[i])
			continue;
		if (DDEntityHasSkill(pContext, (*peaEntsIn)[i], skillName))
			return true;
	}
	return false;
}

//Rolls a skill check on a party, returning the highest result.
AUTO_EXPR_FUNC(player, CEFuncsCharacter); 
S32 DDPartyRollSkill(ExprContext *pContext, ACMD_EXPR_ENTARRAY_IN  peaEntsIn,
					 const char *skillName)
{
	int i;
	for (i = 0; i < eaSize(peaEntsIn); i++)
	{
		if(!(*peaEntsIn)[i])
			continue;
		if (DDEntityHasSkill(pContext, (*peaEntsIn)[i], skillName))
			return 9999;
	}
	return 0;
}

//Rolls a skill check on a character, returning the result. (or 1 on a critical failure)
AUTO_EXPR_FUNC(player, CEFuncsCharacter); 
bool DDCharacterPassiveSkillCheck(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
								 const char *skillName, S32 DC)
{
	return !!DDCharacterHasSkill(pContext, pChar, skillName);
}

//Returns the result of taking 10 vs. a DC.
AUTO_EXPR_FUNC(player, CEFuncsCharacter, util); 
bool DDCharacterTake10(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
					  const char *skillName, S32 DC)
{
	return !!DDCharacterHasSkill(pContext, pChar, skillName);
}


//Returns the result of taking 10 vs. a DC.
AUTO_EXPR_FUNC(player, CEFuncsCharacter, util); 
bool DDEntityTake10(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt,
				   const char *skillName, S32 DC)
{
	return pEnt && DDEntityHasSkill(pContext, pEnt, skillName);
}