#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef struct Character Character;
typedef struct Entity Entity;
typedef struct Item Item;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;

void NNOParseSkillsForTooltip(Expression* pExpr, const char** ppchConditionInfoOut);
F32 DDGetPlayerSkillBonus(Character *pChar, const char *skillName);
F32 DDCharacterGetSkillBonus(ExprContext *pContext, SA_PARAM_OP_VALID Character *pChar,
							 const char *skillName);