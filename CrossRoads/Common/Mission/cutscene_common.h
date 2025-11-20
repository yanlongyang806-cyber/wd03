#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "referencesystem.h"
#include "Message.h"
#include "CombatEnums.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "itemEnums.h"
#include "itemEnums_h_ast.h"

typedef struct Entity				Entity;
typedef struct FSM					FSM;
typedef struct Spline				Spline;
typedef struct ClientOnlyEntity		ClientOnlyEntity;
typedef struct AIAnimList			AIAnimList;
typedef struct PlayerCostume		PlayerCostume;
typedef struct SMFBlock				SMFBlock;
typedef struct WorldVariable		WorldVariable;
typedef struct ItemDef				ItemDef;
typedef enum   SlotType				SlotType;

extern DictionaryHandle g_hCutsceneDict;

#define CUTSCENE_DICTIONARY "Cutscene"

AUTO_ENUM;
typedef enum CutsceneType
{
	CutsceneType_Unspecified,
	CutsceneType_ArrivalTransition,
	CutsceneType_DepartureTransition,
} CutsceneType;

// Default interpolation type is smooth, but a cutscene can be set to have a different default
AUTO_ENUM;
typedef enum InterpolationType
{
	InterpolationType_Default = 0,
	InterpolationType_Linear,
	InterpolationType_Smooth,
	InterpolationType_SpeedUp,
	InterpolationType_SlowDown,
} InterpolationType;

AUTO_ENUM;
typedef enum CutsceneEntityType
{
	CutsceneEntityType_Custom=0,
	CutsceneEntityType_Player,
	CutsceneEntityType_TeamMember,
	CutsceneEntityType_Actor,
	CutsceneEntityType_TeamSpokesman
} CutsceneEntityType;

AUTO_ENUM;
typedef enum CutsceneOffsetType
{
	CutsceneOffsetType_Actor=0,
	CutsceneOffsetType_Player,
	CutsceneOffsetType_CutsceneEntity,
	CutsceneOffsetType_Contact,
} CutsceneOffsetType;

AUTO_ENUM;
typedef enum CutsceneEntityActionType
{
	CutsceneEntAction_Spawn=0,
	CutsceneEntAction_Waypoint,
	CutsceneEntAction_Animation,
	CutsceneEntAction_PlayFx,
	CutsceneEntAction_AddStance,
	CutsceneEntAction_Despawn,
} CutsceneEntityActionType;

AUTO_ENUM;
typedef enum CutsceneTextureSource
{
	CutsceneTextureSource_Specific=0,
	CutsceneTextureSource_FromVariable,
} CutsceneTextureSource;

AUTO_ENUM;
typedef enum CutsceneXScreenAlignment
{
	CutsceneAlignX_Center=0,
	CutsceneAlignX_Left,
	CutsceneAlignX_Right,
} CutsceneXScreenAlignment;

AUTO_ENUM;
typedef enum CutsceneYScreenAlignment
{
	CutsceneAlignY_Center=0,
	CutsceneAlignY_Top,
	CutsceneAlignY_Bottom,
} CutsceneYScreenAlignment;

AUTO_ENUM;
typedef enum CutsceneUIGenActionType
{
	CutsceneUIGenAction_MessageAndVariable=0,
	CutsceneUIGenAction_MessageOnly,
	CutsceneUIGenAction_VariableOnly,
} CutsceneUIGenActionType;

AUTO_STRUCT;
typedef struct CutscenePos
{
	F32 fMoveTime;
	F32 fHoldTime;
	Vec3 vPos;
	const char* pchNamedPoint;		// Pointname will be turned into a pos when the cutscene is run
	InterpolationType eInterpolate;
} CutscenePos;

AUTO_STRUCT;
typedef struct CutsceneEnt
{
	// Which critter this is
	char* actorName;
	const char* staticEncounterName;

	// Which FSM to use.  If blank, use the critter's current FSM
	REF_TO(FSM) hFSM;						AST( NAME(FSM) NAME(aiFSMName) )
		// TODO: ideally, we'd have FSM vars here
} CutsceneEnt;

