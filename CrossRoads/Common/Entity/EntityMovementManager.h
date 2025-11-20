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

#include "EntityMovementManagerDemo.h"
#include "GlobalTypes.h"

#include "dynNodeInline.h"
#include "file.h"

#define MM_CHECK_DYNPOS_DEVONLY(vInput)	\
	if (isDevelopmentMode()) {			\
		CHECK_DYNPOS(vInput);			\
	}

// Debug flags.

#define MM_CHECK_STRING_ENABLED			0
#define MM_ENABLE_EXTRA_ASSERTS			0
#define MM_ENABLE_DEBUG_COUNTERS		0

// Extra asserts.

#if !MM_ENABLE_EXTRA_ASSERTS
	#define MM_EXTRA_ASSERT(x)
#else
	#define MM_EXTRA_ASSERT(x)			assert(x)
#endif

// Extra counters.

#if MM_ENABLE_DEBUG_COUNTERS
	#define MM_DEBUG_COUNT(name, val)	ADD_MISC_COUNT(val, name)
#else
	#define MM_DEBUG_COUNT(name, val)
#endif

// Timing constants.

#define MM_STEPS_PER_SECOND				(30)
#define MM_SECONDS_PER_STEP				(1.f / (F32)MM_STEPS_PER_SECOND)
#define MM_PROCESS_COUNTS_PER_STEP		(2)
#define MM_PROCESS_COUNTS_PER_SECOND	(MM_PROCESS_COUNTS_PER_STEP * MM_STEPS_PER_SECOND)
#define MM_SECONDS_PER_PROCESS_COUNT	(1.f / (F32)MM_PROCESS_COUNTS_PER_SECOND)

// Struct macros.

#define mmStructCopy(pti,copyMe,s)										\
			StructCopyAllVoid(pti,copyMe,s)
#define mmStructAllocIfNull(pti,s,ownerID)								\
			if(!(s)){													\
				(s) = StructAlloc_dbg(pti, NULL MEM_DBG_PARMS_INIT);	\
				/*smallAllocSetOwnerID((s), (U16)(intptr_t)ownerID);*/	\
			}
#define mmStructDestroy(pti,s,ownerID)									\
			if(s){														\
				/*smallAllocClearOwnerID((s), (U16)(intptr_t)ownerID);*/\
				StructDestroySafeVoid(pti, &(s));						\
			}
#define mmStructAllocAndCopy(pti,s,copyMe,ownerID)						\
			mmStructAllocIfNull(pti, s, mm);							\
			mmStructCopy(pti, copyMe, s)

// Structs declarations.

typedef struct CritterFactionMatrix						CritterFactionMatrix;
typedef struct MovementClient							MovementClient;
typedef struct MovementManager							MovementManager;
typedef struct MovementRequester						MovementRequester;
typedef struct MovementLogLine							MovementLogLine;
typedef struct MovementRequesterMsgPrivateData			MovementRequesterMsgPrivateData;
typedef struct MovementManagedResourceMsgPrivateData	MovementManagedResourceMsgPrivateData;
typedef struct MovementOffsetInstance					MovementOffsetInstance;
typedef struct MovementDisabledHandle					MovementDisabledHandle;
typedef struct MovementCollSetHandle					MovementCollSetHandle;
typedef struct MovementCollBitsHandle					MovementCollBitsHandle;
typedef struct MovementCollGroupHandle					MovementCollGroupHandle;
typedef struct MovementNoCollHandle						MovementNoCollHandle;
typedef struct MovementTrigger							MovementTrigger;
typedef struct MovementGeometry 						MovementGeometry;
typedef struct MovementBody								MovementBody;
typedef struct MovementBodyDesc							MovementBodyDesc;
typedef U32												MovementRequesterPipeHandle;

typedef struct WorldColl								WorldColl;
typedef struct WorldCollObject							WorldCollObject;
typedef struct WorldCollIntegrationMsg					WorldCollIntegrationMsg;

typedef struct PSDKActor								PSDKActor;
typedef struct PSDKCookedMesh							PSDKCookedMesh;

typedef struct Packet									Packet;
typedef struct FrameLockedTimer							FrameLockedTimer;
typedef struct DynSkeletonPreUpdateParams				DynSkeletonPreUpdateParams;
typedef struct Capsule									Capsule;
typedef U32												EntityRef;

// Struct, enum, and macro definitions.

AUTO_STRUCT;
typedef struct MovementManagerConfig
{
	// The double tap interval. The default is 400 msecs.
	S32 iDoubleTapInterval;		AST(DEFAULT(400))
} MovementManagerConfig;

// The movement manager config
extern MovementManagerConfig g_MovementManagerConfig;

typedef struct MovementGlobalPublicState {
	U32				activeLogCount;
	S32				isServer;
} MovementGlobalPublicState;

extern MovementGlobalPublicState mgPublic;

typedef struct MovementAnimBitHandles {
	union {
		struct {
			U32 flight;
			U32 move;
			U32 moving;
			U32 forward;
			U32 bankLeft;
			U32 bankRight;
			U32 run;
			U32 running;
			U32 trot;
			U32 trotting;
			U32 sprint;
			U32 air;
			U32 jump;
			U32 jumping;
			U32 falling;
			U32 fallFast;
			U32 rising;
			U32 jumpApex;
			U32 landed;
			U32 lunge;
			U32 lurch;
			U32 left;
			U32 right;
			U32 backward;
			U32 lockon;
			U32 idle;
			U32 knockback;
			U32 knockdown;
			U32 pushback;
			U32 death_impact;
			U32 neardeath_impact;
			U32 runtimeFreeze;
			U32 moveLeft;
			U32 moveRight;
			U32 repel;
			U32 getUp;
			U32 getUpBack;
			U32 prone;
			U32 crouch;
			U32 aim;
			U32 death;
			U32 neardeath;
			U32 dying;
			U32 revive;
			U32 exitDeath;
			U32 aiming;
			U32 dragonTurn;
			U32 turnLeft;
			U32 turnRight;
			U32 impact;
			U32 hitPause;
			U32 hitTop;
			U32 hitBottom;
			U32 hitLeft;
			U32 hitRight;
			U32 hitFront;
			U32 hitRear;
			U32 interrupt;
			U32 flourish;
			U32 exit;
			
			struct {
				U32 dodgeRoll;
			} flash;
		};
		
		U32 all[61];
	};
} MovementAnimBitHandles;

STATIC_ASSERT(SIZEOF2(MovementAnimBitHandles, all) == sizeof(MovementAnimBitHandles));

extern MovementAnimBitHandles mmAnimBitHandles;

#if !MM_CHECK_STRING_ENABLED
	#define MM_CHECK_STRING_READ(pak, checkString)
	#define MM_CHECK_STRING_WRITE(pak, checkString)
#else
	#define MM_CHECK_STRING_READ(pak, checkString)								\
		{																		\
			char temp[100];														\
			pktGetString(pak, SAFESTR(temp));									\
			assertmsgf(	!strcmp(temp, checkString),								\
						"Mismatched check string, expected \"%s\", got \"%s\"",	\
						checkString,											\
						temp);													\
		}

	#define MM_CHECK_STRING_WRITE(pak, checkString)	\
		pktSendString(pak, checkString)
#endif

typedef struct MovementDrawFuncs {
	S32			sx;
	S32			sy;
	
	void (*drawLine3D)(	const Vec3 p0,
						U32 argb0,
						const Vec3 p1,
						U32 argb1);

	void (*drawTriangle3D)(	const Vec3 p0,
							U32 argb0,
							const Vec3 p1,
							U32 argb1,
							const Vec3 p2,
							U32 argb2);

	void (*drawCapsule3D)(	const Vec3 p,
							const Vec3 dir,
							F32 length,
							F32 radius,
							U32 argb);

	void (*drawBox3D)(	const Vec3 xyzSize,
						const Mat4 mat,
						U32 argb);
} MovementDrawFuncs;

typedef enum MovementCollisionGroup {
	MCG_PLAYER				= BIT(0),
	MCG_PLAYER_PET			= BIT(1),
	MCG_OTHER				= BIT(2)
} MovementCollisionGroup;

typedef enum MovementInputValueIndex {
	// Direction.
	
	MIVI_BIT_LOW,
	MIVI_BIT_LOW_PREV = MIVI_BIT_LOW - 1,
		// Bit values start here.
		MIVI_BIT_FORWARD,
		MIVI_BIT_BACKWARD,
		MIVI_BIT_LEFT,
		MIVI_BIT_RIGHT,
		MIVI_BIT_UP,
		MIVI_BIT_DOWN,
		MIVI_BIT_SLOW,
		MIVI_BIT_TURN_LEFT,
		MIVI_BIT_TURN_RIGHT,
		MIVI_BIT_RUN,
		MIVI_BIT_ROLL,
		MIVI_BIT_AIM,
		MIVI_BIT_CROUCH,
		MIVI_BIT_TACTICAL,
		// Bit values end here.
	MIVI_BIT_HIGH,
	MIVI_BIT_COUNT = MIVI_BIT_HIGH - MIVI_BIT_LOW,
	MIVI_BIT_HIGH_MINUS_ONE = MIVI_BIT_HIGH - 1,
	
	// Rotation.
	
	MIVI_F32_LOW,
	MIVI_F32_LOW_PREV = MIVI_F32_LOW - 1,
		// F32 values start here.
		MIVI_F32_DIRECTION_SCALE,
		MIVI_F32_FACE_YAW,
		MIVI_F32_ROLL_YAW,
		MIVI_F32_MOVE_YAW,
		MIVI_F32_PITCH,
		MIVI_F32_TILT,
		// F32 values end here.
	MIVI_F32_HIGH,
	MIVI_F32_COUNT = MIVI_F32_HIGH - MIVI_F32_LOW,
	MIVI_F32_HIGH_MINUS_ONE = MIVI_F32_HIGH - 1,
	
	// Extras.
	
	MIVI_RESET_ALL_VALUES,
	MIVI_DEBUG_COMMAND,
	
	// Total count.
	
	MIVI_COUNT,
} MovementInputValueIndex;

typedef enum MovementPositionTargetType {
	MPTT_NOT_SET = 0,
	MPTT_STOPPED,
	MPTT_INPUT,
	MPTT_VELOCITY,
	MPTT_POINT,
	//MPTT_ENTITY,

	MPTT_COUNT,
} MovementPositionTargetType;

typedef enum MovementRotationTargetType {
	MRTT_STOPPED = 0,
	MRTT_INPUT,
	MRTT_ROTATION,
	MRTT_POINT,
	MRTT_DIRECTION,
	MRTT_ENTITY,

	MRTT_COUNT,
} MovementRotationTargetType;

typedef enum MovementSpeedType {
	MST_NORMAL = 0,		// Move using whatever speed you normally move at.
	MST_OVERRIDE,		// Move using the specified max speed.
	MST_CONSTANT,		// Move at this exact speed, with no acceleration.
	MST_IMPULSE,		// Set an impulse velocity that will stop me on my target.
	MST_NONE,			// Ignore if set to this.

	MST_COUNT,
} MovementSpeedType;

typedef enum MovementTurnRateType {
	MTRT_NORMAL = 0,		// Move using default
	MTRT_OVERRIDE,			// Move using specified turn rate
} MovementTurnRateType;

typedef enum MovementFrictionType {
	MFT_NORMAL = 0,			// Move using default
	MFT_OVERRIDE,			// Move using specified traction
} MovementFrictionType;

typedef enum MovementTractionType {
	MTT_NORMAL = 0,			// Move using default
	MTT_OVERRIDE,			// Move using specified traction
} MovementTractionType;

