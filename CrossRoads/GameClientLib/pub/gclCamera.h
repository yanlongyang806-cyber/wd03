/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLCAMERA_H
#define GCLCAMERA_H
GCC_SYSTEM

#include "ControlScheme.h"

typedef struct GfxCameraController GfxCameraController;
typedef struct GfxCameraView GfxCameraView;
typedef struct gclCameraSettingsStruct gclCameraSettingsStruct;
typedef struct Entity Entity;
typedef struct PowerActivation PowerActivation;

#define CAMERA_MODE_VALID(eMode) ((eMode) > kCameraMode_None && (eMode) < kCameraMode_Count)
#define CAM_DISTANCE_PRESET_COUNT	3
#define CAMERA_VALID_FOV(fFOV) (fFOV >= 1 && fFOV < 179)

AUTO_ENUM;
typedef enum CameraMode
{
	kCameraMode_None = -1,
	kCameraMode_Default = 0,
	kCameraMode_HarshTargetLock,	//Deprecated camera mode
	kCameraMode_FollowTarget,		//Mode that tracks a target
	kCameraMode_AutoTarget,			//Mode that tries to always keep the current target on-screen
	kCameraMode_LeashCamera,		//Deprecated
	kCameraMode_TurnToFace,			//The character will try to turn to face the direction of the camera
	kCameraMode_AimCamera,			//This camera mode is zoomed in and over-the-shoulder for aiming
	kCameraMode_ChaseCamera,		//The camera rotates when the character rotates
	kCameraMode_TweenToTarget,		//Tweens towards a target position and pyr
	kCameraMode_ShooterCamera,		//FPS Camera
	kCameraMode_GiganticCamera,		//Camera mode for very large characters
	
	kCameraMode_Count, EIGNORE
} CameraMode;

AUTO_STRUCT;
typedef struct CameraModeSettings
{
	CameraMode eMode;		AST( NAME(CameraMode) KEY WIKI("The mode that these settings apply to") )
	void* pModeSettings;	NO_AST
} CameraModeSettings;

AUTO_ENUM;
typedef enum ECameraInterpSpeed 
{	
	ECameraInterpSpeed_NONE = -1,
	ECameraInterpSpeed_FAST1 = 0,
	ECameraInterpSpeed_FAST,
	ECameraInterpSpeed_MEDIUM,
	ECameraInterpSpeed_SLOW,
	ECameraInterpSpeed_CONTROLLER,
	ECameraInterpSpeed_COUNT, EIGNORE
} ECameraInterpSpeed;

AUTO_STRUCT;
typedef struct CameraInterpSpeed
{
	ECameraInterpSpeed eType;	AST(NAME(Type) KEY SUBTABLE(ECameraInterpSpeedEnum))
	F32 fSpeed;					AST(NAME(Speed))
} CameraInterpSpeed;

AUTO_ENUM;
typedef enum RotationalInterpType 
{	
	kRotationalInterpType_Default		= 0,
	kRotationalInterpType_Controller	= 1,
	kRotationalInterpType_MAX,			EIGNORE
} RotationalInterpType;

AUTO_STRUCT;
typedef struct CameraSmoothNode
{
	Vec3 vVal;
	F32 fElapsedTime;
	U32 uTime;
} CameraSmoothNode;

// This struct is currently owned by a "settings" struct, but there is a precedent of state being included in the settings structs in this file
AUTO_STRUCT;
typedef struct CameraFocusSmoothingData
{
	// state data
	CameraSmoothNode** eaPositionSmoothHistory;

	// actual settings data
	F32 fFilterMagnitude;							AST(DEFAULT(0.01))
	F32 fFilterSampleTime;							AST(DEFAULT(CAMERA_DEFAULT_FILTER_TIME))

	// this is a visual choice
	F32 fMaxLagDist;								AST(DEFAULT(1.0f))

	// this is a safety check for cases where the camera has changed focus, but we are not told.  Ideally, we would be told.
	F32 fAutoTeleportDist;							AST(DEFAULT(20.0f))
} CameraFocusSmoothingData;

AUTO_STRUCT;
typedef struct CameraFocusOverrideSettings
{
	F32 fDistanceBasis;								AST(DEFAULT(15.f))		
	F32 fSpeed;										AST(DEFAULT(30.f))
	F32 fMinSpeed;
	F32 fMaxSpeed;
} CameraFocusOverrideSettings;

typedef struct CameraFocusOverride
{
	CameraSmoothNode** eaPositionSmoothHistory;
		
	CameraFocusOverrideSettings	settings;
		
	Vec3 vCurFocusPosition;
	Vec3 vLastDesiredCameraCenter;
	
	// if we are currently enabled
	bool	bEnabled;

	// need to disable, interpolating back to the desired cameraCenter before we turn off the override
	bool	bMovingToDisable;

	// on update, if set will attempt to disable the override
	bool	bResetOverride;

	bool	bOverrideLocked;
	
} CameraFocusOverride;


