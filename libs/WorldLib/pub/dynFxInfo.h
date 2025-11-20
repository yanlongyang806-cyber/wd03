
#pragma once
GCC_SYSTEM

#include "textparser.h"
#include "referencesystem.h"
#include "dynFxManager.h"
#include "MultiVal.h"

#include "dynBitField.h"
#include "dynFxEnums.h"
#include "WorldLibEnums.h"


#define FxFileError(a, format, ...) { dynFxInfoLogBadFile(a); ErrorFilenameGroupf(a, "FX", 3, format, __VA_ARGS__); }
#define SndFxFileError(a, str, ...) dynFxInfoLogBadFile(a); ErrorFilenameGroupf(a, "FX Audio", 3, str, __VA_ARGS__)

extern StaticDefineInt ParseDynParamOp[];
extern StaticDefineInt ParseDynNodeXFormFlags[];
extern StaticDefineInt ParseDynParentFlags[];
extern ParseTable ParseDynChildCall[];
extern StaticDefineInt ParseDynLightType[];
extern StaticDefineInt ParseMessageTo[];
extern ParseTable ParseSimpleDynFxMessage[];
extern StaticDefineInt ParseDynFxDictType[];

typedef struct DynInterp DynInterp;
typedef struct DynForce DynForce;
typedef struct DynJitterList DynJitterList;
typedef struct DynFxPathSet DynFxPathSet;
typedef struct DynFxPhysicsInfo DynFxPhysicsInfo;
typedef struct DynFxFastParticleInfo DynFxFastParticleInfo;
typedef struct StashTableIterator;

typedef void (*dynFxInfoReloadCallback)(const char *relpath, int when);
typedef U32 (*dynJitterListSelectionCallback)(DynJitterList* pJList, void* pUserData);

extern DictionaryHandle hDynFxInfoDict;


// Do not change any of these without changing the way messages are stored in FX
#define DYNFX_MAX_EVENTS 8
#define DYNFX_KILL_MESSAGE_INDEX 8
#define DYNFX_MESSAGE_BITS 4
#define DYNFX_MESSAGE_MASK ((0x1 << DYNFX_MESSAGE_BITS)-1)
#define DYNFX_MAX_MESSAGES 8
STATIC_ASSERT(DYNFX_MESSAGE_BITS * DYNFX_MAX_MESSAGES <= 32)

// MAX_DYN_FX_TOKENS is determined by the size of the token bit field, currently 160
#define MAX_DYN_FX_TOKENS 160
typedef struct DynFxTokenBitField 
{
	U32 bf[5];
} DynFxTokenBitField;

AUTO_STRUCT;
typedef struct DynDefineParam
{
	const char* pcParamName; AST(STRUCTPARAM POOL_STRING)
	MultiVal mvVal; AST(STRUCTPARAM)
	int iLineNum; AST(STRUCTPARAM LINENUM)
} DynDefineParam;

AUTO_STRUCT;
typedef struct DynParamRedirect
{
	const char *pcParamSrcName; AST(STRUCTPARAM POOL_STRING)
	const char *pcParamDstName; AST(STRUCTPARAM POOL_STRING)
} DynParamRedirect;

AUTO_STRUCT;
typedef struct DynEditorParam
{
	const char* pcParamName; AST(STRUCTPARAM POOL_STRING)
	eDynFxDictType eParamType; AST(STRUCTPARAM SUBTABLE(ParseDynFxDictType), DEFAULT(eDynFxDictType_None))
	int iLineNum; AST(STRUCTPARAM LINENUM)
} DynEditorParam;

AUTO_STRUCT;
typedef struct DynApplyParam
{
	eDynParamOperator eParamOp; AST(STRUCTPARAM SUBTABLE(ParseDynParamOp) DEFAULT(DynOpType_None))
	const char* pcTokenString; AST(STRUCTPARAM POOL_STRING)
	const char* pcParamName; AST(STRUCTPARAM POOL_STRING)
	U32 uiTokenIndex; NO_AST
	eDynParamType edpt; NO_AST
	int iLineNum; AST(STRUCTPARAM LINENUM)
} DynApplyParam;

AUTO_STRUCT;
typedef struct DynListNode
{
	MultiVal mvVal; AST(STRUCTPARAM)
	F32 fChance; AST(STRUCTPARAM DEFAULT(1))
} DynListNode;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynList
{
	const char*		pcListName; AST(POOL_STRING STRUCTPARAM NAME("Name"))
	DynListNode**	eaNodes; AST(NAME("Type"))
	bool			bGlobal; AST(BOOLFLAG)
	MultiValType	mvType; NO_AST
	bool			bEqualChance; NO_AST
} DynList;