typedef enum MovementRequesterMsgHandlerID {
	MR_CLASS_ID_INVALID,
	MR_CLASS_ID_SURFACE,
	MR_CLASS_ID_POWERS,
	MR_CLASS_ID_TEST,
	MR_CLASS_ID_FLIGHT,
	MR_CLASS_ID_PLATFORM,
	MR_CLASS_ID_BEACON,
	MR_CLASS_ID_DOOR,
	MR_CLASS_ID_DOOR_GEO,
	MR_CLASS_ID_PROJECTILE,
	MR_CLASS_ID_PUSH,
	MR_CLASS_ID_TARGETED_ROTATION,
	MR_CLASS_ID_DISABLE,
	MR_CLASS_ID_SIMBODY,
	MR_CLASS_ID_RAGDOLL,
	MR_CLASS_ID_SWING,
	MR_CLASS_ID_TACTICAL,
	MR_CLASS_ID_DEAD,
	MR_CLASS_ID_INTERACTION,
	MR_CLASS_ID_GRAB,
	MR_CLASS_ID_EMOTE,
	MR_CLASS_ID_AI, // Only valid on server
	MR_CLASS_ID_AI_CIVILIAN,
	MR_CLASS_ID_PROJECTILEENTITY,
	MR_CLASS_ID_DRAGON,

	MR_CLASS_ID_COUNT,
} MovementRequesterMsgHandlerID;

typedef enum MovementManagedResourceMsgHandlerID {
	// None of these can ever be deleted because they're referenced by number in demos.  Grrrr. :(
	
	MMR_CLASS_ID_INVALID,
	MMR_CLASS_ID_FX,
	MMR_CLASS_ID_ATTACHMENT,
	MMR_CLASS_ID_OFFSET,
	MMR_CLASS_ID_BODY,
	MMR_CLASS_ID_SKELETON,
	MMR_CLASS_ID_DRAW,
	MMR_CLASS_ID_HIT_REACT,

	MMR_CLASS_ID_COUNT,
} MovementManagedResourceMsgHandlerID;

typedef enum MovementSharedDataType {
	MSDT_INVALID,
	MSDT_VEC3,
	MSDT_QUAT,
	MSDT_S32,
	MSDT_F32,
	MSDT_ENTITYREF,
} MovementSharedDataType;

typedef union MovementSharedDataValue {
	Vec3						vec3;
	Quat						quat;
	S32							s32;
	U32							u32;
	F32							f32;
} MovementSharedDataValue;

typedef struct MovementInputValue {
	MovementInputValueIndex		mivi;

	union {
		U32						bit : 1;
		F32						f32;
		const char*				command;
		U32						u32;
	};
	
	struct {
		U32						isDoubleTap : 1;
	} flags;
} MovementInputValue;

AUTO_STRUCT;
typedef struct MovementLogLine {
	char*						text;	AST(ESTRING)
} MovementLogLine;

AUTO_STRUCT;
typedef struct MovementLog {
	char*						name;
	MovementLogLine**			lines;
	U32							bytesTotal;
} MovementLog;

typedef struct MovementSharedData {
	const char*					name;
	MovementSharedDataType		dataType;
	MovementSharedDataValue		data;
} MovementSharedData;

typedef struct MovementPositionTarget {
	MovementPositionTargetType	targetType;
	MovementSpeedType			speedType;
	MovementTurnRateType		turnRateType;
	MovementFrictionType		frictionType;
	MovementTractionType		tractionType;

	union {
		Vec3					point;
		//EntityRef				entity;
		Vec3					vel;
	};

	F32							speed;
	F32							turnRate;
	F32							friction;
	F32							traction;
	
	Vec3						jumpTarget;

	struct {
		U32						hasJumpTarget	: 1;
		U32						startJump		: 1;
		U32						useY			: 1;
		U32						noWorldColl		: 1;
	} flags;
} MovementPositionTarget;

typedef struct MovementRotationTarget {
	MovementRotationTargetType	targetType;
	MovementSpeedType			speedType;
	MovementTurnRateType		turnRateType;

	F32							turnRate;

	union {
		Quat					rot;
		Vec3					point;
		Vec3					dir;
		//EntityRef				entity;
	};
} MovementRotationTarget;

typedef enum MovementDataClassBit {
	MDC_BIT_POSITION_TARGET		= BIT(0),
	MDC_BIT_POSITION_CHANGE		= BIT(1),
	MDC_BIT_ROTATION_TARGET		= BIT(2),
	MDC_BIT_ROTATION_CHANGE		= BIT(3),
	MDC_BIT_ANIMATION			= BIT(4),

	MDC_BITS_ALL				= BIT(5) - 1,

	MDC_BITS_TARGET_ALL			=	MDC_BIT_POSITION_TARGET |
									MDC_BIT_ROTATION_TARGET,

	MDC_BITS_CHANGE_ALL			=	MDC_BIT_POSITION_CHANGE |
									MDC_BIT_ROTATION_CHANGE |
									MDC_BIT_ANIMATION,
} MovementDataClassBit;

// Messages received by the MovementRequesterMsgHandler.

typedef enum MovementRequesterMsgType {
	MR_MSG_INVALID = 0,

	MR_MSG_GET_SYNC_DEBUG_STRING,					//

	// Foreground messages.

	MR_MSG_FG_BIT = 0x1000,
	MR_MSG_FG_LOW,									// All these have user fg
		MR_MSG_FG_CREATE_TOBG,						// toBG
		MR_MSG_FG_UPDATED_TOFG,						// toFG
		MR_MSG_FG_AFTER_SYNC,						//
		MR_MSG_FG_BEFORE_DESTROY,					//
		MR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT,		//
	MR_MSG_FG_HIGH,

	// Background messages.

	MR_MSG_BG_BIT = 0x2000,
	MR_MSG_BG_LOW,									// All these have user bg
		MR_MSG_BG_INITIALIZE,						//
		
		MR_MSG_BG_QUERY_ON_GROUND,					//
		MR_MSG_BG_QUERY_VELOCITY,					//
		MR_MSG_BG_QUERY_MAX_SPEED,					//
		MR_MSG_BG_QUERY_IS_SETTLED,					//
		MR_MSG_BG_QUERY_TURN_RATE,					//
		MR_MSG_BG_QUERY_CAPSULE_SIZE,				//
		MR_MSG_BG_QUERY_LURCH_INFO,

		MR_MSG_BG_FORCE_CHANGED_POS,				//
		MR_MSG_BG_FORCE_CHANGED_ROT,				//

		MR_MSG_BG_PREDICT_DISABLED,				//
		MR_MSG_BG_PREDICT_ENABLED,				//

		MR_MSG_BG_CLIENT_ATTACHMENT_CHANGED,		//

		MR_MSG_BG_MMR_HANDLE_CHANGED,				//

		MR_MSG_BG_WCO_ACTOR_CREATED,				//
		MR_MSG_BG_WCO_ACTOR_DESTROYED,				//

		MR_MSG_BG_INIT_REPREDICT_SIM_BODY,			//
		MR_MSG_BG_SIM_BODIES_DO_CREATE,				//

		MR_MSG_BG_DATA_RELEASE_REQUESTED,			//
		MR_MSG_BG_DATA_WAS_RELEASED,				//
		MR_MSG_BG_RECEIVE_OLD_DATA,					//
		
		MR_MSG_BG_DETAIL_ANIM_BIT_REQUESTED,		//
		MR_MSG_BG_DETAIL_RESOURCE_REQUESTED,		//

		MR_MSG_BG_GET_DEBUG_STRING,					//
		
		MR_MSG_BG_OVERRIDE_ALL_UNSET,				//
		MR_MSG_BG_OVERRIDE_VALUE_SHOULD_REJECT,		//
		MR_MSG_BG_OVERRIDE_VALUE_DESTROYED,			//
		MR_MSG_BG_OVERRIDE_VALUE_SET,				//
		MR_MSG_BG_OVERRIDE_VALUE_UNSET,				//

		MR_MSG_BG_BEFORE_REPREDICT,					//
		MR_MSG_BG_IS_MISPREDICTED,					//
		
		MR_MSG_BG_POST_STEP,						//
		MR_MSG_BG_PIPE_DESTROYED,					//
		MR_MSG_BG_PIPE_CREATED,						//
		MR_MSG_BG_PIPE_MSG,							//

		MR_MSG_BG_UPDATED_TOBG,						// toFG, toBG
		MR_MSG_BG_UPDATED_SYNC,						//
		MR_MSG_BG_INPUT_EVENT,						//
		MR_MSG_BG_BEFORE_DISCUSSION,				//
		MR_MSG_BG_DISCUSS_DATA_OWNERSHIP,			//
		MR_MSG_BG_CREATE_OUTPUT,					//
		MR_MSG_BG_CONTROLLER_MSG,					//
		MR_MSG_BG_CREATE_DETAILS,					//
		MR_MSG_BG_COLLIDED_ENT,						//
			

	MR_MSG_BG_HIGH,
} MovementRequesterMsgType;

#define MR_MSG_TYPE_IS_FG(msgType) ((msgType) & MR_MSG_FG_BIT)
#define MR_MSG_TYPE_IS_BG(msgType) ((msgType) & MR_MSG_BG_BIT)

typedef enum MovementRequesterHandledMsgFlagBG {
	MR_HANDLED_MSG_INPUT_EVENT						= BIT(0),
	MR_HANDLED_MSG_BEFORE_DISCUSSION				= BIT(1),
	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP			= BIT(2),
	MR_HANDLED_MSG_CREATE_DETAILS					= BIT(3),
	MR_HANDLED_MSG_OVERRIDE_VALUE_SHOULD_REJECT		= BIT(4),
	
	MR_HANDLED_MSG_OUTPUT_POSITION_TARGET			= BIT(5),
	MR_HANDLED_MSG_OUTPUT_POSITION_CHANGE			= BIT(6),
	MR_HANDLED_MSG_OUTPUT_ROTATION_TARGET			= BIT(7),
	MR_HANDLED_MSG_OUTPUT_ROTATION_CHANGE			= BIT(8),
	MR_HANDLED_MSG_OUTPUT_ANIMATION					= BIT(9),
	
	MR_HANDLED_MSGS_OUTPUT_ALL						=	MR_HANDLED_MSG_OUTPUT_POSITION_TARGET |
														MR_HANDLED_MSG_OUTPUT_POSITION_CHANGE |
														MR_HANDLED_MSG_OUTPUT_ROTATION_TARGET |
														MR_HANDLED_MSG_OUTPUT_ROTATION_CHANGE |
														MR_HANDLED_MSG_OUTPUT_ANIMATION,

	MR_HANDLED_MSGS_OUTPUT_CHANGE_ALL				=	MR_HANDLED_MSG_OUTPUT_POSITION_CHANGE |
														MR_HANDLED_MSG_OUTPUT_ROTATION_CHANGE |
														MR_HANDLED_MSG_OUTPUT_ANIMATION,

	MR_HANDLED_MSGS_DEFAULT							= MR_HANDLED_MSGS_OUTPUT_ALL,
	
	MR_HANDLED_MSGS_BIT_COUNT						= 10,
	MR_HANDLED_MSGS_ALL								= BIT_RANGE(0, MR_HANDLED_MSGS_BIT_COUNT),
} MovementRequesterHandledMsgFlagBG;

typedef struct MovementRequesterMsgOut {
	union {
		union {
			struct {
				U32								id;
			} getClassID;
		} misc;

		union {
			struct {
				U32								denied		: 1;
			} dataReleaseRequested;

			struct {
				U32								denied		: 1;
			} detailAnimBitRequested;

			struct {
				U32								denied		: 1;
			} detailResourceRequested;

			struct {
				U32								onGround	: 1;
				Vec3							normal;
			} queryOnGround;

			struct {
				Vec3							vel;
			} queryVelocity;

			struct {
				F32								maxSpeed;
			} queryMaxSpeed;

			struct {
				F32								addedRadius;
				EntityRef						erTarget;
			} queryLurchInfo;

			struct {
				U32								isSettled	: 1;
			} queryIsSettled;

			struct {
				F32								turnRate;
			} queryTurnRate;
			
			struct {
				U32								unused : 1;
			} initRepredictSimBody;

			struct {
				U32								shouldReject : 1;
			} overrideValueShouldReject;
		} bg;
	};
} MovementRequesterMsgOut;

