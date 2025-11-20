/***************************************************************************
*     Copyright (c) 2003-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef ITEMENUMS_H
#define ITEMENUMS_H
GCC_SYSTEM

/*
AUTO_ENUM;
typedef enum kItemDefFlag
{
	kItemDefFlag_Tradeable			= (1<<0),
	kItemDefFlag_BindOnPickup		= (1<<1),
	kItemDefFlag_BindOnEquip		= (1<<2),
	kItemDefFlag_Enigma				= (1<<3),
	kItemDefFlag_Fused				= (1<<4),
	kItemDefFlag_CanUseUnequipped	= (1<<5),
	kItemDefFlag_CantSell			= (1<<6),
	kItemDefFlag_Silent				= (1<<7),
	kItemDefFlag_Unique				= (1<<8),
}kItemDefFlag;

AUTO_ENUM;
typedef enum kItemPowerFlag
{
	kItemPowerFlag_Gadget			= (1<<0),
	kItemPowerFlag_CanUseUnequipped	= (1<<1),
	kItemPowerFlag_Rechargeable  	= (1<<2),
	kItemPowerFlag_LocalEnhancement	= (1<<3),
	kItemPowerFlag_DefaultStance	= (1<<4),
}kItemPowerFlag;


AUTO_ENUM;
typedef enum kItemType
{
	kItemType_Upgrade,
	kItemType_Component,
	kItemType_ItemRecipe, ENAMES(ItemRecipe Recipe)
	kItemType_ItemValue,  ENAMES(ItemValue ValueRecipe)
	kItemType_ItemPowerRecipe,
	kItemType_ItemPowerValue,
	kItemType_QualityRecipe,
	kItemType_QualityValue,
	kItemType_Mission,
	kItemType_MissionGrant,
	kItemType_Boost,
	kItemType_Badge,
	kItemType_Device,
	kItemType_Numeric,
	kItemType_Weapon,
	kItemType_Bag,
	kItemType_Callout,
	kItemType_Token,
	kItemType_Title,
	kItemType_None,
}kItemType;


AUTO_ENUM;
typedef enum kUpgradeSlotType
{
	kUpgradeSlotType_Any,
	kUpgradeSlotType_Primary,
	kUpgradeSlotType_Secondary, 
}kUpgradeSlotType;
*/

AUTO_ENUM;
typedef enum SkillType
{
	kSkillType_None = 0,
	kSkillType_Arms = (1<<0), 
	kSkillType_Mysticism = (1<<1), 
	kSkillType_Science = (1<<2), 
} SkillType;


typedef struct	DefineContext DefineContext;
extern DefineContext *g_pDefineItemQualities;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pDefineItemQualities);
typedef enum ItemQuality
{
	kItemQuality_None = -1, ENAMES(None)
	kItemQuality_FIRST_DATA_DEFINED, EIGNORE
}ItemQuality;

/*
AUTO_ENUM;
typedef enum kItemFlag
{
	kItemFlag_Bound				= (1<<0),
	kItemFlag_Zdummy			= (1<<31),  //dummy field to force enum prefix
}kItemFlag;


AUTO_ENUM;
typedef enum kItemPowerGroup
{
	kItemPowerGroup_1			= (1<<0), ENAMES(Group1)
	kItemPowerGroup_2			= (1<<1), ENAMES(Group2)
	kItemPowerGroup_3			= (1<<2), ENAMES(Group3)
	kItemPowerGroup_4			= (1<<3), ENAMES(Group4)
	kItemPowerGroup_5			= (1<<4), ENAMES(Group5)
	kItemPowerGroup_6			= (1<<5), ENAMES(Group6)
	kItemPowerGroup_7			= (1<<6), ENAMES(Group7)
	kItemPowerGroup_8			= (1<<7), ENAMES(Group8)

}kItemPowerGroup;


AUTO_ENUM;
typedef enum kComponentCountType
{
	kComponentCountType_Fixed,
	kComponentCountType_LevelAdjust,
	kComponentCountType_Common1,
	kComponentCountType_Common2,
	kComponentCountType_UnCommon1,
	kComponentCountType_UnCommon2,
	kComponentCountType_Rare1,

}kComponentCountType;
*/
//#include "AutoGen/itemEnums_h_ast.h"

