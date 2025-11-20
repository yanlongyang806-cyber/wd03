/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#ifndef MICROTRANSACTIONS_COMMON_H
#define MICROTRANSACTIONS_COMMON_H

GCC_SYSTEM

#include "message.h"
#include "stdtypes.h"

typedef U32 ContainerID;

AUTO_ENUM;
typedef enum MicroItemType
{
	kMicroItemType_None,			EIGNORE
		// Invalid

	kMicroItemType_AttribValue,		ENAMES(Attrib ChangeAttrib AttribChange)
		// Used to change an attrib in the GameAccountData struct
		//  To unlock items to be granted later
	
	kMicroItemType_Costume,			ENAMES(Costume CostumeUnlock UnlockCostume)
		//Unlocks a costume from a costume item
	
	kMicroItemType_Item,			ENAMES(Item GiveItem)
		// Gives an item through a standard microtransaction

	kMicroItemType_ItemGrant,		ENAMES(ItemGrant GrantItem)
		// Grants an item that requires it has been unlocked
		// (Requires the product name in the attrib array in the game account struct

	kMicroItemType_VanityPetUnlock,	ENAMES(VanityPetUnlock PetUnlock)
		// Unlocks the vanity pet, but does not grant it
	
	kMicroItemType_Other,			ENAMES(Special Other)
		// Other micro items:  character slots, costume slots, bank size upgrade etc...

	kMicroItemType_PlayerCostume,	ENAMES(CostumeRef)
		// Directly references the costume, not an item.

	kMicroItemType_Species,			ENAMES(Species)
		//	Grants the ability to create a species.  The ItemID is the logical name of the SpeciesDef.

	kMicroItemType_MAX,				EIGNORE
} MicroItemType;


//A Per-player micro transaction purchase struct.  Used to track the first purchase of pre-order bonuses that you can redeem on each player on an account.
AUTO_STRUCT AST_CONTAINER;
typedef struct MicroTransactionPurchase
{
	CONST_STRING_MODIFIABLE		pchProduct;				AST(PERSIST KEY)
		// The name of the product purchased

	const U32					iPurchaseTime;			AST(PERSIST)
		// The times they are purchased
		
} MicroTransactionPurchase;


// A MicroTransaction Purchase timestamp struct.  So you can track each time you've purchased a micro transaction.
//  May be used in the future to help fixup previously purchased microtransactions who's data gets changed
AUTO_STRUCT AST_CONTAINER;
typedef struct MTPurchaseStamp
{
	const U32 uiPurchaseTime;								AST(PERSIST SUBSCRIBE)
		// What time this was purchased.  (In SS2000)

	const U32 iVersion;										AST(PERSIST SUBSCRIBE)
		// The version of the micro transaction purchased (For fixup later)

	S64	iCost;
		//The cost of the microtransaction (In cryptic points)

} MTPurchaseStamp;

AUTO_STRUCT AST_CONTAINER;
typedef struct MicroTransaction
{
	CONST_STRING_POOLED pchName;							AST(PERSIST KEY POOL_STRING SUBSCRIBE)
		//The name of the microtransaction def

	CONST_EARRAY_OF(MTPurchaseStamp) ppPurchaseStamps;		AST(PERSIST SUBSCRIBE)
		//The timestamps of when you purchased them

} MicroTransaction;

S32 MicroTrans_TokenizeItemID(char *pchItem, SA_PARAM_OP_VALID char **ppchGameTitle, SA_PARAM_OP_VALID MicroItemType *eType, SA_PARAM_OP_VALID char **ppchItemOut);
	// Tokenizes the item id into requisite parts
	// Returns 0 if there was an error.  Otherwise it returns the number of items to grant.

void MicroTrans_FormItemEstr(char **ppchItemOut, const char *pchGameTitle, MicroItemType eType, const char *pchItem, S32 iCount);
	// Forms the item string from the requisite parts

const char *MicroTrans_GetCharSlotsKeyID(void);
	// Returns the key identifier for extra character slots

char *MicroTrans_GetCharSlotsGADKey(void);
	// Returns the key name for extra character slots

char *MicroTrans_GetCharSlotsASKey(void);
	// Returns the AS key name for extra character slots

const char *MicroTrans_GetSuperPremiumKeyID(void);
	// Returns the key identifier for super premium slots

char *MicroTrans_GetSuperPremiumGADKey(void);
	// Returns the key name for extra playertype_special characters

