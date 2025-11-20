#pragma once
GCC_SYSTEM

#include "GraphicsLib.h"
#include "inputMouse.h"
#include "WorldLibEnums.h"

typedef struct NetLink NetLink;
typedef struct Packet Packet;

void editLibStartup(int editCmd);
void editLibSetLink(NetLink **link);
void editLibOncePerFrame(F32 fFrameTime);
void editLibDrawGhosts(void);

void editLibCursorSegment(Mat4 cammat, F32 x,F32 y,F32 len,Vec3 start,Vec3 end);
void editLibCursorRayEx(Mat4 cammat, F32 x,F32 y,Vec3 start,Vec3 dir);
void editLibCursorRay(Vec3 rayStart, Vec3 rayEnd);
void editLibCursorRayClick(Vec3 rayStart, Vec3 rayEnd, MouseButton button);
void editLibGetScreenPos(Vec3 worldVec, Vec2 screenVec);
void editLibGetScreenPosOthro(Vec3 worldVec, Vec2 screenVec);
void editLibFindScreenCoords(const Vec3 minbounds, const Vec3 maxbounds, const Mat4 worldMat, Mat44 scrProjMat, Vec3 bottomLeft, Vec3 topRight);

void editLibBudgetsReset(void);

void editLibSetEncounterBudgets(U32 numSpawnedEnts, U32 numActiveEncs, U32 numTotalEncs, U32 spawnedFSMCost, U32 potentialFSMCost);

// ASYNCHRONOUS EVENT HANDLING
typedef bool (*EditAsyncOpFunction)(ServerResponseStatus status, void *userdata, int step, int total_steps);

#ifndef NO_EDITORS
void editLibCreateAsyncOp(U32 req_id, EditAsyncOpFunction callback, void *userdata);
void editLibHandleServerReply(Packet* pak);
#endif
