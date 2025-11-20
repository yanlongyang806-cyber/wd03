#pragma once

AST_PREFIX(WIKI(AUTO));

#include "referencesystem.h"
	
typedef struct Entity Entity;
typedef struct ExprContext ExprContext;
typedef struct Expression Expression;
typedef struct Capsule Capsule;
typedef struct MovementRequester MovementRequester;
typedef struct MovementNoCollHandle MovementNoCollHandle;
typedef struct PlayerCostume PlayerCostume;
typedef struct PowerDef PowerDef;
typedef U32 EntityRef;

AUTO_ENUM;
typedef enum EProjectileType
{
	EProjectileType_DEFAULT,	ENAMES(Default)
	EProjectileType_BEAM,		ENAMES(Beam)
} EProjectileType;

// settings for the movement requester
AUTO_STRUCT;
typedef struct ProjectileEntityMMSettings
{
	// the initial speed the projectile will start at, if negative will start at TargetSpeed
	F32 fSpeed;						AST(DEFAULT(-1.f))

	// the feet per second acceleration to get to target speed 
	F32 fAcceleration;				AST(DEFAULT(0.f))

	// the target speed of the projectile
	F32 fTargetSpeed;				AST(DEFAULT(0.f))

	// the range of the projectile before it destroys itself
	F32 fRange;						AST(DEFAULT(300.f))

	// the time before the projectile will kill itself
	F32 fLifetime;					AST(DEFAULT(0.f))

	// the gravity applied. Gravity is applied downwards. A negative gravity will cause it to go upwards.
	F32 fGravity;					AST(DEFAULT(0.f))

	// the collision radius that is used to detect collision with other entities
	F32 fEntityCollisionRadius;		AST(DEFAULT(6.f))

	// the radius used for collision against the world
	F32 fWorldCollisionRadius;		AST(DEFAULT(3.f))

	// the maximum number of entities this projectile can hit. 0 or less will make it hit every entity
	S32 iMaxHitEntities;			AST(DEFAULT(0))

	// (degrees) the angle threshold at which the projectile will report an OnCollideWorld
	F32 fSurfaceObstructAngle;		AST(DEFAULT(0))

	// the height in feet the projectile will attempt to step over small curbs in the geometry
	F32 fStepHeight;				AST(DEFAULT(0))

	// if true, will collide vs friendlies
	U32	bHitsFriendlies : 1;

	// defaults to true- if true, will collide vs enemies
	U32 bHitsEnemies : 1;			AST(DEFAULT(1))

	// uses a linear velocity movement model, which better preserves velocity when hitting surfaces
	// and instantly turns towards targets
	U32 bLinearVelocity : 1;

	// If set, the projectile will attempt to snap downwards to make contact with the ground 
	U32 bHugGround : 1;

	// walls are classified as 45 degrees of a vertical wall. 
	// walls always cause obstructions (onCollideWorld)
	U32 bWallsObstruct : 1;			

	// if set the projectile will detect hits on the owner
	U32 bHitsOwner : 1;

	// if set, will not detect hits while not moving
	U32 bIgnoreHitWhileStationary : 1;

} ProjectileEntityMMSettings;


AUTO_ENUM;
typedef enum EProjectileAttachOnCreate
{
	// standard projectile behavior
	EProjectileAttachOnCreate_None,		ENAMES(Default)

	// attaches to the owner using the attrib offsets
	EProjectileAttachOnCreate_Owner,	ENAMES(Owner)

	// attaches to the target using the attrib offsets
	EProjectileAttachOnCreate_Target,	ENAMES(Target)
} EProjectileAttachOnCreate;


