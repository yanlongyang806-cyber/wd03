#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynSkeleton.h"

#define ANIM_CHART_EDITED_DICTIONARY "AnimChart"
#define ANIM_CHART_DEFAULT_KEY "Default"
#define STANCE_DICTIONARY "Stance"

typedef struct DynAnimGraph DynAnimGraph;
typedef struct DynAnimChartRunTime DynAnimChartRunTime;
typedef struct DynAnimChartLoadTime DynAnimChartLoadTime;
typedef struct DynMove DynMove;
typedef struct DynMoveTransition DynMoveTransition;
typedef struct DynMovementSet DynMovementSet;
typedef struct DynMovementSequence DynMovementSequence;
typedef struct DynMoveSeq DynMoveSeq;
typedef struct SkelInfo SkelInfo;
typedef struct DynSkeleton DynSkeleton;

extern DictionaryHandle hAnimChartDictLoadTime;
extern DictionaryHandle hAnimChartDictRunTime;
extern DictionaryHandle hStanceDict;

AUTO_STRUCT AST_ENDTOK("\n");
typedef struct DynAnimStanceData
{
	F32 fStancePriority;				AST(STRUCTPARAM)
	const char *pcName;					AST(STRUCTPARAM POOL_STRING KEY)
	const char *pcTag;					AST(STRUCTPARAM POOL_STRING)
	SkelBoneVisibilitySet eVisSet;		AST(STRUCTPARAM NAME("BoneVisSet") SUBTABLE(SkelBoneVisibilitySetEnum) DEFAULT(-1))
	const char *pcBankingNodeOverride;	AST(STRUCTPARAM POOL_STRING)
	const char *pcFileName;				AST(CURRENTFILE)
}
DynAnimStanceData;
extern ParseTable parse_DynAnimStanceData[];
#define TYPE_parse_DynAnimStanceData DynAnimStanceData

AUTO_STRUCT;
typedef struct DynAnimStancesList
{
	DynAnimStanceData**	eaStances;			AST(NAME(Stance))
	DynAnimStanceData** eaMovementStances;	NO_AST
	StashTable stStances;					NO_AST
}
DynAnimStancesList;

extern DynAnimStancesList stance_list;

AUTO_STRUCT;
typedef struct DynAnimTimedStance
{
	const char *pcName; AST(POOL_STRING)
	F32 fTime;
}
DynAnimTimedStance;
extern ParseTable parse_DynAnimTimedStance[];
#define TYPE_parse_DynAnimTimedStance DynAnimTimedStance

AUTO_STRUCT;
typedef struct DynAnimGraphChanceRef
{
	REF_TO(DynAnimGraph) hGraph; AST(NAME(graph) NON_NULL_REF)
	F32 fChance;
}
DynAnimGraphChanceRef;
extern ParseTable parse_DynAnimGraphChanceRef[];
#define TYPE_parse_DynAnimGraphChanceRef DynAnimGraphChanceRef

AUTO_STRUCT;
typedef struct DynAnimChartGraphRefRunTime
{
	const char* pcKeyword; AST(POOL_STRING)
	REF_TO(DynAnimGraph) hGraph; AST(NAME(graph))
	DynAnimGraphChanceRef **eaGraphChances;
	bool bBlank; AST(BOOLFLAG)
}
DynAnimChartGraphRefRunTime;

AUTO_STRUCT;
typedef struct DynAnimChartGraphRefLoadTime
{
	const char* pcKeyword; AST(POOL_STRING)
	REF_TO(DynAnimGraph) hGraph; AST(NAME(graph) NON_NULL_REF)
	DynAnimGraphChanceRef **eaGraphChances;
	const char** eaStanceWords; AST(NAME(StanceWord) POOL_STRING)
	bool bBlank; AST(BOOLFLAG)
}
DynAnimChartGraphRefLoadTime;
extern ParseTable parse_DynAnimChartGraphRefLoadTime[];
#define TYPE_parse_DynAnimChartGraphRefLoadTime DynAnimChartGraphRefLoadTime

AUTO_STRUCT;
typedef struct DynAnimMoveChanceRef
{
	REF_TO(DynMove) hMove; AST(NAME(Move) NON_NULL_REF)
	F32 fChance;
}
DynAnimMoveChanceRef;
extern ParseTable parse_DynAnimMoveChanceRef[];
#define TYPE_parse_DynAnimMoveChanceRef DynAnimMoveChanceRef

AUTO_STRUCT;
typedef struct DynAnimChartMoveRefRunTime
{
	const char* pcMovementType; AST(POOL_STRING NAME(MovementType))
	REF_TO(DynMove) hMove; AST(NAME(Move))
	DynAnimMoveChanceRef **eaMoveChances;
	bool bBlank; AST(BOOLFLAG)
}
DynAnimChartMoveRefRunTime;