AUTO_STRUCT;
typedef struct CameraLookatOverrideDef
{
	// degrees/sec. the starting camera interpolation speed
	F32 fInterpStartSpeed;

	// degrees. the camera interpolation speed acceleration 
	F32 fInterpSpeedAccel;
	
	// degrees. the maximum interpolation speed
	F32 fInterpMaxSpeed;

	// After the user moves the mouse, the amount of time it takes before the camera tries to look back at the target
	F32 fLookatTimeout;

	// degrees/sec. the camera must move beyond this amount before the input camera movement is used
	F32 fIgnoreInputAngleThreshold;
	
	// degrees. The cumulative amount camera must have moved before the lookat override is ignored completely
	F32 fDisableLookatInputAngleThreshold;

	// degrees. if within this angle, the camera will begin snapping to the target
	F32 fSnapAngleThreshold;

	// when casting maintains 
	F32 fMaintainRefreshPeriod;

	// the normalized distance threshold of the current camera distance that will stop 
	// any further camera lookAt overriding.
	F32 fObstructionDistNormalizedThreshold;

} CameraLookatOverrideDef;

typedef struct CameraLookatOverride
{
	// not to be dereferenced, used only to check the current entity's
	// power activations to see what powerActivation target we care about focusing on 
	PowerActivation *pActivation;

	// if we're not using a powerActivation, what position we're looking at
	Vec3 vCameraLookatOverridePos;

	Vec2 vPYCameraOffset;

	//F32 fStartingOverrideSpeed;
	F32 fCurOverrideSpeed;
	EntityRef erEntity;
	
	// timeout on the lookat if the user tries to move the mouse
	F32 fTimeout;

	F32 fMaintainPeriod;
	
	CameraLookatOverrideDef	def;
			
	U32 bHasCameraLookAtOverride : 1;

	// if set, will continue to ignore lookat override until a new one is issued
	U32 bLockOutCameraLookatOverride : 1;

	// when set, will snap to the lookAt location after it gets close to looking at the location
	U32	bSnapAfterMatching : 1;
	
	// if not set, will ignore the pitch
	U32 bUsePitch : 1;
	
	// disallows camera input to override the lookat 
	U32 bDisallowInputOverride : 1;
	
	// if set, will disable the lookat each frame, this is mainly used for FX controlling the lookat 
	U32 bDisableLookatEachFrame : 1;

	// 
	U32 bSnapToTarget : 1;

	// 
	U32 bMaintainCheck : 1;

} CameraLookatOverride;

AUTO_STRUCT;
typedef struct CameraTargetMouseDampingDef
{
	// the amount the mouse is damped
	F32 fMouseDamp;
	
	// degrees. The angle that the mouse starts getting damped, scaling down to the InnerAngleThreshold
	F32 fOuterAngleThreshold;

	// within this angle the mouse will be fully damped at the MouseDamp value
	F32 fInnerAngleThreshold; 

	// the seconds past the power activation that the mouse will continue to be damped
	F32 fSecondsPostActivation;

} CameraTargetMouseDampingDef;

typedef struct CameraTargetMouseDamping
{
	// not to be dereferenced, used only to check the current entity's power activations
	PowerActivation *pActivation;
	PowerActivation *pCurrentActivation;

	F32 fTimeout;
	
	U32 bActive : 1;
	U32 bStartedDamping : 1;
	
} CameraTargetMouseDamping;

