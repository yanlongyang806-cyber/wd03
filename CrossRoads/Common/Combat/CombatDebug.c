/***************************************************************************
*     Copyright (c) 2000-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "CombatDebug.h"

#include "Character.h"
#include "EntityIterator.h"
#include "Player.h"

#include "AutoGen/AttribMod_h_ast.h"
#include "CharacterAttribs.h"
#include "CharacterAttribsMinimal_h_ast.h"
#include "CombatEval.h"
#include "GameAccountDataCommon.h"
#include "PowerActivation.h"
#include "itemCommon.h"
#include "inventorycommon.h"
#include "AutoGen/PowerActivation_h_ast.h"
#include "AutoGen/CombatDebug_h_ast.h"
#include "AutoGen/PowersEnums_h_ast.h"

#if GAMESERVER || GAMECLIENT
#include "PowersMovement.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// If the server should be writing its own CombatLog from CombatTrackerNets
S32 g_bCombatLogServer = false;
EntityRef g_erCombatDebugEntRef = 0;

// do not turn on EPowerDebugFlags_ENHANCEMENT by default since it is very spammy
static EPowerDebugFlags s_powerDebugFlags = (EPowerDebugFlags_ALL & ~EPowerDebugFlags_ENHANCEMENT);

// Enables server-side CombatLog.log
AUTO_COMMAND ACMD_CATEGORY(Powers,Debug) ACMD_SERVERONLY;
void CombatLogServer(S32 bEnabled)
{
	g_bCombatLogServer = !!bEnabled;
}

// Returns a new struct full of combat debugging info about the given ent
CombatDebug *combatdebug_GetData(EntityRef erDebugEnt)
{
	CombatDebug *pDbg = StructAlloc(parse_CombatDebug);
	Entity *e = entFromEntityRefAnyPartition(erDebugEnt);

	if(e)
	{
		Character *p = e->pChar;
		int iPartitionIdx = entGetPartitionIdx(e);
		pDbg->bValid = 1;
		pDbg->erDebugEnt = erDebugEnt;

		if(p)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(e);
			int i, s;

			pDbg->fTimerSleep = p->fTimerSleep;
			pDbg->fTimeSlept = p->fTimeSlept;

			// If the powers array needs to be reset, do that now
			if (p->bResetPowersArray)
			{
				character_ResetPowersArray(iPartitionIdx, p, pExtract);
				p->bResetPowersArray = false;
			}

			// Copy the powers
			s = eaSize(&p->ppPowers);
			MIN1(s,100);
			for(i=0; i<s; i++)
			{
				Power *ppow = StructAlloc(parse_Power);
				PowerDef *pdef;
				StructCopyFields(parse_Power,p->ppPowers[i],ppow,0,0);
				eaPush(&pDbg->ppPowers,ppow);
				
				pdef = GET_REF(ppow->hDef);
				if(pdef->eType == kPowerType_Innate)
				{
					int c;
					combateval_ContextSetupSimple(e->pChar, e->pChar->iLevelCombat, ppow->pSourceItem);

					for(c=0;c<eaSize(&pdef->ppOrderedMods);c++)
					{
						AttribMod *pmod = StructCreate(parse_AttribMod);

						pmod->uiApplyID=0;
						pmod->fMagnitude = mod_GetInnateMagnitude(entGetPartitionIdx(e),pdef->ppOrderedMods[c],e->pChar,character_GetClassCurrent(e->pChar),e->pChar->iLevelCombat,ppow->fTableScale);
						pmod->pDef = pdef->ppOrderedMods[c];
						SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdef,pmod->hPowerDef);
						pmod->uiDefIdx = c;
						pmod->bActive = true;

						//This copied from
						//StructCopyFields(parse_AttribModDef,pdef->ppOrderedMods[c],pmod,0,0);
						eaPush(&pDbg->ppMods,pmod);
					}
				}
			}

			// Copy innate powers from items
			if(e->pInventoryV2)
			{
				for(i=eaSize(&e->pInventoryV2->ppInventoryBags)-1; i>=0; i--)
				{
					BagIterator *iter = invbag_IteratorFromEnt(e,e->pInventoryV2->ppInventoryBags[i]->BagID, pExtract);
					for(; !bagiterator_Stopped(iter); bagiterator_Next(iter))
					{
						Item *pItem = (Item*)bagiterator_GetItem(iter);
						ItemDef *pItemDef = bagiterator_GetDef(iter);
						InventoryBag *pItemBag = (InventoryBag*)bagiterator_GetCurrentBag(iter);
						int iMinItemLevel = pItem ? item_GetMinLevel(pItem) : 0;
						
						S32 iPower;
						int NumPowers = item_GetNumItemPowerDefs(pItem, true);
						int GemPowers = item_GetNumGemsPowerDefs(pItem);

						if (!pItem || !pItemDef || !pItemBag)
							continue;

						// Check to make sure that the player can actually use this item, if it's not in a special bag
						if (!(invbag_flags(pItemBag) & InvBagFlag_SpecialBag) && (!(pItemDef->flags & kItemDefFlag_CanUseUnequipped) ||
							!itemdef_VerifyUsageRestrictions(iPartitionIdx, e, pItemDef, iMinItemLevel, NULL, -1)))
						{
							continue;
						}

						for(iPower=NumPowers-1; iPower>=0; iPower--)
						{
							F32 fItemScale = item_GetItemPowerScale(pItem, iPower);
							PowerDef *ppowdef = item_GetPowerDef(pItem, iPower);

							if ( !item_ItemPowerActive(e, pItemBag, pItem, iPower) )
								continue;

							if(ppowdef && ppowdef->eType == kPowerType_Innate)
							{
								int c;
								int iLevelPower = item_GetLevel(pItem) ? item_GetLevel(pItem) : e->pChar->iLevelCombat;
								NOCONST(Power) *ppow = StructCreateNoConst(parse_Power);
								SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,ppowdef,ppow->hDef);

								if(pItemDef->flags & kItemDefFlag_LevelFromQuality && iPower >= NumPowers - GemPowers)
								{
									iLevelPower = item_GetGemPowerLevel(pItem);
								}

								combateval_ContextSetupSimple(e->pChar, iLevelPower, ppow->pSourceItem);

								for(c=0;c<eaSize(&ppowdef->ppOrderedMods);c++)
								{
									AttribMod *pmod = StructCreate(parse_AttribMod);

									pmod->uiApplyID=0;
									pmod->fMagnitude = fItemScale * mod_GetInnateMagnitude(entGetPartitionIdx(e),ppowdef->ppOrderedMods[c],e->pChar,character_GetClassCurrent(e->pChar),iLevelPower,ppow->fTableScale);
									if (gConf.bRoundItemStatsOnApplyToChar)
									{
										pmod->fMagnitude = (F32)round(pmod->fMagnitude);
									}
									pmod->pDef = ppowdef->ppOrderedMods[c];
									SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,ppowdef,pmod->hPowerDef);
									pmod->uiDefIdx = c;
									pmod->bActive = true;

									//This copied from
									//StructCopyFields(parse_AttribModDef,pdef->ppOrderedMods[c],pmod,0,0);
									eaPush(&pDbg->ppMods,pmod);
								}

								eaPush(&pDbg->ppPowers,(Power*)ppow);
							}
						}
					}
					bagiterator_Destroy(iter);
				}
			}

			// Copy the current active power, if any
			if(e->pChar->pPowActCurrent)
			{
				PowerActivation *pactivate = StructCreate(parse_PowerActivation);
				StructCopyFields(parse_PowerActivation,e->pChar->pPowActCurrent,pactivate,0,0);
				pDbg->pactivation = pactivate;
			}
			
			// Copy the AttribMods
			s = eaSize(&p->modArray.ppMods);
			MIN1(s,100);
			for(i=0; i<s; i++)
			{
				AttribModDef *pdef = p->modArray.ppMods[i]->pDef;
				if (!p->modArray.ppMods[i]->bIgnored)
				{
					AttribMod *pmod = StructCreate(parse_AttribMod);
					StructCopyFields(parse_AttribMod,p->modArray.ppMods[i],pmod,0,0);
					eaPush(&pDbg->ppMods,pmod);
				}
			}

			// Copy the attributes
			pDbg->pattrBasic = StructCreate(parse_CharacterAttribs);
			StructCopyFields(parse_CharacterAttribs,p->pattrBasic,pDbg->pattrBasic,0,0);
			
			// Find the generic strengths and resists
			pDbg->pattrStr = StructCreate(parse_CharacterAttribs);
			pDbg->pattrRes = StructCreate(parse_CharacterAttribs);
			for(i=0; i<NUM_NORMAL_ATTRIBS; i++)
			{
				F32 fImm = 0;
				int offAttrib = i*SIZE_OF_NORMAL_ATTRIB;
				F32 fResTrue = 1.f;
				F32 fStr = character_GetStrengthGeneric(iPartitionIdx,p,offAttrib, NULL);
				F32 fRes = character_GetResistGeneric(iPartitionIdx,p,offAttrib,&fResTrue,&fImm);
				*F32PTR_OF_ATTRIB(pDbg->pattrStr,offAttrib) = fStr;
				*F32PTR_OF_ATTRIB(pDbg->pattrRes,offAttrib) = fResTrue ? fRes/fResTrue : 0.f;
			}
		}
	}

	return pDbg;
}

// Destroys an existing CombatDebug structure
void combatdebug_Destroy(CombatDebug *pDbg)
{
	if(pDbg) StructDestroy(parse_CombatDebug,pDbg);
}


// Number of Players with perf info enabled
U32 g_uiCombatDebugPerf = 0;

// Enable/disable perf tracking to the Player
void combatdebug_PerfEnable(Entity* pEnt, bool bEnable)
{
	PlayerDebug *pDebug = entGetPlayerDebug(pEnt, bEnable);
	if(pDebug && pDebug->combatDebugPerf != (U32)bEnable)
	{
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

		if(bEnable)
		{
			pDebug->combatDebugPerf = true;
		}
		else
		{
			pDebug->combatDebugPerf = false;
		}
	}
}

static StashTable s_stEventPerf = NULL;

// Tracks performance data
void combatdebug_PerfTrack(SA_PARAM_NN_STR char *pchEvent, U32 uiCount, U64 ulTime, U64 ulTimeSub)
{
	CombatDebugPerfEvent *pPerf = NULL;

	if(!s_stEventPerf)
	{
		s_stEventPerf = stashTableCreateWithStringKeys(32, StashDefault);
	}

	if(!stashFindPointer(s_stEventPerf,pchEvent,&pPerf))
	{
		pPerf = StructAlloc(parse_CombatDebugPerfEvent);
		pPerf->pchEvent = StructAllocString(pchEvent);
		stashAddPointer(s_stEventPerf,pchEvent,pPerf,true);
	}

	pPerf->uiCount += uiCount;
	pPerf->ulTime += ulTime;
	pPerf->ulTimeSub += ulTimeSub;
}

// Resets performance data
void combatdebug_PerfReset(void)
{
	if(s_stEventPerf)
	{
		StashTableIterator iter;
		StashElement elem;

		stashGetIterator(s_stEventPerf, &iter);

		while(stashGetNextElement(&iter, &elem))
		{
			StructDestroy(parse_CombatDebugPerfEvent,(CombatDebugPerfEvent*)stashElementGetPointer(elem));
		}
		stashTableClear(s_stEventPerf);
	}
}

// Updates the Player's combat perf data
void combatdebug_PerfPlayerUpdate(Entity* pEnt)
{
	if(s_stEventPerf)
	{
		PlayerDebug *pDebug = entGetPlayerDebug(pEnt, true);
		StashTableIterator iter;
		StashElement elem;

		devassert(pDebug);
		eaDestroyStruct(&pDebug->ppCombatEvents,parse_CombatDebugPerfEvent);

		stashGetIterator(s_stEventPerf, &iter);

		while(stashGetNextElement(&iter, &elem))
		{
			CombatDebugPerfEvent *pEvent = StructAlloc(parse_CombatDebugPerfEvent);
			StructCopyFields(parse_CombatDebugPerfEvent,(CombatDebugPerfEvent*)stashElementGetPointer(elem),pEvent,0,0);
			eaPush(&pDebug->ppCombatEvents,pEvent);
		}
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

void combatdebug_SetDebugFlagByName(const char* flagname)
{
	EPowerDebugFlags toggleFlags = StaticDefineIntGetInt(EPowerDebugFlagsEnum, flagname);

	if(toggleFlags == -1)
		return;

	s_powerDebugFlags ^= toggleFlags;
}


void combatdebug_PowersDebugPrint(Entity *e, S32 detailFlag, const char *format, ...)
{
	if ((detailFlag & s_powerDebugFlags) && 
		(!g_erCombatDebugEntRef || (g_erCombatDebugEntRef && e && e->myRef == g_erCombatDebugEntRef)))
	{
		static char *s_str = NULL;
		static char *s_noSrc = "<NO SOURCE>";
		S32 timeStamp = 0;

#if GAMESERVER || GAMECLIENT
		timeStamp = pmTimestamp(0);
#endif

		VA_START(args, format);
		estrConcatfv(&s_str, format, args);
		VA_END();
		
		printf("%d: %s %s\n", timeStamp, (e ? ENTDEBUGNAME(e) : s_noSrc), s_str);

		estrClear(&s_str);
	}
}

#include "AutoGen/CombatDebug_h_ast.c"