AUTO_STRUCT;
typedef struct DynJitterList
{
	const char* pcTokenString; AST(STRUCTPARAM POOL_STRING)
	const char* pcJListName; AST(STRUCTPARAM POOL_STRING)
	U32 uiTokenIndex; NO_AST
	DynList* pList; NO_AST
	eDynParamType edpt; NO_AST
	int iLineNum; AST(STRUCTPARAM LINENUM)
} DynJitterList;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynMNCRename
{
	const char* pcBefore; AST(STRUCTPARAM POOL_STRING)
	const char* pcAfter; AST(STRUCTPARAM POOL_STRING)
} DynMNCRename;


typedef struct DynObjectInfo
{
	DynObject obj;
	DynOperator** eaDynOps;
	DynApplyParam** eaParams;
	DynInterp**	eaInterps;
	DynJitterList** eaJLists;
	U32 bfParamsSpecified[5]; 
	U8* puiInterpTypes;
} DynObjectInfo;



typedef enum eDynObjectInfo
{
	edoValue = 0,
	edoRate,
	edoAmp,
	edoFreq,
	edoCycleOffset,
	edoTotal
} eDynObjectInfo;

typedef enum eMessageTo
{
	emtSelf = (1 << 0),
	emtChildren = (1 << 1),
	emtParent = (1 << 2),
	emtNear = (1 << 3),
	emtEntity = (1 << 4),
	emtSiblings = (1 << 5),
} eMessageTo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynFxMessage
{
	const char* pcMessageType; AST(STRUCTPARAM POOL_STRING)
	eMessageTo eSendTo; AST(SUBTABLE(ParseMessageTo) DEFAULT(emtSelf) FLAGS)
	F32 fDistance; AST(DEFAULT(10))
	U32 bRunTimeAlloc			:1; NO_AST
} DynFxMessage;

AUTO_STRUCT;
typedef struct DynParamBlock
{
	DynDefineParam** eaDefineParams; AST(NAME("PassParam"))
	const char** eaPassThroughParams; AST(NAME("PassThrough") POOL_STRING)
	DynParamRedirect** eaRedirectParams; AST(NAME("PassThroughAlias") POOL_STRING)
	bool bRunTimeAllocated; NO_AST
	const char *pcReason; AST(POOL_STRING)
	bool bClaimedByFX; NO_AST
} DynParamBlock;
extern ParseTable parse_DynParamBlock[];
#define TYPE_parse_DynParamBlock DynParamBlock


AUTO_STRUCT;
typedef struct DynParamConditional
{
	const char* pcParamName; AST(POOL_STRING STRUCTPARAM)
	eDynParamConditionalType condition; AST(NAME(Condition) STRUCTPARAM)
	F32 fValue; AST(STRUCTPARAM)
} DynParamConditional;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynChildCall
{
	REF_TO(DynFxInfo) hChildFx; AST(STRUCTPARAM REQUIRED)
	int iTimesToCall; AST(STRUCTPARAM DEFAULT(1))
	int iLineNum; AST(STRUCTPARAM LINENUM)
	DynParamBlock paramBlock; AST(EMBEDDED_FLAT)
	const char* pcGroupTexturesTag; AST(POOL_STRING NAME(GroupTag))
	bool bParentToLastChild; AST(BOOLFLAG)
	bool bGroupTextures_Deprecated; AST(BOOLFLAG NAME(GroupTextures))
	eDynPriority ePriorityOverride;	AST(NAME("Priority") DEFAULT(edpNotSet))
	bool bSortAsBlock_Deprecated; AST(BOOLFLAG NAME(SortAsBlock))
	DynParamConditional* pCallIf; AST(NAME(CallIf))
	F32 fChance; AST(DEFAULT(1))
} DynChildCall;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynChildCallList
{
	DynChildCall**		eaChildCall; AST(NAME("CallParam") REDUNDANT_STRUCT("Call", "ParseDynChildCall"))
	int iTimesToCall;
	bool bEqualChance; NO_AST
} DynChildCallList;

