GCC_SYSTEM
#pragma once

#include "EditLibEnums.h"

typedef struct TranslateGizmo TranslateGizmo;
typedef struct RotateGizmo RotateGizmo;
typedef struct TrackerHandle TrackerHandle;
typedef struct EditState EditState;

void wleGizmoInit(void);
bool wleGizmoIsActive(void);
void wleGizmoSetMatrix(SA_PARAM_NN_VALID const Mat4 mat);
void wleGizmoUpdateMatrix(void);
void wleGizmosOncePerFrame(void);

// gizmo UI callbacks
void wleGizmosUIGizmoChanged(void *gizmo);
void wleGizmosUITransGizmoChanged(TranslateGizmo *transGizmo);
void wleGizmosUIRotGizmoChanged(RotateGizmo *rotGizmo);

// gizmo setting/cycling
bool wleTransformModeIsValid(EditTransformMode mode, EditTrackerType trackerType);
void wleChangeTransformMode(EditTransformMode mode, EditTrackerType trackerType);
EditTransformMode wleTransformModeCycle(EditTransformMode oldMode, EditTrackerType trackerType, bool backward);
void wleGizmosTransformModeSet(EditTrackerType trackerType, EditTransformMode mode);
