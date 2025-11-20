#ifndef GLOBALTYPES_H_
#define GLOBALTYPES_H_
GCC_SYSTEM

C_DECLARATIONS_BEGIN

#define MIN_GLOBAL_TYPE_NAME_LENGTH 3

#ifdef _XBOX
#define PLATFORM_NAME "Xbox 360"
#else
#define PLATFORM_NAME "Win32"
#endif

// This is for defining the interface for defining the mapping between a schema name and a global type enum
// The actual mapping is defined in ProductTypes, which is per-project

#define GLOBALTYPE_MAXSCHEMALEN 128 //the max length of a schema
#define CopyDictionaryPrefix "CopyDict_"

#include "structdefines.h"
#include "GlobalTypeEnum.h"
#include "language\AppLocale.h"

AUTO_ENUM;
typedef enum SchemaType
{
	SCHEMATYPE_NOSCHEMA, // No schema associated with this
	SCHEMATYPE_LOCAL, // Has a schema, but can't be saved to database
	SCHEMATYPE_TRANSACTED, // Is transactable, but is not saved to the database
	SCHEMATYPE_PERSISTED, // Has a global schema type, and is persisted to the database
} SchemaType;

AUTO_STRUCT;
typedef struct GlobalTypeMapping
{
	GlobalType type;
	char name[GLOBALTYPE_MAXSCHEMALEN];
	char shortname[GLOBALTYPE_MAXSCHEMALEN];
	SchemaType schemaType;
	GlobalType parent;
} GlobalTypeMapping;

AUTO_STRUCT;
typedef struct ContainerRef
{
	GlobalType containerType; AST(STRUCTPARAM)
	ContainerID containerID; AST(STRUCTPARAM)
} ContainerRef;

AUTO_STRUCT;
typedef struct ContainerLocation
{
	GlobalType containerType;
	ContainerID containerID;

	GlobalType ownerType;
	ContainerID ownerID;
} ContainerLocation;

AUTO_STRUCT;
typedef struct ContainerLocationList
{
	ContainerLocation **ppList;
} ContainerLocationList;

AUTO_STRUCT;
typedef struct ContainerRefArray
{
	ContainerRef **containerRefs;
} ContainerRefArray;

AUTO_STRUCT AST_CONTAINER;
typedef struct SavedContainerRef
{
	const GlobalType containerType; AST(STRUCTPARAM PERSIST SUBSCRIBE)
	const ContainerID containerID; AST(STRUCTPARAM PERSIST SUBSCRIBE)
} SavedContainerRef;

AUTO_STRUCT;
typedef struct IntEarrayWrapper 
{
	INT_EARRAY eaInts;
}IntEarrayWrapper;

AUTO_STRUCT;
typedef struct S64Struct
{
	S64 iInt;
} S64Struct;

// Textparser doesn't like 'S64* eaInts'
AUTO_STRUCT;
typedef struct S64EarrayWrapper
{
	EARRAY_OF(S64Struct) eaValues;
} S64EarrayWrapper;

AUTO_STRUCT;
typedef struct PooledStringWrapper
{
	const char *cpchString; AST(NAME(String) POOL_STRING)
} PooledStringWrapper;

AUTO_STRUCT;
typedef struct PooledStringArrayStruct
{
    STRING_EARRAY eaStrings;  AST(POOL_STRING)
} PooledStringArrayStruct;

// This is used for the ability scores screen in NNO.
// If another project needs to use it they can add
// their get default attrib values implementation.
AUTO_ENUM;
typedef enum CCGetBaseAttribValues
{
	CCGETBASEATTRIBVALUES_RETURN_0,			// Returns 0 for default attrib values in character creation
	CCGETBASEATTRIBVALUES_RETURN_DD_BASE,	// Uses D&D minimum ability score regardless of attrib type (which is 8)
	CCGETBASEATTRIBVALUES_RETURN_CLASS_VALUE	// Uses the value at level one on the chosen class table.
} CCGetBaseAttribValues;

// The method to calculate the points left to distribute to attribs
AUTO_ENUM;
typedef enum CCGetPointsLeft
{
	CCGETPOINTSLEFT_RETURN_0,					// Returns 0
	CCGETPOINTSLEFT_RETURN_USE_DD_POINT_SYSTEM	// Uses D&D point system
} CCGetPointsLeft;

// The method to validate attrib changes
AUTO_ENUM;
typedef enum CCValidateAttribChanges
{
	CCVALIDATEATTRIBCHANGES_NOT_ALLOWED,		// Modifications are not allowed
	CCVALIDATEATTRIBCHANGES_USE_DD_RULES		// Uses D&D rules
} CCValidateAttribChanges;


//different types of shards within a cluster
AUTO_ENUM;
typedef enum ClusterShardType
{
	SHARDTYPE_UNDEFINED, //presumably this is not a clustered shard

	SHARDTYPE_NORMAL,
	SHARDTYPE_UGC,

	SHARDTYPE_LAST, EIGNORE
} ClusterShardType;
extern StaticDefineInt ClusterShardTypeEnum[]; 

