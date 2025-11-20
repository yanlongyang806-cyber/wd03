/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef CONTACT_COMMON_H
#define CONTACT_COMMON_H
GCC_SYSTEM

#include "contact_enums.h"
#include "entCritter.h"
#include "inventoryCommon.h"
#include "MinigameCommon.h"
#include "mission_enums.h"
#include "itemCommon.h"
#include "referencesystem.h"
#include "Message.h"
#include "wlGenesisMissionsGameStructs.h"
#include "CharacterClass.h"

#include "../StaticWorld/group.h"

typedef struct AuctionBrokerDef AuctionBrokerDef;
typedef struct DynBitFieldGroup DynBitFieldGroup;
typedef struct Entity Entity;
typedef struct ExprFuncTable ExprFuncTable;
typedef struct Expression Expression;
typedef struct ItemAssignmentDefRef ItemAssignmentDefRef;
typedef struct ItemAssignmentRarityCount ItemAssignmentRarityCount;
typedef struct MissionCategory MissionCategory;
typedef struct MissionDef MissionDef;
typedef struct MissionOfferOverride MissionOfferOverride;
typedef struct MissionOfferParams MissionOfferParams;
typedef struct ParseTable ParseTable;
typedef struct PersistedStore PersistedStore;
typedef struct PowerStoreDef PowerStoreDef;
typedef struct PowerStorePowerInfo PowerStorePowerInfo;
typedef struct SpecialDialogOverride SpecialDialogOverride;
typedef struct StoreDef StoreDef;
typedef struct StoreDiscountInfo StoreDiscountInfo;
typedef struct StoreItemInfo StoreItemInfo;
typedef struct StoreSellableItemInfo StoreSellableItemInfo;
typedef enum MissionUIType MissionUIType;
typedef U32 EntityRef;
typedef struct AuctionBrokerLevelInfo AuctionBrokerLevelInfo;

extern StaticDefineInt MinigameTypeEnum[];
extern StaticDefineInt ColorEnum[];
extern StaticDefineInt CharClassCategoryEnum[];
extern StaticDefineInt ItemAssignmentRarityCountTypeEnum[];

// Dictionary holding the dialog formatters
extern DictionaryHandle g_hContactDialogFormatterDefDictionary;

// ----------------------------------------------------------------------------
// ContactDef structs and enums
// ----------------------------------------------------------------------------

// List type contacts try to show a list to the player, although they skip the list for
// certain "important" interactions.
// SingleDialog contacts never show a list, and only ever show one screen of text
AUTO_ENUM;
typedef enum ContactType
{
	ContactType_List,
	ContactType_SingleDialog,
} ContactType;
extern StaticDefineInt ContactTypeEnum[];

AUTO_ENUM;
typedef enum ContactMissionAllow
{
	ContactMissionAllow_GrantAndReturn,
	ContactMissionAllow_GrantOnly,
	ContactMissionAllow_ReturnOnly,
	ContactMissionAllow_SubMissionComplete,
	ContactMissionAllow_FlashbackGrant,
	ContactMissionAllow_ReplayGrant,
} ContactMissionAllow;
extern StaticDefineInt ContactMissionAllowEnum[];

AUTO_STRUCT;
typedef struct ContactAudioPhraseNames
{
	char **ppchPhrases;					AST(NAME(Phrase))
} ContactAudioPhraseNames;
extern ParseTable parse_ContactAudioPhraseNames[];
#define TYPE_parse_ContactAudioPhraseNames ContactAudioPhraseNames

extern DefineContext *g_ExtraContactAudioPhrases;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_ExtraContactAudioPhrases);
typedef enum ContactAudioPhrases
{
	ContactAudioPhrases_None = 0,	ENAMES(None)
	// Pulled from data
} ContactAudioPhrases;
extern StaticDefineInt ContactAudioPhrasesEnum[];

AUTO_STRUCT;
typedef struct ContactDialogFormatterDef
{
	// The name of the formatter
	const char* pchName;						AST(STRUCTPARAM KEY POOL_STRING)

	// The scope for the dialog formatter
	const char *scope;							AST(POOL_STRING SERVER_ONLY)

	// Used for reloading
	const char* pchFilename;					AST(CURRENTFILE)

	// The message which formats the dialog
	DisplayMessage msgDialogFormat;				AST(NAME(DialogFormatMessage) STRUCT(parse_DisplayMessage))
} ContactDialogFormatterDef;

// Struct contains all the things to do when saying dialog
AUTO_STRUCT
AST_IGNORE("animBits");
typedef struct DialogBlock
{
	// Requirements for player to see this dialog
	Expression* condition;				AST(NAME("Condition") REDUNDANT_STRUCT("Condition:", parse_Expression_StructParam) LATEBIND SERVER_ONLY)
	DisplayMessage displayTextMesg;		AST(STRUCT(parse_DisplayMessage))
	// The reference to the dialog formatter which is used by UI for the display text
	REF_TO(ContactDialogFormatterDef) hDialogFormatter;	AST(NAME("DialogFormatter") REFDICT(ContactDialogFormatterDef))
	const char* audioName;				AST( NAME("Audio", "Audio:") POOL_STRING )
	ContactAudioPhrases ePhrase;		AST( NAME("Phrase") )
	// The anim list to play when this dialog option is selected
	REF_TO(AIAnimList) hAnimList;			AST(NAME("AnimList"))
	// The text displayed for the continue button
	DisplayMessage continueTextMesg;	AST(STRUCT(parse_DisplayMessage))
	// The reference to the dialog formatter which is used by UI for the continue text
	REF_TO(ContactDialogFormatterDef) hContinueTextDialogFormatter;	AST(NAME("ContinueTextDialogFormatter") REFDICT(ContactDialogFormatterDef))
} DialogBlock;
extern ParseTable parse_DialogBlock[];
#define TYPE_parse_DialogBlock DialogBlock

AUTO_STRUCT;
typedef struct EndDialogAudio
{
	const char * pchAudioName;			AST( NAME("Audio") POOL_STRING )
} EndDialogAudio;


AUTO_STRUCT;
typedef struct SpecialDialogAction
{
	DisplayMessage displayNameMesg;			AST(STRUCT(parse_DisplayMessage))  // Name to be displayed when this action listed
	REF_TO(ContactDialogFormatterDef) hDisplayNameFormatter;	AST(NAME("DisplayNameFormatter") REFDICT(ContactDialogFormatterDef))
	REF_TO(ContactDef) contactDef;			AST( NAME("TargetContact"))
	const char* dialogName;					AST( NAME("TargetDialog") POOL_STRING )
	WorldGameActionBlock actionBlock;		AST( NAME("ActionBlock") STRUCT(parse_WorldGameActionBlock) )
	U8 bSendComplete;						AST( DEF(1) )// True if the action should send a completion event
	bool bEndDialog;						AST(NAME("EndDialog")) // If set to true, when this action is executed, the player exits the dialog
	Expression* condition;					AST(NAME("Condition") REDUNDANT_STRUCT("Condition:", parse_Expression_StructParam) LATEBIND SERVER_ONLY)
	Expression* canChooseCondition;			AST(NAME("CanChooseCondition") REDUNDANT_STRUCT("CanChooseCondition:", parse_Expression_StructParam) LATEBIND SERVER_ONLY)
} SpecialDialogAction;
extern ParseTable parse_SpecialDialogAction[];
#define TYPE_parse_SpecialDialogAction SpecialDialogAction

AUTO_ENUM;
typedef enum HeadshotStyleFOV
{
	kHeadshotStyleFOV_Default,	//< 55 deg fov
	kHeadshotStyleFOV_Fisheye,	//< 120 deg fov
	kHeadshotStyleFOV_Telephoto,//< 20 deg fov
	kHeadshotStyleFOV_Portrait, //< 40 deg fov
} HeadshotStyleFOV;

AUTO_STRUCT;
typedef struct HeadshotStyleContactCameraParams
{
	// Positive values place the camera in front of the target and negative values put the camera behind the target.
	F32 fForward;				AST(NAME("Forward"))	

	// Positive values place the camera to the right of the target and negative values put the camera to the left of the target.
	F32 fRight;					AST(NAME("Right"))

	// Positive values place the camera above target and negative values put the camera below the target.
	F32 fAbove;					AST(NAME("Above"))

	// Pitch amount in degrees
	F32 fPitch;					AST(NAME("Pitch"))

	// Yaw amount in degrees
	F32 fYaw;					AST(NAME("Yaw"))

	// Roll amount in degrees
	F32 fRoll;					AST(NAME("Roll"))

	// FOV
	F32 fFOV;					AST(NAME("FOV"))

} HeadshotStyleContactCameraParams;

AUTO_STRUCT;
typedef struct HeadshotStyleFX
{
	const char* pchFXName; AST(STRUCTPARAM POOL_STRING)

	// ...Add FX params when needed
} HeadshotStyleFX;

