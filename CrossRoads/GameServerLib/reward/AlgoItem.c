/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "gslSendToClient.h"
#include "utils.h"

#include "textparser.h"
#include "earray.h"
#include "fileutil.h"
#include "foldercache.h"
#include "referencesystem.h"
#include "rand.h"
#include "StringCache.h"

#include "Entity.h"
#include "entCritter.h"

#include "itemCommon.h"
#include "inventoryTransactions.h"
#include "inventoryCommon.h"
#include "rewardCommon.h"
#include "reward.h"
#include "algoitem.h"
#include "AlgoItemCommon.h"

#include "AlgoItem_h_ast.h"
#include "itemEnums_h_ast.h"

AlgoTables g_AlgoTables;


static void AlgoTables_ReloadCallback( const char *relpath, int when)
{
	loadstart_printf("Reloading AlgoTables...");

	StructDeInit(parse_AlgoTables, &g_AlgoTables);
	StructInit(parse_AlgoTables, &g_AlgoTables);

	ParserLoadFiles( NULL, "defs/rewards/algotables.data", "algotables.bin", 0, parse_AlgoTables, &g_AlgoTables);	

	loadend_printf(" done.");
}


AUTO_STARTUP(AlgoTables);
void AlgoTables_Load(void)
{
	loadstart_printf("Loading AlgoTables...");

	StructInit(parse_AlgoTables, &g_AlgoTables);

	ParserLoadFiles( NULL, "defs/rewards/algotables.data", "algotables.bin", 0, parse_AlgoTables, &g_AlgoTables);	

	loadend_printf(" done." );

	if (isDevelopmentMode())
	{
		// Have reload take effect immediately
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/rewards/algotables.data", AlgoTables_ReloadCallback);
	}
}

static ItemDef * algobase_generate(int iPartitionIdx, RewardContext *pContext, RewardTable *reward_table, U32 *pSeed)
{
	RewardEntry *algo_base = NULL;
	RewardContext *pLocalContext = NULL;
	ItemDef *pItemDef = NULL;
	InventoryBag **ppRewardBags = NULL;

	//validate parameters
	if  ( !pContext ||
		  !reward_table )
	{
		return NULL;
	}

	pLocalContext = StructClone(parse_RewardContext, pContext);
	pLocalContext->pAlgoRewards = calloc(sizeof(AlgoRewardContext),1);
	pLocalContext->type = RewardContextType_AlgoBase;
	
	// use player level flag items will have this value
	if(pContext->iPlayerLevelForItem > 0)
	{
		pLocalContext->RewardLevel = pContext->iPlayerLevelForItem;
	}

	reward_generate(iPartitionIdx, NULL, pLocalContext, reward_table, &ppRewardBags, NULL, pSeed);

	if (eaSize(&pLocalContext->pAlgoRewards->ppBaseItems) == 1)
	{
		pItemDef = pLocalContext->pAlgoRewards->ppBaseItems[0];
	}
	
	// Destroy the algo-rewards on the local context (it's marked as NO_AST)
	eaDestroy(&pLocalContext->pAlgoRewards->ppBaseItems);
	eaDestroy(&pLocalContext->pAlgoRewards->ppExtras);
	eaDestroy(&pLocalContext->pAlgoRewards->ppCostumes);
	free(pLocalContext->pAlgoRewards);
	
	// Destroy the local context
	StructDestroy(parse_RewardContext, pLocalContext);

	// Destroy the reward bags
	eaDestroyStruct(&ppRewardBags, parse_InventoryBag);
	return pItemDef;
}