AUTO_STRUCT;
typedef struct CameraSpringSettings
{
	F32 fSpringFactor;
	F32 fDampening;
	const char* pchSkeletonNode;
	const char** ppchAnimBits;
	Vec3 vPosition;
	Vec3 vLastPosition;
	Vec3 vLastNodePosition;
	
	bool bUseOnlyTangentialMotion;		AST(NAME(UseOnlyTangentialMotion))
		//Use only the tangential motion to the character facing

	// Smoothing fields
	F32 fHistorySampleTime;				AST(NAME(HistorySampleTime))
	F32 fHistoryMultiplier;				AST(NAME(HistoryMultiplier) DEFAULT(1.0))
	CameraSmoothNode** eaSmoothHistory;	
} CameraSpringSettings;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndCameraSettings) AST_STRIP_UNDERSCORES WIKI("Camera Settings Doc");
typedef struct CameraSettings 
{
	// Run-time values
	U32 bIsCameraRotatingBoom : 1;				AST( NAME(IsRotatingBoom) )
	U32 bIsMouseLooking : 1;					AST( NAME(IsMouseLooking) WIKI("Is the player currently using the mouse to look around?") )				

	CameraFocusOverride	focusOverride;			NO_AST			
	CameraLookatOverride lookatOverride;		NO_AST
	CameraTargetMouseDamping mouseDamping;		NO_AST
	
	// optional.
	CameraLookatOverrideDef *pPowerActivationLookatOverride;	AST(NAME(PowerActivationLookatOverride))

	// optional. 
	CameraTargetMouseDampingDef *pPowerActivationMouseDamping;	AST(NAME(PowerActivationMouseDamping))

	// optional. Primarily used by the FX to set the camera to focus on a position. 
	// if not specified, defaults will be used
	// Note that not all fields are used for this setting. 
	CameraLookatOverrideDef *pDefaultLookatOverride;		AST(NAME(DefaultLookatOverride))

	// Values used to apply FX to the camera (as opposed to values the default camera uses to control itself).
	// Used by all client cameras (i.e., both default camera and cutscene camera)
	F32 fCameraShakeTime;						AST( NAME(CameraShakeTime) WIKI("How much longer to shake the camera for") )
	F32 fCameraShakeMagnitude;					AST( NAME(CameraShakeMagnitude) WIKI("How much the camera can deviate from its current position while shaking (on a scale of 0.0 - 1.0)") )
	F32 fCameraShakeVertical;
	F32 fCameraShakePan;
	F32 fCameraShakeSpeed;						AST( NAME(CameraShakeSpeed) WIKI("Speed at which the camera will shake (0.0 to 1.0)") )
	
	// Camera shake values used internally for slower camera movement (CameraShakeSpeed < 1).
	Vec3 vCameraShakeDestPYR;					AST( NAME(CameraShakeDestPYR) WIKI("Current goal of the camera's rotation motion for camera shake") )
	Vec3 vCameraShakeDestXYZ;					AST( NAME(CameraShakeDestXYZ) WIKI("Current goal of the camera's panning offset motion for camera shake") )
	Vec3 vCameraShakePYRVel;					AST( NAME(CameraShakePYRVel) WIKI("Current velocity of the camera's rotation motion for camera shake") )
	Vec3 vCameraShakeXYZVel;					AST( NAME(CameraShakeXYZVel) WIKI("Current velocity of the camera's panning motion for camera shake") )
	Vec3 vCameraShakeCurrentPYR;				AST( NAME(CameraShakeCurrentPYR) WIKI("Current rotation of the camera for camera shake") )
	Vec3 vCameraShakeCurrentXYZ;				AST( NAME(CameraShakeCurrentXYZ) WIKI("Current panning offset of the camera for camera shake") )
	F32 fCameraShakeShiftTime;					AST( NAME(CameraShakeShiftTime) WIKI("Time until the camera shake changes direction and picks new values for CamShakeDestPYR and CamShakeDestXYZ") )
	bool bEnableCameraShake;					AST( NAME(EnableCameraShake) WIKI("Whether or not camera shake is currently enabled") )

	// FX camera override.
	Mat4 xFXCameraMatrixOverride;				AST( NAME(FXCameraMatrixOverride) WIKI("Camera matrix for FX camera control") )
	bool bUseFXCameraMatrixOverride;			AST( NAME(UseFXCameraMatrixOverride) WIKI("Whether FX camera control is currently enabled") )
	F32 fFXCameraInfluence;						AST( NAME(FXCameraInfluence) WIKI("Percentage of influence the FX system has over camera control (position and orientation are interpolated between FX and normal control)") )

	// Some values that the client camera needs to keep up with
	F32 fLastPlayerYaw;							AST( NAME(LastPlayerYaw) WIKI("Last yaw angle of the player") )
	F32 fCameraCenterOffset;					AST( NAME(CameraCenterOffset) WIKI("How much to offset the camera in Y from the target") )

	U32 bCameraIsDefault : 1;					AST( NAME(CameraIsDefault) WIKI("Is the camera using default values") )
	U32 bCameraFirstFrameAfterDefault : 1;		AST( NAME(CameraFirstFrameAfterDefault) WIKI("Is this the first frame after the camera was using default values") )
	U32 bPlayerIsBlocked : 1;					AST( NAME(PlayerIsBlocked) WIKI("Is the player blocked by terrain or geometry") )
	U32 bForceAutoAdjust : 1;					AST( NAME(ForceAutoAdjust) WIKI("We are trying to turn the camera to face targetLoc") ) 
	U32 bResetCameraWhenAlive : 1;				AST( NAME(ResetCameraWhenAlive) WIKI("If the player is dead, wait until respawn to reset the camera") ) 
	U32 bInitialTargetLockHandled : 1;			AST( NAME(InitialTargetLockHandled) WIKI("For doing some initial setup in the target lock code") ) 
	U32 bSmoothCamera : 1;						AST( NAME(SmoothCamera) WIKI("If true, camera py rotation due to mouse movement is filtered.") )
	U32 bFlightZoom : 1;						AST( NAME(FlightZoom) WIKI("Modifies zoom path: when near target - the target is at the center of the screen, when far from target - target is near bottom half of the screen") )
	U32 bIsInAimMode : 1;						AST( NAME(IsInAimMode) )
	U32 bScaleMouseSpeedByFOV : 1;				AST( NAME(ScaleMouseSpeedByFOV) WIKI("Automatically scale the mouse speed when the FOV changes") )
	U32 bAdjustOffsetsByCapsuleSize : 1;		AST( NAME(AdjustOffsetsByCapsuleSize) WIKI("Adjust the near offset and zoom distance by the capsule size") )
	U32 bAdjustFacingTowardsCameraTarget : 1;	AST( NAME(AdjustFacingTowardsCameraTarget) WIKI("If this is on, adjust the player's facing direction to that it lines up with the camera target point") )
	U32 bEnableCameraShakeByDefault : 1;		AST( NAME(EnableCameraShakeByDefault) WIKI("Whether or not camera shake should be enabled by default") DEFAULT(1) )
	bool bUseNearOffset;						AST( NAME(UseNearOffset) WIKI("Offsets the camera to the right when zooming in") )
	U32 bIsInInspectMode : 1;					AST( NAME(IsInInspectMode) )

	// Camera positioning
	F32 fHeight;								AST( NAME(Height) WIKI("Height in feet above ground to focus camera at") )
	F32 fDistance;								AST( NAME(Distance) WIKI("Current distance in feet behind player") ) 
	F32 fOffset;								AST( NAME(Offset) WIKI("The x-offset distance in feet to the left or right of the player") )
	F32 fRadius;								AST( NAME(Radius) DEFAULT(1.8) WIKI("the collision radius around the camera") )
	F32 fTargetRadius;							AST( NAME(TargetRadius) DEFAULT(0.81) WIKI("the collision radius around the target" ) )
	F32 fPlayerHeightMultiplier;				AST( NAME(PlayerHeightMultiplier) DEFAULT(0.87) )
	F32 fMountedHeightMultiplier;				AST( NAME(MountedHeightMultiplier) DEFAULT(1.f) )
	F32 fFOVAdjustRate;							AST( NAME(FOVAdjustRate) DEFAULT(80) )
	F32 fDefaultCapsuleRadius;					AST( NAME(DefaultCapsuleRadius) DEFAULT(0.5) )
	F32 fDefaultCapsuleHeight;					AST( NAME(DefaultCapsuleHeight) DEFAULT(6.0) )
	F32 fHeightOffsetMult;						AST( NAME(HeightOffsetMult) DEFAULT(1.0) )
	F32 fZoomMultWeight;						AST( NAME(ZoomMultiplierWeight) DEFAULT(1.0) )
	F32 fZoomMultWeightLargeEntity;				AST( NAME(ZoomMultiplierWeightLargeEntity) )

	// Mouse sensivity
	F32 fMouseSensitivityDefault;				AST( NAME(MouseSensitivityDefault) DEFAULT(2.5) )
	F32 fMouseSensitivityMin;					AST( NAME(MouseSensitivityMin) DEFAULT(0.25) )
	F32 fMouseSensitivityMax;					AST( NAME(MouseSensitivityMax) DEFAULT(7.0) )

	F32 fMouseFilter;							AST( NAME(MouseFilter) )
		// Mouse filter value

	F32 fMaxDistanceMinValue;					AST( NAME(MaxDistanceMinimumValue) DEFAULT(30) )	
		// The minimum value that the player can specify for maximum camera distance
	F32 fMaxDistanceMaxValue;					AST( NAME(MaxDistanceMaximumValue) DEFAULT(70) )
		// The maximum value that the player can specify for maximum camera distance

	F32 fControllerSensitivityDefault;			AST( NAME(ControllerSensitivityDefault) DEFAULT(2) )
		// The default camera controller sensitivity value
	F32 fControllerSensitivityMin;				AST( NAME(ControllerSensitivityMin) DEFAULT(0.25) )
		// The minimum camera controller sensitivity value
	F32 fControllerSensitivityMax;				AST( NAME(ControllerSensitivityMax) DEFAULT(5) )
		// The maximum camera controller sensitivity value

	CameraInterpSpeed** eaRotInterpSpeeds;		AST( NAME(RotInterpSpeed) )
	ECameraInterpSpeed eDefaultRotInterpSpeed;	AST( NAME(DefaultRotInterpSpeed) DEFAULT(ECameraInterpSpeed_FAST) SUBTABLE(ECameraInterpSpeedEnum) )
	
	F32 pfDistancePresets[3];					AST( NAME(DistancePresets) VEC3 WIKI("Min, default, max distance in feet behind player") )
	
	F32 fAutoLevelSpeed;						AST( NAME(AutoLevelSpeed) WIKI("The current speed of the camera's auto leveling") ) 
	F32 fAutoLevelPitch;						AST( NAME(AutoLevelPitch) WIKI("Current pitch in radians to use for auto-levelling") )
	F32 v3AutoLevelPitchPresets[3];				AST( NAME(AutoLevelPitchPresets) VEC3 WIKI("Pitch in radians corresponding to distance presets") )
	F32 fAutoLevelThreshold;					AST( NAME(AutoLevelThreshold) WIKI("The range (around PI/2 in radians) around which the auto leveling is turned off or reduced") )
	F32 fAutoLevelThresholdSpeedReduction;		AST( NAME(AutoLevelThresholdSpeedReduction) WIKI("the amount the auto leveling is reduced by if within the autolevel threshold") ) 
	F32 fAutoLevelMinSpeed;						AST( NAME(AutoLevelMinSpeed) WIKI("The minimum speed the camera can auto level at") )
	F32 fAutoLevelMaxSpeed;						AST( NAME(AutoLevelMaxSpeed) WIKI("The maximum speed the camera can auto level at") ) 
	F32 fAutoLevelPitchSpeedReduction;			AST( NAME(AutoLevelPitchSpeedReduction) WIKI("The amount the auto leveling in the pitch is reduced by") )
	F32 fAutoLevelSpeedIncrease;				AST( NAME(AutoLevelSpeedIncrease) WIKI("The amount the autoleveling speed changes by when changing between the min and max speeds") )

	F32 pyrKeyboardSpeed[3];					AST( NAME(PYRKeyboardSpeed) VEC3 WIKI("Speed in radians/sec for keyboard rotation (multiplied by analog)") )
	F32 pyrJoypadSpeed[3];						AST( NAME(PYRJoypadSpeed) VEC3 WIKI("Speed in radians/sec for joypad rotation (multiplied by analog)") )
	F32 pyrMouseSpeed[3];						AST( NAME(PYRMouseSpeed) VEC3 WIKI("Current speed in radians for mouse rotation (multiplied by mouse movement)") ) 
	F32 pyrDefaultMouseSpeed[3];				AST( NAME(PYRDefaultMouseSpeed) VEC3 WIKI("Default speed in radians for mouse rotation (multiplied by mouse movement) when the camera is at the default FOV") ) 

	F32 fMinPitch;								AST( NAME(MinPitch) WIKI("The min pitch of the camera - so that when looking up, the player cannot make the camera go underneath the character (clips the character's legs)") ) 
	F32 fMaxPitch;								AST( NAME(MaxPitch) WIKI("The max pitch of the camera") ) 

	F32 pyrInterpSpeed[3];						AST( NAME(InterpSpeedPYR) VEC3 WIKI("Speed in radians/sec, for interpolating camera rotation") ) 
			
	F32 fRotInterpBasis;						AST( NAME(RotInterpBasis) WIKI("The basis used for damping the rotation interpolation") ) 
	F32 fRotInterpNormMin;						AST( NAME(RotInterpNormMin) WIKI("The minimum rotation damp normilization") ) 	
	F32 fRotInterpNormMax;						AST( NAME(RotInterpNormMax) WIKI("The maximum rotation damp normilization") ) 
	F32 fDefaultRotInterpBasis[2];				AST( NAME(DefaultRotInterpBasis) VEC2 )
	F32 fDefaultRotInterpNormMin[2];			AST( NAME(DefaultRotInterpNormMin) VEC2 )
	F32 fDefaultRotInterpNormMax[2];			AST( NAME(DefaultRotInterpNormMax) VEC2 )

	F32 fDistanceInterpSpeed;					AST( NAME(DistanceInterpSpeed) WIKI("Speed in feet/sec, for interpolating camera distance") )
	F32 fAutoLevelInterpSpeed;					AST( NAME(AutoLevelInterpSpeed) WIKI("Speed in radians/sec, for matching pitch to autolevel pitch. 0 to disable auto level") )
								
	F32 fMaxShake;								AST( NAME(MaxShake) WIKI("The maximum value that a camera can shake") ) 

	F32 fPlayerFadeDistance;					AST( NAME(PlayerFadeDistance) WIKI("Distance at which the player begins to fade (when the camera gets too close)") DEFAULT(2.0))
	F32 fPlayerInvisibleDistance;				AST( NAME(PlayerInvisibleFadeDistance) WIKI("Distance at which the player completely disappears (to avoid clipping the character). This must be less than player_fade_distance!") DEFAULT(1.0))
	F32 fFadeDistanceScale;						AST( NAME(FadeScale, FadeDistanceScale) DEFAULT(1.0) )

	F32 v2NearOffsetRanges[2];					AST( NAME(NearOffsetRanges) VEC2 WIKI("Min/Max range to begin applying near offset (if bUseNearOffset is set)") )
	F32 v2ShooterOffsetRanges[2];				AST( NAME(ShooterOffsetRanges) VEC2 )
	F32 fNearOffsetScreen;						AST( NAME(NearOffsetScreenSpace) WIKI("Offset distance in screen-space to use when the camera is close to the player (if bUseNearOffset is set)") )
	Vec3 vCurrentOffset;						AST( NAME(CurrentOffset) )
	bool bIgnoreCamPYRForNearOffset;				AST( NAME(IgnoreCamPYRForNearOffset) )

	F32 fClosestDistance;						AST( NAME(ClosestDistance) WIKI("The closest distance the camera is allowed to get to the target") )
	F32 fZoomMult;								AST( NAME(ZoomMult) )
	F32 fCloseAdjustDistance;					AST (NAME(CloseAdjustDistance) DEFAULT(5) )
	F32 fDefaultAdjustDistance;					AST (NAME(DefaultAdjustDistance) DEFAULT(5) )

	F32 fAimHeightMult;							AST( NAME(AimHeightMultiplier))
	F32 fCrouchHeightMult;						AST( NAME(CrouchHeightMultiplier) DEFAULT(0.65) )
	F32 fSitHeightMult;							AST( NAME(SitHeightMultiplier) DEFAULT(0.5) )
	F32 fHeightMultiplier;						AST( NAME(HeightMultiplier, CrouchHeightInterp) WIKI("The amount to multiply the height of the player by due to crouching or aiming, etc") )
	F32 fHeightAdjustRate;						AST( NAME(HeightAdjustRate, CrouchHeightInterpRate) )
	F32 fGiganticPlayerHeight;					AST( NAME(GiganticPlayerHeight) )
	const char** ppchCrouchActions;				AST( NAME(CrouchAction) POOL_STRING )

	int iCurrentPreset;							AST( NAME(CurrentPreset) WIKI("Which preset level we're currently at") )

	CameraMode eLastMode;						AST( NAME(LastCameraMode) DEFAULT(kCameraMode_None) )
	CameraMode eMode;							AST( NAME(CameraMode) WIKI("This is the current camera mode that the camera is using. This corresponds to a unique camera function that determines the camera behavior.") )
	void* pModeSettings;						NO_AST //The current camera mode settings
	CameraModeSettings** eaModeSettings;		AST( NAME(CameraModeSettings) WIKI("An array of settings for each of the unique camera modes") )

	CameraFocusSmoothingData* pFocusSmoothingData;			AST( NAME(FocusSmoothing) )
	
} CameraSettings;

