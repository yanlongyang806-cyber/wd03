/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityMovementDefault.h"

#include "Character_combat.h"
#include "CombatConfig.h"
#include "EntityLib.h"
#include "EntityMovementFx.h"
#include "EntityMovementManager.h"
#include "EntityMovementProjectile.h"
#include "EntityMovementRequesterDefs.h"
#include "PhysicsSDK.h"
#include "AutoGen/EntityMovementDefault_c_ast.h"
#include "TriCube/vec.h"

#include "GlobalTypes.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

AUTO_RUN_MM_REGISTER_PUBLIC_REQUESTER_MSG_HANDLER(	mrSurfaceMsgHandler,
	"SurfaceMovement",
	Surface);

static const F32	loSlipSlopeAngle = RAD(45.f);
static const F32	hiSlipSlopeAngle = RAD(60.f);
static const F32	speedToTriggerFastFall = 80.f;
static const F32	speedToTriggerGroundImpactNotification = 25.f;
static const F32	speedInputTurning = 2.75f;

static F32	jumpApexTrim = 0.33f;
AUTO_CMD_FLOAT(jumpApexTrim, mrJumpApexTrim);

static F32 jumpStationaryVelocity = 0.5f;
AUTO_CMD_FLOAT(jumpStationaryVelocity, mrJumpStationaryVelocity);

static F32 physicsCheatDistance = 0.1f;
AUTO_CMD_FLOAT(physicsCheatDistance, mrPhysicsCheatDistance);

static F32			g_fRotationYawScale = 0.8f;

static F32			g_fFaceNormalBasis = HALFPI;
static F32			g_fFaceFastRotScale = 1.4f;
static F32			g_fFaceInAirRotScale = 0.333f;

static SurfaceMovementTurnDef	g_defaultSurfaceTurnDef = { 
	QUARTERPI,	// fFaceMinTurnRate;			
	12.f * PI,	// fFaceMaxTurnRate;			
	4.f * PI,	// fFaceTurnRate;		
};

AUTO_CMD_FLOAT(g_fRotationYawScale, mrSurfaceRotYawScale);

AUTO_CMD_FLOAT(g_defaultSurfaceTurnDef.fFaceMinTurnRate, mrSurfaceFaceMinTurnRate);
AUTO_CMD_FLOAT(g_defaultSurfaceTurnDef.fFaceMaxTurnRate, mrSurfaceFaceMaxTurnRate);
AUTO_CMD_FLOAT(g_defaultSurfaceTurnDef.fFaceTurnRate, mrSurfaceFaceTurnRate);
AUTO_CMD_FLOAT(g_fFaceNormalBasis, mrSurfaceFaceNormalBasis);
AUTO_CMD_FLOAT(g_fFaceFastRotScale, mrSurfaceFaceFastRotScale);


AUTO_STRUCT;
typedef struct SurfaceFGFlags {
	U32								doCameraShake				: 1;
	U32								onGround					: 1;
} SurfaceFGFlags;

AUTO_STRUCT;
typedef struct SurfaceSpeedPenaltyFlags {
	U32								isStop						: 1;
	U32								isStrictScale				: 1;
} SurfaceSpeedPenaltyFlags;

AUTO_STRUCT;
typedef struct SurfaceSpeedPenalty {
	F32								penalty;
	U32								spc;
	U32								id;
	SurfaceSpeedPenaltyFlags		flags;
} SurfaceSpeedPenalty;

AUTO_STRUCT;
typedef struct SurfaceFGToBG {
	SurfaceSpeedPenalty**			speedPenalties;

	U32								spcStrafingOverride;
	U32								spcDisableJump;			
	U32								scheduledStrafingOverride	: 1;
	U32								scheduledDisableJump		: 1;
} SurfaceFGToBG;

AUTO_STRUCT;
typedef struct SurfaceFG {
	F32								surfaceImpactSpeed;
	SurfaceFGToBG					toBG;
	SurfaceFGFlags					flags;
} SurfaceFG;

typedef enum SurfaceStanceType {
	MR_SURFACE_STANCE_JUMPING,
	MR_SURFACE_STANCE_RISING,
	MR_SURFACE_STANCE_FALLING,
	MR_SURFACE_STANCE_TROTTING,
	MR_SURFACE_STANCE_RUNNING,
	MR_SURFACE_STANCE_LANDED,
	MR_SURFACE_STANCE_MOVING,
	MR_SURFACE_STANCE_JUMPAPEX,

	MR_SURFACE_STANCE_COUNT,
} SurfaceStanceType;

AUTO_STRUCT;
typedef struct SurfaceBGFlags {
	U32								isAtRest					: 1;
	U32								isUsingInput				: 1;

	U32								preCheckIfOnGround			: 1;
	U32								onGround					: 1;
	U32								touchingSurface				: 1;
	U32								wasOffGroundMotion			: 1;

	U32								isJumping					: 1;
	U32								offGroundByJumping			: 1;
	U32								jumpButtonNotReleased		: 1;
	U32								jumpButtonCausedJump		: 1;
	U32								jumpButtonPressed			: 1;
	U32								wasOffGroundAnim			: 1;
	U32								justLanded					: 1;
	U32								fastFalling					: 1;

	U32								flourishIsActive			: 1;
	U32								holdForFlourish				: 1;
	U32								playFlourish				: 1;

	U32								onSteepSlope				: 1;
	U32								stuckOnSteepSlope			: 1;

	U32								sticking					: 1;
	U32								hasAdditiveVel				: 1;
	U32								hasConstantForceVel			: 1;
	U32								setRepelAnim				: 1;

	U32								turnBecomesStrafe			: 1;
	U32								isTurning					: 1;

	U32								ownsAnimation				: 1;

	U32								pitchDiffInput				: 1;

	U32								hasStanceMask				: MR_SURFACE_STANCE_COUNT;
	U32								speedPenaltyIsStrictScale	: 1;
} SurfaceBGFlags;

AUTO_STRUCT;
typedef struct SurfaceBG {
	Vec3							vel;
	Vec3							velAdditive;
	Vec3							velConstantPush;

	union {
		Vec3						surfaceNormalMutable;
		const Vec3					surfaceNormal;				NO_AST
	};

	F32								yawFaceTarget;
	Quat							rotTarget;

	F32								onGroundVelY;

	F32								minOnSteepSlopeY;
	Vec3							stuckOnSteepSlopePos;
	U32								stuckOnSteepSlopeCount;

	F32								speedPenalty;
	F32								inputMoveYaw;
	F32								inputFaceYaw;

	U32								stickingFrameCount;

	F32								storedSpeed;
	F32								yawStoredSpeed;
	U32								spcStoredSpeed;

	U32								spcLastOnGroundUpdate;

	U32								spcPlayedFlourishAnim;

	F32								pitchDiffInterpScale;

	union {
		SurfaceBGFlags				flagsMutable;
		const SurfaceBGFlags		flags;				NO_AST
	};
} SurfaceBG;

AUTO_STRUCT;
typedef struct SurfaceLocalBGFlags {
	U32								inputDirIsCurrent			: 1;
	U32								tryingToMove				: 1;
	U32								onGroundToFG				: 1;
	U32								hasOverrideMaxSpeed			: 1;
	U32								hitGroundDuringTranslate	: 1;
	U32								autoRun						: 1;
	
	// set from the message MR_MSG_BG_OVERRIDE_VALUE_SET
	U32								isJumpDisabledOverrideValue	: 1;
	// set from foreground scheduling
	U32								isJumpDisabled				: 1;

	// set from the message MR_MSG_BG_OVERRIDE_VALUE_SET
	// isStrafingOverride are only ever is considered if enabled, otherwise ignored
	U32								isStrafingOverrideValue		: 1;
	// set from foreground scheduling
	U32								isStrafingOverride			: 1;
	
	U32								scheduledStrafingOverride	: 1;
	U32								scheduledDisableJump		: 1;
} SurfaceLocalBGFlags;

typedef struct SurfaceLocalBGOverrides {
	F32								maxSpeed;
} SurfaceLocalBGOverrides;

AUTO_STRUCT;
typedef struct SurfaceLocalBG {
	F32								overrideMaxSpeed;
	Vec3							inputDir;
	Vec3							velTarget;
	U32								spcLanded;
	U32								stanceHandle[MR_SURFACE_STANCE_COUNT];	NO_AST
	SurfaceLocalBGOverrides			overrides;								NO_AST
	SurfaceSpeedPenalty**			speedPenaltiesQueued;
	SurfaceSpeedPenalty**			speedPenaltiesActive;

	U32								spcStrafingOverride;
	U32								spcDisableJump;

	union {
		SurfaceLocalBGFlags			flagsMutable;					
		const SurfaceLocalBGFlags	flags;							NO_AST
	};
} SurfaceLocalBG;

AUTO_STRUCT;
typedef struct SurfaceToFGFlags {
	U32								doCameraShake	: 1;

	U32								hasOnGround		: 1;
	U32								onGround		: 1;
} SurfaceToFGFlags;

AUTO_STRUCT;
typedef struct SurfaceToFG {
	F32								surfaceImpactSpeed;
	SurfaceToFGFlags				flags;
} SurfaceToFG;

AUTO_STRUCT;
typedef struct SurfaceToBG {
	SurfaceSpeedPenalty**			speedPenalties;

	U32								spcStrafingOverride;
	U32								spcDisableJump;			

	U32								scheduledStrafingOverride	: 1;
	U32								scheduledDisableJump		: 1;
	
} SurfaceToBG;

AUTO_STRUCT;
typedef struct SurfaceSyncSetVel {
	Vec3							vel;
	U32								useVel : 1;
} SurfaceSyncSetVel;

AUTO_STRUCT;
typedef struct SurfaceSyncCollTest {
	U32								flagBits;
	Vec3							loOffset;
	Vec3							hiOffset;
	F32								radius;
} SurfaceSyncCollTest;

AUTO_STRUCT;
typedef struct SurfaceSyncJumpTest {
	U32								doJumpTest			: 1;
	Vec3							target;
} SurfaceSyncJumpTest;

AUTO_STRUCT;
typedef struct SurfaceSyncTest {
	SurfaceSyncSetVel				setVel;
	SurfaceSyncCollTest				coll;
	SurfaceSyncJumpTest				jump;
} SurfaceSyncTest;

AUTO_STRUCT;
typedef struct SurfaceSyncJump {
	F32								height;
	F32								maxSpeed;
	F32								traction;
	F32								upGravity;
	F32								downGravity;
} SurfaceSyncJump;

AUTO_STRUCT;
typedef struct SurfaceSyncFlags {
	U32								isStrafing				: 1;
	U32								flourishEnabled			: 1;
	U32								inCombat				: 1; AST(NAME(Incombat))
	U32								spawnedOnGround			: 1; //denotes recently spawned on ground (consumed on use)
	U32								canStick				: 1;
	U32								orientToSurface			: 1;
	U32								softGroundSnap			: 1;
	U32								isDisabled				: 1;
	U32								useSyncTurnDef			: 1; // if 0 (default), uses the default SurfaceMovementTurnDef
	U32								allowPhysicsCheating	: 1;	// If true, will be ignored for last bit of movement to target points

	U32								strafeOverride			: 1;
	U32								strafeOverrideEnabled	: 1;

} SurfaceSyncFlags;

AUTO_STRUCT;
typedef struct SurfaceSyncSpeedRange {
	F32								loDirScale;
	F32								hiDirScale;
	F32								speed;
} SurfaceSyncSpeedRange;

AUTO_STRUCT;
typedef struct SurfaceSync {
	F32								flourishTimer; AST(NAME(FlourishTimer))
	F32								traction;
	F32								backScale;
	F32								pitchDiffMult;
	F32								turnRateScale;			AST(NAME(TurnRateScale))
	F32								turnRateScaleFast;		
	SurfaceSyncJump					jump;
	SurfaceSyncTest					test;
	SurfaceMovementTurnDef			turn;
	SurfaceSyncFlags				flags;
} SurfaceSync;

AUTO_STRUCT;
typedef struct SurfaceSyncPublic {
	SurfaceSyncSpeedRange			speeds[4]; AST(AUTO_INDEX(speeds))
	F32								friction;
	F32								gravity;
} SurfaceSyncPublic;

STATIC_ASSERT(TYPE_ARRAY_SIZE(SurfaceSyncPublic, speeds) == MR_SURFACE_SPEED_COUNT);

#if 0
#define LOG_VEC3(v) mrmLog(	msg,\
	NULL,\
	"[surface.vel]"\
	" %s at line %d: (%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]",\
#v,\
	__LINE__,\
	vecParamsXYZ(v),\
	vecParamsXYZ((S32*)v))
#else
#define LOG_VEC3(v)
#endif

typedef struct HandleTrianglesData {
	const MovementRequesterMsg*		msg;
	U32								triCount;
} HandleTrianglesData;

static void queryTrianglesInCapsuleCallback(HandleTrianglesData* htd,
	const Vec3 (*tris)[3],
	U32 triCount)
{
	const SurfaceSync* sync = htd->msg->in.userStruct.sync;

	htd->triCount += triCount;

	if(sync->test.coll.flagBits & 1){
		FOR_BEGIN(i, (S32)triCount);
		mrmLogSegment(	htd->msg,
			NULL,
			"surface.tritest",
			0,
			tris[i][0],
			tris[i][1]);

		mrmLogSegment(	htd->msg,
			NULL,
			"surface.tritest",
			0,
			tris[i][0],
			tris[i][2]);

		mrmLogSegment(	htd->msg,
			NULL,
			"surface.tritest",
			0,
			tris[i][1],
			tris[i][2]);
		FOR_END;
	}
}

typedef struct HandleShapeTrianglesData {
	const MovementRequesterMsg* msg;
	Mat4						shapeMat;
	S32							triCount;
	S32							shapeIndex;
} HandleShapeTrianglesData;

#if !PSDK_DISABLED
static void handleShapeTriangles(PSDKShapeQueryTrianglesCBData* psdkData){
	HandleShapeTrianglesData*	hstData = psdkData->input.userPointer;
	char						buffer[100];

	sprintf(buffer, "surface.tritest(%d)", hstData->shapeIndex);

	hstData->triCount += psdkData->triCount;

	FOR_BEGIN(i, (S32)psdkData->triCount);
	Vec3 tri[3];
	Vec3 triTransformed[3];

	// Get the triangle verts in shape space.

	psdkShapeQueryTrianglesByIndex(	psdkData->input.shape,
		psdkData->triIndexes + i,
		1,
		&tri,
		0);

	// Transform the triangle into world space.

	FOR_BEGIN(j, 3);
	mulVecMat4(	tri[j],
		hstData->shapeMat,
		triTransformed[j]);
	FOR_END;

	if(0){
		FOR_BEGIN(j, 3);
		mrmLogSegmentOffset(hstData->msg,
			NULL,
			buffer,
			0xff000000 | (0xff << (8 * (2 - j))),
			hstData->shapeMat[3],
			hstData->shapeMat[j]);

		mrmLogSegment(	hstData->msg,
			NULL,
			buffer,
			0xff00ff00,
			hstData->shapeMat[3],
			triTransformed[j]);
		FOR_END;
	}

	mrmLogSegment(	hstData->msg,
		NULL,
		buffer,
		0,
		triTransformed[0],
		triTransformed[1]);

	mrmLogSegment(	hstData->msg,
		NULL,
		buffer,
		0,
		triTransformed[0],
		triTransformed[2]);

	mrmLogSegment(	hstData->msg,
		NULL,
		buffer,
		0,
		triTransformed[1],
		triTransformed[2]);
	FOR_END;
}
#endif

typedef struct StoredShape {
	void*						shape;
	Mat4						mat;
} StoredShape;

typedef struct HandleShapesData {
	const MovementRequesterMsg* msg;
	S32							triCount;
	StoredShape**				shapes;
} HandleShapesData;

#if !PSDK_DISABLED
static void handleShapes(PSDKQueryShapesCBData* psdkData){
	HandleShapesData* hsData = psdkData->input.userPointer;

	FOR_BEGIN(i, (S32)psdkData->shapeCount);
	{
		StoredShape* ss = callocStruct(StoredShape);

		ss->shape = psdkData->shapes[i];

		psdkShapeGetMat(ss->shape, ss->mat);

		eaPush(&hsData->shapes, ss);
	}
	FOR_END;
}
#endif

static S32 mrSurfaceGetBitHandleFromStanceType(	SurfaceStanceType stanceType,
	U32* handleOut)
{
	switch(stanceType){
#define CASE(x, y) xcase x: *handleOut = mmAnimBitHandles.y
		CASE(MR_SURFACE_STANCE_JUMPING, jumping);
		CASE(MR_SURFACE_STANCE_RISING, rising);
		CASE(MR_SURFACE_STANCE_FALLING, falling);
		CASE(MR_SURFACE_STANCE_TROTTING, trotting);
		CASE(MR_SURFACE_STANCE_RUNNING, running);
		CASE(MR_SURFACE_STANCE_LANDED, landed);
		CASE(MR_SURFACE_STANCE_MOVING, moving);
		CASE(MR_SURFACE_STANCE_JUMPAPEX, jumpApex);
#undef CASE
xdefault:{
		return 0;
		 }
	}

	return 1;
}

static bool mrSurfaceGetNameFromStanceType(	SurfaceStanceType stanceType,
	char** nameOut)
{
	switch(stanceType){
#define CASE(x, y) xcase x: *nameOut = #y
		CASE(MR_SURFACE_STANCE_JUMPING, jumping);
		CASE(MR_SURFACE_STANCE_RISING, rising);
		CASE(MR_SURFACE_STANCE_FALLING, falling);
		CASE(MR_SURFACE_STANCE_TROTTING, trotting);
		CASE(MR_SURFACE_STANCE_RUNNING, running);
		CASE(MR_SURFACE_STANCE_LANDED, landed);
		CASE(MR_SURFACE_STANCE_MOVING, moving);
		CASE(MR_SURFACE_STANCE_JUMPAPEX, jumpApex);
#undef CASE
xdefault:{
		return false;
		 }
	}

	return true;
}