AUTO_STRUCT;
typedef struct GlobalConfig
{
	U32 bHideChatWindow : 1;			// Hide chat window
	U32 bUseAwayTeams : 1;				// Enable away team chooser
	U32 bHideCombatMessages : 1;		// Hide "out of range" and "not recharged" messages
	U32 bServerSaving : 1;				// Enable client/server saving
	U32 bUserContent : 1;				// Rather user content editing is allowed at all
	U32 bAllowOldPatrolData : 1;		// Enable support for old patrol data
	U32 bAllowOldEncounterData : 1;		// Enable support for old encounter data
	U32 bManageOffscreenGens : 1;		// Do more advanced things with offscreen gen icons on the HUD
	U32 bOverheadEntityGens : 1;		// Put entity gens above characters instead of around them
	U32 bDisableFollowCritters : 1;		// Allow players to follow critters?
	U32 bRequirePowerTrainer : 1;		// Require power trainer to purchase powers
	U32 bItemArt : 1;					// Enable ItemArt system
	U32 bExposeExploitTray : 1;			// Use the expose/exploit system for tray highlighting
	U32	bBlockItemMoveIfInUse : 1;		// Don't allow items to be moved while they are in use
	U32 bAllowPlayerCostumeFX : 1;		// temp NNO feature for steve stacy
	U32 bAllowInteractWhileContactDialogOpen : 1;
	U32 bPlayerRequiredToHaveAFaction : 1;		//Player Entity->hFaction must be valid
	U32 bPlayerRequiredToHaveASpecies : 1;		//Player Entity->pChar->hSpecies must be valid
	U32 bPlayerRequiredToHaveAllegiance : 1;	//Player Entity->hAllegiance must be valid
	U32 bNNOInteractionTooltips : 1;		//Enable new NNO interaction tooltips
	U32 bAlwaysQuickplay : 1;				//Quickplay is always enabled.
	U32 bUpdateCurrentMapMissions : 1;		// Enables updating of the lastClientNotifyTime for missions on the current map upon entering the map
	U32 bWeightMapTransferLists : 1;		// Use a weighted sum (friends/guildmates) to determine best map transfer
	U32 bManualSubRank : 1;					// If set, subrank should be set directly in a critter def and not automatically computed per critter
	U32 bIgnoreUnboundItemCostumes : 1;		// Ignores the warning about not allowing item costumes if the item can't be bound. (For NNO)
	U32 bDisplayFreeCostumeChangeAfterLoad : 1;			//Player Entity->hAllegiance must be valid
	U32 bLoginCharactersSortByLastPlayed : 1;			//Sort character selection list by "last played time"
	U32 bCostumeClothBackSideDefaultsToAvatarCloth: 1;	// Here so Champions backward compatibility works
	U32 bCostumeColorDoesNotAffectSkin : 1;				// Here for Champions so that the last color in the tailor does not incorrectly change skin tone
	U32 bUseNNOPowerDescs: 1;						//Use NNO-style power descs for tooltips.
	U32 bUseNNOAlgoNames: 1;						//Use NNO-style power descs for tooltips.
	U32 bDisableMapRevealData : 1;					// Disables map reveal data tracking
	U32 bAllowMultipleItemPowersWithCharges : 1;	// Allow items to have more than one limited-use itempower
	U32 bClientDangerData : 1;						// If set, send aggro data down to the client for display in UI
	U32 bOpenMissionMusic : 1;						// SoundLib watches active OpenMission to play music
	U32 bTimeControlAllowed : 1;					// Allow users to control timestepscale (safely). See TimeControl.h/c
	U32 bKillMeCommandRespawns : 1;					// The "killme" command respawns instead of causing the player to die
	U32 bEnableMissionReturnCmd : 1;				// Allow "ReturnFromMissionMap" to return the player from a mission map to the last static map they were on
	U32 bAllowBuyback : 1;							// Stores allow items to be repurchased at discount price
	U32 bAllowBuybackUntilMapMove : 1;				// Stores do not clear buyback items until the player changes maps.
	U32 bAllowSellback : 1;							// Stores allow items to be resold at full price
	U32 bAutoRewardLogging : 1;						// automatically enable RewardLog on gameserver startup
	U32 bAutoCombatLogging : 1;						// automatically enable CombatLog on gameserver startup
	U32 bVerboseCombatLogging : 1;					// Add additional information to combat log for better analysis
	U32 bDisableMissionMapHeatmaps : 1;				// Will skip making heat map data for mission maps
	U32 bDefaultToOpenTeaming : 1;					// If set will default new characters to open teaming, as opposed to prompt on creation
	U32 bListModeContactsDoNotContinue : 1;			// If list mode contacts should hide the continue option 
	U32 bDisableFullRespec : 1;						// If this is set, full respecs will do nothing.
	U32 bAllowRespecAwayFromContact : 1;			// if set, respecs don't require you to be interacting with a respec contact
	U32 bAllowNoResultRecipes : 1;					// If this is set, item recipes with no result won't throw a validation error.
	U32 bNoUserMapInstanceChoice: 1;				// Whether users should have the option of selecting an instance when switching maps
	U32 bSwapMissionOfferDialogText : 1;			// Swaps the mission offer text (detail and summary) in contact dialogs
	U32 bDisableRecruitUpdates : 1;					// Disables recruit updates				
	U32 bDisableGuildEPWithdrawLimit : 1;			// Disables the Guild Withdraw limit using EP.
	U32 bEnableGuildItemWithdrawLimit : 1;			// Enables the Guild Withdraw limit using item count.
	U32 bGuildWithdrawalUsesShardInterval : 1;		// Causes the guild withdrawal limits to be applied based in shard interval timing rather then based on the first withdrwal. Really we should convert STO to use this as well.
	U32 bShowContactOffersOnCooldown : 1;			// Enables display of Contact Mission Offers for repeatable missions which cannot be offered since they are on a cooldown.
	U32 bLogEncounterSummary : 1;					// If true, log an encounter summary for every encounter
	U32 bKeepLootsOnCorpses : 1;					// When set to true server does not create a loot entity instead places the loot on the corpse
	U32 bDisableGuildFreeCostumeViaNumeric : 1;		// Disables granting a PrimaryCostume numeric on joining a guild
	U32 bAutoEquipDevices : 1;						// Automatically equip devices if they are added to the inventory
	U32 bEnableAutoLoot : 1;						// Interacting with a loot bag will instantly loot all the items in the loot bag to your inventory
	U32 bShowAutoLootOption : 1;					// If set, show the AutoLoot option in Basic Options
	U32 bAutoUnholsterWeapons : 1;					// Unholster weapons automatically on pvp/pve ground maps
	U32 bTailorLinesDefaultToNoColorLinkAll : 1;	// When using the line list expressions for tailor force default color link to not 'all' before randomizing
	U32 bShowCameraFilteringOption : 1;				// Show a camera mouse filtering slider in the options menu								
	U32 bShowCriticalStatusInfoInHUDOptions : 1;	// Add an option in HUD options to determine whether each reticle will show critical status information or not
	U32 bShowPlayerRoleInfoInHUDOptions : 1;		// Add an option in HUD options to determine whether other players' role icons are displayed with the player title
	U32 bDoNotShowMissionGrantDialogsForContact : 1;// If this is set, mission grant dialogs defined for the contact is not displayed. If you turn this on you probably want to grant missions via special dialogs.
	U32 bDoNotShowInProgressMissionsForContact : 1;	// If this is set, the dialog options for in progress missions are not displayed for the contact
	U32 bDoNotSkipContactOptionsForMissionTurnIn : 1;	// If this is set, the mission turn in dialog is not automatically selected for the contact
	U32 bDoNotEndDialogForMissionOfferActionIfContactOffers : 1;	// If this is set, the dialog with the current contact will not be ended if the contact offers the same mission as the mission offer game action
	U32 bDoNotShowHUDOptions : 1;					// If this is set, the hud options menu will not be displayed
	U32 bDoNotShowChatOptions : 1;					// If this is set, the chat options menu will not be displayed
	U32 bDoNotInitJoystick : 1;
	U32 bUseNNOItemCategoryNamingRules : 1;			// If this is set, the item category string will be build based on NNO rules. The default behavior is a comma separated list
	U32 bDisableDeathPenaltyOnPvPMaps : 1;			// If this is set, do not grant the pvp reward bag to a player that respawns
	U32 bPlayerCanTrainPowersAnywhere : 1;			// If true, the player can train their powers anywhere without an open contact dialogue.
	U32 bAutoBuyPowersGoInTray : 1;					// If true, autobuy powers will be automatically added to the tray.
	U32 bEncountersScaleWithActivePets : 1;			// If true, active henchmen are counted as part of your teamsize when spawning encounters.
	U32 bDisableEncounterTeamSizeRangeCheckOnMissionMaps : 1; //If true, teammates will never be discounted from TeamSize calculations due to their distance from the spawning player when on a mission map
	U32 bRecruitRequiresUpgraded : 1;				// If this is set, a referred account is considers a recruit if it is in the Upgraded state, rather than the Accepted state
	U32 bFCSpecialPublicShardVariablesFeature : 1;	// POSSIBLY SHARD-CRUSHING: All maps subscribe to ShardVariables and send ShardVariables to the client.
													//  In ADDITION all shardvariable vars are put into map state pPublicVarData->eaShardPublicVars.
													//  Deprecated. Each variable specifies what type it is now. Broadcast replicates this feature per-variable
													//  (it's still possibly SHARD-CRUSHING)
	U32 bClickiesGlowOnMouseover : 1;				//If set, clickies will glow on mouseover instead of all the time.
	U32 bDestructibleInteractOption : 1;			// If true, players may interact with otherwise non-interactable destructible objects
	U32 bTargetDual;								// If true, Players can have two targets at one, the main target as Foe, the dual target as non-Foe
	U32 bTailorPaymentChoice : 1;					// If true, the user has a choice to use their costume tokens or pay using the resource cost.  It also forces changes tokens to be used at a tailor.  IE You cannot change your costume without being near a tailor contact (even with free change tokens)
	U32 bAutoAttackFirstSlot : 1;					// If true, force the auto attack code to use the first slotted power. Otherwise only enable if explicit
	U32 bDelayNotificationsDuringContactDialog : 1;	// If this is set to true, all notifications received during a contact dialog are triggered after the dialog ends.
	U32 bAllowGuildstoHaveNoEmblems : 1;			// If this is set to true, guilds can be set to have no emblems
	U32 bAllowGuildAllegianceChanges : 1;			// If this is set to true, guilds can have their allegiance changed after creation
	U32 bEnforceGuildGuildMemberAllegianceMatch : 1;// If this is set to true, allegiance is checked when joining the guild to see that it matches the guild itself (dubious compatibility with previous option)
	U32 bAllowSpecifiedMapVarSeed : 1;				// If this is set to true, the seed used for calculating map variables may be passed into a map using a specific map variable (see gslMapVariable.h)
	U32 bForceEnableAmbientCube : 1;				// If this is set to true, forcibly add the UseAmbientCube flag to all material templates
	U32 bEnableBulletins : 1;						// If true, run code pertaining to Bulletins
	U32 bGenMapHideUndiscoveredRooms : 1;			// Whether or not to display unvisited rooms on an instanced map.  There may be a better way to do this.
	U32 bShowLifetimeOnPassiveBuffs : 1;			// If true, show lifetimes on buffs from passive powers
	U32 bBetterFSMValidation : 1;					// Specifies whether action/onentry/transition expressions use different func tables
	U32 bAllowContainerItemsInAuction : 1;			// If true, allows saved pets to be transferred through auction.
	U32 IgnoreAwayTeamRulesForTeamPets : 1;			// Ignore the Away Team size restrictions when adding pets to teams.
	U32 bAddRelatedOpenMissionsToPersonalMissionHelper;	// If this is set, any open mission objectives related to the personal mission will be shown in mission helper UI
	U32 bTailorDefaultLowQuality : 1;				// If true, then by default the costume view uses the lowest set graphics options instead of highest supported graphics options
	U32 bDeactivateHenchmenWhenDroppedFromTeam : 1; // If true, when a henchman is auto-dropped from a team (due to merging) they will actually be set to "offline"
	U32 bCostumeCategoryReplace : 1;				// If true the costume system will attempt to use the Category Replace feature
	U32 bEnableClientComboTracker : 1;				// If true, the client will store information about the players "combo" powers, and past powers that were activated
	U32 bPreventTerrainMeshImprovements : 1;		// If true, terrain will not use the higher poly count or the fix to prevent extreme normal changes.
	U32 bSinglePerPlayerOptionWhenDoorKeyIsPassed : 1; // If true, only one interaction option will be shown on per player doors which pass a door key map variable instead of one option per player being shown.
	U32 bEnableUGCUIGens : 1;						// If true, allows UGC uigens to be shown
	U32 bShowMapIconsOnFakeZones : 1;
	U32 bNewAnimationSystem : 1;					// If true, use the new, graph-driven animation system instead of the old, DynSeqData animation system.
	U32 bUseMovementGraphs : 1;						// If true, use movement graphs instead of the movement "spreadsheet" and transition setup that we currently use
	U32 bUseNWOMovementAndAnimationOptions : 1;		// If true, modify various animation and movement routines to function as desired in NW when running the new animation system.
	U32 bUseSTOMovementAndAnimationOptions : 1;		// If true, modify various animation and movement routines to function as desired in STO when running the new animation system.
	U32 bDamageFloatsDrawOverEntityGen : 1;		// If true, entity overhead things like damage floaters will draw on top of the UI gens, not under them.
	U32 bDamageTrackerServerOnlySendFlaggedSpecial : 1;		// If true, the server will only send especially marked damage trackers, which are those attribmods that are flagged bUIShowSpecial, which get the flag kCombatTrackerFlag_ShowSpecial
	U32 bTabTargetingLoopsWithNoBreaks : 1;	//If true, tab-targeting won't have an empty target in between cycles.
	U32 bTabTargetingAlwaysIncludesActiveCombatants : 1; //If true, tab-targeting ALWAYS includes active combatants, regardless of LoS.
	U32 bEnablePersistedStores : 1;					// If true, load/process persisted stores
	U32 bSelfOnlyPowerModeTimers : 1;				// If true, show power mode timers on the UI for attribs that target Self
	U32 bUseCombinedContactIndicators : 1;			// If true, use contact indicator type "Multiple" for contacts that have multiple non-mission contact indicators
	U32 bUseGlobalExpressionForInteractableGlowDistance : 1;// If true, the expression found in GlobaleExpressions will be used to determine the distance at which objects glow, rather than their targetting distance.
	U32 bRememberVisitedDialogs : 1;				// If true, the server stores the visited dialog for the most recent contact players talk to.
	U32 bEntBuffListOnlyShowBestModInStackGroup : 1;// If true, only show the best attrib mod for each stack group in the entity buff list UI
	U32 bRewardTablesUseEncounterLevel : 1;			// If true, encounter level will be used to determine reward level instead of individual entity level.
	U32 bCharacterPathMustBeFollowed : 1;			// If true, power purchases must follow the character path (if specified)
	U32 bPetsDontCollideWithPlayer : 1;				// If true, pets do not collide with players
	U32 bEnemiesDontCollideWithPlayer : 1;			// If true, enemies do not collide with players
	U32 bNPCsDontCollideWithNPCs : 1;				// If true, NPCs will not collide with other NPCs
	U32 bNoEntityCollision : 1;						// If true, entities do not collide with each other
	U32 bUseDDBaseStatPointsFunction : 1;			// If true, the base stat point values in character creation is calculated based on D&D rules
	U32 bAllowVisitedTrackingForNamespaceMaps : 1;	// If true, do visited tracking for namespaced maps
	U32 bDontUseStickyFlightGroundCollision : 1;	// if true, turns off an unintentional side-effect in some old collision code from CoH, when using flying and trying to fly at a very low angle across the ground- this would keep snapping you to the ground. 
	U32 bLootBagsRemainOnInteractablesUntilEmpty : 1;	//If true, interactables won't destroy all their items when you take a single thing out.
	U32 bVoiceChat : 1;								// Enables voice chattery
	U32 bVoiceAds : 1;								// Enables voice ads
	U32 bLoadPlayerStatResoucesOnClient : 1;
	U32 bEnableAssists : 1;							// Kill credit only goes to the entity with the killing blow. All other entities get assist credit. Only turned on in ZMTYPE_PVP
	U32 bDisableSuperJump : 1;						// Disables the AI doing crazy impossible jumps to try to get unstuck
	U32 bDontAutoEquipUpgrades : 1;		// If set, upgrades and weapons will behave as through the inventory is their "best" bag.
	U32 bEnableSpectator : 1;		
	U32	bEnableDeadPlayerBodies : 1;	// if set, will allow dead player bodies to be created after a player dies. 
	U32 bTargetSelectUnderMouseUsesPowersLOSCheck : 1; // If set, in target_SelectUnderMouseEx the LOS check will use a method closer to the powers LOS instead of checking LOS based on where the cursor is targeting 
	U32 bAutoChooseMapOptionInGameServer : 1; // If set, the game server chooses the best possible map option automatically when there is more than one choice
	U32 bSkipPressAnyKey : 1;						// If set, skips the "Press Any Key" on the client when loading a map

	// Team Stuff
		U32 bEnableNNOTeamWarp : 1;						//Enables assorted NNO-specific team maptransfer behavior
		U32 bKeepOnTeamOnLogout : 1;					// When logging out when on a team, do not leave the team. Just set us to the logged out state.
		U32 bManageTeamDisconnecteds : 1;				// Keep track of and manage disconnected TeamMembers. Conceptually keep them on the team for purposes of team size.
		U32 bAlwaysTryToRejoinTeamAfterLogout : 1;		// Ignore the TEAM_REJOIN_TIMEOUT and always try to rejoin an old team (if it still exists)
		U32 bAllowSuballegianceTeaming : 1;				// Allows players to team together if one player's suballegiance matches the other player's allegiance.

	U32 bQueueSmartGroupNNO : 1;					// Use NNO smart grouping. Sad that game-specific is done like this. But whatever. The alternatives are much worse
													// This is only the type of grouping. QueueConfig can enable/disable smart grouping overall
	
	U32 bUIGenSaneLayoutOrder : 1;					// Reverses the longstanding UI ordering so that the UI Ticking order happens in the order children are declared. 
	U32 bUIGenDisallowExprObjPathing : 1; // Disables the use of object paths in UIGen expression contexts
	U32 bDisablePatchStreamingBuiltinProgress : 1;	// If set, don't draw the builtin patch streaming progress, presumably because something else is displaying it in a per-game way
	U32 bShowOpenMissionInCompass : 1;				// if TRUE will show open missions on compass in a mission map
	U32 bStoreKeybindsPerProfile; // If set, player keybinds are stored per profile instead of putting them all in a single list
	U32 bLootModesDontCountAsInteraction : 1;	//If set, participating in a master loot or Need Before Greed bag won't flag you as "interacting"
	U32 bEnableLootModesForInteractables : 1;	//If set, loot modes will be applied to interactable loot as well.
	U32 bCheckOverlayCostumeSpecies : 1;	//If set, overlay costumes will be restricted according to their species.
	U32 bDontShowLuckyCharmsInOptions : 1;	// So far, only NNO is actually using the lucky charms targeting, other games don't need to see it. 
	U32 bInvertMousePerControlSchemeOption : 1;	//If set, display an option in BasicOptions to invert mouse X/Y options per control scheme
	U32 bNeverUseItemLevelOnItemInstance : 1; //If set, never use the item level on the item instance, and instead look at the ItemDef
	U32 bAutoConvertCharacterToPremium : 1; //If logging in to a premium account, automatically convert a standard character to premium
    U32 bAutoConvertCharacterToStandard : 1; //If logging in to a standard account, automatically convert a premium character to standard
	U32 bXboxControllersAreJoysticks : 1; // If this is true, then Xbox controllers will work as normal gamepads.
	U32 bSilentInteractLocationValidation : 1; // If this is true, interact location validation function will not report errors.
	U32 bLightInteractLocationValidation : 1; // If this is true, interact location validation is made less restrictive.
	U32 bSetMapScaleDefaultOnLoad : 1; //If this is true, map scaling will not persist, and maps will instead reset to default on area change
    U32 bDisableOldCharacterSlotCheck : 1; //Temporary flag to disable old character slot check.  Old character slot code should be removed soon.
	U32 bDiscardDuplicateCritterPets : 1; //If a character attempts to add a duplicate critterpet def, just discard the source item silently without failing the transaction.
	U32 bDontOverrideLODOnScaledUnlessUsingLODTemplate : 1; // Only auto-get a new lod for scaled object when the object is already using an LOD template
	U32 bRolloverLootIsForPlayersOnly : 1; //Only players can pick up rollover loot.
	U32 bExposeDeprecatedPowerConfigVars : 1; // Continue to expose deprecated power config vars that have (Dep) in the name, and use them
	U32 bEnableAutoHailDisableOption : 1;			// If set, show the Disable Auto Hail option in Basic Options
	U32 bAutomaticallyCleanUpDeadPets : 1;			// If set, critter pets will get cleaned up on death just like every other kind of critter.

	U32 bUsePowerStatBonuses : 1;			// If set the client receives a list of power stat bonuses which maybe be used for UI purposes.

	U32 bSendInnateAttribModData : 1;		// If set the client receives data used to display innate attrib mods

	U32 bUseLinearMapSnapResolutionScaling : 1; //If true, the map snaps will be rendered at a resolution that scales between the min and max, rather than only having two possible resolutions

	U32 bEnableGoldenPath : 1; //If true, the golden path (waypoint) feature will be active
	U32 bDelayDoorInteraction : 1; // If true, Door Interactions are delayed by 1 frame before completing

	U32 bAutoDescRoundToTenths : 1; // If true, AutoDesc will round values over 10 to the nearest 1/10th

	U32 bSortPowerTreeStepsByTableLevel : 1;

	U32 bHTMLViewerEnabled : 1;	// Here to prevent requiring license for non-using games

	U32 bEnableFMV : 1;

	U32 bEnableShaderQualitySlider : 2;

	U32 bEnableClusters : 1;

	// Adam introduced a change that was designed to prevent players from exploiting AI's by standing inside of one-way collision.
	// That change created a paradigm where interacts could be used through collision.  This bool maintains that behavior.
	U32 bLegacyInteractBehaviorInUGC : 1;

	U32 bCombatDeathPrediction : 1;  // If true, enables death prediction to happen on the server, sending predicted death info to players when necessary

	// if this is true, encounters that have no spawn properties are considered "static" rather than "default"
	U32 bLegacyEncounterDynamicSpawnType : 1;

	// If this is true, items can only apply integral stats to characters.  The idea is that the users can add up the stats given
	// by their items and see it reflected in their total.
	U32 bRoundItemStatsOnApplyToChar : 1;

	// This is for the camera, I'm putting it here for now because I don't want it to be configurable by the user or by the designers,
	// and I want to just turn it on for Neverwinter, until I'm certain whether this is a virtuous change
	U32 bSmartInputSmoothing : 1;

	// Does this project override the standard 20 minutes for a ugc reward qualify?
	U32 bUgcRewardOverrideEnable : 1;

	// If this is true, the Game Account Data container may not be modified by the shard (except for very limited circumstances). This
	// is intended to support multi-shard clusters (e.g. NW), where we want all the GAD containers on all the shards to be identical.
	U32 bDontAllowGADModification : 1;

	// ContactIndicator_LowImportanceInfo takes the place of HasInfoDialog.
	// The rationale here is that it is unlikely that a game will have 2 different levels of info dialog that it wishes to
	// slice this finely, but we don't want to change the behavior of STO.  This won't work with the multiple indicators feature. [RMARR - 12/7/12]
	U32 bLowImportanceInfoDialogs : 1;

	// This flag is used by the login server to decide whether to request puppets when getting character details.
	U32 bCharacterDetailIncludesPuppets : 1;

	// This flag is used by the login server to decide whether to request TeamRequest pets when getting character details.
	U32 bCharacterDetailIncludesTeamRequestPets: 1;

	U32 bVirtualShardsOnlyUseRestrictedCharacterSlots : 1; // If true, virtual shards will only use character slots from their slot restricted pool.

	U32 bHideZoneInstanceNumberInChat : 1;

	U32 bClientImperceptibleFadesImmediately : 1;	// If true, entities on clients that are imperceptible will immediately disappear instead of fading away

	U32 bShowOnlyOneCopyOfEachResolution : 1; // If true, we'll reduce the clutter in the Graphics Options to only show one copy of each resolution. If false, you'll see each resolution @ each refresh rate.

    // Use simplified character slot rules so that we can do something sensible in multi-shard environments when one shard is down.
    // If bUseSimplifiedMultiShardCharacterSlotRules is true, then bVirtualShardsOnlyUseRestrictedCharacterSlots must also be true.
    U32 bUseSimplifiedMultiShardCharacterSlotRules: 1;

	// If this is true, then the designers don't get a choice
	U32 bAlwaysAllowInteractsInCombat: 1;

	// Cutscene: When creating a cutscene, sets the default of bPlayersAreUntargetable
	U32 bCutsceneDefault_bPlayersAreUntargetable : 1;

	// Instead of the default maximum items in the buyback bag use the override value
	U32 bUseBuyBackCountOverride : 1;

	// Makes the client perform a map leave confirmation if the user attempts to abandon a mission map in progress via an interact
	U32 bInteractMapLeaveConfirm : 1;

	//When true, changes behavior of trade requests to add an extra step for the person on the receiving end.
	U32 bEnableTwoStepTradeRequest : 1;

	//When true, changes behavior of trades to include an escrow state
	U32 bEnableEscrowTrades : 1;

	//Enables the old graphics quality slider with 4 options as opposed to the new one with a continuous setting
	U32 bUseLegacyGraphicsQualitySlider : 1;

	// Don't use this ill-conceived feature, which forces you to run 2 expressions per EntityGen instead of one
	U32 bIgnoreEntityGenOffscreenExpression : 1;

	//Enables the render scale slider and disables the half resolution radio button in the graphics options
	U32 bUseRenderScaleSlider : 1;

	// Neverwinter needs to set keybinds by language.  Adding a matrix of control schemes and languages is not viable today,
	// due to the exceptionally weird code in gclControlScheme.c.  I am side-stepping the whole thing.  [RMARR - 4/15/13]
	U32 bKeybindsFromControlSchemes : 1;			AST(DEFAULT(1))

	// Only allow players to Need roll on items that their class, character path and allegience can use.
	U32 bNeedRollRequiresUsageMatch : 1;

	//Prevents the forced respawn when a PvP match starts
	U32 bDisableRespawnOnPvPMatchStart : 1;

	U32 bNoAudioOnPetContactsWithoutPet : 1;

	// When true, the only keys you can press while rearranging the HUD Jails are Escape, Shift and Tab (or whatever that s_JailProfile key profile defines).
	U32 bHUDRearrangeModeEatsKeys : 1;

	U32 bUseLegacyContactNameOverrideBehavior : 1;

	U32 bCostumeEditorDoesntUseItems : 1;

	U32 bUseChatTradeChannel : 1; // When true, create the Zone-like Trade channel and auto-join everyone on the server.
	U32 bUseChatLFGChannel : 1; // When true, create the Zone-like LFG channel and auto-join everyone on the server.
	U32 bPreventItemLinksInZoneLikeChatChannels : 1; // When true, you can't link items in Zone and LFG channels, but you can in Trade.

	U32 bNoCostDonationTasksAllowed : 1; //Allows donation tasks to have no cost

	// always subscribe to shared bank on login success
	U32 bSubscribeToSharedBank : 1;

	//Disables remote contact selling
	U32 bDisableRemoteContactSelling : 1;

	//Allows VO metadata generation to assume one contact per mission.
	//Adds validation that this is the case, and changes .vo.txt
	//generation to take advantage of this fact.
	U32 bOnlyOneContactPerMission : 1;

	//Causes rewardvaltables to ignore critter subrank. This was moved from RewardConfig to resolve a circular autostartup dependency.
	U32 bRewardsIgnoreCritterSubRank : 1;

	// This is the number of items allowed in the buyback list. See store_GetBuyBackMaximumItemCount()
	U32 uBuyBackCountOverride;

	int iMinimumFullscreenResolutionWidth;
	int iMinimumFullscreenResolutionHeight;

	U32 iFontSmallSize;
	U32 iFontMediumSize;
	U32 iFontLargeSize;
	U32 iDXTQuality; // 0 for no compression, 1-4 for DXT setting 

	U32 iDefaultEditorAggroRadius;	// When showing aggro radius in the editor

	U32 iMaxOffscreenIconsPlayers;
	U32 iMaxOffscreenIconsCritters;
	F32 fOffscreenIconCombineDistance;	//How close icons need to be in order to combine them [0,1]Screen-space distance

	U32 iMaxActiveMissions;  // Maximum number of missions a player can have; 0 means no limit
	F32 fSecondaryMissionCooldownHours;  // Time limit between repeating a mission for secondary credit

	F32 fCostumeMirrorChanceRequired;
	F32 fCostumeMirrorChanceOptional;

	F32 fMapSnapOutdoorRes;	//In pixel/feet.  Larger numbers exponentially increase map snap binning time.
	F32 fMapSnapIndoorRes;
	int iMapSnapImageCellSize;	//Pixel width and height of smaller high-res images.  Greater than 512 will error during binning.

	// These could maybe be per-map options?
	F32 fMapSnapOutdoorOrthoSkewX;
	F32 fMapSnapOutdoorOrthoSkewZ;
	F32 fMapSnapIndoorOrthoSkewX;
	F32 fMapSnapIndoorOrthoSkewZ;

	F32 fFollowTargetEntityDistance;	AST(DEFAULT(0.5)) // This determines the distance at which players auto-follow their target

	const char *pcLevelingNumericItem; AST(POOL_STRING) // The numeric item name used for determining level
	const char *pcUISkinTrayPowerIconHack; //An ugly hack for changing tray icons to klingon-ified versions when running under the klingon UI skin
	const char* pcNeedBeforeGreedThreshold; //Determines the default threshold for need before greed looting.
	const char* pcDefaultLootMode; //Determines the default loot mode for teams

	F32 rollover_distance;  AST(DEFAULT(3.0))  // how close you need to be to pick up rollover loot
	F32 rollover_display_time;      AST(DEFAULT(0.5))  // how many seconds before rollover loot shows up on the client
	F32 rollover_pickup_time;      AST(DEFAULT(0.5))  // how many seconds before you can pick up rollover loot
	F32 rollover_postpickup_linger_time;      AST(DEFAULT(0))  // how many seconds rollover drops hang around after being picked up
	F32 lootent_postloot_linger_time;      AST(DEFAULT(0))  // how many seconds rollover drops hang around after being picked up

	CCGetBaseAttribValues		eCCGetBaseAttribValues; // How the default attrib values are retrieved in character creation
	CCGetPointsLeft				eCCGetPointsLeft; // Indicates the method to return the points left to distribute to attrib values in character creation
	CCValidateAttribChanges		eCCValidateAttribChanges; // Indicates the method to validate attrib changes
	
	const char* pchRenamePlayerPuppetClassType; //When renaming a player, rename the active puppet of this type as well
	
	F32 fCharSelectionPortraitTextureFov; AST(DEFAULT(-1.0f)) // FOV used while creating the portrait texture for the character selection screen
	S32 iCharSelectionHeadshotWidth; AST(DEFAULT(128)) // The width of the headshot image used in the character selection screen
	S32 iCharSelectionHeadshotHeight; AST(DEFAULT(128)) // The height of the headshot image used in the character selection screen

	F32 fSpawnPointLoadingScreenDistSq; AST(DEFAULT(40000)) // square of distance a player has to travel before it forces a loading screen

	// The client side animation played for the entity during the contact dialog
	// This can be overriden on the contact level.
	// Leave blank for no client side animation override
	const char *pchClientSideContactDialogAnimList;

	// The theme assigned to a guild when it's created unless a theme is specified
	const char *pchDefaultGuildThemeName;

	// Bump this to force world cell rebinning on only this game
	U32 uWorldCellGameCRC;

	// The path to the launch video (Optional)
	const char *pchLaunchVideoPath;

    // The path to the character creation video (Optional)
    const char *pchCharCreateVideoPath;

	// The name of the category used to distinguish doors from other interactables in interaction_findMouseoverInteractableEx function
	const char *pchCategoryNameForDoors;

	F32 combatUpdateTimer;	AST(DEFAULT(.1))
	F32 maxServerFPS;		AST(DEFAULT(30.f))

	// Any numeric reward in the default reward table scales linearly up to this time (in minutes)
	int iUGCMissionRewardScaleOverTime;

	// At UGC_MISSION_REWARD_SCALE_OVER_TIME minutes, the reward scale will be (this value / 1.5).  
	// After UGC_MISSION_REWARD_SCALE_OVER_TIME*2 the reward scale will be UGC_MISSION_REWARD_MAX_SCALE and will not increase further.
	F32 fUGCMissionRewardMaxScale;

	// Default reward tables for UGC Mission rewards
	const char* pchUGCMissionDefaultRewardTable;
	const char* pchUGCMissionFeaturedRewardTable;
	const char* pchUGCMissionDefaultNonCombatRewardTable; 

	// Whether or not we want UGC achievements data tracked. If false, a Fixup is run in MapManager Periodic Update to remove any per-project achievement data created.
	// STO wants this off. NNO will have this on.
	bool bUGCAchievementsEnable;

	// If this is turned out, UGC missions do not ever generate rewards in preview mode
	bool bNoUGCRewardsInPreview;

	//Default microtransaction currency if nothing is configured in the MTConfig.def
	const char *pchMicrotransactionDefaultCurrency;			AST(ADDNAMES(MicrotransactionCurrency, MTCurrency, DefaultCurrency))

	//if a UGC project hasn't been created, played, or published within the most recent n days, and a republish
	//pass is happening, just withdraw it instead of republishing it
	int iUGCProjectNoPlayDaysBeforeNoRepublish; AST(DEFAULT(30))

	bool bUGCPreviouslyFeaturedMissionsQualifyForRewards;

	// for overhead entity gens if bOverheadEntityGens is true
	const char *pchOverheadGenBone;

	// Since there is no BasicOptionsDefaults.def file, I am putting this in the gConf.  If you add any more defaults for the Basic options tab,
	// You might want to make one.
	F32 fDefaultTooltipDelay;					 AST(DEFAULT(1.0f))

	// If this is set the login server will send the player to the tutorial map until this mission succeeds.
	const char *pchTutorialMissionName;

	// The name of the cutscene to be used as the default contact cutscene
	const char* pchDefaultCutsceneForContacts;

	//The minimum and maximum values for character names used in character creation.
	int iMinNameLength;			AST(DEFAULT(ASCII_NAME_MIN_LENGTH))
	int iMaxNameLength;			AST(DEFAULT(ASCII_NAME_MAX_LENGTH))

	int iMaxTeamUpGroupSize;		AST(DEFAULT(5))

	// 1-based minimum level for teaming.  Defaults to 0, so no restrictions.
	int iMinimumTeamLevel;

	int iMaxHideShowCollisionTris;				AST(DEFAULT(2000))

	F32 fUgcRewardOverrideTimeMinutes;

    // Which extra header in the objectdb contains allegiance used for character selection and character slot unlocking.
    //  Currently only used by STO.
    int iCharacterChoiceExtraHeaderForAllegiance;

	// Which extra header in the objectdb contains allegiance used for character selection and character slot unlocking.
	//  Currently only used by STO.
	int iCharacterChoiceExtraHeaderForSubAllegiance;

    // Which extra header in the objectdb contains the class name used for character selection.
    int iCharacterChoiceExtraHeaderForClass;

    // Which extra header in the objectdb contains the character path name used for character selection.
    int iCharacterChoiceExtraHeaderForPath;

    // Which extra header in the objectdb contains the character species name used for character selection.
    int iCharacterChoiceExtraHeaderForSpecies;

    // How often to force an update of the project specific log string on entities.  If set to zero, then don't do any time based updating.
    // If you set this to non-zero be sure that the game's OVERRIDE_LATELINK_entity_CreateProjSpecificLogString() function is setting
    //  pEnt->lastProjSpecificLogTime each time it sets the string.
    U32 uProjSpecificLogStringUpdateInterval;

	// Seconds between when entering a map and being able to ChangeInstances to a new instance of it.
	U32 uSecondsBetweenChangeInstance;

	// If true, average playing time for UGC missions is computed off of time spent on the Open Missions of custom UGC maps. Both NW and STO are using this.
	// If false, total time from when mission is picked up until when it is turned in is used.
	bool bUGCAveragePlayingTimeUsesCustomMapPlayingTime;
	bool bUGCMigrateToAveragePlayingTimeUsingCustomMaps; // enable this also to gracefully migrate from the previous being false to true without a republish. STO is using this.

	// If greater than zero, specifies the number of hours between each allowed recording of an Entity's completion of a UGC mission.
	U32 uUGCHoursBetweenRecordingCompletionForEntity;

	bool bUGCSearchTreatsDefaultLanguageAsAll;

	//This is used for beacon pruning in the beaconizer. Range: 2-20. Each unit is 5 feet, so the range corresponds to 10 feet - 100 feet.
	//At 100 feet, beacon pruning will be disabled. This is the default (20).
	U32 uBeaconizerJumpHeight;		AST(DEFAULT(20))
} GlobalConfig;