AUTO_STRUCT 
AST_IGNORE( "OverrideFOV" )
AST_IGNORE( "Forward" )
AST_IGNORE( "Right" )
AST_IGNORE( "Above" )
AST_IGNORE( "Pitch" )
AST_IGNORE( "Yaw" )
AST_IGNORE( "Roll" )
AST_IGNORE( "Tags" )
AST_IGNORE_STRUCT( "UGCProperties" );
typedef struct HeadshotStyleDef
{
	const char* pchFileName;	AST( CURRENTFILE )

	const char* pchName;		AST(STRUCTPARAM KEY POOL_STRING)

	// What texture, if any, to display as a background for the headshot
	const char* pchBackground;	AST(NAME("BackgroundImage") POOL_STRING)

	// Background color to modulate the color of the background texture
	U32 uiBackgroundColor;		AST(NAME("BackgroundColor") SUBTABLE(ColorEnum) DEFAULT(0xFFFFFFFF))

	// What sky file to use
	const char* pchSky;			AST(NAME("Sky") POOL_STRING)

	// Material to go over the headshot image
	const char* pchMaterial;	AST(NAME("Material") POOL_STRING)

	// Frame for the headshot (".headshot" files)
	const char* pchFrame;		AST(NAME("Frame") POOL_STRING)

	// Default anim bits to play for this headshot style
	const char* pchAnimBits;	AST(NAME("DefaultAnimBits") POOL_STRING)

	// Default anim keyword to play for this headshot style
	const char *pcAnimKeyword;	AST(NAME("AnimKeyword") POOL_STRING)

	// Default stance to play for this headshot style
	const char *pchStance;	AST(NAME("Stance") POOL_STRING)

	// Headshot FX to play
	HeadshotStyleFX** eaHeadshotFX; AST(NAME("HeadshotFX"))

	// Use the background image only, don't make the headshot
	bool bUseBackgroundOnly;	AST(NAME("UseBackgroundOnly"))

	// The field of view to use for the headshot
	HeadshotStyleFOV eFOV;		AST(NAME("FOV") SUBTABLE(HeadshotStyleFOVEnum))

	// The parameters contained in this struct is used for the contact camera in the world. Thus actually not respected in the headshot system.
	// Likewise the headshot system does not respect the contact camera params.
	CONST_OPTIONAL_STRUCT(HeadshotStyleContactCameraParams) pContactCameraParams;

	//Used to specify that the frame should be ignored
	bool bIgnoreFrame;
} HeadshotStyleDef;
extern ParseTable parse_HeadshotStyleDef[];
#define TYPE_parse_HeadshotStyleDef HeadshotStyleDef

AUTO_ENUM;
typedef enum PetContactType
{
	PetContactType_AlwaysPropSlot,		//name = AlwaysPropSlot
	PetContactType_Class,				//name = CharacterClass
	PetContactType_Officer,				//Currently unimplemented officer stations
	PetContactType_AllPets				//Ignores the name and uses all the players pets (usually used to narrow down with expressions)
} PetContactType;
extern StaticDefineInt PetContactTypeEnum[];

AUTO_STRUCT;
typedef struct PetContact
{
	const char* pchName;			AST(STRUCTPARAM POOL_STRING)
	Expression* exprCondition;		AST(NAME(ConditionBlock) REDUNDANT_STRUCT(Condition, parse_Expression_StructParam) LATEBIND)
	PetContactType eType;			AST(NAME("Type"))
} PetContact;
extern ParseTable parse_PetContact[];
#define TYPE_parse_PetContact PetContact

AUTO_STRUCT AST_IGNORE(Tags) AST_IGNORE_STRUCT(UGCProperties);
typedef struct PetContactList
{
	const char* pchName;						AST(STRUCTPARAM KEY POOL_STRING)
	PetContact** ppNodes;						AST(NAME("PetContact"))
	REF_TO(CritterDef) hDefaultCritter;			AST(NAME("DefaultCritter"))
	const char* pchFilename;					AST(CURRENTFILE)
} PetContactList;
extern ParseTable parse_PetContactList[];
#define TYPE_parse_PetContactList PetContactList

AUTO_ENUM;
typedef enum ContactCostumeType
{
	ContactCostumeType_Default,			// No override specified, uses default costume (from entity)
	ContactCostumeType_Specified,		// A specific costume
	ContactCostumeType_PetContactList,	// A costume generated from a PetContactList
	ContactCostumeType_CritterGroup,	// A costume generated from a critter group
	ContactCostumeType_Player,			// Use the player's costume
} ContactCostumeType;
extern StaticDefineInt ContactCostumeTypeEnum[];

AUTO_STRUCT;
typedef struct ContactCostume 
{
		ContactCostumeType eCostumeType;				AST(NAME("CostumeType"))

		// Costume override
		REF_TO(PlayerCostume) costumeOverride;			AST(NAME("CostumeOverride"))

		// Use a PetContactList to determine the costume
		REF_TO(PetContactList) hPetOverride;			AST(NAME("UsePetCostume"))


		//-- Use a critter group (or critter group/def from map var) and identifier to determine the costume --//

		// Whether the critter group is specified or gathered from a map variable
		ContactMapVarOverrideType eCostumeCritterGroupType;	AST(NAME("CostumeCritterGroupType"))

		// Critter group
		REF_TO(CritterGroup) hCostumeCritterGroup;		AST(NAME("CostumeCritterGroup"))

		// Map variable of critter group/def to generate costume from
		const char* pchCostumeMapVar;					AST(NAME("CostumeMapVar") POOL_STRING)

		// Identifier
		const char* pchCostumeIdentifier;				AST(NAME("CostumeIdentifier") POOL_STRING)


} ContactCostume;
extern ParseTable parse_ContactCostume[];
#define TYPE_parse_ContactCostume ContactCostume

AUTO_ENUM;
typedef enum ContactSourceType
{
	ContactSourceType_None,
	ContactSourceType_Clicky,
	ContactSourceType_NamedPoint,
	ContactSourceType_Encounter
} ContactSourceType;

AUTO_STRUCT AST_IGNORE("FlavorDialog");
typedef struct SpecialDialogBlock
{
	const char* name;						AST(POOL_STRING) // Unique internal name
	DisplayMessage displayNameMesg;			AST(STRUCT(parse_DisplayMessage))  // Name to be displayed when this option is listed
	REF_TO(ContactDialogFormatterDef) hDisplayNameFormatter;	AST(NAME("DisplayNameFormatter") REFDICT(ContactDialogFormatterDef))
	DialogBlock** dialogBlock;				AST(NAME ("DialogBlock")) // Message(s) to display
	SpecialDialogIndicator eIndicator;		AST(NAME("Indicator"))    // How this dialog should display

	// Costume information for this contact
	ContactCostume costumePrefs;				AST( EMBEDDED_FLAT )

	// Contact Headshot Style Def Override
	const char* pchHeadshotStyleOverride;	AST(NAME("HeadshotStyleOverride") POOL_STRING)

	// The cut-scene to play
	REF_TO(CutsceneDef) hCutSceneDef;		AST(NAME(CutsceneDef) REFDICT(CutScene))

	// Flags which specify specialized behavior for this special dialog block
	SpecialDialogFlags eFlags;				AST(NAME("Flags") FLAGS)
	bool bDelayIfInCombat;					AST(NAME("DelayIfInCombat") BOOLFLAG)

	SpecialDialogAction **dialogActions;

	//The name of the SpecialActionBlock which will be appended to the list of actions
	char *pchAppendName;					AST(NAME("SpecialDialogAppendName"))

	bool bUsesLocalCondExpression;			// If true, this special dialog block uses pCondition as its condition expression.  Otherwise
											// it uses the condition expression of its first dialog block.

	Expression* pCondition;					AST(NAME("Condition") REDUNDANT_STRUCT("Condition:", parse_Expression_StructParam) LATEBIND SERVER_ONLY)

	// The object type that defines the point in the world which is used as the contact camera's origin (optional)
	ContactSourceType eSourceType;

	// The object name that defines the point in the world which is used as the contact camera's origin (optional)
	const char *pchSourceName;

	// The secondary name for the source. This is used in encounters for the actor name (optional)
	const char *pchSourceSecondaryName;

	// The sort order for this special dialog block
	S32 iSortOrder;

	// If this special dialog is an override added by a mission, this field stores the overriding mission.
	REF_TO(MissionDef) overridingMissionDef;	AST(NO_TEXT_SAVE)
} SpecialDialogBlock;
extern ParseTable parse_SpecialDialogBlock[];
#define TYPE_parse_SpecialDialogBlock SpecialDialogBlock

//A group of actions that can be appended to a special dialog
AUTO_STRUCT;
typedef struct SpecialActionBlock
{
	//Unique internal name
	const char* name;						AST(POOL_STRING)

	//The special dialog actions that make up the block
	SpecialDialogAction **dialogActions;

	// If this special action block is an override added by a mission, this field stores the overriding mission.
	REF_TO(MissionDef) overridingMissionDef;	AST(NO_TEXT_SAVE)
} SpecialActionBlock;
extern ParseTable parse_SpecialActionBlock[];
#define TYPE_parse_SpecialActionBlock SpecialActionBlock

AUTO_ENUM;
typedef enum ContactMissionRemoteFlags
{
	ContactMissionRemoteFlag_Grant		= (1<<0), // Whether this mission offer can be granted remotely
	ContactMissionRemoteFlag_Return		= (1<<1), // Whether this mission offer can be returned remotely
} ContactMissionRemoteFlags;
extern StaticDefineInt ContactMissionRemoteFlagsEnum[];

AUTO_ENUM;
typedef enum ContactMissionUIType
{
	ContactMissionUIType_Default		= 0, // Standard behavior
	ContactMissionUIType_FauxTreasureChest, // A contact which is dressed up like a treasure chest in the UI. Mostly for sub-mission turn-ins.
} ContactMissionUIType;
extern StaticDefineInt ContactMissionUITypeEnum[];

