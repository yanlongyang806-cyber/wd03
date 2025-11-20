#pragma once
GCC_SYSTEM

#include "UtilitiesLibEnums.h"

typedef struct Frustum Frustum;
typedef struct ModelLOD ModelLOD;
typedef struct BasicTexture BasicTexture;
typedef struct GfxOcclusionBuffer GfxOcclusionBuffer;
typedef struct FrameCounts FrameCounts;

GfxOcclusionBuffer *zoCreate(SA_PARAM_NN_VALID Frustum *frustum, bool is_primary);
void zoDestroy(SA_PRE_OP_VALID SA_POST_P_FREE GfxOcclusionBuffer *zo);

void zoWaitUntilOccludersDrawn(SA_PARAM_OP_VALID GfxOcclusionBuffer *zo);
void zoWaitUntilDone(SA_PARAM_OP_VALID GfxOcclusionBuffer *zo);

int zoGetThreadCount(SA_PARAM_OP_VALID GfxOcclusionBuffer *zo);
void zoInitFrame(SA_PARAM_OP_VALID GfxOcclusionBuffer *zo, SA_PARAM_NN_VALID const Mat44 projection_mat, SA_PARAM_NN_VALID const Frustum *frustum);
void zoAccumulateStats(GfxOcclusionBuffer *zo, FrameCounts *frame_counts);
void zoCreateZHierarchy(GfxOcclusionBuffer * zo);
void zoSetOccluded(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, SA_PARAM_NN_VALID U32 *last_update_time, SA_PARAM_NN_VALID U8 *occluded_bits);
int zoTestBounds(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, SA_PRE_NN_RELEMS(8) const Vec4 eyespaceBounds[8], int isZClipped, SA_PARAM_OP_VALID U32 *last_update_time, SA_PARAM_OP_VALID U8 *occluded_bits, SA_PARAM_OP_VALID U8 *inherited_bits, SA_PARAM_OP_VALID F32 *screen_space, SA_PARAM_OP_VALID bool *occlusionReady);
int zoTestSphere(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, SA_PRE_NN_RELEMS(3) const Vec3 centerEyeCoords, float radius, int isZClipped, bool *occlusionReady); // if you specify occlusionReady then the test will only happen if the thread is done
int zoCheckAddOccluder(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, SA_PARAM_NN_VALID ModelLOD *m, SA_PARAM_NN_VALID const Mat4 matModelToWorld, U32 ok_materials, bool double_sided); // make sure zoTest is called before zoCheckAddOccluder
int zoCheckAddOccluderVolume(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, SA_PRE_NN_RELEMS(3) const Vec3 minBounds, SA_PRE_NN_RELEMS(3) const Vec3 maxBounds, VolumeFaces faceBits, SA_PARAM_NN_VALID const Mat4 matModelToWorld);
int zoCheckAddOccluderCluster(GfxOcclusionBuffer *zo, ModelLOD *m, const Mat4 matModelToWorld, U32 ok_materials, bool double_sided);

void zoNotifyModelFreed(SA_PARAM_NN_VALID ModelLOD *m);
void zoClearOccluders(void);

// debug
BasicTexture *zoGetZBuffer(SA_PARAM_OP_VALID GfxOcclusionBuffer *zo);
void zoShowDebug(SA_PARAM_OP_VALID GfxOcclusionBuffer *zo, int debug_type);

int zoTestBoundsSimple(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, SA_PRE_NN_RELEMS(8) const Vec4 eyespaceBounds[8], int isZClipped);
int zoTestBoundsSimpleWorld(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo, SA_PRE_NN_RELEMS(3) const Vec3 minBounds, SA_PRE_NN_RELEMS(3) const Vec3 maxBounds, SA_PARAM_NN_VALID const Mat4 matLocalToEye, int isZClipped, bool *occlusionReady);

int gfxGetScreenSpace(SA_PARAM_NN_VALID const Frustum *frustum, const Mat44 projection_mat, int near_clipped, SA_PRE_NN_RELEMS(8) const Vec4 transBounds[8], SA_PARAM_OP_VALID F32 *screen_space);

void zoLimitsScreenRect(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo,F32 fViewSpaceZ,Vec2 vMin,Vec2 vMax);

bool zoIsReadyForRead(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo);

void zoFinishRejectedOccluders(SA_PARAM_NN_VALID GfxOcclusionBuffer *zo);