static void mrSurfaceGetBGDebugString(	const SurfaceBG* bg,
										const SurfaceLocalBG* localBG,
										char* buffer,
										S32 bufferLen)
{
	char	extrasString[1000];
	char*	pos;

	extrasString[0] = 0;

	// Make the speed penalty string.

	pos = extrasString;

	if(eaSize(&localBG->speedPenaltiesQueued)){
		pos += snprintf_s(	pos,
			STRBUF_REMAIN(extrasString, pos),
			"Queued speed penalties:\n");

		EARRAY_CONST_FOREACH_BEGIN(localBG->speedPenaltiesQueued, i, isize);
		{
			const SurfaceSpeedPenalty* sp = localBG->speedPenaltiesQueued[i];

			if(sp->flags.isStop){
				pos += snprintf_s(	pos,
					STRBUF_REMAIN(extrasString, pos),
					"  STOP: id %u, spc %u\n",
					sp->id,
					sp->spc);
			}else{
				pos += snprintf_s(	pos,
					STRBUF_REMAIN(extrasString, pos),
					"  START: id %u, spc %u, penalty %1.3f [%8.8x]\n",
					sp->id,
					sp->spc,
					sp->penalty,
					*(S32*)&sp->penalty);
			}
		}
		EARRAY_FOREACH_END;
	}

	if(eaSize(&localBG->speedPenaltiesActive)){
		pos += snprintf_s(	pos,
			STRBUF_REMAIN(extrasString, pos),
			"Active speed penalties:\n");

		EARRAY_CONST_FOREACH_BEGIN(localBG->speedPenaltiesActive, i, isize);
		{
			const SurfaceSpeedPenalty* sp = localBG->speedPenaltiesActive[i];

			pos += snprintf_s(	pos,
				STRBUF_REMAIN(extrasString, pos),
				"  id %u, spc %u, penalty %1.3f [%8.8x]\n",
				sp->id,
				sp->spc,
				sp->penalty,
				*(S32*)&sp->penalty);
		}
		EARRAY_FOREACH_END;
	}

	if(bg->speedPenalty){
		pos += snprintf_s(	pos,
			STRBUF_REMAIN(extrasString, pos),
			"speed penalty %1.3f [%8.8x]\n",
			bg->speedPenalty,
			*(S32*)&bg->speedPenalty);
	}

	if(bg->flags.onSteepSlope){
		pos += snprintf_s(	pos,
			STRBUF_REMAIN(extrasString, pos),
			"Steep slope min y %1.3f [%8.8x] count %u\n",
			bg->minOnSteepSlopeY,
			*(S32*)&bg->minOnSteepSlopeY,
			bg->stuckOnSteepSlopeCount);
	}

	// Make the stance string.

	{
		S32 wroteLabel = 0;

		ARRAY_FOREACH_BEGIN(localBG->stanceHandle, i);
		{
			if(localBG->stanceHandle[i]){
				char* name;

				if(mrSurfaceGetNameFromStanceType(i, &name)){
					pos += snprintf_s(	pos,
						STRBUF_REMAIN(extrasString, pos),
						"%s%s(%u)",
						FALSE_THEN_SET(wroteLabel) ? "Stances: " : "",
						name,
						localBG->stanceHandle[i]);
				}
			}
		}
		ARRAY_FOREACH_END;

		if(wroteLabel){
			pos += snprintf_s(	pos,
								STRBUF_REMAIN(extrasString, pos),
								"\n");
		}
	}

#define FLAG(x) bg->flags.x ? #x", " : ""
	snprintf_s(	buffer,
				bufferLen,
				"vel(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
				"velAdditive(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
				"velConstantPush(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
				"velTarget(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
				"yawFaceTarget %1.2f [%8.8x]\n"
				"surfNorm(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
				"inputDir(%1.3f, %1.3f, %1.3f)\n"
				"inputMoveYaw %1.3f, inputFaceYaw %1.3f\n"
				"overrides: maxSpeed %1.3f (%s)\n"
				"%s"
				"flags:%s%s%s%s%s%s%s%s%s%s%s%s"
				,
				vecParamsXYZ(bg->vel),
				vecParamsXYZ((S32*)bg->vel),
				vecParamsXYZ(bg->velAdditive),
				vecParamsXYZ((S32*)bg->velAdditive),
				vecParamsXYZ(bg->velConstantPush),
				vecParamsXYZ((S32*)bg->velConstantPush),
				vecParamsXYZ(localBG->velTarget),
				vecParamsXYZ((S32*)localBG->velTarget),
				bg->yawFaceTarget,
				*(S32*)&bg->yawFaceTarget,
				vecParamsXYZ(bg->surfaceNormal),
				vecParamsXYZ((S32*)bg->surfaceNormal),
				vecParamsXYZ(localBG->inputDir),
				bg->inputMoveYaw,
				bg->inputFaceYaw,
				localBG->overrides.maxSpeed,
				localBG->flags.hasOverrideMaxSpeed ? "on" : "off",
				extrasString,
				FLAG(jumpButtonNotReleased),
				FLAG(offGroundByJumping),
				FLAG(onGround),
				FLAG(touchingSurface),
				FLAG(onSteepSlope),
				FLAG(stuckOnSteepSlope),
				FLAG(isAtRest),
				FLAG(turnBecomesStrafe),
				FLAG(ownsAnimation),
				localBG->flags.inputDirIsCurrent ? "inputDirIsCurrent, " : "",
				localBG->flags.tryingToMove ? "tryingToMove, " : "",
				localBG->flags.isStrafingOverrideValue ? "isStrafingOverrideValue, " : ""
				);
#undef FLAG
}

static void mrSurfaceGetSyncDebugString(SurfaceSync* sync,
										SurfaceSyncPublic* syncPublic,
										char* buffer,
										S32 bufferLen)
{
	char	speedsBuffer[300];
	char*	speedsBufferPos = speedsBuffer;

	speedsBuffer[0] = 0;

	ARRAY_FOREACH_BEGIN(syncPublic->speeds, i);
	{
		speedsBufferPos += snprintf_s(	speedsBufferPos,
			STRBUF_REMAIN(speedsBuffer, speedsBufferPos),
			"speed[%d](%1.2f) [%8.8x] ds(%1.3f - %1.3f)\n",
			i,
			syncPublic->speeds[i].speed,
			*(S32*)&syncPublic->speeds[i].speed,
			syncPublic->speeds[i].loDirScale,
			syncPublic->speeds[i].hiDirScale);
	}
	ARRAY_FOREACH_END;

	snprintf_s(	buffer,
		bufferLen,
		"gravity(%1.2f) [%8.8x]\n"
		"friction(%1.2f) [%8.8x]\n"
		"traction(%1.2f) [%8.8x]\n"
		"%s"
		"jumpHeight(%1.2f) [%8.8x]\n"
		"jumpTraction(%1.2f) [%8.8x]\n"
		"jumpSpeed(%1.2f) [%8.8x]\n"
		"jumpUpGravity(%1.2f) [%8.8x]\n"
		"jumpDownGravity(%1.2f) [%8.8x]\n"
		"pitchDiffMult(%1.2f) [%8.8x]\n"
		"v%s(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
		"flags: %s%s"
		,
		syncPublic->gravity,
		*(S32*)&syncPublic->gravity,
		syncPublic->friction,
		*(S32*)&syncPublic->friction,
		sync->traction,
		*(S32*)&sync->traction,
		speedsBuffer,
		sync->jump.height,
		*(S32*)&sync->jump.height,
		sync->jump.traction,
		*(S32*)&sync->jump.traction,
		sync->jump.maxSpeed,
		*(S32*)&sync->jump.maxSpeed,
		sync->jump.upGravity,
		*(S32*)&sync->jump.upGravity,
		sync->jump.downGravity,
		*(S32*)&sync->jump.downGravity,
		sync->pitchDiffMult,
		*(S32*)&sync->pitchDiffMult,
		sync->test.setVel.useVel ? ".use" : "",
		vecParamsXYZ(sync->test.setVel.vel),
		vecParamsXYZ((S32*)sync->test.setVel.vel),
		sync->flags.isStrafing ? "isStrafing, " : "",
		sync->flags.canStick ? "canStick, " : ""
		);
}

#if 0
static void projectAOntoBXZ(const Vec3 a,
	const Vec3 b,
	const F32 bLenSQR,
	Vec3 projectionOut,
	F32* scaleOut)
{
	F32 scale = dotVec3XZ(a, b) / bLenSQR;

	scaleVec3XZ(b, scale, projectionOut);

	if(scaleOut){
		*scaleOut = scale;
	}
}
#endif

static void projectVecYOntoPlane(	Vec3 v,
	const Vec3 n)
{
	if(fabs(n[1]) <= 0.0001f){
		v[1] = 0;
	}else{
		v[1] = -dotVec3XZ(v, n) / n[1];
	}
}

static void rotateVecTowardsYOntoPlane(	const Vec3 v,
	const Vec3 n,
	Vec3 vOut)
{
	F32		length = lengthVec3(v);
	F32		scale;
	Vec3	rotationVec;

	assert(v != vOut);

	crossVec3(	v,
		unitmat[1],
		rotationVec);

	crossVec3(	n,
		rotationVec,
		vOut);

	scale = lengthVec3(vOut);

	if(scale > 0.f){
		scale = length / scale;
		scaleVec3(vOut, scale, vOut);
	}
}

static void projectAOntoB(	const Vec3 a,
	const Vec3 b,
	const F32 bLenSQR,
	Vec3 projectionOut,
	F32* scaleOut)
{
	if(bLenSQR <= 0.0001f){
		zeroVec3(projectionOut);

		if(scaleOut){
			*scaleOut = 0;
		}
	}else{
		F32 scale = dotVec3(a, b) / bLenSQR;

		scaleVec3(b, scale, projectionOut);

		if(scaleOut){
			*scaleOut = scale;
		}
	}
}

static void mrSurfaceStanceSetBG(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG,
	SurfaceStanceType stanceType)
{
	if(FALSE_THEN_SET_BIT(bg->flagsMutable.hasStanceMask, BIT(stanceType))){
		U32 bitHandle;

		if(!mrSurfaceGetBitHandleFromStanceType(stanceType, &bitHandle)){
			return;
		}

		mrmAnimStanceCreateBG(	msg,
			&localBG->stanceHandle[stanceType],
			bitHandle);
	}
}

static void mrSurfaceStanceClearBG(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG,
	SurfaceStanceType stanceType)
{
	if(TRUE_THEN_RESET_BIT(bg->flagsMutable.hasStanceMask, BIT(stanceType))){
		U32 bitHandle;

		if(!mrSurfaceGetBitHandleFromStanceType(stanceType, &bitHandle)){
			return;
		}
		
		mrmAnimStanceDestroyBG(	msg,
			&localBG->stanceHandle[stanceType]);
	}
}

static void mrSurfaceActivateFlourish(	const MovementRequesterMsg *msg,
										SurfaceBG *bg,
										const SurfaceSync* sync)
{
	if (FALSE_THEN_SET(bg->flagsMutable.flourishIsActive)) {
		mrmLog(	msg,
				NULL,
				"Setting flourishIsActive");

		bg->spcPlayedFlourishAnim = 0;
		bg->flagsMutable.playFlourish = 1;
		bg->flagsMutable.holdForFlourish = sync->flourishTimer > 0.f;

		if (gConf.bNewAnimationSystem) {
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
		}

		if (!bg->flagsMutable.ownsAnimation) {
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}
	}
}

static void mrSurfaceDeactivateFlourish(const MovementRequesterMsg *msg,
										SurfaceBG *bg)
{
	if (TRUE_THEN_RESET(bg->flagsMutable.flourishIsActive)) {
		mrmLog(	msg,
				NULL,
				"Clearing flourishIsActive");

		bg->spcPlayedFlourishAnim = 0;
		bg->flagsMutable.playFlourish = 0;
		bg->flagsMutable.holdForFlourish = 0;
		
		if (gConf.bNewAnimationSystem) {
			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
			mrmAnimStartBG(msg, mmGetReleaseBit(), 0); // (1)
			// Need to do (1) since the movement system is setup to reset the animation view whenever the base cskel changes,
			// a result of which is that the last network animation bits will be flashed again whenever you mount / dismount.
			// Apparently the re-flashing of bits was desired behavior by Martin (rev. 112999), and I'd rather just send release
			// for the special case here instead of modifying that behavior game-wide.
		}
	}
}

static void checkJump(	const MovementRequesterMsg* msg,
						SurfaceBG* bg,
						SurfaceLocalBG* localBG,
						const SurfaceSync* sync,
						const SurfaceSyncPublic* syncPublic,
						S32 jump,
						S32 jumpButtonPressed,
						const Vec3 velTarget)
{
	if(jump)
	{
		const F32 maxSpeed =	localBG->flags.hasOverrideMaxSpeed ?
									localBG->overrides.maxSpeed :
									MAX(syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed, sync->jump.maxSpeed);

		const F32 bgVelSqrXZ = lengthVec3SquaredXZ(bg->vel);

		if(	!bg->flags.isJumping
			&&
			(bg->flags.onGround || !mrmProcessCountHasPassedBG(msg, bg->spcLastOnGroundUpdate + MM_PROCESS_COUNTS_PER_STEP * 3))
			&&
			(bg->surfaceNormal[1] >= 0.5f || bg->flags.sticking)
			&&
			bgVelSqrXZ <= SQR(maxSpeed * 1.5f)
			&& 
			(!jumpButtonPressed || !bg->flags.jumpButtonNotReleased)
			&&
			(!bg->flags.holdForFlourish || !bg->flags.ownsAnimation))
		{
			if (jumpButtonPressed &&
				sync->flags.flourishEnabled &&
				bgVelSqrXZ < jumpStationaryVelocity*jumpStationaryVelocity)
			{
				mrSurfaceActivateFlourish(msg,bg,sync);
			}
			else //the hold could be active here, but animation ownership will lag the activate function by a frame
			{
				if(jumpButtonPressed){
					bg->flagsMutable.jumpButtonNotReleased = 1;
					bg->flagsMutable.jumpButtonCausedJump = 1;
				}else{
					bg->flagsMutable.jumpButtonCausedJump = 0;
				}

				bg->flagsMutable.isJumping = 1;
				bg->flagsMutable.offGroundByJumping = 1;
				bg->flagsMutable.stuckOnSteepSlope = 0;
				bg->flagsMutable.onGround = 0;
				bg->flagsMutable.touchingSurface = 0;

				if (gConf.bNewAnimationSystem && sync->flags.spawnedOnGround) {
					mrmAnimResetGroundSpawnBG(msg);
				}

				mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_LANDED);
				mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPING);
				mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_RISING);

				bg->spcLastOnGroundUpdate = 0;

				bg->flagsMutable.offGroundByJumping = 1;

				// Retain jump speed.

				if(localBG->flags.tryingToMove){
					if(!mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcStoredSpeed, 0.7f)){
						// Time has not elapsed yet, use our stored speed in the jump.

						if(!vec3IsZeroXZ(velTarget)){
							F32 yawTarget = getVec3Yaw(velTarget);
							F32 yawDiff = subAngle(yawTarget, bg->yawStoredSpeed);

							if(ABS(yawDiff) < RAD(30.f)){
								Vec3 unitVelTarget;

								copyVec3(velTarget, unitVelTarget);
								normalVec3(unitVelTarget);
								scaleVec3(unitVelTarget, bg->storedSpeed, bg->vel);
							}
						}

					}

					bg->storedSpeed = 0.f;
				}

				if(	sync->jump.upGravity < 0.f &&
					sync->jump.downGravity < 0.f)
				{
					F32 maxUpTimeWithDownGravity = 0.5f;
					F32 downVel = fsqrt(2 * -sync->jump.downGravity * sync->jump.height);
					F32 maxUpVelWithDownGravity = -sync->jump.downGravity * maxUpTimeWithDownGravity;

					if(downVel <= maxUpVelWithDownGravity){
						// Not enough to bother with upGravity.

						bg->vel[1] = downVel;
					}else{
						F32 upDistWithDownGravity = 0.5f *
													-sync->jump.downGravity *
													SQR(maxUpTimeWithDownGravity);
						F32 upDistWithUpGravity =	sync->jump.height -
													upDistWithDownGravity;

						if(upDistWithUpGravity > 0.f){
							F32 t = (	-maxUpVelWithDownGravity
										+
										fsqrt(	SQR(maxUpVelWithDownGravity)
										-
										4.f *
										-sync->jump.upGravity *
										0.5f *
										-upDistWithUpGravity)
									)
									/
									(	2.f *
										-sync->jump.upGravity *
										0.5f);

							bg->vel[1] =	maxUpVelWithDownGravity
											+
											t *
											-sync->jump.upGravity;
						}else{
							bg->vel[1] = downVel;
						}
					}
				}else{
					bg->vel[1] = 0.f;
				}

				// Check if pushing away from the wall.

				if(TRUE_THEN_RESET(bg->flagsMutable.sticking))
				{
					F32 targetSpeedXZ = lengthVec3XZ(velTarget);

					if(targetSpeedXZ > 0.01f){
						Vec3	targetVelXZ;
						F32		scale = syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed *
										1.5f /
										targetSpeedXZ;

						targetVelXZ[1] = 0.f;
						scaleVec3XZ(velTarget, scale, targetVelXZ);

						if(dotVec3XZ(velTarget, bg->surfaceNormal) > 0){
							copyVec3XZ(targetVelXZ, bg->vel);
						}else{
							Vec3 tempVel;

							rotateVecTowardsYOntoPlane(targetVelXZ, bg->surfaceNormal, tempVel);

							copyVec3XZ(tempVel, bg->vel);
						}
					}
				}

				mrmLog(	msg,
						NULL,
						"[surface] Starting jump: "
						"v(%1.2f) [%8.8x]",
						bg->vel[1],
						*(S32*)&bg->vel[1]);
			}
		}
	}
	else if(TRUE_THEN_RESET(bg->flagsMutable.isJumping))
	{
		if(	bg->flags.jumpButtonCausedJump && !bg->flags.jumpButtonNotReleased)
		{
			if(bg->vel[1] > -sync->jump.downGravity * 0.5f){
				bg->vel[1] = -sync->jump.downGravity * 0.5f;
			}
			else if(bg->vel[1] > 0.f){
				bg->vel[1] *= 0.5f;
			}
		}

		mrmLog(	msg,
			NULL,
			"[surface] Released jump: "
			"v(%1.2f) [%8.8x]",
			bg->vel[1],
			*(S32*)&bg->vel[1]);
	}
}

void mrmApplyFriction(	const MovementRequesterMsg* msg,
	Vec3 vVelInOut, 
	F32 frictionLen, 
	const Vec3 velTarget)
{
	Vec3	diffToTargetVel;
	F32		scale;

	if(vec3IsZero(vVelInOut)){
		return;
	}

	subVec3(velTarget,
		vVelInOut,
		diffToTargetVel);

	scale = dotVec3(vVelInOut,
		diffToTargetVel);

	LOG_VEC3(diffToTargetVel);

	if(scale < 0.f){
		F32	lengthToTargetVelSQR = lengthVec3Squared(diffToTargetVel);

		if(lengthToTargetVelSQR > SQR(0.0001f)){
			// Project current velocity onto diffToTargetVel and apply friction.

			Vec3	curVelProj;
			Vec3	unitCurVelProj;
			F32		curVelProjLen;

			scale /= lengthToTargetVelSQR;

			scaleVec3(	diffToTargetVel,
				scale,
				curVelProj);

			LOG_VEC3(curVelProj);

			copyVec3(	curVelProj,
				unitCurVelProj);

			curVelProjLen = normalVec3(unitCurVelProj);

			LOG_VEC3(unitCurVelProj);

			if(curVelProjLen >= frictionLen){
				F32		newLen = curVelProjLen - frictionLen;
				Vec3	reduceVel;

				scaleVec3(	unitCurVelProj,
					frictionLen,
					reduceVel);

				LOG_VEC3(reduceVel);

				subVec3(vVelInOut,
					reduceVel,
					vVelInOut);

				LOG_VEC3(vVelInOut);

				frictionLen = 0;
			}else{
				frictionLen -= curVelProjLen;

				subVec3(	vVelInOut,
					curVelProj,
					vVelInOut);

				LOG_VEC3(vVelInOut);			
			}
		}
	}

	if(frictionLen != 0.f){
		// Project current velocity onto target velocity and apply friction.

		Vec3	curVelProj;
		Vec3	diffToVelProj;
		F32		lenToVelProj;
		Vec3	unitDiffToVelProj;

		projectAOntoB(	vVelInOut,
			velTarget,
			lengthVec3Squared(velTarget),
			curVelProj,
			NULL);

		subVec3(curVelProj,
			vVelInOut,
			diffToVelProj);

		copyVec3(	diffToVelProj,
			unitDiffToVelProj);

		lenToVelProj = normalVec3(unitDiffToVelProj);

		if(lenToVelProj >= frictionLen){
			Vec3 reduceVel;

			scaleVec3(	unitDiffToVelProj,
				frictionLen,
				reduceVel);

			addVec3(vVelInOut,
				reduceVel,
				vVelInOut);

			LOG_VEC3(vVelInOut);			
		}else{
			copyVec3(	curVelProj,
				vVelInOut);

			LOG_VEC3(vVelInOut);
		}
	}
}

void mrmApplyTraction(	const MovementRequesterMsg* msg,
						Vec3 vVelInOut,
						const F32 tractionLen,
						const Vec3 velTarget)
{
	Vec3	diffToTargetVel;
	F32		diffToTargetVelLen;
	Vec3	unitDiffToTargetVel;

	subVec3( velTarget, vVelInOut, diffToTargetVel);

	copyVec3( diffToTargetVel, unitDiffToTargetVel);

	diffToTargetVelLen = normalVec3(unitDiffToTargetVel);

	if(diffToTargetVelLen > tractionLen)
	{
		scaleVec3(unitDiffToTargetVel, tractionLen, diffToTargetVel);
	}

	LOG_VEC3(diffToTargetVel);

	addVec3(vVelInOut, diffToTargetVel, vVelInOut);

	LOG_VEC3(vVelInOut);
}

F32 mrmCalculateTurningStep(F32 fAngleDiff, 
							F32 fAngleBasis, 
							F32 fTurnRate, 
							F32 fMinTurnRate, 
							F32 fMaxTurnRate)
{
	F32 fTurningNorm = fAngleDiff / (fAngleBasis ? fAngleBasis : ONEOVERPI);
	F32 fFinalTurnRate;
	F32 fStep;

	fTurningNorm = ABS(fTurningNorm); 
	// damp the rotation
	fFinalTurnRate = fTurningNorm * fTurnRate;
	fFinalTurnRate = CLAMP(fTurnRate, fMinTurnRate, fMaxTurnRate);

	fStep = fFinalTurnRate * MM_SECONDS_PER_STEP;

	if (fStep > ABS(fAngleDiff))
	{
		fStep = fAngleDiff;
	}
	else if (fAngleDiff < 0.0f)
	{
		fStep = -fStep;
	}
	return fStep;
}