typedef struct MovementRequesterMsgCreateOutputShared {
	struct {
		Vec3									pos;
		Quat									rot;
	} orig;
	
	struct {
		const MovementPositionTarget*			pos;
		const MovementRotationTarget*			rot;
		F32										minSpeed;
	} target;
} MovementRequesterMsgCreateOutputShared;

typedef enum MovementRequesterPipeMsgType {
	MR_PIPE_MSG_INVALID,
	MR_PIPE_MSG_STRING,
} MovementRequesterPipeMsgType;

typedef struct MovementRequesterMsg {
	MovementRequesterMsgPrivateData*			pd;
	MovementRequesterMsgOut*					out;

	struct {
		MovementRequesterMsgType				msgType;

		struct {
			U32									debugging : 1;
		} flags;

		struct {
			void*								fg;
			void*								bg;
			void*								localBG;
			void*								toFG;
			void*								toBG;
			void*								sync;
			void*								syncPublic;
		} userStruct;

		union {
			struct {
				char*							buffer;
				U32								bufferLen;
			} getSyncDebugString;

			// fg.

			struct {
				MovementRequester*				mr;

				union {
					struct {
						struct {
							U32					first;
							U32					last;
						} spc;
					} createToBG;
				};
			} fg;

			// bg.

			union {
				struct {
					U32							dataClassBits;
				} dataReleaseRequested;

				struct {
					U32							dataClassBits;
				} dataWasReleased;

				struct {
					MovementRequester*			mrOther;
				} detailAnimBitRequested;

				struct {
					MovementRequester*			mrOther;
				} detailResourceRequested;

				struct {
					char*						buffer;
					U32							bufferLen;
				} getDebugString;

				struct {
					struct {
						U32						isDuringCreateOutput			: 1;
					} flags;
				} discussDataOwnership;

				struct {
					U32							dataClassBit;
					
					const MovementRequesterMsgCreateOutputShared* shared;

					struct {
						U32						isFirstCreateOutputOnThisStep	: 1;
						U32						isLastStepOnThisFrame			: 1;
					} flags;
				} createOutput;

				struct {
					MovementInputValue			value;
				} inputEvent;

				struct {
					Vec3						pos;
					Vec3						normal;
					U32							isGround : 1;
				} controllerMsg;
				
				struct {
					U32							dataClassBits;
					const MovementSharedData*	sharedData;
				} receiveOldData;
				
				struct {
					U32							handle;
				} initRepredictSimBody;

				struct {
					const WorldCollIntegrationMsg*	wciMsg;
				} wcoActorCreated;
				
				struct {
					U32								handleOld;
					U32								handleNew;
				} mmrHandleChanged;
				
				struct {
					const char*						name;
					MovementSharedDataType			valueType;
					MovementSharedDataValue			value;
				} overrideValueShouldReject;
				
				struct {
					const char*						name;
					MovementSharedDataType			valueType;
					MovementSharedDataValue			value;
				} overrideValueSet;

				struct {
					const char*						name;
				} overrideValueUnset;

				struct {
					U32								handle;
				} overrideValueDestroyed;
				
				struct {
					U32								handle;
				} pipeDestroyed;
				
				struct {
					struct {
						EntityRef					er;
						U32							mrClassID;
						U32							mrHandle;
					} source;
				} pipeCreated;
				
				struct {
					MovementRequesterPipeMsgType	msgType;
					
					union {
						const char*					string;
					};
				} pipeMsg;
			} bg;
		};
	} in;
} MovementRequesterMsg;

typedef void (*MovementRequesterMsgHandler)(const MovementRequesterMsg* msg);

typedef struct MovementRequesterClassParseTables {
	ParseTable*		fg;
	ParseTable*		bg;
	ParseTable*		localBG;
	ParseTable*		toFG;
	ParseTable*		toBG;
	ParseTable*		sync;
	ParseTable*		syncPublic;
} MovementRequesterClassParseTables;

#define _AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER_MAIN(	msgHandlerFunc,						\
															msgHandlerName,						\
															typePrefix)							\
		S32	mmRequesterMsgHandlerRegisterName(	MovementRequesterMsgHandler msgHandler,			\
												const char* name,								\
												const MovementRequesterClassParseTables* ptis,	\
												S32 syncToClient);								\
		typedef struct ParseTable ParseTable;													\
		void msgHandlerFunc(const MovementRequesterMsg* msg);									\
		extern ParseTable parse_##typePrefix##FG[];												\
		extern ParseTable parse_##typePrefix##BG[];												\
		extern ParseTable parse_##typePrefix##LocalBG[];										\
		extern ParseTable parse_##typePrefix##ToFG[];											\
		extern ParseTable parse_##typePrefix##ToBG[];											\
		extern ParseTable parse_##typePrefix##Sync[];											\
		MovementRequesterClassParseTables ptis = {0};											\
		ptis.fg = parse_##typePrefix##FG;														\
		ptis.bg = parse_##typePrefix##BG;														\
		ptis.localBG = parse_##typePrefix##LocalBG;												\
		ptis.toFG = parse_##typePrefix##ToFG;													\
		ptis.toBG = parse_##typePrefix##ToBG;													\
		ptis.sync = parse_##typePrefix##Sync													\

#define AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	msgHandlerFunc,								\
													msgHandlerName,								\
													typePrefix)									\
	void _AUTOGEN_MM_REGISTER_REQUESTER_MSG_HANDLER##msgHandlerFunc(void){						\
		_AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER_MAIN(	msgHandlerFunc,						\
															msghandlerName,						\
															typePrefix);						\
		mmRequesterMsgHandlerRegisterName(	msgHandlerFunc,										\
											msgHandlerName,										\
											&ptis,												\
											1);													\
	}

#define AUTO_RUN_MM_REGISTER_UNSYNCED_REQUESTER_MSG_HANDLER(msgHandlerFunc,						\
															msgHandlerName,						\
															typePrefix)							\
	void _AUTOGEN_MM_REGISTER_UNSYNCED_REQUESTER_MSG_HANDLER##msgHandlerFunc(void){				\
		_AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER_MAIN(	msgHandlerFunc,						\
															msghandlerName,						\
															typePrefix);						\
		mmRequesterMsgHandlerRegisterName(	msgHandlerFunc,										\
											msgHandlerName,										\
											&ptis,												\
											0);													\
	}

#define AUTO_RUN_MM_REGISTER_PUBLIC_REQUESTER_MSG_HANDLER(	msgHandlerFunc,						\
															msgHandlerName,						\
															typePrefix)							\
	void _AUTOGEN_MM_REGISTER_PUBLIC_REQUESTER_MSG_HANDLER##msgHandlerFunc(void){				\
		extern ParseTable parse_##typePrefix##SyncPublic[];										\
		_AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER_MAIN(	msgHandlerFunc,						\
															msghandlerName,						\
															typePrefix);						\
		ptis.syncPublic = parse_##typePrefix##SyncPublic;										\
		mmRequesterMsgHandlerRegisterName(	msgHandlerFunc,										\
											msgHandlerName,										\
											&ptis,												\
											1);													\
	}

