#ifndef __EDITOROBJECT_H__
#define __EDITOROBJECT_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "StashTable.h"

typedef struct EditorObject EditorObject;
typedef struct TranslateGizmo TranslateGizmo;
typedef struct RotateGizmo RotateGizmo;
typedef struct EditUndoStack EditUndoStack;
typedef struct UIMenuItem UIMenuItem;
typedef struct GroupProperties GroupProperties;
typedef struct LogicalGroup LogicalGroup;

// These define the possible types that can be passed into the attribute editor callbacks.
typedef enum EditorObjectTypeEnum
{
	EDTYPE_NONE = 0,
	EDTYPE_LAYER,
	EDTYPE_TRACKER,
	EDTYPE_CURVE_CP, // New stuff
	EDTYPE_CURVE_GAP, // New stuff
	EDTYPE_PATROL_POINT,
	EDTYPE_ENCOUNTER_ACTOR,
	EDTYPE_FX_TARGET_NODE,
	EDTYPE_DUMMY,
	EDTYPE_LOGICAL_GROUP,
} EditorObjectTypeEnum;

// Moving
typedef enum EditorObjectPivotMode
{
	EdObjEditActual = 0,
	EdObjEditPivot,
} EditorObjectPivotMode;

typedef enum EditorObjectGizmoMode
{
	EdObjTranslate,
	EdObjRotate,
} EditorObjectGizmoMode;

typedef void (*EdObjVoidCallback)(void);
typedef bool (*EdObjVoidCheckCallback)(void);
typedef void (*EdObjCallback)(EditorObject*);
typedef U32 (*EdObjCRCCallback)(EditorObject*);
typedef void (*EdObjMatCallback)(EditorObject*, Mat4);
typedef void (*EdObjBoundsCallback)(EditorObject*, Vec3, Vec3);
typedef bool (*EdObjCheckCallback)(EditorObject*);
typedef bool (*EdObjMovementEnableCallback)(EditorObject*, EditorObjectGizmoMode);
typedef int (*EdObjCompCallback)(EditorObject*, EditorObject*);
typedef void (*EdObjListCallback)(EditorObject**);
typedef float (*EdObjClickDetectCallback)(int, int, EditorObject***);
typedef void (*EdObjMarqueeDetectCallback)(int, int, int, int, int, Mat4, Mat44, EditorObject***, bool);
typedef void (*EdObjChangedCallback)(EditorObject*, void *);
typedef void (*EdObjSelectionChangedCallback)(EditorObject**, bool);
typedef void (*EdObjContextMenuFunc)(EditorObject*, UIMenuItem***);
typedef void (*EdObjChildrenCallback)(EditorObject *, EditorObject ***);
typedef EditorObject* (*EdObjParentCallback)(EditorObject *);

typedef struct EditorObjectType
{
	EditorObjectTypeEnum objType;
	EdObjCallback freeFunc;
	EdObjCRCCallback crcFunc;
	EdObjListCallback drawFunc;
	EdObjClickDetectCallback clickFunc;
	EdObjMarqueeDetectCallback marqueeFunc;
	EdObjChangedCallback changedFunc;
	EdObjChildrenCallback childFunc;
	EdObjParentCallback parentFunc;

	// selection callbacks
	EdObjCompCallback compareFunc;			// used for selection checking, possible ordering later;
											// must use convention of returning zero when equal, non-zero otherwise
	EdObjCompCallback compareForUIFunc;
	EdObjCheckCallback selectFunc;			
	EdObjCheckCallback deselectFunc;
	EdObjSelectionChangedCallback selectionChangedFunc;

	EdObjMovementEnableCallback movementEnableFunc;	// Called once per frame to determine if the selected object can be moved

	// movement callbacks
	EdObjMatCallback getMatFunc;
	EdObjBoundsCallback getBoundsFunc;		// called to get axis-aligned bounding box vertices
	EdObjListCallback startMoveFunc;		// called once the user starts a movement action
	EdObjListCallback movingFunc;			// called once per frame while a gizmo is active
	EdObjListCallback endMoveFunc;			// called after the user ends a movement action

	// context menu callbacks
	EdObjContextMenuFunc menuFunc;			// called to create a context menu

	// actions
	StashTable action_table;				// set of name -> callback structs for selection actions

	// type-specific data
	EditorObject **selection;
} EditorObjectType;