static void logBeforePhysicsUpdate(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG,
	SurfaceSync* sync,
	const SurfaceSyncPublic* syncPublic,
	const Vec3 velTarget,
	S32 jump,
	S32 jumpButtonPressed,
	F32	maxSpeed,
	F32 gravity,
	F32 traction,
	F32 friction)
{
	char buffer[1000];

	mrSurfaceGetBGDebugString(bg, localBG, SAFESTR(buffer));

	mrmLog(	msg,
		NULL,
		"[surface] Updating physics:\n"
		"%s\n"
		"tv(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
		"Flags: "
		"%s"// jump.
		"%s"// jumpButtonPressed.
		"\n"
		"ms(%1.2f) [%8.8x]\n"
		"g(%1.2f) [%8.8x]\n"
		"t(%1.2f) [%8.8x]\n"
		"f(%1.2f) [%8.8x]\n"
		,
		buffer,
		vecParamsXYZ(velTarget),
		vecParamsXYZ((S32*)velTarget),
		jump ? "jump, " : "",
		jumpButtonPressed ? "jumpButtonPressed, " : "",
		maxSpeed,
		*(S32*)&maxSpeed,
		gravity,
		*(S32*)&gravity,
		traction,
		*(S32*)&traction,
		friction,
		*(S32*)&friction
		);
}

static void mrSurfaceSetIsNotAtRestBG(	const MovementRequesterMsg* msg,
										SurfaceBG* bg,
										const char* reason)
{
	if(TRUE_THEN_RESET(bg->flagsMutable.isAtRest)){
		mrmLog(	msg,
				NULL,
				"Clearing isAtRest flag (%s).",
				reason);

		if(!gConf.bNewAnimationSystem){
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
		}

		if(!bg->flags.ownsAnimation){
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}
	}
}

static void mrSurfaceDestroyOffGroundStancesBG(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG)
{
	mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPING);
	mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_RISING);
	mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_FALLING);
	mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPAPEX);
}

static __forceinline void physicsUpdateOnGround(const MovementRequesterMsg* msg,
												SurfaceBG* bg,
												SurfaceSync* sync,
												const SurfaceSyncPublic* syncPublic,
												const Vec3 velTarget,
												const Vec3 posCur,
												const S32 jump,
												const S32 jumpButtonPressed,
												const F32 maxSpeed,
												const F32 gravity,
												const F32 traction,
												const F32 friction,
												const F32 deltaSeconds)
{
	F32			tractionMaxSpeed = MAX(syncPublic->speeds[MR_SURFACE_SPEED_NATIVE].speed, maxSpeed);
	F32			tractionLen = traction * tractionMaxSpeed * deltaSeconds;
	F32			frictionLen = friction * tractionMaxSpeed * deltaSeconds;
	Vec3		gravityProj;
	Vec3		unitGravityProj;
	F32			gravityProjLen;
	F32			surfAngle;
	F32			maxFriction;
	F32			maxTraction;

	if(bg->flags.sticking){
		if(++bg->stickingFrameCount >= MM_STEPS_PER_SECOND * 2){
			bg->flagsMutable.sticking = 0;

			mrSurfaceSetIsNotAtRestBG(msg, bg, "done sticking");
		}

		return;
	}

	mrmGetProcessCountBG(msg, &bg->spcLastOnGroundUpdate);

	if(bg->flags.stuckOnSteepSlope){
		if(distance3Squared(bg->stuckOnSteepSlopePos, posCur) > SQR(2)){
			bg->flagsMutable.stuckOnSteepSlope = 0;
		}else{
			setVec3(bg->surfaceNormalMutable, 0, 1, 0);
		}
	}

	if(	bg->surfaceNormal[1] >= -1.f &&
		bg->surfaceNormal[1] <= 1.f)
	{
		surfAngle = RAD(90.f) - asinf(bg->surfaceNormal[1]);
	}else{
		surfAngle = 0.f;
	}

	mrmLog(	msg,
		NULL,
		"[surface] Surface angle: %1.3f radians [%8.8x], %1.3f degrees",
		surfAngle,
		*(S32*)&surfAngle,
		DEG(surfAngle));

	if(surfAngle <= loSlipSlopeAngle){
		maxFriction = 1.f;
		maxTraction = 1.f;
	}
	else if(surfAngle <= hiSlipSlopeAngle){
		maxFriction =	(surfAngle - hiSlipSlopeAngle) /
			(loSlipSlopeAngle - hiSlipSlopeAngle);

		maxTraction = maxFriction;
	}
	else{
		maxFriction = 0.f;
		maxTraction = 0.f;
	}

	mrmLog(	msg,
		NULL,
		"[surface] Max traction/friction: %1.3f [%8.8x] %1.3f [%8.8x]",
		maxTraction,
		*(S32*)&maxTraction,
		maxFriction,
		*(S32*)&maxFriction);

	tractionLen *= maxTraction;
	frictionLen *= maxFriction;

	projectYOntoPlane(	gravity * deltaSeconds,
		bg->surfaceNormal,
		gravityProj);

	scaleVec3(	gravityProj,
		1.f - maxFriction,
		gravityProj);

	copyVec3(	gravityProj,
		unitGravityProj);

	gravityProjLen = normalVec3(unitGravityProj);

	if(gravityProjLen < frictionLen){
		frictionLen -= gravityProjLen;

		zeroVec3(gravityProj);
	}else{
		gravityProjLen -= frictionLen;

		frictionLen = 0.f;

		scaleVec3(	unitGravityProj,
			gravityProjLen,
			gravityProj);
	}

	if(MRMLOG_IS_ENABLED(msg)){
		mrmLogSegmentOffset(msg,
			NULL,
			"surface.gravityProj",
			0xff0000bb,
			posCur,
			gravityProj);

		mrmLog(	msg,
			NULL,
			"[surface.gravityProj]"
			" Gravity projected: (%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(gravityProj),
			vecParamsXYZ((S32*)gravityProj));
	}

	addVec3(gravityProj,
		bg->vel,
		bg->vel);

	LOG_VEC3(bg->vel);

	MAX1(bg->vel[1], -300.f);

	bg->onGroundVelY = MIN(0, bg->vel[1]);

	if(sync->flags.allowPhysicsCheating)
	{
		tractionLen = FLT_MAX;
	}

	if(	tractionLen != 0.f ||
		frictionLen != 0.f)
	{
		F32		targetVelLen = lengthVec3(velTarget);
		Vec3	targetVelInPlane;

		LOG_VEC3(bg->vel);

		if(vec3IsZeroXZ(velTarget)){
			zeroVec3(targetVelInPlane);
		}else{
			rotateVecTowardsYOntoPlane(	velTarget,
										bg->surfaceNormal,
										targetVelInPlane);
		}

		mrmLogSegmentOffset(msg,
							NULL,
							"surface.targetVelInPlane",
							0xff0000bb,
							posCur,
							targetVelInPlane);

		mrmLogSegmentOffset(msg,
							NULL,
							"surface.velBeforeFriction",
							0xff0000bb,
							posCur,
							bg->vel);

		mrmApplyFriction(msg, bg->vel, frictionLen, targetVelInPlane);

		LOG_VEC3(bg->vel);

		mrmLogSegmentOffset(msg,
							NULL,
							"surface.velBeforeTraction",
							0xff0000bb,
							posCur,
							bg->vel);

		mrmApplyTraction(msg, bg->vel, tractionLen, targetVelInPlane);

		LOG_VEC3(bg->vel);
	}else{
		LOG_VEC3(bg->vel);

		mrmLogSegmentOffset(msg,
							NULL,
							"surface.velNoFrictionOrTraction",
							0xff0000bb,
							posCur,
							bg->vel);

		LOG_VEC3(bg->vel);
	}
}

static bool mrSurfaceShouldPlayFallingAnimBG(const MovementRequesterMsg* msg,
											const SurfaceBG* bg)
{
	const SurfaceSyncPublic* syncPublic;

	if(bg->flags.offGroundByJumping){
		return true;
	}

	syncPublic = msg->in.userStruct.syncPublic;

	return bg->vel[1] < bg->onGroundVelY + syncPublic->gravity * 0.3f;
}

static bool mrSurfaceShouldPlayRisingAnimBG(	const MovementRequesterMsg *msg,
											const SurfaceBG *bg)
{
	const SurfaceSyncPublic *syncPublic;

	if (bg->flags.offGroundByJumping) {
		return true;
	}

	syncPublic = msg->in.userStruct.syncPublic;

	return bg->vel[1] > bg->onGroundVelY + syncPublic->gravity * -0.3f;
}

static void physicsUpdateNotOnGround(	const MovementRequesterMsg* msg,
										SurfaceBG* bg,
										SurfaceLocalBG* localBG,
										SurfaceSync* sync,
										const SurfaceSyncPublic* syncPublic,
										const Vec3 velTarget,
										const Vec3 posCur,
										const S32 jump,
										const S32 jumpButtonPressed,
										const F32 maxSpeed,
										const F32 gravity,
										const F32 traction,
										const F32 friction,
										const F32 deltaSeconds)
{
	F32		tractionMaxSpeed = MAX(syncPublic->speeds[MR_SURFACE_SPEED_NATIVE].speed, maxSpeed);
	F32		tractionLen = traction * tractionMaxSpeed * deltaSeconds;
	F32		frictionLen = friction * tractionMaxSpeed * deltaSeconds;
	Vec3	targetVelXZ;

	targetVelXZ[1] = bg->vel[1];

	if (gConf.bNewAnimationSystem && sync->flags.spawnedOnGround) {
		mrmAnimResetGroundSpawnBG(msg);
	}

	mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_LANDED);

	if(bg->flags.offGroundByJumping && fabs(bg->vel[1]) <= fabs(jumpApexTrim*gravity)) {
		mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPAPEX);
	}else{
		mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPAPEX);
	}

	if(bg->vel[1] <= 0.f){
		mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_RISING);
		if(mrSurfaceShouldPlayFallingAnimBG(msg, bg)){
			mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_FALLING);
		}
	}else{
 		mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_FALLING);
		if(!gConf.bNewAnimationSystem || mrSurfaceShouldPlayRisingAnimBG(msg, bg)){
			mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_RISING);
		}
	}

	if(!vec3IsZeroXZ(velTarget))
	{
		copyVec3XZ(velTarget, targetVelXZ);

		if(	!bg->flags.touchingSurface ||
			dotVec3(targetVelXZ, bg->surfaceNormal) > 0.f)
		{
			mrmApplyFriction(msg, bg->vel, frictionLen, targetVelXZ);
			mrmApplyTraction(msg, bg->vel, tractionLen, targetVelXZ);
		}else{
			const Vec3	vecDown = {0,-1,0};
			Vec3		targetVelOnSurface;
			Vec3		vecDownSlope;
			Vec3		vecAcrossSlope;
			Vec3		velProjected;
			F32			scale;

			projectVecOntoPlane(vecDown,
				bg->surfaceNormal,
				vecDownSlope);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.vecDownSlope1",
								0xff00ffff,
								posCur,
								vecDownSlope);

			crossVec3(	vecDownSlope,
						bg->surfaceNormal,
						vecAcrossSlope);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.vecAcrossSlope",
								0xffffff00,
								posCur,
								vecAcrossSlope);

			if(vecDownSlope[1]==0)
				scale = 1;
			else
				scale = bg->vel[1] / vecDownSlope[1];
			scaleVec3(	vecDownSlope,
						scale,
						vecDownSlope);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.vecDownSlope2",
								0xff00ffff,
								posCur,
								vecDownSlope);

			projectVecOntoPlane(targetVelXZ,
								bg->surfaceNormal,
								targetVelOnSurface);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.targetVelOnSurface1",
								0xffff8800,
								posCur,
								targetVelOnSurface);

			projectAOntoB(	targetVelOnSurface,
							vecAcrossSlope,
							lengthVec3Squared(vecAcrossSlope),
							velProjected,
							NULL);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.velProjected",
								0xff8000ff,
								posCur,
								velProjected);

			addVec3(velProjected,
					vecDownSlope,
					targetVelOnSurface);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.targetVelOnSurface2",
								0xffff0080,
								posCur,
								targetVelOnSurface);

			scaleAddVec3(	bg->surfaceNormal,
							-0.01f,
							targetVelOnSurface,
							targetVelOnSurface);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.targetVelOnSurface3",
								0xffff0080,
								posCur,
								targetVelOnSurface);

			mrmApplyFriction(msg, bg->vel, frictionLen, targetVelOnSurface);
			mrmApplyTraction(msg, bg->vel, tractionLen, targetVelOnSurface);
		}
	}
	else if(!vec3IsZeroXZ(bg->vel) &&
			bg->vel[1])
	{
		zeroVec3XZ(targetVelXZ);

		mrmApplyFriction(msg, bg->vel, frictionLen, targetVelXZ);
	}

	if(	!bg->flags.touchingSurface ||
		bg->surfaceNormal[1] > cosf(hiSlipSlopeAngle))
	{
		bg->vel[1] += gravity * deltaSeconds;
	}else{
		const Vec3	vecGravity = {0, gravity * deltaSeconds, 0};
		Vec3		vecGravityProj;

		projectVecOntoPlane(vecGravity, bg->surfaceNormal, vecGravityProj);
		addVec3(bg->vel, vecGravityProj, bg->vel);
	}

	MAX1(bg->vel[1], -300.f);
}

static void physicsGetRepelVel(	const MovementRequesterMsg* msg,
								SurfaceBG* bg)
{
	Vec3	repelVel;
	S32		resetBGVel;

	if(mrmGetAdditionalVelBG(msg, repelVel, NULL, &resetBGVel))
	{
		Vec3	velProj;
		F32		scale;

		projectAOntoB(	bg->velAdditive,
						repelVel,
						lengthVec3Squared(repelVel),
						velProj,
						&scale);

		if(scale < 0){
			copyVec3(repelVel, bg->velAdditive);
		}else{
			addVec3(repelVel, bg->velAdditive, bg->velAdditive);
			subVec3(bg->velAdditive, velProj, bg->velAdditive);
		}

		if(resetBGVel){
			zeroVec3(bg->vel);
			LOG_VEC3(bg->vel);
		}

		if (g_CombatConfig.fRepelSpeedAnimationThreshold > 0){
			if (!bg->flags.setRepelAnim)
			{
				if (lengthVec3Squared(bg->velAdditive) >= SQR(g_CombatConfig.fRepelSpeedAnimationThreshold)){
					bg->flagsMutable.setRepelAnim = true;
				} else {
					bg->flagsMutable.setRepelAnim = false;
				}
			}
		} else {
			bg->flagsMutable.setRepelAnim = true;
		}

		bg->flagsMutable.hasAdditiveVel = 1;
	}
}

static void physicsCheckForStuck(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG,
	const SurfaceSync* sync,
	const SurfaceSyncPublic* syncPublic,
	const Vec3 oldPos)
{
	S32 forceOntoFlatGround = 0;

	if(!bg->flags.onGround){
		// Check if I'm stuck in the air (on other entity capsules).

		Vec3	newPos;
		F32		velY = bg->vel[1];

		if(bg->flags.hasAdditiveVel){
			velY += bg->velAdditive[1];
		}

		mrmGetPositionBG(msg, newPos);

		if(	velY < syncPublic->gravity * 0.5f &&
			newPos[1] >= oldPos[1])
		{
			mrmLog(msg, NULL, "Forcing onto flat ground due to high velocity with no Y delta.");

			bg->flagsMutable.onGround = 1;
			bg->flagsMutable.touchingSurface = 1;
			setVec3(bg->surfaceNormalMutable, 0, 1, 0);
			bg->flagsMutable.offGroundByJumping = 0;
			forceOntoFlatGround = 1;

			mrSurfaceDestroyOffGroundStancesBG(msg, bg, localBG);
		}
	}
	else if(!bg->flags.stuckOnSteepSlope){
		const F32	slopeAngleSineToGetStuckOn = 0.6f;
		Vec3		newPos;

		mrmGetPositionBG(msg, newPos);

		// Check if I'm stuck in a slope trap.

		if(!bg->flags.onSteepSlope){
			if(	bg->surfaceNormal[1] < slopeAngleSineToGetStuckOn &&
				bg->vel[1] < 0.f)
			{
				bg->flagsMutable.onSteepSlope = 1;
				bg->minOnSteepSlopeY = newPos[1];
				bg->stuckOnSteepSlopeCount = 0;
			}
		}
		else if(bg->surfaceNormal[1] >= slopeAngleSineToGetStuckOn){
			bg->flagsMutable.onSteepSlope = 0;
			bg->stuckOnSteepSlopeCount = 0;
		}
		else if(newPos[1] < bg->minOnSteepSlopeY){
			bg->minOnSteepSlopeY = newPos[1];
			bg->stuckOnSteepSlopeCount = 0;
		}
		else if(bg->vel[1] < 0.f &&
			newPos[1] >= oldPos[1])
		{
			if(++bg->stuckOnSteepSlopeCount < 10){
				mrmLog(	msg,
					NULL,
					"Still on steep slope with negative y vel (%d times).",
					bg->stuckOnSteepSlopeCount);
			}else{
				mrmLog(msg, NULL, "Forcing onto flat ground due to slope trap.");
				forceOntoFlatGround = 1;
				copyVec3(newPos, bg->stuckOnSteepSlopePos);
				bg->flagsMutable.stuckOnSteepSlope = 1;
				bg->flagsMutable.onSteepSlope = 0;
				bg->stuckOnSteepSlopeCount = 0;
			}
		}
	}

	if(forceOntoFlatGround){
		// Stuck on something while falling, so pretend we're on something flat.

		F32 velLen;

		bg->vel[1] = 0;

		velLen = normalVec3XZ(bg->vel);

		MIN1(velLen, syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed);

		scaleVec3XZ(bg->vel,
			velLen,
			bg->vel);

		LOG_VEC3(bg->vel);
	}
}

static void mrSurfaceSetNotOnGroundBG(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG,
	const char* reason)
{
	if(!TRUE_THEN_RESET(bg->flagsMutable.onGround)){
		return;
	}

	mrmLog(msg, NULL, "Setting not on ground: %s", reason);

	if (gConf.bNewAnimationSystem) {
		mrmAnimResetGroundSpawnBG(msg);
	}

	if(bg->flags.offGroundByJumping &&
		fabs(bg->vel[1]) <= fabs(jumpApexTrim*((SurfaceSyncPublic*)(msg->in.userStruct.syncPublic))->gravity)){
			mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPAPEX);
	}

	if(bg->vel[1] > 0.f){
		if(!gConf.bNewAnimationSystem || mrSurfaceShouldPlayRisingAnimBG(msg, bg)){
			mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_RISING);
		}
	}else{
		if(mrSurfaceShouldPlayFallingAnimBG(msg, bg)){
			mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_FALLING);
		}
	}
}

static void mrSurfaceTranslatePositionBG(	const MovementRequesterMsg* msg,
											SurfaceBG* bg,
											SurfaceLocalBG* localBG,
											const Vec3 stepVel)
{
	localBG->flagsMutable.hitGroundDuringTranslate = 0;
	bg->flagsMutable.touchingSurface = 0;

	mrmTranslatePositionBG(msg, stepVel, 1, 0);

	if(!localBG->flags.hitGroundDuringTranslate){
		mrSurfaceSetNotOnGroundBG(msg, bg, localBG, "translate didn't hit ground");
	}
}