AUTO_STRUCT;
typedef struct SnapToData
{
	F32 fHistorySampleTime; AST(NAME(HistorySampleTime))
	F32 fHistoryMultiplier;	AST(NAME(HistoryMultiplier) DEFAULT(1.0))
	F32 fMinSpeed;			AST(NAME(MinSpeed) DEFAULT(5))
	F32 fMaxSpeed;			AST(NAME(MaxSpeed) DEFAULT(30))
	F32 fMaxAngle;			AST(NAME(MaxAngle) DEFAULT(120))
} SnapToData;

//Shared between ChaseCamera, AimCamera, and AutoTargetCamera
AUTO_STRUCT;
typedef struct SnapToSettings
{
	SnapToData Data; AST(EMBEDDED_FLAT)

	//Fields that shouldn't be read from file
	CameraSmoothNode** eaHistory;
	F32 fAccum;
	bool bSnappedToTarget;
} SnapToSettings;

AUTO_STRUCT;
typedef struct ChaseCameraSettings
{
	F32 fMouseLookDelay;	AST(NAME(MouseLookDelay))
	U32 uiNextChaseTime;

	//Shared settings
	SnapToSettings SnapData; AST(EMBEDDED_FLAT)
} ChaseCameraSettings;

AUTO_STRUCT;
typedef struct AimCameraSettings
{
	F32 fMouseLookDelay;	AST(NAME(MouseLookDelay))
	F32 fDistInterpSpeed;	AST(NAME(DistInterpSpeed))
	F32 fSavedDistance;		AST(NAME(AimCameraSavedDistance) WIKI("The distance the camera was at before going into aim camera mode"))
	F32 fDistance;			AST(NAME(AimCameraDistance) WIKI("The distance the camera should be from the camera entity"))
	F32 fClosestDistance;	AST(NAME(ClosestDistanceOverride))
	F32 fTime;				AST(NAME(AimCameraTime))
	F32 fNearOffsetAim;		AST(NAME(NearOffsetAim))
	bool bUseClosestDistanceInShooterOnly; AST(NAME(UseClosestDistanceInShooterOnly))

	//These values shouldn't be read from file
	U32 uiNextAimTime;
	bool bHadTargetLastFrame;

	//Shared Settings
	SnapToSettings SnapData; AST(EMBEDDED_FLAT)
} AimCameraSettings;