extern GlobalConfig gConf;

extern StaticDefineInt GlobalTypeEnum[GLOBALTYPE_MAXTYPES + 2];

extern GlobalTypeMapping globalTypeMapping[GLOBALTYPE_MAXTYPES + 1];

// Adds an entire other list of mappings
void AddGlobalTypeMappingList(GlobalTypeMapping *list);

// Adds a single entry to the list
//void AddGlobalTypeMapping(GlobalType type, const char *name, SchemaType schemaType, GlobalType parent);

// Converts a global type to a name
//char *GlobalTypeToName(GlobalType type);

#define GlobalTypeToName(type) ((type) >= 0 && (type) < GLOBALTYPE_MAXTYPES) ? globalTypeMapping[(type)].name : globalTypeMapping[GLOBALTYPE_NONE].name

#define GlobalTypeToName_OrNULL(type) ((type) > 0 && (type) < GLOBALTYPE_MAXTYPES) ? globalTypeMapping[(type)].name : NULL
	

// Converts a global type to a shortname
//char *GlobalTypeToShortName(GlobalType type);

#define GlobalTypeToShortName(type) ((type) >= 0 && (type) < GLOBALTYPE_MAXTYPES) ? (globalTypeMapping[(type)].shortname[0] ? globalTypeMapping[(type)].shortname : globalTypeMapping[(type)].name) : globalTypeMapping[GLOBALTYPE_NONE].name
	
