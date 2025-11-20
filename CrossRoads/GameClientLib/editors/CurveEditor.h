#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "SplineEditUI.h"

#include "WorldEditorAttributes.h"
#include "EditorManager.h"
#include "EditorObject.h"

#include "wlCurve.h"

typedef struct CurveControlPoint
{
	TrackerHandle *handle;
	int index;
} CurveControlPoint;

EditorObject *curveCPCreateEditorObject(TrackerHandle *handle, int index, ZoneMapLayer *layer);
EditorObject *curveGapCreateEditorObject(TrackerHandle *handle, int index, ZoneMapLayer *layer);
void curveEditorDrawFunction(EditorObject **objects);

// Apply constraints to an *editable* tracker (within wleOpPropsBegin/wleOpPropsEnd)
void curveApplyConstraints(GroupTracker *tracker);

#endif
