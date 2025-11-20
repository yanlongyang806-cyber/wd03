#ifndef _COMBATADVANTAGE_H__
#define _COMBATADVANTAGE_H__

typedef U32 EntityRef;
typedef struct Character Character;

AUTO_STRUCT;
typedef struct CombatAdvantageNode
{
	EntityRef	erEntity;
	U32			uApplyID;

} CombatAdvantageNode;

#define COMBAT_ADVANTAGE_SYSTEM_APPLYID		-1
#define COMBAT_DISADVANTAGE					-1
#define COMBAT_ADVANTAGE_TO_EVERYONE		-2

void CombatAdvantage_CleanupCharacter(Character *pChar);

int CombatAdvantage_HasAdvantageOnCharacter(SA_PARAM_NN_VALID Character *pChar, SA_PARAM_NN_VALID Character *pTarget);
bool CombatAdvantage_HasUnconditionalAdvantageOnCharacter(Character *pChar, Character *pTarget);


#endif