char *MicroTrans_GetSuperPremiumASKey(void);
	// Returns the AS key name for extra playertype_special characters

char *MicroTrans_GetCSRCharSlotsGADKey(void);
	// The name of the GameAccountData key that Customer Support can use to give additional character slots.

const char *MicroTrans_GetCostumeSlotsKeyID(void);
	// Returns the key identifier for extra costume slots

char *MicroTrans_GetCostumeSlotsGADKey(void);
	// Returns the key name for extra costume slots

char *MicroTrans_GetCostumeSlotsASKey(void);
	// Returns the AS key name for extra costume slots

const char *MicroTrans_GetExtraMaxAuctionsKeyID(void);
	// Returns the key identifier for extra max auctions

char *MicroTrans_GetExtraMaxAuctionsGADKey(void);
	// Returns the key name for extra max auctions

char *MicroTrans_GetExtraMaxAuctionsASKey(void);
	// Returns the AS key name for extra max auctions

const char *MicroTrans_GetRespecTokensKeyID(void);
	// Returns the key identifier for respec tokens

char *MicroTrans_GetRespecTokensGADKey(void);
	// Returns the key name for respec tokens

char *MicroTrans_GetRespecTokensASKey(void);
	// Returns the AS key name for respec tokens

const char *MicroTrans_GetRenameTokensKeyID(void);
	// Returns the key identifier for rename tokens

char *MicroTrans_GetRenameTokensGADKey(void);
	// Returns the key name for rename tokens

char *MicroTrans_GetRenameTokensASKey(void);
	// Returns the AS key name for rename tokens

const char *MicroTrans_GetOfficerSlotsKeyID(void);
	// Returns the key identifier for officer slots

char *MicroTrans_GetOfficerSlotsGADKey(void);
	// Returns the key name for officer slots

char *MicroTrans_GetOfficerSlotsASKey(void);
	// Returns the AS key name for officer slots

const char *MicroTrans_GetFreeCostumeChangeKeyID(void);
	// Returns the key identifier for a free costume change

char *MicroTrans_GetFreeCostumeChangeGADKey(void);
	// Returns the key name for a free costume change

char *MicroTrans_GetFreeCostumeChangeASKey(void);
	// Returns the AS key name for a free costume change

const char *MicroTrans_GetFreeShipCostumeChangeKeyID(void);
	// Returns the key identifier for a free ship costume change

char *MicroTrans_GetFreeShipCostumeChangeGADKey(void);
	// Returns the key name for a free ship costume change

char *MicroTrans_GetFreeShipCostumeChangeASKey(void);
	// Returns the AS key name for a free ship costume change

const char *MicroTrans_GetRetrainTokensKeyID(void);
	// Returns the key identifier for a retrain token

char *MicroTrans_GetRetrainTokensGADKey(void);
	// Returns the key name for a retrain token

char *MicroTrans_GetRetrainTokensASKey(void);
	// Returns the AS key name for a retrain token

const char *MicroTrans_GetItemAssignmentCompleteNowKeyID(void);
	// Returns the key identifier for a ItemAssignment completion token

char *MicroTrans_GetItemAssignmentCompleteNowGADKey(void);
	// Returns the key name for a ItemAssignment completion token

char *MicroTrans_GetItemAssignmentCompleteNowASKey(void);
	// Returns the AS key name for a ItemAssignment completion token

const char *MicroTrans_GetItemAssignmentUnslotTokensKeyID(void);
	// Returns the key identifier for a ItemAssignment unslot token

char *MicroTrans_GetItemAssignmentUnslotTokensGADKey(void);
	// Returns the key name for a ItemAssignment unslot token

char *MicroTrans_GetItemAssignmentUnslotTokensASKey(void);
	// Returns the AS key name for a ItemAssignment unslot token

char *MicroTrans_GetVirtualShardCharSlotsGADKey(ContainerID iVirtualShardID);
	// Returns the key name for extra virtual shard specific character slots.

const char *MicroTrans_GetSharedBankSlotKeyID(void);
	// Returns the key identifier for extra shared bank slots

char *MicroTrans_GetSharedBankSlotGADKey(void);
	// Returns the key name for extra shared bank slots

char *MicroTrans_GetSharedBankSlotASKey(void);
	// Returns the AS key name for extra shared bank slots

#endif //MICROTRANSACTIONS_COMMON_H
