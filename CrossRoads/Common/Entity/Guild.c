/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityLib.h"
#include "Guild.h"
#include "chatCommon.h"
#include "utilitiesLib.h"
#include "ResourceManager.h"
#include "inventoryCommon.h"
#include "AutoTransDefs.h"
#include "ItemCommon.h"
#include "GameStringFormat.h"
#include "Player.h"
#include "ActivityLogCommon.h"
#include "mission_common.h"
#include "Expression.h"

#include "AutoGen/Guild_h_ast.h"
#include "AutoGen/ActivityLogCommon_h_ast.h"

static GuildBankConfig s_GuildBankConfig;

bool guild_LazySubscribeToBank()
{
	return s_GuildBankConfig.iLazySubscribeToBank;
}

bool guild_LazyCreateBank()
{
	return s_GuildBankConfig.iLazyCreateBank;
}

const char *guildBankConfig_GetBankTabPurchaseNumericName(void)
{
    return s_GuildBankConfig.bankTabPurchaseNumericName;
}

const char *guildBankConfig_GetBankTabUnlockNumericName(void)
{
    return s_GuildBankConfig.bankTabUnlockNumericName;
}

U32 guildBankConfig_GetBankTabPurchaseCost(int index)
{
    if ( index < ea32Size(&s_GuildBankConfig.bankTabPurchaseCosts) )
    {
        return s_GuildBankConfig.bankTabPurchaseCosts[index];
    }

    ErrorDetailsf("index=%d", index);
    Errorf("Attempt to get price of guild bank tab purchase beyond the end of the configured set");
    return 0;
}


static GuildRecruitParam s_GuildRecruitParam;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("GuildRecruitParam", BUDGET_GameSystems););

// Dictionary holding the guild stat definitions
DictionaryHandle g_GuildStatDictionary = NULL;

// Dictionary holding the guild theme definitions
DictionaryHandle g_GuildThemeDictionary = NULL;

