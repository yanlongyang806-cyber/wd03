/***************************************************************************
*     Copyright (c) 2005-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// NOTE: Copy this file and do the following:
//       1. Find-and-Replace the word "Door" (case sensitive) with your requester name.
//       2. Find-and-Replace the word "" with blank "".
//       3. Change the #if 0 to #if 1.

#include "EntityMovementDoor.h"
#include "EntityMovementManager.h"
#include "error.h"
#include "file.h"
#include "Quat.h"
#include "ResourceManager.h"
#include "wlGroupPropertyStructs.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrDoorMsgHandler,
											"DoorMovement",
											Door);

AUTO_STRUCT;
typedef struct DoorFG {
	U32 start			: 1;
	U32 finished		: 1;
	U32 usepos			: 1;
	U32 usefacing		: 1;
	U32 useroll			: 1;

	U32 delay;
	Vec3 position;
	Vec2 pyFace;

	U32 cbFG			: 1;	NO_AST
	DoorCompleteCallback cb;	NO_AST
	void *userdata;				NO_AST
} DoorFG;

AUTO_STRUCT;
typedef struct DoorBG {
	U32 start			: 1;
	U32 finished		: 1;
	U32 usepos			: 1;
	U32 usefacing		: 1;
	U32 useroll			: 1;

	U32 delay;
	Vec3 position;
	Vec2 pyStart;
	Vec2 pyEnd;

	U32 cbFG			: 1;	NO_AST
	DoorCompleteCallback cb;	NO_AST
	void *userdata;				NO_AST

	U32 timer;
} DoorBG;

AUTO_STRUCT;
typedef struct DoorToFG {
	U32 finished		: 1;
} DoorToFG;

AUTO_STRUCT;
typedef struct DoorToBG {
	U32 start			: 1;
	U32 usepos			: 1;
	U32 usefacing		: 1;
	U32 useroll			: 1;

	U32 delay;
	Vec3 position;
	Vec2 pyFace;

	U32 cbFG			: 1;	NO_AST
	DoorCompleteCallback cb;	NO_AST
	void *userdata;				NO_AST
} DoorToBG;

AUTO_STRUCT;
typedef struct DoorSync {
	S32 unused;
} DoorSync;

AUTO_STRUCT;
typedef struct DoorLocalBG{
	S32 unused;
} DoorLocalBG;

#define MM_DOOR_DEFAULT_DELAY 10

void mrDoorMsgHandler(const MovementRequesterMsg* msg){
	DoorFG*			fg;
	DoorBG*			bg;
	DoorLocalBG*	localBG;
	DoorToFG*		toFG;
	DoorToBG*		toBG;
	DoorSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, Door);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_UPDATED_TOFG:{
			fg->finished = toFG->finished;

			// Call the callback
			if(fg->cbFG && fg->cb)
			{
				fg->cb(fg->userdata);
			}

			// Destroy myself
			mrmDestroySelf(msg);
		}

		xcase MR_MSG_FG_CREATE_TOBG:{
			mrmEnableMsgUpdatedToBG(msg);

			toBG->start = fg->start;
			toBG->cb = fg->cb;
			toBG->userdata = fg->userdata;
			toBG->cbFG = fg->cbFG;
			toBG->usepos = fg->usepos;
			toBG->usefacing = fg->usefacing;
			toBG->useroll = fg->useroll;
			toBG->delay = fg->delay;
			copyVec3(fg->position, toBG->position);
			copyVec2(fg->pyFace, toBG->pyFace);
		}

		xcase MR_MSG_BG_INITIALIZE:{
			const U32 handledMsgs =	MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP;
									
			mrmHandledMsgsAddBG(msg, handledMsgs);
		}

		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"");
		}

		xcase MR_MSG_BG_UPDATED_TOBG:{
			bg->start = toBG->start;
			bg->timer = 0;

			bg->cbFG = toBG->cbFG;
			bg->cb = toBG->cb;
			bg->userdata = toBG->userdata;
			bg->usepos = toBG->usepos;
			bg->usefacing = toBG->usefacing;
			bg->useroll = toBG->useroll;
			bg->delay = toBG->delay;
			copyVec3(toBG->position, bg->position);
			copyVec2(toBG->pyFace, bg->pyEnd);
			mrmGetFacePitchYawBG(msg, bg->pyStart);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(bg->start)
			{
				if (!mrmAcquireDataOwnershipBG(msg,MDC_BITS_ALL,1,NULL,NULL))
				{
					mrmEnableMsgUpdatedToFG(msg);
				}
			}
		}

		xcase MR_MSG_BG_CREATE_OUTPUT:{
			if(bg->start && !bg->finished)
			{
				U32 bit = msg->in.bg.createOutput.dataClassBit;

				switch(bit)
				{
					xcase MDC_BIT_POSITION_TARGET:{
						
					}
					xcase MDC_BIT_POSITION_CHANGE:{
						bg->timer++;

						if(bg->timer>bg->delay)
						{
							bg->finished = 1;

							toFG->finished = 1;
							
							mrmEnableMsgUpdatedToFG(msg);

							if(bg->usepos)
							{
								mrmSetPositionBG(msg, bg->position);
							}			

							if(!bg->cbFG && bg->cb)
							{
								bg->cb(bg->userdata);

								mrmDestroySelf(msg);
							}
						}
					}
					xcase MDC_BIT_ROTATION_CHANGE:{
						if (bg->usefacing)
						{
							Quat qRot;
							Vec3 pyrFace = {0,0,0};
							F32 fRatio = 1.0f;
							
							if (bg->delay > 0) {
								F32 fProgress = bg->timer/(F32)bg->delay;
								fRatio = -2*powf(fProgress,3)+3*powf(fProgress,2);
								MINMAX1(fRatio, 0.0f, 1.0f);
								if (bg->useroll) {
									F32 fRollProgress = 0.0f;
									if (bg->delay >= 2*MM_STEPS_PER_SECOND) {
										if (bg->timer < bg->delay-MM_STEPS_PER_SECOND) {
											fRollProgress = MINF(bg->timer/(F32)MM_STEPS_PER_SECOND,1.0f);
										} else {
											S32 iTimeleft = bg->delay-bg->timer;
											fRollProgress = MAXF(iTimeleft/(F32)MM_STEPS_PER_SECOND,0.0f);
										}
									} else {
										fRollProgress = 0.5f*sinf(2*PI*fProgress-0.5f*PI)+0.5f;
										fRollProgress *= MAXF(bg->delay/(F32)(2*MM_STEPS_PER_SECOND),0.1f);
									}
									pyrFace[2] = SIGN(bg->pyEnd[1]-bg->pyStart[1])*fRollProgress*RAD(15);
								}
							}
							interpPY(fRatio, bg->pyStart, bg->pyEnd, pyrFace);
							mrmSetFacePitchYawBG(msg, pyrFace);
							PYRToQuat(pyrFace, qRot);
							mrmSetRotationBG(msg, qRot);
						}
					}
				}
			}			
		}
	}
}

static void mrDoorStartEx(MovementRequester *mr, DoorCompleteCallback cb, void *data, bool bUsePosition, Vec3 position, U32 foreground, U32 delay)
{
	DoorFG *fg = NULL;
	mrGetFG(mr, mrDoorMsgHandler, &fg);

	if(!fg)
	{
		return;
	}

	fg->cb = cb;
	fg->userdata = data;
	fg->start = 1;
	mrEnableMsgCreateToBG(mr);
	fg->cbFG = foreground;
	fg->delay = delay;

	if(bUsePosition)
	{
		fg->usepos = 1;
		copyVec3(position, fg->position);
	}
}

void mrDoorStart(MovementRequester *mr, DoorCompleteCallback cb, void *data, U32 foreground)
{
	mrDoorStartEx(mr, cb, data, 0, NULL, foreground, MM_DOOR_DEFAULT_DELAY);
}

void mrDoorStartWithDelay(MovementRequester *mr, DoorCompleteCallback cb, void *data, U32 foreground, U32 delay)
{
	mrDoorStartEx(mr, cb, data, 0, NULL, foreground, delay);
}

void mrDoorStartWithTime(MovementRequester *mr, DoorCompleteCallback cb, void *data, U32 foreground, F32 seconds)
{
	mrDoorStartEx(mr, cb, data, 0, NULL, foreground, seconds / MM_SECONDS_PER_STEP);
}

void mrDoorStartWithPosition(MovementRequester *mr, DoorCompleteCallback cb, void *data, Vec3 position, U32 foreground)
{
	mrDoorStartEx(mr, cb, data, true, position, foreground, MM_DOOR_DEFAULT_DELAY);
}

void mrDoorStartWithPositionAndTime(MovementRequester *mr, DoorCompleteCallback cb, void *data, Vec3 position, U32 foreground, F32 seconds)
{
	mrDoorStartEx(mr, cb, data, true, position, foreground, seconds / MM_SECONDS_PER_STEP);
}

void mrDoorStartWithFacingDirection(MovementRequester* mr, 
									DoorCompleteCallback cb, 
									void *data,
									const Vec3 vDir,
									F32 seconds,
									bool roll)
{
	DoorFG* fg = NULL;
	if(mrGetFG(mr, mrDoorMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		fg->start = 1;
		fg->usefacing = 1;
		fg->useroll = roll;
		fg->cbFG = 1;
		fg->cb = cb;
		fg->userdata = data;
		fg->delay = seconds / MM_SECONDS_PER_STEP;
		getVec3YP(vDir, &fg->pyFace[1], &fg->pyFace[0]);
	}
}

// DoorGeoMovement Requester -----------------------------------------------------------------------

AUTO_RUN_MM_REGISTER_REQUESTER_MSG_HANDLER(	mrDoorGeoMsgHandler,
											"DoorGeoMovement",
											DoorGeo);

AUTO_STRUCT;
typedef struct DoorGeoFG {
	Vec3	posTarget;
	Quat	rotTarget;
	F32		timeTotal;
} DoorGeoFG;

AUTO_STRUCT;
typedef struct DoorGeoBGFlags {
	U32		isFirst : 1;
} DoorGeoBGFlags;

AUTO_STRUCT;
typedef struct DoorGeoBG {
	Vec3				posTarget;
	Quat				rotTarget;
	Vec3				posStart;
	Quat				rotStart;
	F32					timeTotal;
	F32					timeRemaining;
	F32					ratioFromTarget;
	DoorGeoBGFlags		flags;
} DoorGeoBG;

AUTO_STRUCT;
typedef struct DoorGeoToFG {
	S32 unused;
} DoorGeoToFG;

AUTO_STRUCT;
typedef struct DoorGeoToBG {
	Vec3	posTarget;
	Quat	rotTarget;
	F32		timeTotal;
} DoorGeoToBG;

AUTO_STRUCT;
typedef struct DoorGeoSync {
	S32 unused;
} DoorGeoSync;

AUTO_STRUCT;
typedef struct DoorGeoLocalBG{
	S32 unused;
} DoorGeoLocalBG;

void mrDoorGeoMsgHandler(const MovementRequesterMsg* msg){
	DoorGeoFG*			fg;
	DoorGeoBG*			bg;
	DoorGeoLocalBG*		localBG;
	DoorGeoToFG*		toFG;
	DoorGeoToBG*		toBG;
	DoorGeoSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, DoorGeo);

	switch(msg->in.msgType){
		xcase MR_MSG_FG_CREATE_TOBG:{
			mrmEnableMsgUpdatedToBG(msg);
			copyVec3(fg->posTarget, toBG->posTarget);
			copyQuat(fg->rotTarget, toBG->rotTarget);
			toBG->timeTotal = fg->timeTotal;
		}
		
		xcase MR_MSG_BG_GET_DEBUG_STRING:{
			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"posTarget(%1.2f, %1.2f, %1.2f)\n"
						"posStart(%1.2f, %1.2f, %1.2f)\n"
						"rotTarget(%1.2f, %1.2f, %1.2f, %1.2f)\n"
						"rotStart(%1.2f, %1.2f, %1.2f, %1.2f)\n"
						"time(%1.2f/%1.2f)"
						,
						vecParamsXYZ(bg->posTarget),
						vecParamsXYZ(bg->posStart),
						quatParamsXYZW(bg->rotTarget),
						quatParamsXYZW(bg->rotStart),
						bg->timeRemaining,
						bg->timeTotal
						);
		}
		
		xcase MR_MSG_BG_UPDATED_TOBG:{
			copyVec3(toBG->posTarget, bg->posTarget);
			copyQuat(toBG->rotTarget, bg->rotTarget);
			bg->timeTotal = toBG->timeTotal;
			bg->timeRemaining = bg->timeTotal;
			bg->flags.isFirst = 1;
			mrmGetPositionAndRotationBG(msg, bg->posStart, bg->rotStart);
			mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}
		
		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:{
			if(!mrmAcquireDataOwnershipBG(msg, MDC_BITS_ALL, 1, NULL, NULL)){
				mrmDestroySelf(msg);
			}
		}
		
		xcase MR_MSG_BG_DATA_WAS_RELEASED:{
			mrmDestroySelf(msg);
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:{
			switch(msg->in.bg.createOutput.dataClassBit){
				xcase MDC_BIT_POSITION_CHANGE:{
					Vec3	posDiff;
					Vec3	pos;
					
					if(!TRUE_THEN_RESET(bg->flags.isFirst)){
						bg->timeRemaining -= MM_SECONDS_PER_STEP;
					}
					
					MAX1(bg->timeRemaining, 0);

					if(bg->timeTotal > 0.f){
						bg->ratioFromTarget = bg->timeRemaining / bg->timeTotal;
						
						MINMAX1(bg->ratioFromTarget, 0.f, 1.f);
					}else{
						bg->ratioFromTarget = 1.f;
					}
					
					subVec3(bg->posStart, bg->posTarget, posDiff);
					scaleAddVec3(posDiff, bg->ratioFromTarget, bg->posTarget, pos);
					
					mrmSetPositionBG(msg, pos);
				}
				
				xcase MDC_BIT_ROTATION_CHANGE:{
					Quat rot;

					quatInterp(bg->ratioFromTarget, bg->rotTarget, bg->rotStart, rot);
					
					mrmSetRotationBG(msg, rot);
					
					{
						Vec3 pyFace;
						quatToPYR(rot, pyFace);
						mrmSetFacePitchYawBG(msg, pyFace);
					}
				}
			}
		}
	}
}

void mrDoorGeoSetTarget(MovementRequester* mr,
						const Vec3 pos,
						const Quat rot,
						F32 timeTotal)
{
	DoorGeoFG* fg;
	
	if(mrGetFG(mr, mrDoorGeoMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		copyVec3(pos, fg->posTarget);
		copyQuat(rot, fg->rotTarget);
		fg->timeTotal = timeTotal;
	}
}

#include "AutoGen/EntityMovementDoor_c_ast.c"