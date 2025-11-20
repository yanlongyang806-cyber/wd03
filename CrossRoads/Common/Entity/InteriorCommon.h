/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
GCC_SYSTEM
#include "Message.h"
#include "contact_enums.h"
#include "CombatEnums.h"

typedef struct Entity Entity;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef struct NOCONST(GameAccountData) NOCONST(GameAccountData);
typedef struct MicroTransactionDef MicroTransactionDef;
typedef U32 ContainerID;
typedef struct Expression Expression;
typedef struct ItemChangeReason ItemChangeReason;
typedef struct CharClassCategorySet CharClassCategorySet;

AUTO_STRUCT;
typedef struct InteriorOptionChoice
{
	// Logical name of the choice
	STRING_POOLED name;							AST(STRUCTPARAM KEY POOL_STRING)

	// The name of this choice to display to the user in selection UI, etc.
	REF_TO(Message) hDisplayName;				AST(NAME(DisplayName) STRUCT(parse_Message))

	// filename that this def came from
	const char *filename;						AST(CURRENTFILE)

	// Icon that will be displayed in the UI
	STRING_POOLED iconName;						AST(POOL_STRING)

	// Expression to evaluate to determine whether this option is available for the current player
	Expression *availableExpression;			AST(LATEBIND)

	// A game specific value.  Used in STO ship interiors to select which child in a GroupDef is displayed.
	S32 value;

} InteriorOptionChoice;

//
// Used to make Earrays of InteriorOptionChoice references
//
AUTO_STRUCT;
typedef struct InteriorOptionChoiceRef
{
	REF_TO(InteriorOptionChoice) hChoice;		AST(STRUCTPARAM KEY REFDICT(InteriorOptionChoice))
} InteriorOptionChoiceRef;

AUTO_STRUCT;
typedef struct InteriorOptionDef
{
	// Logical name of the option
	STRING_POOLED name;							AST(STRUCTPARAM KEY POOL_STRING)

	// filename that this def came from
	const char *filename;						AST(CURRENTFILE)

	// The name of this option to display to the user in selection UI, etc.
	REF_TO(Message) hDisplayName;				AST(NAME(DisplayName) STRUCT(parse_Message))

	
	// The available choices for this option
	EARRAY_OF(InteriorOptionChoiceRef) choiceRefs;	AST(NAME(Choice))

	REF_TO(InteriorOptionChoice) hDefaultChoice;	AST(NAME(DefaultChoice) REFDICT(InteriorOptionChoice))

	// cost to change this option
	S32				optionChangeCost;
	const char*		optionChangeCostNumeric;		AST(POOL_STRING)

	// Used to flag an option as being interesting to the tailor.  For STO this is used to select ship interior layout options.
	bool isTailorOption;						AST(BOOLFLAG)

} InteriorOptionDef;


//
// Used to make Earrays of InteriorOptionDef references
//
AUTO_STRUCT;
typedef struct InteriorOptionDefRef
{
	REF_TO(InteriorOptionDef) hOptionDef;		AST(STRUCTPARAM KEY REFDICT(InteriorOptionDef))
} InteriorOptionDefRef;

//
// Defines an entity interior.
// Currently used to define ship interiors in STO and hideouts in Champs.
//
AUTO_STRUCT;
typedef struct InteriorDef
{
	STRING_POOLED name;					AST( STRUCTPARAM KEY POOL_STRING )

	// filename that this def came from
	const char *filename;				AST( CURRENTFILE )

	// the name of this bridge to display to the user in selection UI, etc.
	DisplayMessage displayNameMsg;		AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))

	// the name of the map
	STRING_POOLED mapName;				AST(POOL_STRING)

	// optional spawn point name
	STRING_POOLED spawnPointName;		AST(POOL_STRING)

	// the game account key required to unlock this interior
	STRING_POOLED pchUnlockKey;			AST(NAME(UnlockKey) POOL_STRING)

	// whether or not this InteriorDef is unlockable (and not accessible by default)
	bool bUnlockable;					AST(NAME(Unlockable))

	// What interior configuration options are available for this interior
	EARRAY_OF(InteriorOptionDefRef) optionRefs; AST(NAME(Option))
} InteriorDef;

//
// Used to make Earrays of InteriorDef references
//
AUTO_STRUCT;
typedef struct InteriorDefRef
{
	REF_TO(InteriorDef) hInterior;				AST(STRUCTPARAM REFDICT(InteriorDef))
} InteriorDefRef;

AUTO_STRUCT;
typedef struct InteriorOptionRef
{
	REF_TO(InteriorOptionDef) hOption;					AST(KEY REQUIRED STRUCTPARAM REFDICT(InteriorOptionDef))
		//Which option

	REF_TO(InteriorOptionChoice) hChoice;				AST(REQUIRED STRUCTPARAM REFDICT(InteriorOptionChoice))
		//Which choice?
} InteriorOptionRef;