// Return rather a certain global type has an associated schema
SchemaType GlobalTypeSchemaType(GlobalType type);

// Returns the parent global type of a type, or invalid if it has no parent
GlobalType GlobalTypeParent(GlobalType type);

// Converts type shortname to type id
GlobalType ShortNameToGlobalType(const char *shortname);

// Converts type name to type id
GlobalType NameToGlobalType(const char *name);

// Converts between container copy dictionary name and global type
const char *GlobalTypeToCopyDictionaryName(GlobalType type);
GlobalType CopyDictionaryNameToGlobalType(const char *name);

// Converts between strings and container IDs
const char *ContainerIDToString_s(ContainerID id, size_t len, char *buf);
#define ContainerIDToString(id, buf) ContainerIDToString_s((id), ARRAY_SIZE_CHECKED(buf), (buf))
ContainerID StringToContainerID(const char *string);

// Parses out string of form "TypeName[ID]" into a type and an ID. Sets them both to zero on failure.
void ParseGlobalTypeAndID(const char *string, GlobalType *pOutType, ContainerID *pOutID);

//similar to above, sticks result in a containerRef. 
ContainerRef ParseGlobalTypeAndIDIntoContainerRef(const char *string);

//put macros for getting/putting container types and IDs here. They should always be used, so that later on we can optimize all ID/type sending/receiving
//at once
#define GetContainerTypeFromPacket(pak) ((GlobalType)pktGetBitsPack(pak, 1))
#define GetContainerIDFromPacket(pak) (pktGetBitsPack(pak, 1))
#define PutContainerTypeIntoPacket(pak, eType) pktSendBitsPack(pak, 1, ((int)eType))
#define PutContainerIDIntoPacket(pak, ID) pktSendBitsPack(pak, 1, ID)

