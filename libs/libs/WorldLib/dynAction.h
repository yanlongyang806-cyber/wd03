#pragma once
GCC_SYSTEM

#include "referencesystem.h"

#include "dynSequencer.h"

#include "dynFxInfo.h"
#include "dynBitField.h"
#include "dynMove.h"

/******************

A DynActionMove is a DynMove along with probability and other, Action-specific info

A DynAction is a collection of DynActionMove's, how they interact, and a set of sequencer bits that determine when this action is played

******************/

typedef struct DynMove DynMove;
typedef struct DynNode DynNode;
typedef struct DynAction DynAction;
typedef struct SkelInfo SkelInfo;
typedef struct DynMoveSeq DynMoveSeq;
typedef struct DynSeqData DynSeqData;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynAnimFXMessage
{
	const char* pcMessage; AST(STRUCTPARAM POOL_STRING)
	int iFrame; AST(STRUCTPARAM)
} DynAnimFXMessage;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynActTriggerImpact
{
	Vec3 vDirection;
	const char* pcBone;			AST(POOL_STRING)
	int iFrame;
	Vec3 vOffset;
	int iLineNum;				AST(LINENUM)
} DynActTriggerImpact;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynActCallFx
{
	const char*					pcFx;				AST(STRUCTPARAM REQUIRED POOL_STRING)
	int							iFrame;
	int iLineNum;									AST(LINENUM)
} DynActCallFx;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynActionMove
{
	CONST_REF_TO(DynMove)		hMove;				AST(STRUCTPARAM REQUIRED NON_NULL_REF)
	F32							fChance;			AST(DEFAULT(1.0f))
	DynLogicBlock				logic;				AST(NAME(UseIf))
	DynBitFieldStatic			setBits;
	DynAnimFXMessage**			eaFXMessages;		AST(NAME(FXMessage))
	DynActCallFx**				eaCallFx;
	DynActTriggerImpact**		eaImpact;
	bool						bRequiresBits;		NO_AST
	bool						bVerified;			NO_AST
} DynActionMove;								

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynActIfBlock
{
	DynLogicBlock				logic;				AST(EMBEDDED_FLAT)
	const char*					pcNextAction;		AST(POOL_STRING)
	const DynAction*			pNextAction;		NO_AST
	bool						bEndSequence;		AST(BOOLFLAG)
	DynBitFieldStatic			setBits;
} DynActIfBlock;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynActFirstIfBlock
{
	DynLogicBlock				logic;				AST(EMBEDDED_FLAT)
	DynAction*					pAction;			NO_AST
	F32							fPriority;
} DynActFirstIfBlock;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynNextActionChance
{
	const char* pcNextAction;		AST(STRUCTPARAM POOL_STRING)
	F32 fChance;					AST(STRUCTPARAM)
	const DynAction* pNextAction;	NO_AST
} DynNextActionChance;




AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynAction
{
	const char*					pcName;				AST(POOL_STRING STRUCTPARAM)
	DynActionMove**				eaDynActionMoves;	AST(NAME("DynMove"))
	DynActionMove**				eaLogicDynActionMoves;	NO_AST
	DynAnimInterpolation		interpBlock;		AST( EMBEDDED_FLAT )
	const char*					pcNextAction;		AST(POOL_STRING)
	const DynAction*			pNextAction;		NO_AST
	DynNextActionChance**		eaNextActionChance; 
	const char*					pcFileName;			AST(CURRENTFILE)
	bool						bVerified;			NO_AST
	const char**				eaInterruptibleBy;	AST(POOL_STRING)
	const char**				eaSuppress;			AST(POOL_STRING)
	DynActIfBlock**				eaIf;
	const DynSeqData*			pParentSeq;			NO_AST
	bool						bCycle;				AST(BOOLFLAG)
	bool						bOverrideSeqs;		AST(BOOLFLAG NAME(OverrideSeqs) NAME(OverrideLegs))
	bool						bOverrideAll;		AST(BOOLFLAG)
	bool						bSnapOverrideAll;	AST(BOOLFLAG)
	bool						bForceLowerBody;	AST(BOOLFLAG)
	bool						bPitchToTarget;		AST(BOOLFLAG)
	bool						bDefaultFirst;		AST(BOOLFLAG)
	bool						bNoSelfInterp;		AST(BOOLFLAG)
	bool						bForceEndFreeze;	AST(BOOLFLAG)
	bool						bAllowRandomRepeats;	AST(BOOLFLAG)
	bool						bSeqRestartAllowed; AST(BOOLFLAG)
	bool						bLoopUntilInterrupted; AST(BOOLFLAG)
	bool						bForceVisible;		AST(BOOLFLAG)
	DynActFirstIfBlock**		eaFirstIf;
	DynActCallFx**				eaCallFx;
	DynActTriggerImpact**		eaImpact;
	DynBitFieldStatic			setBits;
} DynAction;							

void dynPreloadActionInfo();

const DynMoveSeq* dynMoveSeqFromAction(SA_PARAM_NN_VALID const DynAction* pAction, SA_PARAM_NN_VALID const SkelInfo* pSkelInfo, int iOldActionMoveIndex, SA_PARAM_NN_VALID U8* puiActionMoveIndex, U32* puiSeed, const DynBitField* pBits);
bool dynActionVerify(SA_PARAM_NN_VALID DynAction* pDynAction, SA_PARAM_NN_VALID DynSeqData* pSeqData);
bool dynActionLookupNextActions(SA_PARAM_NN_VALID DynAction* pAction, SA_PARAM_NN_VALID const DynAction*** ppActionList);
void dynActionProcessFxAndImpactCalls(SA_PARAM_NN_VALID DynSkeleton* pSkeleton, SA_PARAM_NN_VALID const DynAction* pAction, U32 uiActionMoveIndex, SA_PARAM_NN_VALID DynFxManager* pFxManager, F32 fStartTime, F32 fEndTime);
void dynActionSetBits(SA_PARAM_NN_VALID const DynAction* pAction, U32 uiActionMoveIndex, SA_PARAM_NN_VALID DynBitField* pBF, SA_PARAM_NN_VALID DynSequencer* pSqr);

__forceinline static bool dynActionIsInterruptibleBy(const DynAction* pAction, const char* pcInterrupter)
{
	FOR_EACH_IN_EARRAY(pAction->eaInterruptibleBy, const char, pcIRQ)
		if (pcInterrupter == pcIRQ)
			return true;
		if (pcIRQ[0] == '[' && dynIRQGroupContainsBit(pcIRQ, pcInterrupter))
			return true; // Is an irq group, so look it up accordingly
	FOR_EACH_END
	return false;
}