AUTO_STRUCT;
typedef struct DynChildCallCollection
{
	DynChildCall**		eaChildCall; AST(NAME("CallParam") REDUNDANT_STRUCT("Call", "ParseDynChildCall"))
	DynChildCallList**	eaChildCallList; AST(NAME("CallList"))
} DynChildCallCollection;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynFxCostume
{
	//both
	const char* pcCostume;		AST(POOL_STRING NAME(CostumeName) INDEX_DEFINE)
	const char* pcCostumeTag;	AST(POOL_STRING)
	bool bCloneSourceCostume;	AST(BOOLFLAG)
	bool bSubCostume;			AST(BOOLFLAG)
	bool bSnapshotOfCallersPose;AST(BOOLFLAG)
	bool bReleaseSnapshot;		AST(BOOLFLAG)
	bool bInheritBits;
	//parameterizable options
	const char *pcMaterial; AST(NAME(Material) INDEX_DEFINE)
	const char *pcTexture1New; AST(NAME(Texture1New) INDEX_DEFINE)
	const char *pcTexture1Old; AST(NAME(Texture1Old) INDEX_DEFINE)
	const char *pcTexture2New; AST(NAME(Texture2New) INDEX_DEFINE)
	const char *pcTexture2Old; AST(NAME(Texture2Old) INDEX_DEFINE)
	const char *pcTexture3New; AST(NAME(Texture3New) INDEX_DEFINE)
	const char *pcTexture3Old; AST(NAME(Texture3Old) INDEX_DEFINE)
	const char *pcTexture4New; AST(NAME(Texture4New) INDEX_DEFINE)
	const char *pcTexture4Old; AST(NAME(Texture4Old) INDEX_DEFINE)
	Vec3 vColor;  AST(NAME(Color)  INDEX_DEFINE)
	Vec4 vColor1; AST(NAME(Color1) INDEX_DEFINE)
	Vec4 vColor2; AST(NAME(Color2) INDEX_DEFINE)
	Vec4 vColor3; AST(NAME(Color3) INDEX_DEFINE)
	//alternate parameterizable options
	const char *pcAltMaterial; AST(NAME(AltMaterial) INDEX_DEFINE)
	const char *pcAltTexture1New; AST(NAME(AltTexture1New) INDEX_DEFINE)
	const char *pcAltTexture1Old; AST(NAME(AltTexture1Old) INDEX_DEFINE)
	const char *pcAltTexture2New; AST(NAME(AltTexture2New) INDEX_DEFINE)
	const char *pcAltTexture2Old; AST(NAME(AltTexture2Old) INDEX_DEFINE)
	const char *pcAltTexture3New; AST(NAME(AltTexture3New) INDEX_DEFINE)
	const char *pcAltTexture3Old; AST(NAME(AltTexture3Old) INDEX_DEFINE)
	const char *pcAltTexture4New; AST(NAME(AltTexture4New) INDEX_DEFINE)
	const char *pcAltTexture4Old; AST(NAME(AltTexture4Old) INDEX_DEFINE)
	Vec3 vAltColor;  AST(NAME(AltColor)  INDEX_DEFINE)
	Vec4 vAltColor1; AST(NAME(AltColor1) INDEX_DEFINE)
	Vec4 vAltColor2; AST(NAME(AltColor2) INDEX_DEFINE)
	Vec4 vAltColor3; AST(NAME(AltColor3) INDEX_DEFINE)
	//old
	const char** eapcFlashBits;	AST(NAME(BitFlash))
	const char** eapcToggleBits;AST(NAME(BitToggle))
	const char** eapcSetBits;	AST(NAME(BitSet))
	const char** eapcClearBits;	AST(NAME(BitClear))
	//new
	const char** eapcAnimKeyword;	AST(NAME(AnimKeyword))
	const char** eapcAnimFlag;		AST(NAME(AnimFlag))
	const char** eapcSetStance;		AST(NAME(SetStance))
	const char** eapcClearStance;	AST(NAME(ClearStance))
	const char** eapcToggleStance;	AST(NAME(ToggleStance))
	//both
	DynApplyParam** eaParams; AST(NAME(Param))
	bool bNotDependentSkeleton; AST(BOOLFLAG NAME(NotDependentSkeleton))
} DynFxCostume;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynLoop
{
	bool				bGlobal; AST(BOOLFLAG)
	F32					fCyclePeriod;
	F32					fCyclePeriodJitter;
	F32					fDistance;
	F32					fDistanceJitter;
	F32					fLODNear;
	F32					fLODFar;
	F32					fLifeSpan;
	DynChildCallCollection childCallCollection; AST(EMBEDDED_FLAT)
	DynFxMessage**		eaMessage;  AST(NAME("SendMessage") REDUNDANT_STRUCT("SelfMessage", "ParseSimpleDynFxMessage") REDUNDANT_STRUCT("Message", "ParseSimpleDynFxMessage"))
	char*				pcLoopTag; AST(POOL_STRING STRUCTPARAM NAME("Name"))
	bool bGroupTextures_Deprecated; AST(BOOLFLAG NAME(GroupTextures))
	bool bSortAsBlock_Deprecated; AST(BOOLFLAG NAME(SortAsBlock))
} DynLoop;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynLoopRef
{
	char*			pcTag; AST(POOL_STRING STRUCTPARAM)
	DynLoop* pLoop; NO_AST
} DynLoopRef;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynForceRef
{
	char*			pcTag; AST(POOL_STRING STRUCTPARAM)
	DynForce* pForce; NO_AST
} DynForceRef;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynParticleEmitter
{
	char*				pcTag; AST(POOL_STRING STRUCTPARAM NAME("Name"))
	REF_TO(DynFxFastParticleInfo)	hParticle; AST(NAME("Particle") REQUIRED NON_NULL_REF)
	bool bSoftKill; AST(BOOLFLAG)
	bool bApplyCountEvenly; AST(BOOLFLAG)
	bool bDontHueShift; AST(BOOLFLAG)
	bool bJumpStart; AST(BOOLFLAG)
	bool bPatternModel; AST(BOOLFLAG)
	bool bPatternModelUseTriangles; AST(BOOLFLAG)
	bool bOverrideSpecialParam; AST(BOOLFLAG)
	bool bLightModulation; AST(BOOLFLAG)
	bool bColorModulation; AST(BOOLFLAG)
	bool bNormalizeTransformTarget; AST(BOOLFLAG)
	bool bNormalizeTransformTargetOtherAxes; AST(BOOLFLAG)
	bool bLocalPlayerOnly; AST(BOOLFLAG)
	DynParticleEmitFlag position; AST(DEFAULT(DynParticleEmitFlag_Inherit))
	DynParticleEmitFlag rotation; AST(DEFAULT(DynParticleEmitFlag_Ignore))
	DynParticleEmitFlag scale; AST(DEFAULT(DynParticleEmitFlag_Ignore))
	eDynPriority	iPriorityLevel;	AST(NAME("Priority") DEFAULT(edpNotSet))
	F32				fDrawDistance; 
	F32				fMinDrawDistance; 
	const char*		pcMagnet;
	const char*		pcEmitTarget;
	const char*		pcTransformTarget;
	const char**	eaAtNodes;
	const char**	eaEmitTargetNodes;
	F32*			eaWeights;
	F32				fCutoutDepthScale; AST(DEFAULT(1.0f))
	F32				fScaleSprite;   AST(DEFAULT(1.0f))
	F32				fScalePosition; AST(DEFAULT(1.0f))
	F32				fParticleMass; AST(DEFAULT(0.0f))
	F32				fSystemAlphaFromFx; AST(DEFAULT(0.0f))
} DynParticleEmitter;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynParticleEmitterRef
{
	char*			pcTag; AST(POOL_STRING STRUCTPARAM)
	DynParticleEmitter* pEmitter; NO_AST
} DynParticleEmitterRef;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynRaycastHitEvent
{
	char**				eaHitTypes; AST(POOL_STRING STRUCTPARAM)
	DynChildCallCollection childCallCollection; AST(EMBEDDED_FLAT)
	DynFxMessage**		eaMessage;  AST(NAME("SendMessage") REDUNDANT_STRUCT("SelfMessage", "ParseSimpleDynFxMessage") REDUNDANT_STRUCT("Message", "ParseSimpleDynFxMessage"))
	DynLoopRef**		eaLoopStart; 
	DynParticleEmitterRef** eaEmitterStart;
	char**				eaSoundStart;
	bool				bFireOnce; AST(BOOLFLAG)
	bool				bMissEvent; AST(BOOLFLAG)
} DynRaycastHitEvent;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynRaycast
{
	char*			pcTag; AST(POOL_STRING STRUCTPARAM)
	const char*		pcBoneName; AST(POOL_STRING)
	// Collision Type
	bool bOrientToNormal; 
	bool bUseParentRotation; 
	F32 fRange; 
	eDynRaycastFilter	eFilter; AST(DEFAULT(eDynRaycastFilter_World) FLAGS NAME(Filter))
	bool bUpdate; 
	bool bForceDown; AST(BOOLFLAG)
	bool bCopyScale; AST(BOOLFLAG)
	bool bCheckPhysProps; NO_AST
	F32 fUpdatePeriod;
	DynRaycastHitEvent** eaHitEvent;
} DynRaycast;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct DynRaycastRef
{
	char*			pcTag; AST(POOL_STRING STRUCTPARAM)
	DynRaycast*		pRaycast; NO_AST
} DynRaycastRef;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynContactEvent
{
	char**					eaHitTypes; AST(POOL_STRING STRUCTPARAM)
	F32						fMinForce;
	F32						fMaxForce;
	DynChildCallCollection	childCallCollection; AST(EMBEDDED_FLAT)
	DynFxMessage**			eaMessage;  AST(NAME("SendMessage") REDUNDANT_STRUCT("SelfMessage", "ParseSimpleDynFxMessage") REDUNDANT_STRUCT("Message", "ParseSimpleDynFxMessage"))
	DynLoopRef**			eaLoopStart; 
	DynParticleEmitterRef** eaEmitterStart;
	char**					eaSoundStart;
	bool					bFireOnce; AST(BOOLFLAG)
	bool					bMissEvent; AST(BOOLFLAG)
} DynContactEvent;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynParentNearEvent
{
	F32					fDistance;
	DynFxMessage**		eaMessage;  AST(NAME("SendMessage") REDUNDANT_STRUCT("SelfMessage", "ParseSimpleDynFxMessage") REDUNDANT_STRUCT("Message", "ParseSimpleDynFxMessage"))
	DynChildCallCollection childCallCollection; AST(EMBEDDED_FLAT)
	bool				bLock; AST(BOOLFLAG)
	bool				bLockKeepsOrientation; AST(BOOLFLAG)
} DynParentNearEvent;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End");
typedef struct DynParentBhvr
{
	const char*				pcAtNode; AST(POOL_STRING NAME("At") INDEX_DEFINE)
	const char*				pcGoToNode; AST(POOL_STRING NAME("GoTo", "To")  INDEX_DEFINE)
	const char*				pcOrientToNode; AST(POOL_STRING NAME("OrientTo", "Orient") INDEX_DEFINE)
	const char*				pcScaleToNode; AST(POOL_STRING NAME("ScaleTo") INDEX_DEFINE)
	//F32						fSpeed;
	DynParentNearEvent**	eaNearEvents; AST(NAME("Near"))
	U32						uiDynFxInheritFlags; AST(NAME("Inherit") SUBTABLE(ParseDynNodeXFormFlags) DEFAULT(ednNone) FLAGS)
	U32						uiDynFxUpdateFlags; AST(NAME("Update") SUBTABLE(ParseDynNodeXFormFlags) DEFAULT(ednNone) FLAGS)
	U32						uiDynFxParentFlags; AST(NAME("SetFlags") SUBTABLE(ParseDynParentFlags) DEFAULT(edpfNone) FLAGS)
	DynApplyParam**				eaParams; AST(NAME("Param") )
	DynJitterList**				eaJLists; AST(NAME("JitterList") )
	bool					bSeverWithBone; AST(BOOLFLAG)
} DynParentBhvr;