AUTO_STRUCT;
typedef struct ContactMissionOffer
{
	// Which mission to grant as a result of accepting this offer
	REF_TO(MissionDef) missionDef;			AST(STRUCTPARAM REFDICT(Mission))

	// If this mission offer is an override added by a mission, this field stores the overriding mission.
	REF_TO(MissionDef) overridingMissionDef;	AST(NO_TEXT_SAVE)

	ContactMissionAllow allowGrantOrReturn; AST(NAME("AllowGrantOrReturn") DEF(ContactMissionAllow_GrantAndReturn))
	
	ContactMissionUIType eUIType;					
	
	// This is the unique name of this mission offer which is used to target it as a special dialog
	const char* pchSpecialDialogName;		AST(NAME("SpecialDialogName") POOL_STRING)

	// Which sub-mission to complete, for ContactMissionAllow_SubMissionComplete
	const char *pchSubMissionName;

	// Dialogs used for greeting the player when the mission is in progress
	DialogBlock** greetingDialog;			AST(NAME("GreetingDialog"))

	// Dialog used by the contact when the mission is being offered
	DialogBlock** offerDialog;				AST(NAME("OfferDialog", "AcceptDialog"))

	// Dialog used by the contact when the player has an in progress mission
	DialogBlock** inProgressDialog;			AST(NAME("InProgressDialog"))

	// Dialog used by the contact when a player turns in a completed mission
	DialogBlock** completedDialog;			AST(NAME("CompleteDialog"))

	// Dialog used by the contact when a player turns in a failed mission
	DialogBlock** failureDialog;			AST(NAME("FailureDialog"))

	// String to use for the accept mission button
	DisplayMessage acceptStringMesg;		AST(STRUCT(parse_DisplayMessage))

	// Dialog formatter used for the mission accept text
	REF_TO(ContactDialogFormatterDef) hAcceptDialogFormatter;	AST(NAME("AcceptDialogFormatter") REFDICT(ContactDialogFormatterDef))

	// The dialog the player is taken to after the player accepts the mission
	const char* pchAcceptTargetDialog;		AST(NAME("AcceptTargetDialog") POOL_STRING)

	// String to use for the decline mission button
	DisplayMessage declineStringMesg;		AST(STRUCT(parse_DisplayMessage))

	// Dialog formatter used for the mission accept text
	REF_TO(ContactDialogFormatterDef) hDeclineDialogFormatter;	AST(NAME("DeclineDialogFormatter") REFDICT(ContactDialogFormatterDef))

	// The dialog the player is taken to after the player declines the mission
	const char* pchDeclineTargetDialog;		AST(NAME("DeclineTargetDialog") POOL_STRING)

	// String to use for the turn in mission button
	DisplayMessage turnInStringMesg;		AST(STRUCT(parse_DisplayMessage))

	// String to use for the complete mission button when there is no reward to choose
	DisplayMessage rewardAcceptMesg;		AST(STRUCT(parse_DisplayMessage))

	// Dialog formatter used for the reward accept text
	REF_TO(ContactDialogFormatterDef) hRewardAcceptDialogFormatter;	AST(NAME("RewardAcceptDialogFormatter") REFDICT(ContactDialogFormatterDef))

	// The dialog the player is taken to after the player completes the mission when there is no reward to choose
	const char* pchRewardAcceptTargetDialog;	AST(NAME("RewardAcceptTargetDialog") POOL_STRING)

	// String to use for the complete mission button when there is a reward to choose
	DisplayMessage rewardChooseMesg;		AST(STRUCT(parse_DisplayMessage))

	// Dialog formatter used for the reward choose text
	REF_TO(ContactDialogFormatterDef) hRewardChooseDialogFormatter;	AST(NAME("RewardChooseDialogFormatter") REFDICT(ContactDialogFormatterDef))

	// The dialog the player is taken to after the player completes the mission when there is a reward to choose
	const char* pchRewardChooseTargetDialog;	AST(NAME("RewardChooseTargetDialog") POOL_STRING)

	// If this message is defined, this message is displayed as a dialog option in addition to back and accept options.
	DisplayMessage rewardAbortMesg;		AST(STRUCT(parse_DisplayMessage))

	// Dialog formatter used for the reward abort text
	REF_TO(ContactDialogFormatterDef) hRewardAbortDialogFormatter;	AST(NAME("RewardAbortDialogFormatter") REFDICT(ContactDialogFormatterDef))

	// The dialog the player is taken to after the player decides to abort the reward accept process
	const char* pchRewardAbortTargetDialog;	AST(NAME("RewardAbortTargetDialog") POOL_STRING)

	// The required allegiances in order to get this offer
	AllegianceRef** eaRequiredAllegiances;	AST(NAME(RequiredAllegiance))

	// Flags which specify the remote behavior of this mission
	ContactMissionRemoteFlags eRemoteFlags;	AST(NAME("RemoteFlags") FLAGS)	

} ContactMissionOffer;
extern ParseTable parse_ContactMissionOffer[];
#define TYPE_parse_ContactMissionOffer ContactMissionOffer

AUTO_STRUCT;
typedef struct ContactLoreDialog
{
	DisplayMessage optionText;				AST(STRUCT(parse_DisplayMessage))
	Expression* pCondition;					AST(NAME("Condition") LATEBIND SERVER_ONLY)
	REF_TO(ItemDef) hLoreItemDef;			AST(REFDICT(ItemDef) NAME("LoreItem") NON_NULL_REF__ERROR_ONLY)

} ContactLoreDialog;


AUTO_STRUCT;
typedef struct StoreRef
{
	REF_TO(StoreDef) ref;					 AST(STRUCTPARAM REFDICT(Store))
} StoreRef;
extern ParseTable parse_StoreRef[];
#define TYPE_parse_StoreRef StoreRef

AUTO_STRUCT;
typedef struct PowerStoreRef
{
	REF_TO(PowerStoreDef) ref;					 AST(STRUCTPARAM REFDICT(PowerStore))
} PowerStoreRef;
extern ParseTable parse_PowerStoreRef[];
#define TYPE_parse_PowerStoreRef PowerStoreRef

AUTO_STRUCT;
typedef struct StoreCollection
{
	DisplayMessage optionText;			AST(STRUCT(parse_DisplayMessage))
	Expression* pCondition;				AST(NAME("Condition"))
	StoreRef** eaStores;				AST(NAME("Store"))
} StoreCollection;
extern ParseTable parse_StoreCollection[];
#define TYPE_parse_StoreCollection StoreCollection

AUTO_STRUCT;
typedef struct AuctionBrokerContactData
{
	DisplayMessage optionText;					AST(STRUCT(parse_DisplayMessage))
	REF_TO(AuctionBrokerDef) hAuctionBrokerDef;	AST(REFDICT(AuctionBroker) NAME("AuctionBrokerDef"))
} AuctionBrokerContactData;
extern ParseTable parse_AuctionBrokerContactData[];
#define TYPE_parse_AuctionBrokerContactData AuctionBrokerContactData

AUTO_STRUCT;
typedef struct UGCSearchAgentData
{
	DisplayMessage optionText;					AST(STRUCT(parse_DisplayMessage))
	DisplayMessage dialogTitle;					AST(STRUCT(parse_DisplayMessage))
	DisplayMessage dialogText;					AST(STRUCT(parse_DisplayMessage))
	char* location;								AST(NAME("Location"))
	int maxDuration;							AST(NAME("maxDuration"))
	int lastNDays;								AST(NAME("LastNDays"))
	int numResults;								AST(NAME("numResults"))
} UGCSearchAgentData;
extern ParseTable parse_UGCSearchAgentData[];
#define TYPE_parse_UGCSearchAgentData UGCSearchAgentData

AUTO_STRUCT;
typedef struct ContactItemAssignmentData
{
	// Defines what kinds of assignments are generated, and how many
	S32* peRarityCounts; AST(NAME(RarityCount) SUBTABLE(ItemAssignmentRarityCountTypeEnum))

	// How often item assignments are refreshed on this contact (if 0, then never refresh)
	U32 uRefreshTime; AST(NAME(RefreshTime))
} ContactItemAssignmentData;

AUTO_STRUCT;
typedef struct ContactImageMenuItem
{ 
	//position 0.0-1.0 of this icon on the image.
	F32 x;							AST(NAME(XPosition))
	F32	y;							AST(NAME(YPosition))
	//the name of the icon to use.
	char* iconImage;				AST(NAME(IconImage))
	//the name of this option.
	DisplayMessage name;			AST(STRUCT(parse_DisplayMessage) NAME(Name))
	//only visible if this condition is met.
	Expression* visibleCondition;			AST(NAME(VisibleCondition) LATEBIND SERVER_ONLY)
	//only choosable if this condition is met.
	Expression* requiresCondition;			AST(NAME(RequiresCondition) LATEBIND SERVER_ONLY)
	//This just sets a UI hint for the player.
	Expression* recommendedCondition;			AST(NAME(RecommendedCondition) LATEBIND SERVER_ONLY)
	//the action to take if this is picked.
	WorldGameActionBlock* action;	AST(NAME(Action) SERVER_ONLY)
	// If this special dialog is an override added by a mission, this field stores the overriding mission.
	REF_TO(MissionDef) overridingMissionDef;	AST(NO_TEXT_SAVE)
}ContactImageMenuItem;

extern ParseTable parse_ContactImageMenuItem[];
#define TYPE_parse_ContactImageMenuItem ContactImageMenuItem

AUTO_STRUCT;
typedef struct ContactImageMenuData
{
	ContactImageMenuItem **items;	AST(NAME(Items))
	ContactImageMenuItem **itemOverrides; AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_3)) // TOK_USEROPTIONBIT_3 is ignored in struct compare and no editor copies for the messages are created
	char* backgroundImage;			AST(NAME(BackgroundImage))
	DisplayMessage title;			AST(STRUCT(parse_DisplayMessage) NAME(Title))
}ContactImageMenuData;
extern ParseTable parse_ContactImageMenuData[];
#define TYPE_parse_ContactImageMenuData ContactImageMenuData