/*
	alwaysGenerate ignores the hard-coded droprate calculation and will give you an algoitem 100% of the time.
	Otherwise, the function will return -1 quite frequently due to the fact that the item drop rate is inexplicably intertwined with the rarity determination.
*/
ItemQuality algoitem_GetRandomQuality(int i, const char *pcRank, U32* pSeed, bool alwaysGenerate)
{
	int TabIdx;
	AlgoTableDef *pAlgoTable = NULL;
	AlgoEntryDef* pAlgoEntry = NULL;
	int result = 0;

	TabIdx = eaIndexedFindUsingString(&g_AlgoTables.ppAlgoTables, pcRank);

	if (TabIdx<0)
		return kItemQuality_None;

	pAlgoTable = (AlgoTableDef*)eaGet(&g_AlgoTables.ppAlgoTables, TabIdx);

	if (!pAlgoTable)
		return kItemQuality_None;

	for(; i >= 0; --i)
	{
		pAlgoEntry = (AlgoEntryDef*)eaGet(&pAlgoTable->ppAlgoEntry, i);

		if (pAlgoEntry)
			break;
	}
	if(!pAlgoEntry)
		return kItemQuality_None;

	{
		F32 weightSum = 0;
		F32 ChoiceRoll = 0;
		F32 WeightWalker = 0;
		int ii;

		/*
			Okay, here's what's going on here: this value ALGO_TOTAL_WEIGHT exists to
			make up for the fact that algoitems are put into reward tables with a 100% drop rate.
			The combination of excel sheets and xls2algotables.exe basically spits out numbers which are then
			compared to a flat value of 10000 to determine whether they drop at all, and if so, what rarity they should be.
		*/
		if (alwaysGenerate)
		{
			for (ii = 0; ii < NUM_ALGO_COLORS; ii++)
				weightSum += (F32)pAlgoEntry->weight[ii];
		}
		ChoiceRoll = randomPositiveF32Seeded(pSeed, RandType_LCG) * (alwaysGenerate ? weightSum : ALGO_TOTAL_WEIGHT);
		//update all reward entries
		for(ii=0; ii<NUM_ALGO_COLORS; ii++)
		{
			WeightWalker += (F32)pAlgoEntry->weight[ii];

			if ( ChoiceRoll <= WeightWalker )
			{
				result = ii;

				//done with this choice
				break;
			}
		}

		if (ii>=NUM_ALGO_COLORS)
		{
			return -1;
		}
	}
	return result;
}

Item* algoitem_generate(int iPartitionIdx, RewardContext *pContext, U32 *pSeed)
{
    int i;
	RewardEntry *pAlgoBaseEntry = NULL;
	RewardEntry *pAlgoCharEntry = NULL;
	RewardEntry *pAlgoCostEntry = NULL;
	NOCONST(Item)* pItem = NULL;
	Critter *pCritter = NULL;
	CritterDef *pCritterDef = NULL;

	//validate parameters
	if  ( !pContext || !pContext->pKilled || !pContext->pKilled->pCritter )
		return NULL;

	pCritter = pContext->pKilled->pCritter;
	pCritterDef = pCritter ? GET_REF(pCritter->critterDef) : NULL;

	if (!pCritterDef)
		return NULL;

    i = reward_GetItemLevel(pContext)-1;
    if(i >= g_AlgoTables.MaxLevel)
        i = g_AlgoTables.MaxLevel - 1;

	pContext->Quality = algoitem_GetRandomQuality(i, pCritterDef->pcRank, pSeed, false);

	if (pContext->Quality == kItemQuality_None)
        return NULL;// almost always means that algoitem_getrandomquality decided not to drop an algoitem.

	return algoitem_generate_quality(iPartitionIdx, pContext, pContext->Quality, pSeed);
}