AUTO_STRUCT WIKI("ProjectileEntityDef");
typedef struct ProjectileEntityDef
{
	char*	pchName;								AST(STRUCTPARAM KEY POOL_STRING)		

	char*	pchFilename;							AST(CURRENTFILE)

	// the costume of the projectile
	REF_TO(PlayerCostume) hCostume;					AST(REFDICT(PlayerCostume) NAME(Costume) )

	ProjectileEntityMMSettings	mmSettings;			AST(EMBEDDED_FLAT) 

	// todo(rp): targets some of these powers might need to be defined somehow. 

	// the power that will fire when it hits another entity
	// targets hit entity
	REF_TO(PowerDef) hPowerOnHitEnt;				AST(NAME(PowerOnHitEnt), REFDICT(PowerDef))

	// the power that will fire when it hits a wall
	// targets nothing
	REF_TO(PowerDef) hPowerOnCollideWorld;			AST(NAME(PowerOnCollideWorld), REFDICT(PowerDef))

	// the power that will fire when it expires due to range or lifetime
	// targets nothing
	REF_TO(PowerDef) hPowerOnExpire;				AST(NAME(PowerOnExpire), REFDICT(PowerDef))

	// the power that will fire when it expires due to range or lifetime
	// targets hit entity
	REF_TO(PowerDef) hPowerOnHitGotoEnt;			AST(NAME(PowerOnHitGotoEnt), REFDICT(PowerDef))

	// the power that will fire when it expires due to range or lifetime
	// targets creator
	REF_TO(PowerDef) hPowerOnDeath;					AST(NAME(PowerOnDeath), REFDICT(PowerDef))

	// expression that is called when the projectile would be queued for death
	Expression			**ppExprOnQueuedDeath;		AST(NAME("OnQueuedDeath"), REDUNDANT_STRUCT("ExprOnQueuedDeath", parse_Expression_StructParam), LATEBIND)

	// expression is called when the projectile expires due to range, lifetime, 
	Expression			**ppExprOnExpire;			AST(NAME("OnExpire"), REDUNDANT_STRUCT("ExprOnExpire", parse_Expression_StructParam), LATEBIND)

	// called when the projectile hits a wall that obstructs it
	Expression			**ppExprOnCollideWorld;		AST(NAME("OnCollideWorld"), REDUNDANT_STRUCT("ExprOnCollideWorld", parse_Expression_StructParam), LATEBIND)

	// Called for each entity that is hit
	Expression			**ppExprOnHitEntity;		AST(NAME("OnHitEntity"), REDUNDANT_STRUCT("ExprOnHitEntity", parse_Expression_StructParam), LATEBIND)

	// called on when the projectile hits the entity it was going towards
	Expression			**ppExprOnHitGotoEnt;		AST(NAME("OnHitGotoEnt"), REDUNDANT_STRUCT("ExprOnHitGotoEnt", parse_Expression_StructParam), LATEBIND)

	// the period at which the projectile will reset its hit list and re-hit entities
	F32					fResetHitEntsTimer;

	// when fResetHitEntsTimer pops and removes an entity, also decrement the hit counter
	// this is default behavior
	S8					bDecrementNumEntitiesHitCounterOnReset; AST(DEFAULT(1))

	// an initial delay when the projectile gets created before it will start hitting entities
	F32					fInitialEntityHitDelay;

	// a new type of projectile that is a (currently)stationary beam
	EProjectileType		eProjectileType;			AST(SUBTABLE(EProjectileTypeEnum))

	// on creation, it attaches to
	EProjectileAttachOnCreate	eAttachOnCreate;

} ProjectileEntityDef;



AUTO_STRUCT;
typedef struct ProjectileEntity
{
	REF_TO(ProjectileEntityDef)		hProjectileDef;
	
	EntityRef	erGotoEnt; 
	S32			iNumEntitiesHit;

	U64			*eaEntitiesHitAndTime;					NO_AST
	U32			uiLastProcessHitTime;
		
	// time to hang around until the entity is destroyed
	// used to persist the FX until they die
	F32			fTimeToLinger;

	U32			muQueueDieTime;
	F32			mfResetHitEntsTime;
		
	S32			miOnExpireIndex;
	S32			miOnQueueDeathIndex;
	S32			miOnCollideWorldIndex;
	S32			miOnHitEntityIndex;
	S32			miOnHitGotoEnt;

	MovementRequester *pRequester;						NO_AST
	MovementNoCollHandle *pNoCollHandle;				NO_AST
	
	U32			bQueuedDie : 1;
} ProjectileEntity;

extern DictionaryHandle g_hProjectileDefDict;

S32 RayVsSphere(const Vec3 vRaySt, 
				const Vec3 vRayDir, 
				const Vec3 vSphereCenter, 
				F32 fSphereRadius, 
				F32 *pfHitTime,
				S32 *bInside);

S32 SphereSweepVsCapsule(	const Capsule *pcap, 
							const Vec3 vCapPos, 
							const Quat qCapRot,	
							const Vec3 vSphereSt,
							const Vec3 vSphereEnd,
							F32 fSphereRadius,
							F32 *pfHitTime);