void editorObjectTypeRegister(EditorObjectTypeEnum type,
							  SA_PARAM_OP_VALID EdObjCRCCallback crcFunc,
							  SA_PARAM_OP_VALID EdObjCallback freeFunc, 
							  SA_PARAM_OP_VALID EdObjChildrenCallback childFunc,
							  SA_PARAM_OP_VALID EdObjParentCallback parentFunc,
							  SA_PARAM_OP_VALID EdObjListCallback drawFunc, 
							  SA_PARAM_OP_VALID EdObjClickDetectCallback clickFunc,
							  SA_PARAM_OP_VALID EdObjMarqueeDetectCallback marqueeFunc, 
							  SA_PARAM_OP_VALID EdObjChangedCallback changedFunc,
							  SA_PARAM_OP_VALID EdObjMovementEnableCallback movementEnableFunc);
void edObjTypeSetCompCallback(EditorObjectTypeEnum objType, SA_PARAM_NN_VALID EdObjCompCallback compareFunc, SA_PARAM_OP_VALID EdObjCompCallback compareForUIFunc);
void edObjTypeSetSelectionCallbacks(EditorObjectTypeEnum objType, SA_PARAM_OP_VALID EdObjCheckCallback selectFunc, SA_PARAM_OP_VALID EdObjCheckCallback deselectFunc, SA_PARAM_OP_VALID EdObjSelectionChangedCallback selectionChangedFunc);
void edObjTypeSetMovementCallbacks(EditorObjectTypeEnum objType, SA_PARAM_OP_VALID EdObjMatCallback getMatFunc, SA_PARAM_OP_VALID EdObjListCallback startMoveFunc, SA_PARAM_OP_VALID EdObjListCallback movingFunc, SA_PARAM_OP_VALID EdObjListCallback endMoveFunc);
void edObjTypeSetMenuCallbacks(EditorObjectTypeEnum type, SA_PARAM_OP_VALID EdObjContextMenuFunc menuCreateFunc);
void edObjTypeSetBoundsCallback(EditorObjectTypeEnum type, SA_PARAM_OP_VALID EdObjBoundsCallback getBoundsFunc);
SA_RET_OP_VALID EditorObjectType *editorObjectTypeGet(EditorObjectTypeEnum type);
void edObjTypeActionRegister(EditorObjectTypeEnum objType, SA_PARAM_NN_STR const char *action_name, SA_PARAM_NN_VALID EdObjListCallback actionFunc);

// This is a generic encapsulation of manipulated objects in the editor and is passed around a variety
// of systems to ensure behavioral uniformity
#endif
AUTO_STRUCT;
typedef struct EditorObject
{
	char *name;

#ifndef NO_EDITORS
	EditorObjectType *type;			NO_AST
	void *context;					NO_AST
	void *obj;						NO_AST
	int ref_count;					NO_AST

	GroupProperties *props_cpy;		NO_AST
	LogicalGroup *logical_grp_cpy;	NO_AST
	bool logical_grp_editable;		NO_AST

	Mat4 oldMat;					NO_AST
	Mat4 mat;						NO_AST
#endif
} EditorObject;
#ifndef NO_EDITORS

SA_RET_NN_VALID EditorObject *editorObjectCreate(SA_PARAM_OP_VALID void *obj, SA_PARAM_OP_STR const char *name, SA_PARAM_OP_VALID void *context, EditorObjectTypeEnum objType);
void editorObjectRef(SA_PARAM_NN_VALID EditorObject *edObj);
void editorObjectDeref(SA_PARAM_NN_VALID EditorObject *edObj);

