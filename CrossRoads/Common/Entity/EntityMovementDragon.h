#pragma once

typedef struct DragonMovementDef DragonMovementDef;
typedef struct MovementRequesterMsg MovementRequesterMsg;
typedef struct MovementRequester MovementRequester;
typedef U32 EntityRef;

void	mrDragonMsgHandler(const MovementRequesterMsg* msg);

S32		mrDragon_InitializeSettings(MovementRequester* mr, 
									DragonMovementDef *pSettings);

S32		mrDragon_SetMaxSpeed(	MovementRequester* mr,
								F32 speed);

S32		mrDragon_SetTraction(	MovementRequester* mr,
								F32 traction);
	
S32		mrDragon_SetFriction(	MovementRequester* mr,
								F32 friction);

S32		mrDragon_SetTargetFaceEntity(	MovementRequester* mr,
										EntityRef erEnt);

S32		mrDragon_SetOverrideTargetAndPowersAngleOffset(	MovementRequester* mr,
														EntityRef erEnt,
														F32 fAngleOffset);

S32		mrDragon_SetOverrideTargetPosAndPowersAngleOffset(	MovementRequester* mr,
															const Vec3 vTargetPos,
															F32 fAngleOffset);

S32		mrDragon_SetOverrideRotateHeadToBodyOrientation( MovementRequester *mr);

S32		mrDragon_SetOverrideRotateBodyToHeadOrientation( MovementRequester *mr);