// Defines an NPC that interacts with the player and can provide services
AUTO_STRUCT 
AST_IGNORE("EndDialogAudioName")
AST_IGNORE_STRUCT("DialogNodeWindowPositions");
typedef struct ContactDef
{
	// Contact name that uniquely identifies the contact. Required.
	const char* name;							AST(STRUCTPARAM KEY POOL_STRING)

	// Filename that this contact came from.
	const char* filename;						AST(CURRENTFILE)

	// Scope for the contact
	const char *scope;							AST(POOL_STRING SERVER_ONLY)
	
	// If this contact was created by genesis, this is the zonemap that created it.
	char* genesisZonemap;

	// Contact type (list or single dialog)
	ContactType type;							AST(NAME("ContactType"))

	// Display Name of the Contact
	DisplayMessage displayNameMsg;				AST(STRUCT(parse_DisplayMessage))

	// Dialog exit text override
	DisplayMessage dialogExitTextOverrideMsg;	AST(STRUCT(parse_DisplayMessage))

	// Contains information about the contact (MUO Encyclopedia, history, etc.)
	// Unused
	DisplayMessage infoStringMsg;				AST(STRUCT(parse_DisplayMessage))

	// Costume information for this contact
	ContactCostume costumePrefs;				AST( EMBEDDED_FLAT )

	// Contact Headshot Style Def
	const char* pchHeadshotStyleDef;			AST(NAME("HeadshotStyleDef"))

	// The cut-scene to play
	REF_TO(CutsceneDef) hCutSceneDef;			AST(NAME(CutsceneDef) REFDICT(CutScene))

	// The name of the anim list to play for the headshot entity (Client side only animation)
	REF_TO(AIAnimList) hAnimListToPlay;			AST(NAME("AnimListToPlay"))

	// Requirements to interact with this contact
	Expression* interactReqs;					AST(NAME("CanInteractIf") REDUNDANT_STRUCT("CanInteractIf:", parse_Expression_StructParam) LATEBIND SERVER_ONLY)

	// The map where this contact lives; only used for Mission Search so far
	const char *pchMapName;						AST( POOL_STRING NAME("MapName"))

	// ** Callout Dialog - Dialog spoken by contacts prior to active interaction by players ** \\

	// General Callout Text - Randomly used when player comes nearby the contact. Seen by all players in proximity.
	DialogBlock** generalCallout;				AST(NAME("GeneralCallout"))

	// Mission Callout Text - Triggered when player with an available mission comes the contact. Seen by all players in proximity.
	DialogBlock** missionCallout;				AST(NAME("MissionCallout"))

	// Range Callout Text - Triggered when contact sends a message to a player.
	DialogBlock** rangeCallout;					AST(NAME("RangeCallout"))

	// ** Interaction Dialog - Dialog spoken by contacts in the contact interaction screen ** \\

	// Dialog whenever a player first clicks on the contact
	DialogBlock** greetingDialog;				AST(NAME("GreetingDialog"))

	// Dialog whenever a player is on the contact info screen
	DialogBlock** infoDialog;					AST(NAME("InfoDialog"))

	// Default dialog whenever a player is on the contact info screen
	// Unused
	DialogBlock** defaultDialog;				AST(NAME("DefaultDialog"))

	// Dialog whenever a contact has any missions for a player, displayed above the mission list
	DialogBlock** missionListDialog;			AST(NAME("MissionListDialog"))

	// Dialog whenever a contact currently does not have any missions for a player
	DialogBlock** noMissionsDialog;				AST(NAME("NoMissionDialog"))
	
	// List of offers that the contact will attempt to give the player
	// >>>     DO NOT ACCESS THIS FIELD DIRECTLY!	  <<<
	// *** use contact_GetMissionOfferList() instead! ***
	ContactMissionOffer** offerList;			AST(NAME("MissionOffer:"))
	ContactMissionOffer** eaOfferOverrides;		AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_3)) // TOK_USEROPTIONBIT_3 is ignored in struct compare and no editor copies for the messages are created

	// List of series that the contact will offer to the player
	// Not currently used
//	ContactMissionSeries** seriesList;			AST(NAME("MissionSeries:"))

	// Dialog whenever a player exits the interaction window while having a mission
	// Unused
	DialogBlock** missionExitDialog;			AST(NAME("MissionFarewellDialog"))

	// Dialog whenever a player exits the interaction window
	DialogBlock** exitDialog;					AST(NAME("FarewellDialog"))

	// Dialog when viewing the Mission Search screen
	DialogBlock** eaMissionSearchDialog;		AST(NAME("MissionSearchDialog"))

	// Old format of special dialogs
	DialogBlock** oldSpecialDialog;				AST(NAME("SpecialDialog"))

	// Optional dialogs that show up if their conditions are met
	// >>>     DO NOT ACCESS THIS FIELD DIRECTLY!	  <<<
	// *** use contact_GetSpecialDialogs() or contact_SpecialDialogFromName() instead! ***
	SpecialDialogBlock** specialDialog;			AST(NAME("SpecialDialogs"))
	SpecialDialogBlock ** eaSpecialDialogOverrides;	AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_3)) // TOK_USEROPTIONBIT_3 is ignored in struct compare and no editor copies for the messages are created

	SpecialActionBlock** specialActions;		AST(NAME("SpecialActions"))
	SpecialActionBlock** eaSpecialActionBlockOverrides; AST(NO_TEXT_SAVE USERFLAG(TOK_USEROPTIONBIT_3)) // TOK_USEROPTIONBIT_3 is ignored in struct compare and no editor copies for the messages are created

	// ImageMenu-specific data for this contact
	ContactImageMenuData* pImageMenuData;
	//////pImageMenuData contains **items and **itemOverrides, which follow the pattern of specialDialogBlock and specialActionBlock above.

	// Lore items that this Contact can display
	ContactLoreDialog** eaLoreDialogs;			AST(NAME("LoreDialog"))

	// Stores that this contact has
	StoreRef** stores;							AST(NAME("Stores"))

	// Power Stores that this contact has
	PowerStoreRef** powerStores;				AST(NAME("PowerStore"))

	// Display message for the Buy, Sell, and Buy Back store options
	DisplayMessage buyOptionMsg;				AST(STRUCT(parse_DisplayMessage) NAME("BuyOptionMsg"))
	DisplayMessage sellOptionMsg;				AST(STRUCT(parse_DisplayMessage) NAME("SellOptionMsg"))
	DisplayMessage buyBackOptionMsg;			AST(STRUCT(parse_DisplayMessage) NAME("BuyBackOptionMsg"))

	// Collections of stores (each collection is selectable through a dialog option
	StoreCollection** storeCollections;			AST(NAME("StoreCollection"))

	// The list of auction broker options
	AuctionBrokerContactData **ppAuctionBrokerOptionList;	AST(NAME("AuctionBrokerOption"))

	// The list of UGC Search Agent options
	UGCSearchAgentData **ppUGCSearchAgentOptionList;	AST(NAME("UGCSearchAgentOption"))

	// Dialog to display if the store has no items to buy or sell
	DialogBlock** noStoreItemsDialog;			AST(NAME("NoStoreItemsDialog"))

	// The name of the option for doing a Mission Search
	DisplayMessage missionSearchStringMsg;		AST(STRUCT(parse_DisplayMessage))

	// Which category of OptionalActions this Contact should show (if any)
	const char *pcOptionalActionCategory;		AST( NAME("OptionalActionCategory") POOL_STRING )

	// Which contact types this contact provides
	ContactFlags eContactFlags;					AST(NAME("ContactFlags") FLAGS)

	// Which crafting skill this contact trains
	SkillType skillTrainerType;					AST(NAME("SkillTrainerType", "TrainerType"))

	// Which minigame this contact offers
	MinigameType eMinigameType;					AST(NAME("MinigameType") SUBTABLE(MinigameTypeEnum))

	// The allowed character classes for specific contact types (currently works only for StarshipChooser contacts)
	S32* peAllowedClassCategories;				AST(NAME("AllowedClassCategory"))

	// Expression which determines if the contact is usable remotely
	Expression* canAccessRemotely;				AST(NAME("CanAccessRemotely") REDUNDANT_STRUCT("CanAccessRemotely:", parse_Expression_StructParam) LATEBIND )

	// The list of audio files played whenever the dialog ends.
	EndDialogAudio **eaEndDialogAudios;			AST(NAME("EndDialogAudios"))

	// The object type that defines the point in the world which is used as the contact camera's origin (optional)
	ContactSourceType eSourceType;

	// The object name that defines the point in the world which is used as the contact camera's origin (optional)
	const char *pchSourceName;

	// The secondary name for the source. This is used in encounters for the actor name (optional)
	const char *pchSourceSecondaryName;

	// Overrides the automatically generated contact indicator for this contact
	ContactIndicator eIndicatorOverride;		AST(NAME("ContactIndicatorOverride"))

	// ItemAssignment-specific data for this contact
	ContactItemAssignmentData* pItemAssignmentData;

	// Determines whether or not the UI should display the research dialog
	U32 bIsResearchStoreCollection : 1;	AST(ADDNAMES(IsCraftingStoreCollection))

	// Determines whether or not to hide the contact from the remote contact list
	U32 bHideFromRemoteContactList : 1;

	// Allow setting the last active puppet as the new active puppet (for StarshipChooser contacts)
	U32 bAllowSwitchToLastActivePuppet : 1;

	// Whether or not this contact should update its options every tick
	U32 bUpdateOptionsEveryTick : 1;
} ContactDef;
extern ParseTable parse_ContactDef[];
#define TYPE_parse_ContactDef ContactDef

