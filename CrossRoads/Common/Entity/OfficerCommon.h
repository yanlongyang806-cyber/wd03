#ifndef OFFICERCOMMON_H
#define OFFICERCOMMON_H
GCC_SYSTEM

#include "message.h"

typedef struct AllegianceDef		AllegianceDef;
typedef struct Entity				Entity;
typedef struct NOCONST(Entity)		NOCONST(Entity);
typedef struct GameAccountDataExtract GameAccountDataExtract;
typedef struct PetDef				PetDef;
typedef struct PTNodeDef			PTNodeDef;

#define DEFAULT_OFFICER_GRADE_COUNT 10
#define DEFAULT_OFFICER_RANK_NUMERIC "StarfleetRank"

AUTO_ENUM;
typedef enum OfficerActionReturnValue
{
	OfficerActionReturnValue_Success,
	OfficerActionReturnValue_InvalidOfficer,
	OfficerActionReturnValue_InvalidRank,
	OfficerActionReturnValue_InvalidAction,
	OfficerActionReturnValue_InvalidNode,
	OfficerActionReturnValue_SameNode,
	OfficerActionReturnValue_OnAwayTeam,
	OfficerActionReturnValue_MismatchedNodePurposes,
	OfficerActionReturnValue_NotEnoughPoints,
	OfficerActionReturnValue_CannotPayCost,
	OfficerActionReturnValue_ExceededMaxActions,
	OfficerActionReturnValue_ActionAlreadyQueued,
	OfficerActionReturnValue_InCombat,
	OfficerActionReturnValue_BeingTraded,
} OfficerActionReturnValue;

AUTO_STRUCT;
typedef struct OfficerRankDef
{
	char*			pchName;					AST(STRUCTPARAM POOL_STRING)
	DisplayMessage*	pDisplayMessage;			AST(NAME(DisplayName))		
	DisplayMessage* pDisplayMsgShort;			AST(NAME(DisplayNameShort))
	DisplayMessage* pDisplayMsgAlt;				AST(NAME(DisplayNameAlt))
	REF_TO(AllegianceDef) hAllegiance;			AST(NAME(Allegiance) REFDICT(Allegiance))
	char*			pchRankNumeric;				AST(NAME(RankNumeric) POOL_STRING)
	const char*		pchPointsSpentNumeric;		AST(NAME(PointsSpentNumeric) DEFAULT("OfficerSkillPointSpent") POOL_STRING)
	S32				iRequiredPointsSpent;		AST(NAME(RequiredPointsSpent,RequiredPointsSpentToPromote))
	const char*		pchPointsSpentPlayerNumeric;AST(NAME(PointsSpentPlayerNumeric) DEFAULT("SkillpointSpent") POOL_STRING)
	S32				iRequiredPointsSpentPlayer;	AST(NAME(RequiredPointsSpentPlayer,RequiredPointsSpentToPromotePlayer))
	char*			pchCostNumeric;				AST(NAME(CostNumeric,PromotionCostNumeric) POOL_STRING)
	S32				iCost;						AST(NAME(Cost,PromotionCost))
	char*			pchTrainingNumeric;			AST(NAME(TrainingNumeric) POOL_STRING)
	S32				iTrainingCost;				AST(NAME(TrainingCost))
	U32				uiTrainingTime;				AST(NAME(TrainingTime))
	F32				fTrainingRefundPercent;		AST(NAME(TrainingRefundPercent) DEFAULT(1))
	S32				iBaseAllowedPets;			AST(NAME(BaseAllowedPets))
	S32				iExtraAllowedPets;			AST(NAME(ExtraAllowedPets))
	S32				iExtraAllowedPuppets;		AST(NAME(ExtraPuppets))
	S32				iGradeCount;				AST(NAME(GradeCount) DEFAULT(DEFAULT_OFFICER_GRADE_COUNT))
	bool			bPlayerOnly;				AST(NAME(PlayerOnly,PlayerAccessibleOnly))
} OfficerRankDef;

AUTO_STRUCT;
typedef struct OfficerRankStruct
{
	OfficerRankDef** eaRanks; AST(NAME(OfficerRankDef))
} OfficerRankStruct;

extern ParseTable parse_OfficerRankStruct[];
#define TYPE_parse_OfficerRankStruct OfficerRankStruct
extern OfficerRankStruct g_OfficerRankStruct;

