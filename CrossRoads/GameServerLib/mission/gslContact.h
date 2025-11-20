/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef CONTACT_H
#define CONTACT_H

#include "contact_enums.h"

typedef U32 EntityRef;
typedef enum ContactInteractType ContactInteractType;
typedef struct ContactCostume ContactCostume;
typedef struct ContactCostumeFallback ContactCostumeFallback;
typedef struct ContactDef ContactDef;
typedef struct ContactDialogInfo ContactDialogInfo;
typedef struct ContactDialogOption ContactDialogOption;
typedef struct ContactDialogOptionData ContactDialogOptionData;
typedef struct ContactHeadshotData ContactHeadshotData;
typedef struct ContactImageMenuItem ContactImageMenuItem;
typedef struct ContactInfo ContactInfo;
typedef struct ContactInteractState ContactInteractState;
typedef struct ContactMissionOffer ContactMissionOffer;
typedef struct ContactRewardChoices ContactRewardChoices;
typedef struct DebugMenuItem DebugMenuItem;
typedef struct DialogBlock DialogBlock;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct ImageMenuItemOverrideData ImageMenuItemOverrideData;
typedef struct ItemDef ItemDef;
typedef struct Mission Mission;
typedef struct MissionDef MissionDef;
typedef struct MissionOfferOverride MissionOfferOverride;
typedef struct MissionOfferOverrideData MissionOfferOverrideData;
typedef struct PetContactList PetContactList;
typedef struct PetDef PetDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct RemoteContact RemoteContact;
typedef struct SpecialActionBlock SpecialActionBlock;
typedef struct SpecialActionBlockOverrideData SpecialActionBlockOverrideData;
typedef struct SpecialDialogBlock SpecialDialogBlock;
typedef struct SpecialDialogOverrideData SpecialDialogOverrideData;
typedef struct StoreDef StoreDef;
typedef struct WorldScope WorldScope;

AUTO_STRUCT;
typedef struct SpecialDialogBlockRandomData
{
	const char* pchDialogBlockName; AST(KEY POOL_STRING)
	S32 iSeed;
} SpecialDialogBlockRandomData;
extern ParseTable parse_SpecialDialogBlockRandomData[];
#define TYPE_parse_SpecialDialogBlockRandomData SpecialDialogBlockRandomData

AUTO_STRUCT;
typedef struct DialogResponseQueueItem
{
	int iPartitionIdx;

	// The entity who initiated the response
	EntityRef entRef;

	// The key of the dialog option selected
	char* pchResponseKey; 
	
	// The reward choices
	ContactRewardChoices* pRewardChoices;

	// The time response is initiated
	U32 uiQueueEntryTime;

	// The flag that tells us if we've already notified the team members to clear the dialog choice made
	bool bNotifiedTeamMembersToClearDialogChoice;

	// The ID of the entity who enabled the option for the team spokesman. Only valid for critical team dialogs.
	U32 iOptionAssistEntID;
} DialogResponseQueueItem;

AUTO_STRUCT;
typedef struct ContactLocation{
	const char *pchContactDefName;	AST(KEY POOL_STRING)
	const char *pchStaticEncName;		AST(POOL_STRING)
	Vec3 loc;
} ContactLocation;
extern ParseTable parse_ContactLocation[];
#define TYPE_parse_ContactLocation ContactLocation

// List of Contacts on the current map
extern ContactLocation** g_ContactLocations;

void contact_DebugMenu(Entity* playerEnt, DebugMenuItem* groupRoot);

// Loads all project data and initialize all contact related systems
void contactsystem_Load(void);

// Returns true if contact data has been loaded on this server
bool contactsystem_IsLoaded();

// Called on map load
void contactsystem_MapLoad(void);
// Called on map validation
void contactsystem_MapValidate(void);
// Called on map unload
void contactsystem_MapUnload(void);

// Called when loading a ContactDef
int contact_DefPostProcess(ContactDef* def);

// Can a player interact with this ContactDef?
bool contact_CanInteract(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt);

// Can a player interact with this ContactDef remotely?
bool contact_CanInteractRemote(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID Entity* playerEnt);

// Begin interaction with a contact
void contact_InteractBegin(Entity* playerEnt, Entity *pContactEnt, ContactDef* contact, const char *dialogName, ContactCostumeFallback* pCostumeFallback);
void contact_InteractBeginWithOptionData(Entity* pPlayerEnt, ContactDialogOptionData* pOptionData);
void contact_InteractBeginWithInitialOption(Entity* pPlayerEnt, Entity *pContactEnt, ContactDef* pContactDef, ContactDialogOptionData* pInitialOption, ContactCostumeFallback* pCostumeFallback);

