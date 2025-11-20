/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CharacterAttribute.h"
#include "CharacterAttribute_h_ast.h"


#include "Character.h"
#include "Entity.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Creates the Character's array of CharacterAttributes, moves
//  all AttribMods into their proper places
void TEST_character_CreateAttributeArray(Character *pchar)
{
	int i, s = ATTRIB_INDEX(kAttribType_LAST) + 1;
	eaSetSize(&pchar->modArray.ppAttributes,s);
	for(i=0; i<s; i++)
	{
		pchar->modArray.ppAttributes[i] = StructAlloc(parse_CharacterAttribute);
	}
	s = eaSize(&pchar->modArray.ppMods);
	for(i=0; i<s; i++)
	{
		int idx = ATTRIB_INDEX(pchar->modArray.ppMods[i]->pDef->offAttrib);
		eaPush(&pchar->modArray.ppAttributes[idx]->ppMods,pchar->modArray.ppMods[i]);
	}
	eaDestroy(&pchar->modArray.ppMods);
	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
}

// Destroys the Character's array of CharacterAttributes, moves
//  all AttribMods back to main array
void TEST_character_DestroyAttributeArray(Character *pchar)
{
	if(pchar->modArray.ppAttributes)
	{
		int i, s = ATTRIB_INDEX(kAttribType_LAST) + 1;
		for(i=0; i<s; i++)
		{
			int j, t = eaSize(&pchar->modArray.ppAttributes[i]->ppMods);
			for(j=0; j<t; j++)
			{
				eaPush(&pchar->modArray.ppMods,pchar->modArray.ppAttributes[i]->ppMods[j]);
			}
			eaDestroy(&pchar->modArray.ppAttributes[i]->ppMods);
		}
		eaDestroyStruct(&pchar->modArray.ppAttributes,parse_CharacterAttribute);
		entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
		pchar->modArray.ppAttributes = NULL;
	}
}

// Utility function to get AttribMods we care about
AttribMod **TEST_character_GetAttribMods(Character *pchar, int attrib)
{
	int i = ATTRIB_INDEX(attrib);
	if(pchar->modArray.ppAttributes && pchar->modArray.ppAttributes[i])
	{
		return pchar->modArray.ppAttributes[i]->ppMods;
	}
	return pchar->modArray.ppMods;
}

#include "CharacterAttribute_h_ast.c"