//a single globaltype-of-cur-app variable, not needed for random utility apps, but needed
//for all servers, gameclient
extern GlobalType gAppGlobalType;

__forceinline static void SetAppGlobalType(GlobalType eType) { gAppGlobalType = eType; }
#define GetAppGlobalType() (gAppGlobalType)

LATELINK;
U32 GetAppGlobalID(void);

//special version of GetAppGlobalID which returns 0 for the controller, so that outcomes reported to the cluster controller
//will be identical from all controller in a cluster
U32 GetAppGlobalID_ForCmdParse(void);

LATELINK;
void* GetAppIDStr(void);

//special functions called things that need to parse global-type-setting out of the command line. Do not use these
//unless you really know what you're doing.
void parseGlobalTypeArgc(int argc, char **argv, GlobalType defaultType);
void parseGlobalTypeCmdLine(const char *lpCmdLine, GlobalType defaultType);

// Is this a Server, or a client?
bool IsServer(void);
bool IsClient(void); // Not the same as !IsServer for GetVRML, GetTex, etc.
bool IsLoginServer(void);
bool IsUGCSearchManager(void);
bool IsAuctionServer(void);
bool IsAccountProxyServer(void);
bool IsMapManager(void);
bool IsChatServer(void);
bool IsTicketTracker(void);
bool IsGuildServer(void);
bool IsTeamServer(void);
bool IsQueueServer(void);
bool IsResourceDB(void);
bool IsGroupProjectServer(void);
bool IsGatewayServer(void);
bool IsGatewayLoginLauncher(void);


