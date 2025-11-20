/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLEVENTSEND_H
#define GSLEVENTSEND_H

#include "encounter_enums.h"
#include "entEnums.h"
#include "GameEvent.h"
#include "GlobalTypeEnum.h"
#include "GlobalEnums.h"
#include "itemEnums.h"
#include "MinigameCommon.h"
#include "mission_enums.h"

typedef struct Entity Entity;
typedef struct Mission Mission;
typedef struct UGCMissionData UGCMissionData;
typedef struct ContactDef ContactDef;
typedef struct ContactInteractState ContactInteractState;
typedef struct Critter Critter;
typedef struct GameEncounter GameEncounter;
typedef struct GameEncounterPartitionState GameEncounterPartitionState;
typedef struct GameEventParticipant GameEventParticipant;
typedef struct GameInteractable GameInteractable;
typedef struct GameNamedVolume GameNamedVolume;
typedef U32 EntityRef; // This is an opaque entity identifier, which is implemented elsewhere
typedef struct OldEncounter OldEncounter;
typedef struct PowerDef PowerDef;
typedef struct AttribModDef AttribModDef;
typedef struct GameEvent GameEvent;
typedef struct MissionDef MissionDef;
typedef struct FSM FSM;
typedef struct KillCreditTeam KillCreditTeam;

// Called for manually created GameEvents
void eventsend_AddEntSourceExtern(GameEvent *pEvent, int iPartitionIdx, Entity *pEnt, F32 fCreditPercentage, bool bHasCredit, F32 fTeamCreditPercentage, bool bHasTeamCredit);
void eventsend_AddEntTargetExtern(GameEvent *pEvent, int iPartitionIdx, Entity *pEnt, F32 fCreditPercentage, bool bHasCredit, F32 fTeamCreditPercentage, bool bHasTeamCredit);

// Used for interesting things
GameEventParticipant* eventsend_GetOrCreateEntityInfo(Entity *pEnt);

// Called whenever something on a map dies, regardless of who or what killed it
void eventsend_RecordDeath(KillCreditTeam ***peaCreditTeams, Entity *pDeadEnt);

// Called for Throwable/destructible objects that are destroyed while not in entity form
void eventsend_RecordObjectDeath(Entity *pKillerEnt, const char *pcObject);

//Called when a player enters the near death state
void eventsend_RecordNearDeath(Entity *pNearlyDeadEnt);

// Called when a throwable/destructable object is picked up
void eventsend_RecordPickupObject(Entity *pLifterEnt, Entity *pLiftedEnt);

void eventsend_RecordPvPEvent(int iPartitionIdx, PvPEvent ePvPEvent, Entity **eaSourceEnts, Entity **eaTargetEnts);

// Encounter changed states event
void eventsend_EncounterStateChange(int iPartitionIdx, OldEncounter *pEncounter, EncounterState eState);
void eventsend_Encounter2StateChange(GameEncounter *pEncounter, GameEncounterPartitionState *pState, EncounterState eState);

// The critter has changed to a new state
void eventsend_EncounterActorStateChange(Entity *pCritterEnt, char *pcStateName);

// A critter has "poked" some entities from its FSM.  (Generic Event to complete Missions with)
void eventsend_RecordPoke(int iPartitionIdx, Entity *pSourceEnt, Entity **eaTargetEnts, const char *pcMessage);

// A global FSM (not a critter) changed state
void eventsend_EncLayerFSMStateChange(int iPartitionIdx, FSM *pFSM, char *pcStateName);

// A mission or submission changed state
void eventsend_RecordMissionState(int iPartitionIdx, Entity *pEnt, const char *pcPooledMissionRefString, MissionType eMissionType, MissionState eState, const char *pcPooledMissionCategoryName, bool bIsRoot, UGCMissionData *pUGCMissionData);
void eventsend_RecordMissionStateMultipleEnts(int iPartitionIdx, Entity ***peaEnts, const char *pcPooledMissionRefString, MissionType eMissionType, MissionState eState, const char *pcPooledMissionCategoryName, bool bIsRoot, UGCMissionData *pUGCMissionData);

// A MissionLockoutList changed state
void eventsend_RecordMissionLockoutState(int iPartitionIdx, const char *pcPooledMissionRefString, MissionType eMissionType, MissionLockoutState eState, const char *pcPooledMissionCategoryName);

// Arbitrary string global event, passed directly to the systems that care with no modification
void eventsend_RecordEvent(Entity *playerEnt, char *pcEventName);

// Record that a player has gained a level
void eventsend_RecordLevelUp(Entity *pEnt);

//Record that a player's pet has gained a level
void eventsend_RecordLevelUpPet(Entity *pOwner);

// Record any damage that has been done
void eventsend_RecordDamage(Entity *pDamagedEnt, Entity *pSourceEnt, int iDamageDone, const char *pcDamageType);
void eventsend_RecordHealing(Entity *pDamagedEnt, Entity *pSourceEnt, int iHealingDone, const char *pcAttribType);
void eventsend_RecordHealthState(Entity *pDamagedEnt, F32 fOldHealth, F32 fNewHealth);
void eventsend_RecordNewHealthState(Entity *pEnt, F32 fHealth);

