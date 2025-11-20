#include "GfxRecord.h"
#include "GfxRecord_h_ast.c"
#include "Quat.h"
#include "GraphicsLibPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

CameraMat*** s_playback = NULL;
CameraMatRelative*** s_playbackRelative = NULL;
CutsceneDef** s_playbackCutscene = NULL;
static bool replaying = false;
static Vec3 s_targetPos;
gfxRecordPlayCutsceneFunc g_RecordPlayCutsceneFunc = NULL;

void gfxSetRecordPlayCutsceneCB(gfxRecordPlayCutsceneFunc func)
{
	g_RecordPlayCutsceneFunc = func;
}

void gfxRecordCameraPos(CameraMat*** record, F32 time)
{
	CameraMat *camMat = StructAlloc(parse_CameraMat);

	gfxGetActiveCameraMatrix(camMat->mat);
	camMat->time = time;

	eaPush(record, camMat);
}

static void gfxDestroyRecordings()
{
	if(s_playback)
	{
		eaDestroy(s_playback);
		s_playback = NULL;
	}
	if(s_playbackRelative)
	{
		eaDestroy(s_playbackRelative);
		s_playbackRelative = NULL;
	}
	if(s_playbackCutscene)
		s_playbackCutscene = NULL;
}

// Load for reading or writing
void gfxLoadRecording(CameraMat*** record)
{
    if( s_playback == record ) {
        return;
    }
	gfxDestroyRecordings();
	s_playback = record;
}


void gfxRecordCameraPosRelative(SA_PARAM_NN_VALID CameraMatRelative*** record, F32 time)
{
    CameraMatRelative* camMat = StructAlloc(parse_CameraMatRelative);
    GfxCameraController* camController = gfxGetActiveCameraController();

    copyVec3( camController->campyr, camMat->pyr );
    camMat->dist = camController->camdist;
    camMat->time = time;

    eaPush(record, camMat);
}

void gfxLoadRecordingRelative(SA_PARAM_NN_VALID CameraMatRelative*** record)
{
    if( s_playbackRelative == record ) {
        return;
    }
	gfxDestroyRecordings();
	s_playbackRelative = record;
}

void gfxLoadRecordingCutscene(SA_PARAM_NN_VALID CutsceneDef** record)
{
	if( s_playbackCutscene == record ) {
		return;
	}
	gfxDestroyRecordings();
	s_playbackCutscene = record;
}

void gfxSetRecordingTargetPos( Vec3 targetPos )
{
	globMovementLog("[gfx] Setting recording target pos (%f, %f, %f) was (%f, %f, %f).",
					vecParamsXYZ(targetPos),
					vecParamsXYZ(s_targetPos));
					
    copyVec3( targetPos, s_targetPos );
}

/// Should be called once-per-frame to update the camera's target
/// position
void gfxUpdateTargetPos( const Vec3 targetPos )
{
	globMovementLog("[gfx] Setting target pos (%f, %f, %f) was (%f, %f, %f).",
					vecParamsXYZ(targetPos),
					vecParamsXYZ(s_targetPos));

    copyVec3( targetPos, s_targetPos );
}

// While loading a map, the camera shouldn't move until the map is fully loaded.
void gfxReplayRecording(void)
{
	replaying = true;
}

U32 gfxFindRecordedFrame(U32 frame, F32 time)
{
	U32 next_frame, max_frame;

	if(NULL == s_playback && NULL == s_playbackRelative)
		return 0;

	// Return 0 (the first frame) if we're still loading and haven't started to "play" yet
	if(!replaying)
		return 0;

    if( s_playback != NULL )
    {
        max_frame = eaSize(s_playback);
        next_frame = frame + 1;

        // Find the first frame whose timestamp is later than the current time, and return the frame before it
        while(next_frame < max_frame && time >= (*s_playback)[next_frame]->time)
        {
            frame++;
            next_frame++;
        }
    }
    else
    {
        max_frame = eaSize(s_playbackRelative);
        next_frame = frame + 1;

        // Find the first frame whose timestamp is later than the current time, and return the frame before it
        while(next_frame < max_frame && time >= (*s_playbackRelative)[next_frame]->time)
        {
            frame++;
            next_frame++;
        }
    }

	return frame;
}

static F32 s_timeElapsed = 0;

void gfxRecordSetTimeElapsed(F32 total_time_elapsed)
{
	// If the camera has started moving, advance time
	s_timeElapsed = total_time_elapsed;
}

#define WARP_DISTANCE 100
static bool g_didWarp;