// This should be used instead of gConf for all settings related to contacts
AUTO_STRUCT;
typedef struct ContactConfig
{
	F32 fTeamDialogResponseQueueTime; AST(NAME(TeamDialogResponseQueueTime) DEFAULT(0.7))
		// The amount of time to wait before making the team dialog choice (in seconds)

	U32 uTeamDialogResponseTimeout; AST(NAME(TeamDialogResponseTimeout) DEFAULT(30))
		// How much time is allowed for team dialog voting

	U32 bIgnoreDistanceCheckForTeamDialogs : 1; AST(NAME(IgnoreDistanceCheckForTeamDialogs))
		// Don't do distance checks for team dialogs

	U32 bServerDecidesChoiceForTeamDialogs : 1; AST(NAME(ServerDecidesChoiceForTeamDialogs))
		// The server makes the dialog choice based on voting results
	
	U32 bTeamDialogAllowRevote : 1; AST(NAME(TeamDialogAllowRevote))
		// Allow players to change their votes

	U32 bMissionOffersUseDedicatedScreenType : 1;	AST(NAME(MissionOffersUseDedicatedScreenType))
		// Mission offer dialogs sets the screen type of the dialog to ContactScreenType_MissionOffer

	U32 bMissionTurnInsUseDedicatedScreenType : 1;	AST(NAME(MissionTurnInsUseDedicatedScreenType))
		// Mission turn in dialogs sets the screen type of the dialog to ContactScreenType_MissionTurnIn

	U32 bCheckLastCompletedDialogInteractable : 1; AST(NAME(CheckLastCompletedDialogInteractable))
		// Changing dialog states will inform the client as to whether or not the last recently completed dialog can be interacted with

	U32 bUseReplayableMissionOfferIndicatorForDialogOptionsWhenAvailable : 1;	AST(NAME(UseReplayableMissionOfferIndicatorForDialogOptionsWhenAvailable))
		// When this option is enabled, the dialog option type for a repeatable mission offer is set to ContactIndicator_MissionRepeatableAvailable

	U32 bUseDifferentIndicatorForRepeatableMissionTurnIns : 1;	AST(NAME(UseDifferentIndicatorForRepeatableMissionTurnIns))
		// When this option is enabled, the repeatable mission turn ins will use the ContactIndicator_MissionCompletedRepeatable instead of ContactIndicator_MissionCompleted

	U32 bShowRemoteContactsFromMissionSearch : 1; AST(NAME(ShowRemoteContactsFromMissionSearch))
		// Search for missions... dialog option should tell client to show "RemoteContacts_Window" Gen

	U32 bIncludeMissionSearchResultContactsInRemoteContacts : 1; AST(NAME(IncludeMissionSearchResultContactsInRemoteContacts))
		// If a Contact is flagged as ShowInSearchResults, then assume it is a RemoteContact as well. Oh boy!
} ContactConfig;

// Master list of contact definitions
extern DictionaryHandle g_ContactDictionary;
extern ContactConfig g_ContactConfig;


bool contact_IsTailor(ContactDef* pContactDef);
bool contact_IsStarshipTailor(ContactDef* pContactDef);
bool contact_IsStarshipChooser(ContactDef* pContactDef);
bool contact_IsWeaponTailor(ContactDef* pContactDef);
bool contact_IsNemesis(ContactDef* pContactDef);
bool contact_IsGuild(ContactDef* pContactDef);
bool contact_IsRespec(ContactDef* pContactDef);
bool contact_IsPowersTrainer(ContactDef* pContactDef);
bool contact_IsBank(ContactDef* pContactDef);
bool contact_IsSharedBank(ContactDef* pContactDef);
bool contact_IsGuildBank(ContactDef* pContactDef);
bool contact_IsMissionSearch(ContactDef* pContactDef);
bool contact_ShowInSearchResults(ContactDef* pContactDef);
bool contact_IsMarket(ContactDef* pContactDef);
// Whether this contact is an auction broker
bool contact_IsAuctionBroker(ContactDef* pContactDef);
bool contact_IsUGCSearchAgent(ContactDef* pContactDef);
bool contact_IsZStore(ContactDef* pContactDef);
bool contact_IsMailbox(ContactDef* pContactDef);
bool contact_IsReplayMissionGiver(ContactDef* pContactDef);
bool contact_IsMinigame(ContactDef* pContactDef);
bool contact_IsImageMenu(ContactDef* pContactDef);
bool contact_IsPuppetVendor(ContactDef* pContactDef);
bool contact_IsItemAssignmentGiver(ContactDef* pContactDef);

// ----------------------------------------------------------------------------
// Contact Dialog state structs and enums
// ----------------------------------------------------------------------------

// This describes all the things a player can do at a Contact
AUTO_ENUM;
typedef enum ContactActionType
{
	ContactActionType_None,
	ContactActionType_AcceptMissionOffer,
	ContactActionType_RestartMission,
	ContactActionType_ReturnMission,
	ContactActionType_DialogComplete,
	ContactActionType_CompleteSubMission,
	ContactActionType_ContactInfo,
	ContactActionType_ChangeCraftingSkill,
	ContactActionType_Respec,
	ContactActionType_RespecAdvantages,
	ContactActionType_MissionSearchSetContactWaypoint,
	ContactActionType_PerformAction,
	ContactActionType_PerformOptionalAction,
	ContactActionType_GiveLoreItem,
	ContactActionType_PowerStoreFromItem,
	ContactActionType_GivePetFromItem,
	ContactActionType_RemoteContacts,
	ContactActionType_PerformImageMenuAction
} ContactActionType;
extern StaticDefineInt ContactActionTypeEnum[];


// This describes the player's current state in a Contact interaction
// This should be server-only; the player just sees the ContactDialog
AUTO_ENUM;
typedef enum ContactDialogState
{
	ContactDialogState_None,

	ContactDialogState_Greeting,
	ContactDialogState_OptionList,
	ContactDialogState_OptionListFarewell,	// The option list, but with the contact's farewell text
	ContactDialogState_ContactInfo,
	ContactDialogState_SpecialDialog,
	ContactDialogState_ViewOfferedNamespaceMission,
	ContactDialogState_ViewOfferedMission,
	ContactDialogState_ViewInProgressMission,
	ContactDialogState_ViewFailedMission,
	ContactDialogState_ViewCompleteMission,
	ContactDialogState_ViewSubMission,
	ContactDialogState_Store,
	ContactDialogState_RecipeStore,
	ContactDialogState_PowerStore,
	ContactDialogState_PowerStoreFromItem,
	ContactDialogState_InjuryStore,
	ContactDialogState_StoreCollection,
	ContactDialogState_TrainerFromEntity,
	ContactDialogState_Tailor,
	ContactDialogState_StarshipTailor,
	ContactDialogState_StarshipChooser,
	ContactDialogState_NewNemesis,
	ContactDialogState_Nemesis,
	ContactDialogState_Guild,
	ContactDialogState_NewGuild,
	ContactDialogState_Respec,
	ContactDialogState_Bank,
	ContactDialogState_SharedBank,
	ContactDialogState_GuildBank,
	ContactDialogState_PowersTrainer,
	ContactDialogState_MissionSearch,
	ContactDialogState_MissionSearchViewContact,
	ContactDialogState_BridgeOfficerOfferSelfOrTraining,
	ContactDialogState_MailBox,
	ContactDialogState_WeaponTailor,

	ContactDialogState_ViewLore,
	ContactDialogState_Market,
	ContactDialogState_Minigame,
	ContactDialogState_ItemAssignments,
	ContactDialogState_AuctionBroker,
	ContactDialogState_UGCSearchAgent,
	ContactDialogState_ImageMenu,
	ContactDialogState_ZStore,

	ContactDialogState_Exit,

} ContactDialogState;
extern StaticDefineInt ContactDialogStateEnum[];


// This describes how the UI should display the contact dialog
AUTO_ENUM;
typedef enum ContactScreenType
{
	ContactScreenType_None,

	// Generic screens that form most of the Contact interactions
	ContactScreenType_List,
	ContactScreenType_Buttons,

	// External screens
	ContactScreenType_Store,
	ContactScreenType_RecipeStore,
	ContactScreenType_PowerStore,
	ContactScreenType_InjuryStore,
	ContactScreenType_InjuryStoreFromPack,
	ContactScreenType_PuppetStore,
	ContactScreenType_StoreCollection,
	ContactScreenType_ResearchStoreCollection,
	ContactScreenType_Tailor,
	ContactScreenType_StarshipTailor,
	ContactScreenType_StarshipChooser,
	ContactScreenType_NewNemesis,
	ContactScreenType_Nemesis,
	ContactScreenType_Guild,
	ContactScreenType_NewGuild,
	ContactScreenType_Respec,
	ContactScreenType_Bank,
	ContactScreenType_SharedBank,
	ContactScreenType_GuildBank,
	ContactScreenType_PowersTrainer,
	ContactScreenType_Market,
	ContactScreenType_AuctionBroker,
	ContactScreenType_UGCSearchAgent,
	ContactScreenType_ZStore,
	ContactScreenType_ImageMenu,
	ContactScreenType_MailBox,
	ContactScreenType_WeaponTailor,
	ContactScreenType_Minigame,
	ContactScreenType_MissionOffer,
	ContactScreenType_MissionTurnIn,
	ContactScreenType_ItemAssignments,
	ContactScreenType_FauxTreasureChest,
	ContactScreenType_WindowedStoreCollection,

} ContactScreenType;
extern StaticDefineInt ContactScreenTypeEnum[];

AUTO_STRUCT;
typedef struct TeamDialogVote
{
	// The player that voted
	ContainerID iEntID;
	
	// Dialog option key the player voted for
	char *pchDialogKey;
} TeamDialogVote;

AUTO_STRUCT;
typedef struct TeamVoteTally
{
	// The dialog option key that was voted for
	char *pchDialogKey; NO_AST

	// The number of votes
	S32 iVoteCount;

	// The priority of this option
	S32 iPriority;
} TeamVoteTally;

// Data used for namespaced mission contacts
AUTO_STRUCT;
typedef struct NamespacedMissionContactData
{
	// Override the mission detail string
	const char *pchMissionDetails;

	// Override the costume
	REF_TO(PlayerCostume) hContactCostume;
	REF_TO(PetContactList) hPetContactList;
} NamespacedMissionContactData;

AUTO_STRUCT;
typedef struct ContactHeadshotData
{
	U32						iPetID;		// Container ID of a pet. If set, the notification will use the pet's costume
	REF_TO(PlayerCostume)	hCostume;
	PlayerCostume*			pCostume;

	const char*				pchHeadshotStyleDef; AST(POOL_STRING)
} ContactHeadshotData;
extern ParseTable parse_ContactHeadshotData[];
#define TYPE_parse_ContactHeadshotData ContactHeadshotData