typedef struct DynKeyFrame
{
	F32					fParseTimeStamp;
	DynFxTime			uiTimeStamp;
	eDynKeyFrameType	eType;
	U32					uiCount;
	DynObjectInfo		objInfo[edoTotal];
	DynChildCallCollection childCallCollection;
	DynFxMessage**		eaMessage;
	char**				ppcSoundStarts;
	char**				ppcSoundEnds;
	char**				ppcSoundDSPStarts;
	char**				ppcSoundDSPEnds;
	const char**		eaEntCostumeParts;
	const char**		eaSeverBones;
	const char**		eaRestoreSeveredBones;
	DynLoopRef**		eaLoopStart;
	DynLoopRef**		eaLoopStop;
	DynParticleEmitterRef** eaEmitterStart;
	DynParticleEmitterRef** eaEmitterStop;
	DynRaycastRef**		eaRaycastStart;
	DynRaycastRef**		eaRaycastStop;
	DynForceRef**		eaForceStart;
	DynForceRef**		eaForceStop;
	DynFxCostume**		eaCostume;
	DynParentBhvr*		pParentBhvr;
	DynFxPhysicsInfo*	pPhysicsInfo;
	DynFxTokenBitField	bfAnyChanges; 
	DynFxTokenBitField	bfChanges[edoTotal];
	F32					fFadeOutTime;
	F32					fFadeInTime;
	F32					fInheritParentVelocity;
	bool				bSystemFade;
	bool				bSkinToChildren;
	bool				bHide;
	bool				bUnhide;
	bool				bEntMaterialExcludeOptionalParts;
} DynKeyFrame;



