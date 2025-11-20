/***************************************************************************
*     Copyright (c) 2003-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSLCRITTER_H
#define GSLCRITTER_H

typedef struct OldEncounter OldEncounter;
typedef struct OldActor OldActor;
typedef struct OldActorInfo OldActorInfo;
typedef struct GameEncounter GameEncounter;
typedef struct AITeam AITeam;
typedef struct CritterDef CritterDef;
typedef struct CritterOverrideDef CritterOverrideDef;
typedef struct Entity Entity;
typedef struct AIPowerConfig AIPowerConfig;
typedef struct ContactDef ContactDef;
typedef struct RewardTable RewardTable;
typedef struct Entity Entity;
typedef struct AICombatRolesDef AICombatRolesDef;
typedef struct WorldInteractionNode WorldInteractionNode;
typedef struct NemesisMinionCostumeSet NemesisMinionCostumeSet;

#include "entCritter.h"

typedef struct CritterCreateParams
{
	CritterOverrideDef *pOverrideDef;
	int enttype;
	int iPartitionIdx;
	U32 uiPartition_dbg ;
	const char *fsmOverride;
	OldEncounter *pEncounter;
	OldActor *pActor;
	GameEncounter *pEncounter2;
	int iActorIndex;
	int iLevel;
	int iBaseLevel;
	int iTeamSize;
	const char *pcSubRank;
	CritterFaction * pFaction;
	Message *pCritterGroupDisplayNameMsg;
	Message *pDisplayNameMsg;
	Message *pDisplaySubNameMsg;
	const char *pchSpawnAnim;
	F32 fSpawnTime;
	Entity* spawningPlayer;
	EntityRef erOwner;
	EntityRef erCreator;
	AITeam *aiTeam;
	AITeam *aiCombatTeam;
	PlayerCostume *pCostume;
	int iCostumeKey;
	int bPlaceNumericsOnOwner;
	AICombatRolesDef *pCombatRolesDef;
	const char *pcCombatRoleName;
	WorldInteractionNode* pCreatorNode;
	F32 fHue;
	F32 fRandom;
	bool bFakeEntity;
	NemesisMinionCostumeSet *pCostumeSet;		// Costume set for this minion type passed along in order to find a costume
	bool bPowersEntCreated;
} CritterCreateParams;


// Creates a critter from a given def
Entity* critter_CreateByDef( SA_PARAM_NN_VALID CritterDef * def, SA_PARAM_NN_VALID CritterCreateParams *pCreateParams, SA_PARAM_OP_VALID const char* blameFile, bool bImmediateCombatUpdate);

Entity * critter_Create( const char * name, const char *overridename, int enttype, int iPartitionIdx, const char* fsmOverride, 
						int iLevel, int iTeamSize, const char *pcSubrank, CritterFaction *pFaction, 
						Message * pCritterGroupDisplayNameMsg, Message * pDisplayNameMsg, Message * pDisplaySubNameMsg, char * pchSpawn, F32 fSpawnTime, 
						AITeam* aiTeam, WorldInteractionNode* pCreatorNode);
// Looks for critter matching name and creates entity

//creates a critter with a specified costume
Entity * critter_CreateWithCostume(const char * name, const char * overridename, int enttype, int iPartitionIdx,
								   const char* fsmOverride, int iLevel, int iTeamSize, const char *pcSubrank, 
								   CritterFaction *pFaction, Message * pCritterGroupDisplayNameMsg, Message * pDisplayNameMsg, Message * pDisplaySubNameMsg, char *pchSpawnAnim, 
								   F32 fSpawnTime, PlayerCostume* pCostume );

Entity* critter_FindAndCreate( CritterGroup *pGroup, const char *pcRank, OldEncounter *pEncounter, 
							  OldActor *pActor, GameEncounter *pEncounter2, int iActorIndex, int iLevel, 
							  int iTeamSize, const char *pcSubRank, int enttype, int iPartitionIdx, const char* fsmOverride, 
							  CritterFaction * pFaction, Message * pCritterGroupDisplayNameMsg, Message * pDisplayNameMsg, Message * pDisplaySubNameMsg, CritterDef*** excludeDefs, 
							  const char * pchSpawn, F32 fSpawnTime, Entity* spawningPlayer, 
							  AITeam* aiTeam, PlayerCostume* pCostume, NemesisMinionCostumeSet *pNemMinionSet);
// Searches for a matching critter and created entity

// Spawn a Nemesis Minion
Entity* critter_CreateNemesisMinion(Entity *pNemesisEnt, const char *pcRank, const char *pchFSM, OldEncounter *pEnc, OldActor *pActor, GameEncounter *pEnc2, int iActorIndex, int iLevel, int iTeamSize, const char *pcSubRank, int iPartitionIdx, CritterFaction* pFaction, Message* pDisplayNameMsg, CritterDef*** excludeDefs, const char * pchSpawnAnim, F32 fSpawnTime, Entity* spawningPlayer, AITeam* aiTeam);

void critter_AddNewCritterItems(CritterDef* def, Entity* e, int iLevel);

int critter_CritterDefPostTextRead(CritterDef *def);

void critter_OverrideDisplayMessage( Entity *be, const Message *displayNameMsgOverride, const Message *displaySubNameMsgOverride);

const char* critter_GetEncounterName(Critter *pCritter);
const char* critter_GetActorName(Critter* pCritter);


//Nemesis
CritterDef* critter_GetNemesisCritter(Entity *pNemesisEnt);
CritterDef* critter_GetNemesisCritterAndSetParams(int iPartitionIdx, Entity *pNemesisEnt, CritterCreateParams *createParams, bool bUseLeader, S32 iTeamIndex);
void critter_SetupNemesisEntity(Entity* pEnt, Entity *pNemesisEnt, bool bOverrideDisplayName, S32 iPartitionIdx, S32 iTeamIndex, bool bUseTeamLeader);

//Nemesis Minions
CritterGroup* critter_GetNemesisMinionGroup(Entity *pNemesisEnt);
CritterGroup* critter_GetNemesisMinionGroupAndSetParams(int iPartitionIdx, Entity *pNemesisEnt, CritterCreateParams *createParams, bool bUseLeader, S32 iTeamIndex);
void critter_SetupNemesisMinionEntity(Entity* pEnt, Entity *pNemesisEnt);
//Sets up the critter encounter expression
void critter_SetupEncounterExprContext(ExprContext *pContext, Entity *pEnt, int iPartitionIdx, int iTeamSize, F32 fRandom, OldEncounter *pEncounter, GameEncounter *pGameEncounter);

//Gets the critter encounter expression context
ExprContext *critter_GetEncounterExprContext();


#endif

#include "AutoGen/entCritter_h_ast.h"