// End interaction with the current contact
void contact_InteractEnd(Entity* playerEnt, bool bPersistCleanup);

// Choose a single mission from a contact, then exit
void contact_ChooseMissionInteractBegin(Entity* playerEnt, char* contactName);

// Get the reward for a single mission from a contact, then exit
void contact_SingleMissionRewardInteractBegin(Entity* playerEnt, char* contactName, char* missionName, bool noRewardHack);

// Updates a players contact interaction information
// Also sets of any nearby player callout strings
void contact_ProcessPlayer(SA_PARAM_NN_VALID Entity* playerEnt);

// Updates a player's current Contact Dialog info
void contact_ProcessDialogsForPlayer(SA_PARAM_NN_VALID Entity* playerEnt);

// Whenever a player's inventory changes, this should be called
void contact_PlayerInventoryChanged(Entity* pEnt);

// Response from a player to the contact on the server
void contact_InteractResponse(SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_NN_STR const char* responseKey, SA_PARAM_OP_VALID ContactRewardChoices* rewardChoices, U32 iOptionAssistEntID, bool bAutomaticallyChosenBySystem);


bool contact_MissionCanBeOffered(SA_PARAM_NN_VALID ContactDef* contact, SA_PARAM_NN_VALID MissionDef* missionDef, SA_PARAM_NN_VALID ContactMissionOffer* missionOffer, SA_PARAM_NN_VALID Entity* playerEnt, int* nextOfferLevel);

bool contact_CanReturnMissionHere(SA_PARAM_NN_VALID const ContactDef* contactDef, SA_PARAM_NN_VALID MissionDef* missionDef, SA_PARAM_NN_VALID const ContactMissionOffer* missionOffer);

bool contact_HasSpecialDialogForPlayer(SA_PARAM_NN_VALID Entity* playerEnt, SA_PARAM_NN_VALID ContactDef* contact);

// Creates a potential pet contact from an item with a PetDef
bool contact_CreatePotentialPetFromItem(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pEntSrc, S32 iBagID, S32 iSlot, SA_PARAM_NN_VALID ItemDef* pItemDef, bool bFirstInteract, bool bQueueContact);

// Creates a trainer contact using the trainable node list on an item
bool contact_CreateTrainerFromEntity(SA_PARAM_NN_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pTrainer);

// Creates a power store contact using powers on an item
bool contact_CreatePowerStoreFromItem(Entity* pPlayerEnt, S32 iType, U32 iID, S32 iBagID, S32 iSlot, U64 iItemID, bool bIsOfficerTrainer);

// Creates a contact dialog based on an itemDef that the player has in their inventory.  Returns false if the mission can't be granted (player doesn't meet requirements)
bool contact_OfferMissionFromItem(SA_PARAM_OP_VALID Entity* playerEnt, SA_PARAM_NN_VALID ItemDef* itemDef, SA_PARAM_NN_VALID MissionDef* missionDef);

// Creates a contact dialog to offer a mission with the specificed headshot and display name
bool contact_OfferMissionWithHeadshot(Entity* playerEnt, MissionDef* missionDef, const char* estrHeadshotDisplayName, ContactHeadshotData* pHeadshotData);

// Brings up a dialog for a shared/queued Mission
void contact_OfferNextQueuedMission(SA_PARAM_OP_VALID Entity* playerEnt);

// Checks whether a player is near a Nemesis contact
bool contact_IsPlayerNearNemesisContact(Entity *pEnt);

// Shows a Lore screen to the player
void contact_DisplayLoreItem(Entity* pPlayerEnt, ItemDef *pItemDef);

// Create a contact that grants a specified mission
bool contactdialog_CreateNamespaceMissionGrantContact(Entity* pEnt, const char* pchMission, PlayerCostume* pCostume, PetContactList* pPetContactList, const char* pchMissionDetails);

// Add a remote contact to the remote contact list
void contact_AddRemoteContactToServerList(ContactDef* pContact, ContactFlags eRemoteFlags);

// Get the remote contact list
void contact_GetGlobalRemoteContactList(RemoteContact*** peaLocalList);

// Evaluate and trim the remote contact list for a given player
void contact_EvaluateRemoteContactList(SA_PARAM_NN_VALID Entity* pEnt);

