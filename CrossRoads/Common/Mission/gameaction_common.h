/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GAMEACTION_COMMON_H
#define GAMEACTION_COMMON_H
GCC_SYSTEM

typedef struct MultiVal MultiVal;
typedef struct MissionDef MissionDef;
typedef struct WorldGameActionBlock WorldGameActionBlock;
typedef struct WorldGameActionProperties WorldGameActionProperties;
typedef struct WorldVariable WorldVariable;


AUTO_ENUM;
typedef enum FloaterActionColors
{
	FloaterActionColors_Failed,
	FloaterActionColors_Gained,
	FloaterActionColors_Progress,
	FloaterActionColors_Custom,

	FloaterActionColors_Count, EIGNORE
} FloaterActionColors;

AUTO_STRUCT;
typedef struct GameActionDoorDestinationVariable
{
	WorldVariable* pDoorDestination;
	const char* pchMapVars; AST(POOL_STRING)
	int iGameActionIndex;
} GameActionDoorDestinationVariable;

AUTO_STRUCT;
typedef struct GameActionDoorDestinationVarArray
{
	GameActionDoorDestinationVariable** eaVariables;
} GameActionDoorDestinationVarArray;

// Runs generation on the given list of GameActions
// If a MissionDef is provided, it will be used to validate GrantSubMission Actions
void gameaction_GenerateActions(WorldGameActionProperties ***actions, MissionDef *def, const char *pchFilename);

// Runs validation on the given list of GameActions
// If a MissionDef is provided, it will be used to validate GrantSubMission Actions
bool gameaction_ValidateActions(WorldGameActionProperties ***actions, const char *pchSourceMapName, MissionDef *pRootMissionDef, MissionDef *pMissionDef, bool bAllowMapVariables, const char *pchFilename);

// Returns TRUE if the two blocks are identical
// Note that NULL and a block with no actions are considered identical
bool gameactionblock_Compare(WorldGameActionBlock *a, WorldGameActionBlock *b);

// Returns TRUE if the two WorldGameActionBlocks are similar enough to edit.
// (They have the same number of actions, and all actions are of the same type)
bool gameactionblock_CompareForEditing(WorldGameActionBlock *a, WorldGameActionBlock *b);

bool gameactionblock_CanOpenContactDialog(WorldGameActionBlock *pBlock);

// Cleans any expressions
void gameactionblock_Clean(WorldGameActionBlock *pBlock);

void gameaction_FixupMessages(WorldGameActionProperties *pAction, const char *pcScope, const char *pcBaseMessageKey, int iIndex, bool bCreate);
void gameaction_FixupMessageList(WorldGameActionProperties*** peaActionList, const char *pcScope, const char *pcBaseMessageKey, int iBaseIndex);

// Helper function to count the number of actions in an array by type
int gameaction_CountActionsByType(WorldGameActionProperties** eaActionList, S32 eType);

#endif