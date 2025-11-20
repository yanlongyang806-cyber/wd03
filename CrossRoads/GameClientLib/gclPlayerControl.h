#pragma once

typedef struct Entity Entity;
typedef struct MovementManager MovementManager;


void gclPlayerControl_Reset(void);

void gclPlayerControl_SetMoveAndFaceYawByEntTracked(Entity *pEnt,
													F32 yaw,
													const char* fileName,
													U32 fileLine);

#define gclPlayerControl_SetMoveAndFaceYawByEnt(e, yaw)\
		gclPlayerControl_SetMoveAndFaceYawByEntTracked(e, yaw, __FILE__, __LINE__)

void gclPlayerControl_HandleControl_TurnRight(int bTurnleft, int bTurnRight);



int gclPlayerControl_IsInSTOSpaceshipMovement(void);
bool gclPlayerControl_AllowFollowTargetByRef( U32 uiEntRef );

void gclPlayerControl_Update(Entity *e, MovementManager* mm, F32 fDTime, U32 milliseconds);

bool gclPlayerControl_ShouldAutoUnholster(bool bControlSchemeChanged);

// returns true if either mouse button is down rotating the camera
int gclPlayerControl_IsMouseRotating(void);
// returns true if the use is mouse looking ('right mouse' look)
bool gclPlayerControl_IsMouseLooking(void);
// If this is set to true, then make sure that MouseLook_Forced is always on
void gclPlayerControl_SetAlwaysUseMouseLookForced(bool bEnable);
// Update mouse input parameters
void gclPlayerControl_UpdateMouseInput(int bFreeLookDown, int bMouseLook_Forced);

bool gclPlayerControl_IsFacingTarget(void);
F32 gclPlayerControl_GetMoveYaw(void);
F32 gclPlayerControl_GetFaceYaw(void);
int gclPlayerControl_IsHoldingAim();

bool gclPlayerControl_GetCameraSnapToFacing(Vec2 pyFace);
void gclResetMouseLookFlags(void);

void gclPlayerControl_DisableTurnToFaceThisFrame();
void gclPlayerControl_DisableFollowThisFrame();

void gclPlayerControl_SetFreeCamera(int bFreeCamera);
bool gclPlayerControl_IsMouseLookToggledOn();

bool gclPlayerControl_IsAlwaysUsingMouseLookForced();
void exprCameraSuppressForcedMouseLockThisFrame(bool bSuppress);

bool gclPlayerControl_IsHardLockPressed();

void gclPlayerControl_SetForward(bool bForward);
void gclPlayerControl_SetBack(bool bEnabled);
void gclPlayerControl_SetLeft(bool bEnabled);
void gclPlayerControl_SetRight(bool bEnabled);

void gclPlayerControl_SetAutoForward(int bAutoForwardEnabled);

void gclPlayerControl_SuspendMouseLook();
void gclPlayerControl_StopMoving( bool stopAutorunToo );
