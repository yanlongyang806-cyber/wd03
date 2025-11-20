#ifndef _MINIGAME_COMMON_H_
#define _MINIGAME_COMMON_H_
GCC_SYSTEM

#include "Message.h"

// Data-defined minigame types
extern DefineContext *g_pDefineMinigameTypes;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineMinigameTypes);
typedef enum MinigameType
{
	kMinigameType_None = 0, ENAMES(None)
} MinigameType;

extern StaticDefineInt MinigameTypeEnum[];

AUTO_STRUCT;
typedef struct MinigameDef
{
	const char* pchName;			AST(NAME(Name) STRUCTPARAM)
	DisplayMessage displayMsgPlay;	AST(NAME(PlayDisplayMessage) STRUCT(parse_DisplayMessage))
} MinigameDef;

AUTO_STRUCT;
typedef struct Minigames
{
	MinigameDef** eaGames; AST(NAME(Minigame))
} Minigames;

// Find a MinigameDef from a MinigameType
MinigameDef* Minigame_FindByType(MinigameType eType);

extern Minigames g_Minigames;

LATELINK;
void GameSpecific_MinigameLoad(void);

#endif