AUTO_STRUCT AST_IGNORE(blurSize);
typedef struct CutsceneDOF
{
	F32 nearDist;
	F32 nearValue;
	F32 focusDist;
	F32 focusValue;
	F32 farDist;
	F32 farValue;
	bool fade_in;
	F32 fade_in_rate;
	bool fade_out;
	F32 fade_out_rate;
} CutsceneDOF;

AUTO_ENUM;
typedef enum CutscenePathType
{
	CutscenePathType_EasyPath = 0,
	CutscenePathType_NormalPath,
	CutscenePathType_Orbit,
	CutscenePathType_LookAround,
	CutscenePathType_WatchEntity,
	CutscenePathType_ShadowEntity,
} CutscenePathType;

AUTO_STRUCT;
typedef struct CutsceneCommonPointData
{
	F32 time;								AST(NAME("Time"))
	F32 length;								AST(NAME("Length"))
	bool fixedLength;						AST(NAME("FixedLength"))
}CutsceneCommonPointData;

AUTO_STRUCT;
typedef struct CutsceneOffsetData
{
	CutsceneOffsetType offsetType;			AST(NAME("OffsetType"))
	char *pchCutsceneEntName;				AST(NAME("CutsceneEntName"))
	char *pchStaticEncName;					AST(NAME("EncounterName"))
	char *pchActorName;						AST(NAME("ActorName"))
	char *pchBoneName;						AST(NAME("BoneName"))
	EntityRef entRef;						AST(NAME("EntityRef"))
}CutsceneOffsetData;

AUTO_STRUCT;
typedef struct CutsceneCommonTrackData
{
	char *pcName;							AST(NAME("Name"))

	//Location Offsets
	bool bRelativePos;						AST(NAME("RelativePos"))
	CutsceneOffsetData main_offset;			AST(EMBEDDED_FLAT)
	bool bTwoRelativePos;					AST(NAME("TwoRelativePos"))
	CutsceneOffsetData second_offset;		AST(NAME("SecondaryOffset"))

}CutsceneCommonTrackData;

typedef struct CutsceneDummyTrack
{
	CutsceneCommonTrackData common;
	CutsceneCommonPointData **ppGenPnts;
}CutsceneDummyTrack;

// CutsceneEffectsAndEvents
// Add a container struct and a point struct here
// Point struct must have CutsceneCommonPointData embedded as it's first member
// Container struct is an earray of points and any data global to the set of points
// Container's earray must come second and a name string comes first

AUTO_STRUCT;
typedef struct CutsceneFadePoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	Vec4 vFadeValue;						AST(NAME("FadeValue"))
	bool bAdditive;							AST(NAME("Additive"))
}CutsceneFadePoint;

AUTO_STRUCT;
typedef struct CutsceneFadeList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneFadePoint **ppFadePoints;		AST(NAME("FadePoint"))	//Must be second
}CutsceneFadeList;

AUTO_STRUCT;
typedef struct CutsceneDOFPoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	U32 bDOFIsOn : 1;						AST(NAME("DOFIsOn"))
	F32 fDOFBlur;							AST(NAME("DOFBlur"))
	F32 fDOFDist;							AST(NAME("DOFDist"))
}CutsceneDOFPoint;

AUTO_STRUCT;
typedef struct CutsceneDOFList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneDOFPoint **ppDOFPoints;			AST(NAME("DOFPoint"))	//Must be second
}CutsceneDOFList;

AUTO_STRUCT;
typedef struct CutsceneFOVPoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	F32 fFOV;								AST(NAME("FOV"))
}CutsceneFOVPoint;

AUTO_STRUCT;
typedef struct CutsceneFOVList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneFOVPoint **ppFOVPoints;			AST(NAME("FOVPoint"))	//Must be second
}CutsceneFOVList;

AUTO_STRUCT;
typedef struct CutsceneShakePoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	F32 fMagnitude;							AST(NAME("Magnitude"))
	F32 fVertical;							AST(NAME("Vertical"))
	F32 fPan;								AST(NAME("Pan"))
}CutsceneShakePoint;

