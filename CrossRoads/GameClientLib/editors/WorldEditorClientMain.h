#ifndef __WORLDEDITORCLIENTMAIN_H__
#define __WORLDEDITORCLIENTMAIN_H__
GCC_SYSTEM

#include "EditLibEnums.h"
#include "EditLib.h"

typedef struct EditorObject EditorObject;
typedef struct EMEditorDoc EMEditorDoc;
typedef struct EMEditorSubDoc EMEditorSubDoc;
typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct TrackerHandle TrackerHandle;
typedef struct GroupDefBak GroupDefBak;
typedef struct WorldCollCollideResults WorldCollCollideResults;
typedef struct WorldCollisionEntry WorldCollisionEntry;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct Model Model;
typedef struct HeightMap HeightMap;
typedef struct Material Material;
typedef struct ResourceInfo ResourceInfo;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct EMPicker EMPicker;
typedef struct WorldPatrolPointProperties WorldPatrolPointProperties;
typedef struct WorldEncounterProperties WorldEncounterProperties;
typedef struct WorldActorProperties WorldActorProperties;
typedef struct WorldActorProperties WorldActorProperties;
typedef struct WleEncObjSubHandle WleEncObjSubHandle;
typedef struct GMesh GMesh;
typedef struct GConvexHull GConvexHull;
typedef struct RotateGizmo RotateGizmo;
typedef struct PhysicalProperties PhysicalProperties;
typedef struct StashTableImp* StashTable;

#ifndef NO_EDITORS

typedef enum ResourcePackType ResourcePackType;
typedef enum EditSpecialSnapMode EditSpecialSnapMode;

typedef enum
{
	EditNormal,
	EditSelectParent,
	EditPlaceObjects,
} EditMode;

typedef enum 
{
	EditActual = 0,
	EditPivot,
	EditPivotTemp,
} EditGizmoMode;

typedef enum SplineUIMode
{
	SplineUILocked = 0,
	SplineUISelectMove,
	SplineUISelectRotate,
	SplineUICreate,
    SplineUICreateGap,
	SplineUIAttach,
} SplineUIMode;

typedef struct NetLink NetLink;
typedef struct RotateGizmo RotateGizmo;
typedef struct TranslateGizmo TranslateGizmo;
typedef struct ConeGizmo ConeGizmo;
typedef struct PyramidGizmo PyramidGizmo;
typedef struct RadialGizmo RadialGizmo;
typedef struct ScaleMinMaxGizmo ScaleMinMaxGizmo;

typedef struct WleCopyBuffer
{
	void *data;
	ParseTable *pti;
	Mat4 relMat;

	bool useCurrentSelection;
} WleCopyBuffer;

#endif
AUTO_STRUCT;
typedef struct WleTrackerBuffer
{
	char *layerName;						// NULL for object library pieces
	int uid;								// group UID
} WleTrackerBuffer;

AUTO_STRUCT;
typedef struct WlePatrolPointBuffer
{
	TrackerHandle *parentHandle;			// parent patrol of the patrol point
	WorldPatrolPointProperties *properties;	// patrol point properties
} WlePatrolPointBuffer;

AUTO_STRUCT;
typedef struct WleEncounterActorBuffer
{
	TrackerHandle *parentHandle;			// parent encounter of the actor
	WorldActorProperties *properties;		// encounter actor properties
} WleEncounterActorBuffer;
#ifndef NO_EDITORS

