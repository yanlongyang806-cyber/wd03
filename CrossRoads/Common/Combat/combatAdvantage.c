#include "combatAdvantage.h"
#include "Character.h"
#include "Entity.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// -----------------------------------------------------------------------------------------------------------------------

// Character
//  Inputs: Character, Character target
//  Return: 1 if the Character has combat advantage against the given character, 0 otherwise
// This is NOT predicted
AUTO_EXPR_FUNC(CEFuncsCharacter) ACMD_NAME(HasCombatAdvantageOnCharacter);
int CombatAdvantage_HasAdvantageOnCharacter(SA_PARAM_NN_VALID Character *pChar, SA_PARAM_NN_VALID Character *pTarget)
{
	EntityRef erCharEntRef = entGetRef(pChar->pEntParent);

	// check if the given character has advantage on everyone
	FOR_EACH_IN_EARRAY(pChar->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if (pNode->erEntity == COMBAT_ADVANTAGE_TO_EVERYONE)
			return true;
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(pTarget->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if (pNode->erEntity == erCharEntRef || pNode->erEntity == COMBAT_DISADVANTAGE)
			return true;
	}
	FOR_EACH_END

	return false;
}

// -----------------------------------------------------------------------------------------------------------------------
bool CombatAdvantage_HasUnconditionalAdvantageOnCharacter(Character *pChar, Character *pTarget)
{
	EntityRef erCharEntRef = entGetRef(pChar->pEntParent);

	// check if the given character has advantage on everyone
	FOR_EACH_IN_EARRAY(pChar->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if (pNode->erEntity == COMBAT_ADVANTAGE_TO_EVERYONE)
			return true;
	}
	FOR_EACH_END

	FOR_EACH_IN_EARRAY(pTarget->ppCombatAdvantages, CombatAdvantageNode, pNode)
	{
		if ((pNode->erEntity == COMBAT_DISADVANTAGE) || 
			(pNode->erEntity == erCharEntRef && pNode->uApplyID != COMBAT_ADVANTAGE_SYSTEM_APPLYID))
			return true;
	}
	FOR_EACH_END

	return false;
}

// -----------------------------------------------------------------------------------------------------------------------
void CombatAdvantage_CleanupCharacter(Character *pChar)
{
	if (pChar->ppCombatAdvantages)
	{
		eaDestroyEx(&pChar->ppCombatAdvantages, NULL);
	}
}



#include "combatAdvantage_h_ast.c"