AUTO_STRUCT;
typedef struct CutsceneShakeList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneShakePoint **ppShakePoints;		AST(NAME("ShakePoint"))	//Must be second
}CutsceneShakeList;

AUTO_STRUCT;
typedef struct CutsceneObjectPoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	F32 fAlpha;								AST(NAME("Alpha"))
	Vec3 vPosition;							AST(NAME("Position"))
	Vec3 vRotation;							AST(NAME("Rotation"))
}CutsceneObjectPoint;

AUTO_STRUCT;
typedef struct CutsceneObjectList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneObjectPoint **ppObjectPoints;	AST(NAME("ObjectPoint"))//Must be second
	char *pcObjectName;						AST(NAME("ObjectName"))
	U32 iObjectID;							AST(NAME("ObjectID"))
}CutsceneObjectList;

AUTO_STRUCT;
typedef struct CutsceneEntityPoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneEntityActionType actionType;	AST(NAME("ActionType"))
	REF_TO(AIAnimList) hAnimList;			AST(NAME("AnimList"))
	const char *pchStance;					AST(NAME("StanceWord") POOL_STRING)
	const char *pchFXName;					AST(NAME("FXName") POOL_STRING)
	bool bFlashFx;							AST(NAME("FlashFX"))
	Vec3 vPosition;							AST(NAME("Position"))
	Vec3 vRotation;							AST(NAME("Rotation"))
	U32	*dfxUIDs;							NO_AST 
	U32	*dfxFlashUIDs;						NO_AST 
}CutsceneEntityPoint;

AUTO_STRUCT;
typedef struct CostumeRefWrapper
{
	REF_TO(PlayerCostume) hCostume;
}CostumeRefWrapper;

AUTO_STRUCT;
typedef struct CutsceneEntityOverrideEquipment
{
	REF_TO(ItemDef) hItem;			AST(NAME("Item"))
	EARRAY_OF(CostumeRefWrapper) eaCostumes;
	ItemCategory* eaiCategories;
	SlotType eSlot;
	kCostumeDisplayMode eMode;
}CutsceneEntityOverrideEquipment;

AUTO_STRUCT;
typedef struct CutsceneEntityList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneEntityPoint **ppEntityPoints;	AST(NAME("EntityPoint"))//Must be second
	
	CutsceneEntityType entityType;			AST(NAME("EntityType"))
	REF_TO(PlayerCostume) hCostume;			AST(NAME("Costume"))
	CutsceneEntityOverrideEquipment** eaOverrideEquipment;	AST(NAME(OverrideEquipment))
	bool bPreserveMovementFX;				AST(NAME("PreserveMovementFX") DEFAULT(1))
	int EntityIdx;							AST(NAME("TeamMemberIdx"))
	char *pchStaticEncName;					AST(NAME("EntityEncounterName"))
	char *pchActorName;						AST(NAME("EntityActorName"))
	EntityRef entActorRef;					AST(NAME("EntityActorRef"))
	CharClassTypes charClassType;			AST(NAME("CharClassType"))

	EntityRef entRefParent;					NO_AST
	EntityRef entRef;						NO_AST
	U8 bCostumeLoaded;						NO_AST
	int iFramesLoaded;						NO_AST
	Vec3 vPrevPos;							NO_AST
	Vec3 vPrevRot;							NO_AST
}CutsceneEntityList;

AUTO_STRUCT;
typedef struct CutsceneTexturePoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first

	Vec2 vPosition;							AST(NAME("Position"))
	F32 fAlpha;								AST(NAME("Alpha"))
	F32 fScale;								AST(NAME("Scale") DEFAULT(1))
}CutsceneTexturePoint;

AUTO_STRUCT;
typedef struct CutsceneTextureList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneTexturePoint **ppTexPoints;		AST(NAME("TexturePoint"))//Must be second
	const char *pcTextureName;				AST(NAME("TextureName") POOL_STRING)
	const char *pcTextureVariable;			AST(NAME("TextureVariable") POOL_STRING)
	CutsceneXScreenAlignment eXAlign;		AST(NAME("XAlign"))
	CutsceneYScreenAlignment eYAlign;		AST(NAME("YAlign"))
}CutsceneTextureList;