static void physicsDoTranslation(	const MovementRequesterMsg* msg,
									SurfaceBG* bg,
									SurfaceLocalBG* localBG,
									const SurfaceSync* sync,
									const SurfaceSyncPublic* syncPublic,
									const Vec3 posCur)
{
	Vec3	stepVel;
	Vec3	oldPos;

	if(!FINITEVEC3(bg->vel))
	{
		mrmLog(	msg,
			NULL,
			"ERROR: vel is not finite: (%f, %f, %f) [%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(bg->vel),
			vecParamsXYZ((S32*)bg->vel));

		zeroVec3(bg->vel);
	}
	else
	{
		if(MRMLOG_IS_ENABLED(msg))
		{
			LOG_VEC3(bg->vel);

			mrmLogSegmentOffset(msg,
								NULL,
								"surface.vel",
								0xff00ff00,
								posCur,
								bg->vel);

			if(bg->flags.hasAdditiveVel)
			{
				Vec3 velTotal;

				addVec3(bg->vel,
						bg->velAdditive,
						velTotal);

				mrmLogSegmentOffset(msg,
									NULL,
									"surface.vel",
									0xffff0000,
									posCur,
									bg->velAdditive);

				mrmLogSegmentOffset(msg,
									NULL,
									"surface.vel",
									0xff0000ff,
									posCur,
									velTotal);
			}

			if (bg->flags.hasConstantForceVel)
			{
				mrmLogSegmentOffset(msg,
									NULL,
									"surface.vel",
									0xffff0000,
									posCur,
									bg->velConstantPush);
			}
		}

		copyVec3(bg->vel, stepVel);

		if(bg->flags.hasAdditiveVel)
		{
			addVec3(bg->velAdditive, stepVel, stepVel);
		}

		// add in any constant push velocity
		if (bg->flags.hasConstantForceVel)
		{
			addVec3(bg->velConstantPush, stepVel, stepVel);
		}



		scaleVec3(stepVel, MM_SECONDS_PER_STEP, stepVel);

		mrmGetPositionBG(msg, oldPos);

		mrSurfaceTranslatePositionBG(msg, bg, localBG, stepVel);

		if(	!bg->flags.onGround &&
			stepVel[1] > 0.f)
		{
			Vec3 newPos;

			mrmGetPositionBG(msg, newPos);

			if(newPos[1] - oldPos[1] > stepVel[1] * 1.1f){
				Vec3	diff;
				F32		scale;

				subVec3(newPos, oldPos, diff);
				scale = stepVel[1] / diff[1];
				scaleAddVec3(diff, scale, oldPos, diff);
				subVec3(diff, newPos, diff);

				mrmLog(	msg,
						NULL,
						"[surface] Went too high, going back (%1.3f, %1.3f, %1.3f).",
						vecParamsXYZ(diff));

				mrSurfaceTranslatePositionBG(msg, bg, localBG, diff);
			}
		}

		// config option to snap to the ground, if we are in the MST_CONSTANT speed type
		// we want the character to hug the ground more tightly
		// Move straight down to the ground or back to start
		// note: some common code in mrtactical
		if(	g_CombatConfig.bSurfaceRequester_DoConstantSpeedGroundSnap &&
			!bg->flags.onGround && !bg->flags.wasOffGroundMotion)
		{
			const MovementRequesterMsgCreateOutputShared*	shared = msg->in.bg.createOutput.shared;
			MovementSpeedType								speedType = shared->target.pos->speedType;

			if(speedType == MST_CONSTANT){
				Vec3 velDown = {0, -3.f, 0};
				Vec3 posBefore;
				mrmGetPositionBG(msg, posBefore);

				mrmTranslatePositionBG(msg, velDown, 1, 0);
				if(!bg->flags.onGround){
					mrmSetPositionBG(msg, posBefore);
				}
			}
		}

		physicsCheckForStuck(	msg,
			bg,
			localBG,
			sync,
			syncPublic,
			oldPos);
		LOG_VEC3(bg->vel);
	}
}

static void physicsUpdate(	const MovementRequesterMsg* msg,
							SurfaceBG* bg,
							SurfaceLocalBG* localBG,
							SurfaceSync* sync,
							const SurfaceSyncPublic* syncPublic,
							SurfaceToFG* toFG,
							const Vec3 velTarget,
							const S32 jump,
							const S32 jumpButtonPressed,
							const F32 maxSpeed,
							const S32 ignoreGravity,
							F32 traction,
							F32 friction,
							const F32 deltaSeconds)
{
	Vec3	posCur;
	F32		gravity;

	copyVec3(	velTarget,
				localBG->velTarget);

	mrmGetPositionBG(msg, posCur);

	checkJump(	msg,
				bg,
				localBG,
				sync,
				syncPublic,
				jump,
				jumpButtonPressed,
				velTarget);

	// Decide which kind of gravity to use.

	if(ignoreGravity){
		gravity = 0.f;
	}
	else if(bg->flags.offGroundByJumping &&
		bg->flags.jumpButtonCausedJump)
	{
		if(	bg->flags.jumpButtonNotReleased &&
			bg->vel[1] > -sync->jump.downGravity * 0.5f)
		{
			gravity = sync->jump.upGravity;
		}else{
			gravity = sync->jump.downGravity;
		}
	}else{
		gravity = syncPublic->gravity;
	}

	if(!bg->flags.onGround){
		traction *= 0.3f;
		friction *= 0.05f;
	}

	if(msg->in.flags.debugging){
		logBeforePhysicsUpdate(	msg,
								bg,
								localBG,
								sync,
								syncPublic,
								velTarget,
								jump,
								jumpButtonPressed,
								maxSpeed,
								gravity,
								traction,
								friction);
	}

	if(bg->flags.onGround){
		physicsUpdateOnGround(	msg,
								bg,
								sync,
								syncPublic,
								velTarget,
								posCur,
								jump,
								jumpButtonPressed,
								maxSpeed,
								gravity,
								traction,
								friction,
								deltaSeconds);
	}else{
		physicsUpdateNotOnGround(	msg,
									bg,
									localBG,
									sync,
									syncPublic,
									velTarget,
									posCur,
									jump,
									jumpButtonPressed,
									MAX(maxSpeed, sync->jump.maxSpeed),
									gravity,
									MAX(traction, sync->jump.traction),
									friction,
									deltaSeconds);
	}

	physicsGetRepelVel(msg, bg);
	
	if(bg->flags.hasAdditiveVel)
	{
		bg->flagsMutable.hasAdditiveVel = !mrProjectileApplyFriction(bg->velAdditive, bg->flags.onGround);
		if(!bg->flags.hasAdditiveVel)
		{
			bg->flagsMutable.setRepelAnim = false;
		}
	}

	if (mrmGetConstantPushVelBG(msg, bg->velConstantPush))
	{
		bg->flagsMutable.hasConstantForceVel = 1;
	}
	else
	{
		bg->flagsMutable.hasConstantForceVel = 0;
	}

	if(	!bg->flags.onGround ||
		lengthVec3Squared(bg->vel) >= SQR(0.001f) ||
		bg->flags.hasAdditiveVel || 
		bg->flags.hasConstantForceVel)
	{
		LOG_VEC3(bg->vel);
		physicsDoTranslation(	msg,
								bg,
								localBG,
								sync,
								syncPublic,
								posCur);
		LOG_VEC3(bg->vel);
	}
	else if(!bg->flags.isAtRest &&
			!localBG->flags.tryingToMove &&
			bg->surfaceNormal[1] >= 0.5f)
	{
		// Go to sleep.

		zeroVec3(bg->vel);

		LOG_VEC3(bg->vel);

		if(FALSE_THEN_SET(bg->flagsMutable.isAtRest)){
			if(!gConf.bNewAnimationSystem){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
			}
		}
	}

	mrmLog(	msg,
			NULL,
			"[surface] Updated physics:\n"
			"velTarget(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
			"vel(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]",
			vecParamsXYZ(velTarget),
			vecParamsXYZ((S32*)velTarget),
			vecParamsXYZ(bg->vel),
			vecParamsXYZ((S32*)bg->vel));

	// Send flags.onGround to FG if it changed locally.

	if(localBG->flags.onGroundToFG != bg->flags.onGround)
	{
		localBG->flagsMutable.onGroundToFG = bg->flags.onGround;

		mrmEnableMsgUpdatedToFG(msg);
		toFG->flags.hasOnGround = 1;
		toFG->flags.onGround = bg->flags.onGround;
	}
}

static void mrSurfaceInputDirIsDirtyBG(	const MovementRequesterMsg* msg,
										SurfaceLocalBG* localBG,
										const char* reason)
{
	if(TRUE_THEN_RESET(localBG->flagsMutable.inputDirIsCurrent)){
		mrmLog(msg, NULL, "Reset inputDirIsCurrent (%s).", reason);
	}
}

static void mrSurfaceHandleTurningInputBG(	const MovementRequesterMsg* msg,
											SurfaceBG* bg, 
											SurfaceLocalBG* localBG,
											const SurfaceSync* sync)
{
	const S32 turnInputSign = mrmGetInputValueBitDiffBG(msg,
														MIVI_BIT_TURN_RIGHT,
														MIVI_BIT_TURN_LEFT);

	mrmLog(	msg,
			NULL,
			"[surface] Turning Input Sign: %d\n"
			"Input MoveYaw: %1.3f [%8.8x]\n"
			"Input FaceYaw: %1.3f [%8.8x]\n",
			turnInputSign,
			bg->inputMoveYaw,
			*(S32*)&bg->inputMoveYaw,
			bg->inputFaceYaw,
			*(S32*)&bg->inputFaceYaw);

	if(turnInputSign){
		F32 yawDelta =	MM_SECONDS_PER_STEP *
						speedInputTurning *
						turnInputSign;

		bg->inputMoveYaw = addAngle(bg->inputMoveYaw, yawDelta);
		bg->inputFaceYaw = addAngle(bg->inputFaceYaw, yawDelta);

		mrSurfaceInputDirIsDirtyBG(msg, localBG, "turning");
	}
}

static void mrSurfaceGetInputDirBG(	const MovementRequesterMsg* msg,
									SurfaceBG* bg, 
									SurfaceLocalBG* localBG,
									Vec3 dirOut)
{
	if (FALSE_THEN_SET(localBG->flagsMutable.inputDirIsCurrent))
	{
		Vec3 dirRelative;
		Vec3 dirYaw;

		setVec3(dirRelative,
				mrmGetInputValueBitDiffBG(msg, MIVI_BIT_RIGHT, MIVI_BIT_LEFT),
				mrmGetInputValueBitDiffBG(msg, MIVI_BIT_UP, MIVI_BIT_DOWN),
				mrmGetInputValueBitDiffBG(msg, MIVI_BIT_FORWARD, MIVI_BIT_BACKWARD));

		if (localBG->flags.autoRun && dirRelative[2] == 0.f)
		{
			dirRelative[2] = 1.f;
		}

		if(bg->flags.turnBecomesStrafe){
			dirRelative[0] += mrmGetInputValueBitDiffBG(msg,
														MIVI_BIT_TURN_RIGHT,
														MIVI_BIT_TURN_LEFT);

			dirRelative[0] = CLAMP(dirRelative[0], -1.f, 1.f);
		}

		sincosf(bg->inputMoveYaw,
				dirYaw + 0,
				dirYaw + 2);

		setVec3(localBG->inputDir,
				dirRelative[0] * dirYaw[2] +
				dirRelative[2] * dirYaw[0],
				dirRelative[1],
				dirRelative[2] * dirYaw[2] -
				dirRelative[0] * dirYaw[0]);

		normalVec3XZ(localBG->inputDir);

		mrmLog(	msg,
				NULL,
				"Created inputDir (%1.3f, %1.3f, %1.3f).",
				vecParamsXYZ(localBG->inputDir));
	}

	copyVec3(	localBG->inputDir,
				dirOut);
}

static void mrSurfaceHandleCreateOutputPositionTarget(	const MovementRequesterMsg* msg,
														SurfaceBG* bg,
														SurfaceLocalBG* localBG,
														SurfaceSync* sync,
														SurfaceSyncPublic* syncPublic,
														SurfaceToFG* toFG)
{
	//const MovementRequesterMsgCreateOutputShared* shared = msg->in.bg.createOutput.shared;

	if(TRUE_THEN_RESET(sync->test.jump.doJumpTest)){
		mrmTargetSetAsPointBG(msg, sync->test.jump.target);
		mrmTargetSetStartJumpBG(msg, sync->test.jump.target, 1);

		mrmReleaseDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET);
	}
}

static void mrSurfaceUpdateMovingStancesBG(	const MovementRequesterMsg* msg,
											SurfaceBG* bg,
											SurfaceLocalBG* localBG,
											const SurfaceSyncPublic* syncPublic)
{
	Vec3 velTarget;

	if(!gConf.bNewAnimationSystem){
		return;
	}

	copyVec3(	localBG->velTarget,
		velTarget);

	if(	!localBG->flags.tryingToMove ||
		vec3IsZeroXZ(velTarget))
	{
		mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_MOVING);
		mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_RUNNING);
		mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_TROTTING);
	}else{
		const F32 curSpeedXZ = lengthVec3XZ(velTarget);

		mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_LANDED);
		mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_MOVING);

		if(curSpeedXZ > syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed + 0.5f){
			// The entity is cheating its movement speed past the fastest speed.
			// This is a special case where we will just set both the trot and run bits.

			mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_TROTTING);
			mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_RUNNING);
		}else{
			S32	lastGoodSpeedIndex = 0;
			F32	lastGoodSpeed = 0.f;

			ARRAY_FOREACH_BEGIN(syncPublic->speeds, i);
			{
				const F32 speed = syncPublic->speeds[i].speed;

				if(i == MR_SURFACE_SPEED_NATIVE ||
					speed == lastGoodSpeed)
				{
					continue;
				}

				if(	curSpeedXZ >= lastGoodSpeed &&
					curSpeedXZ <= speed
					||
					i == ARRAY_SIZE(syncPublic->speeds) - 1)
				{
					F32 diff = speed - lastGoodSpeed;

					// Found the speed I want, or it's the last speed.

					if(curSpeedXZ >= lastGoodSpeed + diff * 0.5f ){
						lastGoodSpeedIndex = i;
					}

					break;
				}

				lastGoodSpeed = speed;
				lastGoodSpeedIndex = i;
			}
			ARRAY_FOREACH_END;

			switch(lastGoodSpeedIndex){
				xcase MR_SURFACE_SPEED_SLOW:{
					mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_RUNNING);
					mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_TROTTING);
				}

				xcase MR_SURFACE_SPEED_MEDIUM:{
					mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_RUNNING);
					mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_TROTTING);
				}

				xcase MR_SURFACE_SPEED_FAST:{
					mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_TROTTING);
					mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_RUNNING);
				}
			}
		}
	}
}

static void mrSurfaceHandleCreateOutputPositionChange(	const MovementRequesterMsg* msg,
														SurfaceBG* bg,
														SurfaceLocalBG* localBG,
														SurfaceSync* sync,
														SurfaceSyncPublic* syncPublic,
														SurfaceToFG* toFG)
{
	const MovementRequesterMsgCreateOutputShared*	shared = msg->in.bg.createOutput.shared;

	S32							slow = 0;
	S32							jump = 0;

	S32							hasTargetPos = 0;
	Vec3 						targetPos = {0};
	Vec3						targetDirXZ = {0};
	Vec3						dirTarget = {0};
	Vec3						velTarget = {0};

	F32							targetSpeed = (localBG->flags.hasOverrideMaxSpeed) ? 
													localBG->overrides.maxSpeed : syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed;

	U32							speedIndex = MR_SURFACE_SPEED_FAST;

	F32							friction;
	F32							traction;
	S32							ignoreGravity = 0;

	MovementPositionTargetType	targetType = shared->target.pos->targetType;
	MovementSpeedType			speedType = shared->target.pos->speedType;
	
	// Say I'm trying to move until someone says otherwise.

	localBG->flagsMutable.tryingToMove = 1;

	if(TRUE_THEN_RESET(sync->test.setVel.useVel)){
		mrmLog(	msg,
				NULL,
				"[surface] Applying sync vel:"
				" (%1.2f, %1.2f, %1.2f)"
				" [%8.8x, %8.8x, %8.8x]",
				vecParamsXYZ(sync->test.setVel.vel),
				vecParamsXYZ((S32*)sync->test.setVel.vel));

		copyVec3(sync->test.setVel.vel, bg->vel);

		mrSurfaceSetNotOnGroundBG(msg, bg, localBG, "test setvel");
		mrSurfaceSetIsNotAtRestBG(msg, bg, "test setvel");
	}
	
	if(bg->flags.isAtRest)
	{
		if(	bg->flags.sticking || 
			mrmHasConstantPushVelBG(msg) ||
			mrmGetAdditionalVelBG(msg, NULL, NULL, NULL))
		{
			mrSurfaceSetIsNotAtRestBG(msg, bg, "has add vel");
		}
	}

	if(shared->target.pos->frictionType != MST_OVERRIDE){
		friction = syncPublic->friction;
	}else{
		friction = shared->target.pos->friction;
	}

	if(shared->target.pos->tractionType != MST_OVERRIDE){
		traction = sync->traction;
	}else{
		traction = shared->target.pos->traction;
	}

	if(TRUE_THEN_RESET(bg->flagsMutable.preCheckIfOnGround)){
		// Some geometry was changed, check if I'm still on the ground.

		const Vec3	offset = {0.f, -0.3f, 0.f};
		Vec3		posStart;

		mrmGetPositionBG(msg, posStart);

		mrSurfaceTranslatePositionBG(msg, bg, localBG, offset);

		if(!bg->flags.onGround){
			mrSurfaceSetIsNotAtRestBG(msg, bg, "precheck says not on ground");
			mrmSetPositionBG(msg, posStart);
		}
	}

	bg->flagsMutable.wasOffGroundMotion = !bg->flags.onGround;

	if(	targetType != MPTT_INPUT &&
		TRUE_THEN_RESET(bg->flagsMutable.isUsingInput))
	{
		mrmLog(	msg, NULL, "[surface] Stopped using input.");
	}

	if (localBG->spcStrafingOverride && mrmProcessCountHasPassedBG(msg, localBG->spcStrafingOverride))
	{
		localBG->flagsMutable.isStrafingOverride = localBG->flagsMutable.scheduledStrafingOverride;
		localBG->spcStrafingOverride = 0;
	}

	if (localBG->spcDisableJump && mrmProcessCountHasPassedBG(msg, localBG->spcDisableJump))
	{
		localBG->flagsMutable.isJumpDisabled = localBG->flagsMutable.scheduledDisableJump;
		localBG->spcDisableJump = 0;
	}

	switch(targetType)
	{
		xcase MPTT_INPUT:{
			if(FALSE_THEN_SET(bg->flagsMutable.isUsingInput)){
				mrmLog(	msg,
						NULL,
						"[surface] Switched to using input.");

				mrSurfaceSetIsNotAtRestBG(msg, bg, "switched to input");
			}
			else if(bg->flags.isAtRest ||
					(	bg->flags.holdForFlourish &&
						bg->flags.ownsAnimation))
			{
				localBG->flagsMutable.tryingToMove = !bg->flags.isAtRest;

				mrmLog(	msg,
						NULL,
						"%s",
						bg->flags.holdForFlourish ?
							"[surface] held for flourish" :
							"[surface] isAtRest, not moving.");

				break;
			}

			mrmLog(	msg,
					NULL,
					"[surface] Using input.");

			if (bg->flags.isTurning) {
				mrSurfaceHandleTurningInputBG(msg, bg, localBG, sync);
			}

			mrSurfaceGetInputDirBG(msg, bg, localBG, dirTarget);

			if(vec3IsZeroXZ(dirTarget)){
				localBG->flagsMutable.tryingToMove = 0;

				mrmLog(	msg,
						NULL,
						"[surface] dirTarget has no XZ.");
			}else{
				const F32	turnOnlyZone = 0.1f;
				F32			dirScale = mrmGetInputValueF32BG(msg, MIVI_F32_DIRECTION_SCALE);

				copyVec3XZ(	dirTarget,
					targetDirXZ);

				targetDirXZ[1] = 0.f;

				if(dirScale <= turnOnlyZone){
					targetSpeed = 0.f;
				}else{
					F32 overrideScale = 1.f;

					if(localBG->flags.hasOverrideMaxSpeed){
						overrideScale = localBG->overrides.maxSpeed /
							syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed;
					}

					dirScale = (dirScale - turnOnlyZone) / (1.f - turnOnlyZone);

					MINMAX1(dirScale, 0.f, 1.f);

					ARRAY_FOREACH_BEGIN(syncPublic->speeds, i);
					{
						if(dirScale < syncPublic->speeds[i].loDirScale){
							speedIndex = i;

							if(i){
								F32 range, diff, speedRange;
								ANALYSIS_ASSUME(i > 0);
								// Scale between my lo and previous hi.
#pragma warning(suppress:6201) // /analyze isn't realizing i bigger than 0 even with ANALYSIS_ASSUME
								range =	syncPublic->speeds[i].loDirScale - syncPublic->speeds[i - 1].hiDirScale;

#pragma warning(suppress:6201) // /analyze isn't realizing i bigger than 0 even with ANALYSIS_ASSUME
								diff = dirScale - syncPublic->speeds[i - 1].hiDirScale;

#pragma warning(suppress:6201) // /analyze isn't realizing i bigger than 0 even with ANALYSIS_ASSUME
								speedRange =	overrideScale *	(syncPublic->speeds[i].speed - syncPublic->speeds[i - 1].speed);

								if(range < 0.0001f){
									dirScale = 1;
								}else{
									dirScale = diff / range;
								}

#pragma warning(suppress:6201) // /analyze isn't realizing i bigger than 0 even with ANALYSIS_ASSUME
								targetSpeed =	overrideScale * syncPublic->speeds[i - 1].speed + speedRange * dirScale;
							}else{
								// This is the lowest speed, so ramp from zero.

								dirScale /= syncPublic->speeds[i].loDirScale;

								targetSpeed =	overrideScale *
									syncPublic->speeds[i].speed *
									dirScale;
							}

							break;
						}
						else if(i == ARRAY_SIZE(syncPublic->speeds) - 1 ||
							dirScale <= syncPublic->speeds[i].hiDirScale)
						{
							// Between lo and hi dirScale, or this is top speed.

							speedIndex = i;
							targetSpeed =	overrideScale *
								syncPublic->speeds[i].speed;
							break;
						}
					}
					ARRAY_FOREACH_END;
				}
			}

			if (!localBG->flags.hasOverrideMaxSpeed)
				slow = mrmGetInputValueBitBG(msg, MIVI_BIT_SLOW);

			jump = bg->flags.jumpButtonPressed;
		}

		xcase MPTT_POINT:{
			mrSurfaceSetIsNotAtRestBG(msg, bg, "target is point");

			copyVec3(	shared->target.pos->point,
				targetPos);

			hasTargetPos = 1;

			jump = shared->target.pos->flags.startJump;

			if(	shared->target.pos->flags.hasJumpTarget &&
				bg->flags.onGround)
			{
#define TARGET_HEIGHT_OFFSET	2.f

				Vec3	posCur;
				Vec3	jumpTarget;
				Vec3	diffXZ;
				F32		distXZ, targetHeight;
				F32		t, speedXZ;
				F32		g = -syncPublic->gravity;

				mrmGetPositionBG(msg, posCur);

				bg->flagsMutable.isJumping = 1;
				bg->flagsMutable.offGroundByJumping = 1;
				bg->flagsMutable.stuckOnSteepSlope = 0;
				bg->flagsMutable.onGround = 0;
				bg->flagsMutable.jumpButtonCausedJump = 0;

				if (gConf.bNewAnimationSystem && sync->flags.spawnedOnGround) {
					mrmAnimResetGroundSpawnBG(msg);
				}

				mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPING);
				mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_RISING);

				copyVec3(	shared->target.pos->jumpTarget,
					jumpTarget);

				subVec3XZ(	jumpTarget,
					posCur,
					diffXZ);

				targetHeight = jumpTarget[1] - posCur[1] + TARGET_HEIGHT_OFFSET;
				if (targetHeight < 0.f){ // jumping to a target below us, 
					// set a default target height so we can clear some standard height object incase
					// there is a railing we're jumping over or something
					targetHeight = 15.f;
				}

				// get the velocity to get to the target height
				bg->vel[1]  = sqrt(2 * targetHeight * g);

				t = 2.f * (bg->vel[1] - TARGET_HEIGHT_OFFSET)  / g;

				distXZ = normalVec3XZ(diffXZ);
				// roughly calculate the speed it should take us to get to the position 
				// by the time we are a little bit before the peak of our jump
				speedXZ = distXZ / t;

				scaleVec3XZ(diffXZ,
					speedXZ,
					bg->vel);

				LOG_VEC3(bg->vel);

				mrmLog(	msg,
					NULL,
					"[surface] Jumping to point: "
					"t(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]"
					"New vel: "
					"t(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]"
					,
					vecParamsXYZ(jumpTarget),
					vecParamsXYZ((S32*)jumpTarget),
					vecParamsXYZ(bg->vel),
					vecParamsXYZ((S32*)bg->vel)
					);
			}

			mrmLog(	msg,
				NULL,
				"[surface] Targeting point: "
				"t(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]"
				,
				vecParamsXYZ(targetPos),
				vecParamsXYZ((S32*)targetPos)
				);
		}

		xcase MPTT_VELOCITY:{
			mrSurfaceSetIsNotAtRestBG(msg, bg, "target is velocity");

			copyVec3(	shared->target.pos->vel,
				bg->vel);

			LOG_VEC3(bg->vel);

			bg->flagsMutable.jumpButtonNotReleased = 0;
			bg->flagsMutable.isJumping = 0;

			mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_JUMPING);

			mrSurfaceSetNotOnGroundBG(msg, bg, localBG, "velocity target");

			speedType = MST_NONE;

			mrmLog(	msg,
				NULL,
				"[surface] Targeting velocity: "
				"v(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]"
				,
				vecParamsXYZ(bg->vel),
				vecParamsXYZ((S32*)bg->vel)
				);
		}

