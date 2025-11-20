/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameSession.h"
#include "itemCommon.h"
#include "Team.h"
#include "mission_common.h"
#include "chatCommon.h"
#include "CharacterClass.h"
#include "CostumeCommon.h"
#include "progression_common.h"

#include "AutoGen/GameSession_h_ast.h"
#include "AutoGen/Team_h_ast.h"
#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/GameSession_h_ast.c"
#include "AutoGen/LobbyCommon_h_ast.h"

// Returns a static string which will be shared by all calls to this function. This function never returns NULL.
const char* gameSession_GetDestinationName(SA_PARAM_NN_VALID GameSession *pGameSession)
{
	return gameContentNode_GetUniqueNameByParams(pGameSession->destination.iUGCProjectID ? GameContentNodeType_UGC : GameContentNodeType_GameProgressionNode,
												 REF_STRING_FROM_HANDLE(pGameSession->destination.hNode),
												 pGameSession->destination.iUGCProjectID,
												 0,
												 0);
}