/******
* The EditState struct holds all of the data that determines the world editor's state.
******/
typedef struct EditState
{
	// STATE DATA
	// these are the various modes that determine editor behavior
	EditMode mode;							// overall "mode" of the editor
	EditGizmoMode gizmoMode;				// determines whether moving pivot (permanently/temporarily) or actual object
	EditTrackerType trackerType;			// used to determine if a special type of tracker is selected (i.e. lights)
	EditTransformMode transformMode;		// the active/current gizmo
	EditTransformMode queuedTransformMode;	// the gizmo that will be active when possible
	bool replaceOnCreate;					// determines whether objects placed from library will replace selection
	bool repeatCreateAcrossSelection;		// determines whether one object or multiple objects will be placed when placing from the object library while
											// existing objects are selected
	bool editOriginal;						// determines whether the user is modifying original definitions or instancing groups

	struct 
	{
		RotateGizmo *quickRotateGizmo;		// rotate gizmo used for quick rotation
		bool objectLibraryClicked;			// this is used to tell whether an object library node was clicked during the current frame
		bool quickRotating;					// indicates whether quick rotate gizmo is being used
		Mat4 pivotMat;						// holds the transformation matrix for the ghosted object about to be placed
		WleCopyBuffer ***buffer;			// things being pasted
		int timerIndex;						// timer holding the drag delay
	} quickPlaceState;

	// default parent, where new objects are created by default
	TrackerHandle *defaultParent;

	// this is a state boolean to indicate that a change has been made to the temporary pivot;
	// this is used to determine whether these changes need to be committed if the user switches
	// from temporary pivot to actual pivot editing
	bool tempPivotModified;				

	// these remember the last mod/update times of the server and object library to detect whether 
	// a change occurred to the server/library ON the server
	int lastWorldTime;
	int lastZmapTime;
	int lastObjLibTime;

	// GIZMOS DATA
	ConeGizmo *coneGizmo;
	PyramidGizmo *pyramidGizmo;
	RadialGizmo *radGizmo;
	bool drawGhosts;						// used to draw ghosts of a particular tracker while it is being modified;

	// SPLINE DATA
	bool splineDrawOnTerrain;
	F32 splineDrawOffset;
	SplineUIMode splineMode;

	// TRACKER REFRESH
	bool trackerRefreshRequested;
	U8 trackerRefreshFrames;

	// SELECTION DATA
	TrackerHandle **frozenTrackers;			// EArray of frozen trackers
	TrackerHandle **hiddenTrackers;			// EArray of hidden trackers
	TrackerHandle **editingTrackers;		// EArray of subobject editable trackers
	TrackerHandle **patrolTrackers;			// EArray of patrol trackers
	TrackerHandle **encounterTrackers;		// EArray of encounter trackers
	TrackerHandle **curveTrackers;			// EArray of curve trackers
	bool lockedSelection;					// indicates that the current selection is locked; further
											// selection-related input is ignored
	int focusedIdx;							// indicates which tracker will be the focus of the camera and tree
											// the next time tracker focusing is executed
	struct {
		int def_id1;
		int def_id2;
	} selectedPathNodeLink;

	// MOUSE POINTER DATA
	struct  
	{
		WorldCollCollideResults* results;	// updated to hold ray collide results for what is underneath the mouse

		// the following is populated whenever wleRayCollideUpdate is invoked
		WorldCollisionEntry *entry;
		WorldVolumeEntry *volumeEntry;
		PhysicalProperties *physProp;
		Model *model;
		Material *mat;
		HeightMap *heightMap;
	} rayCollideInfo;

	// DRAW DATA
	GroupDef *arrowDef;
	GroupDef *actorDef;
	GroupDef *actorDisabledDef;
	GroupDef *pointDef;

	Vec4 topFrustumPlane;					// this is the top frustum plane when snapping to Y
	Vec4 bottomFrustumPlane;				// this is the bottom frustum plane when snapping to Y
	bool planesCalculatedThisFrame;			// reset once every frame so the planes only get calculated once per frame

	// NETWORK DATA
	int editCmd;							// command sent to server to indicate that a packet is editor-related
	NetLink **link;							// address of link pointer used for sending packets
	U32 reqID;								// current request ID for outgoing packets
	StashTable asyncOpTable;				// Table of currently open asynchronous operations

	// INPUT DATA
	struct
	{
		bool boreKey;
		bool moveCopyKey;
		int transSnapEnabledKey;
	} inputData;

	// DEBUG DATA
	struct
	{
		bool debugRooms;

		GMesh *activeMesh;
		GConvexHull *activeHull;
		int *activeTriToPlane;
	} debugInfo;

} EditState;

// initialization
void worldEditorInit(int editCmd);
void worldEditorSetLink(NetLink **link);

// accessors
SA_RET_OP_VALID EMEditorDoc *wleGetWorldEditorDoc(void);