typedef struct DynEvent
{
	DynKeyFrame**			keyFrames;
	char*					pcMessageType;
	U32						uiLargestTokenThatChanges;
	DynFxTokenBitField		bfAnyChanges; // does this dynamic token ever change at all?
	DynFxTokenBitField		bfChanges[edoTotal];
	DynFxPathSet*			pPathSet;
	DynMNCRename**			eaMatConstRename;
	U32						uiNumDynamicPaths;
	U32						uiNumStaticPaths;
	F32						fAutoCallTime;
	bool					bKeepAlive;
	bool					bTriggerOnce;
	bool					bDebris;
	bool					bLoop;
	U8						uiEventIndex;
	U32						bScaleChanges : 1;
	U32						bColorChanges : 1;
	U32						bAlphaChanges : 1;
	U32						bTexScaleChanges : 1;
	U32						bCreatesParticle : 1;
	U32						bKillEvent : 1;
	U32						bMultiColor : 1;
} DynEvent;


// these are flags you can access without unpacking the compressed FX for load time verification from other systems
typedef enum DynFxInfoFlags
{
	FxInfoSelfTerminates = (1 << 0),
} DynFxInfoFlags;

// This corresponds to the .dfx files on disk, via a parse table defined in dynFxInfo.c, so this is the "def"
// They generally live inside a packed data block, and are uncompressed on demand
typedef struct DynFxInfo
{
	DynLoop**		eaLoops;
	DynParticleEmitter**		eaEmitters;
	DynRaycast**	eaRaycasts;
	DynForce**		eaForces;
	DynList**		eaLists;
	DynContactEvent**	eaContactEvents;
	const char*		pcDynName;							// The name of this fx info
	DynEvent**		events;								// An EArray of DynEvents for this fx info
	DynEditorParam**	eaEditorParams;					// Names of all the editor-exposed parameters.

	const char*		pcNonTargetVersion;					// Name of a subdued FX to play instead of this one.
	const char*		pcSourcePlayerVersion;				// Name of another effect to play if that player is the source.
	const char*		pcTargetPlayerVersion;				// Name of another effect to play if that player is the target.
	const char*		pcEnemyVersion;						// Name of another effect to play if the source or target is of an enemy faction.

	eDynPriority	iPriorityLevel;						// The priority level (1,2, or 3) of this fx
	eDynPriority	iDropPriorityLevel;					// The priority level after which point we should not start this fx
	F32				fDrawDistance;						// How far away should we draw this particle (overrides default draw distance)
	F32				fFadeDistance;						// How far away should we draw this particle (overrides default draw distance)
	F32				fMinFadeDistance;						// How far away should we draw this particle (overrides default draw distance)
	F32				fMinDrawDistance;						// How far away should we draw this particle (overrides default draw distance)
	F32				fRadius;
	F32				fDefaultHue;
	F32				fPlaybackJitter;					// Randomly scales the speed at which this plays back
	F32				fEntityFadeSpeedOverride;
	F32				fMovedTooFarMessageDistance;		// Distance that the particle can move in a frame before a message is sent to it.
	F32				fAlienColorPriority;				// Alienware light color control.
	const char*		pcSuppressionTag;
	const char*		pcIKTargetTag;
	const char**	eaIKTargetBone;
	const char*		pcExclusionTag;
	bool			bDontDraw;							// If true, never draw anything created directly by me
	bool			bForceDontDraw;						// If true, never draw anything created directly by me
	bool			bVerifyFailed;						// Set if failed verifications
	bool			bSelfTerminates;
	bool			bKillIfOrphaned;					// If parent dies, kill me!
	bool			bDontHueShift;						// If true, dont allow hue shifting
	bool			bDontHueShiftChildren;				// If true, dont allow hue shifting, recursively
	bool			bHasAutoEvents;
	bool			bDebugFx;
	bool			bForwardMessages;
	bool			bNoAlphaInherit; 
	bool			bUseSkeletonGeometryAlpha;
	bool			bUnique;							// This flag means the FX can only play once per manager
	bool			bForce2D;
	bool			bOverrideForce2DNonLocal;			// Normally FX that are Force2D are LocalPlayerOnly also. This turns that off for some special cases.
	bool			bLocalPlayerOnly;
	bool			bLowRes;							// Render the FX in low resolution (faster, blurrier)
	bool			bEntNeedsAuxPass;
	bool			bDontLeakTest;
	bool			bInheritPlaybackSpeed;
	bool			bHibernate;
	bool			bRequiresNode;
	bool			bUseMountNodeAliases;

	bool			bGetClothModelFromWorld;			// Get cloth geometry from a WorldModelEntry.
	bool			bGetModelFromWorld;					// Get the Geometry from a WorldModelEntry.
	bool			bUseWorldModelScale;				// Use the scale set up in the editor for what this is attached to.
	bool			bHideWorldModel;					// Hide whatever this effect is attached to in the world.
	bool			bAllowWorldModelSwitch;				// Allow world model orientation changes (switching to a different model with switches, gates, etc).
	bool			bLateUpdateFastParticles;			// Update particle sets at the last minute before drawing.

	// [NNO-19698] NW: Archer: String FX are not on stowed bow (floating) when in Melee mode & Idle
	// Normally, for animated alpha, it is automatically propagated to children. We need this flag because there is a little bit of code overriding alpha during dynParticleCategorize
	// (during draw) that needs to propagate to children.
	bool			bPropagateZeroAlpha;				// If an FX ends up with a zero alpha in dynParticleCategorize, propagate this zero alpha to all of its children.

	const char*		pcPlayOnCostumeTag;					// Play the FX on the associated keyframe created costume instead of the actual caller

	//
	//  DEPRECATED
	//
	bool			bDontAutoKill_Deprecated;						// Whether or not to destroy a DynFx created with this info once there are no objects
	bool			bKillOnEmpty_Deprecated;
	bool			bSortAsBlock_Deprecated;						// Sort all children FX under this as a single block, for performance gains

//	U32				uiInheritFlags;						// What a DynFx should inherit 
//	U32				uiParentFlags;						// Which transform aspects to inherit from the parent
	char*			pcFileName;							// The physical file name, For debugging and reloading
	__time32_t		fileAge;
	DynParamBlock	paramBlock;
} DynFxInfo;