static int guild_GuildThemeDefResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GuildThemeDef *pGuildThemeDef, U32 userID)
{
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void guild_RegisterGuildThemeDictionary(void)
{
	g_GuildThemeDictionary = RefSystem_RegisterSelfDefiningDictionary("GuildThemeDef", false, parse_GuildThemeDef, true, true, NULL);

	resDictManageValidation(g_GuildThemeDictionary, guild_GuildThemeDefResValidateCB);
}

AUTO_STARTUP(AS_GuildThemes);
void guild_LoadGuildThemeDefs(void)
{
	resLoadResourcesFromDisk(g_GuildThemeDictionary, "defs/GuildThemes", ".GuildTheme", NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
}

static int guild_GuildStatDefResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GuildStatDef *pStatDef, U32 userID)
{
	switch(eType)
	{
		case RESVALIDATE_POST_TEXT_READING:
			{
				if (pStatDef->eValidOperations == GuildStatUpdateOperation_None)
				{
					// Validate the guild stats
					ErrorFilenamef(pStatDef->pchFilename, "There is no valid operation defined for guild stat '%s'.", pStatDef->pchName);
				}
			}	
			return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
void guild_RegisterGuildStatDictionary(void)
{
	g_GuildStatDictionary = RefSystem_RegisterSelfDefiningDictionary("GuildStatDef", false, parse_GuildStatDef, true, true, NULL);

	resDictManageValidation(g_GuildStatDictionary, guild_GuildStatDefResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_GuildStatDictionary);
	} 
	else
	{
		resDictRequestMissingResources(g_GuildStatDictionary, 8, false, resClientRequestSendReferentCommand);
	}
}

AUTO_STARTUP(AS_GuildStats);
void guild_LoadGuildStatDefs(void)
{
	if (IsServer())
	{
		resLoadResourcesFromDisk(g_GuildStatDictionary, "defs/GuildStats", ".GuildStat", NULL,  RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	}
}

AUTO_STARTUP(AS_GuildRecruitParam);
void gslLoadGuildRecruitParam(void)
{
	StructInit(parse_GuildRecruitParam, &s_GuildRecruitParam);
	ParserLoadFiles(NULL, "defs/config/GuildRecruitParam.def", "defs/GuildRecruitParam.bin", PARSER_OPTIONALFLAG, parse_GuildRecruitParam, &s_GuildRecruitParam);
}

AUTO_STARTUP(AS_GuildBankConfig);
void gslLoadGuildBankConfig(void)
{
	StructInit(parse_GuildBankConfig, &s_GuildBankConfig);
	ParserLoadFiles(NULL, "defs/config/GuildBankConfig.def", "defs/GuildBankConfig.bin", PARSER_OPTIONALFLAG, parse_GuildBankConfig, &s_GuildBankConfig);
}

GuildRecruitParam *Guild_GetGuildRecruitParams(void)
{
	return &s_GuildRecruitParam;
}

AUTO_RUN_LATE;
int guild_RegisterContainer(void)
{
	objRegisterNativeSchema(GLOBALTYPE_GUILD, parse_Guild, NULL, NULL, NULL, NULL, NULL);
	return 1;
}

void guild_FixupRanks(GuildRankList* pRankList)
{
	if (pRankList)
	{
		GuildRank *pLeader = eaTail(&pRankList->eaRanks);
		if (pLeader)
		{
			// Make sure that the leader gets all permissions
			pLeader->ePerms = ~0;
		}
	}
}

const char *guild_GetBankTabName(Entity *pEnt, InvBagIDs eBagID) 
{
	Guild *pGuild = guild_GetGuild(pEnt);
	Entity *pGuildBank = guild_GetGuildBank(pEnt);
	InventoryBag *pBag = NULL;

	pBag = pGuildBank ? inv_guildbank_GetBag(pGuildBank, eBagID) : NULL;
	
	if (pBag && pBag->pGuildBankInfo && pBag->pGuildBankInfo->pcName && pBag->pGuildBankInfo->pcName[0]) {
		return pBag->pGuildBankInfo->pcName;
	} else {
		char pcBuffer[64];
		sprintf(pcBuffer, "GuildServer_Log_Tab%d", eBagID - InvBagIDs_Bank1 + 1);
		return entTranslateMessageKey(pEnt, pcBuffer);
	}
}

AUTO_EXPR_FUNC(Player) ACMD_NAME("PlayerHasJoinedGuild");
bool guild_expr_PlayerHasJoinedGuild( ExprContext *pContext )
{
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pGuild) {
		return pEnt->pPlayer->pGuild->bJoinedGuild;
	}
	return false;
}

AUTO_EXPR_FUNC(Player) ACMD_NAME("PlayerIsGuildMember");
bool guild_expr_PlayerIsGuildMember( ExprContext *pContext )
{
    Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
    return guild_IsMember(pEnt);
}

U32 guildevent_GetReplyCount(Guild *pGuild, GuildEvent *pGuildEvent, U32 iStartTime, GuildEventReplyType eReplyType)
{
	int i, count = 0;
	for (i = eaSize(&pGuildEvent->eaReplies)-1; i >= 0; i--)
	{
		if (pGuildEvent->eaReplies[i]->iStartTime == iStartTime &&
			pGuildEvent->eaReplies[i]->eGuildEventReplyType == eReplyType)
		{
			GuildMember *pReplyMember = eaIndexedGetUsingInt(&pGuild->eaMembers, pGuildEvent->eaReplies[i]->iMemberID);
			if (pReplyMember && pReplyMember->iRank >= pGuildEvent->iMinGuildRank &&
				pReplyMember->iLevel >= pGuildEvent->iMinLevel && pReplyMember->iLevel <= pGuildEvent->iMaxLevel)
			{
				count++;
			}
		}
	}
	return count;
}

U64 guildevent_GetReplyKey(U32 iMemberID, U32 iStartTime)
{
	return (0.5 * (iMemberID + iStartTime) * (iMemberID + iStartTime + 1)) + iStartTime;
}

bool guild_CanClaimLeadership(GuildMember *pGuildMember, Guild *pGuild)
{
	int i, iGuildSize;
	S32 iRank;
	U32 iTimestamp = timeSecondsSince2000();

	// Make sure we're a member of this guild, and that the guild is not owned by the system
	if (!pGuildMember || !pGuild || pGuild->bIsOwnedBySystem)
	{
		return false;
	}

	iRank = pGuildMember->iRank;

	// Cannot claim leadership if we are already the leader
	if (iRank == eaSize(&pGuild->eaRanks) - 1)
	{
		return false;
	}

	// If any guild member has logged in during the past thirty days
	// that is higher rank than us, we cannot claim leadership
	iGuildSize = eaSize(&pGuild->eaMembers);
	for (i = 0; i < iGuildSize; i++)
	{
		if (pGuild->eaMembers[i]->iRank > iRank && (pGuild->eaMembers[i]->bOnline || pGuild->eaMembers[i]->iLogoutTime + GUILD_LEADERSHIP_TIMEOUT > iTimestamp))
		{
			return false;
		}
	}

	return true;
}

bool guild_AccountIsMember(Guild* pGuild, unsigned int iAccountID)
{
	int i;
	for (i = 0; i < eaSize(&pGuild->eaMembers); i++)
	{
		if (pGuild->eaMembers[i]->iAccountID == iAccountID)
		{
			return true;
		}
	}
	return false;
}

#include "AutoGen/Guild_h_ast.c"
