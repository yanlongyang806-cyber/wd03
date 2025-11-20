#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// Testbed for new Attribute system

// Forward declarations
typedef struct AttribMod	AttribMod;
typedef struct Character	Character;

// Tracks all data relevant to a particular Attribute on a Character
AUTO_STRUCT;
typedef struct CharacterAttribute
{
	AttribMod **ppMods;
};

// Creates the Character's array of CharacterAttributes, moves
//  all AttribMods into their proper places
void TEST_character_CreateAttributeArray(SA_PARAM_NN_VALID Character *pchar);

// Destroys the Character's array of CharacterAttributes, moves
//  all AttribMods back to main array
void TEST_character_DestroyAttributeArray(SA_PARAM_NN_VALID Character *pchar);

// Utility function to get AttribMods we care about
AttribMod **TEST_character_GetAttribMods(SA_PARAM_NN_VALID Character *pchar, int attrib);