#define MR_MSG_HANDLER_GET_DATA(msg, typePrefix, fg, bg, localBG, toFG, toBG, sync)				\
	fg = (typePrefix##FG*)msg->in.userStruct.fg;												\
	bg = (typePrefix##BG*)msg->in.userStruct.bg;												\
	localBG = (typePrefix##LocalBG*)msg->in.userStruct.localBG;									\
	toFG = (typePrefix##ToFG*)msg->in.userStruct.toFG;											\
	toBG = (typePrefix##ToBG*)msg->in.userStruct.toBG;											\
	sync = (typePrefix##Sync*)msg->in.userStruct.sync

#define MR_MSG_HANDLER_GET_DATA_BG(msg, typePrefix, bg)											\
	bg = (typePrefix##BG*)msg->in.userStruct.bg

#define MR_MSG_HANDLER_GET_DATA_FG(msg, typePrefix, fg)											\
	fg = (typePrefix##FG*)msg->in.userStruct.fg

#define MR_MSG_HANDLER_GET_DATA_TOBG(msg, typePrefix, toBG)										\
	toBG = (typePrefix##ToBG*)msg->in.userStruct.toBG

#define MR_MSG_HANDLER_GET_DATA_TOFG(msg, typePrefix, toFG)										\
	toFG = (typePrefix##ToFG*)msg->in.userStruct.toFG

#define MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, typePrefix)										\
	MR_MSG_HANDLER_GET_DATA(msg, typePrefix, fg, bg, localBG, toFG, toBG, sync)

#define MR_MSG_HANDLER_GET_DATA_DEFAULT_PUBLIC(msg, typePrefix)									\
	MR_MSG_HANDLER_GET_DATA(msg, typePrefix, fg, bg, localBG, toFG, toBG, sync);				\
	syncPublic = (typePrefix##SyncPublic*)msg->in.userStruct.syncPublic

#define MR_GET_FG(mr, msgHandler, typePrefix, fg)												\
	(	(0?(*(typePrefix##FG***)0)=fg:0),														\
		mrGetFG(mr, msgHandler, fg))

#define MR_GET_SYNC(mr, msgHandler, typePrefix, sync)											\
	(	(0?(*(typePrefix##Sync***)0)=sync:0),													\
		mrGetSyncFG(mr, msgHandler, sync, NULL))

#define MR_GET_SYNC_PUBLIC(mr, msgHandler, typePrefix, sync, syncPublic)						\
	(	(0?(*(typePrefix##Sync***)0)=sync:0),													\
		(0?(*(typePrefix##SyncPublic***)0)=syncPublic:0),										\
		mrGetSyncFG(mr, msgHandler, sync, syncPublic))

#define MR_SYNC_SET_IF_DIFF(mr, a, b)															\
	(((a) != (b))?((a) = (b)),mrEnableMsgUpdatedSync(mr),1:0)

#define MR_SYNC_SET_IF_DIFF_WITH_AFTER(mr, a, b)															\
	(((a) != (b))?((a) = (b)),mrEnableMsgUpdatedSyncWithAfterSync(mr),1:0)

typedef enum MovementManagedResourceMsgType {
	MMR_MSG_GET_CONSTANT_DEBUG_STRING,

	MMR_MSG_FG_LOW,
		MMR_MSG_FG_TEMP_FIX_FOR_DEMO,
		MMR_MSG_FG_FIXUP_CONSTANT_AFTER_COPY,
		
		MMR_MSG_FG_TRANSLATE_SERVER_TO_CLIENT,

		MMR_MSG_FG_GET_STATE_DEBUG_STRING,

		MMR_MSG_FG_SET_STATE,
		MMR_MSG_FG_DESTROYED,
		MMR_MSG_FG_DEBUG_DRAW,
		MMR_MSG_FG_ALWAYS_DRAW,
		MMR_MSG_FG_TRIGGER,
		MMR_MSG_FG_SET_ANIM_BITS,
		MMR_MSG_FG_WAKE,
		MMR_MSG_FG_CHECK_FOR_INVALID_STATE,
		MMR_MSG_FG_LOG_STATE,
		MMR_MSG_FG_ALL_BODIES_DESTROYED,
	MMR_MSG_FG_HIGH,
	
	MMR_MSG_BG_LOW,
		MMR_MSG_BG_GET_STATE_DEBUG_STRING,

		MMR_MSG_BG_SET_STATE,
		MMR_MSG_BG_DESTROYED,
		MMR_MSG_BG_ALL_BODIES_DESTROYED,
	MMR_MSG_BG_HIGH,

	MMR_MSG_COUNT,
} MovementManagedResourceMsgType;

typedef struct MovementManagedResourceMsgOut {
	union {
		union {
			struct {
				struct {
					U32						needsRetry : 1;
				} flags;
			} setState;
			
			struct {
				struct {
					U32						eatTrigger : 1;
				} flags;
			} trigger;
		} fg;

		union {
			struct {
				struct {
					U32						needsRetry : 1;
				} flags;
			} setState;
		} bg;
	};
} MovementManagedResourceMsgOut;

typedef struct MovementManagedResourceMsg {
	MovementManagedResourceMsgPrivateData*			pd;

	MovementManagedResourceMsgOut*					out;

	struct {
		MovementManagedResourceMsgType				msgType;
		const void*									constant;
		const void*									constantNP;
		void*										activatedStruct;
		U32											handle;
		MovementManager*							mm;
		
		struct {
			U32										debugging : 1;
			U32										clear : 1;
		} flags;

		union {
			struct {
				char**								estrBuffer;
			} getDebugString;

			struct {
				void*								mmUserPointer;

				union {
					struct {
						char**						estrBuffer;
					} getStateDebugString;
					
					struct {
						struct {
							struct {
								const void*			olderStruct;
								const void*			newerStruct;
								F32					interpOlderToNewer;
								U32					spcStart;
							} net, local;
						} state;
						
						F32							interpLocalToNet;
					} setState;
				
					struct {
						Mat4						matWorld;
						const MovementDrawFuncs*	drawFuncs;
					} debugDraw;
					
					struct {
						Mat4						matWorld;
						const MovementDrawFuncs*	drawFuncs;
					} alwaysDraw;
					
					struct {
						const MovementTrigger*		trigger;
					} trigger;

					struct {
						U32							eventID;
					} triggerFromEventID;

					struct {
						void*						constant;
						void*						constantNP;
					} tempFixForDemo;

					struct {
						void*						constant;
						void*						constantNP;
					} translateServerToClient;
					
					struct {
						void*						constant;
						void*						constantNP;
						MovementManager*			mm;
						EntityRef					er;
						const MovementManager*		mmSource;
						EntityRef					erSource;
					} fixupConstantAfterCopy;
				};
			} fg;

			union {
				struct {
					char**							estrBuffer;
				} getStateDebugString;
			} bg;
		};
	} in;
} MovementManagedResourceMsg;

typedef void (*MovementManagedResourceMsgHandler)(const MovementManagedResourceMsg* msg);

typedef struct MovementManagedResourceClassParseTables {
	ParseTable*								constant;
	ParseTable*								constantNP;
	ParseTable*								state;
	ParseTable*								activatedFG;
	ParseTable*								activatedBG;
} MovementManagedResourceClassParseTables;

#define AUTO_RUN_MM_REGISTER_RESOURCE_MSG_HANDLER(	msgHandlerFunc,								\
													msgHandlerName,								\
													typePrefix,									\
													approvingMDCBit)							\
	typedef struct ParseTable ParseTable;														\
	void _AUTOGEN_MM_REGISTER_RESOURCE_MSG_HANDLER##msgHandlerFunc(void){						\
		S32 mmRegisterManagedResourceClass(	const char* name,									\
											MovementManagedResourceMsgHandler msgHandler,		\
											const MovementManagedResourceClassParseTables* ptis,\
											U32 approvingMDCBit);								\
		void msgHandlerFunc(const MovementManagedResourceMsg* msg);								\
		extern ParseTable parse_##typePrefix##Constant[];										\
		extern ParseTable parse_##typePrefix##ConstantNP[];										\
		extern ParseTable parse_##typePrefix##State[];											\
		extern ParseTable parse_##typePrefix##ActivatedFG[];									\
		extern ParseTable parse_##typePrefix##ActivatedBG[];									\
		MovementManagedResourceClassParseTables ptis;											\
		ptis.constant = parse_##typePrefix##Constant;											\
		ptis.constantNP = parse_##typePrefix##ConstantNP;										\
		ptis.state = parse_##typePrefix##State;													\
		ptis.activatedFG = parse_##typePrefix##ActivatedFG;										\
		ptis.activatedBG = parse_##typePrefix##ActivatedBG;										\
		mmRegisterManagedResourceClass(	msgHandlerName,											\
										msgHandlerFunc,											\
										&ptis,													\
										approvingMDCBit);										\
	}

AUTO_STRUCT;
typedef struct MovementClientStatsFrameFlags {
	U32												isCorrectionFrame : 1;
} MovementClientStatsFrameFlags;

AUTO_STRUCT;
typedef struct MovementClientStatsFrame {
	U32												serverStepCount;
	U32												leftOverSteps;
	U32												behind;
	U32												usedSteps;
	U32												forcedSteps;
	U32												skipSteps;
	U32												consolidateStepsEarly;
	U32												consolidateStepsLate;
	MovementClientStatsFrameFlags					flags;
} MovementClientStatsFrame;

AUTO_STRUCT;
typedef struct MovementClientStatsFrames {
	MovementClientStatsFrame*						frames; AST(BLOCK_EARRAY)
	U32												count;
} MovementClientStatsFrames;

AUTO_STRUCT;
typedef struct MovementClientStatsPacketFlags {
	U32												notMovementPacket : 1;
} MovementClientStatsPacketFlags;

AUTO_STRUCT;
typedef struct MovementClientStatsPacket {
	S32												msOffsetFromExpectedTime;
	U32												msLocalOffsetFromLastPacket;
	U32												size;
	S32												spcOffset;
	MovementClientStatsPacketFlags					flags;
} MovementClientStatsPacket;

AUTO_STRUCT;
typedef struct MovementClientStatsPacketArray {
	MovementClientStatsPacket*						packets; AST(BLOCK_EARRAY)
	U32												count;
} MovementClientStatsPacketArray;

AUTO_STRUCT;
typedef struct MovementClientStatsStored {
	MovementClientStatsPacketArray*					packetsFromClient;
	MovementClientStatsPacketArray*					packetsFromServer;
	MovementClientStatsFrames*						frames;
} MovementClientStatsStored;

typedef enum MovementManagerMsgType {
	// FG msgs.
	
	MM_MSG_FG_LOW,
		MM_MSG_FG_DESTROY,
		MM_MSG_FG_VIEW_STATUS_CHANGED,
		MM_MSG_FG_SET_VIEW,
		MM_MSG_FG_LOG_SKELETON,
		MM_MSG_FG_LOG_VIEW,
		MM_MSG_FG_FIRST_VIEW_SET,
		MM_MSG_FG_COLL_RADIUS_CHANGED,
		MM_MSG_FG_QUERY_POS_AND_ROT,
		MM_MSG_FG_INPUT_VALUE_CHANGED,
		MM_MSG_FG_LOG_BEFORE_SIMULATION_SLEEPS,
		MM_MSG_FG_NEARBY_GEOMETRY_DESTROYED,
		MM_MSG_FG_FIND_UNOBSTRUCTED_POS,
		MM_MSG_FG_MOVE_ME_SOMEWHERE_SAFE,
		MM_MSG_FG_UPDATE_FROM_BG,
		MM_MSG_FG_AFTER_SEND_UPDATE_TO_BG,
		MM_MSG_FG_GET_DEBUG_STRING_AFTER_SIMULATION_WAKES,
		MM_MSG_FG_QUERY_IS_POS_VALID,
		MM_MSG_FG_REQUESTER_CREATED,
		MM_MSG_FG_REQUESTER_DESTROYED,
		MM_MSG_FG_LOGGING_ENABLED,
		MM_MSG_FG_LOGGING_DISABLED,
	MM_MSG_FG_HIGH,
	
	// BG msgs.
	
	MM_MSG_BG_LOW,
		MM_MSG_BG_POS_CHANGED,
		MM_MSG_BG_UPDATE_FROM_FG,
		MM_MSG_BG_AFTER_SEND_UPDATE_TO_FG,
	MM_MSG_BG_HIGH,
} MovementManagerMsgType;

#define MM_MSG_IS_FG(msgType) ((msgType) > MM_MSG_FG_LOW && (msgType) < MM_MSG_FG_HIGH)
#define MM_MSG_IS_BG(msgType) ((msgType) > MM_MSG_BG_LOW && (msgType) < MM_MSG_BG_HIGH)

typedef struct MovementManagerMsgOut {
	union {
		union {
			struct {
				Vec3								pos;
				Quat								rot;
				
				struct {
					U32								didSet : 1;
				} flags;
			} queryPosAndRot;
			
			struct {
				Vec3								pos;
				
				struct {
					U32								found : 1;
				} flags;
			} findUnobstructedPos;
			
			struct {
				char*								buffer;
				U32									bufferLen;
			} getDebugString;
			
			struct {
				struct {
					U32								posIsValid : 1;
				} flags;
			} queryIsPosValid;
		} fg;
	};
} MovementManagerMsgOut;

typedef struct MovementManagerMsg {
	MovementManagerMsgType							msgType;
	void*											userPointer;
	MovementManagerMsgOut*							out;
	
	union {
		union {
			struct {
				struct {
					U32								posIsAtRest		: 1;
					U32								rotIsAtRest		: 1;
					U32								pyFaceIsAtRest	: 1;
				} flags;
			} viewStatusChanged;

			struct {
				F32									radius;
			} collRadiusChanged;
			
			struct {
				const F32*							vec3Pos;
				const F32*							quatRot;
				const F32*							vec2FacePitchYaw;
				const F32*							vec3Offset;
				U32									netViewInitUnmoving : 1;
			} setView;
			
			struct {
				MovementInputValueIndex				index;
				MovementInputValue					value;
			} inputValueChanged;

			struct {
				Vec3								posStart;
			} findUnobstructedPos;
			
			struct {
				void*								threadData;
			} updateFromBG;
			
			struct {
				void*								threadData;
			} afterSendUpdateToBG;
			
			struct {
				const F32*							vec3Pos;
			} queryIsPosValid;
			
			struct {
				MovementRequester*					mr;
			} requesterCreated;

			struct {
				const MovementRequester*			mr;
			} requesterDestroyed;
		} fg;
		
		union {
			struct {
				void*								threadData;
				const F32*							pos;
			} posChanged;

			struct {
				void*								threadData;
			} updateFromFG;

			struct {
				void*								threadData;
			} afterSendUpdateToFG;
		} bg;
	};
} MovementManagerMsg;

typedef void (*MovementManagerMsgHandler)(const MovementManagerMsg* msg);

typedef struct MovementTrigger {
	U32				triggerID;
	//Vec3			pos;
	Vec3			vel;
	
	struct {
		U32			isEntityID : 1;
	} flags;
} MovementTrigger;

// Body stuff.

typedef enum MovementGeometryType {
	MM_GEO_INVALID,
	MM_GEO_MESH,
	MM_GEO_GROUP_MODEL,
	MM_GEO_WL_MODEL,
} MovementGeometryType;

typedef struct MovementGeometryDesc {
	MovementGeometryType			geoType;
	union {
		struct {
			const F32*				verts;
			const U32*				tris;
			U32						vertCount;
			U32						triCount;
		} mesh;

		struct {
			const char*				fileName;
			const char*				modelName;
			Vec3					scale;
		} model;
	};
} MovementGeometryDesc;

// Global stuff.

typedef enum MovementConflictResolution {
	MCR_ASK_OWNER,
	MCR_RELEASE_DENIED,
	MCR_RELEASE_ALLOWED,
} MovementConflictResolution;

typedef struct MovementOwnershipConflict {
	struct {
		MovementRequester*					mrOwner;
		U32									mrOwnerClassID;
		MovementRequester*					mrRequester;
		U32									mrRequesterClassID;
		U32									dataClassBits;
	} in;

	struct {
		MovementConflictResolution			resolution;
	} out;
} MovementOwnershipConflict;

typedef void (*MovementConflictResolverCB)(MovementOwnershipConflict* conflict);

typedef enum MovementGlobalMsgType {
	MG_MSG_CREATE_PACKET_TO_SERVER,
	MG_MSG_SEND_PACKET_TO_SERVER,
	MG_MSG_GET_GEOMETRY_DATA,
	MG_MSG_FRAME_UPDATED,
} MovementGlobalMsgType;

typedef void (*MovementGetGeometryDataCB)(	void* userPointer,
											const U32* tris,
											U32 triCount,
											const F32* verts,
											U32 vertCount);

typedef struct MovementGlobalMsg {
	MovementGlobalMsgType			msgType;
	
	union {
		struct {
			Packet**				pakOut;
		} createPacketToServer;
		
		struct {
			Packet*					pak;
		} sendPacketToServer;

		struct {
			MovementGeometryType	geoType;
			const char*				fileName;
			const char*				modelName;
			Vec3					scale;

			struct {
				MovementGetGeometryDataCB	cb;
				void*						userPointer;
			} cb;
		} getGeometryData;

		struct {
			U32						frameCount;
		} frameUpdated;
	};
} MovementGlobalMsg;

typedef void (*MovementGlobalMsgHandler)(const MovementGlobalMsg* msg);

S32		mmSetOwnershipConflictResolver(MovementConflictResolverCB cb);

void	mmGlobalSetMsgHandler(MovementGlobalMsgHandler msgHandler);

void	mmSetIsServer(void);

void	mmSetLocalProcessing(S32 enabled);

#define MM_ENTITY_HIT_REACT_ID(er,id)		((((id) & 0xff) << 8) | (er))

void	mmDynFxHitReactCallback(U32 triggerID,
								const Vec3 pos,
								const Vec3 vel);

void	mmDynAnimHitReactCallback(	EntityRef er,
									U32 uid,
									const Vec3 pos,
									const Vec3 vel);

// WorldColl callbacks.

void	mmProcessRawInputOnClientFG(const FrameLockedTimer* flt);

void	mmCreateWorldCollIntegration(void);

// MovementClient stuff.

typedef enum MovementClientMsgType {
	MC_MSG_CREATE_PACKET_TO_CLIENT,
	MC_MSG_SEND_PACKET_TO_CLIENT,
	MC_MSG_LOG_STRING,
} MovementClientMsgType;

typedef struct MovementClientMsg {
	MovementClientMsgType		msgType;
	MovementClient*				mc;
	void*						userPointer;
	
	union {
		struct {
			Packet**			pakOut;
		} createPacketToClient;
		
		struct {
			Packet*				pak;
		} sendPacketToClient;

		struct {
			const char*			text;
			const char*			error;
		} logString;
	};
} MovementClientMsg;

typedef void (*MovementClientMsgHandler)(const MovementClientMsg* msg);

void	mmClientCreate(	MovementClient** mcOut,
						MovementClientMsgHandler msgHandler,
						void* userPointer);

void	mmClientDestroy(MovementClient** mcInOut);

void	mmClientReceiveFromServer(Packet* pak);

void	mmClientReceiveFromClient(	MovementClient* mc,
									Packet* pak);

void	mmClientSetSendLogListUpdates(S32 enabled);

void	mmClientRequestNetAutoDebug(MovementClient* mc);

void	mmClientStatsSetFramesEnabled(	MovementClient* mc,
										bool enabled);

void	mmClientStatsSetPacketTimingEnabled(MovementClient* mc,
											bool enabled);

S32		mmAttachToClient(	MovementManager* mm,
							MovementClient* mc);

S32		mmDetachFromClient(	MovementManager* mm,
							MovementClient* mc);

S32		mmIsAttachedToClient( MovementManager *mm );

S32		mmClientDetachManagers(MovementClient* mc);

S32		mmClientGetManagerCount(MovementClient* mc);

S32		mmClientGetManager(	MovementManager** mmOut,
							MovementClient* mc,
							U32 index);

S32		mmGetLocalManagerByIndex(	MovementManager** mmOut,
									U32 index);
							
S32		mmClientSetSendFullRotations(	MovementClient* mc,
										S32 enabled);

S32		mmClientResetSendState(MovementClient* mc);

// Global queries.

U32		mmGetReleaseBit(void); //used for a temp hack related to mounts, need to fix this

S32		mmIsForegroundThread(void);
S32		mmIsBackgroundThread(void);

U32		mmGetFrameCount(void);

U32		mmGetAnimBitHandleByName(	const char* name,
									S32 isFlashBit);

void	mmGetLocalAnimBitHandleFromServerHandle(U32 bitHandleServer,
												U32* bitHandleLocalOut,
												U32 hasHandleId);

void	mmTranslateAnimBitServerToClient(	U32* animBitHandle,
											U32 hasHandleId);

void	mmTranslateAnimBitsServerToClient(	U32* animBitHandles,
											U32 hasHandleId);

// Logging.

void	mmSetIsForegroundThreadForLogging(void);
S32		mmIsForegroundThreadForLogging(void);

#define MMLOG_IS_ENABLED(mm) (mgPublic.activeLogCount && mmIsDebugging(mm))
#define MMLOG_WRAPPER(mm, x) (MMLOG_IS_ENABLED(mm)?wrapped_mmLog##x:0)
#define MMLOG_WRAPPERX(mm, x) (MMLOG_IS_ENABLED(mm)?wrapped_mmLog x:0)

#define MRLOG_IS_ENABLED(mr) (mgPublic.activeLogCount && mrIsDebugging(mr))
#define MRLOG_WRAPPER(mr, x) (MRLOG_IS_ENABLED(mr)?wrapped_mrLog##x:0)
#define MRLOG_WRAPPERX(mr, x) (MRLOG_IS_ENABLED(mr)?wrapped_mrLog x:0)

#define MRMLOG_IS_ENABLED(msg) (mgPublic.activeLogCount && (msg)->in.flags.debugging)
#define MRMLOG_WRAPPER(msg, x) (MRMLOG_IS_ENABLED(msg)?wrapped_mrmLog##x:0)
#define MRMLOG_WRAPPERX(msg, x) (MRMLOG_IS_ENABLED(msg)?wrapped_mrmLog x:0)

#define MMRMLOG_IS_ENABLED(msg) (mgPublic.activeLogCount && (msg)->in.flags.debugging)
#define MMRMLOG_WRAPPER(msg, x) (MMRMLOG_IS_ENABLED(msg)?wrapped_mmrmLog##x:0)
#define MMRMLOG_WRAPPERX(msg, x) (MMRMLOG_IS_ENABLED(msg)?wrapped_mmrmLog x:0)

// mmLog*

#define mmGlobalLog(format, ...)\
		(mgPublic.activeLogCount?wrapped_mmGlobalLog(FORMAT_STRING_CHECKED(format), __VA_ARGS__):0)
S32		wrapped_mmGlobalLog(FORMAT_STR const char* format,
							...);

#define mmLogv(mm, module, mr, format, va)\
		MMLOG_WRAPPER(mm, v(mm, module, mr, FORMAT_STRING_CHECKED(format), va))
S32		wrapped_mmLogv(	MovementManager* mm,
						const char* module,
						MovementRequester* mr,
						const char* format,
						va_list va);
				
#define mmLog(mm, module, format, ...)\
		MMLOG_WRAPPERX(mm, (mm, module, FORMAT_STRING_CHECKED(format), ##__VA_ARGS__))
S32		wrapped_mmLog(	MovementManager* mm,
						const char* module,
						FORMAT_STR const char* format,
						...);

#define mmLogSegmentList(mm, module, tags, argb, vecs, segmentCount)\
		MMLOG_WRAPPER(mm, SegmentList(mm, module, tags, argb, vecs, segmentCount))
S32		wrapped_mmLogSegmentList(	MovementManager* mm,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3* vecs,
									S32 segmentCount);

#define mmLogSegment(mm, module, tags, argb, a, b)\
		MMLOG_WRAPPER(mm, Segment(mm, module, tags, argb, a, b))
S32		wrapped_mmLogSegment(	MovementManager* mm,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3 a,
								const Vec3 b);

#define mmLogSegmentOffset(mm, module, tags, argb, a, offset)\
		MMLOG_WRAPPER(mm, SegmentOffset(mm, module, tags, argb, a, offset))
S32 	wrapped_mmLogSegmentOffset(	MovementManager* mm,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3 a,
									const Vec3 offset);

#define mmLogSegmentOffset2(mm, module, tags, argb, a, aOffset, b, bOffset)\
		MMLOG_WRAPPER(mm, SegmentOffset2(mm, module, tags, argb, a, aOffset, b, bOffset))
S32 	wrapped_mmLogSegmentOffset2(MovementManager* mm,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3 a,
									const Vec3 aOffset,
									const Vec3 b,
									const Vec3 bOffset);

#define mmLogPointList(mm, module, tags, argb, points, pointCount)\
		MMLOG_WRAPPER(mm, PointList(mm, module, tags, argb, points, pointCount))
S32		wrapped_mmLogPointList(	MovementManager* mm,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3* points,
								S32 pointCount);

#define mmLogPoint(mm, module, tags, argb, point)\
		MMLOG_WRAPPER(mm, Point(mm, module, tags, argb, point))
S32		wrapped_mmLogPoint(	MovementManager* mm,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3 point);

void	mmLogCameraMat(	MovementManager* mm,
						const Mat4 mat,
						const char* tag,
						const char* tagDraw);

// mrLog*

#define mrLog(mr, module, format, ...)\
        MRLOG_WRAPPERX(mr, (mr, module, FORMAT_STRING_CHECKED(format), ##__VA_ARGS__))
S32		wrapped_mrLog(	const MovementRequester* mr,
						const char* module,
						FORMAT_STR const char* format,
						...);

#define mrLogSegmentList(mr, module, tags, argb, vecs, segmentCount)\
		MRLOG_WRAPPER(mr, SegmentList(mr, module, tags, argb, vecs, segmentCount))
S32		wrapped_mrLogSegmentList(	const MovementRequester* mr,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3* vecs,
									S32 segmentCount);

#define mrLogSegment(mr, module, tags, argb, a, b)\
		MRLOG_WRAPPER(mr, Segment(mr, module, tags, argb, a, b))
S32		wrapped_mrLogSegment(	const MovementRequester* mr,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3 a,
								const Vec3 b);

#define mrLogPointList(mr, module, tags, argb, points, pointCount)\
		MRLOG_WRAPPER(mr, PointList(mr, module, tags, argb, points, pointCount))
S32		wrapped_mrLogPointList(	const MovementRequester* mr,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3* points,
								S32 pointCount);

#define mrLogPoint(mr, module, tags, argb, point)\
		MRLOG_WRAPPER(mr, Point(mr, module, tags, argb, point))
S32		wrapped_mrLogPoint(	const MovementRequester* mr,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3 point);

// mrmLog*

#define mrmLog(msg, module, format, ...)\
		MRMLOG_WRAPPERX(msg, (msg,module,FORMAT_STRING_CHECKED(format),##__VA_ARGS__))
S32		wrapped_mrmLog(	const MovementRequesterMsg* msg,
						const char* module,
						FORMAT_STR const char* format,
						...);

#define mrmLogSegmentList(msg, module, tags, argb, vecs, segmentCount)\
		MRMLOG_WRAPPER(msg, SegmentList(msg, module, tags, argb, vecs, segmentCount))
S32 	wrapped_mrmLogSegmentList(	const MovementRequesterMsg* msg,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3* vecs,
									S32 segmentCount);

#define mrmLogSegment(msg, module, tags, argb, a, b)\
		MRMLOG_WRAPPER(msg, Segment(msg, module, tags, argb, a, b))
S32 	wrapped_mrmLogSegment(	const MovementRequesterMsg* msg,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3 a,
								const Vec3 b);

#define mrmLogSegmentOffset(msg, module, tags, argb, a, offset)\
		MRMLOG_WRAPPER(msg, SegmentOffset(msg, module, tags, argb, a, offset))
S32 	wrapped_mrmLogSegmentOffset(const MovementRequesterMsg* msg,
									const char* module,
									const char* tags,
									U32 argb,
									const Vec3 a,
									const Vec3 offset);

#define mrmLogPointList(msg, module, tags, argb, points, pointCount)\
		MRMLOG_WRAPPER(msg, PointList(msg, module, tags, argb, points, pointCount))
S32		wrapped_mrmLogPointList(const MovementRequesterMsg* msg,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3* points,
								S32 pointCount);

#define mrmLogPoint(msg, module, tags, argb, point)\
		MRMLOG_WRAPPER(msg, Point(msg, module, tags, argb, point))
S32		wrapped_mrmLogPoint(const MovementRequesterMsg* msg,
							const char* module,
							const char* tags,
							U32 argb,
							const Vec3 point);

// mmrmLog*

#define mmrmLog(msg, module, format, ...)\
		MMRMLOG_WRAPPERX(msg, (msg,module,FORMAT_STRING_CHECKED(format),##__VA_ARGS__))
S32		wrapped_mmrmLog(const MovementManagedResourceMsg* msg,
						const char* module,
						FORMAT_STR const char* format,
						...);

#define mmrmLogSegment(msg, module, tags, argb, a, b)\
		MMRMLOG_WRAPPER(msg, Segment(msg, module, tags, argb, a, b))
S32 	wrapped_mmrmLogSegment(	const MovementManagedResourceMsg* msg,
								const char* module,
								const char* tags,
								U32 argb,
								const Vec3 a,
								const Vec3 b);

void	mmLogSend(	MovementClient* mc,
					EntityRef erTarget);

S32		mmLogGetCopy(	EntityRef er,
						MovementLog** logOut,
						S32 useLocalLog,
						const char* moduleName);
						
typedef void (*MovementLogsForEachCallback)(EntityRef er,
											const char* name,
											S32 isLocal);

void	mmLogsForEach(MovementLogsForEachCallback callback);

S32		mmLogDestroy(MovementLog** logInOut);

void	mmLogAllSkeletons(void);

S32		mmGetClientStatsFrames(const MovementClientStatsFrames** framesOut);

S32		mmGetClientStatsPacketsFromClient(const MovementClientStatsPacketArray** packetsOut);

S32		mmGetClientStatsPacketsFromServer(const MovementClientStatsPacketArray** packetsOut);

void	mmGetOffsetHistoryClientToServerSync(	S32* buffer,
												S32 bufferSize,
												S32* bufferSizeOut);

void	mmGetOffsetHistoryServerSyncToServer(	S32* buffer,
												S32 bufferSize,
												S32* bufferSizeOut);

// Various physics updates.

S32		mmGetCapsules(	MovementManager* mm,
						const Capsule*const** capsulesOut);

S32		mmGetCapsuleBounds(	MovementManager *mm, 
							Vec3 boundsMinOut,
							Vec3 boundsMaxOut);

S32		mmGetCollisionRadius(	MovementManager* mm,
								F32* radiusOut);

void	mmSetWorldColl(	MovementManager* mm,
						const WorldColl* wc);

// Forced position and rotation.

S32		mmSetPositionFG(MovementManager* mm,
						const Vec3 pos,
						const char* reason);

S32		mmSetRotationFG(MovementManager* mm,
						const Quat rot,
						const char* reason);

// Accessors.  Copy the value into the vec/quat provided

S32		mmGetPositionFG(MovementManager* mm,
						Vec3 posOut);

S32		mmGetRotationFG(MovementManager* mm,
						Quat rotOut);

S32		mmGetFacePitchYawFG(MovementManager* mm,
							Vec2 pyFaceOut);

void	mmUpdateCurrentViewFG(MovementManager* mm);

// Some random stuff.

S32		mmGetUserPointer(	const MovementManager* mm,
							void** userPointerOut);

S32		mmGetUserThreadData(const MovementManager* mm,
							void** userThreadDataOut);

void	mmSendMsgUserThreadDataUpdatedToBG(MovementManager* mm);

void	mmSendUserThreadDataUpdatedToFG(const MovementManagerMsg* msg);

S32		mrGetManagerUserPointer(const MovementRequester* mr,
								void** userPointerOut);

S32		mrmGetManagerUserPointerFG(	const MovementRequesterMsg* msg,
									void** userPointerOut);

S32		mmHasProcessed(MovementManager* mm);

S32		mmSetIgnoreActorCreateFG(	MovementManager *mm,
									S32 ignore);

U32		mmCollisionSetGetNextID(void);

S32		mmCollisionSetHandleCreateFG(	MovementManager* mm,
										MovementCollSetHandle** mcshOut,
										const char* fileName, 
										U32 fileLine,
										int setID);

S32		mmCollisionSetHandleDestroyFG(	MovementCollSetHandle** mcshInOut);

void	mmSetNetReceiveCollisionSetFG(	MovementManager* mm,
										S32 set);

S32		mmCollisionBitsHandleCreateFG(	MovementManager* mm,
										MovementCollBitsHandle** mcbhOut,
										const char* fileName,
										U32 fileLine,
										U32 groupBits);

S32		mmCollisionBitsHandleDestroyFG(MovementCollBitsHandle** mcbhInOut);

void	mmSetNetReceiveCollisionBitsFG(	MovementManager* mm,
										U32 bits);

S32		mmCollisionGroupHandleCreateFG(	MovementManager* mm,
										MovementCollGroupHandle** mcghOut,
										const char* fileName,
										U32 fileLine,
										U32 groupBit);

S32		mmCollisionGroupHandleDestroyFG(MovementCollGroupHandle** mcghInOut);

void	mmSetNetReceiveCollisionGroupFG(MovementManager* mm,
										U32 bits);

S32		mmNoCollHandleCreateFG(	MovementManager* mm,
								MovementNoCollHandle** nchOut,
								const char* fileName,
								U32 fileLine);

S32		mmNoCollHandleDestroyFG(MovementNoCollHandle** nchInOut);


void	mmSetNetReceiveNoCollFG(MovementManager* mm,
								bool enabled);

S32		mmGetVelocityFG( MovementManager* mm,
						 Vec3 velOut);
						 
void	mmTriggerFromEntityID(U32 entityID);

void	mmTriggerFromEventID(U32 eventID);

void	mmSetInDeathPredictionFG(MovementManager* mm, bool enabled);

void	mmSetUseRotationForCapsuleOrientationFG(MovementManager* mm, bool enabled);

S32		mrmSetUseRotationForCapsuleOrientationBG(const MovementRequesterMsg* msg, bool enabled);

// Debugging functions.

void	mmDebugDraw(const MovementDrawFuncs* funcs,
					const Vec3 posCamera,
					S32 doDrawBodies,
					S32 doDrawBounds,
					S32 doDrawNetOutputs,
					S32 doDrawOutputs);

void	mmAlwaysDraw(	const MovementDrawFuncs* funcs,
						const Mat4 matCamera);

void	mmDrawResourceDebug(const MovementDrawFuncs* funcs,
							const Mat4 matCamera);

S32		mmIsCollisionEnabled(MovementManager* mm);

S32		mmDoesCapsuleOrientationUseRotation(MovementManager* mm);

S32		mmSetDebugging(	MovementManager* mm,
						S32 enabled);

S32		mmIsDebugging(const MovementManager* mm);

S32		mrIsDebugging(const MovementRequester* mr);

void	mmDebugStopRecording(struct Entity *eTarget);

S32		mmSetWriteLogFiles(	MovementManager* mm,
							S32 enabled);

void	mmSetSyncWithServer(S32 set);

void	mmSetNoDisable(S32 set);

S32		mmIsSyncWithServerEnabled(void);

void	mmGetNetOffsetsFromEnd(	F32* normalOut,
								F32* fastOut,
								F32* lagOut);

S32		mmGetDebugLatestServerPositionFG(	MovementManager* mm,
											Vec3 posOut);

S32		mmGetLatestServerPosFaceFG(	MovementManager* mm,
									Vec3 posOut,
									Vec2 pyOut);

S32		mmGetPositionRotationAtTimeFG(	MovementManager* mm,
										U32 timeStamp,
										Vec3 posOut,
										Quat qRotOut);

void	mmCopyServerLogList(U32** logListOut);

void	mmCopyLocalLogList(MovementManager*** logListOut);

void	mmDebugValidateResources(void);

// Input.

void	mmGetInputValueIndexName(	MovementInputValueIndex mivi,
									const char** nameOut);

S32		mmInputEventSetValueBitTracked(	MovementManager* mm,
										MovementInputValueIndex mivi,
										S32	value,
										U32 msTime,
										S32 canDoubleTap,
										S32 *pIsDoubleTap,
										const char* fileName,
										U32 fileLine);

#define mmInputEventSetValueBit(mm, index, value, msTime, canDoubleTap) \
			mmInputEventSetValueBitTracked(mm, index, value, msTime, canDoubleTap, NULL, __FILE__, __LINE__);

S32		mmInputEventSetValueF32Tracked(	MovementManager* mm,
										MovementInputValueIndex mivi,
										F32	value,
										U32 msTime,
										const char* fileName,
										U32 fileLine);

#define mmInputEventSetValueF32(mm, index, value, msTime) \
			mmInputEventSetValueF32Tracked(mm, index, value, msTime, __FILE__, __LINE__);

S32		mmInputEventDebugCommand(	MovementManager* mm,
									const char* command,
									U32 msTime);

S32		mmGetLastQueuedInputValueBitFG(	MovementManager* mm, 
										MovementInputValueIndex mivi);

// Resource initialization.

S32		mmGetManagedResourceIDByMsgHandler(	MovementManagedResourceMsgHandler msgHandler,
											U32* idOut);

S32		mmRegisterManagedResourceClassID(	const char* name,
											U32 id);

// Beacon movement stuff.

void	mmBeaconControllerSetPosTargetSteps(int iPartitionIdx,
											const Vec3 pos,
											const Vec3 target,
											S32 count);

S32		mmBeaconControllerReachedTarget(void);

void	mmBeaconControllerGetResults(	S32* result,
										S32* optional,
										F32* dist);

// Offset thingy (somewhat temporary).

S32		mmCreateOffsetInstanceFG(	MovementManager* mm,
									F32 rotationOffset,
									MovementOffsetInstance** instanceOut);

S32		mmDestroyOffsetInstanceFG(	MovementManager* mm,
									MovementOffsetInstance** instanceInOut);

// MovementRequester.

S32		mmRequesterGetByNameFG(	MovementManager* mm,
								const char* name,
								MovementRequester** mrOut);

S32		mrNameRegisterID(	const char* name,
							U32 id);

S32		mmRequesterCreateBasic(	MovementManager* mm,
								MovementRequester** mrOut,
								MovementRequesterMsgHandler msgHandler);

S32		mmRequesterCreateBasicByName(	MovementManager* mm,
										MovementRequester** mrOut,
										const char* name);

S32		mmRequesterCreate(	MovementManager* mm,
							MovementRequester** mrOut,
							U32* mrHandleOut,
							MovementRequesterMsgHandler msgHandler,
							U32 id);

S32		wrapped_mrDestroy(SA_PRE_NN_OP_VALID SA_POST_NN_NULL MovementRequester** mrInOut,
							const char* fileName,
							const U32 fileLine);
#define mrDestroy(mrInOut) wrapped_mrDestroy(mrInOut, __FILE__, __LINE__)

S32		mrEnableMsgCreateToBG(MovementRequester* mr);

S32		mrEnableMsgUpdatedSync(MovementRequester* mr);

S32		mrEnableMsgUpdatedSyncWithAfterSync(MovementRequester* mr);

S32		mrGetFG(MovementRequester* mr,
				MovementRequesterMsgHandler msgHandler,
				void** fgOut);

S32		mrGetSyncFG(MovementRequester* mr,
					MovementRequesterMsgHandler msgHandler,
					void** syncFGOut,
					void** syncPublicFGOut);

S32		mrGetHandleFG(	MovementRequester* mr,
						U32* handleOut);

// MovementRequesterMsg FG or BG things.

S32		mrmDestroySelf(const MovementRequesterMsg* msg);

// MovementRequesterMsg FG things.

S32		mrmEnableMsgCreateToBG(const MovementRequesterMsg* msg);

S32		mrmEnableMsgUpdatedToBG(const MovementRequesterMsg* msg);

S32		mrmEnableMsgUpdatedSyncFG(const MovementRequesterMsg* msg);

S32		mrmEnableMsgUpdatedSyncWithAfterSyncFG(const MovementRequesterMsg* msg);

S32		mrmGetManagerFG(const MovementRequesterMsg* msg,
						MovementManager** mmOut);

S32		mrmRequesterCreateFG(	const MovementRequesterMsg* msg,
								MovementRequester** mrOut,
								const char* name,
								MovementRequesterMsgHandler msgHandler,
								U32 id);

// WorldCollObject queries.

S32		mmGetFromWCO(	const WorldCollObject* wco,
						MovementManager** mmOut);

S32		mmGetUserPointerFromWCO(const WorldCollObject* wco,
								void** userPointerOut);

// Networking.

void	mmAllBeforeSendingToClients(const FrameLockedTimer* flt);

void	mmAllAfterSendingToClients(const FrameLockedTimer* flt);

S32		mmAfterSendingToClientsInThread(void* unused,
										S32 mmIndex,
										void* userPointerUnused);

void	mmClientSendHeaderToClient(	MovementClient* mc,
									Packet* pak);

void	mmClientSendFooterToClient(	MovementClient* mc,
									Packet* pak);

void	mmClientReceiveHeaderFromServer(Packet* pak,
										U32* processCountOut);

void	mmClientReceiveFooterFromServer(Packet* pak);

void	mmSendToServer(Packet* pak);

void	mmReceiveFromClient(MovementClient* mc,
							Packet* pak);

S32		mmSendToClientShouldCache(	MovementClient* mc,
									MovementManager* mm,
									S32 fullUpdate);

void	mmSendToClient(	MovementClient* mc,
						MovementManager* mm,
						Packet* pak,
						S32 fullUpdate);

void	mmReceiveFromServer(MovementManager* mm,
							Packet* pak,
							S32 fullUpdate,
							RecordedEntityUpdate* recUpdate);

void	mmReceiveHeaderFromDemoReplay(	U32 spc,
										U32 cpc);

void	mmReceiveFromDemoReplay(MovementManager* mm,
								const RecordedEntityUpdate* recUpdate,
								const U32 demoVersion);

void	mmClientRecordPacketSizeFromClient(	MovementClient* mc,
											S32 useCurrentPacket,
											U32 size);

void	mmClientRecordPacketSizeFromServer(	S32 useCurrentPacket,
											U32 size);
											
void	mmClientConnectedToServer(void);

void	mmClientDisconnectedFromServer(void);

U32		mmNetGetLatestSPC(void);

U32		mmNetGetCurrentViewSPC(F32 fSecondsOffset);

// Create and Destroy MovementManager.

S32		mmCreate(	MovementManager** mmOut,
					MovementManagerMsgHandler msgHandler,
					void* userPointer,
					EntityRef entityRef,
					U32 threadDataSize,
					const Vec3 pos,
					WorldColl* wc);

S32		mmDestroy(MovementManager** mmInOut);

S32		mmSetMsgHandler(MovementManager* mm,
						MovementManagerMsgHandler msgHandler);

S32		mmSetUserPointer(	MovementManager* mm,
							void* userPointer);

S32		mmSetUserThreadDataSize(MovementManager* mm,
								U32 size);

S32		mmGetManagerCountFG(void);

S32		mmGetManagerFG(	U32 index,
						MovementManager** mmOut);
					
S32		mmSkeletonPreUpdateCallback(const DynSkeletonPreUpdateParams* params);

// MovementDisabledHandle stuff.
					
S32		mmDisabledHandleCreate(	MovementDisabledHandle** dhOut,
								MovementManager* mm,
								const char* fileName,
								U32 fileLine);
								
S32		mmDisabledHandleDestroy(MovementDisabledHandle** dhInOut);

// Body stuff.

S32		mmGeometryCreate(	MovementGeometry** geoOut,
							const MovementGeometryDesc* geoDesc);

S32		mmGeometryGetTriangleMesh(	MovementGeometry* g,
									PSDKCookedMesh** meshOut,
									const char* name);

S32		mmGeometryGetConvexMesh(MovementGeometry* g,
								PSDKCookedMesh** meshOut,
								const char* name);

S32		mmBodyCreate(	MovementBody** bOut,
						MovementBodyDesc** bdInOut);
						
S32		mmBodyGetIndex(	MovementBody* b,
						U32* indexOut);

S32		mmBodyGetByIndex(	MovementBody** bOut,
							U32 index);

S32		mmBodyGetCapsules(	MovementBody* b,
							const Capsule*const** capsulesOut);

S32		mmBodyDescCreate(MovementBodyDesc** bdOut);

S32		mmBodyDescAddGeometry(	MovementBodyDesc* bd,
								MovementGeometry* geo,
								const Vec3 pos,
								const Vec3 pyr);

S32		mmBodyDescAddCapsule(	MovementBodyDesc* bd,
								const Capsule* capsuleToCopy);

void	mmBodyDraw(	const MovementDrawFuncs* funcs,
					const MovementBody* body,
					const Mat4 matWorld,
					U32 argb,
					S32 doDrawBounds);

S32		mmrBodyCreateFG(MovementManager* mm,
						U32* handleOut,
						MovementBody* b,
						U32 isShell,
						U32 hasOneWayCollision);

void	mmDestroyBodies(MovementManager* mm);

// Collision queries.

S32		mmTranslatePosInSpaceFG(MovementManager* mm,
								const Vec3 posStart,
								const Vec3 vecOffset,
								Vec3 posReachedOut,
								S32* hitGroundOut);

void	mmFactionUpdateFactionMatrix(const CritterFactionMatrix* factionMatrix);

void	mmSetEntityFactionIndex(MovementManager* mm, 
								S32 factionIdx);


//--- EntityMovementManagerBG.c --------------------------------------------------------------------

// Global stuff.

S32		mmIsRepredictingBG(void);

// MovementManager user thread data.

void	mmMsgSetUserThreadDataUpdatedBG(const MovementManagerMsg* msg);

// MovementRequester

S32		mrmRequesterCreateBG(	const MovementRequesterMsg* msg,
								MovementRequester** mrOut,
								const char* name,
								MovementRequesterMsgHandler msgHandler,
								U32 id);

S32		mrmIsAttachedToClientBG(const MovementRequesterMsg* msg);

S32		mrmAcquireDataOwnershipBG(	const MovementRequesterMsg* msg,
									U32 dataClassBits,
									S32 needAllBits,
									U32* acquiredBitsOut,
									U32* ownedBitsOut);

S32		mrmReleaseDataOwnershipBG(	const MovementRequesterMsg* msg,
									U32 dataClassBits);

S32		mrmReleaseAllDataOwnershipBG(const MovementRequesterMsg* msg);

S32		mrmAnimAddBitBG(const MovementRequesterMsg* msg,
						U32 bitHandle);

S32		mrmAnimStanceCreateBG(	const MovementRequesterMsg* msg,
								U32* handleOut,
								U32 animBitHandle);

S32		mrmAnimStanceDestroyBG(	const MovementRequesterMsg* msg,
								U32* handleInOut);

S32		mrmAnimStanceDestroyPredictedBG(const MovementRequesterMsg *msg);

S32		mrmAnimResetGroundSpawnBG(	const MovementRequesterMsg *msg);

S32		mrmAnimStartBG(	const MovementRequesterMsg* msg,
						U32 bitHandle,
						U32 id);

S32		mrmAnimPlayFlagBG(	const MovementRequesterMsg* msg,
							U32 bitHandle,
							U32 id);

S32		mrmAnimPlayForcedDetailFlagBG(	const MovementRequesterMsg *msg,
										U32 bitHandle,
										U32 id);

// Handled msgs.

S32		mrmHandledMsgsSetBG(const MovementRequesterMsg* msg,
							U32 handledMsgs);

S32		mrmHandledMsgsAddBG(const MovementRequesterMsg* msg,
							U32 handledMsgs);

S32		mrmHandledMsgsRemoveBG(	const MovementRequesterMsg* msg,
								U32 handledMsgs);

S32		mrmEnableMsgUpdatedToFG(const MovementRequesterMsg* msg);

// Sharing old data when ownership is released.

void	mrmShareOldS32BG(	const MovementRequesterMsg* msg,
							const char* name,
							S32 s32);

void	mrmShareOldF32BG(	const MovementRequesterMsg* msg,
							const char* name,
							F32 f32);

void	mrmShareOldVec3BG(	const MovementRequesterMsg* msg,
							const char* name,
							const Vec3 vec3);

void	mrmShareOldQuatBG(	const MovementRequesterMsg* msg,
							const char* name,
							const Quat quat);

// Queries.

U32		mmGetProcessCountAfterSecondsFG(F32 deltaSeconds);

U32		mmGetProcessCountAfterMillisecondsFG(S32 deltaMilliseconds);

F32		mmGetLocalViewSecondsSinceSPC(U32 spc);

S32		mrmGetPrimaryBodyRadiusBG(	const MovementRequesterMsg* msg,
									F32* radiusOut);

S32		mrmGetEntityPositionAndRotationBG(	const MovementRequesterMsg* msg,
											EntityRef er,
											Vec3 posOut,
											Quat rotOut);

S32		mrmGetEntityPositionBG(	const MovementRequesterMsg* msg,
								EntityRef er,
								Vec3 posOut);

S32		mrmGetEntityRotationBG(	const MovementRequesterMsg* msg,
								EntityRef er,
								Quat rotOut);

S32		mrmGetEntityFacePitchYawBG(	const MovementRequesterMsg *msg,
									EntityRef er,
									Vec2 pyFaceOut);

S32		mrmGetEntityPositionAndFacePitchYawBG(	const MovementRequesterMsg *msg,
												EntityRef er,
												Vec3 posOut,
												Vec2 pyFaceOut);

S32		mrmGetPositionAndRotationBG(const MovementRequesterMsg* msg,
									Vec3 posOut,
									Quat rotOut);

S32		mrmGetInputValueBitBG(	const MovementRequesterMsg* msg,
								MovementInputValueIndex mivi);

S32		mrmGetInputValueBitDiffBG(	const MovementRequesterMsg* msg,
									MovementInputValueIndex indexOne,
									MovementInputValueIndex indexNegativeOne);

F32		mrmGetInputValueF32BG(	const MovementRequesterMsg* msg,
								MovementInputValueIndex mivi);

S32		mrmGetPositionBG(	const MovementRequesterMsg* msg,
							Vec3 posOut);

S32		mrmGetRotationBG(	const MovementRequesterMsg* msg,
							Quat rotOut);

S32		mrmGetFacePitchYawBG(	const MovementRequesterMsg* msg,
								Vec2 pyFaceOut);

S32		mrmGetOnGroundBG(	const MovementRequesterMsg* msg,
							S32* onGroundOut,
							Vec3 groundNormalOut);

S32		mrmGetEntityOnGroundBG(	const MovementRequesterMsg* msg,
								EntityRef er,
								S32* onGroundOut);

S32		mrmGetNoCollBG(	const MovementRequesterMsg* msg, 
						S32 *noCollOut);

S32		mmGetEntityOnGroundBG(	const MovementRequesterMsg* msg,
								EntityRef ref,
								S32* onGroundOut);

S32		mrmGetVelocityBG(	const MovementRequesterMsg* msg,
							Vec3 velOut);

S32		mrmGetEntityCurrentSpeedBG(	const MovementRequesterMsg* msg,
									EntityRef er,
									F32 *currentSpeedOut);
						
S32		mrmGetMaxSpeedBG(	const MovementRequesterMsg* msg,
							F32* maxSpeedOut);

S32		mrmGetEntityMaxSpeedBG(	const MovementRequesterMsg* msg,
								EntityRef er,
								F32 *maxSpeedOut);

S32		mrmGetIsSettledBG(	const MovementRequesterMsg* msg,
							S32* isSettledOut);

S32		mrmGetTurnRateBG(	const MovementRequesterMsg* msg,
							F32* turnRateOut);
							
S32		mrmGetProcessCountBG(	const MovementRequesterMsg* msg,
								U32* processCountOut);

S32		mrmProcessCountHasPassedBG(	const MovementRequesterMsg* msg,
									U32 processCount);

S32		mrmProcessCountPlusSecondsHasPassedBG(	const MovementRequesterMsg* msg,
												U32 processCount,
												F32 seconds);

S32		mrmGetEntityDistanceBG(	const MovementRequesterMsg* msg,
								EntityRef er,
								F32* distOut,
								S32 ignoreOwnCapsule);

S32		mrmGetEntityDistanceXZBG(const MovementRequesterMsg* msg,
								EntityRef er,
								F32* distOut,
								S32 ignoreOwnCapsule);

S32		mrmGetCapsulePointDistanceBG(	const MovementRequesterMsg* msg,
										const Vec3 posTarget,
										F32* distOut,
										S32 ignoreOwnCapsule);

S32		mrmGetCapsulePointDistanceXZBG(	const MovementRequesterMsg* msg,
										const Vec3 posTarget,
										F32* distOut,
										S32 ignoreOwnCapsule);

S32		mrmGetPointEntityDistanceBG(const MovementRequesterMsg* msg,
								 	const Vec3 posSource,
								 	EntityRef target,
								 	F32* distOut);

S32		mrmGetLineEntityDistanceBG(	const MovementRequesterMsg* msg,
								 	const Vec3 lineStart,
								 	const Vec3 lineDir,
									F32 lineLen,
									F32 radius,
								 	EntityRef target,
								 	F32* distOut);

S32		mrmGetWorldCollPointDistanceXZBG(	const MovementRequesterMsg *msg,
											const Vec3 targetPos,
											F32 *distOut);

S32		mrmGetWorldCollPointDistanceBG(	const MovementRequesterMsg *msg,
										const Vec3 targetPos,
										F32 *distOut);

// Knockback and repel.

S32		mrmSetAdditionalVelBG(	const MovementRequesterMsg* msg,
								const Vec3 vel, 
								S32 isRepel, 
								S32 resetBGVel);

S32		mrmGetAdditionalVelBG(	const MovementRequesterMsg* msg,
								Vec3 velOut, 
								S32* isRepelOut, 
								S32* resetBGVelOut);

S32		mrmSetConstantPushVelBG(	const MovementRequesterMsg* msg,
									const Vec3 vel);

S32		mrmGetConstantPushVelBG(	const MovementRequesterMsg* msg,
									Vec3 velOut);

S32		mrmHasConstantPushVelBG(	const MovementRequesterMsg* msg);

// Position targeting.

S32		mrmTargetSetAsInputBG(const MovementRequesterMsg* msg);

S32		mrmTargetSetAsStoppedBG(const MovementRequesterMsg* msg);

S32		mrmTargetSetAsVelocityBG(	const MovementRequesterMsg* msg,
									const Vec3 vel);

S32		mrmTargetSetAsPointBG(	const MovementRequesterMsg* msg,
								const Vec3 target);

S32		mrmTargetSetStartJumpBG(const MovementRequesterMsg* msg,
								const Vec3 target,
								S32 startJump);

S32		mrmTargetSetNoWorldCollBG(	const MovementRequesterMsg* msg,
									S32 noWorldColl);
								
S32		mrmTargetSetUseYBG(	const MovementRequesterMsg* msg,
							S32 useY);

S32		mrmTargetSetSpeedAsNormalBG(const MovementRequesterMsg* msg);

S32		mrmTargetSetSpeedAsOverrideBG(	const MovementRequesterMsg* msg,
										F32 maxSpeed);

S32		mrmTargetSetSpeedAsImpulseBG(const MovementRequesterMsg* msg);

S32		mrmTargetSetSpeedAsConstantBG(	const MovementRequesterMsg* msg,
										F32 speed);

S32		mrmTargetSetMinimumSpeedBG(	const MovementRequesterMsg* msg,
									F32 minSpeed);

S32		mrmTargetSetTurnRateAsNormalBG(const MovementRequesterMsg* msg);

S32		mrmTargetSetTurnRateAsOverrideBG(	const MovementRequesterMsg* msg,
											F32 turnRate);

S32		mrmTargetSetFrictionAsNormalBG(const MovementRequesterMsg* msg);

S32		mrmTargetSetFrictionAsOverrideBG(	const MovementRequesterMsg* msg,
											F32 friction);

S32		mrmTargetSetTractionAsNormalBG(const MovementRequesterMsg* msg);

S32		mrmTargetSetTractionAsOverrideBG(	const MovementRequesterMsg* msg,
											F32 traction);

// Rotation targeting.

S32		mrmRotationTargetSetAsStoppedBG(const MovementRequesterMsg* msg);

S32		mrmRotationTargetSetAsInputBG(const MovementRequesterMsg* msg);

S32		mrmRotationTargetSetAsRotationBG(	const MovementRequesterMsg* msg,
											const Quat rot);

S32		mrmRotationTargetSetAsPointBG(	const MovementRequesterMsg* msg,
										const Vec3 point);

S32		mrmRotationTargetSetAsDirectionBG(	const MovementRequesterMsg* msg,
											const Vec3 dir);

S32		mrmRotationTargetSetSpeedAsImpulseBG(const MovementRequesterMsg* msg);

S32		mrmRotationTargetSetTurnRateAsNormalBG(const MovementRequesterMsg* msg);

S32		mrmRotationTargetSetTurnRateAsOverrideBG(	const MovementRequesterMsg* msg,
													F32 turnRate);

// Moving and rotating.

S32		mrmTranslatePositionBG(	const MovementRequesterMsg* msg,
								const Vec3 vecOffset,
								S32 useController,
								S32 disableStickyGround);

S32		mrmSetStepIsNotInterpedBG(const MovementRequesterMsg* msg);

S32		mrmSetPositionBG(	const MovementRequesterMsg* msg,
							const Vec3 pos);

S32		mrmSetRotationBG(	const MovementRequesterMsg* msg,
							const Quat rot);
							
S32		mrmSetFacePitchYawBG(	const MovementRequesterMsg* msg,
								const Vec2 pyFace);

void	mrmMoveToValidPointBG(const MovementRequesterMsg *msg);

S32		mrmCheckCollisionWithOthersBG(	const MovementRequesterMsg* msg, 
										const Vec3 pos, 
										Vec3 vDirInOut,
										F32 addedCapsuleRadius, 
										EntityRef erLurchTarget);

S32		mrmMoveIfCollidingWithOthersBG(const MovementRequesterMsg* msg);

// Body.

S32		mrmNeedsSimBodyCreateBG(const MovementRequesterMsg* msg);

S32		mrmSimBodyCreateFromIndexBG(const MovementRequesterMsg* msg,
									U32* handleOut,
									U32 bodyIndex,
									U32 materialIndex,
									const Mat4 mat);

S32		mrmSimBodyCreateFromCapsuleBG(	const MovementRequesterMsg* msg,
										U32* handleOut,
										const Capsule* capsule,
										U32 materialIndex,
										F32 density,
										const Mat4 mat);

S32		mrmSimBodyCreateFromBoxBG(	const MovementRequesterMsg* msg,
									U32* handleOut,
									const Vec3 xyzSizeBox,
									const Mat4 matLocalBox,
									U32 materialIndex,
									F32 density,
									const Mat4 mat);

S32		mrmSimBodyDestroyBG(const MovementRequesterMsg* msg,
							U32* handleInOut);

S32		mrmSimBodyGetPSDKActorBG(	const MovementRequesterMsg* msg,
									U32 handle,
									PSDKActor** psdkActorOut);

// Override values.

S32		mrmOverrideValueCreateF32BG(const MovementRequesterMsg* msg,
									U32* movhOut,
									const char* name,
									F32 value);

S32		mrmOverrideValueCreateS32BG(const MovementRequesterMsg* msg,
									U32* movhOut,
									const char* name,
									S32 value);

S32		mrmOverrideValueDestroyBG(	const MovementRequesterMsg* msg,
									U32* movhInOut,
									const char* name);

S32		mrmOverrideValueDestroyAllBG(	const MovementRequesterMsg* msg,
										const char* name);

// Requester pipe.

S32		mrmNeedsPostStepMsgBG(const MovementRequesterMsg* msg);

S32		mrmPipeCreateBG(	const MovementRequesterMsg* msg,
							MovementRequesterPipeHandle* mrphOut,
							U32 erTarget,
							U32 mrClassID,
							U32 mrHandle);

S32		mrmPipeDestroyBG(	const MovementRequesterMsg* msg,
							MovementRequesterPipeHandle* mrphInOut);

S32		mrmPipeSendMsgStringBG(	const MovementRequesterMsg* msg,
								MovementRequesterPipeHandle mrph,
								const char* string);

// 

S32		mrmEnableMsgCollidedEntBG(	const MovementRequesterMsg* msg, 
									S32 enabled);

S32		mrmIgnoreCollisionWithEntsBG(	const MovementRequesterMsg* msg, 
										S32 enabled);

S32		mrmSetIsFlyingBG(	const MovementRequesterMsg* msg, 
							S32 bFlying);

// Forced simulation.

void	mrForcedSimEnableFG(MovementRequester* mr,
							bool enabled);

// Collision.

S32		mrmCheckGroundAheadBG(	const MovementRequesterMsg* msg,
								const Vec3 pos,
								const Vec3 dir);

S32		mrmGetWorldCollBG(	const MovementRequesterMsg *msg,
							WorldColl** wcOut);


S32		mrmDoNotRestartPrediction(const MovementRequesterMsg *msg);

//--- EntityMovementManagerResource.c --------------------------------------------------------------

S32		mmResourceCreateFG(	MovementManager* mm,
							U32* handleOut,
							U32 resourceID,
							const void* constant,
							const void* constantNP,
							const void* state);

void	mmResourceDestroyFG(MovementManager* mm,
							U32* handleInOut);
							
S32		mmResourceFindFG(	MovementManager* mm,
							U32* startIndexInOut,
							U32 resourceID,
							const void** constantOut,
							const void** constantNPOut,
							const void** activatedOut);
							
S32		mmResourcesCopyFromManager(	MovementManager* mm,
									const MovementManager* mmSource,
									U32 id);


S32		mmrmSetAlwaysDrawFG(const MovementManagedResourceMsg* msg,
							S32 enabled);

S32		mmrmSetHasAnimBitFG(const MovementManagedResourceMsg* msg);

S32		mmrmSetAnimBitFG(	const MovementManagedResourceMsg* msg,
							U32 bitHandle,
							S32 isKeyword);

S32		mmrmSetWaitingForTriggerFG(	const MovementManagedResourceMsg* msg,
									S32 enabled);

S32		mmrmSetNeedsSetStateFG(const MovementManagedResourceMsg* msg);

S32		mmrmSetWaitingForWakeFG(const MovementManagedResourceMsg* msg,
								U32 spc);

S32		mmrmSetNoPredictedDestroyFG(const MovementManagedResourceMsg* msg);

void	mmResourcesCheckForInvalidStateFG(MovementManager* mm);

void	mmAnimViewQueueResetFG(MovementManager* mm);

//--- EntityMovementManagerResourceBG.c ------------------------------------------------------------

S32		mrmResourceCreateBG(const MovementRequesterMsg* msg,
							U32* handleOut,
							U32 resourceID,
							const void* constant,
							const void* constantNP,
							const void* state);

S32		mrmResourceDestroyBG(	const MovementRequesterMsg* msg,
								U32 resourceID,
								U32* handleInOut);

S32		mrmResourceClearBG(	const MovementRequesterMsg* msg,
							U32 resourceID,
							U32* handleInOut);

S32		mrmResourceCreateStateBG(	const MovementRequesterMsg* msg,
									U32 resourceID,
									U32 mrHandle,
									const void* state);

S32		mrmResourceSetNoAutoDestroyBG(	const MovementRequesterMsg* msg,
										U32 resourceID,
										U32 mrHandle);

//--- EntityMovementManagerNetPrepare.c ------------------------------------------------------------

void	mmCreateNetOutputsFG(MovementManager* mm);