AUTO_STRUCT;
typedef struct ShooterCameraSettings
{
	F32 fMinDist;			AST(NAME(MinDistance))
	F32 fMaxDist;			AST(NAME(MaxDistance))
	F32 fAimDist;			AST(NAME(AimDistance))
	F32 fClosestDistance;	AST(NAME(ClosestDistanceOverride))
	F32 fHeightMult;		AST(NAME(PlayerHeightMultiplier))
	F32 fAimFOV;			AST(NAME(AimFOV))
	F32 fNearOffsetAim;		AST(NAME(NearOffsetAim))
	F32 fNearOffsetRange;	AST(NAME(NearOffsetRange))
	F32 fHardTargetSnapAngleThreshold;	AST(NAME(HardTargetSnapAngleThreshold))
	// Shouldn't be read from file
	AST_STOP
	F32 fHeightMultOrig;
	F32 fSavedDistance;
	bool bAimModeInit;
	bool bLockOnSnap;
} ShooterCameraSettings;

AUTO_STRUCT;
typedef struct TargetLockCameraSettings
{
	F32 fMaxOffset;		AST(NAME(MaxOffset) WIKI("The maximum amount the camera can look away from the locked target")) 
	F32 fAdjustSpeed;	AST(NAME(AdjustSpeed) WIKI("The amount the camera offsets by when locked to a target and moving"))
	F32 fOffset;		AST(NAME(TargetLockOffset) WIKI("How much to look to the side when locked to a target") )
} TargetLockCameraSettings;