RewardTable* find_algo_table(char* rootname, ItemType type, SlotType slot, InvBagIDs BagId, SkillType Skill, ItemQuality Quality)
{
	// the algo tables will be scanned for using the strictest filename first and then will loosen up if
	// that specific name is not found

	RewardTable *reward_table = NULL;

	bool bTypeSet = false;	
	bool bSlotSet = false;	
	bool bBagIdSet = false;	
	bool bSkillSet = false;	
	bool bQualitySet = false;	

	bool bTryType = false;	
	bool bTrySlot = false;	
	bool bTryBagId = false;	
	bool bTrySkill = false;	
	bool bTryQuality = false;	

	char *tmpName = NULL;
		
	estrStackCreate(&tmpName);


	if (type != kItemType_None)
	{
		bTypeSet = true;
		bTryType = true;
	}

	if (slot != kSlotType_Any)
	{
		bSlotSet = true;
		bTrySlot = true;
	}

	if (BagId != InvBagIDs_None)
	{	
		bBagIdSet = true;
		bTryBagId = true;
	}

	if (Skill != kSkillType_None)
	{	
		bSkillSet = true;
		bTrySkill = true;
	}

	if (Quality >= 0)
	{
		bQualitySet = true;
		bTryQuality = true;
	}


	for(;;)
	{
		estrClear(&tmpName);

		estrAppend2(&tmpName, rootname);

		if (bTryType)
		{
			estrAppend2(&tmpName, "_");
			estrAppend2(&tmpName, StaticDefineIntRevLookup(ItemTypeEnum,type));
		}

		if (bTrySlot)
		{
			estrAppend2(&tmpName, "_");
			estrAppend2(&tmpName, StaticDefineIntRevLookup(SlotTypeEnum,slot));
		}

		if (bTryBagId)
		{
			estrAppend2(&tmpName, "_");
			estrAppend2(&tmpName, StaticDefineIntRevLookup(InvBagIDsEnum,BagId));
		}

		if (bTrySkill)
		{
			estrAppend2(&tmpName, "_");
			estrAppend2(&tmpName, StaticDefineIntRevLookup(SkillTypeEnum,Skill));
		}

		if (bTryQuality)
		{
			estrAppend2(&tmpName, "_");
			estrAppend2(&tmpName, StaticDefineIntRevLookup(ItemQualityEnum,Quality));
		}

		reward_table = (RewardTable*)RefSystem_ReferentFromString(g_hRewardTableDict,tmpName);
		
		if (reward_table)
			break;		//found one

		if (!reward_table && !bTryType && !bTrySlot && !bTryBagId && !bTrySkill && !bTryQuality)
			break;		//no matches found

		if (bTryQuality)
		{
			bTryQuality = false;
			continue;
		}

		if (bTrySkill)
		{
			bTrySkill = false;
			if (bQualitySet) bTryQuality = true;
			continue;
		}

		if (bTryBagId)
		{
			bTryBagId = false;
			if (bQualitySet) bTryQuality = true;
			if (bSkillSet) bTrySkill = true;
			continue;
		}

		if (bTrySlot)
		{
			bTrySlot = false;
			if (bQualitySet) bTryQuality = true;
			if (bSkillSet) bTrySkill = true;
			if (bBagIdSet) bTryBagId = true;
			continue;
		}

		if (bTryType)
		{
			bTryType = false;
			if (bQualitySet) bTryQuality = true;
			if (bSkillSet) bTrySkill = true;
			if (bBagIdSet) bTryBagId = true;
			if (bSlotSet) bTrySlot = true;
			continue;
		}
	}


	estrDestroy(&tmpName);

	return reward_table;
}

static NOCONST(Item)* algoitem_create(ItemDef *item_def, int level, int quality)
{
	
    AlgoItemLevelsDef *algo_level_table = NULL;
    NOCONST(Item) *item;
    if(!item_def)
        return NULL;
    item = StructCreateNoConst(parse_Item);
    SET_HANDLE_FROM_REFDATA(g_hItemDict,item_def->pchName,item->hItem);
    item->flags |= kItemFlag_Algo;

	//Need to upper-limit the MaxLevel value to the size of the algoleveltable array so it doesn't throw an obnoxious error.
    algo_level_table = eaIndexedGetUsingString(&g_CommonAlgoTables.ppAlgoItemLevels, StaticDefineIntRevLookup(ItemQualityEnum,quality));
	
	item_trh_SetAlgoPropsMinLevel(item, MINMAX(level, 1, MINMAX(g_AlgoTables.MaxLevel,1,(int)ARRAY_SIZE(algo_level_table->level))));
	item_trh_SetAlgoPropsLevel(item, item_trh_GetMinLevel(item));
	if (algo_level_table)
		item_trh_SetAlgoPropsLevel(item, algo_level_table->level[item_trh_GetMinLevel(item) - 1]);

    item->pAlgoProps->Quality = quality;

    return item;
}

