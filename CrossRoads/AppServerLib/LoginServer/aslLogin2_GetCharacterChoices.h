#pragma once
/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

typedef U32 ContainerID;
typedef struct Login2CharacterChoices Login2CharacterChoices;

typedef void (*GetCharacterChoicesCB)(Login2CharacterChoices *characterChoices, void *userData);

// Get the character choices for an account, including from other shards if the current shard is part of a cluster.
// The callback will be called when the character choices are ready.
void aslLogin2_GetCharacterChoices(ContainerID accountID, GetCharacterChoicesCB cbFunc, void *userData);

// This function should be called every frame and will do periodic processing related to getting character choices.
void aslLogin2_CharacterChoicesTick(void);