void gfxInterpolateCameraFrames(F32 timestep)
{
	globMovementLog("[gfx] Interping camera frames.");
	
	if( s_playbackCutscene )
	{
		if(g_RecordPlayCutsceneFunc)
			g_RecordPlayCutsceneFunc( *s_playbackCutscene, s_timeElapsed, timestep );
	}
    else if( s_playback )
    {
        U32 frameIdx;
        CameraMat* frame, *nextFrame;
        Quat quatOne, quatTwo, interpQuat;
        Vec3 vecOne, vecTwo;
        Mat4 result;
        F32 frameLen;
        F32 weight;
        U32 max_frame = eaSize(s_playback);

        if(NULL == s_playback || NULL == *s_playback)
            return;

        // Find the right frames
        frameIdx = gfxFindRecordedFrame(0, s_timeElapsed);

        frame = (*s_playback)[frameIdx];

        if(!frame || frameIdx >= max_frame)
        {
            return;
        }

        nextFrame = (*s_playback)[(frameIdx == (max_frame - 1))?(max_frame - 1):(frameIdx + 1)];

        // Figure out what weight to give to each frame
        frameLen = nextFrame->time - frame->time;
        if (frameLen == 0) {
            weight = 1.0;
        } else {
            F32 timeInFrame = s_timeElapsed - frame->time;
            weight = 1.0 - timeInFrame / frameLen;
        }

        mat3ToQuat(frame->mat, quatOne);
        mat3ToQuat(nextFrame->mat, quatTwo);
        quatInterp(1 - weight, quatOne, quatTwo, interpQuat);
        quatToMat(interpQuat, result);

		{ // Check for warp
			Vec3 distance;
			subVec3(frame->mat[3], nextFrame->mat[3], distance);
			if (lengthVec3Squared(distance) > WARP_DISTANCE*WARP_DISTANCE) {
				g_didWarp = true;
			}
		}

        scaleVec3(frame->mat[3], weight, vecOne);
        scaleVec3(nextFrame->mat[3], (1.0-weight), vecTwo);
        addVec3(vecOne, vecTwo, result[3]);

        gfxSetActiveCameraMatrix(result,false);
    }
    else
    {
        U32 frameIdx;
        CameraMatRelative* frame, *nextFrame;
        F32 frameLen;
        F32 weight;
        U32 max_frame = eaSize(s_playbackRelative);

        if(NULL == s_playbackRelative || NULL == *s_playbackRelative)
            return;

        // Find the right frames
        frameIdx = gfxFindRecordedFrame(0, s_timeElapsed);

        frame = (*s_playbackRelative)[frameIdx];

        if(!frame || frameIdx >= max_frame)
        {
            return;
        }

        nextFrame = (*s_playbackRelative)[(frameIdx == (max_frame - 1))?(max_frame - 1):(frameIdx + 1)];

        // Figure out what weight to give to each frame
        frameLen = nextFrame->time - frame->time;
		if (nextFrame->isAbsolute != frame->isAbsolute) {
			weight = 0;
		}
        else if (frameLen == 0) {
            weight = 1.0;
        } else {
            F32 timeInFrame = s_timeElapsed - frame->time;
            weight = 1.0 - timeInFrame / frameLen;
        }

		if( !nextFrame->isAbsolute )
		{
			Vec3 interpedPYR;
			F32 interpedDist;

			interpPYR( 1 - weight, frame->pyr, nextFrame->pyr, interpedPYR );
			interpedDist = weight * frame->dist + (1 - weight) * nextFrame->dist;
			
			{
				Mat3 tempMat;
				Vec3 cameraPos;
				createMat3YPR(tempMat, interpedPYR);
				
				globMovementLog("[gfx] Using target camera pos (%f, %f, %f)",
								vecParamsXYZ(s_targetPos));

				scaleVec3( tempMat[2], interpedDist, tempMat[2]);
				copyVec3( s_targetPos, cameraPos );
				addVec3( cameraPos, tempMat[2], cameraPos );
				
				if(globMovementLogIsEnabled){
					Vec3 posDown;

					globMovementLogSegment(	"gfx.camToTarget",
											cameraPos,
											s_targetPos,
											0xffff00ff);
	            
					copyVec3(s_targetPos, posDown);
					posDown[1] -= 6;
						            
					globMovementLogSegment(	"gfx.targetToDown",
											s_targetPos,
											posDown,
											0x80ff00ff);
				}
				
				gfxSetActiveCameraYPRPos( interpedPYR, cameraPos, false );
			}
		}
		else
		{
			Vec3 interpedPYR;
			Vec3 interpedPos;

			interpPYR( 1 - weight, frame->pyr, nextFrame->pyr, interpedPYR );
			interpVec3( 1 - weight, frame->pos, nextFrame->pos, interpedPos );

			gfxSetActiveCameraYPRPos( interpedPYR, interpedPos, false );
		}
		
		if( gfx_state.record_cam_pp_fn ) {
			Mat4 camera_matrix;
			gfxGetNextActiveCameraMatrix( camera_matrix );
			gfx_state.record_cam_pp_fn( camera_matrix, nextFrame->isAbsolute );
			gfxSetActiveCameraMatrix( camera_matrix, false );
		}
    }
}

bool gfxRecordDidPositionWarp(void) // Returns true once if we recently "warped" by a great distance
{
	if (g_didWarp) {
		g_didWarp = false;
		return true;
	}
	return false;
}