// Server-only data that is used to execute a ContactDialogOption
AUTO_STRUCT;
typedef struct ContactDialogOptionData
{
	ContactActionType action;
	ContactDialogState targetState;

	// This is the first time the player has viewed the Options List
	bool bFirstOptionsList;
	// This is the first time the player has interacted with this contact
	bool bFirstInteract;
	// Whether or not this PowerStore provides officer training
	bool bIsOfficerTrainer;

	// Cooldown timer.  This option should be disabled until the cooldown timer expires
	U32 uCooldownExpireTime;

	// Mission corresponding to this option
	REF_TO(MissionDef) hRootMission;
	// Sometimes the option refers to 2 missions. e.g: Option turns in a mission and offers another one
	REF_TO(MissionDef) hRootMissionSecondary;
	const char *pchSubMissionName;				AST( POOL_STRING )
	const char *pchSubMissionNameSecondary;		AST( POOL_STRING )

	// If this is a Mission Offer, extra parameters for the offer
	MissionOfferParams *pMissionOfferParams;
	
	// Headshot information
	ContactHeadshotData *pHeadshotData;

	// A Name to display in the contact dialog to go along with the headshot. Optional. Currently only used with MissionOffer Actions
	const char *pchHeadshotDisplayName;					AST( ESTRING )

	// Extra data for namespaced missions
	NamespacedMissionContactData *pNamespacedMissionContactData;

	REF_TO(CharClassCategorySet) hClassCategorySet;

	// Contact corresponding to this option
	REF_TO(ContactDef) hMissionListContactDef;
	REF_TO(ContactDef) hTargetContactDef;
	const char *pchTargetDialogName;	AST( POOL_STRING )
	const char *pchCompletedDialogName;	AST( POOL_STRING )
	int iDialogActionIndex;

	// Optional Action for this option
	char *pchVolumeName;
	int iOptionalActionIndex;
	
	// ItemDef corresponding to this option
	REF_TO(ItemDef) hItemDef;
	
	S32 iEntType;
	U32 iEntID;
	S32 iItemBagID;
	S32 iItemSlot;

	//lots of things have added an "blahblahIndex" int here, probably they could all use one "index".
	//Index of the dialog to display within a special dialog block
	U32 iSpecialDialogSubIndex;

	//Index of the store to display in a store collection
	S32 iStoreCollection;

	//Index of the imageMenuItem to call the action of.
	int iImageMenuItemIndex;

	// Auction broker def associated with the option
	REF_TO(AuctionBrokerDef) hAuctionBrokerDef;

	//info for UGC Search Agent:
	//client needs whole thing: some fields to draw UI and some to make the search request.
	UGCSearchAgentData* pUGCSearchAgentData;	AST( NAME(UGCSearchAgentData))

} ContactDialogOptionData;
extern ParseTable parse_ContactDialogOptionData[];
#define TYPE_parse_ContactDialogOptionData ContactDialogOptionData

AUTO_STRUCT;
typedef struct ContactDialogOption
{
	// -- All the info the client needs to display this option --
	char* pchKey;				// Unique key used to specify this option in the client's response
	char* pchDisplayString;				AST( ESTRING )
	REF_TO(ContactDialogFormatterDef) hDialogFormatter; AST(NAME("DialogFormatter") REFDICT(ContactDialogFormatterDef))
	ContactIndicator eType;

	// Confirmation dialog required to select this option
	bool bNeedsConfirm;
	char* pchConfirmHeader;				AST( ESTRING )
	char* pchConfirmText;				AST( ESTRING )

	//ImageMenu uses these:
	char* pchIconName;					AST( ESTRING NAME(IconName))
	F32 xPos;							AST( NAME(XPos))
	F32 yPos;							AST( NAME(YPos))
		//this is used by the client to add some UI frills like "you are here".
	const char* pchMapName;				AST( NAME(MapName) POOL_STRING )	//filled in by server and used by client for YouAreHere, WaypointHere, GoldenPathWaypointHere
	bool bYouAreHere;					AST(CLIENT_ONLY)
	bool bWaypointHere;					AST(CLIENT_ONLY)
	bool bGoldenPathWaypointHere;		AST(CLIENT_ONLY)
	bool bRecommended;			//filled in by server based on recommendedExpression.

	// Show a reward chooser
	bool bShowRewardChooser;

	// Cooldown Timer
	U32 uCooldownExpireTime;

	// This is the default "Back" or "Exit" option
	bool bIsDefaultBackOption;

	// Indicates if this dialog option is selected by the player before.
	bool bVisited;

	// Indicates whether the player is eligible to choose this dialog option.
	bool bCannotChoose;

	//Indicates whether the option was appended from a special action block
	bool bWasAppended;

	// Indicates which team members are eligible to see this dialog option. Used only in critical team dialogs.
	U32 *piTeamMembersEligibleToSee;

	// Indicates which team members are eligible to choose this dialog option. Used only in critical team dialogs.
	U32 *piTeamMembersEligibleToInteract;

	// -- Server-only data that is used to execute to this option --
	ContactDialogOptionData *pData;		AST( SERVER_ONLY )

} ContactDialogOption;
extern ParseTable parse_ContactDialogOption[];
#define TYPE_parse_ContactDialogOption ContactDialogOption

AUTO_STRUCT;
typedef struct ContactDialogStoreProvisioning
{
	U32 eGroupProjectType;					// Group project type for provisioning
	const char *pchGroupProjectDef;			AST( POOL_STRING ) // Group project that has the provisioning numeric
	const char *pchGroupProjectNumericDef;	AST( POOL_STRING ) // Provisioning numeric
	char *estrNumericName;					AST( NAME(NumericName) ESTRING ) // The display name of the numeric
	S32 iNumericValue;						// The current value of the numeric
	bool bStoreGuildMapOwnerMembersOnly;	// Set when only guild members of the guild owned map may purchase from the vendor
	const char **eapchStores;				AST( POOL_STRING )
} ContactDialogStoreProvisioning;

