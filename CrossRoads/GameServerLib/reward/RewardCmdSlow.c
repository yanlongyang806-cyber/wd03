#include "Entity.h"
#include "GameAccountDataCommon.h"
#include "Reward.h"
#include "player.h"
#include "file.h"
#include "utils.h"
#include "NotifyCommon.h"
#include "inventoryCommon.h"

// Take all loot currently being viewed and store it in the specified bag.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(loot_InteractTakeAll) ACMD_PRIVATE;
void loot_InteractTakeAllSpecifyBagCmd(Entity* pPlayerEnt, S32 iBagID)
{
	loot_InteractTakeAll(pPlayerEnt,iBagID, true);
}

//take just one item from the loot bag
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(loot_InteractTake) ACMD_PRIVATE;
void loot_InteractTakeCmd( Entity* pPlayerEnt, U64 iID, S32 iBagID, S32 iDstSlot )
{
	ItemChangeReason reason = {0};

	inv_FillItemChangeReason(&reason, pPlayerEnt, "Loot:TakeItemViaInteract", NULL);

	loot_InteractTake(pPlayerEnt, iID, iBagID, iDstSlot, &reason);
}

// Take all loot of the specified type currently being viewed and store it in the specified bag.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(loot_InteractTakeAllOfType) ACMD_PRIVATE;
void loot_InteractTakeAllOfTypeSpecifyBagCmd(Entity* pPlayerEnt, S32 eItemType, S32 iBagID)
{
	loot_InteractTakeAllOfType(pPlayerEnt, eItemType, iBagID);
}

// Take all loot of the specified type currently being viewed and store it in the specified bag.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(loot_InteractTakeAllExceptType) ACMD_PRIVATE;
void loot_InteractTakeAllExceptTypeSpecifyBagCmd(Entity* pPlayerEnt, S32 eItemType, S32 iBagID)
{
	loot_InteractTakeAllExceptType(pPlayerEnt, eItemType, iBagID);
}

// Reset this gate type to zero, clear time
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_NAME(RewardResetGatedType);
void Reward_ResetGatedType(Entity* pPlayerEnt, ACMD_NAMELIST(RewardGatedTypeEnum, STATICDEFINE) const char* gateType)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer && gateType)
	{
		RewardGatedTypeData *pPlayerGated = eaIndexedGetUsingString(&pPlayerEnt->pPlayer->eaRewardGatedData, gateType);
		if(pPlayerGated)
		{
			pPlayerGated->uNumTimes = 0;
			pPlayerGated->uTimeSet = 0;
			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, true);
		}
	}
}

// Reset all reward gated types to zero (destroy them)
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_NAME(RewardResetGatedTypeAll);
void Reward_ResetGatedTypeAll(Entity* pPlayerEnt)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer)
	{
		eaDestroyStruct(&pPlayerEnt->pPlayer->eaRewardGatedData, parse_RewardGatedTypeData);
		entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, true);
	}
}

// Set gated type (by ID) to this value
AUTO_COMMAND ACMD_ACCESSLEVEL(4) ACMD_SERVERCMD ACMD_NAME(RewardSetGatedType);
void Reward_SetGatedType(Entity* pPlayerEnt, U32 gatedType, U32 uCount)
{
	if(pPlayerEnt && pPlayerEnt->pPlayer && gatedType)
	{
		RewardGatedInfo *pRewardGatedInfo = eaIndexedGetUsingInt(&g_RewardConfig.eRewardGateInfo, gatedType);
		if(pRewardGatedInfo)
		{
			RewardGatedTypeData *pPlayerGated = eaIndexedGetUsingString(&pPlayerEnt->pPlayer->eaRewardGatedData, StaticDefineIntRevLookup(RewardGatedTypeEnum, gatedType));

			if(pPlayerGated)
			{
				pPlayerGated->uNumTimes = uCount;
				pPlayerGated->uTimeSet = timeSecondsSince2000();
				entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, true);
			}
			else
			{
				RewardGatedTypeData *pNewGatedData = StructCreate(parse_RewardGatedTypeData);

				pNewGatedData->eType = gatedType;
				pNewGatedData->uNumTimes = uCount;
				pNewGatedData->uTimeSet = timeSecondsSince2000();

				if(!pPlayerEnt->pPlayer->eaRewardGatedData)
				{
					eaIndexedEnable(&pPlayerEnt->pPlayer->eaRewardGatedData, parse_RewardGatedTypeData);
				}

				eaIndexedAdd(&pPlayerEnt->pPlayer->eaRewardGatedData, pNewGatedData);
			}
		}
	}
}

