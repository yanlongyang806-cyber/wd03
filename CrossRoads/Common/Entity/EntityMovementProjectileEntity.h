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

typedef struct MovementRequesterMsg MovementRequesterMsg;
typedef struct MovementRequester MovementRequester;
typedef struct ProjectileEntityMMSettings ProjectileEntityMMSettings;
typedef struct ProjectileEntityMMSweptSphere ProjectileEntityMMSweptSphere;

void mrProjectileEntMsgHandler(const MovementRequesterMsg* msg);

S32 mrProjectileEnt_InitializeSettings(	MovementRequester* mr, 
										EntityRef erCreator, 
										ProjectileEntityMMSettings *pSettings);

S32 mrProjectileEnt_InitializeAsSweptSphere(MovementRequester* mr, 
											EntityRef erCreator, 
											ProjectileEntityMMSettings *pSettings,
											const Vec3 vOffset);

S32 mrProjectileEnt_SetDirection(	MovementRequester* mr, 
									const Vec3 vDirection);

S32 mrProjectileEnt_MatchTrajectory(MovementRequester* mr, 
									const Vec3 vBasePos, 
									const Vec3 vDirection);

S32 mrProjectileEnt_GotoEntity(MovementRequester* mr, EntityRef er);

S32 mrProjectileEnt_ResetExpirations(MovementRequester* mr);

S32 mrProjectileEnt_OverrideRange(MovementRequester* mr, F32 fRange);

S32 mrProjectileEnt_ResetHitEntities(MovementRequester* mr);

S32 mrProjectileEnt_ClearHitEntity(MovementRequester* mr, EntityRef er);

S32 mrProjectileEnt_BeginHugGround(MovementRequester* mr);

S32 mrProjectileEnt_AttachToEnt(MovementRequester *mr, EntityRef er, const Vec3 vOffset);

// delays the collision checks with entities, only meant to be called on initialization of the projectile
S32 mrProjectileEnt_InitialEntityHitDelay(MovementRequester *mr, F32 fSeconds);