AUTO_STRUCT;
typedef struct PCFXTemp {
	const char* pcName;				  AST( POOL_STRING )
	F32 fHue;
	DynParamBlock *pParams;
} PCFXTemp;
extern ParseTable parse_PCFXTemp[];
#define TYPE_parse_PCFXTemp PCFXTemp



extern ParseTable ParseDynObject[];
extern ParseTable ParseDynKeyFrame[];
extern ParseTable parse_DynFxInfo[];

extern U32 uiFirstDynObjectStaticToken;
extern U32 uiFirstDynObjectDynamicToken;
extern U32 uiDynObjectTokenTerminator;
extern U32 uiTextureTokenIndex;
extern U32 uiTexture2TokenIndex;
extern U32 uiMaterialTokenIndex;
extern U32 uiGeoDissolveMaterialTokenIndex;
extern U32 uiGeoAddMaterialsTokenIndex;
extern U32 uiMaterial2TokenIndex;
extern U32 uiGeometryTokenIndex;
extern U32 uiGetModelFromCostumeBoneIndex;
extern U32 uiClothTokenIndex;
extern U32 uiCostumeTokenIndex;
extern U32 uiLightTypeTokenIndex;
extern U32 uiFlareTypeTokenIndex;
extern U32 uiMeshTrailTypeTokenIndex;
extern U32 uiShakePowerTokenIndex;
extern U32 uiShakeSpeedTokenIndex;
extern U32 uiCameraFOVTokenIndex;
extern U32 uiCameraInfluenceTokenIndex;
extern U32 uiAttachCameraTokenIndex;
extern U32 uiCameraDelaySpeedTokenIndex;
extern U32 uiCameraLookAtNodeTokenIndex;
extern U32 uiSplatTypeTokenIndex;
extern U32 uiSkyNameTokenIndex;
extern U32 uiColorTokenIndex;
extern U32 uiColor1TokenIndex;
extern U32 uiColor2TokenIndex;
extern U32 uiColor3TokenIndex;
extern U32 uiAlphaTokenIndex;
extern U32 uiScaleTokenIndex;
extern U32 uiScale2TokenIndex;
extern U32 uiTimeScaleTokenIndex;
extern U32 uiTimeScaleChildrenTokenIndex;