AUTO_STRUCT;
typedef struct CutsceneFXPoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	char *pcFXName;							AST(NAME("FXName"))
	Vec3 vPosition;							AST(NAME("Position"))
	U32	dfxUID;								NO_AST 
}CutsceneFXPoint;

AUTO_STRUCT;
typedef struct CutsceneFXList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneFXPoint **ppFXPoints;			AST(NAME("FXPoint"))	//Must be second
}CutsceneFXList;

AUTO_STRUCT;
typedef struct CutsceneSoundPoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	const char *pSoundPath;					AST(NAME("SoundPath") POOL_STRING)
	const char *pcSoundVariable;			AST(NAME("SoundVariable") POOL_STRING)
	bool bUseCamPos;						AST(NAME("UseCamPos"))
	char *pchCutsceneEntName;				AST(NAME("CutsceneEntName"))
	Vec3 vPosition;							AST(NAME("Position"))
}CutsceneSoundPoint;

AUTO_STRUCT;
typedef struct CutsceneSoundList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)		//Must be first
	CutsceneSoundPoint **ppSoundPoints;		AST(NAME("SoundPoint")) //Must be second
}CutsceneSoundList;

AUTO_STRUCT;
typedef struct CutsceneSubtitlePoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)		//Must be first
	DisplayMessage displaySubtitle;			AST(STRUCT(parse_DisplayMessage))
	const char *pcSubtitleVariable;			AST(NAME("SubtitleVariable") POOL_STRING)
	char *pchCutsceneEntName;				AST(NAME("CutsceneEntName"))
	const char *pcTranslatedSubtitle;       AST(NO_TEXT_SAVE)
	SMFBlock *pTextBlock;					NO_AST
}CutsceneSubtitlePoint;

AUTO_STRUCT;
typedef struct CutsceneSubtitleList
{
	CutsceneCommonTrackData common;				AST(EMBEDDED_FLAT)		//Must be first
	CutsceneSubtitlePoint **ppSubtitlePoints;	AST(NAME("SubtitlePoint")) //Must be second
}CutsceneSubtitleList;

AUTO_STRUCT;
typedef struct CutsceneUIGenPoint
{
	CutsceneCommonPointData common;		AST(EMBEDDED_FLAT)		//Must be first
	CutsceneUIGenActionType actionType;	AST(NAME("ActionType"))
	char *pcMessage;					AST(NAME("Message"))
	char *pcVariable;					AST(NAME("Variable"))
	char *pcStringValue;				AST(NAME("StringValue"))
	F32 fFloatValue;					AST(NAME("FloatValue"))
	DisplayMessage messageValue;		AST(NAME("MessageValue") STRUCT(parse_DisplayMessage))
	char *pcMessageValueVariable;       AST(NAME("MessageValueVariable"))
	const char *pcTranslatedMessage;    AST(NO_TEXT_SAVE)
}CutsceneUIGenPoint;

AUTO_STRUCT;
typedef struct CutsceneUIGenList
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)	//Must be first
	CutsceneUIGenPoint **ppUIGenPoints;		AST(NAME("UIGenPoint"))	//Must be second
}CutsceneUIGenList;

AUTO_STRUCT;
typedef struct CutscenePathPoint
{
	CutsceneCommonPointData common;			AST(EMBEDDED_FLAT)
	Vec3 pos;								AST(NAME("Position"))
	Vec3 up;								AST(NAME("CurveUp"))//up vector to the movement curve
	Vec3 tangent;							AST(NAME("CurveTangent"))//tangent to the movement curve
	U32 easeIn : 1;							AST(NAME("EaseIn"))
	U32 easeOut : 1;						AST(NAME("EaseOut"))

	// No longer used Version 0 data
	U32 Ver0_usePntDoF : 1;					AST(NAME("UsePntDoF"))//If set then use this point's DoF instead of the paths
	F32 Ver0_moveTime;						AST(NAME("MoveTime"))//From Last Location
	F32 Ver0_holdTime;						AST(NAME("HoldTime"))
	Vec4 Ver0_cameraFade;					AST(NAME("CameraFade"))
	F32 Ver0_DoFBlur;						AST(NAME("DOFBlur"))//Depth of field is set on the target points only
	F32 Ver0_DoFDist;						AST(NAME("DOFDist"))
} CutscenePathPoint;