AUTO_STRUCT;
typedef struct DynAnimChartMoveRefLoadTime
{
	const char* pcMovementType; AST(POOL_STRING NAME(MovementType))
	REF_TO(DynMove) hMove; AST(NAME(Move))
	DynAnimMoveChanceRef **eaMoveChances;
	const char** eaStanceWords; AST(NAME(StanceWord) POOL_STRING)
	const char *pcMovementStance; AST(POOL_STRING)
	bool bBlank; AST(BOOLFLAG)
}
DynAnimChartMoveRefLoadTime;
extern ParseTable parse_DynAnimChartMoveRefLoadTime[];
#define TYPE_parse_DynAnimChartMoveRefLoadTime DynAnimChartMoveRefLoadTime

AUTO_STRUCT;
typedef struct DynAnimSubChartRef
{
	REF_TO(DynAnimChartLoadTime) hSubChart; AST(NAME("ChartName") REQUIRED)
}
DynAnimSubChartRef;
extern ParseTable parse_DynAnimSubChartRef[];
#define TYPE_parse_DynAnimSubChartRef DynAnimSubChartRef

AUTO_STRUCT;
typedef struct DynAnimChartRunTime
{
	const char* pcName; AST(POOL_STRING KEY)
	const char* pcFilename; AST(POOL_STRING FILENAME) // Not necessarily accurate because of sub-charts, etc
	const char** eaStanceWords; AST(POOL_STRING)
	F32 fChartPriority;

	REF_TO(DynMovementSet) hMovementSet;
	DynAnimGraphChanceRef** eaDefaultChances;
	DynAnimGraphChanceRef** eaMoveDefaultChances;
	DynAnimChartGraphRefRunTime** eaGraphRefs;
	DynAnimChartGraphRefRunTime** eaMoveGraphRefs;
	DynAnimChartMoveRefRunTime** eaMoveRefs;
	DynMoveTransition** eaMoveTransitions;
	SkelBoneVisibilitySet eVisSet;	NO_AST
	StashTable stGraphs;  			NO_AST // this will have our regular sequencer graphs in it
	StashTable stMovementGraphs;	NO_AST // this wil  have our movement sequencer graphs in it
	StashTable stMoves; NO_AST
	StashTable stChildCharts; NO_AST
	DynAnimChartRunTime** eaMultiStanceWordChildCharts; NO_AST
	DynAnimChartRunTime** eaAllChildCharts; NO_AST
	DynMovementSequence **eaMovementSequencesSubset; NO_AST
	U32 uiNumMovementDirections; NO_AST
	U32 uiNumMovementBlanks; NO_AST
	bool bHasJumpingStance; NO_AST
	bool bHasFallingStance; NO_AST
	bool bHasRisingStance;  NO_AST
}
DynAnimChartRunTime;
extern ParseTable parse_DynAnimChartRunTime[];
#define TYPE_parse_DynAnimChartRunTime DynAnimChartRunTime

AUTO_STRUCT AST_IGNORE(Priority);
typedef struct DynAnimChartLoadTime
{
	const char* pcName; AST(POOL_STRING KEY)
	const char* pcFilename; AST(POOL_STRING CURRENTFILE)
	const char* pcComments;	AST(SERVER_ONLY)
	const char* pcScope;	AST(POOL_STRING SERVER_ONLY)
	const char* pcFileType; AST(POOL_STRING)
	const char** eaStanceWords; AST(NAME(StanceWord) POOL_STRING) // Stance words applied to all
	const char** eaValidStances; AST(NAME(ValidStance) POOL_STRING) // All the stance world combos allowed in any of the graph or move refs (Null string is always valid)
	const char** eaValidKeywords; AST(NAME(ValidKeyword) POOL_STRING) // All the keywords allowed in any of the graph refs
	const char** eaValidMoveKeywords; AST(NAME(ValidMoveKeyword) POOL_STRING) // All of the movement stance driven keywords allowed in any of the move graph refs

	SkelBoneVisibilitySet eVisSet; AST(DEFAULT(-1) NAME("BoneVisSet") SUBTABLE(SkelBoneVisibilitySetEnum))
	REF_TO(DynAnimChartLoadTime) hBaseChart; AST(NAME(BaseChart))
	REF_TO(DynMovementSet) hMovementSet; AST(NAME(MovementSet) NON_NULL_REF)
	DynAnimChartGraphRefLoadTime** eaGraphRefs; AST(NAME(GraphRef))
	DynAnimChartGraphRefLoadTime** eaMoveGraphRefs; AST(NAME(MoveGraphRef))
	DynAnimChartMoveRefLoadTime** eaMoveRefs; AST(NAME(MoveRef))
	DynAnimSubChartRef** eaSubCharts; AST(NAME(SubChart))
	bool bIsSubChart; AST(NAME(IsSubChart))

	U32 uiReportCount; NO_AST
}
DynAnimChartLoadTime;
extern ParseTable parse_DynAnimChartLoadTime[];
#define TYPE_parse_DynAnimChartLoadTime DynAnimChartLoadTime