xdefault:{
		zeroVec3(dirTarget);
		zeroVec3(targetDirXZ);

		bg->flagsMutable.jumpButtonPressed = 0;
		bg->flagsMutable.jumpButtonNotReleased = 0;
		localBG->flagsMutable.tryingToMove = 0;

		mrmLog(	msg,
			NULL,
			"[surface] unknown targetType %u.",
			targetType);
		 }
	}

	localBG->overrideMaxSpeed = 0;

	switch(speedType){
		xcase MST_CONSTANT:{
			// Move to target at constant speed.

			F32 speed = shared->target.pos->speed;

			if(speed <= 0.f){
				zeroVec3(bg->vel);
				localBG->flagsMutable.tryingToMove = 0;

				mrmLog(	msg,
					NULL,
					"[surface] speed (%1.3f) <= 0.",
					speed);
			}
			else if(hasTargetPos){
				Vec3	posCur;
				Vec3	diffToTarget;
				F32		diffLen;
				F32		diffSpeed;

				mrmGetPositionBG(msg, posCur);

				if(shared->target.pos->flags.useY){
					subVec3(targetPos,
						posCur,
						diffToTarget);

					diffLen = lengthVec3(diffToTarget);

					diffSpeed = diffLen / MM_SECONDS_PER_STEP;

					if(diffSpeed > speed){
						F32 scale = speed / diffLen;

						scaleVec3(	diffToTarget,
							scale,
							bg->vel);

						LOG_VEC3(bg->vel);
					}else{
						scaleVec3(	diffToTarget,
							1.f / MM_SECONDS_PER_STEP,
							bg->vel);

						LOG_VEC3(bg->vel);
					}
				}else{
					subVec3XZ(	targetPos,
						posCur,
						diffToTarget);

					diffLen = lengthVec3XZ(diffToTarget);

					diffSpeed = diffLen / MM_SECONDS_PER_STEP;

					if(diffSpeed > speed){
						F32 scale = speed / diffLen;

						scaleVec3XZ(diffToTarget,
							scale,
							bg->vel);

						LOG_VEC3(bg->vel);
					}else{
						scaleVec3XZ(diffToTarget,
							1.f / MM_SECONDS_PER_STEP,
							bg->vel);

						LOG_VEC3(bg->vel);
					}
				}
			}else{
				if(shared->target.pos->flags.useY){
					normalVec3(dirTarget);

					scaleVec3(	dirTarget,
						speed,
						bg->vel);

					LOG_VEC3(bg->vel);
				}else{
					normalVec3XZ(dirTarget);

					scaleVec3XZ(targetDirXZ,
						speed,
						bg->vel);

					LOG_VEC3(bg->vel);
				}
			}

			friction = 0.f;
			traction = 0.f;
			ignoreGravity = 1;

			copyVec3(bg->vel, velTarget);

			mrmLog(	msg,
				NULL,
				"[surface] Speed is constant: "
				"s(%1.2f) [%8.8x]"
				,
				shared->target.pos->speed,
				*(S32*)&shared->target.pos->speed
				);
		}

		xcase MST_IMPULSE:{
			// Calculate an impulse that will cause us to hopefully stop on the target.

			if(hasTargetPos){
				Vec3	posCur;
				F32		len;
				Vec3	offsetToTarget;

				mrmGetPositionBG(msg,
					posCur);

				subVec3XZ(	targetPos,
					posCur,
					offsetToTarget);

				len = lengthVec3XZ(offsetToTarget);

				if(len > 0.0001f){
					F32 speed;
					F32 scale;

					speed = fsqrt(2.f * len * friction * syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed);

					scale = speed / len;

					scaleVec3XZ(offsetToTarget,
						scale,
						bg->vel);

					LOG_VEC3(bg->vel);

					mrmLog(	msg,
						NULL,
						"[surface] Speed is impulse: "
						"s(%1.2f) [%8.8x]"
						,
						speed,
						*(S32*)&speed
						);
				}

				copyVec3(bg->vel, velTarget);
			}
		}

		xcase MST_NORMAL:{
			if(bg->speedPenalty > 0.f)
			{
				F32 maxPenaltySpeed = targetSpeed;
				F32 fSpeedScale;

				if (!bg->flags.speedPenaltyIsStrictScale)
				{	// not a strict scale, treat as a penalty
					fSpeedScale = 1.0f - bg->speedPenalty;
				}
				else
				{	// use speedPenalty as a scale
					fSpeedScale = bg->speedPenalty;
				}
				
				if(bg->flags.offGroundByJumping)
				{
					maxPenaltySpeed = fSpeedScale * sync->jump.maxSpeed;
				}
				else
				{
					maxPenaltySpeed = fSpeedScale * syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed;
				}

				// if it is a speed penalty, don't allow the speed to go over our current targetSpeed
				if (!bg->flags.speedPenaltyIsStrictScale)
				{
					MIN1(targetSpeed, maxPenaltySpeed);
				}
				else
				{
					targetSpeed = maxPenaltySpeed;
				}
			}

			

			if(slow){
				MIN1(targetSpeed, syncPublic->speeds[MR_SURFACE_SPEED_SLOW].speed);
			}

			if(localBG->flags.hasOverrideMaxSpeed){
				MIN1(targetSpeed, localBG->overrides.maxSpeed);
			}

			if(hasTargetPos){
				Vec3 posCur;
				F32 dist;

				mrmGetPositionBG(msg, posCur);

				subVec3XZ(	targetPos,
					posCur,
					targetDirXZ);

				dist = normalVec3(targetDirXZ);

				if(dist < targetSpeed * MM_SECONDS_PER_STEP)
					targetSpeed = dist / MM_SECONDS_PER_STEP;
			}

			// Calculate the backwards speed.

			if(sync->backScale > 0.f){
				F32		yawTargetDir = getVec3Yaw(targetDirXZ);
				Vec2	pyCurFace;
				F32		yawDiff;

				mrmGetFacePitchYawBG(msg, pyCurFace);

				yawDiff = fabs(subAngle(pyCurFace[1], yawTargetDir));

				if(yawDiff > HALFPI){
					F32 scale;

					if(yawDiff >= HALFPI + QUARTERPI){
						scale = sync->backScale;
					}else{
						scale =	1.f +
							(sync->backScale - 1.f) *
							(yawDiff - HALFPI) /
							QUARTERPI;
					}

					targetSpeed *= scale;
				}
			}

			scaleVec3XZ(targetDirXZ,
				targetSpeed,
				velTarget);

			velTarget[1] = 0;

			mrmLog(	msg,
				NULL,
				"[surface] Speed is normal: "
				"targetSpeed(%1.2f) [%8.8x]"
				,
				targetSpeed,
				*(S32*)&targetSpeed
				);
		}

		xcase MST_OVERRIDE:{
			F32 maxSpeed =	shared->target.pos->speed;

			if(slow){
				MIN1(maxSpeed, syncPublic->speeds[MR_SURFACE_SPEED_SLOW].speed);
			}

			localBG->overrideMaxSpeed = maxSpeed;
			targetSpeed = maxSpeed;

			if(hasTargetPos){
				Vec3 posCur;
				F32 dist;

				mrmGetPositionBG(msg, posCur);

				subVec3XZ(	targetPos,
					posCur,
					targetDirXZ);

				dist = normalVec3(targetDirXZ);

				if(dist < targetSpeed * MM_SECONDS_PER_STEP)
					targetSpeed = dist / MM_SECONDS_PER_STEP;
			}

			scaleVec3XZ(targetDirXZ,
				targetSpeed,
				velTarget);

			velTarget[1] = 0;

			mrmLog(	msg,
				NULL,
				"[surface] Speed is override: "
				"ms(%1.2f) [%8.8x]"
				,
				maxSpeed,
				*(S32*)&maxSpeed
				);
		}
	}

	if(!bg->flags.isAtRest)
	{
		bool bJumpButtonPressed = bg->flags.jumpButtonPressed;

		if (localBG->flags.isJumpDisabledOverrideValue || localBG->flags.isJumpDisabled)
		{
			jump = false;
			bJumpButtonPressed = false;
		}

		physicsUpdate(	msg,
						bg,
						localBG,
						sync,
						syncPublic,
						toFG,
						velTarget,
						jump,
						bJumpButtonPressed,
						syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed,
						ignoreGravity,
						traction,
						friction,
						MM_SECONDS_PER_STEP);

		mrSurfaceUpdateMovingStancesBG(msg, bg, localBG, syncPublic);
	}else{
		localBG->flagsMutable.hitGroundDuringTranslate = 0;
		bg->flagsMutable.touchingSurface = 1;

		if(mrmMoveIfCollidingWithOthersBG(msg)){
			mrSurfaceSetNotOnGroundBG(msg, bg, localBG, "at rest, collided with other mm");
			mrSurfaceSetIsNotAtRestBG(msg, bg, "collided with other mm");
		}
	}
}

static void mrSurfaceHandleCreateOutputRotationTarget(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG,
	SurfaceSync* sync,
	SurfaceSyncPublic* syncPublic,
	SurfaceToFG* toFG)
{
	const MovementRequesterMsgCreateOutputShared*	shared = msg->in.bg.createOutput.shared;
}

static void mrSurfaceUpdateIsTurningBG(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG)
{
	bool isTurning = mrmGetInputValueBitBG(msg, MIVI_BIT_TURN_LEFT) ||
		mrmGetInputValueBitBG(msg, MIVI_BIT_TURN_RIGHT);

	if(isTurning != (bool)bg->flags.isTurning){
		bg->flagsMutable.isTurning = isTurning;

		mrSurfaceInputDirIsDirtyBG(	msg,
			localBG,
			isTurning ?
			"turning started" :
		"turning stopped");
	}
}

static bool mrSurfaceIsStrafing(SurfaceLocalBG* localBG, SurfaceSync* sync)
{
	if (sync->flags.strafeOverrideEnabled)
		return sync->flags.strafeOverride;

	return (localBG->flags.isStrafingOverrideValue	||
			localBG->flags.isStrafingOverride		|| 
			(	sync->flags.isStrafing &&
				(	!gConf.bUseNWOMovementAndAnimationOptions ||
					sync->flags.inCombat)));
}

static void mrSurfaceHandleCreateOutputRotationChange(	const MovementRequesterMsg* msg,
														SurfaceBG* bg,
														SurfaceLocalBG* localBG,
														SurfaceSync* sync,
														SurfaceSyncPublic* syncPublic,
														SurfaceToFG* toFG)
{
	const MovementRequesterMsgCreateOutputShared*	shared = msg->in.bg.createOutput.shared;

	F32 pitchTarget = 0.f;

	bool fastYaw =
		sync->flags.inCombat &&
		shared->target.rot->targetType != MRTT_INPUT &&
		gConf.bNewAnimationSystem;

	if(bg->flags.sticking)
	{
		Vec3 dirFace;
		Vec3 pyr;

		scaleVec3(bg->surfaceNormal, -1, dirFace);

		pyr[0] = 0;
		pyr[1] = getVec3Yaw(dirFace);
		pyr[2] = 0;

		PYRToQuat(pyr, bg->rotTarget);

		if (!FINITEQUAT(bg->rotTarget))
		{
			mrmLog(	msg,
					NULL,
					"ERROR: rotTarget is not finite: (%f, %f, %f, %f) [%8.8x, %8.8x, %8.8x, %8.8x]",
					quatParamsXYZW(bg->rotTarget),
					quatParamsXYZW((S32*)bg->rotTarget));
			zeroQuat(bg->rotTarget);
		}
		bg->yawFaceTarget = pyr[1];
	}
	else
	{
		if(shared->target.rot->targetType == MRTT_INPUT){
			if(TRUE_THEN_RESET(bg->flagsMutable.turnBecomesStrafe)){
				mrSurfaceUpdateIsTurningBG(msg, bg, localBG);
			}
		}
		else if(FALSE_THEN_SET(bg->flagsMutable.turnBecomesStrafe)){
			bg->flagsMutable.isTurning = 0;

			mrSurfaceInputDirIsDirtyBG(msg, localBG, "enabling turnBecomesStafe");
		}

		switch(shared->target.rot->targetType){
			xcase MRTT_INPUT:{
				if (!bg->flags.holdForFlourish ||
					!bg->flags.ownsAnimation)
				{
					Vec3 dir;

					if (mrSurfaceIsStrafing(localBG, sync))
					{
						bg->yawFaceTarget = bg->inputFaceYaw;
					}
					else
					{
						mrSurfaceGetInputDirBG(msg, bg, localBG, dir);

						if(!vec3IsZeroXZ(dir))
							bg->yawFaceTarget = getVec3Yaw(dir);
					}
				}

				pitchTarget = mrmGetInputValueF32BG(msg, MIVI_F32_PITCH);
			}

			xcase MRTT_DIRECTION:{
				Vec3 dir;

				copyVec3(	shared->target.rot->dir,
							dir);

				if(!vec3IsZeroXZ(dir)){
					getVec3YP(dir, &bg->yawFaceTarget, &pitchTarget);
				}
			}

			xcase MRTT_ROTATION:{
				Vec3 z;

				quatToMat3_2(shared->target.rot->rot, z);

				getVec3YP(z, &bg->yawFaceTarget, &pitchTarget);
			}

			xcase MRTT_POINT:{
				Vec3 posCur;
				Vec3 dir;

				mrmGetPositionBG(msg, posCur);

				subVec3(shared->target.rot->point,
					posCur,
					dir);

				if(!vec3IsZeroXZ(dir)){
					getVec3YP(dir, &bg->yawFaceTarget, &pitchTarget);
				}
			}

			xcase MRTT_ENTITY:{
			}
		}

		switch(shared->target.rot->speedType){
			xcase MST_NORMAL:{
			}

			xcase MST_CONSTANT:{
			}

			xcase MST_IMPULSE:{
			}
		}
	}

	{
		Vec3	pyCurFace;
		F32		pitchDiff;
		F32		yawDiff;

		// Calculate the face pitch and yaw.

		mrmGetFacePitchYawBG(msg, pyCurFace);
		pitchDiff = subAngle(pitchTarget,       pyCurFace[0]);
		yawDiff   = subAngle(bg->yawFaceTarget, pyCurFace[1]);

		if (shared->target.rot->targetType == MRTT_INPUT)
		{
			if (FALSE_THEN_SET(bg->flagsMutable.pitchDiffInput)) {
				bg->pitchDiffInterpScale = 0.0f;
			}

			if (!bg->flags.holdForFlourish ||
				!bg->flags.ownsAnimation)
			{
				pitchDiff *= interpF32(	bg->pitchDiffInterpScale, 
										MR_SURFACE_DEFAULT_PITCH_DIFF_MULT,
										sync->pitchDiffMult);

				bg->pitchDiffInterpScale += 0.05f;
				MIN1F(bg->pitchDiffInterpScale, 1.0f);
			}
			else {
				pitchDiff = 0.f;
			}	
		}
		else
		{
			pitchDiff *= MR_SURFACE_DEFAULT_PITCH_DIFF_MULT;
			bg->flagsMutable.pitchDiffInput = 0;
		}


		// turn the facing 
		
		switch (shared->target.rot->turnRateType)
		{
			xcase MTRT_OVERRIDE:
			{
				yawDiff = mrmCalculateTurningStep(	yawDiff, 
													g_fFaceNormalBasis, 
													shared->target.rot->turnRate, 
													shared->target.rot->turnRate, 
													shared->target.rot->turnRate);
			}
			xcase MTRT_NORMAL:
			default:
			{
				SurfaceMovementTurnDef *pTurnDef = (sync->flags.useSyncTurnDef ? &sync->turn : &g_defaultSurfaceTurnDef);
				F32 fTurnRate = pTurnDef->fFaceTurnRate;

				if (sync->turnRateScale > 0.f)
					fTurnRate *= sync->turnRateScale;

				if (fastYaw)
					fTurnRate *= (sync->turnRateScaleFast <= 0) ? g_fFaceFastRotScale : sync->turnRateScaleFast;
				if (!bg->flags.onGround)
					fTurnRate *= g_fFaceInAirRotScale;

				yawDiff = mrmCalculateTurningStep(	yawDiff, 
													g_fFaceNormalBasis, 
													fTurnRate, 
													pTurnDef->fFaceMinTurnRate, 
													pTurnDef->fFaceMaxTurnRate);
			}
		}

		pyCurFace[0] = addAngle(pyCurFace[0], pitchDiff);
		pyCurFace[1] = addAngle(pyCurFace[1], yawDiff);
		
		mrmSetFacePitchYawBG(msg, pyCurFace);

		// Calculate the rotation.

		if(!bg->flags.sticking){
			if(	sync->flags.orientToSurface &&
				bg->flags.onGround &&
				bg->surfaceNormal[1] > 0.4f)
			{
				Mat3 mat;

				copyVec3(bg->surfaceNormal, mat[1]);

				if(	localBG->flags.tryingToMove &&
					lengthVec3Squared(localBG->velTarget) >= SQR(0.001f))
				{
					copyVec3(localBG->velTarget, mat[2]);
					normalVec3(mat[2]);
				}else{
					quatToMat3_2(bg->rotTarget, mat[2]);
				}

				crossVec3(mat[1], mat[2], mat[0]);
				normalVec3(mat[0]);
				crossVec3(mat[0], mat[1], mat[2]);
				mat3ToQuat(mat, bg->rotTarget);

				if (!FINITEQUAT(bg->rotTarget))
				{
					mrmLog(	msg,
							NULL,
							"ERROR: rotTarget is not finite: (%f, %f, %f, %f) [%8.8x, %8.8x, %8.8x, %8.8x]",
							quatParamsXYZW(bg->rotTarget),
							quatParamsXYZW((S32*)bg->rotTarget));
					zeroQuat(bg->rotTarget);
				}
			}
			else if(localBG->flags.tryingToMove){
				// Trying to move, so rotate to match target vel.

				yawQuat(-getVec3Yaw(localBG->velTarget),
					bg->rotTarget);

				if (!FINITEQUAT(bg->rotTarget))
				{
					mrmLog(	msg,
							NULL,
							"ERROR: rotTarget is not finite: (%f, %f, %f, %f) [%8.8x, %8.8x, %8.8x, %8.8x]",
							quatParamsXYZW(bg->rotTarget),
							quatParamsXYZW((S32*)bg->rotTarget));
					zeroQuat(bg->rotTarget);
				}
			}
		}

#if 1
		{
			Quat	rotCur;
			Quat	rotCurOrig;
			Vec3	yVecCur;
			Vec3	zVecCur;
			Vec3	zVecCurProj;
			Vec3	yVecTarget;
			Vec3	zVecTarget;
			Vec3	zVecTargetProj;
			Mat3	matNew;
			F32		radTotal;

			mrmGetRotationBG(msg, rotCurOrig);
			copyQuat(rotCurOrig, rotCur);

			quatToMat3_1(rotCur, yVecCur);
			quatToMat3_2(rotCur, zVecCur);
			quatToMat3_1(bg->rotTarget, yVecTarget);
			quatToMat3_2(bg->rotTarget, zVecTarget);

			if(sameVec3((S32*)yVecCur, (S32*)yVecTarget)){
				copyVec3((S32*)yVecTarget, (S32*)matNew[1]);
				copyVec3((S32*)zVecCur, (S32*)zVecCurProj);
				copyVec3((S32*)zVecTarget, (S32*)zVecTargetProj);
			}else{
				rotateUnitVecTowardsUnitVec(yVecCur,
					yVecTarget,
					bg->flags.onGround ? 0.3f : 0.1f,
					matNew[1]);

				// Check if the projected z target is close enough to projected z current.

				projectVecOntoPlane(zVecTarget,
					matNew[1],
					zVecTargetProj);

				normalVec3(zVecTargetProj);

				projectVecOntoPlane(zVecCur,
					matNew[1],
					zVecCurProj);

				normalVec3(zVecCurProj);
			}

			radTotal = acosf(CLAMPF32(dotVec3(zVecTargetProj, zVecCurProj), -1.f, 1.f));

			if(radTotal <= 0.001f){
				// Close enough, snap to target.

				copyVec3(zVecTargetProj, matNew[2]);
			}else{
				// Interp to target, with quarter-pi snapping.
				F32 useRotYawScale = g_fRotationYawScale;
				F32 radRemainder;

				if(radTotal <= HALFPI){
					radRemainder = radTotal;
				}else{
					radRemainder = fmod(radTotal, QUARTERPI);

					if(radRemainder >= 0.5f * QUARTERPI){
						radRemainder -= QUARTERPI;
					}
				}

				radRemainder *= bg->flags.onGround ?
useRotYawScale :
				useRotYawScale + (1.f - useRotYawScale) * 0.3333f;

				rotateUnitVecTowardsUnitVec(zVecCurProj,
					zVecTargetProj,
					1.f - radRemainder / radTotal,
					matNew[2]);
			}

			if(normalVec3(matNew[2]) <= 0.f){
				// This is a really lazy way to handle this condition.

				copyMat3(unitmat, matNew);
			}else{
				crossVec3(matNew[1], matNew[2], matNew[0]);
			}

			// Convert to Quat and set it.

			mat3ToQuat(matNew, rotCur);

			assert(FINITEQUAT(rotCur));

			mrmSetRotationBG(msg, rotCur);
		}
#else
		mrmSetRotationBG(msg, bg->rotTarget);
#endif
	}
}