AUTO_ENUM;
typedef enum AutoCamLockFlags
{
	kAutoCamLockFlags_None =					0,
	kAutoCamLockFlags_MinimalRotation =			(1<<0),
	kAutoCamLockFlags_HasMouseRotated =			(1<<1),
	kAutoCamLockFlags_HadTargetLastFrame =		(1<<2),
	kAutoCamLockFlags_IsTrackingRotate =		(1<<3),
	kAutoCamLockFlags_IsInCombat =				(1<<4),
	kAutoCamLockFlags_TweenToHeading =			(1<<5),
} AutoCamLockFlags;

AUTO_STRUCT;
typedef struct AutoTargetSettings
{
	SnapToData SnapToTarget;		AST(NAME(SnapToTarget))
	SnapToData SnapToCombat;		AST(NAME(SnapToCombat))
	SnapToData SnapToMinimum;		AST(NAME(SnapToMinimum))
	SnapToData SnapToFollow;		AST(NAME(SnapToFollow))

	F32 fFollowCloseAngle;			AST(NAME(FollowCloseAngle) DEFAULT(5.0))
	F32 fMouseDelayTarget;			AST(NAME(MouseDelayTarget))
	F32 fMouseDelayNoTarget;		AST(NAME(MouseDelayNoTarget))
	F32 fCenterMultiplier;			AST(NAME(CenterMultiplier) DEFAULT(0.65))
	F32 fAutoFocusTime;				AST(NAME(AutoFocusTime) DEFAULT(0.5))
	F32 fAutoFocusSpeed;			AST(NAME(AutoFocusSpeed) DEFAULT(5))
	F32 fAutoFocusMinThrottle;		AST(NAME(AutoFocusMinThrottle) DEFAULT(0.1))
	F32 fAutoFocusMinSpeedTarget;	AST(NAME(AutoFocusMinMoveTarget) DEFAULT(0.1))
	F32 fAutoFocusAngle;			AST(NAME(AutoFocusAngle) DEFAULT(1))
	F32 fAutoFocusOffscreenMult;	AST(NAME(AutoFocusOffscreenSpeedMultiplier) DEFAULT(1.0))
	
	//These values shouldn't be read from file
	AutoCamLockFlags eFlags;
	F32 fDelay;			
	Vec2 vRotatePY;
	Vec2 vInitialTargetPY;
	F32 fAutoFocusTimeleft;
	bool bSetInitialTargetPY;
	bool bFarFromDesiredPY;

	SnapToSettings SnapTo;			AST(EMBEDDED_FLAT)
} AutoTargetSettings;

