/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "NNOCommon.h"
#include "file.h"
#include "WorldLibEnums.h"
#include "appRegCache.h"

#if defined(GAMECLIENT) || defined(GAMESERVER)
#include "rewardcommon.h"
#endif

AUTO_RUN_FIRST;
void RegisterProjectVars(void)
{
	SetProductName("Night", "NNO");
	regSetAppName("Neverwinter Nights Online");
}

void OVERRIDE_LATELINK_ProdSpecificGlobalConfigSetup(void)
{
	gConf.bLoadItems = 1;
	gConf.bLoadRewards = 1;
	gConf.bHideCombatMessages = 0;	
	gConf.bAllowOldEncounterData = 0;
	gConf.iDefaultEditorAggroRadius = 60;
	gConf.bUserContent = 1;
	gConf.bUGCSearchFromLoginServer = 1;
	gConf.bItemArt = 1;
	gConf.pcLevelingNumericItem = "XP";
	gConf.bAllowPlayerCostumeFX = 1;
	gConf.bAlwaysQuickplay = 1;
	gConf.bIgnoreUnboundItemCostumes = 1;
	gConf.bUseNNOPowerDescs = 1;
	gConf.bRequirePowerTrainer = 0;
	gConf.bAllowMultipleItemPowersWithCharges = 1;
	gConf.bUseNNOAlgoNames = 1;
	gConf.bAutoRewardLogging = 1;
	gConf.bAutoCombatLogging = 1;
	gConf.bVerboseCombatLogging = 1;
	gConf.bAllowNoResultRecipes = 1;
	gConf.bLogEncounterSummary = 1;
	gConf.bPlayerCanTrainPowersAnywhere = 1;
	gConf.bEncountersScaleWithActivePets = 1;
	gConf.bDisableEncounterTeamSizeRangeCheckOnMissionMaps = 1;
	gConf.bClickiesGlowOnMouseover = 0;//0 means clickies always glow
	gConf.bTimeControlAllowed = 1;
	gConf.IgnoreAwayTeamRulesForTeamPets = 1;
	gConf.bDeactivateHenchmenWhenDroppedFromTeam = 1;
	gConf.bShowMapIconsOnFakeZones = 1;
	gConf.bDamageFloatsDrawOverEntityGen = 1;
	gConf.bTabTargetingLoopsWithNoBreaks = 1;
	gConf.bTabTargetingAlwaysIncludesActiveCombatants = 1;
	gConf.bUseGlobalExpressionForInteractableGlowDistance = 1;
	gConf.bRewardTablesUseEncounterLevel = 1;
	//gConf.bDontAutomaticallySetMinLevelOnItems = 1; // - TODO: REALLY FIX ERROR BY ADDING THIS IN
	gConf.bTargetDual = 1;
	gConf.bCharacterPathMustBeFollowed = 1;
	gConf.bLootBagsRemainOnInteractablesUntilEmpty = 1;
	gConf.bEnableNNOTeamWarp = 1;

	gConf.fCostumeMirrorChanceRequired = 1.0;
	gConf.fCostumeMirrorChanceOptional = 1.0;
	gConf.bNNOInteractionTooltips = true;
	gConf.bManualSubRank = true;
	gConf.bDefaultToOpenTeaming = 1;
	gConf.bClientDangerData = 1;
	gConf.eCCGetBaseAttribValues = CCGETBASEATTRIBVALUES_RETURN_DD_BASE;
	gConf.eCCGetPointsLeft = CCGETPOINTSLEFT_RETURN_USE_DD_POINT_SYSTEM;
	gConf.eCCValidateAttribChanges = CCVALIDATEATTRIBCHANGES_USE_DD_RULES;
	gConf.fCharSelectionPortraitTextureFov = 50.0f;
	gConf.iCharSelectionHeadshotWidth = 512;
	gConf.iCharSelectionHeadshotHeight = 512;
	gConf.bKeepLootsOnCorpses = true;
	gConf.bDontAutoEquipUpgrades = true;
	//gConf.bDoNotShowHUDOptions = true;	
	gConf.bAutoBuyPowersGoInTray = true;
	gConf.pcNeedBeforeGreedThreshold = "Silver";
	gConf.fSpawnPointLoadingScreenDistSq = 160000; // 400 squared

#if defined(GAMECLIENT) || defined(GAMESERVER)
	rewardTable_RegisterItemTypeAsAlwaysSafeForGranting(kItemType_SavedPet);
	rewardTable_RegisterItemTypeAsAlwaysSafeForGranting(kItemType_TradeGood);
#endif

	// Hide the mission grant at root level
	gConf.bDoNotShowMissionGrantDialogsForContact = true;

	// Hide the in progress missions for the contact dialog
	gConf.bDoNotShowInProgressMissionsForContact = true;

	// Do not automatically show mission turn in dialog
	gConf.bDoNotSkipContactOptionsForMissionTurnIn = true;

	// We don't want the dialog to be ended if the contact offers the same mission for the mission offer game action
	gConf.bDoNotEndDialogForMissionOfferActionIfContactOffers = true;

	// Set the animlist for default contact animation
	gConf.pchClientSideContactDialogAnimList = "Contact_Idle";

	// Delay all notifications during contact dialog so they show up after the dialog ends
	gConf.bDelayNotificationsDuringContactDialog = true;

	gConf.bForceEnableAmbientCube = true; // Note, if adding this to a new project, you need to also twiddle two entries in ShaderGraphFlags to force the .bin to rebuild

	// Show open missions under personal missions if they are related
	gConf.bAddRelatedOpenMissionsToPersonalMissionHelper = true;

	// Remember visited dialogs
	gConf.bRememberVisitedDialogs = true;

	// Set the default guild theme
	gConf.pchDefaultGuildThemeName = "GuildTheme_Adventuring_Company";

	// Set the launch video
	gConf.pchLaunchVideoPath = "fmv/LaunchVideo.bik";

	// The name of the interactable category for doors
	gConf.pchCategoryNameForDoors = "Door";

	// Enable persisted stores
	gConf.bEnablePersistedStores = true;

	// Pets won't collide with players
	gConf.bPetsDontCollideWithPlayer = true;

	// Use D&D base stats for stat points UI in character creation
	gConf.bUseDDBaseStatPointsFunction = true;

	//gConf.maxServerFPS = 120.f;
	//gConf.combatUpdateTimer = 0.025f;

	gConf.bOverheadEntityGens = true;
	gConf.pchOverheadGenBone = "FX_RootScale";
	gConf.bManageOffscreenGens = true;
	gConf.iMaxOffscreenIconsPlayers = 4;
	gConf.iMaxOffscreenIconsCritters = 4;


	// Enable the lobby
	gConf.bEnableLobby = true;

	gConf.bDisableSuperJump = true;

	gConf.fDefaultTooltipDelay = 0.5f;
	gConf.pchInitialProgressionNode = "Quest_Tutorial";

	// Enable game server auto map selection algorithm
	gConf.bAutoChooseMapOptionInGameServer = true;

	gConf.bNoEntityCollision = true;

	//This should be true if you want the map and minimap to scale to default when moving to a new area
	gConf.bSetMapScaleDefaultOnLoad = true;
}

bool OVERRIDE_LATELINK_isValidRegionTypeForGame(U32 world_region_type)
{
	return (world_region_type == WRT_Ground) || 
		   (world_region_type == WRT_CharacterCreator) || 
		   (world_region_type == WRT_Indoor) || 
		   (world_region_type == WRT_None);
}