void dynFxLoadFileNamesOnly(void);

void dynFxInfoGetAllNames(const char*** pppcDynFxNames);
bool dynFxInfoExists(SA_PARAM_NN_STR const char* pcDynFxName);
int dynFxInfoGetPriorityLevel(SA_PARAM_NN_STR const char* pcDynFxName);
int dynFxInfoGetDropPriorityLevel(SA_PARAM_NN_STR const char* pcDynFxName);
F32 dynFxInfoGetDrawDistance(SA_PARAM_NN_STR const char* pcDynFxName);
const char* dynFxInfoGetFileName(SA_PARAM_NN_STR const char* pcDynFxName);
void dynFxInfoToggleDebug(const char* pcDynFxName);
int dynEventIndexFind(const DynFxInfo* pFxInfo, const char* pcMessageName);

void dynFxInfoLogBadFile(SA_PARAM_NN_VALID const char* pcFileName);
void dynFxInfoReportFileError(SA_PARAM_NN_VALID const char* pcInfoName);

void dynFxObjectClearToken(ParseTable tpi[], int column, DynObject* pObject);

bool dynObjectInfoSpecifiesToken(const DynObjectInfo* pObjectInfo, int iTokenIndex);

void dynFxSysInit(void);
bool dynFxApplyF32DynOps(F32* fValue, const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex, U32 uiValueIndex, const DynParticle* pParentParticle);
bool dynFxApplyQuatDynOps(Quat qValue, const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex);
bool dynFxApplyStaticDynOps(void* pValue, const DynObjectInfo* pDynObjectInfo, U32 uiTokenIndex, const DynParticle* pParentParticle);
U8 dynFxSizeOfToken(U32 uiTokenIndex, ParseTable* pParseTable);