// Set gated type (by ID) to this value
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(RewardTableOdds);
void Reward_RewardTableOdds(Entity* pPlayerEnt, const char *pchPath, ACMD_NAMELIST("RewardTable", REFDICTIONARY) const char *reward_table_name, bool bUseEnt)
{
	RewardTable *pReward = RefSystem_ReferentFromString(g_hRewardTableDict,reward_table_name);
	char *eaOut = NULL;
	if(pReward)	
	{
		RewardOdds theOdds = {0};
		FILE* out;
		const char* pchRowEnd = "\r\n";
		S32 i;

		// init file
		makeDirectoriesForFile(pchPath);
		if(fileIsAbsolutePath(pchPath))
		{
			out = fopen(pchPath, "w");
		} else
		{
			out = fileOpen(pchPath, "w");
		}

		if (!out)
		{
			estrPrintf(&eaOut, "RewardTableOdds Error: Can't open file %s.",pchPath);
			notify_NotifySend(pPlayerEnt, kNotifyType_Failed, eaOut, NULL, NULL);
			estrDestroy(&eaOut);
			return;
		}

		if(bUseEnt)
		{
			reward_GenerateOdds(pReward, &theOdds, 0.0f, 0, pPlayerEnt);
		}
		else
		{
			reward_GenerateOdds(pReward, &theOdds, 0.0f, 0, NULL);
		}

		// write out the data
		estrPrintf(&eaOut, "Reward Table: %s%s%s", reward_table_name, pchRowEnd,pchRowEnd);
		fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
		if(theOdds.bUnableToCalculate)
		{
			estrPrintf(&eaOut, "Table contains expressions and or range tables, can't calculate odds.%s",pchRowEnd);
			fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
		}
		else
		{
			bool bAddLine = false;
			estrPrintf(&eaOut, "Entry chances:%s%s", pchRowEnd,pchRowEnd);
			fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
			for(i = 0; i < eaSize(&theOdds.eaEntries); ++i)
			{
				if(theOdds.eaEntries[i]->bIsItem)
				{
					if(GET_REF(theOdds.eaEntries[i]->pRewardEntry->hItemDef))
					{
						ItemDef *pItemDef = GET_REF(theOdds.eaEntries[i]->pRewardEntry->hItemDef);
						estrPrintf(&eaOut, "%.5f%% chance of %s %s%s", theOdds.eaEntries[i]->fChance * 100.0f, StaticDefineIntRevLookup(RewardTypeEnum, theOdds.eaEntries[i]->pRewardEntry->Type), pItemDef->pchName, pchRowEnd);
						fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
					}
					else
					{
						estrPrintf(&eaOut, "%.5f%% chance of %s%s", theOdds.eaEntries[i]->fChance * 100.0f, StaticDefineIntRevLookup(RewardTypeEnum, theOdds.eaEntries[i]->pRewardEntry->Type), pchRowEnd);
						fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
					}
				}
				else if(GET_REF(theOdds.eaEntries[i]->pRewardEntry->hRewardTable))
				{
					RewardTable *pIncTable = GET_REF(theOdds.eaEntries[i]->pRewardEntry->hRewardTable);
					estrPrintf(&eaOut, "%.5f%% chance of table %s%s", theOdds.eaEntries[i]->fChance * 100.0f, pIncTable->pchName, pchRowEnd);
					fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
				}
			}

			estrPrintf(&eaOut, "%sItem generation:%s%s", pchRowEnd, pchRowEnd,pchRowEnd);
			fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);

			if(theOdds.fTotalItemsGiven > 0.000001f)
			{
				estrPrintf(&eaOut, "Item total: %f%s",theOdds.fTotalItemsGiven, pchRowEnd);
				fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
				bAddLine = true;
			}
			if(theOdds.fTotalNumericsGiven > 0.000001f)
			{
				estrPrintf(&eaOut, "Numeric total: %f%s", theOdds.fTotalNumericsGiven, pchRowEnd);
				fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
				bAddLine = true;
			}

			if(bAddLine)
			{
				estrPrintf(&eaOut, "%s", pchRowEnd);
				fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
			}

			for(i = 0; i < eaSize(&theOdds.eaItems); ++i)
			{
				estrPrintf(&eaOut, "%.7f of %s%s", theOdds.eaItems[i]->fTotalQuantity, theOdds.eaItems[i]->pcItemName, pchRowEnd);
				fwrite(eaOut, estrLength(&eaOut), sizeof(char), out);
			}
		}

		StructDeInit(parse_RewardOdds, &theOdds);

		fclose(out);
	}
	else
	{
		estrPrintf(&eaOut, "RewardTableOdds Error: Can't find reward table %s.",reward_table_name);
		notify_NotifySend(pPlayerEnt, kNotifyType_Failed, eaOut, NULL, NULL);
	}
	estrDestroy(&eaOut);
}