// Why do we only sometimes want to apply the scale?  [RMARR - 9/21/10]
static void apply_extras(NOCONST(Item) *item,RewardContext *pContext,bool bApplyScale)
{
	int i;
	for (i = 0; i < eaSize(&pContext->pAlgoRewards->ppExtras); i++)
	{
		ItemPowerDef *pItemPower = pContext->pAlgoRewards->ppExtras[i];
		NOCONST(AlgoItemProps)* pProps = item_trh_GetOrCreateAlgoProperties(item);

		if (pItemPower->eItemPowerCategories & kItemPowerCategory_PowerFactor)
		{
			pProps->iPowerFactor = pItemPower->iFactorValue;
		}
		else
		{
			NOCONST(ItemPowerDefRef) *power_def;
			ItemDef *pItemPowerRecipeDef = SAFE_GET_REF(pItemPower, hCraftRecipe);
			power_def = StructCreateNoConst(parse_ItemPowerDefRef);        

			SET_HANDLE_FROM_REFERENT("ItemPowerDef", pItemPower, power_def->hItemPowerDef);
			power_def->iPowerGroup = pItemPowerRecipeDef ? log2_floor(pItemPowerRecipeDef->Group) : 0; // TODO: Check on this
			if (bApplyScale)
			{
				power_def->ScaleFactor = g_AlgoTables.fItemPowerScale;
			}
			eaPush(&pProps->ppItemPowerDefRefs, power_def);
		}
	}
}

// generate the primary/secondary powers from rewards
static void algoitem_gen_powers(int iPartitionIdx, NOCONST(Item) *item, ItemDef *item_def, int Quality, int *pSeed, RewardContext *pContext)
{
	RewardTable *reward_table = NULL;
	RewardEntry *algo_base = NULL;
	RewardContext *pLocalContext = NULL;
	InventoryBag **ppRewardBags = NULL;
	int i,s;

	//validate parameters
	if  ( !pContext)
	{
		return;
	}

	pLocalContext = StructClone(parse_RewardContext, pContext);
	if (pLocalContext == NULL)
		return;

	// player level is used here if > 0
	if(pContext->iPlayerLevelForItem > 0)
	{
		pLocalContext->RewardLevel = pContext->iPlayerLevelForItem;
	}

	pLocalContext->pAlgoRewards = calloc(sizeof(AlgoRewardContext),1);
	pLocalContext->type = RewardContextType_AlgoExtra;

	s = eaiSize(&item_def->peRestrictBagIDs);
	if(s)
	{
		for (i = 0; i < s; i++)
		{
			InvBagIDs eRestrictBagID = item_def->peRestrictBagIDs[i];
			reward_table = find_algo_table("at_power", item_def->eType, item_def->eRestrictSlotType, eRestrictBagID, item_def->kSkillType, Quality);
			if(reward_table)
				break;
		}
	}
	else
	{
		reward_table = find_algo_table("at_power", item_def->eType, item_def->eRestrictSlotType, InvBagIDs_None, item_def->kSkillType, Quality);
	}

	if ( !reward_table )
	{
		ErrorFilenamef(item_def->pchFileName,"for algoitem couldn't find at_power_* reward table to add an itempower to this item, %s, %s", StaticDefineIntRevLookup(ItemQualityEnum,Quality), StaticDefineIntRevLookup(InvBagIDsEnum,item_def->eRestrictSlotType));
	}
	reward_generate(iPartitionIdx, NULL, pLocalContext, reward_table, &ppRewardBags, NULL, pSeed);

	if (eaSize(&pLocalContext->pAlgoRewards->ppCostumes) == 1)
	{
		NOCONST(SpecialItemProps)* pProps = item_trh_GetOrCreateSpecialProperties(item);
		SET_HANDLE_FROM_REFERENT("PlayerCostume",pLocalContext->pAlgoRewards->ppCostumes[0], pProps->hCostumeRef);
	}

	apply_extras(item,pLocalContext,true/*bApplyScale*/);

	// Destroy the algo-rewards on the local context (it's marked as NO_AST)
	eaDestroy(&pLocalContext->pAlgoRewards->ppBaseItems);
	eaDestroy(&pLocalContext->pAlgoRewards->ppExtras);
	eaDestroy(&pLocalContext->pAlgoRewards->ppCostumes);
	free(pLocalContext->pAlgoRewards);

	// Destroy the local context
	StructDestroy(parse_RewardContext, pLocalContext);

	eaDestroyStruct(&ppRewardBags, parse_InventoryBag);

    item_trh_FixupPowers(item);
}