// Evaluate an expression for a contact
int contact_Evaluate(Entity *pEnt, Expression *pExpr, WorldScope *pScope);

// contact_EvalSetupContext and contact_EvaluateAfterManualContextSetup are like contact_Evaluate,
// but faster if you need to evaluate many expressions with the same context vars.
void contact_EvalSetupContext(Entity *pEnt, WorldScope *pScope);
int contact_EvaluateAfterManualContextSetup(Expression *pExpr);

// Refresh the entity's remote contact list
void contact_entRefreshRemoteContactList(SA_PARAM_NN_VALID Entity* pEnt);

// Called by designer init functions to clean up player contact tracking
void contact_ClearPlayerContactTrackingData(Entity *pPlayerEnt);
void contact_ClearAllPlayerContactTrackingData(void);

// Begins the next queued contact dialog (if any) for the player
void contact_MaybeBeginQueuedContact(Entity* pPlayerEnt);

// Begins the next not in combat contact dialog (if any) for the player
void contact_MaybeBeginNotInCombatContact(Entity* pPlayerEnt);

// If the player currently has a contact dialog open, this function adds the specified dialog to the player's queue.
// Otherwise, the specified dialog will launch immediately
void contact_addQueuedContact(Entity* pPlayerEnt, ContactDef* pContactDef, const char* pchDialogName, bool bPartialPermissions, bool bRemotelyAccessing, PlayerCostume* pCostumeFallback, char* pchDisplayNameFallback, bool bClearPreviousDialog);
//Add a queued contact without a ContactDef
bool contact_addQueuedContactFromOptionData(Entity* pPlayerEnt, ContactDialogOptionData* pOptionData);

// Perform game actions for player
bool contactdialog_PerformSpecialDialogAction(SA_PARAM_NN_VALID Entity* playerEnt, ContactDef *pContactDef, ContactDialogOptionData *pData, bool bTeamSpokesmanOrNoTeam);
bool contactdialog_PerformOptionalAction(SA_PARAM_NN_VALID Entity* playerEnt, ContactDef *pContactDef, ContactDialogOptionData *pData);

// Tally a voting player's dialog option choice for a team dialog
bool contactdialog_AddTeamDialogVote(SA_PARAM_OP_VALID Entity* pEnt, ContainerID uVoterID, const char* pchDialogKey);

// Tick function for the contact system
void contact_OncePerFrame(F32 fTimeStep);

// Generates ContactHeadshotData from a ContactCostume
bool contact_CostumeToHeadshotData(Entity* pPlayerEnt, ContactCostume* pCostumePrefs, ContactHeadshotData** ppReturn);

bool contact_ApplyNamespacedSpecialDialogOverride(const char *pchMissionName, ContactDef *pContactDef, SpecialDialogBlock *pDialog);
void contact_RemoveNamespacedSpecialDialogOverridesFromMission(const char *pcMissionName);

bool contact_ApplyNamespacedMissionOfferOverride(ContactDef *pContactDef, ContactMissionOffer *pOffer);
void contact_RemoveNamespacedMissionOfferOverridesFromMission(const char *pcMissionName);

bool contact_ApplyNamespacedImageMenuItemOverride(const char *pchMissionName, ContactDef *pContactDef, ContactImageMenuItem *pItem);
void contact_RemoveNamespacedImageMenuItemOverridesFromMission(const char *pcMissionName);

MissionOfferOverrideData** contact_GetNamespacedMissionOfferOverrideList(ContactDef* pContact);
SpecialDialogOverrideData** contact_GetNamespacedSpecialDialogOverrideList(ContactDef* pContact);
ImageMenuItemOverrideData** contact_GetNamespacedImageMenuItemOverrideList(ContactDef* pContact);

SpecialDialogBlock* contact_SpecialDialogOverrideFromName(ContactDef* pContact, const char* pchSpecialDialog);
S32 contactdialog_SetTextFromDialogBlocks(Entity *pEnt, DialogBlock ***peaDialogBlocks, char** estrResult, const char** ppchPooledSound, const char **ppchPhrasePath, ReferenceHandle *pDialogFormatterRefHandle);
SpecialActionBlock* contact_SpecialActionBlockOverrideFromName(ContactDef *pContact, const char *pchSpecialActionBlock);

ContactDialogInfo* contact_GetLastCompletedDialog(Entity* pEnt, ContactInfo** ppContactInfo, bool bValidateInteract);

#endif