//WARNING - this returns true only for GLOBALTYPE_GAMESERVER, NOT for things like webRequestServer which are
//running gameserver.exe. Consider using IsGameServerBasedType() instead. Generally, if you want your code
//to do one thing on the server and one on the client, in shared code, use IsGameServerBasedType()
bool IsGameServerSpecificallly_NotRelatedTypes(void);
//WARNING - this returns true only for GLOBALTYPE_GAMESERVER, NOT for things like webRequestServer which are
//running gameserver.exe. Consider using IsGameServerBasedType() instead. Generally, if you want your code
//to do one thing on the server and one on the client, in shared code, use IsGameServerBasedType()


LATELINK;
bool IsGameServerBasedType(void); //ie, webrequestServer or anything else which is gameserver.exe internally

LATELINK;
bool IsAppServerBasedType(void);

void RegisterGenericGlobalTypes(void);

// sets/gets the product name (ie, "FightClub")
void SetProductName(const char *pProductName, const char *pShortName);
char *GetProductName(void); //returns "core" if SetProductName hasn't been called
char *GetProductName_IfSet(void); //returns NULL if SetProductName hasn't been called
char *GetShortProductName(void);
char *GetProductDisplayNameKey(void);
const char *GetProductDisplayName(Language langID);

// Returns the Project name, which is executable + short product
// Core returns just the executable
char *GetProjectName(void);
char *GetCoreProjectName(void);