U32 dynFxApplyCopyParamsGeneral( U32 uiNumParams, DynApplyParam** eaParams, U32 uiTokenIndex, const DynParamBlock* pParamBlock, void* pTargetPtr, ParseTable* pParseTable);
void dynFxApplyF32Params( U32 uiNumParams, DynApplyParam** eaParams, U32 uiTokenIndex, const DynParamBlock* pParamBlock, F32* pFloat, U8 uiWhichFloat);
void dynFxApplyIntParams( U32 uiNumParams, DynApplyParam** eaParams, U32 uiTokenIndex, const DynParamBlock* pParamBlock, int* iInt);
void dynFxApplyQuatParams( U32 uiNumParams, DynApplyParam** eaParams, U32 uiTokenIndex, const DynParamBlock* pParamBlock, Quat q);
U32 dynFxSelectRandomDynListNode(DynListNode** eaNodes, bool bEqualChance);
void dynFxApplyJitterLists( const U32 uiNumJLists, DynJitterList** eaJLists, U32 uiTokenIndex, void* pTargetPtr, ParseTable* pParseTable, dynJitterListSelectionCallback cbFunc, void* cbData );
void dynFxApplyF32JitterList( const U32 uiNumJLists, DynJitterList** eaJLists, U32 uiTokenIndex, F32* pFloat, ParseTable* pParseTable, U8 uiWhichFloat);



void dynFxInfoAddReloadCallback(dynFxInfoReloadCallback pCallback);

// Find a better place?
typedef U32 (*dynFxSoundVerifyFunc)(char *event_name);
typedef U32 (*dynFxDSPVerifyFunc)(char* dsp_name);
void dynFxSetSoundVerifyFunc(dynFxSoundVerifyFunc func);
void dynFxSetDSPVerifyFunc(dynFxDSPVerifyFunc func);

typedef void (*dynFxSoundInvalidateFunc)(const char *file_name);
void dynFxSetSoundInvalidateFunc(dynFxSoundInvalidateFunc func);

bool dynFxInfoSelfTerminates(const char* pcName);

void dynFxInfoGetFlagsIterator(StashTableIterator* pIter);

void dynFxMarkTextureAsUsed(const char* pcTextureName);

DynParticle* dynFxGetParticle(DynFx* pFx);

// Helper functions for editors. Non-null return values must be destroyed by caller using MultiValDestroy
MultiVal *dynFxInfoGetParamValueFromBlock(const char *paramName, DynParamBlock *block);
MultiVal *dynFxInfoGetParamValue(const char *fxName, const char *paramName);
MultiVal *dynFxInfoGetDefaultParamValue(const char *fxName, const char *paramName);

void dynFxInfo_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);