// Record that a Special Dialog has been shown to the player
void eventsend_RecordDialogStart(Entity *pPlayerEnt, ContactDef *pContactDef, const char *pcSpecialDialogName);
// Record that a Special Dialog has been completed by the player
void eventsend_RecordDialogComplete(SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_NN_VALID ContactDef *pContactDef, SA_PARAM_OP_VALID const char *pcSpecialDialogName);
// Record that the player has viewed the contact info (actually sends a DialogComplete event)
void eventsend_RecordContactInfoViewed(SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_NN_VALID ContactDef *pContactDef);

// Record that a player starts interacting on something interactable
void eventsend_RecordInteractBegin(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume);
// Record that a player has successfully interacted with an interactable object
void eventsend_RecordInteractSuccess(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume);
// Record that interacting with an interactable failed (Player got a dialog)
void eventsend_RecordInteractFailure(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume);
// Record that interacting with an interactable was interrupted
void eventsend_RecordInteractInterrupted(Entity *pPlayerEnt, Entity *pTargetEnt, ContactDef *pContactDef, GameInteractable *pInteractable, GameNamedVolume *pVolume);
// ...and that an interactable is no longer "active"
void eventsend_RecordInteractableEndActive(int iPartitionIdx, Entity *pPlayerEnt, GameInteractable *pInteractable);

// Records that a Power applied a MissionEvent AttribMod
void eventsend_RecordPowerAttribModApplied(SA_PARAM_OP_VALID Entity *pSourceEnt,
										   SA_PARAM_NN_VALID Entity *pTargetEnt,
										   SA_PARAM_NN_VALID PowerDef *pPowerDef,
										   SA_PARAM_OP_STR const char *pcEventName);

// Record that the player entered or exited a volume
void eventsend_RecordVolumeEntered(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID GameNamedVolume *pVolume);
void eventsend_RecordVolumeExited(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID GameNamedVolume *pVolume);

// Player Spawn In
void eventsend_PlayerSpawnIn(Entity *pPlayerEnt);

// Cutscenes
void eventsend_RecordCutsceneStart(int iPartitionIdx, const char  *pcSceneName, EntityRef **peauPlayerRefs);
void eventsend_RecordCutsceneEnd(int iPartitionIdx, const char *pcSceneName, EntityRef **peauPlayerRefs);

// Videos
void eventsend_RecordVideoStarted(Entity* pPlayerEnt, const char* pchVideoName);
void eventsend_RecordVideoEnded(Entity* pPlayerEnt, const char* pchVideoName);

// Item/Inventory
void eventsend_RecordItemGained(Entity *pPlayerEnt, const char *pcItemName, ItemCategory *eaiCategories, int iCount);
void eventsend_RecordItemLost(Entity *pPlayerEnt, const char *pcItemName, ItemCategory *eaiCategories, int iCount);

void eventsend_RecordItemPurchased(Entity *pPlayerEnt, const char *pcItemName, const char *pcStoreName, const char *pcContactName, int iCount, int iEPValue);

void eventsend_RecordItemUsed(Entity *pPlayerEnt, const char *pcItemName, const char *pcPowerName);

void eventsend_Emote(Entity *pPlayerEnt, const char *pcEmoteName);

void eventsend_RecordNemesisState(Entity *pPlayerEnt, U32 iNemesisID, NemesisState eNewState);

void eventsend_RecordBagGetsItem(Entity *pPlayerEnt, InvBagIDs eBagType);

// PVP/Dueling
void eventsend_RecordDuelVictory(Entity *pVictorEnt, Entity *pDefeatedEnt, PVPDuelVictoryType eType);
void eventsend_RecordPvPQueueMatchResult(Entity *pEnt, PvPQueueMatchResult eResult, const char *pcMissionCategory);

// Minigames
void eventsend_RecordMinigameBet(Entity *pEnt, MinigameType eMinigameType, const char *pcItemName, int iCount);
void eventsend_RecordMinigamePayout(Entity *pEnt, MinigameType eMinigameType, const char *pcItemName, int iCount);
void eventsend_RecordMinigameJackpot(int iPartitionIdx, Entity *pEnt, MinigameType eMinigameType, GameInteractable *pInteractable);

// ItemAssignments
void eventsend_RecordItemAssignmentStarted(int iPartitionIdx, Entity* pEnt, const char* pchName);
void eventsend_RecordItemAssignmentCompleted(int iPartitionIdx, Entity* pEnt, const char* pchName, const char* pchOutcome, F32 fItemAssignmentSpeedBonus);

void eventsend_RecordGemSlotted(Entity *pPlayerEnt, const char *pcItemName, const char *pcGemName);

void eventsend_RecordPowerTreeStepsAdded(Entity* pEnt);

void eventsend_RecordGroupProjectTaskComplete(int iPartitionIdx, Entity *pEnt, const char *pcProjectName);

void eventsend_RecordAllegianceSet(int iPartitionIdx, Entity *pEnt, const char *pcAllegianceName);

void eventsend_ContestWin(Entity *pPlayerEnt, int iRewardTier, const char *pchMissionRefString);

void eventsend_RecordScoreboardMetricFinish(Entity *pEnt, const char *pchMetric, S32 iRank);

void eventsend_RecordUGCAccountChanged(int iPartitionIdx, Entity *pEnt);

#endif
