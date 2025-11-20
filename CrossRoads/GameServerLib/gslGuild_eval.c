/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "Expression.h"
#include "guild.h"
#include "mission_common.h"
#include "Player.h"


// ----------------------------------------------------------------------------------
// Guild Expression Functions
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerGetGuildStat);
S32 guild_FuncGetGuildStat(ExprContext *pContext, const char *pcStatName)
{
	// Get the entity
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild && pGuild->pGuildStatsInfo) {
		GuildStat *pGuildStat = NULL;
		S32 iGuildStatIndex = eaIndexedFindUsingString(&pGuild->pGuildStatsInfo->eaGuildStats, pcStatName);
		if (iGuildStatIndex >= 0) {
			return pGuild->pGuildStatsInfo->eaGuildStats[iGuildStatIndex]->iValue;
		} else {
			// Try to get the default for this specific stat
			GuildStatDef *pGuildStatDef = RefSystem_ReferentFromString(g_GuildStatDictionary, pcStatName);
			return pGuildStatDef ? pGuildStatDef->iInitialValue : 0;
		}
	}
	return 0;
}


// Returns the number of members from the given class in the player's guild
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerGetGuildMemberClassCount);
S32 guild_FuncGetGuildMemberClassCount(ExprContext *pContext, const char *pcClassName)
{
	S32 iMemberCount = 0;

	// Get the entity
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild && eaSize(&pGuild->eaMembers) > 0) {
		FOR_EACH_IN_EARRAY_FORWARDS(pGuild->eaMembers, GuildMember, pGuildMember) {
			if (pGuildMember && stricmp(pGuildMember->pchClassName, pcClassName) == 0) {
				++iMemberCount;
			}
		}
		FOR_EACH_END
	}

	return iMemberCount;
}


// Indicates if the guild is assigned the same theme as the given theme
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerGuildIsTheme);
bool guild_FuncGuildIsTheme(ExprContext *pContext, const char *pcThemeName)
{
	// Get the entity
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	Guild *pGuild = guild_GetGuild(pEnt);

	return pcThemeName && pcThemeName[0] && pGuild && IS_HANDLE_ACTIVE(pGuild->hTheme) && stricmp(REF_STRING_FROM_HANDLE(pGuild->hTheme), pcThemeName) == 0;
}