AUTO_STRUCT;
typedef struct CutscenePath
{
	CutsceneCommonTrackData common;			AST(EMBEDDED_FLAT)

	CutscenePathType type;					AST(NAME("Type"))// Function to use for interpolating points
	CutscenePathPoint **ppPositions;		AST(NAME("CamPosition")) // Places the camera will go
	CutscenePathPoint **ppTargets;			AST(NAME("CamTarget")) // Places the camera will focus on
	
	//Perfect Circle Paths
	F32 angle;								AST(NAME("Angle"))
	
	U32 smoothPositions : 1;				AST(NAME("SmoothPositions"))
	U32 smoothTargets : 1;					AST(NAME("SmoothTargets"))
	Spline *pCamPosSpline;					AST(NO_WRITE)
	Spline *pCamTargetSpline;				AST(NO_WRITE)

	// No longer used Version 0 data
	U32 Ver0_useDoF : 1;					AST(NAME("UseDoF"))
	F32 Ver0_DoFBlur;						AST(NAME("DOFBlur"))//Only used if UseDoF is set
	F32 Ver0_DoFDist;						AST(NAME("DOFDist"))
	bool Ver1_bFollowPlayer;				AST(NAME("FollowPlayer"))
	bool Ver1_bFollowContact;				AST(NAME("FollowContact"))
}CutscenePath;

AUTO_STRUCT;
typedef struct CutscenePathList
{
	CutscenePath **ppPaths;					AST(NAME("Path"))

	// No longer used Version 0 data
	char *Ver0_map_name;					AST(NAME("MapName"))
	F32 Ver0_blend_rate;					AST(NAME("BlendRate"))
}CutscenePathList;

AUTO_STRUCT;
typedef struct CutsceneWorldVars
{
	//When a cut scene starts, copy all the needed map vars to make sure the client has up to date map variables
	WorldVariable **eaWorldVars;
}CutsceneWorldVars;