// sets/gets schema version (if you increase this, the database will delete characters unless
// you set up a version transition function. 0 by default.
int GetSchemaVersion(void);
void SetSchemaVersion(int);


// what the product name will be if no one sets it
#define PRODUCT_NAME_UNSPECIFIED "Core"
#define SHORT_PRODUCT_NAME_UNSPECIFIED "CO"
#define PRODUCT_NAME_KEY_UNSPECIFIED "ProductName_Core"

// Manage ContainerRef structures

// Create a container ref, with the correct values
ContainerRef *CreateContainerRef(GlobalType type, ContainerID id);

// Destroys a single container ref
void DestroyContainerRef(ContainerRef *ref);

// Creates a ContainerRefArray, to which you can add container Refs
ContainerRefArray *CreateContainerRefArray(void);

// Push a single ref on the array
void AddToContainerRefArray(ContainerRefArray *array, GlobalType type, ContainerID id);

// Destroys an array object, and all container refs inside
void DestroyContainerRefArray(ContainerRefArray *array);

//given a string of the form "Launcher[5]", sets eType to GLOBALTYPE_LAUNCHER
//and iID to 5 and returns true, or returns false if the string can't be parsed
bool DecodeContainerTypeAndIDFromString(const char *pString, GlobalType *peType, ContainerID *piID);

