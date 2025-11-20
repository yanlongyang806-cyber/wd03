#pragma once

typedef struct PossibleMapChoices PossibleMapChoices;
typedef struct Entity Entity;


void TransferCommon_AddTeamMembersGuildMembersAndFriends(PossibleMapChoices *pChoices, Entity *pEntity);
int TransferCommon_PossibleMapChoiceSort(const PossibleMapChoice **ppMapA, const PossibleMapChoice **ppMapB);

void TransferCommon_RemoveNonSpecificChoices(SA_PARAM_NN_VALID PossibleMapChoice *** peaPossibleMapChoice);

SA_RET_OP_VALID PossibleMapChoice * TransferCommon_GetBestChoiceBasedOnTeamMembersEtc(PossibleMapChoices *pChoices, Entity *pEntity);

//if there's only one specific choice, then return it if there's at least one team member there, otherwise return NULL
SA_RET_OP_VALID PossibleMapChoice * TransferCommon_ChooseOnlySpecificChoiceIfTeamIsThere(PossibleMapChoices *pChoices, Entity *pEntity);