AUTO_STRUCT;
typedef struct GiganticCameraSettings
{
	F32 fMinDistMult;		AST(NAME(MinDistanceMultiplier))
	F32 fMaxDistMult;		AST(NAME(MaxDistanceMultiplier) DEFAULT(1))
	F32 fBasePlayerHeight;	AST(NAME(BasePlayerHeight) DEFAULT(6))
	F32 fHeightMult;		AST(NAME(HeightMultiplier) DEFAULT(1))
	F32 fHeightAdjustTime;	AST(NAME(HeightAdjustTime) DEFAULT(0.5))
	F32 fInitialPitch;		AST(NAME(InitialPitch))
	F32 fSavedDistance;		NO_AST
	F32 fHeightAdjustRate;	NO_AST
	bool bHeightInit;		NO_AST
	bool bPitchInit;		NO_AST
} GiganticCameraSettings;

AUTO_STRUCT;
typedef struct TweenCameraSettings
{
	Vec3 vDir;	AST(NAME(TweenDirection) WIKI("Tween direction vector"))
} TweenCameraSettings;

#define CAMERA_DEFAULT_FILTER_TIME 1/4.0f

AUTO_STRUCT;
typedef struct SmoothCameraSettings
{
	CameraSmoothNode** eaSmoothHistory;
	F32 fCurveProgress;
	F32 fMouseFilter;								AST(NAME(OverrideMouseFilter) DEFAULT(0.5))
	F32 fCurveSampleRate;							AST(NAME(CurveSampleRate) DEFAULT(0.1))
	F32 fFilterSampleTime;							AST(NAME(FilterSampleTime) DEFAULT(CAMERA_DEFAULT_FILTER_TIME))
	bool bUseSplineCurve;							AST(NAME(UseSplineCurve))
} SmoothCameraSettings;

AUTO_STRUCT;
typedef struct DefaultCameraModeSettings
{
	AimCameraSettings* pAimSettings;				AST(NAME(AimSettings))
	ShooterCameraSettings* pShooterSettings;		AST(NAME(ShooterSettings))
	ChaseCameraSettings* pChaseSettings;			AST(NAME(ChaseSettings))
	TargetLockCameraSettings* pTargetLockSettings;	AST(NAME(TargetLockSettings))
	AutoTargetSettings* pAutoTargetSettings;		AST(NAME(AutoTargetSettings))
	TweenCameraSettings* pTweenSettings;			AST(NAME(TweenTargetSettings))
	GiganticCameraSettings* pGiganticSettings;		AST(NAME(GiganticSettings))
	SmoothCameraSettings SmoothSettings;			AST(NAME(SmoothSettings))
} DefaultCameraModeSettings;

extern CameraSettings g_CameraSettings;
extern CameraSettings g_DefaultCameraSettings;

GfxCameraController *gclLoginGetCameraController(void);
void gclDefaultCameraFunc(GfxCameraController* pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime);

//Helper functions
void			gclCamera_Unlock(GfxCameraController* pCamera, F32 fElapsedTime);
void			gclCamera_ApplyProperZDistanceForDemo(Mat4 xCameraMatrix, bool bIsAbsolute);
void			gclCamera_MatchPlayerFacing(bool bMatchPitch, bool bForce, bool useOverridePitch, F32 fOverridePitch);
void			gclCamera_TurnToFaceTarget(void);
void			gclCamera_Reset(void);
void			gclCamera_Shake(F32 fTime, F32 fMagnitude, F32 fVertical, F32 fPan, F32 fSpeed);
void			gclCamera_DisableMouseLook(void);
void			gclCamera_OnMouseLook(U32 bIsMouseLooking);
void			gclCamera_UpdateModeForRegion(Entity* pEnt);
void			gclCamera_UpdateRegionSettings(SA_PARAM_NN_VALID Entity* pEnt);
void			gclCamera_FindProperZDistance(GfxCameraController *pCamera, const Mat4 xCameraMatrix, F32 fDTime, F32 fDistScale, F32 fDefaultDist);
F32				gclCamera_GetZDistanceFromTargetEx(GfxCameraController *pCamera, const Mat4 xCameraMatrix, Vec3 vEntPos, U32* piRayHitCount);
F32				gclCamera_GetZDistanceFromTarget(GfxCameraController *pCamera, const Mat4 xCameraMatrix);
void			gclCamera_GetTargetPosition(GfxCameraController *pCamera, Vec3 vEntPos);
void			gclCamera_GetShake(const GfxCameraController *pCamera, Vec3 vPYROut, Vec3 vXYZOut, F32 fElapsedTime);
void			gclCamera_DoEntityCollisionFade(Entity* pIgnoreEnt, Vec3 vCameraPos);

