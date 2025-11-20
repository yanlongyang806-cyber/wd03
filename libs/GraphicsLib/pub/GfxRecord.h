#pragma once
GCC_SYSTEM

#include "textparser.h"

typedef struct CutsceneDef CutsceneDef;

AUTO_STRUCT;
typedef struct CameraMat
{
	F32 time;
	Mat4 mat;
} CameraMat;

AUTO_STRUCT;
typedef struct CameraMatRelative
{
    F32 time;
	bool isAbsolute;

    Vec3 pyr;

	// only valid if !isAbsolute
    F32 dist;

	// only valid is isAbsolute
	Vec3 pos;
} CameraMatRelative;

void gfxRecordCameraPos(SA_PARAM_NN_VALID CameraMat*** record, F32 time);
void gfxLoadRecording(SA_PARAM_NN_VALID CameraMat*** record);

void gfxRecordCameraPosRelative(SA_PARAM_NN_VALID CameraMatRelative*** record, F32 time);
void gfxLoadRecordingRelative(SA_PARAM_NN_VALID CameraMatRelative*** record);
void gfxLoadRecordingCutscene(SA_PARAM_NN_VALID CutsceneDef** record);
void gfxSetRecordingTargetPos( Vec3 targetPos );

void gfxUpdateTargetPos( const Vec3 targetPos );
void gfxReplayRecording(void);


// Set the camera according to the list of frames, linearly interpolating between frames
void gfxInterpolateCameraFrames(F32 timestep);
void gfxRecordSetTimeElapsed(F32 total_time_elapsed);
bool gfxRecordDidPositionWarp(void); // Returns true once if we recently "warped" by a great distance

typedef void (*gfxRecordPlayCutsceneFunc)(CutsceneDef* record, F32 time, F32 timestep);
void gfxSetRecordPlayCutsceneCB(gfxRecordPlayCutsceneFunc func);