Item* algoitem_generate_quality(int iPartitionIdx, RewardContext *ctxt, int Quality, U32 *pSeed)
{
    ItemDef *item_def;
	RewardTable *reward_table = NULL;
	NOCONST(Item)* item = NULL;
	SkillType KilledSkillType = kSkillType_None;
    CritterDef *critter_def = NULL;
    S32 iItemLevel = reward_GetItemLevel(ctxt);
    
	//setup the reward skill type based on the killed critters skill type
	if ( SAFE_MEMBER2(ctxt,pKilled,pCritter) )
        critter_def = GET_REF(ctxt->pKilled->pCritter->critterDef);
    
    if (critter_def)
    {
        KilledSkillType = critter_def->eSkillType;
        
        if (KilledSkillType == kSkillType_None)
        {
            CritterGroup *pCritterGroup = GET_REF(critter_def->hGroup);
            
            if (pCritterGroup)
            {
                KilledSkillType = pCritterGroup->eSkillType;
            }
        }
    }
    
	reward_table = find_algo_table("at_base", kItemType_None, kSlotType_Any, InvBagIDs_None, KilledSkillType, Quality);
    
	//validate parameters
	if  ( !ctxt || !reward_table )
		return NULL;
    
	ctxt->Quality = Quality; // aaah! bad bad bad! this is a param.
	item_def = algobase_generate(iPartitionIdx, ctxt, reward_table, pSeed);
    if(!item_def)
        return NULL;
    
    item = algoitem_create(item_def, iItemLevel, Quality);
    if ( !item )
        return NULL;    
    algoitem_gen_powers(iPartitionIdx, item,item_def,Quality, pSeed, ctxt);
    return (Item*)item;
}

// Give a random algorithmic item to yourself. It is put into your inventory
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Inventory);
void GiveRandomAlgoItem(Entity *pEnt, int iQuality, int iLevel)
{
	Item *pItem;
	RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);
	static U32 iSeed = 0;
	
	if (!iSeed) {
		iSeed = randomU32();
	}

	if (pLocalContext == NULL)
		return;
	
	pLocalContext->RewardLevel = iLevel;
	pLocalContext->KillerLevel = inv_GetNumericItemValue(pEnt, "Level");

	iSeed = randomIntSeeded(&iSeed, RandType_LCG);
	pItem = algoitem_generate_quality(entGetPartitionIdx(pEnt), pLocalContext, iQuality, &iSeed);
	if (pItem) {
		ItemChangeReason reason = {0};

		inv_FillItemChangeReason(&reason, pEnt, "Internal:GiveRandomAlgoItem", NULL);
		item_trh_FixupPowers(CONTAINER_NOCONST(Item, pItem));

		invtransaction_AddItem(pEnt, InvBagIDs_Inventory, -1, pItem, 0, &reason, NULL, NULL);
		StructDestroy(parse_Item, pItem);
	}

	// Destroy the local context
	StructDestroy(parse_RewardContext, pLocalContext);
}




