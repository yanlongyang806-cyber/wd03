
typedef struct GenericLogReceiver GenericLogReceiver;

typedef struct DynAnimOverrideUpdater
{
	const DynAnimOverride* pOverride;
	F32 fCurrentFrame;
	F32 fBlendValue;
	bool bActive;
	const DynMoveSeq* pDynMoveSeq;
} DynAnimOverrideUpdater;

typedef struct DynActionBlock
{
	F32							fFrameTime;
	const DynAction*			pAction;
	const DynMoveSeq*			pMoveSeq;
	const char*					pcMoveSeq;
	F32							fInterpParam;
	F32							fInterpFrameTimeOffset;
	bool						bInterpolates;
	U8							uiActionMoveIndex;
	bool						bPrevActionDone;
	F32							fPlaybackSpeed;
	bool						bStoppedTP;
} DynActionBlock;

typedef struct DynSeqBlock
{
	const DynSeqData*			pSeq;
	bool						bAdvancedTo;
	bool						bDefaultSeq;
} DynSeqBlock;


#define MAX_PREV_ACTIONS 4
#define MAX_ANIM_OVERRIDES 4

typedef struct DynSequencer
{
	DynBitField					bits;
	DynActionBlock				currAction;
	DynActionBlock				prevActions[MAX_PREV_ACTIONS];
	DynActionBlock				nextAction;
	DynSeqBlock					currSeq;
	DynSeqBlock					nextSeq;
	char*						esMMLog;
	REF_TO(const SkelInfo)		hSkelInfo;	
	const char*					pcSequencerName;
	DynAnimOverrideUpdater		animOverrideUpdater[MAX_ANIM_OVERRIDES];
	F32							fInterpParam;
	F32							fOverrideTagFactor;
	F32							fOverlayBlend;
	int							iNumPrevActions;
	U32							uiRequiredLOD;
	U32							uiSeed;
	U8							uiNumAnimOverrides;
	GenericLogReceiver*			glr;

	U32							bNextInterrupts		:1;
	U32							bReset				:1;
	U32							bRunSinceReset		:1;
	U32							bNeverOverride		:1;
	U32							bOverlay			:1;
} DynSequencer;