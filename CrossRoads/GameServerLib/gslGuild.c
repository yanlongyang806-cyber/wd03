/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "gslGuild.h"

#include "StringCache.h"
#include "AutoTransDefs.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "LoggedTransactions.h"
#include "logging.h"
#include "TransactionOutcomes.h"

#include "Entity.h"
#include "EntityLib.h"
#include "Player.h"
#include "Guild.h"
#include "GameAccountDataCommon.h"

#include "AutoGen/Player_h_ast.h"
#include "AutoGen/Guild_h_ast.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

// Return whether the entity is a member of the guild
static bool gslGuild_IsMember(Guild *pGuild, U32 iEntID) 
{
	if (eaIndexedGetUsingInt(&pGuild->eaMembers, iEntID)) 
	{
		return true;
	}
	return false;
}

// Returns the guild container matching the guild ID
static Guild * gslGuild_GetGuild(ContainerID iGuildID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_GUILD, iGuildID);
	if (pContainer) 
	{
		return (Guild *)pContainer->containerData;
	} 
	else 
	{
		return NULL;
	}
}

AUTO_COMMAND_REMOTE;
void gslGuild_ResetGuildStats(U32 iGuildID, U32 iEntID)
{
	Guild *pGuild = gslGuild_GetGuild(iGuildID);

	if (pGuild && gslGuild_IsMember(pGuild, iEntID)) 
	{
		AutoTrans_gslGuild_tr_ResetGuildStats(NULL, GetAppGlobalType(), GLOBALTYPE_GUILD, iGuildID);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pguildstatsinfo, .Pcname");
enumTransactionOutcome gslGuild_tr_ResetGuildStats(ATR_ARGS, NOCONST(Guild) *pGuildContainer)
{
	if (ISNULL(pGuildContainer))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid guild container.");
	}
	else
	{
		if (NONNULL(pGuildContainer->pGuildStatsInfo))
		{
			StructDestroyNoConst(parse_GuildStatsInfo, pGuildContainer->pGuildStatsInfo);
			pGuildContainer->pGuildStatsInfo = NULL;
		}
		TRANSACTION_RETURN_LOG_SUCCESS("Guild stats are reset for guild '%s'", pGuildContainer->pcName);
	}

}

AUTO_COMMAND_REMOTE;
void gslGuild_UpdateGuildStat(U32 iGuildID, U32 iEntID, const char *pchStatName, GuildStatUpdateOperation eOperation, S32 iValue)
{
	Guild *pGuild = gslGuild_GetGuild(iGuildID);
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);

	if (pEnt && pGuild && gslGuild_IsMember(pGuild, iEntID))
	{
		ANALYSIS_ASSUME(pEnt != NULL);
		AutoTrans_gslGuild_tr_UpdateGuildStat(NULL, GLOBALTYPE_GAMESERVER, entGetType(pEnt), iEntID, pchStatName, eOperation, iValue);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pguildstatsinfo");
enumTransactionOutcome gslGuild_tr_UpdateGuildStat(ATR_ARGS, NOCONST(Guild) *pGuildContainer, const char *pchStatName, S32 eOperation, S32 iValue)
{
	if (NONNULL(pGuildContainer))
	{
		// Make sure the pchStatName is a valid stat name
		GuildStatDef *pGuildStatDef = RefSystem_ReferentFromString(g_GuildStatDictionary, pchStatName);

		if (pGuildStatDef)
		{
			// Check if this operation is supported for this guild stat
			if ((pGuildStatDef->eValidOperations & eOperation) != 0)
			{
				S32 iStatIndex = -1;
				bool bCreatedStatsInfo = false;

				NOCONST(GuildStat) *pGuildStat = NULL;

				// Create the stats info if it does not already exist
				if (ISNULL(pGuildContainer->pGuildStatsInfo))
				{
					pGuildContainer->pGuildStatsInfo = StructCreateNoConst(parse_GuildStatsInfo);
					bCreatedStatsInfo = true;
				}

				// Try to find the stat entry
				if (bCreatedStatsInfo || 
					(iStatIndex = eaIndexedFindUsingString(&pGuildContainer->pGuildStatsInfo->eaGuildStats, pchStatName)) < 0)
				{
					pGuildStat = StructCreateNoConst(parse_GuildStat);
					pGuildStat->pchStatName = allocAddString(pchStatName);
					pGuildStat->iValue = pGuildStatDef->iInitialValue;

					// Add to the array
					eaPush(&pGuildContainer->pGuildStatsInfo->eaGuildStats, pGuildStat);
				}
				else
				{
					pGuildStat = pGuildContainer->pGuildStatsInfo->eaGuildStats[iStatIndex];
				}

				// At this point pGuildStat should be set to the stat we want to modify
				switch (eOperation)
				{
				case GuildStatUpdateOperation_Add:
					pGuildStat->iValue += iValue;
					break;
				case GuildStatUpdateOperation_Subtract:
					pGuildStat->iValue -= iValue;
					break;
				case GuildStatUpdateOperation_Max:
					pGuildStat->iValue = MAX(pGuildStat->iValue, iValue);
					break;
				case GuildStatUpdateOperation_Min:
					pGuildStat->iValue = MIN(pGuildStat->iValue, iValue);
					break;
				}

				// Update the version of the guild stats info
				pGuildContainer->pGuildStatsInfo->uiVersion++;

				return TRANSACTION_OUTCOME_SUCCESS;
			}
			else
			{
				TRANSACTION_RETURN_LOG_FAILURE("Invalid guild stat update operation attempt on stat '%s'. Attempted operation: %s", pchStatName, StaticDefineIntRevLookup(GuildStatUpdateOperationEnum, eOperation));
			}
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("Invalid stat '%s' for guild stat update operation.", pchStatName);
		}
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("No guild container specified.");
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pGuildContainer, ".Pguildstatsinfo, .Htheme");
enumTransactionOutcome gslGuild_tr_SetGuildTheme(ATR_ARGS, NOCONST(Guild) *pGuildContainer, const char *pchThemeName)
{
	if (NONNULL(pGuildContainer))
	{
		if (pchThemeName && pchThemeName[0] && RefSystem_ReferentFromString(g_GuildThemeDictionary, pchThemeName))
		{
			SET_HANDLE_FROM_STRING(g_GuildThemeDictionary, pchThemeName, pGuildContainer->hTheme);

			// Create the stats info if it does not already exist
			if (pGuildContainer->pGuildStatsInfo == NULL)
			{
				pGuildContainer->pGuildStatsInfo = StructCreateNoConst(parse_GuildStatsInfo);
			}

			// Update the version of the guild stats info
			pGuildContainer->pGuildStatsInfo->uiVersion++;

			return TRANSACTION_OUTCOME_SUCCESS;
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("Invalid guild theme specified.");
		}
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("No guild container specified.");
	}
}

// See if this character qualifies to added to an auto guild
void gslGuild_AutoJoinGuild(Entity *pEntity)
{
	if(pEntity && pEntity->pPlayer && gGuildConfig.pcAutoName && gGuildConfig.pcAutoName[0])
	{
		GameAccountData *pGameAccount;
		// check to see if this player should join a guild

		// check if in a guild
		if(pEntity->pPlayer->iGuildID || pEntity->pPlayer->pGuild)
		{
			// already in a guild or was in guild
			return;
		}

		// check level
		if(entity_GetSavedExpLevel(pEntity) > 1)
		{
			// not first level
			return;
		}

		// check time played. If its enough then skip as this is an experienced player
		pGameAccount = entity_GetGameAccount(pEntity);
		if(pGameAccount)
		{
			if(!gGuildConfig.bAddDevs || (entGetAccessLevel(pEntity) == 0 && !pEntity->pPlayer->bIsDev))
			{
				if(pGameAccount->uTotalPlayedTime_AccountServer >= SECONDS_PER_HOUR * 8)
				{
					// player is experienced, don't add to newb guild
					return;
				}
			}
		}

		// Do add remote command
		RemoteCommand_aslGuild_AddToAutoGuild(GLOBALTYPE_GUILDSERVER, 0, pEntity->myContainerID);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE ACMD_SERVERONLY ACMD_CATEGORY(Interface);
void gslGuild_AllowInvites(Entity *pEnt, bool bAllowInvites)
{
	if (SAFE_MEMBER2(pEnt, pPlayer, pUI))
	{
		pEnt->pPlayer->pUI->bDisallowGuildInvites = !bAllowInvites;
		entity_SetDirtyBit(pEnt, parse_PlayerUI, pEnt->pPlayer->pUI, true);
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}