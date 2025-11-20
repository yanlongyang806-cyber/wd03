/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "earray.h"
#include "cmdparse.h"

#include "UIGen.h"
#include "GameAccountDataCommon.h"
#include "GameClientLib.h"
#include "GameStringFormat.h"
#include "CharacterClass.h"
#include "CharacterAttribs.h"
#include "EntityLib.h"
#include "gclEntity.h"
#include "gclHUDOptions.h"
#include "EntitySavedData.h"
#include "Character.h"
#include "Powers.h"
#include "PowersAutoDesc.h"
#include "PowerTree.h"
#include "PowerVars.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "Expression.h"
#include "StringUtil.h"
#include "SavedPetCommon.h"
#include "Player.h"
#include "Character_Target.h"

#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"

#include "CharacterStatus.h"
#include "Autogen/CharacterStatus_c_ast.h"
#include "Autogen/AttribMod_h_ast.h"
#include "Autogen/entity_h_ast.h"
#include "Autogen/player_h_ast.h"

#include "ClientTargeting.h"
#include "Team.h"
#include "Guild.h"
#include "chatCommonStructs.h"
#include "GfxCommandParse.h"
#include "HUDOptionsCommon.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););



static AttribType s_aeStats[] = {kAttribType_StatHealth, kAttribType_StatDamage, kAttribType_StatPower};

AUTO_STRUCT;
typedef struct StatInfo
{
	AttribType eStat;
	F32 fLastTier;
	F32 fNextTier;
	F32 fValue;
	U32 iCurrentTier;
} StatInfo;

// put all the variables needed by the stats uigen in here
AUTO_STRUCT;
typedef struct PlayerStats
{
	int iPointsLeft;
	int iXP;
	int iNextLevelXP;
	int iReputation;
	int iPvPWins;
	int iPvPLosses;
	int iPvPGames;
	StatInfo **eaStats;
} PlayerStats;

AUTO_STRUCT;
typedef struct TitleWithCategory
{
	int bIsHeader;
	ItemDef* pItemDef;			AST(UNOWNED)	//if not bIsHeader.	
	const char* pchCategory;	AST(POOL_STRING)	//if bIsHeader.
} TitleWithCategory;

static PlayerStats s_Stats = {0, 0, 0, 0, 0, 0, 0, NULL};

