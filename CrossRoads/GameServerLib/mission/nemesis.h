/***************************************************************************
*     Copyright (c) 2008-2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef NEMESIS_H
#define NEMESIS_H

#include "entEnums.h"

typedef struct Player Player;
typedef struct Entity Entity;
typedef struct PlayerNemesis PlayerNemesis;
typedef struct NemesisPowerSet NemesisPowerSet;
typedef struct NemesisMinionPowerSet NemesisMinionPowerSet;
typedef struct NemesisMinionCostumeSet NemesisMinionCostumeSet;
typedef struct NOCONST(Entity) NOCONST(Entity);
typedef enum enumTransactionOutcome enumTransactionOutcome;
typedef struct PlayerCostume PlayerCostume;
typedef struct CritterDef CritterDef;
typedef struct CritterGroup CritterGroup;
typedef struct NemesisTeamStruct NemesisTeamStruct;
typedef struct MapState MapState;

// Called when a Nemesis is added to the Container Subscription dictionary
void nemesis_DictionaryLoadCB(Entity *pNemesisEnt);

// ----------------------------------------------------------------------------
//  Transactions
// ----------------------------------------------------------------------------

enumTransactionOutcome nemesis_tr_ChangePrimaryNemesisState(ATR_ARGS, NOCONST(Entity) *playerEnt, int newState, U32 bCountsAsDefeat);

// ----------------------------------------------------------------------------
//  Misc utilities
// ----------------------------------------------------------------------------

NemesisPowerSet* nemesis_NemesisPowerSetFromName(const char *pchName);
NemesisMinionPowerSet* nemesis_NemesisMinionPowerSetFromName(const char *pchName);
NemesisMinionCostumeSet* nemesis_NemesisMinionCostumeSetFromName(const char *pchName);
PlayerCostume* nemesis_MinionCostumeByClass(const NemesisMinionCostumeSet *pSet, const char *pchClassName);

// Grants a Nemesis Arc if the player needs one
void nemesis_RefreshNemesisArc(Entity *pEnt);

const char* nemesis_ChooseDefaultVoiceSet(Entity *pNemesisEnt);

// Set the team leader id in the mapstate
void Nemesis_RecordTeamLeader(Entity *pEntity, S32 iIndex, S32 iPartitonIdx);

// Set the team index id in the mapstate
void Nemesis_RecordTeamIndex(Entity *pEntity, S32 idx, S32 iPartitonIdx);

// Get the team leader (player ent) that has a nemesis, if bAnyTeamMember true then use any team member if leader does not have nemesis, this ent is then stored in mapstate
Entity *Nemesis_TeamGetTeamLeader(Entity *pEntity, bool bAnyTeamMember);

// Get the ent player at this team index. Is recorded into mapstate so if team changes this player is still used at this index
Entity *Nemesis_TeamGetTeamIndex(Entity *pEntity, S32 iIndex);

// Nemesis get info from map state

S32 Nemesis_GetLeaderIndex(MapState *pState);
Entity *Nemesis_TeamGetPlayerEntAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader);
CritterDef *Nemesis_GetCritterDefAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader);
CritterGroup *Nemesis_GetCritterGroupAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader);
const char *Nemesis_GetCostumeAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader);
const NemesisTeamStruct *Nemesis_GetTeamStructAtIndex(S32 iPartitionIdx, S32 iIndex, bool bUseLeader);

// ----------------------------------------------------------------------------
//  Old Nemesis fixup code
// ----------------------------------------------------------------------------

void nemesis_ConvertFromOldVersion(Entity *pEnt);

#endif