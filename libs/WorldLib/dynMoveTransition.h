#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynAnimChart.h"
#include "dynMove.h"

#define MOVE_TRANSITION_EDITED_DICTIONARY "MoveTransition"

typedef struct DynAnimChartLoadTime DynAnimChartLoadTime;
typedef struct DynMove DynMove;

extern DictionaryHandle hMoveTransitionDict;

AUTO_STRUCT;
typedef struct DynMoveTransition{
	//file defs
	const char* pcName; AST(POOL_STRING KEY)
	const char* pcFilename; AST(POOL_STRING CURRENTFILE)
	const char* pcComments;	AST(SERVER_ONLY)
	const char* pcScope;	AST(POOL_STRING SERVER_ONLY)

	//link data
	REF_TO(DynAnimChartLoadTime) hChart; AST(NAME(Chart))

	//set when the transition should occur even if the movement stays the same (jump-to-jump) and causes it to be uninterruptable
	bool bForced; AST(BOOLFLAG)

	//interpolation data
	bool bBlendLowerBodyFromGraph; AST(BOOLFLAG)
	bool bBlendWholeBodyFromGraph; AST(BOOLFLAG)
	DynAnimInterpolation interpBlockPre;
	DynAnimInterpolation interpBlockPost;
	bool bBlendWholeBodyToGraph; AST(BOOLFLAG)
	bool bBlendLowerBodyToGraph; AST(BOOLFLAG)

	//transition data
	DynAnimTimedStance **eaTimedStancesSource;		AST(POOL_STRING NAME(TimedStanceWordSource))
	DynAnimTimedStance **eaTimedStancesTarget;		AST(POOL_STRING NAME(TimedStanceWordTarget))
	DynAnimTimedStance **eaAllTimedStancesSorted;	AST(POOL_STRING NO_TEXT_SAVE)
	const char** eaStanceWordsSource;		AST(POOL_STRING NAME(StanceWordSource))
	const char** eaStanceWordsTarget;		AST(POOL_STRING NAME(StanceWordTarget))
	const char** eaAllStanceWordsSorted;	AST(POOL_STRING NO_TEXT_SAVE)
	const char** eaJointStanceWordsSorted;	AST(POOL_STRING NO_TEXT_SAVE)
	const char** eaMovementTypesSource;		AST(POOL_STRING NAME(MovementTypeSource))
	const char** eaMovementTypesTarget;		AST(POOL_STRING NAME(MovementTypeTarget))
	REF_TO(DynMove) hMove; AST(NAME(Move))

	U32 uiReportCount; NO_AST
}
DynMoveTransition;
extern ParseTable parse_DynMoveTransition[];
#define TYPE_parse_DynMoveTransition DynMoveTransition


bool dynMoveTransitionVerify(DynMoveTransition* pMoveTransition);
void dynMoveTransitionLoadAll(void);

int dynMoveTransitionGetSearchStringCount(const DynMoveTransition *pMoveTransition, const char *pcSearchText);