AUTO_RUN;
void initPlayerStatsVar(void)
{
	ui_GenInitPointerVar("PlayerStats", parse_PlayerStats);
	ui_GenSetPointerVar("PlayerStats", &s_Stats, parse_PlayerStats);

	if (!s_Stats.eaStats)
	{
		S32 i;
		for (i = 0; i < ARRAY_SIZE(s_aeStats); i++)
		{
			AttribType eStat = s_aeStats[i];
			StatInfo *si = calloc(1, sizeof(StatInfo));
			si->eStat = eStat;
			eaPush(&s_Stats.eaStats, si);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("MoveStatPointsAround");
void MoveStatPointsAround(bool LockWeaponPower, bool LockEnginePower, bool LockShieldPower, bool LockAuxiliaryPower, bool LockStrength, bool LockAgility, int interval, float newvalue, int max, int min, const char* TargetKey)
{
	S32 index = StaticDefineIntGetInt(AttribTypeEnum, TargetKey);
	Entity *pEnt = entActivePlayerPtr();
	F32 value = 0;
	if (pEnt && pEnt->pChar)
	{
		int i;
		//value = *F32PTR_OF_ATTRIB(pEnt->pChar->pattrBasic,index);
		for(i=0;i<eaSize(&pEnt->pChar->ppAssignedStats);i++)
		{
			if(pEnt->pChar->ppAssignedStats[i]->eType == index)
			{
				value = pEnt->pChar->ppAssignedStats[i]->iPoints;
				break;
			}
		}
		if (newvalue > value)
		{
			newvalue = (interval * round(newvalue/interval));
			ServerCmd_MoveStatPointsAroundAdd(LockWeaponPower, LockEnginePower, LockShieldPower, LockAuxiliaryPower, LockStrength, LockAgility, interval, (int)newvalue, max, min, TargetKey);
		}
		else if (newvalue < value)
		{
			newvalue = (interval * round(newvalue/interval));
			ServerCmd_MoveStatPointsAroundSubtract(LockWeaponPower, LockEnginePower, LockShieldPower, LockAuxiliaryPower, LockStrength, LockAgility, interval, (int)newvalue, max, min, TargetKey);
		}
	}
}

// Get the value of a numeric item.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetInventoryNumeric");
S32 gclExprEntGetInventoryNumeric(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItem)
{
	if (pEnt)
		return inv_GetNumericItemValue(pEnt, pchItem);
	else
		return 0;
}

// Get the value of a numeric item, as a string.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetInventoryNumericString");
const char *gclExprEntGetInventoryNumericString(SA_PARAM_OP_VALID Entity *pEnt, const char *pchItem)
{
	if (pEnt)
	{
		S32 iValue = inv_GetNumericItemValue(pEnt, pchItem);
		static unsigned char *s_pchValue;
		estrClear(&s_pchValue);
		FormatGameString(&s_pchValue, "{Value}", STRFMT_INT("Value", iValue), STRFMT_END);
		return s_pchValue;
	}
	else
		return "0";
}

// Get the list of buffs on the player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CharacterStatus_GetBuffList");
void CharacterStatus_GetBuffList(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pEnt = entActivePlayerPtr();
	if (pEnt)
		ui_GenSetList(pGen, &pEnt->pChar->modArray.ppMods, parse_AttribMod);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CharacterStatus_GetBuffText");
const char *CharacterStatus_GetBuffText(SA_PARAM_NN_VALID AttribMod *pMod)
{
	PowerDef *pPowerDef = GET_REF(pMod->hPowerDef);
	if (!pPowerDef)
		return TranslateMessageKey("UI.Loading");

	return powerdef_GetLocalName(pPowerDef);
}

static void GetAttributeList(SA_PARAM_NN_VALID UIGen *pGen, const char* pchKeys, S32 bExcludeZeros)
{
	static char *s_estrKeys = NULL;
	static AttribStat s_AttribCache[kAttribType_LAST] = {0};
	static AttribStat **s_eaAttribs = NULL;
	char *apchKeys[128];
	int count;
	int i;
	Character *pCharacter = characterActivePlayerPtr();

	if(!s_eaAttribs)
	{
		// I don't think this is the right way to do this. It doesn't handle
		//   special attribs, mainly. Also, since the enum is actually an offset
		//   it has a lot of holes in it, which means a bunch of unused space in
		//   this array.
		// It would probably be better to use a stash table which is added to
		//   on the fly. But that's an improvement for a different day. --poz
		for(i=0; i<kAttribType_LAST; i++)
		{
			s_AttribCache[i].pchKeyName = StaticDefineIntRevLookup(AttribTypeEnum, i);
			if(s_AttribCache[i].pchKeyName)
			{
				estrPrintf(&s_AttribCache[i].estrNameMessage,"AutoDesc.AttribName.%s",s_AttribCache[i].pchKeyName);
				estrPrintf(&s_AttribCache[i].estrDescMessage,"AutoDesc.AttribDesc.%s",s_AttribCache[i].pchKeyName);
			}
		}
	}

	eaClear(&s_eaAttribs);

	if(!pchKeys || pchKeys[0]=='\0' || !pCharacter)
		return;

	estrCopy2(&s_estrKeys, pchKeys);
	count = tokenize_line(s_estrKeys, apchKeys, NULL);
	for(i=0; i<count; i++)
	{
		int index = StaticDefineIntGetInt(AttribTypeEnum, apchKeys[i]);
		if(index >= 0)
		{
			F32 fCurrent = *F32PTR_OF_ATTRIB(pCharacter->pattrBasic,index);

			if(bExcludeZeros && fCurrent==0)
				continue;

			eaPush(&s_eaAttribs, &s_AttribCache[index]);

			s_AttribCache[index].fBase = character_GetClassAttrib(pCharacter,kClassAttribAspect_Basic,index);
			s_AttribCache[index].fCurrent = fCurrent;

			if(attrib_AutoDescIsPercent(index))
			{
				s_AttribCache[index].fBase *= 100.f;
				s_AttribCache[index].fCurrent *= 100.f;
			}
		}
	}

	ui_GenSetManagedListSafe(pGen, &s_eaAttribs, AttribStat, false);
}

// Gets all attributes which have a non-zero value from the list of space delimeted keys
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CharacterStatus_GetAttributeListNonZero");
void CharacterStatus_GetAttributeListNonZero(SA_PARAM_NN_VALID UIGen *pGen, const char* pchKeys)
{
	GetAttributeList(pGen,pchKeys,true);
}

// Gets all attributes from the list of space delimeted keys
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CharacterStatus_GetAttributeList");
void CharacterStatus_GetAttributeList(SA_PARAM_NN_VALID UIGen *pGen, const char* pchKeys)
{
	GetAttributeList(pGen,pchKeys,false);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntTitleCount");
int GenExprEntTitleCount(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	int iResult;
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);

	iResult = inv_ent_CountItems(pEntity, InvBagIDs_Titles, NULL, pExtract);

	return iResult;
}

static S32 TitleCategoryComparator(const ItemDef **a, const ItemDef **b)
{
	if (a && b)
	{
		if (*a && !*b)
			return 1;
		else if (!*a && *b)
			return -1;
		else
		{
			// compare enum values of first categories.  Category-less goes first.
			if (eaiSize(&(*a)->peCategories) == 0 && eaiSize(&(*b)->peCategories) > 0)
				return -1;
			if (eaiSize(&(*a)->peCategories) > 0 && eaiSize(&(*b)->peCategories) == 0)
				return 1;
			if (eaiSize(&(*a)->peCategories) > 0 && eaiSize(&(*b)->peCategories) > 0)
			{
				int ret = (*b)->peCategories[0] - (*a)->peCategories[0];
				if (ret)
					return ret;
			}
			//same or no category; alphabetize them:
			{
				const char* pchNameA = TranslateDisplayMessage((*a)->displayNameMsg);
				const char* pchNameB = TranslateDisplayMessage((*b)->displayNameMsg);
				return stricmp(pchNameA, pchNameB);
			}
		}
	}
	return 0;
}

void EntGetTitlesFiltered(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID ItemDef ***peaFilteredItemList, SA_PARAM_OP_VALID Entity *pEntity, const char* pchFilter)
{
	static char *s_pchDisplayName;
	if (pEntity && peaFilteredItemList)

	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		BagIterator* pIter = invbag_LiteIteratorFromEnt(pEntity, InvBagIDs_Titles, pExtract);
		for (; !bagiterator_Stopped(pIter); bagiterator_Next(pIter))
		{
			bool bUnfiltered = true;
			ItemDef* pDef = bagiterator_GetDef(pIter);
			const char* pchItemName = pDef ? itemdef_GetNameLang(&s_pchDisplayName, pDef, locGetLanguage(getCurrentLocale()), pEntity) : NULL;
			char* pchFilterCopy;
			char* pchContext;
			char* pchStart;

			if (!pDef)
				continue;

			// Only need to do this if there's a filter
			if (pchFilter != NULL && pchFilter[0] != '\0')
			{
				// For all whitespace delimited strings in pchFilter, check if that string is a substring of the name
				strdup_alloca(pchFilterCopy, pchFilter);
				pchStart = strtok_r(pchFilterCopy, " ,\t\r\n", &pchContext);
				do
				{
					if (pchStart && !strstri(pchItemName, pchStart))
					{
						bUnfiltered = false;
						break;
					}
				} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
			}

			if (bUnfiltered)
			{
				eaPush(peaFilteredItemList, pDef);
			}
		}
		bagiterator_Destroy(pIter);
	}
	eaQSort(*peaFilteredItemList, TitleCategoryComparator);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTitlesFiltered");
void GenExprEntGetTitlesFiltered(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char* pchFilter)
{
	ItemDef ***peaFilteredItemList = ui_GenGetManagedListSafe(pGen, ItemDef);
	eaClearFast(peaFilteredItemList);
	EntGetTitlesFiltered(pContext, peaFilteredItemList, pEntity, pchFilter);

	// this is an awful hack. A Gen can't build a NULL pointer, so this adds a NULL RowData, which the gen catches, labels "no title",
	// and potentially passes to EntSetTitle.  NW is using EntClearTitle() and EntGetTitlesWithCategories() instead.
	eaInsert(peaFilteredItemList, NULL, 0);

	ui_GenSetListSafe(pGen, peaFilteredItemList, ItemDef);
}

//Gets the titles list, wraps them all in TitleWithCategory structs, and adds dummy members for category headers.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTitlesWithCategories");
void GenExprEntGetTitlesWithCategories(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ItemDef **eaFilteredItemList = NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	TitleWithCategory ***peaFilteredItemListWithHeaders = ui_GenGetManagedListSafe(pGen, TitleWithCategory);
	int i;
	const char* curCategory = NULL;
	eaDestroyStruct(peaFilteredItemListWithHeaders, parse_TitleWithCategory);
	EntGetTitlesFiltered(pContext, &eaFilteredItemList, pEntity, "");
	for (i = 0; i<eaSize(&eaFilteredItemList); i++)
	{
		TitleWithCategory* pTitle = StructCreate(parse_TitleWithCategory);
		if (eaFilteredItemList[i]->peCategories && eaFilteredItemList[i]->peCategories[0])
		{
			const char* pchCategory = StaticDefineGetTranslatedMessage(ItemCategoryEnum, eaFilteredItemList[i]->peCategories[0]);
			if (stricmp(curCategory, pchCategory))
			{
				//new category, add header.
				TitleWithCategory* pCategoryHeader = StructCreate(parse_TitleWithCategory);
				pCategoryHeader->bIsHeader = true;
				pCategoryHeader->pchCategory = allocAddString(pchCategory);
				eaPush(peaFilteredItemListWithHeaders, pCategoryHeader);
				curCategory = pchCategory;
			}
		}
		pTitle->bIsHeader = false;
		pTitle->pItemDef = eaFilteredItemList[i];
		eaPush(peaFilteredItemListWithHeaders, pTitle);
	}
	ui_GenSetListSafe(pGen, peaFilteredItemListWithHeaders, TitleWithCategory);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTitles");
void GenExprEntGetTitles(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	GenExprEntGetTitlesFiltered(pContext, pGen, pEntity, "");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetCategorizedTitles");
void GenExprEntGetCategorizedTitles(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	GenExprEntGetTitlesFiltered(pContext, pGen, pEntity, "");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntClearTitle");
void GenExprEntClearTitle(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	if (!pEntity || pEntity != entActivePlayerPtr())
	{
		return;
	}
	ServerCmd_SetCurrentTitle("");
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntSetTitle");
void GenExprEntSetTitle(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID ItemDef *pItemDef)
{
	if (!pEntity || pEntity != entActivePlayerPtr())
	{
		return;
	}

	if(!pItemDef)
	{
		//Clear the title
		ServerCmd_SetCurrentTitle("");
	}
	else
	{
		//Set the title to the item def's name
		ServerCmd_SetCurrentTitle(pItemDef->pchName);
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntHasTitle");
bool GenExprEntHasTitle(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	return pEntity && pEntity->pPlayer && GET_REF(pEntity->pPlayer->pTitleMsgKey);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetTitle");
const char *GenExprEntGetTitle(SA_PARAM_NN_VALID ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	static char *s_pch;

	estrClear(&s_pch);

	if (pEntity && GenExprEntHasTitle(pContext, pEntity))
	{
		FormatGameString(&s_pch, "{Player.Title}", STRFMT_PLAYER(pEntity), STRFMT_END);
		return exprContextAllocString(pContext, s_pch);
	}

	return "";
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsValidDescription");
bool exprEntIsValidDescription( Entity *pEnt, const char *pchDescription )
{
	return ( !StringIsInvalidDescription(pchDescription) );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntSetServerDescription");
void GenExprSetServerDescription(const char *pchDescription)
{
	ServerCmd_EntSetDescription(pchDescription);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenEntSetPetDescription");
void GenExprSetPetDescription(U32 petType, U32 petID, const char *pchDescription)
{
	ServerCmd_EntSetPetDescription(petType, petID, pchDescription);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsSoftTarget");
bool EntIsSoftTarget(SA_PARAM_NN_VALID Entity *pEnt)
{
	const ClientTargetDef *targ = clientTarget_IsTargetHard() ? NULL : clientTarget_GetCurrentTarget();
	if (targ)
	{
		if (targ->entRef == entGetRef(pEnt))
		{
			return true;
		}
	}
	return false;
}

static bool IsInChatList(ChatPlayerStruct ***peaList, U32 accountID)
{
	if (peaList)
	{
		int i;

		for (i=0; i < eaSize(peaList); i++)
		{
			ChatPlayerStruct *pPlayer = (*peaList)[i];
			if (pPlayer && pPlayer->accountID == accountID)
			{
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsMyPet");
bool EntIsMyPet(SA_PARAM_NN_VALID Entity *pEnt)
{
	Entity *pMasterEntity = entActivePlayerPtr();

	if(!pEnt || !pMasterEntity)
		return false;

	if ( pEnt->myEntityType == GLOBALTYPE_ENTITYCRITTER )
		return pEnt->erOwner == entGetRef(pMasterEntity);

	return (	pEnt->pSaved
			&&	pEnt->pSaved->conOwner.containerID == entGetContainerID(pMasterEntity)
			&&	pEnt->pSaved->conOwner.containerType == entGetType(pMasterEntity));
}

static OverHeadEntityFlags entGetShowFlags(Entity *pEnt)
{
	Entity *pPlayerEntity = entActivePlayerPtr();
	OverHeadEntityFlags eFlags = 0;
	PlayerHUDOptions *pHUDOptions;
	EntityRelation eRelation;

	// First off: Nothing at all for unselectable entities.
	// (this used to check unselectable OR untargetable.  Untargetable seems unnecessary and heavy-handed.  I cleared this change with Champs, but if you
	// are getting some names you didn't expect, this is probably why, and you should come talk about it.) [RMARR - 3/28/12]
	if(entGetFlagBits(pEnt) & ENTITYFLAG_UNSELECTABLE)
	{
		return 0;
	}

	if(!pPlayerEntity || !pPlayerEntity->pPlayer || !pPlayerEntity->pPlayer->pUI || !pPlayerEntity->pPlayer->pUI->pLooseUI)
	{
		return 0;
	}

	// does this entity have at least one flag true to qualify for this?
	//pSO = &pPlayerEntity->pPlayer->pUI->pLooseUI->showOverhead;
	pHUDOptions = entGetCurrentHUDOptions(pPlayerEntity);

	if (!pHUDOptions)
	{
		return 0;
	}

	//if this is the player's entity, just return the "show self" flags
	if (pEnt == pPlayerEntity)
	{
		return pHUDOptions->ShowOverhead.eShowSelf;
	}

	if (team_OnSameTeam(pEnt, pPlayerEntity))
		eFlags |= pHUDOptions->ShowOverhead.eShowTeam;

	eRelation = entity_GetRelation(PARTITION_CLIENT, pPlayerEntity, pEnt);

	if (pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER || (pEnt->myEntityType == GLOBALTYPE_ENTITYPUPPET && entGetRef(pPlayerEntity) == pEnt->erOwner))
	{
		if(eRelation == kEntityRelation_Foe)
			eFlags |= pHUDOptions->ShowOverhead.eShowEnemyPlayer;
		else
			eFlags |= pHUDOptions->ShowOverhead.eShowPlayer;
	}
	else
	{
		if(eRelation == kEntityRelation_Foe)
			eFlags |= pHUDOptions->ShowOverhead.eShowEnemy;
		else if(eRelation == kEntityRelation_Friend)
			eFlags |= pHUDOptions->ShowOverhead.eShowFriendlyNPC;
	}

	if(guild_InSameGuild(pEnt, pPlayerEntity))
		eFlags |= pHUDOptions->ShowOverhead.eShowSupergroup;

	if(pEnt->pPlayer && IsInChatList(&pPlayerEntity->pPlayer->pUI->pChatState->eaFriends, pEnt->pPlayer->accountID))
		eFlags |= pHUDOptions->ShowOverhead.eShowFriends;

	if(entGetOwner(pEnt) == pPlayerEntity)
		eFlags |= pHUDOptions->ShowOverhead.eShowPet;

	return eFlags;
}

static PlayerHUDOptionsPowerMode *entGetPowerModeOptions(Entity *pEnt)
{
	if (g_DefaultHUDOptions.eaPowerModeOptions)
	{
		Entity *pPlayerEntity = entActivePlayerPtr();
		PlayerHUDOptions *pHUDOptions = entGetCurrentHUDOptions(pPlayerEntity);
		PlayerHUDOptionsPowerMode *pPowerModeOption = NULL;
		int i;

		if (!pHUDOptions)
			return NULL;

		for (i = eaSize(&g_DefaultHUDOptions.eaPowerModeOptions) - 1; i >= 0; i--)
		{
			if (g_DefaultHUDOptions.eaPowerModeOptions[i]->eRegion == pHUDOptions->eRegion)
			{
				pPowerModeOption = g_DefaultHUDOptions.eaPowerModeOptions[i];
				break;
			}
		}

		if (pPowerModeOption)
		{
			for (i = eaiSize(&pPowerModeOption->eaiPowerModes) - 1; i >= 0; i--)
			{
				if (character_HasMode(pEnt->pChar, pPowerModeOption->eaiPowerModes[i]))
				{
					return pPowerModeOption;
				}
			}
		}
	}

	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntShowOverhead");
bool EntShowOverhead(SA_PARAM_NN_VALID Entity *pEnt, bool bMouseOver)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	OverHeadEntityFlags eFlags = OVERHEAD_ENTITY_FLAG_ALWAYS;
	const ClientTargetDef *pTarget;
	OverHeadEntityFlags eMustMatch = entGetShowFlags(pEnt);
	PlayerHUDOptionsPowerMode *pPowerModeOption = entGetPowerModeOptions(pEnt);

	if (gclEntGetIsContact(pPlayerEnt, pEnt))
		eFlags |= OVERHEAD_ENTITY_FLAG_ALWAYS_NAME_CONTACTS;

	pTarget = clientTarget_GetCurrentTarget();
	if(pTarget)
	{
		Entity *pTargetEnt = entFromEntityRefAnyPartition(pTarget->entRef);

		if(pTarget->entRef == entGetRef(pEnt))
			eFlags |= pPowerModeOption ? OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODE : OVERHEAD_ENTITY_FLAG_TARGETED;

		if(entity_GetTargetRef(pTargetEnt) == entGetRef(pEnt))
			eFlags |= pPowerModeOption ? OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODE : OVERHEAD_ENTITY_FLAG_TARGETOFTARGET;
	}

	if(pEnt->pEntUI && gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastDamaged < 5000)
	{
		eFlags |= pPowerModeOption ? OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODE : OVERHEAD_ENTITY_FLAG_DAMAGED;
	}

	// If we can match without doing the mouse checks, don't do the mouse checks.
	if (eFlags & eMustMatch)
		return true;
	else if (eMustMatch & (OVERHEAD_ENTITY_FLAG_MOUSE_OVER | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODE))
		// If broad-phase mouse check fails, don't do a raycast either.
		return bMouseOver && entMouseRayCanHitPlayer(NULL, pEnt, false);
	else
		return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntShowOverheadPart");
bool EntShowOverheadPart(SA_PARAM_NN_VALID Entity *pEnt, bool bMouseOver, int part)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	OverHeadEntityFlags eFlags = 0;
	const ClientTargetDef *pTarget;
	OverHeadEntityFlags eMustMatch = entGetShowFlags(pEnt);
	PlayerHUDOptionsPowerMode *pPowerModeOption = entGetPowerModeOptions(pEnt);

	switch (part)
	{
	case 0: eFlags = pPowerModeOption && pPowerModeOption->bEnableName ? OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODENAME : OVERHEAD_ENTITY_FLAG_ALWAYS_NAME; break;
	case 1: eFlags = pPowerModeOption && pPowerModeOption->bEnableLife ? OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODELIFE : OVERHEAD_ENTITY_FLAG_ALWAYS_LIFE; break;
	case 2: eFlags = pPowerModeOption && pPowerModeOption->bEnableReticle ? OVERHEAD_ENTITY_FLAG_ALWAYS_POWERMODERETICLE : OVERHEAD_ENTITY_FLAG_ALWAYS_RETICLE; break;
	}

	if (gclEntGetIsContact(pPlayerEnt, pEnt) && part==0)
		eFlags |= OVERHEAD_ENTITY_FLAG_ALWAYS_NAME_CONTACTS;

	pTarget = clientTarget_GetCurrentTarget();
	if(pTarget)
	{
		Entity *pTargetEnt = entFromEntityRefAnyPartition(pTarget->entRef);

		if(pTarget->entRef == entGetRef(pEnt))
			switch (part)
			{
			case 0: eFlags |= pPowerModeOption && pPowerModeOption->bEnableName ? OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODENAME : OVERHEAD_ENTITY_FLAG_TARGETED_NAME; break;
			case 1: eFlags |= pPowerModeOption && pPowerModeOption->bEnableLife ? OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODELIFE : OVERHEAD_ENTITY_FLAG_TARGETED_LIFE; break;
			case 2: eFlags |= pPowerModeOption && pPowerModeOption->bEnableReticle ? OVERHEAD_ENTITY_FLAG_TARGETED_POWERMODERETICLE : OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE; break;
			}

		if(entity_GetTargetRef(pTargetEnt) == entGetRef(pEnt))
			switch (part)
			{
			case 0: eFlags |= pPowerModeOption && pPowerModeOption->bEnableName ? OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODENAME : OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_NAME; break;
			case 1: eFlags |= pPowerModeOption && pPowerModeOption->bEnableLife ? OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODELIFE : OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_LIFE; break;
			case 2: eFlags |= pPowerModeOption && pPowerModeOption->bEnableReticle ? OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_POWERMODERETICLE : OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_RETICLE; break;
			}
	}

	if(pEnt->pEntUI && (gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastDamaged) < 5000)
	{
		switch (part)
		{
		case 0: eFlags |= pPowerModeOption && pPowerModeOption->bEnableName ? OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODENAME : OVERHEAD_ENTITY_FLAG_DAMAGED_NAME; break;
		case 1: eFlags |= pPowerModeOption && pPowerModeOption->bEnableLife ? OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODELIFE : OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE; break;
		case 2: eFlags |= pPowerModeOption && pPowerModeOption->bEnableReticle ? OVERHEAD_ENTITY_FLAG_DAMAGED_POWERMODERETICLE : OVERHEAD_ENTITY_FLAG_DAMAGED_RETICLE; break;
		}
	}

	// If we can match without doing the mouse checks, don't do the mouse checks.
	if (eFlags & eMustMatch)
		return true;
	else if (part == 0 && (eMustMatch & (OVERHEAD_ENTITY_FLAG_MOUSE_OVER_NAME | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODENAME)))
		// If broad-phase mouse check fails, don't do a raycast either.
		return bMouseOver && entMouseRayCanHitPlayer(NULL, pEnt, false);
	else if (part == 1 && (eMustMatch & (OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODELIFE)))
		// If broad-phase mouse check fails, don't do a raycast either.
		return bMouseOver && entMouseRayCanHitPlayer(NULL, pEnt, false);
	else if (part == 2 && (eMustMatch & (OVERHEAD_ENTITY_FLAG_MOUSE_OVER_RETICLE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_POWERMODERETICLE)))
		// If broad-phase mouse check fails, don't do a raycast either.
		return bMouseOver && entMouseRayCanHitPlayer(NULL, pEnt, false);
	else
		return false;
}

static F32 gclEntTimeSinceLastFlank(SA_PARAM_NN_VALID Entity *pEnt, F32* pfTime)
{
	if(pEnt && pEnt->pEntUI && pEnt->pEntUI->uiLastFlank)
	{
		F32 fTime = (gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastFlank) / 1000.0;
		if ( pfTime && fTime < (*pfTime) )
		{
			(*pfTime) = fTime;
		}
		return fTime;
	}
	return FLT_MAX;
}

static void gclEntTimeSinceLastFlankNoReturn(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, F32* pfTime)
{
	gclEntTimeSinceLastFlank(pEnt, pfTime);
}

// Returns the elapsed time since the Entity was last affected by
//  a combat event involving a Flank, or FLT_MAX in the case
//  they've never been affected.
AUTO_EXPR_FUNC(entityutil);
F32 EntTimeSinceLastFlank(SA_PARAM_NN_VALID Entity *pEnt)
{
	return gclEntTimeSinceLastFlank( pEnt, NULL );
}

AUTO_EXPR_FUNC(entityutil);
F32 EntTimeSinceLastFlankIncludePets(SA_PARAM_NN_VALID Entity *pEnt)
{
	F32 fTime = FLT_MAX;
	gclEntTimeSinceLastFlank( pEnt, &fTime );
	Entity_ForEveryPet( PARTITION_CLIENT, pEnt, gclEntTimeSinceLastFlankNoReturn, &fTime, true, true );
	return fTime;
}

static F32 gclEntTimeSinceLastDamaged(SA_PARAM_NN_VALID Entity *pEnt, F32* pfTime)
{
	if(pEnt && pEnt->pEntUI && pEnt->pEntUI->uiLastDamaged)
	{
		F32 fTime = (gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastDamaged) / 1000.0;
		if ( pfTime && fTime < (*pfTime) )
		{
			(*pfTime) = fTime;
		}
		return fTime;
	}
	return FLT_MAX;
}

static void gclEntTimeSinceLastDamagedNoReturn(int iPartitionIdx, SA_PARAM_NN_VALID Entity *pEnt, F32* pfTime)
{
	gclEntTimeSinceLastDamaged(pEnt, pfTime);
}

AUTO_EXPR_FUNC(entityutil);
F32 EntTimeSinceLastDamaged(SA_PARAM_NN_VALID Entity *pEnt)
{
	return gclEntTimeSinceLastDamaged( pEnt, NULL );
}

// Returns the angle (in degrees)
AUTO_EXPR_FUNC(entityutil);
F32 EntLastDamageAngle(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pEntUI)
	{
		return DEG(pEnt->pEntUI->fLastDamageAngle);
	}
	return -1.0f;
}

// Returns the angle (in degrees)
AUTO_EXPR_FUNC(entityutil);
F32 EntLastDamageTangentAngle(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pEntUI)
	{
		return DEG(pEnt->pEntUI->fLastDamageTangentAngle);
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(entityutil);
F32 EntTimeSinceLastDamagedIncludePets(SA_PARAM_NN_VALID Entity *pEnt)
{
	F32 fTime = FLT_MAX;
	gclEntTimeSinceLastDamaged( pEnt, &fTime );
	Entity_ForEveryPet( PARTITION_CLIENT, pEnt, gclEntTimeSinceLastDamagedNoReturn, &fTime, true, true );
	return fTime;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntWasObject");
bool EntWasObject(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (pEnt)
	{
		WorldInteractionNode *pNode = GET_REF(pEnt->hCreatorNode);
		if (pNode)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ClientHasSoftTarget");
bool ClientHasSoftTarget()
{
	return clientTarget_IsTargetSoft();
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ClientHasHardTarget");
bool ClientHasHardTarget()
{
	return clientTarget_IsTargetHard();
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ClientGetHardTargetEntity");
SA_RET_OP_VALID Entity* ClientGetHardTargetEntity()
{
	const ClientTargetDef* pTargetDef = clientTarget_GetCurrentHardTarget();
	return entFromEntityRefAnyPartition(pTargetDef->entRef);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsHardTarget");
bool EntIsHardTarget(SA_PARAM_NN_VALID Entity *pEnt)
{
	const ClientTargetDef *targ = clientTarget_IsTargetHard() ? clientTarget_GetCurrentTarget() : NULL;
	if (targ)
	{
		if (targ->entRef == entGetRef(pEnt))
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntIsOwnerOnMyTeam");
bool EntIsOwnerOnMyTeam(SA_PARAM_NN_VALID Entity *pEnt)
{
	Entity *pMasterEntity = NULL;
	Entity *pPlayerEntity = entActivePlayerPtr();

	if(!pEnt || !pPlayerEntity || !pPlayerEntity->pTeam)
		return false;

	if (entGetType(pEnt) == GLOBALTYPE_ENTITYCRITTER)
	{
		pMasterEntity = entFromEntityRefAnyPartition(pEnt->erOwner);
	}
	else if (pEnt->pSaved)
	{
		pMasterEntity =
			entFromContainerIDAnyPartition(pEnt->pSaved->conOwner.containerType,pEnt->pSaved->conOwner.containerID);
	}

	if(pMasterEntity && pMasterEntity->pTeam && pMasterEntity->pTeam->iTeamID == pPlayerEntity->pTeam->iTeamID)
		return true;

	return false;
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterStatus_GetSortedStatDescription");
const char *CharacterStatus_GetSortedStatDescription(SA_PARAM_NN_STR const char *pchAttrib, const char* pchPrimaryHeader, const char* pchSecondaryHeader, const char* pchSortTag)
{
	static char *pchDesc = NULL;
	Entity *e = entActivePlayerPtr();
	S32 attrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttrib);
	Language lang = langGetCurrent();

	estrClear(&pchDesc);

	if(e && e->pChar)
		attrib_AutoDescPowerStats(&pchDesc,attrib,e->pChar,lang,entGetPowerAutoDescDetail(e,true), pchPrimaryHeader, pchSecondaryHeader, pchSortTag);

	if(!(pchDesc && *pchDesc))
	{
		estrCopy2(&pchDesc,attrib_AutoDescName(attrib,lang));
	}

	return pchDesc;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterStatus_GetStatDescription");
const char *CharacterStatus_GetStatDescription(SA_PARAM_NN_STR const char *pchAttrib)
{
	return CharacterStatus_GetSortedStatDescription(pchAttrib, NULL, NULL, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterStatus_GetStatLongDescription");
const char *CharacterStatus_GetStatLongDescription(SA_PARAM_NN_STR const char *pchAttrib)
{
	S32 attrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttrib);
	return attrib_AutoDescDescLong(attrib,langGetCurrent());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterStatus_GetStatShortDescription");
const char *CharacterStatus_GetStatShortDescription(SA_PARAM_NN_STR const char *pchAttrib)
{
	S32 attrib = StaticDefineIntGetInt(AttribTypeEnum, pchAttrib);
	return attrib_AutoDescDesc(attrib,langGetCurrent());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterStatus_GetCurrentAttribStatsPreset");
const char* CharacterStatus_GetCurrentAttribStatsPreset( SA_PARAM_OP_VALID Entity* pEntity )
{
	if ( pEntity && pEntity->pChar )
	{
		return pEntity->pChar->pchCurrentAttribStatsPreset;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CharacterStatus_GetAttribStatsPresetDisplayName");
const char* CharacterStatus_GetAttribStatsPresetDisplayName( const char* pchPreset )
{
	if ( pchPreset && pchPreset[0] )
	{
		AttribStatsPresetDef* pDef = attribstatspreset_GetDefByName( pchPreset );

		if ( pDef->pDisplayMessage )
			return TranslateDisplayMessage( (*pDef->pDisplayMessage) );
	}

	return "";
}

// Fill in the global stats structure for the current player.
AUTO_EXPR_FUNC(UIGen);
void PlayerStats_BeforeTick(void);

// Fill in the global stats structure for the current player.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void PlayerStats_BeforeTick(void)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 i;

	if (!pEnt)
		return;

	s_Stats.iPointsLeft = entity_GetAssignedStatUnspent(CONTAINER_NOCONST(Entity, pEnt), STAT_POINT_POOL_DEFAULT);
	s_Stats.iXP = item_GetLevelingNumeric(pEnt);
	s_Stats.iNextLevelXP = entity_ExpOfNextExpLevel(pEnt);
	s_Stats.iReputation = inv_GetNumericItemValue(pEnt, "RP");
	s_Stats.iPvPWins = inv_GetNumericItemValue(pEnt, "PvP_Win_Credit");
	s_Stats.iPvPLosses = inv_GetNumericItemValue(pEnt, "PvP_Loss_Credit");
	s_Stats.iPvPGames = inv_GetNumericItemValue(pEnt, "PvP_Game_Credit");

	for (i = 0; i < ARRAY_SIZE(s_aeStats); i++)
	{
		AttribType eStat = s_aeStats[i];
		CharacterAttribs *pAttribs = pEnt->pChar->pattrBasic;
		CharacterClass *pClass = GET_REF(pEnt->pChar->hClass);
		if (pClass)
		{
			PowerTable *pTable = powertable_FindInClass("StatTier",pClass);
			if (pTable)
			{
				F32 fValue = *(F32 *)(((char *)pAttribs) + eStat);
				F32 fLastValue = 0;
				F32 fNextValue = -1;
				S32 j;
				for (j = 0; j < eafSize(&pTable->pfValues) && fNextValue <= 0; j++)
				{
					if (fValue < pTable->pfValues[j])
					{
						StatInfo *si = s_Stats.eaStats[i];
						assert(si->eStat == eStat);
						si->fNextTier = pTable->pfValues[j];
						si->fLastTier = j ? pTable->pfValues[j - 1] : 0;
						si->fValue = fValue;
						si->iCurrentTier = j;
						break;
					}
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TestOverheadFlagForNode");
bool exprTestOverheadFlagForNode(const char *pcFlag)
{
	int flagToTest;
	Entity *pPlayerEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pPlayerEnt);
	OverHeadEntityFlags savedFlags = pHUDOptions ? pHUDOptions->ShowOverhead.eShowEnemy & OVERHEAD_ENTITY_FLAG_TARGETED : 0;

	if(!pPlayerEnt || !pcFlag) return false;

	if(!stricmp(pcFlag, "Name")) {
		flagToTest = OVERHEAD_ENTITY_FLAG_ALWAYS_NAME | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_NAME | OVERHEAD_ENTITY_FLAG_TARGETED_NAME | OVERHEAD_ENTITY_FLAG_DAMAGED_NAME;
	} else if(!stricmp(pcFlag, "LifeBar")) {
		flagToTest = OVERHEAD_ENTITY_FLAG_ALWAYS_LIFE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE | OVERHEAD_ENTITY_FLAG_TARGETED_LIFE | OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE;
	} else if(!stricmp(pcFlag, "Reticle")) {
		flagToTest = OVERHEAD_ENTITY_FLAG_ALWAYS_RETICLE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_RETICLE | OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE | OVERHEAD_ENTITY_FLAG_DAMAGED_RETICLE;
	} else {
		flagToTest = StaticDefineIntGetInt(OverHeadEntityFlagsEnum, pcFlag);
	}

	return !!(flagToTest & savedFlags);
}

// Test the player's overhead settings for the given entity.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("TestOverheadFlag");
bool exprTestOverheadFlag(SA_PARAM_NN_VALID Entity *pEnt, bool mouse_over, const char *pcFlag)
{
	int flagToTest;
	Entity *pPlayerEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pPlayerEnt);
	//Player *player = pPlayerEnt ? pPlayerEnt->pPlayer : NULL;
	OverHeadEntityFlags savedFlags = 0;
	bool bDualTarget = (pEnt == entity_GetTargetDual(pPlayerEnt));
	const ClientTargetDef *targ;
	bool recentlyDamaged = (pEnt && pEnt->pEntUI && gGCLState.totalElapsedTimeMs - pEnt->pEntUI->uiLastDamaged < 5000);

	if(!pcFlag)
		return false;

	if(!stricmp(pcFlag, "Name")) {
		flagToTest = OVERHEAD_ENTITY_FLAG_ALWAYS_NAME | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_NAME | OVERHEAD_ENTITY_FLAG_TARGETED_NAME | OVERHEAD_ENTITY_FLAG_DAMAGED_NAME | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_NAME;
	} else if(!stricmp(pcFlag, "LifeBar")) {
		flagToTest = OVERHEAD_ENTITY_FLAG_ALWAYS_LIFE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_LIFE | OVERHEAD_ENTITY_FLAG_TARGETED_LIFE | OVERHEAD_ENTITY_FLAG_DAMAGED_LIFE | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_LIFE;
	} else if(!stricmp(pcFlag, "Reticle")) {
		flagToTest = OVERHEAD_ENTITY_FLAG_ALWAYS_RETICLE | OVERHEAD_ENTITY_FLAG_MOUSE_OVER_RETICLE | OVERHEAD_ENTITY_FLAG_TARGETED_RETICLE | OVERHEAD_ENTITY_FLAG_DAMAGED_RETICLE | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET_RETICLE;
	} else {
		flagToTest = StaticDefineIntGetInt(OverHeadEntityFlagsEnum, pcFlag);
	}

	if(flagToTest == -1) return false;

	if(!pPlayerEnt || !pPlayerEnt->pPlayer || !pHUDOptions || !pEnt) return false;

	if(pEnt->pChar && gclEntGetIsFoe(pPlayerEnt, pEnt)) {
		// This is an enemy.
		savedFlags |= pHUDOptions->ShowOverhead.eShowEnemy;
	}

	if(pEnt == pPlayerEnt) {

		// This is the current player
		savedFlags |= pHUDOptions->ShowOverhead.eShowSelf;

	} else if(pEnt->pPlayer) {

		if(pEnt->myEntityType == GLOBALTYPE_ENTITYPLAYER || (pEnt->myEntityType == GLOBALTYPE_ENTITYPUPPET && entGetRef(pPlayerEnt) == pEnt->erOwner)) {
			// This is another player.
			savedFlags |= pHUDOptions->ShowOverhead.eShowPlayer;

			if (gclEntGetIsFoe(pPlayerEnt, pEnt)) {
				// Hostile player
				savedFlags |= pHUDOptions->ShowOverhead.eShowEnemyPlayer;
			}
		}

		if(guild_InSameGuild(pEnt, pPlayerEnt)) {
			// Same supergroup.
			savedFlags |= pHUDOptions->ShowOverhead.eShowSupergroup;
		}

		if(IsInChatList(&pPlayerEnt->pPlayer->pUI->pChatState->eaFriends, pEnt->pPlayer->accountID)) {
			// Friends list.
			savedFlags |= pHUDOptions->ShowOverhead.eShowFriends;
		}

		if(team_OnSameTeam(pEnt, pPlayerEnt)) {
			// Same team as the player.
			savedFlags |= pHUDOptions->ShowOverhead.eShowTeam;
		}

	} else {

		if(pEnt->pChar && gclEntGetIsFriend(pPlayerEnt, pEnt)) {
			// This is a friendly.
			savedFlags |= pHUDOptions->ShowOverhead.eShowFriendlyNPC;
		}

	}

	if(entGetOwner(pEnt) == pPlayerEnt) {
		// Is a pet.
		savedFlags |= pHUDOptions->ShowOverhead.eShowPet;
	}

	if (mouse_over)
		mouse_over = entMouseRayCanHitPlayer(NULL, pEnt, pEnt != pPlayerEnt);

	if(!mouse_over)
		savedFlags &= ~OVERHEAD_ENTITY_FLAG_MOUSE_OVER;

	targ = clientTarget_GetCurrentTarget();
	if(targ) {
		Entity *targetEnt = entFromEntityRefAnyPartition(targ->entRef);

		if (targ->entRef != entGetRef(pEnt) && !bDualTarget) {
			// Not the current target.
			savedFlags &= ~OVERHEAD_ENTITY_FLAG_TARGETED;
		}

		if(entity_GetTargetRef(targetEnt) != entGetRef(pEnt)) {
			// Not the target of the current target.
			savedFlags &= ~OVERHEAD_ENTITY_FLAG_TARGETOFTARGET;
		}
	} else if (bDualTarget) {
		// Only have a second target.
		savedFlags &= ~OVERHEAD_ENTITY_FLAG_TARGETOFTARGET;
	} else {
		// Nothing targeted at all.
		savedFlags &= ~(OVERHEAD_ENTITY_FLAG_TARGETED | OVERHEAD_ENTITY_FLAG_TARGETOFTARGET);
	}

	if(!recentlyDamaged) {
		savedFlags &= ~OVERHEAD_ENTITY_FLAG_DAMAGED;
	}

	if(savedFlags & flagToTest) {
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetShowPlayerTitles");
bool exprGetShowPlayerTitles(void)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);

	if(!pHUDOptions) return false;

	return pHUDOptions->ShowOverhead.bShowPlayerTitles;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetShowPlayerRoles");
bool exprGetShowPlayerRoles(void) 
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);

	if(!pHUDOptions) return false;

	return pHUDOptions->ShowOverhead.bShowPlayerRoles;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetShowInteractionIcons");
bool exprGetShowInteractionIcons(void)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);

	if(!pHUDOptions) return false;

	return pHUDOptions->ShowOverhead.bShowInteractionIcons;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetShowCriticalStatusOnReticle");
bool exprGetShowCriticalStatusOnReticle(void)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);

	if(!pHUDOptions) return false;

	return pHUDOptions->ShowOverhead.bShowCriticalStatusInfo;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetShowReticlesAs");
bool exprGetShowReticlesAs(const char *pcFlag)
{
	Entity* pEnt = entActivePlayerPtr();
	PlayerHUDOptions* pHUDOptions = entGetCurrentHUDOptions(pEnt);
	OverHeadReticleFlags flags = StaticDefineIntGetInt(OverHeadReticleFlagsEnum, pcFlag);

	// If we aren't doing outlining, test as though the box option is selected.
	if(!gfxDoingOutlining()) {
		if(flags & OVERHEAD_RETICLE_BOX) {
			return true;
		}
		return false;
	}

	if(!pHUDOptions) return false;

	return !!(pHUDOptions->ShowOverhead.eShowReticlesAs & flags);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetStasisTime");
int exprGetStasisTime(void)
{
	Player *player = playerActivePlayerPtr();

	if(!player) return 0;

	if(player->iStasis < timeServerSecondsSince2000()) {
		return 0;
	} else {
		return player->iStasis - timeServerSecondsSince2000();
	}
}

//////////////////////////////////////////////////////////////////////////
// Character Forcefield Information.  Used in Champs.
//////////////////////////////////////////////////////////////////////////

AUTO_STRUCT;
typedef struct ForceFieldStruct
{
	EntityRef iRef;		AST(KEY)
	U32 uiLastUpdateTime;
	S32 iHealth;
	S32 iHealthMax;
	F32 fHealthPercent;
} ForceFieldStruct;

static ForceFieldStruct **eaForceFields = NULL;

// Entity and pchar are expected to be non-null, valid pointers in this helper
static ForceFieldStruct *UpdateForceFieldHealth(Entity *pEntity)
{
	static S32 bOnce = false;
	static int eTag = 0;
	ForceFieldStruct *pHealth = NULL;
	if(!bOnce)
	{
		eTag = StaticDefineIntGetInt(PowerTagsEnum, "ForceField");
		eaIndexedEnable(&eaForceFields, parse_ForceFieldStruct);
		bOnce = true;
	}

	pHealth = eaIndexedGetUsingInt(&eaForceFields, entGetRef(pEntity));

	if(!pHealth || pHealth->uiLastUpdateTime != gGCLState.totalElapsedTimeMs )
	{
		int i;
		Character *pchar = pEntity->pChar;

		if(!pHealth)
		{
			pHealth = StructCreate(parse_ForceFieldStruct);
			pHealth->iRef = entGetRef(pEntity);
			eaPush(&eaForceFields, pHealth);
		}
		pHealth->uiLastUpdateTime = gGCLState.totalElapsedTimeMs;

		pHealth->iHealth = 0;
		pHealth->iHealthMax = 0;
		pHealth->fHealthPercent = 0.f;

		for(i=eaSize(&pchar->ppModsNet)-1; i>=0; i--)
		{
			AttribModDef *pdef;
			AttribModNet *pmodNet = pchar->ppModsNet[i];
			PowerDef *ppowerDef = NULL;

			if(!ATTRIBMODNET_VALID(pmodNet))
				continue;

			pdef = modnet_GetDef(pmodNet);

			if(pdef && (pmodNet->iHealth || pmodNet->iHealthMax) &&
				//pmodNet->uiDuration > 0 &&
				powertags_Check(&pdef->tags,eTag))
			{
				pHealth->iHealthMax += pmodNet->iHealthMax == 0.f ? pmodNet->iHealth : pmodNet->iHealthMax;
				pHealth->iHealth += pmodNet->iHealth;
			}
		}
		if(pHealth->iHealthMax > 0)
		{
			pHealth->fHealthPercent = (float)pHealth->iHealth / (float)pHealth->iHealthMax;
			MIN1(pHealth->fHealthPercent, 100.f);
		}
	}
	return(pHealth);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetForceFieldHealthMax");
S32 exprEntGetForceFieldHealthMax(SA_PARAM_OP_VALID Entity *pEntity)
{
	S32 iHealthMax = 0;
	if(pEntity && pEntity->pChar)
	{
		ForceFieldStruct *pHealth = UpdateForceFieldHealth(pEntity);
		if(pHealth)
			iHealthMax = pHealth->iHealthMax;
	}

	return(iHealthMax);
}
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetForceFieldHealth");
S32 exprEntGetForceFieldHealth(SA_PARAM_OP_VALID Entity *pEntity)
{
	S32 iHealth = 0;
	if(pEntity && pEntity->pChar)
	{
		ForceFieldStruct *pHealth = UpdateForceFieldHealth(pEntity);
		if(pHealth)
		iHealth = pHealth->iHealth;
	}

	return(iHealth);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetForceFieldHealthPct");
F32 exprEntGetForceFieldHealthPct(SA_PARAM_OP_VALID Entity *pEntity)
{
	F32 fHealthPct = 0;
	if(pEntity && pEntity->pChar)
	{
		ForceFieldStruct *pHealth = UpdateForceFieldHealth(pEntity);
		if(pHealth)
			fHealthPct = pHealth->fHealthPercent;
	}

	return(fHealthPct);
}

#include "Autogen/CharacterStatus_c_ast.c"