//Mutators
bool			gclCamera_SetMode(CameraMode eMode, bool bCheckValidForRegion);
bool			gclCamera_SetLastMode(bool bCheckValidForRegion, bool bUseDefaultOnFail);
void			gclCamera_AdjustDistancePresetsFromMax(F32 fMaxDistance);
void			gclCamera_SetDistance(F32 fDist);
void			gclCamera_SetOffset(F32 fOffset);
void			gclCamera_AdjustDistancePresets(F32 fClose, F32 fMedium, F32 fFar);
void			gclCamera_AdjustCameraHeight(F32 fHeight);
void			gclCamera_SetLockPitch(bool bLock);
void			gclCamera_SetLockYaw(bool bLock);
void			gclCamera_SetStartingYaw(F32 fVal, bool bSet);
void			gclCamera_SetStartingPitch(F32 fVal, bool bSet);
void			gclCamera_SetClosestDistance(F32 fDist);
void			gclCamera_SetMouseLookSensitivity(F32 fSensitivity);
void			gclCamera_SetControllerLookSensitivity(F32 fSensitivity);
void			gclCamera_SetCameraInputTurnLeft(int bIsTurning);
void			gclCamera_SetCameraInputTurnRight(int bIsTurning);
void			gclCamera_UseNearOffset(bool bEnable);
void			gclCamera_UseFlightZoom(bool bEnable);
void			gclCamera_UseLockToTarget(bool bEnable);
void			gclCamera_UseAutoTargetLock(bool bEnable);
void			gclCamera_SetMaxDistanceRanges(F32 fMin, F32 fMax);
void			gclCamera_UseAimCamera(bool bEnable);
void			gclCamera_UseChaseCamera(bool bEnable);
void			gclCamera_ForceAutoAdjust(bool bEnable);
void			gclCamera_SetFOV(F32 fFOV);
void			gclCamera_SetFxCameraMatrixOverride(Mat4 xMatrix, bool bEnable, F32 fInfluence);

void			gclCamera_SetFocusEntityOverride(Entity *e);

void			gclCamera_EnableFocusOverride(F32 fSpeed, F32 fDistanceBasis);
void			gclCamera_DisableFocusSmoothOverride(bool bDisableImmediate);

void			gclCamera_SetLookatOverride(const Vec3 vPos, F32 fSpeed);
void			gclCamera_SetPowerActivationLookatOverride(PowerActivation *pActivation);

//Accessors
CameraSettings* gclCamera_GetSettings(const GfxCameraController* pCamera);
void*			gclCamera_FindSettingsForMode(SA_PARAM_OP_VALID GfxCameraController* pCamera, CameraMode eMode, bool bCreate);
void*			gclCamera_GetDefaultSettingsForMode(CameraMode eMode);
Entity*			gclCamera_GetEntity(void);
void			gclCamera_GetFacingDirection(GfxCameraController* pCamera, bool bIgnorePitch, Vec3 vTargetDir);
void			gclCamera_GetAdjustedPlayerFacingDirection(GfxCameraController* pCamera, Entity* e, F32 fRange, bool bIgnorePitch, const Vec3 vOverrideTargetDir, Vec3 vOffsetDir);
void			gclCamera_GetAdjustedPlayerFacing(GfxCameraController* pCamera, Entity* e, F32 fRange, F32* pfFaceYaw, F32* pfFacePitch);
bool			gclCamera_IsInMode(CameraMode eMode);
bool			gclCamera_ShouldTurnToFace(void);


bool			gclCamera_IsMouseLooking(GfxCameraController* pCamera);
bool			gclCamera_IsCameraTurningViaInput();
F32				gclCamera_GetDefaultModeDistance(GfxCameraController* pCamera);
F32				gclCamera_GetPlayerHeight(GfxCameraController* pCamera);
F32				gclCamera_GetKeyboardYawTurnSpeed();
void			gclCamera_SetCameraInputLookUp(int bIsTurning);
void			gclCamera_SetCameraInputLookDown(int bIsTurning);
CameraType		gclCamera_GetTypeFromMode(CameraMode eMode);
F32				gclCamera_GetMouseSensitivityMin(void);
F32				gclCamera_GetMouseSensitivityMax(void);

LATELINK;
void gclCamera_GameSpecificInit(void);
LATELINK;
void gclCamera_GameSpecificDeInit(void);
void gclCamera_UpdateNearOffset(SA_PARAM_NN_VALID Entity* pEnt);
#endif //_CAMERA_H_