// Util
bool edObjIsDescendant(SA_PARAM_NN_VALID EditorObject *parent, SA_PRE_NN_VALID SA_POST_OP_VALID EditorObject *child);

// Selection
int edObjCompare(SA_PARAM_NN_VALID EditorObject *edObj1, SA_PARAM_NN_VALID EditorObject *edObj2);
int edObjCompareForUI(SA_PARAM_NN_VALID EditorObject *edObj1, SA_PARAM_NN_VALID EditorObject *edObj2);
EditorObject *edObjFindSelected(SA_PARAM_NN_VALID EditorObject *edObj);
bool edObjSelect(SA_PARAM_NN_VALID EditorObject *edObj, bool additive, bool expandToNode);
bool edObjDeselect(SA_PARAM_NN_VALID EditorObject *edObj);
bool edObjSelectList(SA_PARAM_OP_OP_VALID EditorObject **edObjs, bool additive, bool expandToNode);
void edObjFinishUp();
#define edObjDeselectList(edObjs) edObjDeselectListEx(edObjs, true)
bool edObjDeselectListEx(SA_PARAM_OP_OP_VALID EditorObject **edObjs, bool finishUp);
bool edObjSelectToggle(SA_PARAM_NN_VALID EditorObject *edObj);
void edObjSelectDownTree(void);
void edObjSelectUpTree(void);
EditorObject **edObjSelectionGet(EditorObjectTypeEnum objType);
void edObjSelectionGetAll(EditorObject ***selection);
int edObjSelectionGetCount(EditorObjectTypeEnum objType);
#define edObjSelectionClear(objType) edObjSelectionClearEx(objType, true)
void edObjSelectionClearEx(EditorObjectTypeEnum objType, bool finishUp);
void edObjHarnessInit(void);
void edObjHarnessOncePerFrame(void);
EditUndoStack *edObjGetUndoStack(void);

// Harness
#define HARNESS_QUICKPLACE_TIME (EditorPrefGetFloat(WLE_PREF_EDITOR_NAME, WLE_PREF_CAT_OPTIONS, "QuickPlaceDelay", 1.0))

void edObjGetMatrix(SA_PARAM_NN_VALID EditorObject *edObj, Mat4 outMat);
void edObjSetMatrix(SA_PARAM_NN_VALID EditorObject *edObj, SA_PARAM_NN_VALID const Mat4 mat);
void edObjRefreshMat(SA_PARAM_NN_VALID EditorObject *edObj);
void edObjRefreshAllMatrices(void);
void edObjSelectionRefreshMat(EditorObjectTypeEnum objType);
void edObjSelectionMoveToMat(const Mat4 mat);
void edObjHarnessDeactivate(const Mat4 mat, void *unused);
void edObjHarnessSetGizmoMatrix(SA_PARAM_NN_VALID const Mat4 mat);
void edObjHarnessSetGizmoMatrixAndCallback(SA_PARAM_NN_VALID const Mat4 mat);
void edObjHarnessGetGizmoMatrix(SA_PARAM_NN_VALID Mat4 outMat);
void edObjHarnessGizmoMatrixUpdate(void);
void edObjHarnessSetPivotMode(EditorObjectPivotMode mode);
void edObjHarnessSetGizmo(EditorObjectGizmoMode mode);
EditorObjectGizmoMode edObjHarnessGetGizmo();
SA_RET_NN_VALID TranslateGizmo *edObjHarnessGetTransGizmo(void);
SA_RET_NN_VALID RotateGizmo *edObjHarnessGetRotGizmo(void);
bool edObjHarnessGetMarqueeCrossingMode(void);
void edObjHarnessSetMarqueeCrossingMode(bool crossingMode);
bool edObjHarnessGetSnapNormal(void);
bool edObjHarnessGetSnapNormalInverse(void);
int edObjHarnessGetSnapNormalAxis(void);
bool edObjHarnessGizmoIsActive(void);
void editorObjectActionDispatch(const char *action_name);
void edObjHarnessEnableSelection(bool enabled);

#endif // NO_EDITORS

#endif // __EDITOROBJECT_H__