static void playGroundImpactFX(const MovementRequesterMsg* msg){
	MMRFxConstant	constant = {0};
	S32				noCollision = 0;

	if(	mrmGetNoCollBG(msg, &noCollision) &&
		noCollision)
	{
		return;
	}

	mrmGetPositionBG(msg, constant.vecTarget);
	constant.fxName = "FX_Generic_Player_FallFX";
	unitQuat(constant.quatTarget);

	mmrFxCreateBG(msg, NULL, &constant, NULL);

	mrmLog(	msg,
		NULL,
		"[surface] Ground impactFX:"
		"t(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]",
		vecParamsXYZ(constant.vecTarget),
		vecParamsXYZ((S32*)constant.vecTarget));

	{
		Vec3 posStart;

		copyVec3(constant.vecTarget, posStart);
		posStart[1] += 3.0f;

		mrmLogSegment(	msg,
			NULL,
			"surface.groundImpact",
			0xFF11FF11,
			constant.vecTarget,
			posStart);
	}
}

static void mrSurfaceHandleCreateOutputAnimation(	const MovementRequesterMsg* msg,
	SurfaceBG* bg,
	SurfaceLocalBG* localBG,
	SurfaceSync* sync,
	SurfaceSyncPublic* syncPublic,
	SurfaceToFG* toFG)
{
	const MovementRequesterMsgCreateOutputShared*	shared = msg->in.bg.createOutput.shared;

	Vec3 velTarget;

	if(bg->flags.isAtRest){
		return;
	}

	if(bg->flags.sticking){
		mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("WALLCLING", 0));
	}else{
		copyVec3(	localBG->velTarget,
			velTarget);

		// Setting whether to use running or sprinting or walking

		if(	!bg->flags.onGround ||
			bg->surfaceNormal[1] < 0.5f)
		{
			// Add in-air bits if falling or on a steep slope.

			//in a jump's apex (top part between rising & falling)
			if(bg->flags.offGroundByJumping &&
				fabs(bg->vel[1]) <= fabs(jumpApexTrim*((SurfaceSyncPublic*)(msg->in.userStruct.syncPublic))->gravity)){
					if (gConf.bNewAnimationSystem){
						mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_JumpApex", 0));
					}else{
						mrmAnimAddBitBG(msg, mmAnimBitHandles.jumpApex);
					}
			}

			if(bg->vel[1] <= 0.0f){
				if(bg->flags.offGroundByJumping){
					// Jumping, on the way down.

					if(gConf.bNewAnimationSystem){
						mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Falling", 0));
					}else{
						mrmAnimAddBitBG(msg, mmAnimBitHandles.air);
						mrmAnimAddBitBG(msg, mmAnimBitHandles.jump);
						mrmAnimAddBitBG(msg, mmAnimBitHandles.falling);
					}

					bg->flagsMutable.wasOffGroundAnim = 1;
				}
				else if(bg->vel[1] < bg->onGroundVelY + syncPublic->gravity * 0.3f){
					// Walked off a ledge that's high enough to trigger the falling anim.

					if(gConf.bNewAnimationSystem){
						mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Falling", 0));
					}else{
						mrmAnimAddBitBG(msg, mmAnimBitHandles.air);
						mrmAnimAddBitBG(msg, mmAnimBitHandles.jump);
						mrmAnimAddBitBG(msg, mmAnimBitHandles.falling);
					}

					bg->flagsMutable.wasOffGroundAnim = 1;
				}

				if(bg->vel[1] < -speedToTriggerFastFall){
					if(gConf.bNewAnimationSystem){
						mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_FallFast", 0));
					}else{
						mrmAnimAddBitBG(msg, mmAnimBitHandles.fallFast);
					}

					bg->flagsMutable.fastFalling = 1;
				}
			}
			else if(bg->flags.offGroundByJumping &&
				!TRUE_THEN_RESET(bg->flagsMutable.justLanded))
			{
				// Jumping, on the way up.

				if(gConf.bNewAnimationSystem){
					mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Jumping", 0));
				}else{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.air);
					mrmAnimAddBitBG(msg, mmAnimBitHandles.rising);
					mrmAnimAddBitBG(msg, mmAnimBitHandles.jump);
				}
			}
		}
		else if(TRUE_THEN_RESET(bg->flagsMutable.wasOffGroundAnim)){
			bg->flagsMutable.justLanded = 1;

			if(gConf.bNewAnimationSystem){
				mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Falling", 0));
			}else{
				mrmAnimAddBitBG(msg, mmAnimBitHandles.air);
				mrmAnimAddBitBG(msg, mmAnimBitHandles.jump);
				mrmAnimAddBitBG(msg, mmAnimBitHandles.falling);
			}

			if(TRUE_THEN_RESET(bg->flagsMutable.fastFalling)){
				if(gConf.bNewAnimationSystem){
					mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_FallFast", 0));
				}else{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.fallFast);
				}
				playGroundImpactFX(msg);
			}
		}else{
			bg->flagsMutable.justLanded = 0;
		}

		if(bg->flags.hasAdditiveVel && bg->flags.setRepelAnim){
			if(!gConf.bNewAnimationSystem){
				mrmAnimAddBitBG(msg, mmAnimBitHandles.repel);
			}
		}

		if(	localBG->flags.tryingToMove &&
			!vec3IsZeroXZ(velTarget))
		{
			const F32 curSpeedXZ = lengthVec3XZ(velTarget);

			if(gConf.bNewAnimationSystem){
				mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Moving", 0));
			}else{
				mrmAnimAddBitBG(msg, mmAnimBitHandles.move);
				mrmAnimAddBitBG(msg, mmAnimBitHandles.forward);
			}

			if(curSpeedXZ > syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed + 0.5f){
				// The entity is cheating its movement speed past the fastest speed.
				// This is a special case where we will just set both the trot and run bits.

				if(gConf.bNewAnimationSystem){
					mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Trotting", 0));
					mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Running", 0));
				}else{
					mrmAnimAddBitBG(msg, mmAnimBitHandles.trot);
					mrmAnimAddBitBG(msg, mmAnimBitHandles.run);
				}
			}else{
				S32	lastGoodSpeedIndex = 0;
				F32	lastGoodSpeed = 0.f;

				ARRAY_FOREACH_BEGIN(syncPublic->speeds, i);
				{
					const F32 speed = syncPublic->speeds[i].speed;

					if(speed == lastGoodSpeed){
						continue;
					}

					if(	curSpeedXZ >= lastGoodSpeed &&
						curSpeedXZ <= speed
						||
						i == ARRAY_SIZE(syncPublic->speeds) - 1)
					{
						F32 diff = speed - lastGoodSpeed;

						// Found the speed I want, or it's the last speed.

						if(curSpeedXZ >= lastGoodSpeed + diff * 0.5f ){
							lastGoodSpeedIndex = i;
						}

						break;
					}

					lastGoodSpeed = speed;
					lastGoodSpeedIndex = i;
				}
				ARRAY_FOREACH_END;

				switch(lastGoodSpeedIndex){
					xcase MR_SURFACE_SPEED_MEDIUM:{
						if(gConf.bNewAnimationSystem){
							mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Trotting", 0));
						}else{
							mrmAnimAddBitBG(msg, mmAnimBitHandles.trot);
						}
					}

					xcase MR_SURFACE_SPEED_FAST:{
						if(gConf.bNewAnimationSystem){
							mrmAnimAddBitBG(msg, mmGetAnimBitHandleByName("Stance_Running", 0));
						}else{
							mrmAnimAddBitBG(msg, mmAnimBitHandles.run);
						}
					}
				}
			}
		}
	}
}

static void hitSurface(	const MovementRequesterMsg* msg,
	SurfaceLocalBG* localBG,
	SurfaceBG* bg,
	SurfaceSync* sync,
	const SurfaceSyncPublic* syncPublic, 
	const Vec3 entPos,
	const Vec3 hitPos,
	const Vec3 hitSurfaceNormal)
{
	if(hitSurfaceNormal[1] > 0.f){
		mrmLog(	msg,
			NULL,
			"[surface] On the ground.");

		bg->flagsMutable.touchingSurface = 1;

		copyVec3(	hitSurfaceNormal,
			bg->surfaceNormalMutable);
	}

	if(	hitSurfaceNormal[1] > cosf(hiSlipSlopeAngle)
		&&
		(	!bg->flags.offGroundByJumping ||
		bg->vel[1] <= 0.f)
		)
	{
		mrmLog(	msg,
			NULL,
			"[surface] On the ground.");

		if(FALSE_THEN_SET(localBG->flagsMutable.hitGroundDuringTranslate)){
			// If the player is going fast enough when he hits the ground, shake the camera.

			mrmLog(	msg,
				NULL,
				"[surface] First hitSurface during translate.");

			if(bg->vel[1] < -speedToTriggerFastFall){
				SurfaceToFG* toFG = msg->in.userStruct.toFG;

				mrmLog(	msg,
					NULL,
					"[surface] Setting doCameraShake.");

				mrmEnableMsgUpdatedToFG(msg);
				toFG->flags.doCameraShake = 1;
			}

			// Backup the speed and direction.

			if(bg->flags.wasOffGroundMotion){
				bg->storedSpeed = lengthVec3XZ(bg->vel);
				bg->yawStoredSpeed = getVec3Yaw(bg->vel);
				mrmGetProcessCountBG(msg, &bg->spcStoredSpeed);

				mrmLog(	msg,
					NULL,
					"[surface] Storing impact speed (%1.3f) yaw (%1.3f) and spc (%u).",
					bg->storedSpeed,
					bg->yawStoredSpeed,
					bg->spcStoredSpeed);
			}

			// If going fast enough on impact, inform the foreground

			if(bg->vel[1] < -speedToTriggerGroundImpactNotification){
				SurfaceToFG*	toFG = msg->in.userStruct.toFG;
				Vec3			velNormalized;
				F32				cosAngle;
				F32				speed;

				copyVec3(bg->vel, velNormalized);
				speed = normalVec3(velNormalized);
				cosAngle = dotVec3(velNormalized, hitSurfaceNormal);

				if(cosAngle < 0){
					mrmEnableMsgUpdatedToFG(msg);
					MAX1(toFG->surfaceImpactSpeed, -cosAngle * speed);

					mrmLog(	msg,
						NULL,
						"[surface] Sending impact speed (%1.3f) to FG.",
						toFG->surfaceImpactSpeed);
				}		
			}

			if(	sync->flags.canStick &&
				bg->flags.offGroundByJumping &&
				hitSurfaceNormal[1] < sinf(RAD(90.f) - 0.5f * (hiSlipSlopeAngle + loSlipSlopeAngle)))
			{
				// Stick to the surface.

				bg->flagsMutable.sticking = 1;
				bg->stickingFrameCount = 0;

				zeroVec3(bg->vel);
			}else{

				if(bg->flags.hasAdditiveVel)
				{
					projectVecOntoPlane(bg->velAdditive,
										hitSurfaceNormal,
										bg->velAdditive);
				}

				if(	!bg->flags.wasOffGroundMotion || 
					hitSurfaceNormal[1] < cosf(0.5f * (hiSlipSlopeAngle + loSlipSlopeAngle))) 
				{
					projectVecOntoPlane(bg->vel,
						hitSurfaceNormal,
						bg->vel);

					LOG_VEC3(bg->vel);
				}else{
					S32 tryingToJump =	!bg->flags.isJumping &&
						(	hitSurfaceNormal[1] >= 0.5f ||
						bg->flags.sticking) &&
						bg->flags.jumpButtonPressed; 

					if(	!localBG->flags.tryingToMove &&
						!tryingToJump)
					{
						zeroVec3(bg->vel);
					}else{
						F32 speedSQR;
						F32 maxSpeed = FIRST_IF_SET(localBG->overrideMaxSpeed,
							syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed);

						projectVecOntoPlane(bg->vel, hitSurfaceNormal, bg->vel);

						LOG_VEC3(bg->vel);

						speedSQR = lengthVec3Squared(bg->vel);

						if(speedSQR > SQR(maxSpeed + 2.f)){
							F32 scaleFactor = maxSpeed / fsqrt(speedSQR);

							scaleVec3(bg->vel, scaleFactor, bg->vel);
						}
					}
				} 

				mrmLogSegmentOffset(msg,
					NULL,
					"surface.hitNormal",
					0xffffff00,
					hitPos,
					hitSurfaceNormal);

				mrmLogSegmentOffset(msg,
					NULL,
					"surface.hitProjectedVel",
					0xffffff00,
					hitPos,
					bg->vel);
			}
		}

		if(FALSE_THEN_SET(bg->flagsMutable.onGround)){
			if (gConf.bNewAnimationSystem && sync->flags.spawnedOnGround)
			{
				mrmAnimResetGroundSpawnBG(msg);
				if ((bg->flagsMutable.hasStanceMask & BIT(MR_SURFACE_STANCE_RISING  )) ||
					(bg->flagsMutable.hasStanceMask & BIT(MR_SURFACE_STANCE_FALLING )) ||
					(bg->flagsMutable.hasStanceMask & BIT(MR_SURFACE_STANCE_JUMPING )) ||
					(bg->flagsMutable.hasStanceMask & BIT(MR_SURFACE_STANCE_JUMPAPEX)) ) 
				{
					mrSurfaceDestroyOffGroundStancesBG(msg, bg, localBG);
					mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_LANDED);
				}
			} else {
				mrSurfaceDestroyOffGroundStancesBG(msg, bg, localBG);
				mrSurfaceStanceSetBG(msg, bg, localBG, MR_SURFACE_STANCE_LANDED);
			}
		}

		bg->flagsMutable.wasOffGroundMotion = 0;
		bg->flagsMutable.offGroundByJumping = 0;
	}
	else if(dotVec3(hitSurfaceNormal, bg->vel) < 0.f){
		// Hit a surface that I'm going towards, so project vel onto the surface.

		projectVecOntoPlane(bg->vel,
			hitSurfaceNormal,
			bg->vel);

		LOG_VEC3(bg->vel);

		mrmLogSegmentOffset(msg,
			NULL,
			"surface.hitNormal",
			0xffffff00,
			hitPos,
			hitSurfaceNormal);

		mrmLogSegmentOffset(msg,
			NULL,
			"surface.hitProjectedVel",
			0xffffff00,
			hitPos,
			bg->vel);
	}
}

static void mrSurfaceGetBitsToAcquireBG(const SurfaceBG* bg,
										const SurfaceSync* sync,
										U32* bitsToAcquireOut)
{
	*bitsToAcquireOut = MDC_BIT_POSITION_CHANGE |
						MDC_BIT_ROTATION_CHANGE |
						(bg->flags.isAtRest ? 0 : MDC_BIT_ANIMATION) |
						(bg->flags.holdForFlourish ? MDC_BIT_ANIMATION : 0) |
						(sync->test.jump.doJumpTest ? MDC_BIT_POSITION_TARGET : 0);
}

