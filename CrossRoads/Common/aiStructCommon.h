#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"
#include "Expression.h"

typedef struct AICombatRolesDef AICombatRolesDef;
typedef struct AICombatRoleTemplate AICombatRoleTemplate;

extern const char *g_pcAICombatRolesDictName;
extern const char *g_pcAIMasterMindDefDictName;
extern const char *g_pcAICivDefDictName;

extern StaticDefineInt PowerTagsEnum[];

AUTO_ENUM;
typedef enum AILogType
{
	AI_LOG_MOVEMENT,	ENAMES(movement)
	AI_LOG_FSM,			ENAMES(fsm)
	AI_LOG_EXPR_FUNC,	ENAMES(exprfunc)
	AI_LOG_COMBAT,		ENAMES(combat)
	AI_LOG_TRACE,		ENAMES(trace)
	AI_LOG_COUNT
}AILogType;
extern StaticDefineInt AILogTypeEnum[];

AUTO_ENUM;
typedef enum AIMovementLogTags
{
	AIMLT_UNTAGGED,
	AIMLT_MOVCOM,		ENAMES(MovementCompleted)
	AIMLT_CURMOV,		ENAMES(CurrentlyMoving)
	AIMLT_COUNT,
} AIMovementLogTags;

AUTO_ENUM;
typedef enum AIFSMLogTags
{
	AIFLT_UNTAGGED,
	AIFLT_COUNT,
} AIFSMLogTags;

AUTO_ENUM;
typedef enum AIExprLogTags
{
	AIELT_UNTAGGED,
	AIELT_COUNT,
} AIExprLogTags;

AUTO_ENUM;
typedef enum AICombatLogTags
{
	AICLT_UNTAGGED,
	AICLT_LEGALTARGET,
	AICLT_ATTACKTARGET,
	AICLT_COUNT,
} AICombatLogTags;

AUTO_ENUM;
typedef enum AITraceLogTags
{
	AITLT_COUNT,
} AITraceLogTags;

// APCDefs are used by other systems to pass in values to the AI
AUTO_STRUCT;
typedef struct AIPowerConfigDef{
	const char **inheritData;	AST(ADDNAMES("InheritData:"), SIMPLE_INHERITANCE)

	const char* name;			AST(STRUCTPARAM KEY)
	F32 absWeight;				AST(ADDNAMES("Weight:") DEFAULT(1))
	Expression* weightModifier;	AST(NAME("WeightModifier"), REDUNDANT_STRUCT("WeightModifier:", parse_Expression_StructParam), LATEBIND)
	F32 minDist;				AST(NAME(MinDist) ADDNAMES("MinDist:"))
	F32 maxDist;				AST(NAME(MaxDist) ADDNAMES("MaxDist:"))
	const char* chainTarget;	AST(ADDNAMES("ChainTarget:"), POOL_STRING)
	F32 chainTime;				AST(ADDNAMES("ChainTime:"))

	Expression* aiRequires;		AST(NAME("AIRequires"), REDUNDANT_STRUCT("AIRequires:", parse_Expression_StructParam), LATEBIND)
	Expression* aiEndCondition;	AST(NAME("AIEndCondition"), REDUNDANT_STRUCT("AIEndCondition:", parse_Expression_StructParam), LATEBIND)
	Expression* chainRequires;	AST(NAME("ChainRequires"), REDUNDANT_STRUCT("ChainRequires:", parse_Expression_StructParam), LATEBIND)
	Expression* targetOverride;	AST(NAME("TargetOverride"), REDUNDANT_STRUCT("TargetOverride:", parse_Expression_StructParam), LATEBIND)
	Expression* cureRequires;	AST(NAME("CureRequires"), REDUNDANT_STRUCT("CureRequires:", parse_Expression_StructParam), LATEBIND)
	S32* curePowerTags;			AST(ADDNAMES("CureTags"), SUBTABLE(PowerTagsEnum))

	F32 maxRandomQueueTime;		AST(ADDNAMES("MaxRandomQueueTime:"))

	bool bChainLocksFacing;		AST(NAME("ChainLocksFacing"))
	bool bChainLocksMovement;

	const char* filename;		AST(CURRENTFILE)

	U32 usedFields[2];			AST(USEDFIELD)
}AIPowerConfigDef;

extern ParseTable parse_AIPowerConfigDef[];
#define TYPE_parse_AIPowerConfigDef AIPowerConfigDef

typedef struct AIInitParams
{
	const char*		fsmOverride;
	F32				fSpawnLockdownTime;

	AICombatRolesDef*		pCombatRoleDef;
	const char*				pchCombatRoleName;
	
} AIInitParams;


AUTO_STRUCT;
typedef struct AICivClientMapDefInfo
{
	const char **eapcLegDefNames;	AST(POOL_STRING)
} AICivClientMapDefInfo;
extern ParseTable parse_AICivClientMapDefInfo[];
#define TYPE_parse_AICivClientMapDefInfo AICivClientMapDefInfo

AUTO_STRUCT;
typedef struct AICivRegenOptions
{
	Vec3	vRegenPos;
	F32		fAreaRegen;
	S32		bVolumeLegsOnly;
	S32		bSkipLegSplit;
	S32		bPostPopulate;

} AICivRegenOptions;
extern ParseTable parse_AICivRegenOptions[];
#define TYPE_parse_AICivRegenOptions AICivRegenOptions

AUTO_STRUCT;
typedef struct AICivProblemLocation
{
	Vec3		vPos;
	const char *pchReason;		AST(POOL_STRING)
	S32			bError;			// otherwise a warning
	F32			fProblemAreaSize;	
} AICivProblemLocation;

AUTO_STRUCT;
typedef struct AICivRegenReport
{
	S32						totalLegsCreated;
	S32						problemAreaRequest;
	AICivProblemLocation	**eaProblemLocs;
} AICivRegenReport;
extern ParseTable parse_AICivRegenReport[];
#define TYPE_parse_AICivRegenReport AICivRegenReport

void aiRequestEditingData(void);
int aiPowerConfigDefGenerateExprs(AIPowerConfigDef *pcd);