AUTO_STRUCT;
typedef struct InteriorSetting
{
	STRING_POOLED pchName;								AST(KEY POOL_STRING REQUIRED STRUCTPARAM)
		// The internal name of this setting/preset

	const char *filename;								AST( CURRENTFILE )
		// filename that this interior setting came from

	DisplayMessage displayNameMsg;						AST(NAME(DisplayName) STRUCT(parse_DisplayMessage))
		// The display name for the UI of this preset

	DisplayMessage descriptionMsg;						AST(NAME(Description) STRUCT(parse_DisplayMessage))
		// The description message for the UI of this preset

	REF_TO(InteriorDef) hInterior;						AST(REQUIRED NAME(Interior) REFDICT(InteriorDef))
		// Which interior this is a setting/preset for

	EARRAY_OF(InteriorOptionRef) eaOptionSettings;		AST(NAME(OptionSetting))
		// The setting for the options in this interior.  Not all options need settings

	STRING_POOLED pchPermission;						AST(POOL_STRING)
		// The permission that is supposed to unlock this interior

} InteriorSetting;

//
// Used to make Earrays of InteriorDef references for containers
//
AUTO_STRUCT AST_CONTAINER;
typedef struct InteriorDefRefCont
{
	CONST_REF_TO(InteriorDef) hInterior;				AST(PERSIST REFDICT(InteriorDef))
} InteriorDefRefCont;

AUTO_STRUCT AST_CONTAINER;
typedef struct InteriorOption
{
	CONST_REF_TO(InteriorOptionDef) hOption;			AST(PERSIST SUBSCRIBE KEY REFDICT(InteriorOptionDef))

	CONST_REF_TO(InteriorOptionChoice) hChoice;			AST(PERSIST SUBSCRIBE REFDICT(InteriorOptionChoice))
} InteriorOption;

AUTO_STRUCT AST_CONTAINER;
typedef struct InteriorData
{
	// The InteriorDef that describes the currently selected interior .
	CONST_REF_TO(InteriorDef) hInteriorDef;				AST(PERSIST KEY SUBSCRIBE REFDICT(InteriorDef))

	//The 'setting' of this interior
	CONST_REF_TO(InteriorSetting) hSetting;				AST(PERSIST SUBSCRIBE)

	// The player's interior customization option choices
	CONST_EARRAY_OF(InteriorOption) options;			AST(PERSIST SUBSCRIBE)
} InteriorData;

//
// This information describes the current interior for an entity.
// It will hang off of the SavedEntityData for the pet entity containing
//  the interior.
//
AUTO_STRUCT AST_CONTAINER;
typedef struct EntityInteriorData
{
	// The InteriorDef that describes the currently selected interior .
	CONST_REF_TO(InteriorDef) hInteriorDef;				AST(PERSIST SUBSCRIBE REFDICT(InteriorDef))

		//The 'setting' of this interior
	CONST_REF_TO(InteriorSetting) hSetting;				AST(PERSIST SUBSCRIBE)

	// The player's interior customization option choices
	CONST_EARRAY_OF(InteriorOption) options;			AST(PERSIST SUBSCRIBE)

	//The alternates for this entity's interiors/hideouts
	CONST_EARRAY_OF(InteriorData) alternates;			AST(PERSIST SUBSCRIBE)
} EntityInteriorData;

//
// This is player related data that relates to interiors. 
//
AUTO_STRUCT AST_CONTAINER;
typedef struct PlayerInteriorData
{
	// List of interiors that have been unlocked by the player
	CONST_EARRAY_OF(InteriorDefRefCont) interiorUnlocks;	AST(PERSIST)

} PlayerInteriorData;

//
// The data needed to prompt a player with an interior invite and then move them to the interior
//
AUTO_STRUCT;
typedef struct InteriorInvite
{
	STRING_POOLED mapName;								AST(POOL_STRING)
	STRING_POOLED spawnPointName;						AST(POOL_STRING)
	ContainerID ownerID;
	STRING_MODIFIABLE shipDisplayName;
	STRING_MODIFIABLE ownerDisplayName;
} InteriorInvite;

AUTO_STRUCT;
typedef struct InteriorSettingMTRef
{
	REF_TO(InteriorSetting)			hSetting;			AST(KEY STRUCTPARAM REFDICT(InteriorSetting))
	REF_TO(MicroTransactionDef)		hMTDef;				AST(NAME(MicroTransaction) REFDICT(MicroTransactionDef))
} InteriorSettingMTRef;

AUTO_STRUCT;
typedef struct InteriorConfig 
{
	// which class of pet has interiors
	U32 interiorPetType;								AST(SUBTABLE(CharClassTypesEnum))

	// contact required to change interior.
	ContactFlags interiorChangeContactType;				AST(FLAGS)

	// cost to change ship interior
	S32				interiorChangeCost;
	const char*		interiorChangeCostNumeric;			AST(POOL_STRING)
	const char*		interiorChangeFreeNumeric;			AST(POOL_STRING)

	// a GAD key to look for when determining if an account has used their free interior/hideout purchase
	const char*		pchFreePurchaseKey;

	// An earray of the interior settings that can be free
	EARRAY_OF(InteriorSettingMTRef) eaFreeSettings;		AST(NAME(FreeSetting))

	//Flag to tell the system whether or not to persist alternates
	U32 bPersistAlternates : 1;

	//If you haven't used an interior before, it automatically chooses a setting for you
	U32 bSettingChosenAutomatically : 1;

} InteriorConfig;

