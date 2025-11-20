/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "MinigameCommon.h"
#include "MinigameCommon_h_ast.h"

#include "error.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GlobalTypes.h"
#include "StringCache.h"

#ifdef GAMECLIENT
#include "UIGen.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DefineContext *g_pDefineMinigameTypes = NULL;
Minigames g_Minigames = {0};

static void Minigames_LoadInternal(void)
{
	S32 i;
	loadstart_printf("Loading Minigames...");

	StructInit(parse_Minigames, &g_Minigames);

	g_pDefineMinigameTypes = DefineCreate();

	ParserLoadFiles(NULL, "defs/config/Minigames.def", "Minigames.bin", 
		PARSER_OPTIONALFLAG, parse_Minigames, &g_Minigames);

	for (i = 0; i < eaSize(&g_Minigames.eaGames); i++)
	{
		DefineAddInt(g_pDefineMinigameTypes, g_Minigames.eaGames[i]->pchName, i+1);
	}

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(MinigameTypeEnum, "MinigameType_");
#endif

	loadend_printf(" done (%d Minigames).", eaSize(&g_Minigames.eaGames));
}


void DEFAULT_LATELINK_GameSpecific_MinigameLoad(void)
{
}

AUTO_STARTUP(Minigames) ASTRT_DEPS(AS_Messages);
void Minigames_Load(void)
{
	Minigames_LoadInternal();
	GameSpecific_MinigameLoad();
}

// Find a MinigameDef from a MinigameType
MinigameDef* Minigame_FindByType(MinigameType eType)
{
	const char* pchType = StaticDefineIntRevLookup(MinigameTypeEnum, eType);
	if (pchType && pchType[0])
	{
		S32 i;
		for (i = eaSize(&g_Minigames.eaGames)-1; i >= 0; i--)
		{
			MinigameDef* pDef = g_Minigames.eaGames[i];
			if (stricmp(pDef->pchName, pchType) == 0)
			{
				return pDef;
			}
		}
	}
	return NULL;
}