bool			Officer_ValidateSkills(PetDef* pDef);
OfficerRankDef* Officer_GetRankDefUsingNumeric(S32 iRank, AllegianceDef *pAllegiance, AllegianceDef *pSubAllegiance, const char* pchNumeric);
#define			Officer_GetRankDef(iRank, pAllegiance, pSubAllegiance) Officer_GetRankDefUsingNumeric(iRank, pAllegiance, pSubAllegiance, DEFAULT_OFFICER_RANK_NUMERIC)
OfficerRankDef* Officer_GetRankDefFromNodeUsingNumeric(PTNodeDef* pNodeDef, AllegianceDef* pAllegiance, AllegianceDef *pSubAllegiance, const char* pchNumeric);
#define			Officer_GetRankDefFromNode(pNodeDef, pAllegiance, pSubAllegiance) Officer_GetRankDefFromNodeUsingNumeric(pNodeDef, pAllegiance, pSubAllegiance, DEFAULT_OFFICER_RANK_NUMERIC)
S32				Officer_GetRankCountUsingNumeric(AllegianceDef *pAllegiance, const char* pchNumeric);
#define			Officer_GetRankCount(pAllegiance) Officer_GetRankCountUsingNumeric(pAllegiance, DEFAULT_OFFICER_RANK_NUMERIC)
void			Officer_GetActionMessageKeyFromReturnValue( char** ppchMsg, OfficerActionReturnValue eReturnValue, const char* pchType );
bool			Officer_CanPromote(SA_PARAM_NN_VALID Entity* pEnt, U32 uiOfficerID, AllegianceDef *pAllegiance);
S32				Officer_GetRequiredPointsForRankUsingNumeric(SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_OP_VALID Entity* pOfficer, S32 iRank, const char* pchNumeric, bool bScale);
#define			Officer_GetRequiredPointsForRank(pPlayerEnt, pOfficer, iRank, bScale) Officer_GetRequiredPointsForRankUsingNumeric(pPlayerEnt, pOfficer, iRank, DEFAULT_OFFICER_RANK_NUMERIC, bScale)
S32				Officer_GetPromoteReturnValue(SA_PARAM_NN_VALID Entity* pEnt, U32 uiOfficerID, AllegianceDef *pAllegiance);
bool			Officer_CanTrain(int iPartitionIdx, SA_PARAM_NN_VALID Entity* pBuyer, SA_PARAM_NN_VALID Entity* pTrainer, U32 uiOfficerID, const char* pchOldNode, const char* pchNewNode, bool bCheckCost, bool bCheckNode, AllegianceDef *pAllegiance);
S32				Officer_GetTrainReturnValue(int iPartitionIdx, SA_PARAM_NN_VALID Entity* pBuyer, SA_PARAM_NN_VALID Entity* pTrainer, U32 uiOfficerID, const char* pchOldNode, const char* pchNewNode, bool bCheckCost, bool bCheckNode, AllegianceDef *pAllegiance);

S32				trhOfficer_GetMaxAllowedPets(ATH_ARG NOCONST(Entity)* pEntity, AllegianceDef *pAllegiance, GameAccountDataExtract *pExtract);
#define			Officer_GetMaxAllowedPets(pEntity, pAllegiance, pExtract) trhOfficer_GetMaxAllowedPets(CONTAINER_NOCONST(Entity, (pEntity)),pAllegiance,pExtract)

bool			trhOfficer_CanAddOfficer(ATH_ARG NOCONST(Entity) *pEntity, AllegianceDef *pPetAllegiance, GameAccountDataExtract *pExtract);
#define			Officer_CanAddOfficer(pEntity, pPetAllegiance, pExtract) trhOfficer_CanAddOfficer(CONTAINER_NOCONST(Entity, (pEntity)), pPetAllegiance, pExtract)

S32				trhOfficer_GetExtraPuppets(ATH_ARG NOCONST(Entity) *pEntity, GameAccountDataExtract *pExtract);
#define			Officer_GetExtraPuppets(pEntity, pExtract) trhOfficer_GetExtraPuppets(CONTAINER_NOCONST(Entity, (pEntity)), pExtract)

bool			Officer_GetRankAndGradeFromLevel(S32 iLevel, AllegianceDef* pAllegiance, S32* piRank, S32* piGrade);
S32				Officer_GetGrade(Entity* pOfficer, S32 iOverrideLevel, S32* piLevel, S32* piRank);
S32				Officer_GetGradeFromLevelAndRank(AllegianceDef* pAllegiance, S32 iLevel, S32 iRank);
S32				Officer_GetLevelFromRankAndGrade(AllegianceDef* pAllegiance, S32 iRank, S32 iGrade);
S32				trhOfficer_GetRankUsingNumeric(ATH_ARG NOCONST(Entity)* pOfficer, const char* pchNumeric);
#define			Officer_GetRankUsingNumeric(pOfficer, pchNumeric) trhOfficer_GetRankUsingNumeric(CONTAINER_NOCONST(Entity, (pOfficer)), pchNumeric)
#define			Officer_GetRank(pOfficer) trhOfficer_GetRankUsingNumeric(CONTAINER_NOCONST(Entity, (pOfficer)), DEFAULT_OFFICER_RANK_NUMERIC)

#endif