// This structure is the only thing sent to the client - it completely defines
// a Contact Dialog screen.
AUTO_STRUCT 
AST_IGNORE("AnimBitsToPlay");
typedef struct ContactDialog
{
	// The basic layout of the screen
	ContactScreenType screenType;

	// Text
	char *pchContactDispName;							AST( ESTRING )
	char *pchDialogHeader;								AST( ESTRING )
	char *pchDialogText1;								AST( ESTRING )
	REF_TO(ContactDialogFormatterDef) hDialogText1Formatter; AST( NAME("DialogText1Formatter") REFDICT(ContactDialogFormatterDef) )
	char *pchDialogHeader2;								AST( ESTRING )
	char *pchDialogText2;								AST( ESTRING )
	char *pchListHeader;								AST( ESTRING )
	const char *pchVoicePath;							AST( POOL_STRING )
	const char *pchPhrasePath;							AST( POOL_STRING )
	const char *pchSoundToPlay;							AST( POOL_STRING )
	REF_TO(AIAnimList) hAnimListToPlayForActiveEntity;	AST( NAME("AnimListToPlayForActiveEntity", "AnimListToPlay") )
	REF_TO(AIAnimList) hAnimListToPlayForPassiveEntity;	AST( NAME("AnimListToPlayForPassiveEntity") )
	
	// The position of the camera source. ZeroVec is used for unset state.
	Vec3 vecCameraSourcePos;
	// The rotation of the camera source. Respected only if the camera source is set.
	Quat quatCameraSourceRot;
	// The camera source entity (if any)
	EntityRef cameraSourceEnt;

	// Headshot
	EntityRef headshotEnt;
	REF_TO(PlayerCostume)	hHeadshotOverride;
	PlayerCostume			*pHeadshotOverride; //Not all costumes are in the dictionary - there are auto generated costumes
	const char* pchHeadshotStyleDef;	AST( POOL_STRING )
	
	// Pet contact list headshot overrides
	U32 iHeadshotOverridePetID;						// Container ID of the pet. Set if a pet is found

	// Response Options
	ContactDialogOption** eaOptions;

	// Determines whether the player can respond to dialog options
	bool bViewOnlyDialog;

	// Determines if this dialog is a team dialog and managed by the player
	bool bIsTeamSpokesman;

	// Whether or not this player has voted
	bool bHasVoted;
	
	// If this flag is not set, then sell item info needs to be updated
	bool bSellInfoUpdated;

	// Whether or not selling is enabled for this store
	bool bSellEnabled;

	// If this flag is set, then it uses CPoints instead of resources
	bool bDisplayStoreCPoints;

	// Whether or not the player is currently researching from a store
	bool bIsResearching;

	// Whether or not the current PowerStore trains officers
	bool bIsOfficerTrainer;

	// Whether or not the player has a recently completed dialog to interact with
	bool bLastCompletedDialogIsInteractable;

	// Contact indicator
	ContactIndicator eIndicator;

	// Team dialogs

	// Container ID for the team spokesman
	ContainerID iTeamSpokesmanID;

	// The number of team members watching the conversation (does not include the talker)
	S32 iParticipatingTeamMemberCount;

	// When the team dialog started
	U32 uTeamDialogStartTime;

	// Voting information for team dialogs
	TeamDialogVote** eaTeamDialogVotes;

	// Filtered assignment list sent to the player
	ItemAssignmentDefRef** eaItemAssignments;

	// The rate at which item assignments are refreshed
	U32 uItemAssignmentRefreshTime;

	// Rewards to display
	char *pchRewardsHeader;				AST( ESTRING )
	char *pchOptionalRewardsHeader;		AST( ESTRING )
	InventoryBag** eaRewardBags;		AST( NO_INDEX )

	// Vendors
	StoreItemInfo** eaStoreItems;
	StoreItemInfo** eaUnavailableStoreItems;		AST( SERVER_ONLY)		// Store off items which are unavailable at a given point in time.
																		// We don't want them sent to the client. But need to check periodically
																		// If they've become available.
	StoreDiscountInfo** eaStoreDiscounts;
	PowerStorePowerInfo** eaStorePowers;
	StoreSellableItemInfo** eaSellableItemInfo;
	REF_TO(ItemDef) hStoreCurrency;			//Currently only used by sell stores
	WorldRegionType eStoreRegion;			AST(DEF(WRT_None))		// Used to drive UI
	S32 iCurrentStoreCollection;			// Selected store collection
	char *pchBuyOptionText;					AST( ESTRING )	// Translated display text for the "Buy" tab
	char *pchSellOptionText;				AST( ESTRING )	// Translated display text for the "Sell" tab
	char *pchBuyBackOptionText;				AST( ESTRING )	// Translated display text for the "Buy Back" tab
	ContactDialogStoreProvisioning **eaProvisioning;
	
	// Trainer
	SkillType iSkillType;
	// Minigames
	MinigameType eMinigameType;
	
	//info for UGC Search Agent:
	//client needs whole thing: some fields to draw UI and some to make the search request.
	UGCSearchAgentData* pUGCSearchAgentData;	AST( NAME(UGCSearchAgentData))

	//info for ImageMenu:
	char* pchBackgroundImage;					AST( ESTRING NAME(BackgroundImage))
	
	// Puppet choosers
	S32* peAllowedClassCategories;			AST(SUBTABLE(CharClassCategoryEnum))

	// Mission Offer details
	U32 uMissionTimeLimit;                    AST(NAME("MissionTimeLimit"))
	U32 uMissionExpiredTime;                  AST(NAME("MissionExpiredTime"))
	MissionLockoutType eMissionLockoutType;   AST(NAME("MissionLockoutType"))
	MissionCreditType eMissionCreditType;     AST(NAME("MissionCreditType"))
	int iTeamSize;                            AST(NAME("TeamSize"))
	bool bScalesForTeam;                      AST(NAME("ScalesForTeam"))

	// Mission UI Type
	MissionUIType eMissionUIType;

	ContactType eContactType;

	// Display string for the last response
	char *pchLastResponseDisplayString;		AST( ESTRING )

	// The version number. The number is incremented each time the dialog state changes.
	// Initial use for this field is for UI to detect changes in the dialog state.
	U32 uiVersion;

	// -- Server-only fields that the client shouldn't care about --
	ContactDialogState state;			AST( SERVER_ONLY )
	
	EntityRef contactEnt;				AST( SERVER_ONLY )
	REF_TO(ContactDef) hContactDef;		AST( SERVER_ONLY )
	const char *pchSpecialDialogName;	AST( SERVER_ONLY POOL_STRING)
	U32 iSpecialDialogSubIndex;			AST( SERVER_ONLY )

	REF_TO(MissionDef) hRootMissionDef;	AST( SERVER_ONLY )
	const char *pchSubMissionName;		AST( SERVER_ONLY POOL_STRING )

	
	S32 iEntType;						AST( SERVER_ONLY )
	U32 iEntID;							AST( SERVER_ONLY )
	S32 iItemBagID;						AST( SERVER_ONLY )
	S32 iItemSlot;						AST( SERVER_ONLY )
	U64 iItemID;						AST( SERVER_ONLY )
	bool bPartialPermissions;			AST( SERVER_ONLY )
	bool bRemotelyAccessing;
	bool bForceOnTeamDone;				AST( SERVER_ONLY )
	bool bTeamDialogChoiceMade;			AST( SERVER_ONLY )
	bool bGeneratedItemAssignments;		AST( SERVER_ONLY )

	U32 uItemAssignmentRefreshIndex;	AST( SERVER_ONLY )

	U32 uLastPersistedStoreVersion;		AST( SERVER_ONLY )
	REF_TO(StoreDef) hPersistStoreDef;	AST( SERVER_ONLY USERFLAG(TOK_USEROPTIONBIT_1) )
	REF_TO(PersistedStore) hPersistedStore;	AST(SERVER_ONLY COPYDICT(PersistedStore) FORMATSTRING(DEPENDENT_CONTAINER_TYPE = "PersistedStore") USERFLAG(TOK_USEROPTIONBIT_1))

	REF_TO(StoreDef) hSellStore;		AST( SERVER_ONLY REFDICT(Store) )

	// The cut-scene to be played
	REF_TO(CutsceneDef) hCutSceneDef;	AST(NAME(CutsceneDef) REFDICT(CutScene))

	// All item assignments
	ItemAssignmentDefRef** eaAllItemAssignments; AST(SERVER_ONLY)

	// Research timer server fields
	const char *pchResearchTimerStoreName;	AST(SERVER_ONLY POOL_STRING)
	U32 uResearchTimerStoreItemIndex;		AST(SERVER_ONLY)
	U32 uStoreResearchTimeExpire;			AST(SERVER_ONLY)

	// Currently selected auction broker def
	REF_TO(AuctionBrokerDef) hAuctionBrokerDef;	AST(SERVER_ONLY)
	const AuctionBrokerLevelInfo *pAuctionBrokerLastUsedLevelInfo; 

	U32 uiShipTailorEntityID;

} ContactDialog;
extern ParseTable parse_ContactDialog[];
#define TYPE_parse_ContactDialog ContactDialog


// ----------------------------------------------------------------------------
// Other Contact system structs
// ----------------------------------------------------------------------------

// Sent from the client when turning in a mission
AUTO_STRUCT;
typedef struct ContactRewardChoices
{
	char** ppItemNames;
} ContactRewardChoices;
extern ParseTable parse_ContactRewardChoices[];
#define TYPE_parse_ContactRewardChoices ContactRewardChoices

AUTO_STRUCT;
typedef struct ContactDialogInfo
{
	REF_TO(ContactDef) hContact;
	const char *pcDialogName;					AST(POOL_STRING)
} ContactDialogInfo;
extern ParseTable parse_ContactDialogInfo[];
#define TYPE_parse_ContactDialogInfo ContactDialogInfo

AUTO_STRUCT;
typedef struct ContactCostumeFallback
{
	char* pchDisplayName;
	REF_TO(PlayerCostume) hCostume;
} ContactCostumeFallback;
extern ParseTable parse_ContactCostumeFallback[];
#define TYPE_parse_ContactCostumeFallback ContactCostumeFallback


AUTO_STRUCT;
typedef struct QueuedContactDialog
{
	ContactDialogInfo* pContactDialog;
	bool bPartialPermissions;
	bool bRemotelyAccessing;
	ContactCostumeFallback* pCostumeFallback;
	ContactDialogOptionData* pOptionData;
} QueuedContactDialog;
extern ParseTable parse_QueuedContactDialog[];
#define TYPE_parse_QueuedContactDialog QueuedContactDialog

// Information about a nearby Contact
AUTO_STRUCT;
typedef struct ContactInfo
{
	// Ref for the entity that has this contact def
	EntityRef entRef;

	// The contact def that some entity has
	const char *pchContactDef;					AST(POOL_STRING)

	// The Static Encounter this contact came from
	const char *pchStaticEncName;				AST(POOL_STRING)

	// Current indicator of the contact's state with the player
	ContactIndicator currIndicator;	
	int nextOfferLevel;

	ContactFlags eFlags;
	
	// For puppet chooser contacts: Array of allowed class categories
	S32* peAllowedClassCategories;

	// eArray of indexes of valid special dialogs for this player and contact
	int* availableSpecialDialogs;

	// Set to true if the contact has already done his callout to player dialog
	bool calledOutToPlayer;						NO_AST

	// For puppet chooser contacts: Allow setting the last active puppet as the active puppet
	bool bAllowSwitchToLastActivePuppet;
} ContactInfo;
extern ParseTable parse_ContactInfo[];
#define TYPE_parse_ContactInfo ContactInfo

AUTO_STRUCT;
typedef struct CritterInteractInfo
{
	EntityRef erRef; AST(KEY)
		// The entity ref of the critter

	ContactIndicator currIndicator;
		// This critter will get a contact indicator if it has a contact game action on it

	REF_TO(ContactDef) hActionContactDef; AST(SERVER_ONLY)
		// The game action contact on in this interact
} CritterInteractInfo;
extern ParseTable parse_CritterInteractInfo[];
#define TYPE_parse_CritterInteractInfo CritterInteractInfo

// Logged contact message
AUTO_STRUCT;
typedef struct ContactLogEntry
{
	char* pchName;			AST(ESTRING)
	char* pchText;			AST(ESTRING)
	const char* pchHeadshotStyleDef;			AST(POOL_STRING)
	REF_TO(PlayerCostume) hHeadshotCostumeRef;	AST(POOL_STRING)
	PlayerCostume *pHeadshotCostume;
	EntityRef erHeadshotEntity;
	U32 iHeadshotPetID;
	U32 uiTimestamp;
} ContactLogEntry;
extern ParseTable parse_ContactLogEntry[];
#define TYPE_parse_ContactLogEntry ContactLogEntry

AUTO_STRUCT;
typedef struct RemoteContactOption
{
	char* pchKey;					AST(KEY)
	char* pchDescription1;			
	char* pchDescription2;			
	ContactDialogOption* pOption;

	REF_TO(Message) hMissionDisplayName;
	REF_TO(MissionCategory) hMissionCategory;
	const char *pcMissionName;		AST(POOL_STRING)

	bool bNew;						AST(CLIENT_ONLY)
	bool bDescriptionRequested;		AST(CLIENT_ONLY)
	bool bDirty;					NO_AST
} RemoteContactOption;
extern ParseTable parse_RemoteContactOption[];
#define TYPE_parse_RemoteContactOption RemoteContactOption

AUTO_STRUCT;
typedef struct RemoteContact
{
	const char* pchContactDef;					AST(POOL_STRING KEY)						
	REF_TO(Message) hDisplayNameMsg;		
	ContactFlags eFlags;						AST(FLAGS)
	S32 iPriority;	
	S32 iVersion;								NO_AST
		// The version is incremented when this remote contact changes in the dictionary
	
	ContactHeadshotData* pHeadshot;				AST(LATEBIND)
	bool bHeadshotRequested;					AST(CLIENT_ONLY)
	bool bIsNew;								AST(CLIENT_ONLY)

	char *estrFormattedContactName;				AST(ESTRING)
	
	RemoteContactOption** eaOptions;
} RemoteContact;
extern ParseTable parse_RemoteContact[];
#define TYPE_parse_RemoteContact RemoteContact