//  <level> <quality> <itemdef> <primary> <secondary (opt)> construct an algorithmic item as it may be created from any particular drop. the magic word 'last' can be used with the itemdef and primary/secondary if you want to use the previous invocation's version of this
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Standard, Inventory);
void GiveAlgoItem(Entity *pEnt, int level,
                  ACMD_NAMELIST(ItemQualityEnum,STATICDEFINE) char *quality_name,
                  ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *item_def_name,
                  ACMD_NAMELIST("ItemPowerDef", REFDICTIONARY) char *primary_name,
                  ACMD_NAMELIST("ItemPowerDef", REFDICTIONARY) char *secondary_name)
{
    ItemQuality quality = StaticDefineIntGetInt(ItemQualityEnum, quality_name);
    NOCONST(Item)* item = NULL;
    ItemDef *def = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,item_def_name);
    ItemPowerDef *primary = (ItemPowerDef*)RefSystem_ReferentFromString(g_hItemPowerDict,primary_name);
	ItemDef *primary_recipe = SAFE_GET_REF(primary, hCraftRecipe);
    ItemPowerDef *secondary = (ItemPowerDef*)RefSystem_ReferentFromString(g_hItemPowerDict,secondary_name);
	ItemDef *secondary_recipe = SAFE_GET_REF(secondary, hCraftRecipe);
    NOCONST(ItemPowerDefRef) *tmp = NULL;
	ItemChangeReason reason = {0};
	NOCONST(AlgoItemProps)* pProps = item_trh_GetOrCreateAlgoProperties(item);

    static ItemDef *last_def = NULL;          // won't survive reload, c'est la vie
    static ItemPowerDef *last_primary = NULL;
    static ItemPowerDef *last_secondary = NULL;

    if(0==stricmp(item_def_name,"last"))
        def = last_def;
    if(0==stricmp(primary_name,"last"))
        primary = last_primary;
    if(0==stricmp(secondary_name,"last"))
        secondary = last_secondary;

    if(!def)
    {
        gslSendPrintf(pEnt,"Couldn't find itemdef %s",item_def_name);
        return;
    }
    if(!primary)
    {
        gslSendPrintf(pEnt,"Couldn't find primary %s",primary_name);
        return;
    }

    last_def = def;
    last_primary = primary;
    last_secondary = secondary;

    item = algoitem_create(def,level,quality);
    if(!item)
    {
        gslSendPrintf(pEnt,"failed to create item");
        return;        
    }

    tmp = StructCreateNoConst(parse_ItemPowerDefRef);
    SET_HANDLE_FROM_REFERENT(g_hItemPowerDict,primary,tmp->hItemPowerDef);
	tmp->iPowerGroup = primary_recipe ? log2_floor(primary_recipe->Group) : 0;
	tmp->ScaleFactor = g_AlgoTables.fItemPowerScale;
    eaPush(&pProps->ppItemPowerDefRefs,tmp);

    if(def->eType == kItemType_Upgrade && def->eRestrictSlotType == kSlotType_Primary)
    {
        if(secondary)
        {
            gslSendPrintf(pEnt,"this is a 'primary' base item, slotting secondary");
            tmp = StructCreateNoConst(parse_ItemPowerDefRef);
            SET_HANDLE_FROM_REFERENT(g_hItemPowerDict,secondary,tmp->hItemPowerDef);
			tmp->iPowerGroup = secondary_recipe ? log2_floor(secondary_recipe->Group) : 0;
			tmp->ScaleFactor = g_AlgoTables.fItemPowerScale;
            eaPush(&pProps->ppItemPowerDefRefs,tmp);
        }
        else
        {
            gslSendPrintf(pEnt,"this is a 'primary' base item, but no secondary itempower was specified, aborting");
            StructDestroyNoConst(parse_Item,item);
            return;
        }
        
    }    
    else if(secondary)
    {
        gslSendPrintf(pEnt,"this is not a primary upgrade base item. ignoring secondary itempower");
        secondary = NULL;
    }

    item_trh_FixupPowers(item);
	inv_FillItemChangeReason(&reason, pEnt, "Internal:GiveAlgoItem", NULL);
    invtransaction_AddItem(pEnt, InvBagIDs_Inventory, -1, (Item*)item, 0, &reason, NULL, NULL);
    StructDestroyNoConst(parse_Item, item);
}