// tracker state management
void wleRefreshState(void);
void wleSelectionGetTrackerHandles(SA_PARAM_NN_VALID TrackerHandle ***output);
int wleSelectionGetCount(void);
bool wleTrackerSelect(SA_PARAM_OP_VALID const TrackerHandle *handle, bool additive);
bool wleTrackerSelectList(SA_PARAM_OP_VALID const TrackerHandle **handle, bool additive);
bool wleTrackerDeselect(SA_PARAM_OP_VALID const TrackerHandle *handle);
void wleTrackerDeselectAll(void);
bool wleTrackerIsSelected(SA_PARAM_OP_VALID const TrackerHandle *handle);
bool wleTrackerSelectToggle(SA_PARAM_OP_VALID const TrackerHandle *handle);
void wleSelectionInvert(void);
bool wleTrackerIsHidden(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerHide(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerUnhide(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerToggleHide(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerUnhideAll(void);
void wleVolumeToggleShow(int volumeTypes);
bool wleTrackerIsFrozen(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerFreeze(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerUnfreeze(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerToggleFreeze(SA_PARAM_NN_VALID const TrackerHandle *handle);
void wleTrackerUnfreezeAll(void);
void wleTrackerToggleEditSubObject(const TrackerHandle *handle);

// encounter subobject management
AUTO_STRUCT;
typedef struct WleEncObjSubHandle
{
	TrackerHandle *parentHandle;
	int childIdx;
} WleEncObjSubHandle;

SA_RET_OP_VALID WleEncObjSubHandle *wleEncObjSubHandleCreate(SA_PARAM_OP_VALID const TrackerHandle *trackerHandle, int childIdx);
SA_RET_OP_VALID WorldPatrolPointProperties *wlePatrolPointFromHandle(SA_PARAM_NN_VALID const WleEncObjSubHandle *handle, Mat4 pointMat);
SA_RET_OP_VALID WorldEncounterProperties *wleEncounterFromHandle(SA_PARAM_NN_VALID const WleEncObjSubHandle *handle);
SA_RET_OP_VALID WorldActorProperties *wleEncounterActorFromHandle(SA_PARAM_NN_VALID const WleEncObjSubHandle *handle, Mat4 actorMat);
SA_RET_OP_VALID EditorObject *wlePatrolPointEdObjCreate(const TrackerHandle *handle, int pointIdx);
SA_RET_OP_VALID EditorObject *wleEncounterActorEdObjCreate(const TrackerHandle *handle, int actorIdx);
void wlePatrolPointSelect(SA_PARAM_NN_VALID const TrackerHandle *handle, int pointIdx, bool additive, bool expandToNode);
void wleEncounterActorSelect(SA_PARAM_NN_VALID const TrackerHandle *handle, int actorIdx, bool additive, bool expandToNode);
float wlePatrolPointCollide(Vec3 start, Vec3 end, Vec3 intersection, SA_PARAM_OP_VALID WleEncObjSubHandle **object);
float wleEncounterActorCollide(Vec3 start, Vec3 end, Vec3 intersection, SA_PARAM_OP_VALID WleEncObjSubHandle **object);
bool wleIsActorDisabled(WorldEncounterProperties *encounter, WorldActorProperties *actor);

// layer actions
void wleEdObjLayerActionLock(EditorObject **selection);
void wleEdObjLayerActionRevert(EditorObject **selection);
void wleEdObjLayerActionSave(EditorObject **selection);
void wleEdObjLayerActionSaveAndClose(EditorObject **selection);

// edit state management
void wleSetDefaultParent(SA_PARAM_OP_VALID const TrackerHandle *handle);
void wleSetGizmoMode(EditGizmoMode mode);
void wleSetTranslateSnapMode(EditSpecialSnapMode mode);

// useful operation wrappers
void wleObjectPlace(int uid);
void wlePatrolPointPlace(SA_PARAM_NN_VALID const TrackerHandle *parentHandle);
void wleEncounterActorPlace(SA_PARAM_NN_VALID const TrackerHandle *parentHandle);
void wleEdObjSelectionPlace(void);
void wleTrackersMoveToMats(const TrackerHandle **handles, const Mat4 *mats, bool snapSubObjsToY);
void wleDeleteFromLib(ResourceInfo *ole);
void wleClientSave(bool autosave);
bool wleBoxContainsAA(Vec3 outerMin, Vec3 outerMax, Vec3 innerMin, Vec3 innerMax);

//Get EMPicker "Object Picker"
EMPicker* wleGetObjectPicker();

// command checks
bool wleCheckDefault(UserData unused);
bool wleCheckCmdCutCopyPaste(UserData unused);
bool wleCheckCmdDownTree(UserData unused);
bool wleCheckCmdUpTree(UserData unused);
bool wleCheckCmdDeselect(UserData unused);
bool wleCheckCmdInvertSelection(UserData unused);
bool wleCheckCmdLockSelection(UserData unused);
bool wleCheckCmdHideSelection(UserData unused);
bool wleCheckCmdUnhideAll(UserData unused);
bool wleCheckCmdFreezeSelection(UserData unused);
bool wleCheckCmdUnfreeze(UserData unused);
bool wleCheckCmdCycleGizmo(UserData unused);
bool wleCheckCmdSnapNormal(UserData unused);
bool wleCheckCmdSnapClamp(UserData unused);
bool wleCheckCmdCycleTransSnap(UserData unused);
bool wleCheckCmdCycleTransAxes(UserData unused);
bool wleCheckCmdToggleTransX(UserData unused);
bool wleCheckCmdToggleTransY(UserData unused);
bool wleCheckCmdToggleTransZ(UserData unused);
bool wleCheckCmdToggleRotSnap(UserData unused);
bool wleCheckCmdWorldPivot(UserData unused);
bool wleCheckCmdResetRot(UserData unused);
bool wleCheckCmdLockFiles(UserData unused);
bool wleCheckCmdFindAndReplace(UserData unused);
bool wleCheckCmdInstantiate(UserData unused);
bool wleCheckCmdDuplicate(UserData unused);
bool wleCheckCmdRename(UserData unused);
bool wleCheckCmdSetDefaultParent(UserData unused);
bool wleCheckCmdMoveToDefaultParent(UserData unused);
bool wleCheckCmdGroupExpandAll(UserData unused);
bool wleCheckCmdGroup(UserData unused);
bool wleCheckCmdAddToGroup(UserData unused);
bool wleCheckCmdUngroup(UserData unused);
bool wleCheckCmdDelete(UserData unused);
bool wleCheckCmdEditOrig(UserData unused);
bool wleCheckCmdUndo(UserData unused);
bool wleCheckCmdRedo(UserData unused);
bool wleCheckCmdNewLayer(UserData unused);
bool wleCheckCmdImportLayer(UserData unused);
bool wleCheckCmdNewZoneMap(UserData unused);
bool wleCheckCmdOpenZoneMap(UserData unused);
bool wleCheckCmdSave(UserData unused);
bool wleCheckCmdSaveAs(UserData unused);
bool wleCheckCmdReloadFromSource(UserData unused);
bool wleCheckCmdSaveToLib(UserData unused);
bool wleCheckCmdPlaceObject(UserData unused);
bool wleCheckCmdCopyToScratch(UserData unused);

// commands
void wleCmdFocusCamera(void);
void wleCmdDownTree(void);
void wleCmdUpTree(void);
void wleCmdDeselect(void);
void wleCmdInvertSelection(void);
void wleCmdSelectChildren(void);
void wleCmdLockSelection(void);
void wleCmdHideSelection(void);
void wleCmdUnhideAll(void);
void wleCmdFreezeSelection(void);
void wleCmdUnfreeze(void);
void wleCmdCycleGizmo(void);
void wleCmdSnapNormal(void);
void wleCmdSnapClamp(void);
void wleCmdCycleTransSnap(void);
void wleCmdSetTransSnap(SA_PARAM_NN_VALID const char *mode);
void wleCmdDecTransSnap(void);
void wleCmdIncTransSnap(void);
void wleCmdCycleTransAxes(void);
void wleCmdToggleTransX(void);
void wleCmdToggleTransY(void);
void wleCmdToggleTransZ(void);
void wleCmdToggleRotSnap(void);
void wleCmdDecRotSnap(void);
void wleCmdIncRotSnap(void);
void wleCmdWorldPivot(void);
void wleCmdResetRot(void);
void wleCmdLockFiles(void);
void wleCmdFindAndReplace(void);
void wleCmdInstantiate(void);
void wleCmdCopy(void);
void wleCmdGroup(void);
void wleCmdRename(void);
void wleCmdAddToGroup(void);
void wleCmdUngroup(void);
void wleCmdDelete(void);
void wleCmdEditOrig(void);
void wleCmdUndo(void);
void wleCmdRedo(void);
void wleCmdNewLayer(void);
void wleCmdImportLayer(void);
void wleCmdNewZoneMap(void);
void wleCmdOpenZoneMap(void);
void wleCmdSave(void);
void wleCmdClientSave(void);
void wleCmdSaveAs(void);
void wleCmdReloadFromSource(void);
void wleCmdSaveToLib(void);
void wleCmdExpPositionDummies(void);
void wleExportSelectedToVrml(void);


// main
void wleSetDocSavedBit(void);
void wleRayCollideUpdate(void);
void wleRayCollideClear(void);
void worldEditorClientOncePerFrame(bool forceRefresh);
void wlePathNodeLinkSetSelected( int def1, int def2 );

extern EditState editState;

#endif // NO_EDITORS

#endif // __WORLDEDITORCLIENTMAIN_H__
