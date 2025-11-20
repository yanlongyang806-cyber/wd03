#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "EditLibGizmos.h"
#include "WorldEditorClientMain.h"

typedef struct Spline Spline;
typedef struct UIComboBox UIComboBox;
typedef struct UIButton UIButton;
typedef struct UIWindow UIWindow;
typedef struct EditUndoStack EditUndoStack;
typedef struct SplineUIGizmos SplineUIGizmos;

typedef void (*CreateCurveGapCB)(EditorObject *object, F32 gap_offset);
typedef void (*UpdateControlPointCB)(EditorObject *object, int selected_point,
									 Vec3 *point, Vec3 *dir, Vec3 *up,
									 void *attach_data, int attach_point, bool new_point, F32 new_offset);
typedef bool (*GetAttachmentPointCB)(EditorObject *object, SplineUIMode mode, Vec3 start, Vec3 end, 
								void **out_data, Spline **out_spline, int *out_point);

typedef struct SplineUICallbacks {
	//bool alignedToWorld;
	//Mat4 world_rotate_mat;
	//UIComboBox *transSnapDroplist;
	//UIButton *worldPivotButton, *snapNormalButton, *snapClampingButton, *transSnapButton, *rotSnapButton, *drawModeButton;
	//UITextEntry *heightTextEntry;

	CreateCurveGapCB gap_cb;
	UpdateControlPointCB control_point_cb;
	GetAttachmentPointCB attach_point_cb;
} SplineUICallbacks;

void splineUIDrawCurve(Spline *spline, bool selected);
void splineUIDrawControlPointVectors(Vec3 point, Vec3 delta, Vec3 up, bool selected, bool highlighted, bool attached, bool welded);
void splineUIDrawControlPoint(Spline *spline, int index, bool selected, bool highlighted, bool attached, bool welded);
void splineUIDrawControlPointWidget(Vec3 pos, Vec3 dir, Vec3 up, Color color, bool attached, F32 scale);
void splineUIDrawDebug(Spline *spline, bool selected, int selected_point, int highlighted_point);
void splineUIDrawWithPoints(Spline *spline, bool selected, bool editing);
void splineUIDrawControlPoints(Spline *spline, int highlighted_point);
void splineUIDrawGap(Spline *spline, F32 point, F32 size, bool selected);

bool splineUICollideGap(Vec3 begin, Vec3 end, Spline *spline, F32 point, Vec3 intersect);

//void splineUISetCallbacks(SplineUIGizmos *gizmos, CreateCurveGapCB gap_cb, UpdateControlPointCB control_point_cb, GetAttachmentPointCB attach_point_cb);
//void splineUIInitGizmos(SplineUIGizmos *gizmos, TranslateGizmoTriGetter triF);
//void splineUIDeinitGizmos(SplineUIGizmos *gizmos);
//void splineUIUndo(SplineUIGizmos *gizmos);
//void splineUIRedo(SplineUIGizmos *gizmos);

void splineUISetCurrentMode(SplineUIMode mode);
SplineUIMode splineUIGetCurrentMode();

bool splineUIDoMatrixUpdate(Spline *spline, int selected_point, const Mat4 parent_matrix, const Mat4 new_matrix, Vec3 pos, Vec3 delta, Vec3 up);
//void splineUISetSelectedPoint(SplineUIGizmos *gizmos, Spline *spline, int index);

SplineUIMode splineUIHandleInput(SplineUICallbacks *callbacks, ZoneMapLayer *layer, EditorObject **objects);

#endif