//  <level> <quality> <itemdef> <powerfactor> <power1 (opt)> <power2 (opt)> <power3 (opt)> construct an algorithmic item as it may be created from any particular drop.
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(7) ACMD_CATEGORY(Standard, Inventory);
void GiveAlgoItem2(Entity *pEnt, int level,
                  ACMD_NAMELIST(ItemQualityEnum,STATICDEFINE) char *quality_name,
                  ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *item_def_name,
				  int powerfactor,
                  ACMD_NAMELIST("ItemPowerDef", REFDICTIONARY) char *power1_name,
                  ACMD_NAMELIST("ItemPowerDef", REFDICTIONARY) char *power2_name,
				  ACMD_NAMELIST("ItemPowerDef", REFDICTIONARY) char *power3_name)
{
    ItemQuality quality = StaticDefineIntGetInt(ItemQualityEnum, quality_name);
    NOCONST(Item)* item = NULL;
    ItemDef *def = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,item_def_name);
    ItemPowerDef *power1 = (ItemPowerDef*)RefSystem_ReferentFromString(g_hItemPowerDict,power1_name);
	ItemDef *recipe1 = SAFE_GET_REF(power1, hCraftRecipe);
    ItemPowerDef *power2 = (ItemPowerDef*)RefSystem_ReferentFromString(g_hItemPowerDict,power2_name);
	ItemDef *recipe2 = SAFE_GET_REF(power2, hCraftRecipe);
    ItemPowerDef *power3 = (ItemPowerDef*)RefSystem_ReferentFromString(g_hItemPowerDict,power3_name);
	ItemDef *recipe3 = SAFE_GET_REF(power3, hCraftRecipe);
    NOCONST(ItemPowerDefRef) *tmp = NULL;
	ItemChangeReason reason = {0};

    if(!def)
    {
        gslSendPrintf(pEnt,"Couldn't find itemdef %s",item_def_name);
        return;
    }

    item = algoitem_create(def,level,quality);
    if(!item)
    {
        gslSendPrintf(pEnt,"failed to create item");
        return;        
    }

	item_trh_SetAlgoPropsPowerFactor(item, powerfactor);

	if (power1)
	{
		tmp = StructCreateNoConst(parse_ItemPowerDefRef);
		SET_HANDLE_FROM_REFERENT(g_hItemPowerDict,power1,tmp->hItemPowerDef);
		tmp->iPowerGroup = recipe1 ? log2_floor(recipe1->Group) : 0;
		tmp->ScaleFactor = g_AlgoTables.fItemPowerScale;
		eaPush(&item->pAlgoProps->ppItemPowerDefRefs,tmp);
	}

    if(power2)
    {
        tmp = StructCreateNoConst(parse_ItemPowerDefRef);
        SET_HANDLE_FROM_REFERENT(g_hItemPowerDict,power2,tmp->hItemPowerDef);
		tmp->iPowerGroup = recipe2 ? log2_floor(recipe2->Group) : 0;
		tmp->ScaleFactor = g_AlgoTables.fItemPowerScale;
        eaPush(&item->pAlgoProps->ppItemPowerDefRefs,tmp);
    }

	if(power3)
    {
        tmp = StructCreateNoConst(parse_ItemPowerDefRef);
        SET_HANDLE_FROM_REFERENT(g_hItemPowerDict,power3,tmp->hItemPowerDef);
		tmp->iPowerGroup = recipe3 ? log2_floor(recipe3->Group) : 0;
		tmp->ScaleFactor = g_AlgoTables.fItemPowerScale;
        eaPush(&item->pAlgoProps->ppItemPowerDefRefs,tmp);
    }

    item_trh_FixupPowers(item);
	inv_FillItemChangeReason(&reason, pEnt, "Internal:GiveAlgoItem2", NULL);
    invtransaction_AddItem(pEnt, InvBagIDs_Inventory, -1, (Item*)item, 0, &reason, NULL, NULL);
    StructDestroyNoConst(parse_Item, item);
}