//returns "LogParser[32]" 
char *GlobalTypeAndIDToString(GlobalType eType, ContainerID iID);


//special container IDs are used for various tricky reasons, and should NEVER be actually
//assigned to a container

//fake container ID used to represent the XBOX client (debug only)
#define SPECIAL_CONTAINERID_XBOX_CLIENT 0xffffffff

//if you pass this as the container ID for a transaction to the transaction server, then
//it will try to find the "best" possible server of the given type, that is, the one to which
//the most other step of the transaction are being sent.
#define SPECIAL_CONTAINERID_FIND_BEST_FOR_TRANSACTION 0xfffffffe

//send this transaction to a random server of this type
#define SPECIAL_CONTAINERID_RANDOM 0xfffffffd

//works only for non-returning normal remote commands
#define SPECIAL_CONTAINERID_ALL 0xfffffffc

#define LOWEST_SPECIAL_CONTAINERID 0xfffffff0

bool GlobalTypeIsCriticallyImportant(GlobalType eType);

bool GlobalServerTypeIsLowImportance(GlobalType eType);

LATELINK;
void ProdSpecificGlobalConfigSetup(void);



#define IsThisObjectDB() IsTypeObjectDB(GetAppGlobalType())
#define IsTypeObjectDB(type) (type == GLOBALTYPE_OBJECTDB || type == GLOBALTYPE_CLONEOBJECTDB || type == GLOBALTYPE_CLONEOFCLONE)

#define IsThisObjectDBOrMerger() IsTypeObjectDBOrMerger(GetAppGlobalType())
#define IsTypeObjectDBOrMerger(type) (type == GLOBALTYPE_OBJECTDB || type == GLOBALTYPE_CLONEOBJECTDB \
									  || type == GLOBALTYPE_CLONEOFCLONE || type == GLOBALTYPE_OBJECTDB_MERGER)

#define IsThisMasterObjectDB() IsTypeMasterObjectDB(GetAppGlobalType())
#define IsTypeMasterObjectDB(type) (type == GLOBALTYPE_OBJECTDB)

#define IsThisCloneObjectDB() IsTypeCloneObjectDB(GetAppGlobalType())
#define IsTypeCloneObjectDB(type) (type == GLOBALTYPE_CLONEOBJECTDB || type == GLOBALTYPE_CLONEOFCLONE)

#define IsValidGlobalType(type) ((type) > GLOBALTYPE_NONE && (type) < GLOBALTYPE_MAXTYPES)

C_DECLARATIONS_END
#endif

