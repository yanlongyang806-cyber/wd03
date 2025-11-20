#pragma once
GCC_SYSTEM

// READ BEFORE EDITING THIS ENUM
// 
// These enums are used not just by the client, but also on the game server. 
// Because maps can have actions on them that send these notifications, you should
// never remove any entries in the enum. In order to keep these systems decoupled
// the server stores them as strings. Since their value does not matter you may 
// rearrange them as long as no entries are ever actually removed.
// 
// If you ever add new NotifyTypes, please update the list at:
// todo: just put auto wiki this
// http://crypticwiki:8081/display/Core/Notifications

AUTO_ENUM;
typedef enum NotifyType
{
	kNotifyType_Default = 0,

	// Generic failure.
	kNotifyType_Failed,

	// High-priority broadcast from server administrators
	kNotifyType_ServerBroadcast,

	// High-priority announcement from server administrators.
	// These names are horrible, but they map to the two commands CSR has.
	kNotifyType_ServerAnnounce,

	// High-priority announcement from a game system.
	kNotifyType_GameplayAnnounce,

	// The server forced a disconnect / logout.
	kNotifyType_ForcedDisconnect,
	kNotifyType_LogoutCancel,
	
	// The name given was invalid (profane, bad characters, in use, etc)
	kNotifyType_NameInvalid,
	kNotifyType_ShipNameInvalid,
	kNotifyType_FirstNameInvalid,
	kNotifyType_MiddleNameInvalid,
	kNotifyType_LastNameInvalid,
	kNotifyType_FormalNameInvalid,
	// The description given was invalid (profane etc)
	kNotifyType_DescriptionInvalid,

	kNotifyType_ItemReceived,
	kNotifyType_MissionGrantItemReceived,
	kNotifyType_MissionGrantItemFailed,
	kNotifyType_ItemLost,
	kNotifyType_ExperienceReceived,
	kNotifyType_NumericSet,
	kNotifyType_NumericLevelSet,
	kNotifyType_NumericReceived,
	kNotifyType_NumericLost,
	kNotifyType_LevelUp,
	kNotifyType_InventoryFull,
	kNotifyType_ItemRequired,
	kNotifyType_TradeFailed,
	kNotifyType_ItemUseFailed,
	kNotifyType_ItemBuyFailed,
	kNotifyType_ItemMoveFailed,
	kNotifyType_DuplicateUniqueItem,
	kNotifyType_EquipLimitCheck,
	kNotifyType_CraftingSkillChanged,
	kNotifyType_ItemAssignmentFeedback,
	kNotifyType_ItemAssignmentFeedbackFailed,
	kNotifyType_RewardPackOpened,
	kNotifyType_RewardPackOpenFailure,

	kNotifyType_MailReceived,
	kNotifyType_MailSendFailed,
	kNotifyType_NPCMailSendFailed,

	kNotifyType_OpenMissionNearby,
	kNotifyType_CrimeComputerUpdate,

	kNotifyType_PvPGeneral,
	kNotifyType_PvPCountdown,
	kNotifyType_PvPStart,
	kNotifyType_PvPLoss,
	kNotifyType_PvPWin,
	kNotifyType_PvPWarning,
	kNotifyType_PVPPoints,
	kNotifyType_PVPKillingSpree,
	kNotifyType_PVPKill,

	kNotifyType_AwayKickWarning,

	kNotifyType_BuildChanged,
	kNotifyType_BuildReceived,

	kNotifyType_CostumeChanged,
	kNotifyType_FreeCostumeChange,

	kNotifyType_MissionFloater,
	kNotifyType_RespawnUnlocked,
	kNotifyType_NeighborhoodEntered,
	kNotifyType_PerkAppeared,
	kNotifyType_PerkCompleted,
	kNotifyType_LoreDiscovered,
	kNotifyType_GameTimerTimeAdded,
	
	kNotifyType_PlayerStatChange, 

	// Mission states/updates (normal missions)
	kNotifyType_MissionStarted,
	kNotifyType_MissionCountUpdate,
	kNotifyType_MissionSuccess,
	kNotifyType_MissionSubObjectiveComplete,
	kNotifyType_MissionInvisibleSubObjectiveComplete, // Sub-objectives without UI string or Display string
	kNotifyType_MissionFailed,
	kNotifyType_MissionTurnIn,
	kNotifyType_MissionDropped,
	kNotifyType_MissionJournalFull,
	kNotifyType_MissionError,
	kNotifyType_MissionReturnError,

	// Mission states/updates (Open Missions)
	kNotifyType_OpenMissionSuccess,
	kNotifyType_OpenMissionSubObjectiveComplete,
	kNotifyType_OpenMissionFailed,

	kNotifyType_SharedMissionOffered,
	kNotifyType_SharedMissionAccepted,
	kNotifyType_SharedMissionDeclined,
	kNotifyType_SharedMissionError,
	
	kNotifyType_MissionSplatFX,

	kNotifyType_PowerExecutionFailed,
	kNotifyType_FromPower,
	kNotifyType_TacticalAimDisabled,
	kNotifyType_PowerAttribGained,

	kNotifyType_InteractionSuccess,
	kNotifyType_InteractionFailed,
	kNotifyType_InteractionInterrupted,
	kNotifyType_InteractionDenied,  // the server said no

	kNotifyType_TeamLoot,
	kNotifyType_TeamLootResult,
	kNotifyType_TeamError,
	kNotifyType_TeamFeedback,
	kNotifyType_GuildError,
	kNotifyType_GuildFeedback,
	kNotifyType_GuildDialog,
	kNotifyType_GuildMotD,
	kNotifyType_GuildInfo,

	kNotifyType_CostumeUnlocked,
	kNotifyType_VanityPetUnlocked,

	kNotifyType_Died,

	kNotifyType_TicketError,
	kNotifyType_TicketCreated,

	kNotifyType_ChatAdmin,

	kNotifyType_ChatFriendRequestSent,
	kNotifyType_ChatFriendError,
	kNotifyType_ChatFriendNotify,
	kNotifyType_ChatIgnoreError,
	kNotifyType_ChatIgnoreNotify,
	kNotifyType_ChatLookupError,
	kNotifyType_ChatTellReceived,
	kNotifyType_ChatTeamMessageReceived,
	kNotifyType_ChatAnonymous,
	kNotifyType_ChatLFG,

	kNotifyType_ItemDeconstructed,
	kNotifyType_ExperimentFailed,
	kNotifyType_ExperimentComplete,
	kNotifyType_CraftingSkillCapReached,
	kNotifyType_CraftingRecipeLearned,

	kNotifyType_NemesisAdded,
	kNotifyType_NemesisError,
	kNotifyType_PetAdded,
	kNotifyType_PuppetTransformFailed,

	kNotifyType_SidekickingFailed,

	kNotifyType_LevelUp_OtherInfo,

	kNotifyType_TrainingStarted,
	kNotifyType_TrainingComplete,
	kNotifyType_TrainingCanceled,
	kNotifyType_TrainingAvailable,
	kNotifyType_TrainerNodeUnlocked,
	kNotifyType_SuperCritterPet,

	kNotifyType_TwitterError,
	kNotifyType_TweetSent,
	kNotifyType_TwitterFriendUpdated,

	kNotifyType_FacebookError,
	kNotifyType_FacebookStatusUpdated,
	kNotifyType_FacebookScreenshotUploaded,

	kNotifyType_FlickrError,
	kNotifyType_FlickrScreenshotUploaded,

	kNotifyType_MediaControlError,

	kNotifyType_LiveJournalError,
	kNotifyType_LiveJournalPostSent,

	kNotifyType_AuctionFailed,
	kNotifyType_AuctionSuccess,

	kNotifyType_ServerOffline,

	kNotifyType_MicroTransSuccess,
	kNotifyType_MicroTransFailed,
	kNotifyType_MicroTransFailed_PriceChanged,
	kNotifyType_MicroTrans_SpecialItems,
	kNotifyType_MicroTrans_PointBuySuccess,
	kNotifyType_MicroTrans_PointBuyFailed,
	kNotifyType_MicroTrans_PointBuyPending,

	kNotifyType_EntityResolve_NotFound,
	kNotifyType_EntityResolve_Ambiguous,

	kNotifyType_Tip_General,
	kNotifyType_SkillSet,
	kNotifyType_StuckWarning,
	kNotifyType_ControlScheme_ChangeSucceeded,
	kNotifyType_ControlScheme_ChangeFailed,
	kNotifyType_Costume_JPeg_Saved,
	kNotifyType_RespecSuccess,
	kNotifyType_RespecFailed,
	kNotifyType_Skillcheck,

	kNotifyType_CombatAlert,
	kNotifyType_ControlledPetFeedback,

	kNotifyType_EdgeOfMap,
	kNotifyType_RequestLeaveMap,

	kNotifyType_TimeControl,

	kNotifyType_CannotInteractWithTeamContact,

	kNotifyType_TeamDialogError,

	kNotifyType_MiniContact,

	kNotifyType_PowerGranted,

	kNotifyType_ItemRewardDirectGive,

	kNotifyType_Puzzle,

	kNotifyType_UGCFeedback,
	kNotifyType_UGCError,
	kNotifyType_UGCKillCreditLimit,

	kNotifyType_CannotUseEmote,

	kNotifyType_StoryIntro,

	kNotifyType_BugReport,

	kNotifyType_LegacyFloaterMsg,

	// Notifications to open/bring attention to different UI's.
	kNotifyType_NewItemAssignment,

    kNotifyType_NumericConversionFailure,
    kNotifyType_NumericConversionSuccess,

	kNotifyType_FoundryTipsFailure,
    kNotifyType_FoundryTipsSuccess,

	kNotifyType_CurrencyExchangeSuccess,
	kNotifyType_CurrencyExchangeFailure,

	kNotifyType_SelectAllegiance,

	// Power slotting failed because the power player tried to slot does not have 
	// the required category defined in the power slot
	kNotifyType_PowerSlottingError_PowerCategoryDoesNotExistInInclusionList,

	// Power slotting failed because the power player tried to slot has one of the 
	// excluded categories defined in the power slot
	kNotifyType_PowerSlottingError_PowerCategoryExistsInExclusionList,

	// Power slotting failed because the slot is locked
	kNotifyType_PowerSlottingError_SlotIsLocked,

	kNotifyType_EventStage,

	kNotifyType_MapTransferDenied,
	kNotifyType_MapTransferFailed_NoPuppet,

	// Event reminders
	kNotifyType_EventAboutToStart,
	kNotifyType_EventStarted,
	kNotifyType_EventMissed,

	kNotifyType_ItemSmashSuccess,
	kNotifyType_ItemSmashFailure,

	//Notification for when an open mission event has started
	// probably overlaps with kNotifyType_EventStarted but that seemed to be used for reminders
	kNotifyType_OpenMissionStarted,

	// Item transmutation
	kNotifyType_ItemTransmutationSuccess,
	kNotifyType_ItemTransmutationFailure,
	kNotifyType_ItemTransmutationDiscardSuccess,
	kNotifyType_ItemTransmutationDiscardFailure,

    // Group project system notifications.
    kNotifyType_GroupProjectDonationFailed,

	// UGC Achievements
	kNotifyType_UGCAchievementProgress,
	kNotifyType_UGCAchievementGranted,

    // Promo Game Currency
    kNotifyType_PromoGameCurrencyClaimFailed,

	// Voice Channel
	kNotifyType_VoiceChannelJoin,
	kNotifyType_VoiceChannelLeave,
	kNotifyType_VoiceChannelFailure,

	kNotifyType_COUNT
	// BEFORE EDITING THIS ENUM PLEASE READ THE COMMENT AT THE TOP
	// OR YOU WILL BE EATEN BY RAPTORS

} NotifyType;