// gen an algo item from the given itemdef base. the magic word 'last' can be used with the itemdef
AUTO_COMMAND ACMD_SERVERONLY ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Inventory);
void AlgoGenItemFromDef(Entity *e, int level,
                     ACMD_NAMELIST(ItemQualityEnum,STATICDEFINE) char *quality_name,
                     ACMD_NAMELIST("ItemDef", REFDICTIONARY) char *item_def_name)
{
    ItemQuality quality = StaticDefineIntGetInt(ItemQualityEnum, quality_name);
    NOCONST(Item)* item = NULL;
    ItemDef *def = (ItemDef*)RefSystem_ReferentFromString(g_hItemDict,item_def_name);
    NOCONST(ItemPowerDefRef) *tmp = NULL;
	ItemChangeReason reason = {0};

    static ItemDef *last_def = NULL; // won't survive reload, c'est la vie
    RewardContext *pLocalContext = Reward_CreateOrResetRewardContext(NULL);

	if (pLocalContext == NULL)
		return;

    pLocalContext->type = RewardContextType_PowerExec;
    pLocalContext->pKilled = NULL;
    pLocalContext->RewardLevel = level;
    pLocalContext->RewardScale = 0.f;
	SetKillerRewardContext(e, pLocalContext);

    if(0==stricmp(item_def_name,"last"))
        def = last_def;

    if(!def)
    {
        gslSendPrintf(e,"Couldn't find itemdef %s",item_def_name);
        return;
    }
    last_def = def;

    item = algoitem_create(def,level,quality);
    if(!item)
    {
        gslSendPrintf(e,"failed to create item");
        return;        
    }

    algoitem_gen_powers(entGetPartitionIdx(e), item,def,quality,NULL,pLocalContext);
	inv_FillItemChangeReason(&reason, e, "Internal:AlgoGenItemFromDef", NULL);

    invtransaction_AddItem(e, InvBagIDs_Inventory, -1, (Item*)item, 0, &reason, NULL, NULL);
    StructDestroyNoConst(parse_Item, item);

	// Destroy the local context
	StructDestroy(parse_RewardContext, pLocalContext);
}

//NNO Algo functions.

const char* algoitem_NNO_GetMagicEnhancementName(ItemDef* pItem)
{
	S32 i;
	InvBagIDs eArmor = StaticDefineIntGetInt(InvBagIDsEnum, "Armor");
	InvBagIDs eWeapon = StaticDefineIntGetInt(InvBagIDsEnum, "Melee");
	InvBagIDs eRanged = StaticDefineIntGetInt(InvBagIDsEnum, "Ranged");
	InvBagIDs eNeck = StaticDefineIntGetInt(InvBagIDsEnum, "Neck");

	for (i = 0; i < eaiSize(&pItem->peRestrictBagIDs); i++)
	{
		InvBagIDs eRestrictBagID = pItem->peRestrictBagIDs[i];
		if (eRestrictBagID == eWeapon || eRestrictBagID == eRanged)
		{
			return "Weapon_Enhancement_Plus";
		}
		else if (eRestrictBagID == eArmor)
		{
			return "Armor_Enhancement_Plus";
		}
		else if (eRestrictBagID == eNeck)
		{
			return "Neck_Enhancement_Plus";
		}
	}
	return "";
}

#include "AlgoItem_h_ast.c"