static void mrSurfaceDiscussDataOwnershipBG(const MovementRequesterMsg* msg,
											SurfaceBG* bg,
											SurfaceLocalBG* localBG,
											const SurfaceSync* sync)
{
	S32 doneDiscussing = 1;
	U32 bitsToAcquire;
	U32 acquiredBits;
	U32 ownedBits = 0;

	// Process speed penalty queue.

	if(eaSize(&localBG->speedPenaltiesQueued)){
		mrmLog(	msg,
			NULL,
			"Checking %u queued speed penalties.",
			eaSize(&localBG->speedPenaltiesQueued));

		EARRAY_CONST_FOREACH_BEGIN(localBG->speedPenaltiesQueued, i, isize);
		{
			SurfaceSpeedPenalty* spNew = localBG->speedPenaltiesQueued[i];

			if(!mrmProcessCountHasPassedBG(msg, spNew->spc)){
				continue;
			}

			eaRemove(&localBG->speedPenaltiesQueued, i);
			i--;
			isize--;

			if(spNew->flags.isStop){
				EARRAY_CONST_FOREACH_BEGIN(localBG->speedPenaltiesActive, j, jsize);
				{
					SurfaceSpeedPenalty* sp = localBG->speedPenaltiesActive[j];

					if(sp->id == spNew->id){
						eaRemove(&localBG->speedPenaltiesActive, j);
						StructDestroySafe(parse_SurfaceSpeedPenalty, &sp);
						StructDestroySafe(parse_SurfaceSpeedPenalty, &spNew);
						break;
					}
				}
				EARRAY_FOREACH_END;

				if(spNew){
					// Didn't find in active list, maybe in the queue because spcs are off.

					EARRAY_CONST_FOREACH_BEGIN(localBG->speedPenaltiesQueued, j, jsize);
					{
						SurfaceSpeedPenalty* sp = localBG->speedPenaltiesQueued[j];

						if(sp->id == spNew->id){
							if(j <= i){
								i--;
							}
							isize--;

							eaRemove(&localBG->speedPenaltiesQueued, j);
							StructDestroySafe(parse_SurfaceSpeedPenalty, &sp);
							StructDestroySafe(parse_SurfaceSpeedPenalty, &spNew);
							break;
						}
					}
					EARRAY_FOREACH_END;
				}

				StructDestroySafe(parse_SurfaceSpeedPenalty, &spNew);
			}else{
				eaPush(&localBG->speedPenaltiesActive, spNew);
			}
		}
		EARRAY_FOREACH_END;

		mrmLog(	msg,
			NULL,
			"Done checking queued speed penalties (%u remain).",
			eaUSize(&localBG->speedPenaltiesQueued));

		if(eaSize(&localBG->speedPenaltiesQueued)){
			doneDiscussing = 0;
		}

		if(eaSize(&localBG->speedPenaltiesActive)){
			const SurfaceSpeedPenalty* sp = eaTail(&localBG->speedPenaltiesActive);

			ANALYSIS_ASSUME(sp);

			bg->speedPenalty = sp->penalty;
			bg->flagsMutable.speedPenaltyIsStrictScale = sp->flags.isStrictScale;

			mrmLog(	msg,
				NULL,
				"Set speed penalty to %1.3f [%8.8x].",
				bg->speedPenalty,
				*(U32*)&bg->speedPenalty);
		}
		else if(bg->speedPenalty > 0.f){
			bg->speedPenalty = 0.f;

			mrmLog(	msg,
				NULL,
				"Removed speed penalty.");
		}
	}

	// Figure out which bits we want, and then acquire them.

	if (sync->flags.isDisabled){
		mrmReleaseAllDataOwnershipBG(msg);
		mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		return;
	}

	mrSurfaceGetBitsToAcquireBG(bg, sync, &bitsToAcquire);

	if(mrmAcquireDataOwnershipBG(msg, bitsToAcquire, 0, &acquiredBits, &ownedBits)){
		// Acquired something new.

		if(acquiredBits){
			mrSurfaceSetIsNotAtRestBG(msg, bg, "acquired ownership");
			doneDiscussing = 0;

			if(acquiredBits & MDC_BIT_POSITION_CHANGE){
				bg->flagsMutable.preCheckIfOnGround = 1;
				bg->flagsMutable.sticking = 0;
				bg->flagsMutable.fastFalling = 0;
			}

			if(acquiredBits & MDC_BIT_ANIMATION){
				bg->flagsMutable.ownsAnimation = 1;
			}
		}
	}

	// Release bits I don't want.

	if(~bitsToAcquire & ownedBits){
		mrmReleaseDataOwnershipBG(msg, ~bitsToAcquire);
	}

	// If I didn't acquire everything I want, keep discussing.

	if(~ownedBits & bitsToAcquire){
		doneDiscussing = 0;
	}

	// Stop discussing if nothing still wants it.

	if(doneDiscussing){
		mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
	}
}

static void mrSurfaceHandleInputEventBG(const MovementRequesterMsg* msg,
										SurfaceBG* bg,
										SurfaceLocalBG* localBG)
{
	const MovementInputValueIndex mivi = msg->in.bg.inputEvent.value.mivi;

	mrSurfaceInputDirIsDirtyBG(msg, localBG, "input");

	if(	mivi >= MIVI_BIT_LOW &&
		mivi < MIVI_BIT_HIGH)
	{
		mrSurfaceSetIsNotAtRestBG(msg, bg, "input bit changed");
	}

	switch(msg->in.bg.inputEvent.value.mivi){
		xcase MIVI_BIT_UP:{
			bg->flagsMutable.jumpButtonPressed = msg->in.bg.inputEvent.value.bit;

			bg->flagsMutable.jumpButtonNotReleased = 0;
		}

		xcase MIVI_F32_MOVE_YAW:{
			if(!bg->flags.isTurning){
				bg->inputMoveYaw = msg->in.bg.inputEvent.value.f32;
			}
		}

		xcase MIVI_F32_FACE_YAW:{
			if(!bg->flags.isTurning){
				bg->inputFaceYaw = msg->in.bg.inputEvent.value.f32;
			}
		}

		xcase MIVI_BIT_TURN_LEFT:
		acase MIVI_BIT_TURN_RIGHT:{
			if(!bg->flags.turnBecomesStrafe){
				mrSurfaceUpdateIsTurningBG(msg, bg, localBG);
			}
		}
	}
}

// This function is one of several handlers.  It's basically a big virtual table for a plug-in system.
// It can be called from either the foreground or the background thread.
void mrSurfaceMsgHandler(const MovementRequesterMsg* msg){
	SurfaceFG*			fg;
	SurfaceBG*			bg;
	SurfaceLocalBG*		localBG;
	SurfaceToFG*		toFG;
	SurfaceToBG*		toBG;
	SurfaceSync*		sync;
	SurfaceSyncPublic*	syncPublic;

	MR_MSG_HANDLER_GET_DATA_DEFAULT_PUBLIC(msg, Surface);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_AFTER_SYNC:{
			if(TRUE_THEN_RESET(sync->test.setVel.useVel)){
				mrmEnableMsgUpdatedSyncFG(msg);
			}

			if(TRUE_THEN_RESET(sync->test.jump.doJumpTest)){
				mrmEnableMsgUpdatedSyncFG(msg);
			}
		}

		xcase MR_MSG_FG_CREATE_TOBG:{
			if(fg->toBG.speedPenalties)
			{
				mrmEnableMsgUpdatedToBG(msg);
				assert(!toBG->speedPenalties);
				toBG->speedPenalties = fg->toBG.speedPenalties;
				fg->toBG.speedPenalties = NULL;
			}

			if(fg->toBG.spcStrafingOverride)
			{
				toBG->spcStrafingOverride = fg->toBG.spcStrafingOverride;
				toBG->scheduledStrafingOverride = fg->toBG.scheduledStrafingOverride;
				fg->toBG.spcStrafingOverride = 0;
			}
			if(fg->toBG.spcDisableJump)
			{
				toBG->spcDisableJump = fg->toBG.spcDisableJump;
				toBG->scheduledDisableJump = fg->toBG.scheduledDisableJump;
				fg->toBG.spcDisableJump = 0;
			}
		}

		xcase MR_MSG_FG_UPDATED_TOFG:{
			if(TRUE_THEN_RESET(toFG->flags.doCameraShake)){
				fg->flags.doCameraShake = 1;
			}

			if(toFG->surfaceImpactSpeed){
				Entity* e;
				MAX1(fg->surfaceImpactSpeed, toFG->surfaceImpactSpeed);
				if(mrmGetManagerUserPointerFG(msg, &e)){
					// make sure the entity is valid to apply falling damage.
					// checking the entity's myEntityType is not GLOBALTYPE_NONE to filter out client only entities
					// which currently are GLOBALTYPE_NONE
					if(e->pChar && e->myEntityType != GLOBALTYPE_NONE)
						character_ApplyFalling(e->pChar,toFG->surfaceImpactSpeed);
				}
				toFG->surfaceImpactSpeed = 0;
			}

			if(TRUE_THEN_RESET(toFG->flags.hasOnGround)){
				fg->flags.onGround = toFG->flags.onGround;
			}
		}

		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_INPUT_EVENT |
									MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;

			mrmHandledMsgsAddBG(msg, handledMsgs);

			{
				Vec3 pyr;

				mrmGetRotationBG(msg, bg->rotTarget);
				quatToPYR(bg->rotTarget, pyr);

				bg->yawFaceTarget = pyr[1];
			}

			if(gConf.bNewAnimationSystem){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_OUTPUT_ANIMATION);
			}
		}

		xcase MR_MSG_BG_FORCE_CHANGED_POS:{
			zeroVec3(bg->vel);
			mrSurfaceSetNotOnGroundBG(msg, bg, localBG, "force changed pos");
			mrSurfaceSetIsNotAtRestBG(msg, bg, "force changed pos");
		}

		xcase MR_MSG_BG_FORCE_CHANGED_ROT:{
			Vec3 pyr;

			mrmGetRotationBG(msg, bg->rotTarget);
			quatToPYR(bg->rotTarget, pyr);

			bg->yawFaceTarget = pyr[1];
		}

		xcase MR_MSG_BG_PREDICT_DISABLED:{
			ZeroStruct(localBG);
			ZeroStruct(bg);
			mrmAnimStanceDestroyPredictedBG(msg);
		}

		xcase MR_MSG_BG_PREDICT_ENABLED:{
			Vec2 pyFace; 
			mrmGetFacePitchYawBG(msg, pyFace);
			mrmGetRotationBG(msg, bg->rotTarget);
			bg->yawFaceTarget = pyFace[1];
		}

		xcase MR_MSG_BG_QUERY_ON_GROUND:{
			msg->out->bg.queryOnGround.onGround = bg->flags.onGround;
			copyVec3(bg->surfaceNormal, msg->out->bg.queryOnGround.normal);
		}

		xcase MR_MSG_BG_QUERY_VELOCITY:{
			copyVec3(	bg->vel,
				msg->out->bg.queryVelocity.vel);

			if(bg->flags.hasAdditiveVel){
				addVec3( bg->velAdditive, 
					msg->out->bg.queryVelocity.vel, 
					msg->out->bg.queryVelocity.vel);
			}
		}

		xcase MR_MSG_BG_QUERY_IS_SETTLED:{
			msg->out->bg.queryIsSettled.isSettled = !bg->flags.hasAdditiveVel &&
													!bg->flags.hasConstantForceVel &&
													bg->flags.onGround &&
													bg->surfaceNormal[1] > sinf(RAD(30.f));
		}

		xcase MR_MSG_BG_QUERY_MAX_SPEED: {
			msg->out->bg.queryMaxSpeed.maxSpeed = syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed;
		}

		xcase MR_MSG_GET_SYNC_DEBUG_STRING:{
			mrSurfaceGetSyncDebugString(sync,
										syncPublic,
										msg->in.getSyncDebugString.buffer,
										msg->in.getSyncDebugString.bufferLen);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			mrSurfaceGetBGDebugString(	bg,
										localBG,
										msg->in.bg.getDebugString.buffer,
										msg->in.bg.getDebugString.bufferLen);
		}

		xcase MR_MSG_BG_WCO_ACTOR_CREATED:{
			mrmMoveToValidPointBG(msg);

			if(bg->flags.onGround){
				bg->flagsMutable.preCheckIfOnGround = 1;
			}
		}

		xcase MR_MSG_BG_WCO_ACTOR_DESTROYED:{
			if(bg->flags.onGround){
				bg->flagsMutable.preCheckIfOnGround = 1;
			}
		}

		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:{
			// Animation can only be taken if not moving.

			if(	msg->in.bg.dataReleaseRequested.dataClassBits & MDC_BIT_ANIMATION &&
				!bg->flags.isAtRest)
			{
				msg->out->bg.dataReleaseRequested.denied = 1;
			}
		}

		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			U32 dataClassBits = msg->in.bg.dataWasReleased.dataClassBits;
			U32 bitsToAcquire;

			mrSurfaceGetBitsToAcquireBG(bg, sync, &bitsToAcquire);

			if(dataClassBits & bitsToAcquire){
				// I lost something I want.

				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

			if(dataClassBits & MDC_BIT_POSITION_CHANGE){
				mrmShareOldVec3BG(msg, "Velocity", bg->vel);
				zeroVec3(bg->vel);
				// We may want to clear out more stances here, but for now just clearing all the off ground stances
				mrSurfaceDestroyOffGroundStancesBG(msg, bg, localBG);
				mrSurfaceStanceClearBG(msg, bg, localBG, MR_SURFACE_STANCE_LANDED);
			}

			if(dataClassBits & MDC_BIT_ROTATION_CHANGE){
				mrmShareOldF32BG(msg, "TargetFaceYaw", bg->yawFaceTarget);
				mrmShareOldF32BG(msg, "InputMoveYaw", bg->inputMoveYaw);
				mrmShareOldF32BG(msg, "InputFaceYaw", bg->inputFaceYaw);
				mrmShareOldQuatBG(msg, "TargetRotation", bg->rotTarget);
			}

			if(dataClassBits & MDC_BIT_ANIMATION){
				bg->flagsMutable.ownsAnimation = 0;
				mrSurfaceDeactivateFlourish(msg, bg);
			}
		}

		xcase MR_MSG_BG_RECEIVE_OLD_DATA:{
			const MovementSharedData* sd = msg->in.bg.receiveOldData.sharedData;

			switch(sd->dataType){
				xcase MSDT_S32:{
					if(	!stricmp(sd->name, "OffGroundIntentionally") &&
						sd->data.s32)
					{
						bg->flagsMutable.offGroundByJumping = 1;
					}
				}

				xcase MSDT_VEC3:{
					if(!stricmp(sd->name, "Velocity")){
						copyVec3(	sd->data.vec3,
							bg->vel);
					}
				}

				xcase MSDT_F32:{
					if(!stricmp(sd->name, "TargetFaceYaw")){
						bg->yawFaceTarget = sd->data.f32;
					}
					else if(!stricmp(sd->name, "InputFaceYaw")){
						bg->inputFaceYaw = sd->data.f32;
					}
					else if(!stricmp(sd->name, "InputMoveYaw")){
						bg->inputMoveYaw = sd->data.f32;
					}
				}

				xcase MSDT_QUAT:{
					if(!stricmp(sd->name, "TargetRotation")){
						Vec3 pyr;

						quatToPYR(	sd->data.quat,
							pyr);

						pyr[0] = pyr[2] = 0.f;

						PYRToQuat(	pyr,
							bg->rotTarget);
					}
				}
			}
		}

		xcase MR_MSG_BG_INPUT_EVENT:{
			mrSurfaceHandleInputEventBG(msg, bg, localBG);
		}

		xcase MR_MSG_BG_UPDATED_TOBG:{
			// Check for speed penalty from FG.

			if(toBG->speedPenalties)
			{
				eaPushEArray(&localBG->speedPenaltiesQueued, &toBG->speedPenalties);
				eaDestroy(&toBG->speedPenalties);
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

			if(toBG->spcStrafingOverride)
			{
				localBG->spcStrafingOverride = toBG->spcStrafingOverride;
				localBG->flagsMutable.scheduledStrafingOverride = toBG->scheduledStrafingOverride;
				toBG->spcStrafingOverride = 0;
			}
			if(toBG->spcDisableJump)
			{
				localBG->spcDisableJump = toBG->spcDisableJump;
				localBG->flagsMutable.scheduledDisableJump = toBG->scheduledDisableJump;
				toBG->spcDisableJump = 0;
			}
		}

		xcase MR_MSG_BG_UPDATED_SYNC:{
			mrSurfaceSetIsNotAtRestBG(msg, bg, "updated sync");

			if (!sync->flags.isDisabled){
				// We probably don't want to do this blindly as the requester may already 
				// have all the bits it needs.
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

			if (mrSurfaceIsStrafing(localBG, sync))
			{
				bg->flagsMutable.isTurning = 0;
			}
			else
			{
				mrSurfaceUpdateIsTurningBG(msg, bg, localBG);
			}
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			mrSurfaceDiscussDataOwnershipBG(msg, bg, localBG, sync);
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_TARGET:{
					mrSurfaceHandleCreateOutputPositionTarget(	msg,
																bg,
																localBG,
																sync,
																syncPublic,
																toFG);
				}

				xcase MDC_BIT_POSITION_CHANGE:{
					mrSurfaceHandleCreateOutputPositionChange(	msg,
																bg,
																localBG,
																sync,
																syncPublic,
																toFG);
				}

				xcase MDC_BIT_ROTATION_TARGET:{
					mrSurfaceHandleCreateOutputRotationTarget(	msg,
																bg,
																localBG,
																sync,
																syncPublic,
																toFG);
				}

				xcase MDC_BIT_ROTATION_CHANGE:{
					mrSurfaceHandleCreateOutputRotationChange(	msg,
																bg,
																localBG,
																sync,
																syncPublic,
																toFG);
				}

				xcase MDC_BIT_ANIMATION:{
					if (!gConf.bNewAnimationSystem) {
						mrSurfaceHandleCreateOutputAnimation(	msg,
																bg,
																localBG,
																sync,
																syncPublic,
																toFG);
					}

					if (bg->flags.flourishIsActive)
					{
						if (!sync->flags.flourishEnabled ||
							(	bg->spcPlayedFlourishAnim &&
								mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcPlayedFlourishAnim, sync->flourishTimer)))
						{
							mrSurfaceDeactivateFlourish(msg, bg);
						}
						else if (TRUE_THEN_RESET(bg->flagsMutable.playFlourish))
						{
							mrmAnimStartBG(msg, mmGetAnimBitHandleByName("Flourish",0), 0);
							mrmGetProcessCountBG(msg, &bg->spcPlayedFlourishAnim);
						}
					}
				}
			}
		}

		xcase MR_MSG_BG_CONTROLLER_MSG:{
			Vec3 pos;

			if(!msg->in.bg.controllerMsg.isGround){	
				// For now just project the velocity onto the plane defined by the normal.
				// Only do this for players, let the NPCs cheat and slide a bit more.

				if(mrmIsAttachedToClientBG(msg)){
					projectVecOntoPlane(bg->vel, msg->in.bg.controllerMsg.normal, bg->vel);
					LOG_VEC3(bg->vel);
				}
				break;
			}

			mrmGetPositionBG(msg, pos);

			if(msg->in.bg.controllerMsg.normal[1] > 0){
				mrmLog(	msg,
					NULL,
					"[surface] Hit surface:\n"
					" p(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
					" wp(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]\n"
					" wn(%1.2f, %1.2f, %1.2f) [%8.8x, %8.8x, %8.8x]",
					vecParamsXYZ(pos),
					vecParamsXYZ((S32*)pos),
					vecParamsXYZ(msg->in.bg.controllerMsg.pos),
					vecParamsXYZ((S32*)msg->in.bg.controllerMsg.pos),
					vecParamsXYZ(msg->in.bg.controllerMsg.normal),
					vecParamsXYZ((S32*)msg->in.bg.controllerMsg.normal));

				hitSurface(	msg, 
					localBG, 
					bg,
					sync,
					syncPublic,
					pos,
					msg->in.bg.controllerMsg.pos,
					msg->in.bg.controllerMsg.normal);
			}
			else if(msg->in.bg.controllerMsg.pos[1] < pos[1] + 1.5f){
				Vec3 normal = {0, 1, 0};

				mrmLog(	msg,
					NULL,
					"[surface] Hit fake surface:"
					" p(%1.2f, %1.2f, %1.2f)"
					" [%8.8x, %8.8x, %8.8x]"
					" wp(%1.2f, %1.2f, %1.2f)"
					" [%8.8x, %8.8x, %8.8x]"
					" wn(%1.2f, %1.2f, %1.2f)"
					" [%8.8x, %8.8x, %8.8x]",
					vecParamsXYZ(pos),
					vecParamsXYZ((S32*)pos),
					vecParamsXYZ(msg->in.bg.controllerMsg.pos),
					vecParamsXYZ((S32*)msg->in.bg.controllerMsg.pos),
					vecParamsXYZ(msg->in.bg.controllerMsg.normal),
					vecParamsXYZ((S32*)msg->in.bg.controllerMsg.normal));

				hitSurface(	msg, 
					localBG, 
					bg,
					sync,
					syncPublic,
					pos,
					msg->in.bg.controllerMsg.pos,
					normal);
			}else{
				mrmLog(	msg,
					NULL,
					"[surface] Ignored hit surface:"
					" p(%1.2f, %1.2f, %1.2f)"
					" [%8.8x, %8.8x, %8.8x]"
					" wp(%1.2f, %1.2f, %1.2f)"
					" [%8.8x, %8.8x, %8.8x]"
					" wn(%1.2f, %1.2f, %1.2f)"
					" [%8.8x, %8.8x, %8.8x]",
					vecParamsXYZ(pos),
					vecParamsXYZ((S32*)pos),
					vecParamsXYZ(msg->in.bg.controllerMsg.pos),
					vecParamsXYZ((S32*)msg->in.bg.controllerMsg.pos),
					vecParamsXYZ(msg->in.bg.controllerMsg.normal),
					vecParamsXYZ((S32*)msg->in.bg.controllerMsg.normal));
			}
		}

		xcase MR_MSG_BG_OVERRIDE_ALL_UNSET:{
			localBG->overrides.maxSpeed = 0.f;
			localBG->flagsMutable.hasOverrideMaxSpeed = 0;
			localBG->flagsMutable.autoRun = 0;
			localBG->flagsMutable.isStrafingOverrideValue = 0;
		}

		xcase MR_MSG_BG_OVERRIDE_VALUE_SET:{
			const char*						name = msg->in.bg.overrideValueSet.name;
			const MovementSharedDataType	valueType = msg->in.bg.overrideValueSet.valueType;
			const MovementSharedDataValue	value = msg->in.bg.overrideValueSet.value;

			if(	valueType == MSDT_F32 && !stricmp(name, "MaxSpeed"))
			{
				localBG->overrides.maxSpeed = MINMAX(value.f32, 0, 500);
				localBG->flagsMutable.hasOverrideMaxSpeed = 1;
			}
			else if (valueType == MSDT_S32)
			{
				if (!stricmp(name, "AutoRun"))
				{
					localBG->flagsMutable.autoRun = !!value.s32;
					mrSurfaceSetIsNotAtRestBG(msg, bg, "AutoRun");
					mrSurfaceInputDirIsDirtyBG(msg, localBG, "AutoRun");
				}
				else if (!stricmp(name, "Strafe"))
				{
					localBG->flagsMutable.isStrafingOverrideValue = !!value.s32;
					mrSurfaceSetIsNotAtRestBG(msg, bg, "StrafeOverride");
					mrSurfaceInputDirIsDirtyBG(msg, localBG, "StrafeOverride");
				} 
				else if (!stricmp(name, "JumpDisable"))
				{
					localBG->flagsMutable.isJumpDisabledOverrideValue = !!value.s32;
				}
			}
		}

		xcase MR_MSG_BG_OVERRIDE_VALUE_UNSET:{
			if(!stricmp(msg->in.bg.overrideValueUnset.name, "MaxSpeed"))
			{
				localBG->overrides.maxSpeed = 0.f;
				localBG->flagsMutable.hasOverrideMaxSpeed = 0;
			}
			else if (!stricmp(msg->in.bg.overrideValueUnset.name, "AutoRun"))
			{
				localBG->flagsMutable.autoRun = 0;
			}
			else if (!stricmp(msg->in.bg.overrideValueUnset.name, "Strafe"))
			{
				localBG->flagsMutable.isStrafingOverrideValue = 0;
			}
			else if (!stricmp(msg->in.bg.overrideValueUnset.name, "JumpDisable"))
			{
				localBG->flagsMutable.isJumpDisabledOverrideValue = 0;
			}
		}

		xcase MR_MSG_BG_BEFORE_REPREDICT:{
			if(eaSize(&localBG->speedPenaltiesQueued)){
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

			ZeroArray(localBG->stanceHandle);

			if(bg->flags.hasStanceMask){
				ARRAY_FOREACH_BEGIN(localBG->stanceHandle, i);
				{
					if(TRUE_THEN_RESET_BIT(bg->flagsMutable.hasStanceMask, BIT(i))){
						mrSurfaceStanceSetBG(msg, bg, localBG, i);
					}
				}
				ARRAY_FOREACH_END;
			}
		}
	}
}