extern DefineContext *g_InvExtraBagIDs;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_InvExtraBagIDs);
typedef enum InvBagIDs
{
	// WARNING: if you change the value of any existing BagIDs, make sure you find all the places
	// that call eaIndexed functions with literal ints on inventory bags (structparser doesn't understand
	// enums). All of them should follow this pattern, for easier searching: "31 /* Literal InvBagIDs_Buyback */"

	InvBagIDs_None = 0,
	InvBagIDs_Numeric,
	InvBagIDs_Inventory,
	InvBagIDs_Recipe, 
	InvBagIDs_Callout,
	InvBagIDs_Lore,
	InvBagIDs_Tokens,	ENAMES(Tokens Emotes)
	InvBagIDs_Titles,
	InvBagIDs_ItemSet,

	InvBagIDs_PlayerBags,	// If you change the amount of PlayerBags, please update inv_trh_PlayerBagFromSlotIdx
	InvBagIDs_PlayerBag1,
	InvBagIDs_PlayerBag2,
	InvBagIDs_PlayerBag3,
	InvBagIDs_PlayerBag4,
	InvBagIDs_PlayerBag5,
	InvBagIDs_PlayerBag6,
	InvBagIDs_PlayerBag7,
	InvBagIDs_PlayerBag8,
	InvBagIDs_PlayerBag9,

	InvBagIDs_Bank,
	InvBagIDs_Bank1,
	InvBagIDs_Bank2,
	InvBagIDs_Bank3,
	InvBagIDs_Bank4,
	InvBagIDs_Bank5,
	InvBagIDs_Bank6,
	InvBagIDs_Bank7,
	InvBagIDs_Bank8,
	InvBagIDs_Bank9,

	InvBagIDs_HiddenLocationData,
	InvBagIDs_LocationData,

	InvBagIDs_Buyback,
	InvBagIDs_Hidden,
	InvBagIDs_Overflow,
	InvBagIDs_Injuries,
	InvBagIDs_SuperCritterPets,
	InvBagIDs_PetEquipBag,

	InvBagIDs_Loot

	//Loot is assumed to be the last fixed bag ID by the dynamic enum loading code.
	//Add any new bags before it

} InvBagIDs;

extern DefineContext *s_pDefineItemTags;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(s_pDefineItemTags);
typedef enum ItemTag
{
	ItemTag_None,		ENAMES(None)
	ItemTag_Any,		EIGNORE
} ItemTag;
extern StaticDefineInt ItemTagEnum[];

AUTO_STRUCT;
typedef struct ItemTagData
{
	// The item tag that data is defined for
	ItemTag eItemTag;			AST(KEY)
	
	// name of the item tag, post load sets eItemTag
	char * esItemTagName;		AST(ESTRING)
	
	// The skill type that this tag is used for
	SkillType eSkillType;

	// is the skill specialization required to learn blueprints for this ItemTag?
	bool bSpecializationRequired; 

} ItemTagData;


AUTO_STRUCT;
typedef struct ItemTagInfo
{
	ItemTagData **eItemTagData;

}ItemTagInfo;

extern DefineContext *s_pDefineItemCategories;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(s_pDefineItemCategories);
typedef enum ItemCategory
{
	kItemCategory_None = 0,
	kItemCategory_DDWeapon = 1,
		// Considered a Weapon under DD rules

	kItemCategory_DDImplement,
		// Considered an Implement under DD rules

	kItemCategory_DDShield,
		// Considered a Shield under DD rules

	kItemCategory_FIRST_DATA_DEFINED,	EIGNORE
		// Any ItemCategory that is equal to or greater than this value is defined in data, and thus has no
		//  explicit mechanical meaning.

} ItemCategory;

AUTO_ENUM;
typedef enum NumericOp
{
    NumericOp_Add,     // n += value 
    NumericOp_RaiseTo, // n =  n < value ? value : n
    NumericOp_LowerTo, // n =  n > value ? value : n
    NumericOp_SetTo,   // n = value
} NumericOp;

#endif