typedef struct DynAnimChartStack
{
	union {
		const DynAnimChartRunTime**				eaChartStackMutable;
		const DynAnimChartRunTime*const*const	eaChartStack;
	};

	union {
		const char**			eaStanceWordsMutable;
		const char*const*const	eaStanceWords;
	};

	bool bMovement;
	const char* pcPlayingStanceKey;
	const char** eaStanceKeys;
	const char** eaRemovedStanceKeys;
	const char** eaStanceFlags;
	const char** eaRemovedStanceFlags;
	bool bStopped;

	U32 interruptingMovementStanceCount;
	U32 directionMovementStanceCount;
	bool bStackDirty;
}
DynAnimChartStack;

void dynAnimChartLoadTimeFixup(DynAnimChartLoadTime *pChart);
bool dynAnimChartLoadTimeVerify(DynAnimChartLoadTime *pChart);
bool dynAnimChartVerifyReferences(DynAnimChartLoadTime *pChart);
void dynAnimChartLoadAll(void);
void dynAnimChartMovementSetChanged(DynAnimChartLoadTime *pChart);
void dynAnimChartReloadAll(void);
const char *dynAnimKeyFromStanceWords(const char*const* eaStanceWords);
void dynAnimStanceWordsFromKey(const char *pcKey, const char ***eaStanceWords);

DynAnimChartStack* dynAnimChartStackCreate(const DynAnimChartRunTime* pBaseChart);
DynAnimChartStack* dynAnimChartStackFindByBaseChart(DynAnimChartStack** eaChartStack, const DynAnimChartRunTime* pBaseChart);
void dynAnimChartStackDestroy(DynAnimChartStack* pChartStack);

DynAnimGraph* dynAnimChartStackFindGraph(	DynAnimChartStack* pChartStack,
											const char* pcKeyword,
											bool bMovementSequencer,
											const DynAnimChartRunTime** pChartOut);

S32 dynAnimChartStackGetMoveSeq(const DynSkeleton *pSkeleton,
								DynAnimChartStack* pChartStack,
								const DynMovementSequence** pMovementSequenceInOut,
								const SkelInfo *pSkelInfo,
								const DynMoveSeq** pMoveSeqOut,
								const DynAnimChartRunTime** pChartOut,
								const DynMove *pCurMove);
void dynAnimChartStackSetStanceWord(DynAnimChartStack* pChartStack, const char* pcStance, const char** eaMovementStances, const char** eaDebugStances, U32 uiMovement);
void dynAnimChartStackClearStanceWord(DynAnimChartStack* pChartStack, const char* pcStance, const char** eaMovementStances, const char** eaDebugStances, U32 uiMovement);
void dynAnimChartStackSetFromStanceWords(DynAnimChartStack* pChartStack);

void dynAnimStancesLoadAll(void);
bool dynAnimStanceValid(const char* pcStance);
bool dynAnimMovementStanceKeyValid(const char* pcStance);
bool dynAnimMovementStanceValid(const char* pcStance);
F32 dynAnimStancePriority(const char* pcStance);
int dynAnimStanceIndex(const char* pcStance);
int dynAnimMovementStanceIndex(const char* pcStance);
int dynAnimCompareStanceWordPriority(const void **pa, const void **pb);
int dynAnimCompareStanceWordsPriorityLarge(const char **eaStanceWordsA, const char **eaStanceWordsB, U32 uiIgnoreDetails, U32 uiIgnoreMovementInA, U32 uiIgnoreMovementInB);
int dynAnimCompareStanceWordsPrioritySmall(const char **eaStanceWordsA, const char **eaStanceWordsB, U32 uiIgnoreDetails, U32 uiIgnoreMovementInA, U32 uiIgnoreMovementInB);
int dynAnimCompareTimedStancesPriority(const void **pa, const void **pb);
int dynAnimCompareTimedStancesPriorityLarge(const DynAnimTimedStance **eaTimedStancesA, const DynAnimTimedStance **eaTimedStancesB);
int dynAnimCompareTimedStancesPrioritySmall(const DynAnimTimedStance **eaTimedStancesA, const DynAnimTimedStance **eaTimedStancesB);
int dynAnimCompareJointStancesPriorityLarge(const char **eaStanceWordsA, const DynAnimTimedStance **eaTimedStancesA,
											const char **eaStanceWordsB, const DynAnimTimedStance **eaTimedStancesB);
int dynAnimCompareJointStancesPrioritySmall(const char **eaStanceWordsA, const DynAnimTimedStance **eaTimedStancesA,
											const char **eaStanceWordsB, const DynAnimTimedStance **eaTimedStancesB);
int dynAnimChartGetSearchStringCount(const DynAnimChartLoadTime *pChart, const char *pcSearchText);