// Represents a team member in a team dialog
AUTO_STRUCT;
typedef struct TeamDialogParticipantData
{
	Entity *pEnt;		AST(UNOWNED)
	bool bIsTeamSpokesman;
	char *pchName;
	char *pchVotedDialogKey;
} TeamDialogParticipantData;
extern ParseTable parse_TeamDialogParticipantData[];
#define TYPE_parse_TeamDialogParticipantData TeamDialogParticipantData

AUTO_STRUCT;
typedef struct SpecialDialogOverrideData
{
	const char* pchSourceName;					AST(POOL_STRING) // Name of object which this override is created from
	const char* pchFilename;					AST(POOL_STRING) // Filename of object which this override is created from
	SpecialDialogBlock *pSpecialDialogBlock;
} SpecialDialogOverrideData;
extern ParseTable parse_SpecialDialogOverrideData[];
#define TYPE_parse_SpecialDialogOverrideData SpecialDialogOverrideData

AUTO_STRUCT;
typedef struct SpecialActionBlockOverrideData
{
	const char* pchSourceName;					AST(POOL_STRING) // Name of object which this override is created from
	const char* pchFilename;					AST(POOL_STRING) // Filename of object which this override is created from
	SpecialActionBlock *pSpecialActionBlock;
} SpecialActionBlockOverrideData;
extern ParseTable parse_SpecialActionBlockOverrideData[];
#define TYPE_parse_SpecialActionBlockOverrideData SpecialActionBlockOverrideData

AUTO_STRUCT;
typedef struct MissionOfferOverrideData
{
	const char* pchSourceName;				AST(POOL_STRING) // Name of object which this override is created from
	ContactMissionOffer *pMissionOffer;
} MissionOfferOverrideData;
extern ParseTable parse_MissionOfferOverrideData[];
#define TYPE_parse_MissionOfferOverrideData MissionOfferOverrideData

AUTO_STRUCT;
typedef struct ImageMenuItemOverrideData
{
	const char* pchSourceName;				AST(POOL_STRING) // Name of object which this override is created from
	ContactImageMenuItem *pItem;
} ImageMenuItemOverrideData;
extern ParseTable parse_ImageMenuItemOverrideData[];
#define TYPE_parse_ImageMenuItemOverrideData ImageMenuItemOverrideData

AUTO_STRUCT;
typedef struct ContactOverrideData
{
	const char* pchContact;		AST(POOL_STRING KEY)
	SpecialDialogOverrideData** eaSpecialDialogOverrides;
	SpecialActionBlockOverrideData** eaSpecialActionBlockOverrides;
	MissionOfferOverrideData** eaMissionOfferOverrides;
	ImageMenuItemOverrideData** eaImageMenuItemOverrides;
} ContactOverrideData;
extern ParseTable parse_ContactOverrideData[];
#define TYPE_parse_ContactOverrideData ContactOverrideData

// ----------------------------------------------------------------------------
// Function headers
// ----------------------------------------------------------------------------

// -- ContactDef load/utility functions --

int contact_LoadDefs(void);

bool contact_Validate(ContactDef* def);
void contact_ValidateAll(void);
bool contact_ValidateSpecialDialogBlock(ContactDef* def, SpecialDialogBlock *block, const char* pchOwningMission, SpecialDialogOverride** eaOverridesToInclude, MissionOfferOverride **eaOfferOverridesToInclude);
bool contact_ValidateMissionOffer(ContactDef *def, ContactMissionOffer *offer, const char* pchOwningMission, SpecialDialogOverride** eaOverridesToInclude, MissionOfferOverride **eaOfferOverridesToInclude);
bool contact_ValidateImageMenuItem(ContactDef* def, ContactImageMenuItem *pItem, const char* pchOwningMission);

// Gets the contact definition from the unique contact name
ContactDef* contact_DefFromName(const char* contactName);

// Gets the contact definition from the unique contact name
HeadshotStyleDef* contact_HeadshotStyleDefFromName(const char* pchHeadshotStyleName);
// HeadshotStyle accessors
F32 contact_HeadshotStyleDefGetFOV(HeadshotStyleDef* pStyle, F32 fFallbackFOV);
Color contact_HeadshotStyleDefGetBackgroundColor(HeadshotStyleDef* pStyle);
void contact_HeadshotStyleDefSetAnimBits(DynBitFieldGroup* pBitFieldGroup, HeadshotStyleDef* pStyle, const char* pchFallbackAnimBits);
const char* contact_HeadshotStyleDefGetFrame(HeadshotStyleDef* pStyle);
const char* contact_HeadshotStyleDefGetSky(HeadshotStyleDef* pStyle);

// Returns TRUE if this ContactDef is set up to skip the Options List and only show a single Option
bool contact_IsSingleScreen(ContactDef *pContactDef);

// -- Contact Interaction functions --

bool contact_CanApplySpecialDialogOverride(const char *pchMissionName, const char *pchFilename, ContactDef *pContactDef, SpecialDialogBlock *pDialog, bool bSilent);
bool contact_CanApplyMissionOfferOverride(const char *pchMissionName, const char *pchFilename, ContactDef *pContactDef, ContactMissionOffer *pOffer, bool bSilent);
bool contact_CanApplyImageMenuItemOverride(const char *pchMissionName, const char *pchFilename, ContactDef *pContactDef, ContactImageMenuItem *pItem, bool bSilent);
// Returns true if pOffer points to pMissionDef
bool contact_MissionOfferCheckMissionDef(ContactMissionOffer* pOffer, MissionDef* pMissionDef);
// Returns the index in eaOfferList if this mission is offered in the list
int contact_FindMissionOfferInList(ContactMissionOffer** eaOfferList, MissionDef* pMissionDef);
// Returns the ContactMissionOffer if this mission is offered by the contact
ContactMissionOffer* contact_GetMissionOffer(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_OP_VALID Entity* pEnt, SA_PARAM_NN_VALID MissionDef* missionDef);

void contact_GetMissionOfferList(ContactDef* pContact, Entity* pEnt, ContactMissionOffer*** peaReturnList);

SpecialDialogBlock* contact_SpecialDialogFromName(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_STR const char* name);
ContactImageMenuItem* contact_ImageMenuItemFromName(ContactDef* contact, int index);

void contact_GetSpecialDialogsEx(ContactDef* pContact, SpecialDialogBlock*** peaReturnList, bool* pbListSorted);
#define contact_GetSpecialDialogs(pContact, peaReturnList) contact_GetSpecialDialogsEx(pContact, peaReturnList, NULL)
void contact_GetImageMenuItems(ContactDef* pContact, ContactImageMenuItem*** peaReturnList);

bool contact_HasSpecialDialog(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_STR const char* dialog_name);
bool contact_IsNearSharedBank(SA_PARAM_OP_VALID Entity *pEnt);
bool contact_IsNearMailBox(SA_PARAM_OP_VALID Entity *pEnt);
bool contact_IsNearRespec(SA_PARAM_OP_VALID Entity *pEnt);
int remoteContact_CompareNames(const RemoteContact* a, const RemoteContact* b);
// Compares two remote contacts based on newness, then by priority, then by name
int remoteContact_Compare(const RemoteContact** a, const RemoteContact** b);
// Scans through a contactDef and determines the remote capabilities of that contact
ContactFlags contact_GenerateRemoteFlags(SA_PARAM_NN_VALID ContactDef* pContact);

// Process the PetContactList for the given player entity.
//		If we find an actual pet, return it in pPetEntity
//		If we can't, fill out the CritterDef and CritterCostume fields based on any fallbacks
//		If the excludeList is not NULL, skip any pet's that have names that are in the list. (Used for Bridge Encounter setup)
void PetContactList_GetPetOrCostume(Entity* pEntPlayer, PetContactList* pList, const char*** peaExcludeList, Entity** ppPetEntity, CritterDef** ppCritterDef,
									CritterCostume** ppCostume);

// Generate the expressions for the PetContactList
int PetContactList_PostProcess(PetContactList* def);

// Get a random default costume from the default CritterDef
CritterCostume* PetContactList_GetDefaultCostume(SA_PARAM_NN_VALID PetContactList* pList, Entity* pPlayerEnt, const char*** peaExcludeList);

ExprFuncTable* contact_CreateExprFuncTable(void);

void contact_SpecialDialogPostProcess(SpecialDialogBlock* pDialog, const char* pchFilename);
void contact_SpecialActionPostProcess(SpecialActionBlock* pAction, const char* pchFilename);
void contact_ImageMenuItemPostProcess(ContactImageMenuItem* pItem, const char* pchFilename);

ContactMissionOffer * contact_MissionOfferFromSpecialDialogName(ContactDef* pContactDef, const char* pchSpecialDialogName);

//Returns the number of actions in a special dialog block including any that are appended from another block
int contact_getNumberOfSpecialDialogActions(ContactDef *pContactDef, SpecialDialogBlock *pDialog);

//Returns the special dialog action from a block's actions array and any actions that are appended from another block
SpecialDialogAction *contact_getSpecialDialogActionByIndex(ContactDef *pContactDef, SpecialDialogBlock *pDialog, int iActionIndex);

void contact_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);

#ifdef GAMESERVER

void contact_MissionRemovedOrPreModifiedFixup(SA_PARAM_NN_VALID MissionDef *pMissionDef, SA_PARAM_OP_VALID ContactDef ***peaContactsToUpdate);
void contact_MissionAddedOrPostModifiedFixup(SA_PARAM_NN_VALID MissionDef *pMissionDef, SA_PARAM_OP_VALID ContactDef *** peaContactsToUpdate);

#endif

#endif