#define CUTSCENE_DEF_VERSION 2
AUTO_STRUCT AST_IGNORE(Tags) AST_FIXUPFUNC(fixupCutsceneDef);
typedef struct CutsceneDef
{
	const char *name;							AST( STRUCTPARAM KEY POOL_STRING )

	// Filename this encounter was loaded from.
	const char *filename;						AST( CURRENTFILE )

	int iVersion;								AST(NAME("Version"))

	// List of Camera Paths to use
	CutscenePathList *pPathList;				AST(NAME("PathList"))//If specified, ignores ppCamPositions and ppCamTargets, and uses new interpolation methods

	// Lists for various effects and events
	// To add a new one do a search for CutsceneEffectsAndEvents
	CutsceneFadeList *pFadeList;				AST(NAME("FadeList"))
	CutsceneDOFList *pDOFList;					AST(NAME("DOFList"))
	CutsceneFOVList *pFOVList;					AST(NAME("FOVList"))
	CutsceneShakeList *pShakeList;				AST(NAME("ShakeList"))
	CutsceneObjectList **ppObjectLists;			AST(NAME("ObjectList"))
	CutsceneEntityList **ppEntityLists;			AST(NAME("EntityList"))
	CutsceneTextureList **ppTexLists;			AST(NAME("TextureList"))
	CutsceneFXList **ppFXLists;					AST(NAME("FXLists"))
	CutsceneSoundList **ppSoundLists;			AST(NAME("SoundList"))
	CutsceneSubtitleList **ppSubtitleLists;		AST(NAME("SubtitleList"))
	CutsceneUIGenList *pUIGenList;				AST(NAME("UIGenList"))

	// If this cut scene uses enounter/actor data, what map is it used in
	char *pcMapName;							AST(NAME("MapName"))

	// Value determining how much to blend the last frame camera position to the new desired position
	// 0 = don't blend frames
	F32 fBlendRate;								AST(NAME("BlendRate"))

	// Entities in this cutscene; if this isn't a single player cutscene, all other ents will be frozen
	CutsceneEnt **ppCutsceneEnts;				AST( SERVER_ONLY )

	// Sound to play when the cutscene starts
	const char *pchCutsceneSound;

	// Specially formatted message for cutscene subtitles.
	DisplayMessage Subtitles;					AST(STRUCT(parse_DisplayMessage))
	const char *pcTranslatedSubtitles;          AST(NO_TEXT_SAVE)

	// If false, do our best guess at freezing the map and play this scene for all players on the map.
	// This turns off all damage everywhere and resets the AI's internal clock after the cutscene is over,
	// so FSMs think no time has passed.
	// If true, play for a single player and don't affect the map or any other players
	bool bSinglePlayer;

	// If false and this cutscene is flagged as single-player, then allow the user to skip the cutscene.
	// If true or this cutscene is not flagged as single-player, then it cannot be skipped.
	bool bUnskippable;

	// If not single player and set, will hide all players on the map.
	bool bHideAllPlayers;

	// Automatically adjust the camera distance based on the entity's capsule size
	bool bAutoAdjustCameraDistance;

	// Automatically adjust the camera closer to the entity if it is clipping anything in the world
	bool bCameraClippingAvoidance;

	// if set will make the entity untargetable
	bool bPlayersAreUntargetable;

	// The minimum size the capsule needs to be in order to start adjusting the camera distance
	F32 fAutoAdjustMinEntityRadius;

	// If this is set, the cutscene will transition the camera from where the cutscene ended to the game
	// camera's initial state. This is handled in the default game camera function.
	F32 fGameCameraTweenTime;

	// Crop out world sound FX based on the distance to listener - Ex: a value of 0.5 will cut out sounds that 
	// are further away than 50% of their max distance.  A value of 0.0 would disable world SFX during the cutscene.
	F32 fSoundCropDistanceScalar;				 AST(DEFAULT(1.0))

	// If greater than zero then all entities on a non-static map will have their send range increased to this 
	// during the cut scene
	F32 fMinCutSceneSendRange;

	//If bHideAllPlayers is true, the players will fade out over a period of time defined here.
	//If it is zero it will be like the old behavior
	F32 fTimeToFadePlayers;

	//////////////////////////////////////////////////////////////////////////
	// Pre Cutscene Editor:
	// Things that are only applicable to cutscenes made before the editor was created
	// Not all features above will work with these older cutscenes

	CutscenePos **ppCamPositions;	// Places the camera will go
	CutscenePos **ppCamTargets;		// Places the camera will focus on

	// Default interpolation for the whole cutscene
	InterpolationType eInterpolate;

	// Depth of field settings for this cutscene
	CutsceneDOF *pCutsceneDepthOfField;

	// If true, do not play the cutscene more the once
	bool bPlayOnceOnly;

	// Disable the camera light while the cutscene is playing
	bool bDisableCamLight;
} CutsceneDef;

// Get the time that a path will end in seconds
F32 cutscene_GetPathEndTime(CutscenePathList* pPathList, CutscenePath *pEndPath);

// Get the time that a path will end in seconds
// Returns -1 if there are no points, but that doesn't mean the time is 0
// That means it's end time is the end time of the path before it.
F32 cutscene_GetPathEndTimeUnsafe(CutscenePath *pPath);

F32 cutscene_GetPathLength(CutscenePathList *pPathList, int idx, bool bIncludeHold);

// Get the time that a path will start in seconds
F32 cutscene_GetPathStartTime(CutscenePathList* pPathList, CutscenePath *pEndPath);

// Get the length of a cutscene in seconds
F32 cutscene_GetLength(CutsceneDef* pCutscene, bool bIncludeHold);

// Increment elapsed time but clamp at the end of a Camera Path if the Camera Path changes or the end is reached. This ensures there is one last tick on a path at low framerates.
F32 cutscene_IncrementElapsedTimeAndClamp(CutsceneDef* pCutscene, F32 prevElapsedTime, F32 timestep);

CutsceneDef* cutscene_GetDefByName( const char* pchName );

void cutscene_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);