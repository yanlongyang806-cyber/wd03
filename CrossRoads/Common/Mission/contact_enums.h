#pragma once
GCC_SYSTEM

typedef struct StaticDefineInt StaticDefineInt;
typedef struct DefineContext DefineContext;

// Contact indication state, used to show the player a contact has something interesting
// This should be in order of display priority - lowest priority first.
// We allow for data-defined extensions to this list. Any data-defined enums are considered to
// be lower priority than than MissionFlashbackAvailable.
// Don't change the names of these, they are used to generate the names of FX/texture files

extern DefineContext* g_pContactIndicatorEnums;
AUTO_ENUM AEN_EXTEND_WITH_DYNLIST(g_pContactIndicatorEnums);
typedef enum ContactIndicator
{
	ContactIndicator_NoInfo,
	
	ContactIndicator_PlayerTooLow,

	// this will currently only be used for some games, via a gConf.  It will take the place of HasInfoDialog.
	// The rationale here is that it is unlikely that a game will have 2 different levels of info dialog that it wishes to
	// slice this finely, but we don't want to change the behavior of STO.  This won't work with the multiple indicators feature. [RMARR - 12/7/12]
	ContactIndicator_LowImportanceInfo,

	ContactIndicator_MissionInProgress,

	ContactIndicator_HasInfoDialog,
	
	ContactIndicator_Market,
	ContactIndicator_AuctionBroker,
	ContactIndicator_UGCSearchAgent,
	ContactIndicator_ImageMenu,
	ContactIndicator_SuperGroupBank,
	ContactIndicator_Bank,
	ContactIndicator_SharedBank,
	ContactIndicator_MailBox,
	ContactIndicator_Vendor,
	ContactIndicator_SuperGroup,
	ContactIndicator_StarshipChooser,
	ContactIndicator_Nemesis,
	ContactIndicator_Tailor,
	ContactIndicator_PowerTrainer,
	ContactIndicator_SkillTrainer,
	ContactIndicator_InjuryHealer,
	ContactIndicator_InjuryHealer_Ground,
	ContactIndicator_InjuryHealer_Space,
	ContactIndicator_Minigame,
	ContactIndicator_ItemAssignments,
	ContactIndicator_LoreWindowed,
	ContactIndicator_LoreFullScreen,
	ContactIndicator_ZStore,

	// NOTE: If you are adding a new option here, you may want to consider adding it as a data-defined
	//    type in data/defs/config/ContactIndicatorEnums.def
	ContactIndicator_Multiple,				// More than one of the non-Mission options above or user defined options

	// Mission state indicators. These are treated as higher priority than any of the previous indicators
	//  And also higher priority than any data-defined types that may have higher enum values
	// NOTE: If these are changed please check to see that contact_GetBestIndicatorState in gslContact.c
	//  is updated properly 
	
	ContactIndicator_MissionFlashbackAvailable,
	ContactIndicator_MissionRepeatableAvailable,
	ContactIndicator_MissionAvailable,
	ContactIndicator_HasImportantDialog,
	ContactIndicator_HasGoto,
	ContactIndicator_MissionCompleted,
	ContactIndicator_MissionCompletedRepeatable,
	
	// We allow data defined indicators.  See Contact_LoadContactIndicatorEnum in contact_common.c
	ContactIndicator_FirstDataDefined, EIGNORE

	// We can't use an enum for the total count anymore because of the user defined types
} ContactIndicator;

extern StaticDefineInt ContactIndicatorEnum[];

AUTO_ENUM;
typedef enum SpecialDialogIndicator
{
	SpecialDialogIndicator_Info,
	SpecialDialogIndicator_Unimportant,
	SpecialDialogIndicator_Important,
	SpecialDialogIndicator_Goto,
} SpecialDialogIndicator;
extern StaticDefineInt SpecialDialogIndicatorEnum[];

AUTO_ENUM;
typedef enum ContactFlags
{
	ContactFlag_Tailor				= (1<<0), // Whether this contact is a tailor
	ContactFlag_StarshipTailor		= (1<<1), // Whether this contact is a starship tailor
	ContactFlag_StarshipChooser		= (1<<2), // Whether this contact allows you to change your active starship
	ContactFlag_Nemesis				= (1<<3), // Whether this contact is a nemesis liason
	ContactFlag_Guild				= (1<<4), // Whether this contact is a guild registrar
	ContactFlag_Respec				= (1<<5), // Whether this contact performs respecs
	ContactFlag_PowersTrainer		= (1<<6), // Whether this Contact is a Powers Trainer
	ContactFlag_Bank				= (1<<7), // Whether this contact is a banker
	ContactFlag_GuildBank			= (1<<8), // Whether this contact is a guild banker
	ContactFlag_MissionSearch		= (1<<9), // Whether this contact is a "Crime Computer" that helps the player search for more missions
	ContactFlag_ShowInSearchResults = (1<<10), // Whether this contact is included in Mission Search results
	ContactFlag_Market				= (1<<11),// Whether this contact is a user marketplace/auction
	ContactFlag_MailBox				= (1<<12), // Whether this contact is a mailbox, where you can send/receive items
	ContactFlag_WeaponTailor		= (1<<13), // Whether this contact is a weapon tailor
	ContactFlag_RemoteSpecDialog	= (1<<14), // Marked if the contact has a canAccessRemotely expression and a special dialog block
	ContactFlag_RemoteOfferGrant	= (1<<15), // Marked if the contact has a canAccessRemotely expression and a mission grant, or a remote mission grant
	ContactFlag_RemoteOfferReturn	= (1<<16), // Marked if the contact has a canAccessRemotely expression and a mission return, or a remote mission return
	ContactFlag_RemoteOfferInProgress = (1<<17), // Marked if the contact has a canAccessRemotely expression and a mission return, or a remote mission return, and the mission is in progress or failed
	ContactFlag_ReplayMissionGiver	= (1<<18), // Whether this contact gives mission offers for all completed missions which the player may replay.
	ContactFlag_TailorFree			= (1<<19), // If the contact makes Tailoring free (doesn't make the contact a Tailor)
	ContactFlag_Minigame			= (1<<20), // Whether or not this contact offers minigames
	ContactFlag_PuppetVendor		= (1<<21), // Whether or not this contact specifically sells puppet items
	ContactFlag_SharedBank			= (1<<22), // Whether this contact is a shared banker
	ContactFlag_ItemAssignmentGiver = (1<<23), // Whether or not this contact provides item assignments
	ContactFlag_ImageMenu			= (1<<24), // This contact has a menu with a bg image and WorldGameActions represented as icons at x,y points. Used for overworld map.
	ContactFlag_UGCSearchAgent		= (1<<25),	//This contact presents a filtered UGC search like a quest-offering dialog.
	ContactFlag_Windowed			= (1<<26),	//Tells the UI not to display this as a fullscreen contact. (Currently only supported for storecollections.)
	ContactFlag_ZStore				= (1<<27),  //Whether this contact will open the Zen Store for you.
} ContactFlags;

extern StaticDefineInt ContactFlagsEnum[];

#define contact_IndicatorStateIsAvailable(state) (state == ContactIndicator_MissionRepeatableAvailable || state == ContactIndicator_MissionAvailable)