ContactFlags InteriorConfig_RequiredContactType(void);
CharClassTypes InteriorConfig_InteriorPetType(void);
S32 inv_trh_InteriorChangeCost(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPlayerEnt, ATH_ARG NOCONST(Entity)* pPetEnt, bool bDecrementFreeChanges, const ItemChangeReason *pReason, NOCONST(GameAccountData) *pData);
#define InteriorConfig_InteriorChangeCost(pPlayerEnt,pPetEnt,pData) inv_trh_InteriorChangeCost(ATR_EMPTY_ARGS,CONTAINER_NOCONST(Entity, pPlayerEnt),CONTAINER_NOCONST(Entity, (pPetEnt)),false,NULL,CONTAINER_NOCONST(GameAccountData, (pData)))
const char *InteriorConfig_InteriorChangeCostNumeric(void);
bool InteriorConfig_PersistAlternates(void);

InteriorDef *InteriorCommon_GetCurrentInteriorDef(Entity *pEnt);
Entity *InteriorCommon_GetPetByID(Entity *playerEnt, ContainerID petID);
InteriorDef *InteriorCommon_GetPetInteriorDefByName(Entity *petEnt, const char *interiorDefName);
bool InteriorCommon_CanAffordInteriorChange(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID Entity* pPetEnt);
bool InteriorCommon_IsInteriorInvitee(Entity* pEnt);
bool InteriorCommon_CanMoveToInterior(Entity* pEnt);
InteriorInvite *InteriorCommon_GetCurrentInteriorInvite(Entity* pEnt);
void InteriorCommon_GetInteriorOptionChoices(Entity *playerEnt, InteriorDef *interiorDef, const char *optionName, InteriorOptionChoice ***out_Choices);
const char *InteriorCommon_GetOptionChoiceName(Entity *playerEnt, ContainerID petID, const char *optionName);
const char *InteriorCommon_GetOptionChoiceDisplayMessage(Entity *playerEnt, ContainerID petID, const char *optionName);
S32 InteriorCommon_GetOptionChoiceValue(Entity *playerEnt, ContainerID petID, const char *optionName);

bool InteriorCommon_IsInteriorUnlocked(Entity* pEnt, InteriorDef* pDef);

MultiVal InteriorCommon_Eval(Expression *pExpr, Entity *pEnt, MMType eReturnType);
InteriorOptionDef *InteriorCommon_FindOptionDefByName(const char *optionName);
InteriorOptionChoice *InteriorCommon_FindOptionChoiceByName(const char *choiceName);
InteriorSetting *InteriorCommon_FindSettingByName(const char *settingName);
bool InteriorCommon_IsSettingCurrent(Entity *pEnt, InteriorSetting *setting);
bool InteriorCommon_CanUseSetting(Entity *pEnt, InteriorSetting *setting);
const char *InteriorCommon_GetFreePurchaseKey(void);
int InteriorCommon_trh_EntFreePurchasesRemaining(NOCONST(Entity) *pEnt, GameAccountDataExtract *pExtract);
bool InteriorCommon_EntHasFreePurchase(Entity *pEnt, GameAccountDataExtract *pExtract);
InteriorSettingMTRef *InteriorCommon_FindSettingMTRefByName(const char *pchSetting);
InteriorSettingMTRef ***InteriorCommon_GetFreeSettings(void);
bool InteriorCommon_IsOptionAvailableForInterior(InteriorDef *interiorDef, InteriorOptionDef *optionDef);
bool InteriorCommon_IsChoiceAvailableForOption(InteriorOptionDef *optionDef, const char *choiceName);
bool InteriorCommon_IsSettingAvailableForInterior(InteriorDef *interiorDef, InteriorSetting *setting);
void InteriorCommon_GetSettingsByInterior(InteriorDef *interiorDef, InteriorSetting ***peaSettings);
S32 InteriorCommon_InteriorOptionChangeCost(const char *optionName);
const char *InteriorCommon_InteriorOptionChangeCostNumeric(const char *optionName);
bool InteriorCommon_IsCurrentMapInterior(void);
bool InteriorCommon_IsChoiceActive(Entity *playerEnt, InteriorOptionChoice *pChoice);
Entity *InteriorCommon_GetActiveInteriorOwner(Entity *playerEnt, CharClassCategorySet *pSet);
const char *InteriorCommon_GetOptionChoiceDisplayMessageForInterior(Entity *playerEnt, ContainerID petID, const char *interiorName, const char *optionName);
const char *InteriorCommon_GetOptionChoiceNameForInterior(Entity *playerEnt, ContainerID petID, const char *interiorName, const char *optionName);

extern DictionaryHandle *g_hInteriorDefDict;
extern DictionaryHandle *g_hInteriorOptionDefDict;
extern DictionaryHandle *g_hInteriorOptionChoiceDict;
extern DictionaryHandle *g_hInteriorSettingDict;