bool mrSurfaceCreate(MovementManager* mm,
	MovementRequester** mrOut)
{
	MovementRequester* mr;

	if(!mmRequesterCreateBasic(mm, &mr, mrSurfaceMsgHandler)){
		return false;
	}

	mrSurfaceSetDefaults(mr);

	if(mrOut){
		*mrOut = mr;
	}

	return true;
}

void mrSurfaceSetDefaults(MovementRequester* mr){
	mrSurfaceSetBackScale(mr, 1.f);
	mrSurfaceSetFriction(mr, MR_SURFACE_DEFAULT_PLAYER_FRICTION);
	mrSurfaceSetTraction(mr, MR_SURFACE_DEFAULT_PLAYER_TRACTION);
	mrSurfaceSetSpeed(mr, MR_SURFACE_SPEED_SLOW, MR_SURFACE_DEFAULT_SPEED_SLOW);
	mrSurfaceSetSpeedRange(mr, MR_SURFACE_SPEED_SLOW, 0.3f, 0.8f);
	mrSurfaceSetSpeed(mr, MR_SURFACE_SPEED_FAST, MR_SURFACE_DEFAULT_SPEED_FAST);
	mrSurfaceSetSpeed(mr, MR_SURFACE_SPEED_NATIVE, MR_SURFACE_DEFAULT_SPEED_FAST);
	mrSurfaceSetSpeedRange(mr, MR_SURFACE_SPEED_FAST, 1.f, 1.f);
	mrSurfaceSetGravity(mr, MR_SURFACE_DEFAULT_GRAVITY);
	mrSurfaceSetJumpGravity(mr, MR_SURFACE_DEFAULT_GRAVITY, MR_SURFACE_DEFAULT_GRAVITY);
	mrSurfaceSetJumpHeight(mr, MR_SURFACE_DEFAULT_JUMPHEIGHT);
	mrSurfaceSetPitchDiffMultiplier(mr, MR_SURFACE_DEFAULT_PITCH_DIFF_MULT);
}

#define GET_FG(fg)							if(!MR_GET_FG(mr, mrSurfaceMsgHandler, Surface, fg)){return 0;}
#define GET_SYNC(sync, syncPublic)			if(!MR_GET_SYNC_PUBLIC(mr, mrSurfaceMsgHandler, Surface, sync, syncPublic)){return 0;}
#define IF_DIFF_THEN_SET(a, b)				MR_SYNC_SET_IF_DIFF(mr, a, b)
#define IF_DIFF_THEN_SET_WITH_AFTER(a, b)	MR_SYNC_SET_IF_DIFF_WITH_AFTER(mr, a, b)
#define IF_DIFF_THEN_SET_VEC3(a, b)			if(!sameVec3((a), (b))){copyVec3((b), (a));mrEnableMsgUpdatedSync(mr);}((void)0)

bool mrSurfaceSpeedPenaltyStart(MovementRequester* mr,
								U32 id,
								F32 speedPenalty,
								U32 spc)
{
	SurfaceSpeedPenalty*	sp;
	SurfaceFG*				fg;

	GET_FG(&fg);

	mrLog(	mr,
		NULL,
		"Speed penalty START: id %u, spc %u, penalty %1.3f [%8.8x].",
		id,
		spc,
		speedPenalty,
		*(S32*)&speedPenalty);

	mrEnableMsgCreateToBG(mr);

	sp = StructAlloc(parse_SurfaceSpeedPenalty);
	sp->id = id;
	sp->penalty = speedPenalty;
	sp->spc = spc;

	eaPush(&fg->toBG.speedPenalties, sp);

	return true;
}

bool mrSurfaceSpeedScaleStart(	MovementRequester* mr,
								U32 id,
								F32 speedScale,
								U32 spc)
{
	SurfaceSpeedPenalty*	sp;
	SurfaceFG*				fg;

	GET_FG(&fg);

	mrLog(	mr,
		NULL,
		"Speed scale START: id %u, spc %u, scale %1.3f [%8.8x].",
		id,
		spc,
		speedScale,
		*(S32*)&speedScale);

	mrEnableMsgCreateToBG(mr);

	sp = StructAlloc(parse_SurfaceSpeedPenalty);
	sp->id = id;
	sp->penalty = speedScale;
	sp->spc = spc;
	sp->flags.isStrictScale = true;

	eaPush(&fg->toBG.speedPenalties, sp);

	return true;
}


bool mrSurfaceSpeedPenaltyStop(	MovementRequester* mr,
								U32 id,
								U32 spc)
{
	SurfaceSpeedPenalty*	sp;
	SurfaceFG*				fg;

	GET_FG(&fg);

	mrLog(	mr,
		NULL,
		"Speed penalty STOP: id %u, spc %u.",
		id,
		spc);

	mrEnableMsgCreateToBG(mr);

	sp = StructAlloc(parse_SurfaceSpeedPenalty);
	sp->id = id;
	sp->spc = spc;
	sp->flags.isStop = 1;

	eaPush(&fg->toBG.speedPenalties, sp);

	return true;
}

bool mrSurfaceDoCameraShake(MovementRequester* mr)
{
	SurfaceFG* fg;

	GET_FG(&fg);

	return TRUE_THEN_RESET(fg->flags.doCameraShake);
}

F32	mrSurfaceGetSurfaceImpactSpeed(MovementRequester* mr)
{
	F32			f;
	SurfaceFG*	fg;

	GET_FG(&fg);

	f = fg->surfaceImpactSpeed;
	fg->surfaceImpactSpeed = 0;

	return f;
}

bool mrSurfaceGetOnGround(MovementRequester* mr)
{
	SurfaceFG* fg;

	GET_FG(&fg);

	return fg->flags.onGround;
}

bool mrSurfaceSetBackScale(	MovementRequester* mr,
							F32 backScale)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->backScale, backScale);

	return true;
}

bool mrSurfaceSetFriction(	MovementRequester* mr,
							F32 friction)
{
	SurfaceSyncPublic* syncPublic;

	GET_SYNC(NULL, &syncPublic);
	IF_DIFF_THEN_SET(syncPublic->friction, friction);

	return true;
}

bool mrSurfaceSetTraction(	MovementRequester* mr,
							F32 traction)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->traction, traction);

	return true;
}

bool mrSurfaceSetSpeed(	MovementRequester* mr,
						MRSurfaceSpeedIndex index,
						F32 speed)
{
	SurfaceSyncPublic*	syncPublic;
	S32					i;

	GET_SYNC(NULL, &syncPublic);

	if(	index < 0 ||
		index >= MR_SURFACE_SPEED_COUNT)
	{
		return false;
	}

	MAX1(speed, 0.f);

	IF_DIFF_THEN_SET(syncPublic->speeds[index].speed, speed);

	if (index != MR_SURFACE_SPEED_NATIVE)
	{	// for every other surface speed type besides the native, 
		// make sure the speeds below are not faster
		// and the speeds above it are not slower
		for(i = index - 1; i >= 0; i--){
			MIN1(syncPublic->speeds[i].speed, syncPublic->speeds[i + 1].speed);
		}

		for(i = index + 1; i < ARRAY_SIZE(syncPublic->speeds); i++){
			MAX1(syncPublic->speeds[i].speed, syncPublic->speeds[i - 1].speed);
		}
	}
	

	return true;
}

bool mrSurfaceSetSpeedRange(	MovementRequester* mr,
							MRSurfaceSpeedIndex index,
							F32 loDirScale,
							F32 hiDirScale)
{
	SurfaceSyncPublic*	syncPublic;
	S32					i;

	GET_SYNC(NULL, &syncPublic);

	if(	index < 0 ||
		index >= MR_SURFACE_SPEED_COUNT)
	{
		return false;
	}

	MINMAX1(loDirScale, 0.f, 1.f);
	MINMAX1(hiDirScale, loDirScale, 1.f);

	IF_DIFF_THEN_SET(syncPublic->speeds[index].loDirScale, loDirScale);
	IF_DIFF_THEN_SET(syncPublic->speeds[index].hiDirScale, hiDirScale);

	for(i = index - 1; i >= 0; i--){
		MIN1(syncPublic->speeds[i].hiDirScale, syncPublic->speeds[i + 1].loDirScale);
		MIN1(syncPublic->speeds[i].loDirScale, syncPublic->speeds[i].hiDirScale);
	}

	for(i = index + 1; i < ARRAY_SIZE(syncPublic->speeds); i++){
		MAX1(syncPublic->speeds[i].loDirScale, syncPublic->speeds[i - 1].hiDirScale);
		MAX1(syncPublic->speeds[i].hiDirScale, syncPublic->speeds[i].loDirScale);
	}

	return true;
}

F32 mrSurfaceGetSpeed(MovementRequester* mr)
{
	SurfaceSyncPublic* syncPublic;

	GET_SYNC(NULL, &syncPublic);

	return syncPublic->speeds[MR_SURFACE_SPEED_FAST].speed;
}

bool mrSurfaceSetGravity(MovementRequester* mr,
						F32 gravity)
{
	SurfaceSyncPublic* syncPublic;

	GET_SYNC(NULL, &syncPublic);
	IF_DIFF_THEN_SET(syncPublic->gravity, gravity);

	return true;
}

bool mrSurfaceSetJumpGravity(MovementRequester* mr,
							F32 jumpUpGravity,
							F32 jumpDownGravity)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->jump.upGravity, jumpUpGravity);
	IF_DIFF_THEN_SET(sync->jump.downGravity, jumpDownGravity);

	return true;
}

bool mrSurfaceSetJumpHeight(	MovementRequester* mr,
							F32 jumpHeight)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->jump.height, jumpHeight);

	return true;
}

bool mrSurfaceGetJumpHeight( MovementRequester* mr,
							F32* jumpHeightOut)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);

	if(*jumpHeightOut){
		*jumpHeightOut = sync->jump.height;
	}

	return true;
}

bool mrSurfaceSetJumpTraction(	MovementRequester* mr,
								F32 jumpTraction)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->jump.traction, jumpTraction);

	return true;
}

bool mrSurfaceSetJumpSpeed(	MovementRequester* mr,
							F32 jumpSpeed)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->jump.maxSpeed, jumpSpeed);

	return true;
}

bool mrSurfaceSetVelocity(	MovementRequester* mr,
							const Vec3 vel)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET_VEC3(sync->test.setVel.vel, vel);
	IF_DIFF_THEN_SET_WITH_AFTER(sync->test.setVel.useVel, 1);

	return true;
}

bool mrSurfaceSetDoJumpTest(	MovementRequester* mr,
							const Vec3 target)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);

	if(!target){
		return false;
	}

	IF_DIFF_THEN_SET_VEC3(sync->test.jump.target, target);
	IF_DIFF_THEN_SET_WITH_AFTER(sync->test.jump.doJumpTest, 1);

	return true;
}

bool mrSurfaceSetIsStrafing(MovementRequester* mr,
							bool isStrafing)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->flags.isStrafing, (U32)!!isStrafing);

	return true;
}

// set on the sync struct, can be set just on the server
bool mrSurfaceSetIsStrafingOverride(MovementRequester* mr,
									bool isStrafing, 
									bool enableOverride)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);

	isStrafing = !!isStrafing;
	enableOverride = !!enableOverride;

	IF_DIFF_THEN_SET(sync->flags.strafeOverride, (U32)isStrafing);
	IF_DIFF_THEN_SET(sync->flags.strafeOverrideEnabled, (U32)enableOverride);

	return true;
}

bool mrSurfaceSetStrafingOverride(	MovementRequester* mr,
									bool isStrafing, 
									U32 spc)
{
	SurfaceFG*				fg = NULL;

	GET_FG(&fg);

	isStrafing = !!isStrafing;
	if ((bool)fg->toBG.scheduledStrafingOverride != isStrafing)
	{
		mrLog(	mr,
				NULL,
				"Strafing Override %s: spc %u",
				(isStrafing) ? "START" : "STOP",
				spc);

		mrEnableMsgCreateToBG(mr);

		fg->toBG.scheduledStrafingOverride = isStrafing;
		fg->toBG.spcStrafingOverride = spc;
	}

	return true;
}

bool mrSurfaceDisableJump(	MovementRequester* mr,
								bool disableJump, 
								U32 spc)
{
	SurfaceFG*				fg = NULL;

	GET_FG(&fg);

	disableJump = !!disableJump;
	if ((bool)fg->toBG.scheduledDisableJump != disableJump)
	{
		mrLog(	mr,
			NULL,
			"Disabling Jump %s: spc %u",
			(disableJump) ? "START" : "STOP",
			spc);

		mrEnableMsgCreateToBG(mr);

		fg->toBG.scheduledDisableJump = disableJump;
		fg->toBG.spcDisableJump = spc;
	}

	return true;
}

bool mrSurfaceSetFlourishData(	MovementRequester *mr,
								bool enabled,
								F32 timer)
{
	SurfaceSync *sync; //might need SurfaceSyncPublic

	GET_SYNC(&sync, NULL);
	if (IF_DIFF_THEN_SET(sync->flags.flourishEnabled, (U32)!!enabled))
	{
		sync->flourishTimer = timer;
	}

	return true;
}

bool mrSurfaceSetInCombat(	MovementRequester *mr,
							bool inCombat)
{
	SurfaceSync *sync; //might need SurfaceSyncPublic

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->flags.inCombat, (U32)!!inCombat);

	return true;
}

bool mrSurfaceSetSpawnedOnGround(	MovementRequester *mr,
									bool spawnedOnGround)
{
	SurfaceSync *sync; //might need SurfaceSynPublic

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->flags.spawnedOnGround, (U32)!!spawnedOnGround);

	return true;
}

bool mrSurfaceSetDoCollisionTest(MovementRequester* mr,
								S32 collisionTestFlags,
								const Vec3 loOffset,
								const Vec3 hiOffset,
								F32 radius)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->test.coll.flagBits, (U32)collisionTestFlags);
	IF_DIFF_THEN_SET_VEC3(sync->test.coll.loOffset, loOffset);
	IF_DIFF_THEN_SET_VEC3(sync->test.coll.hiOffset, hiOffset);
	IF_DIFF_THEN_SET(sync->test.coll.radius, radius);

	return true;
}

bool mrSurfaceSetCanStick(	MovementRequester* mr,
							bool enabled)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->flags.canStick, (U32)!!enabled);

	return true;
}

bool mrSurfaceSetOrientToSurface(MovementRequester* mr,
								bool enabled)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->flags.orientToSurface, (U32)!!enabled);

	return true;
}

bool mrSurfaceSetPhysicsCheating(	MovementRequester *mr,
									bool enabled)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	IF_DIFF_THEN_SET(sync->flags.allowPhysicsCheating, (U32)!!enabled);

	return true;
}

bool mrSurfaceSetEnabled(MovementRequester* mr, bool enable)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);
	// reverse flag to mean isDisabled 
	IF_DIFF_THEN_SET(sync->flags.isDisabled, (U32)!enable);

	return true;
}

bool mrSurfaceSetPitchDiffMultiplier(MovementRequester* mr, F32 value)
{
	SurfaceSync* sync;

	GET_SYNC(&sync, NULL);

	IF_DIFF_THEN_SET(sync->pitchDiffMult, value);

	return true;
}

bool mrSurfaceSetTurnParameters(	MovementRequester* mr, const SurfaceMovementTurnDef *pDef)
{
	SurfaceSync* sync;
	F32 rad;
	GET_SYNC(&sync, NULL);

	rad = RAD(pDef->fFaceTurnRate);
	IF_DIFF_THEN_SET(sync->turn.fFaceTurnRate, rad);
	rad = RAD(pDef->fFaceMinTurnRate);
	IF_DIFF_THEN_SET(sync->turn.fFaceMinTurnRate, rad);
	rad = RAD(pDef->fFaceMaxTurnRate);
	IF_DIFF_THEN_SET(sync->turn.fFaceMaxTurnRate, rad);
	


	// notify that we're using the sync's turnDef instead of the default
	IF_DIFF_THEN_SET(sync->flags.useSyncTurnDef, 1);

	return true;
}

bool mrSurfaceSetTurnRateScale(	MovementRequester* mr, 
								F32 fTurnRateScale, 
								F32 fTurnRateScaleFast)
{
	SurfaceSync* sync;
	GET_SYNC(&sync, NULL);

	IF_DIFF_THEN_SET(sync->turnRateScale, fTurnRateScale);
	IF_DIFF_THEN_SET(sync->turnRateScaleFast, fTurnRateScaleFast);
	return true;
}

bool mrSurfaceResetTurnRateScales(MovementRequester* mr)
{
	SurfaceSync* sync;
	GET_SYNC(&sync, NULL);

	IF_DIFF_THEN_SET(sync->turnRateScale, 0.f);
	IF_DIFF_THEN_SET(sync->turnRateScaleFast, 0.f);
	return true;
}


#undef GET_FG
#undef GET_SYNC
#undef IF_DIFF_THEN_SET
#undef IF_DIFF_THEN_SET_VEC3

#include "AutoGen/EntityMovementDefault_c_ast.c"
