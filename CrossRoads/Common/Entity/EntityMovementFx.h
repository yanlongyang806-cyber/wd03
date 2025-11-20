#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

typedef struct MovementManager			MovementManager;
typedef struct MovementRequesterMsg		MovementRequesterMsg;
typedef struct DynParamBlock			DynParamBlock;
typedef U32								EntityRef;
typedef enum PowerAnimFXType			PowerAnimFXType;

//--- Fx Resource ----------------------------------------------------------------------------------

typedef enum MMRFxJitterListSelectType
{
	kMMRFxJitterListSelectType_Random = 0,
	kMMRFxJitterListSelectType_RandomSeeded,
	kMMRFxJitterListSelectType_Closest,
} MMRFxJitterListSelectType;

typedef struct MMRFxConstantJitterListData {
	EntityRef erSource;
	EntityRef erTarget;
	F32 fRange;
	F32 fArc;
	F32 fYaw;
	U32 uSeed;
	MMRFxJitterListSelectType eSelectType;
} MMRFxConstantJitterListData;

AUTO_STRUCT;
typedef struct MMRFxConstant {
	U32					pmID;
	U32					pmSubID;
	U32					pmType;		//PowerAnimFXType
	const char*			fxName;
	EntityRef			erSource;
	EntityRef			erTarget;
	Vec3				vecSource;
	Vec3				vecTarget;
	Quat				quatTarget;
	F32					fHue;
	F32					fRange;
	F32					fArc;
	F32					fYaw;
	S32					nodeSelectionType;

	U32					triggerID;
	U32					wakeAfterThisManyPCs;
	
	U32					noSourceEnt					: 1;
	U32					miss						: 1;
	U32					sendTrigger					: 1;
	U32					waitForTrigger				: 1;
	U32					triggerIsEntityID			: 1;
	U32					useTargetNode				: 1;
	U32					alwaysSelectSameNode		: 1;
	U32					isFlashedFX					: 1;

	DynParamBlock*		fxParams_NEVER_SET_THIS; AST(NAME(fxParams))
} MMRFxConstant;

AUTO_STRUCT;
typedef struct MMRFxConstantNP {
	DynParamBlock*		fxParams;
	Vec3				vecLastKnownTarget;
} MMRFxConstantNP;

AUTO_STRUCT;
typedef struct MMRFxState {
	U32					unused;
} MMRFxState;

S32 mmrFxCreateBG(const MovementRequesterMsg* msg,
					U32* handleOut,
					const MMRFxConstant* constant,
					const MMRFxConstantNP* constantNP);

S32 mmrFxDestroyBG(	const MovementRequesterMsg* msg,
					U32* handleInOut);

S32 mmrFxClearBG(	const MovementRequesterMsg* msg,
					U32* handleInOut);

S32 mmrFxCopyAllFromManager(MovementManager* mm,
							const MovementManager* mmSource);

//--- Hit React Resource ---------------------------------------------------------------------------

AUTO_STRUCT;
typedef struct MMRHitReactConstant {
	U32*	animBits;

	U32		wakeAfterThisManyPCs;

	U32		triggerID;
	U32		waitForTrigger		: 1;
	U32		triggerIsEntityID	: 1;
} MMRHitReactConstant;

AUTO_STRUCT;
typedef struct MMRHitReactConstantNP {
	U32	unused;
} MMRHitReactConstantNP;

AUTO_STRUCT;
typedef struct MMRHitReactState {
	U32 unused;
} MMRHitReactState;

S32		mmrHitReactCreateBG(const MovementRequesterMsg* msg,
							U32* handleOut,
							const MMRHitReactConstant* constant,
							const MMRHitReactConstantNP* constantNP);

S32		mmrHitReactDestroyBG(	const MovementRequesterMsg* msg,
								U32* handleInOut);
