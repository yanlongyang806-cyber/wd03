#pragma once

typedef struct MovementRequesterMsg MovementRequesterMsg;
typedef struct MovementRequester MovementRequester;
typedef struct AICivilianPath AICivilianPath;

void mmAICivilianMovementMsgHandler(const MovementRequesterMsg* msg);

S32 mmAICivilianGetAndClearReachedWp(MovementRequester *mr);
U32 mmAICivilianGetAckReleasedWaypointsID(MovementRequester *mr);
void mmAICivilianSendAdditionalWaypoints(MovementRequester *mr, AICivilianWaypoint**eaAdditionalWaypoints);
U32 mmAICivilianClearWaypoints(MovementRequester *mr);

void mmAICivilianMovementSetMovementOptions(MovementRequester *mr, F32 speedMean, F32 speedVar);
void mmAICivilianMovementSetDesiredSpeed(MovementRequester *mr, F32 fSpeedMinimum, F32 fSpeedRange);
void mmAICivilianMovementSetPause(MovementRequester *mr, bool on);
void mmAICivilianWaypointClearStop(MovementRequester *mr);

void mmAICivilianInitMovement(MovementRequester *mr, EAICivilianType type);
void mmAICivilianMovementSetCollision(MovementRequester *mr, bool bEnable);
void mmAICivilianMovementSetFinalFaceRot(MovementRequester *mr, F32 fFacing);

void mmAICivilianMovementSetCritterMoveSpeed(MovementRequester *mr, F32 fSpeed);
void mmAICivilianMovementUseCritterOverrideSpeed(MovementRequester *mr, bool bEnable);
void mmAICivilianMovementEnable(MovementRequester *mr, bool bEnable);