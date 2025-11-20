/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once
#include "utils.h"
#include "windefinclude.h"

typedef struct OpenMission OpenMission;
typedef struct ErrorMessage ErrorMessage;

// Updates the thread priority of the client, based on current state
void gclUpdateThreadPriority(void);

// Debug function to draw some test objects
void gclDrawTestFxObjects(void);

// Sets up a primary display device
bool gclCreatePrimaryDevice(HINSTANCE hInstance);
bool gclCreateDummyPrimaryDevice(HINSTANCE hInstance);
void gclCheckSystemSpecsEarly(void);
void gclCheckSystemSpecsLate(void);

// Initializes the primary display device
void gclRegisterPrimaryInputDevice(HINSTANCE hInstance);
void gclBeginIgnoringInput();
void gclStopIgnoringInput();

void gclRegisterPrimaryRenderingDevice(HINSTANCE hInstance);
bool gclIsPrimaryRenderingDeviceInactive();

// Errorf callbacks for the client
void gclErrorfCallback(ErrorMessage* errMsg, void *userdata);
void gclFatalErrorfCallback(ErrorMessage* errMsg, void *userdata);
void gclHideWindowOnCrash(void);
void gclQueueError(HWND hwnd, char *str, char* title, char* fault, int highlight, void *userdata);

// Process all of the queued errors that have built up
// Sending to controller or displaying as appropriate
void gclProcessQueuedErrors(bool bDisplayErrors);

// Physics debugging
void gclUpdateRoamingCell(void);
void gclUtil_UpdatePlayerMovement(const FrameLockedTimer* flt);

// Name: gclSndVelCB
// Desc: Callback for setting a Vec3 with the player's current velocity. 
// Args: vel: Vec3 to receive the velocity
void gclSndVelCB(Vec3 vel);
void gclSndCameraMatCB(Mat4 mat);
int gclSndPlayerExistsCB(void);
void gclSndPlayerVoiceCB(const char** unOut, const char **pwOut, int *idOut);
void gclSndGetPlayerMatCB(Mat3 pos);
int gclCutsceneActiveCB(void);
bool gclSndPlayerInCombat(void);
void gclSndVerifyVoice(void);
// get the name of the entity (by ref) NULL if not found
const char *gclSndGetEntNameByRef(U32 entRef);
// turn on/off the talking bit of a entity's skeleton
void gclSndSetTalkingBit(U32 entRef, bool bEnabled);
U32 gclSndAcctToEntref(ContainerID acctid);

//send notify messages related to the voice chat channel
void gclSvNotifyJoin(void);
void gclSvNotifyLeave(void);
void gclSvNotifyFailure(void);

OpenMission *gclSndGetActiveOpenMission();

// return true if ent was found -- otherwise the position will be invalid
bool gclSndGetEntPositionByRef(U32 entRef, Vec3 entityPos);

void gclUtilsOncePerFrame(void);

// Defined in gclBaseStates.c
void gclWaitForDataLoads(void);

extern bool gbGclTestingMode;


void SetTestingMode(int iSet);

void cmdFollowTarget(void);

//called during makeBinsAndExit to make any bin files that are not
//either terrain bins or bins that always get made at startup
LATELINK;
void gclMakeAllOtherBins(void);


extern bool gbConnectToController;
extern int gIntTestClientLinkNum;
extern bool gbQueueErrorsEvenInProductionMode;
void gclUpdateControllerConnection(void);

void gclAboutToExit(void);

void gclUtil_GetStick(SA_PARAM_OP_VALID Entity *e, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfScale, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfPitch, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *pfYaw);

//sometimes useful to report a "fake" state in order to make a controller script work. Otherwise, this is
//used internally by GSM
void gclReportStateToController(char *pStateString);
