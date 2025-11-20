#ifndef __EDITLIBGIZMOS_H__
#define __EDITLIBGIZMOS_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct AutoSnapGizmo AutoSnapGizmo;
typedef struct TranslateGizmo TranslateGizmo;
typedef struct RotateGizmo RotateGizmo;
typedef struct ConeGizmo ConeGizmo;
typedef struct PyramidGizmo PyramidGizmo;
typedef struct RadialGizmo RadialGizmo;
typedef struct ScaleMinMaxGizmo ScaleMinMaxGizmo;
typedef struct ModelOptionsToolbar ModelOptionsToolbar;
typedef struct UIWindow UIWindow;
typedef struct EMToolbar EMToolbar;
typedef struct GfxCameraController GfxCameraController;
typedef struct MaterialNamedConstant MaterialNamedConstant;
typedef struct EditLibGizmosToolbar EditLibGizmosToolbar;

// Callback function types for snap detection
typedef bool (*AutoSnapTriGetter)(Vec3 start, Vec3 end, Mat3 verts, Vec3 normal);
typedef bool (*AutoSnapPointGetter)(Vec3 start, Vec3 end, Vec3 intersection, Vec3 normal);
typedef bool (*AutoSnapNullGetter)(Vec3 start, Vec3 end, Vec3 intersection);

typedef void (*GizmoActivateFn)(const Mat4 matrix, void *context);
typedef void (*GizmoDeactivateFn)(const Mat4 matrix, void *context);

typedef void (*GizmoActivateScaleMinMaxFn)(const Mat4 matrix, const Vec3 scale[2], void *context);
typedef void (*GizmoDeactivateScaleMinMaxFn)(const Mat4 matrix, const Vec3 scale[2], void *context);

// Snap modes
AUTO_ENUM;
typedef enum EditSpecialSnapMode
{
	EditSnapGrid = 0,
	EditSnapVertex,
	EditSnapMidpoint,
	EditSnapEdge,
	EditSnapFace,
	EditSnapTerrain,
	EditSnapSmart,
	EditSnapNone,
} EditSpecialSnapMode;

// Useful Math Functions
void GizmoSnapToNormal(int snapNormalAxis, bool snapNormalInverse, Mat4 mat, Vec3 norm);

// General Gizmo Functions
#define GIZMO_NUM_ANGLES 6
#define GIZMO_NUM_WIDTHS 14
int GizmoGetSnapAngle(int res);
int *GizmoGetSnapAngles(void);
float GizmoGetSnapWidth(int res);
float *GizmoGetSnapWidths(void);

// Constructors and Destructors
ConeGizmo *ConeGizmoCreate(void);
PyramidGizmo *PyramidGizmoCreate(void);
RadialGizmo *RadialGizmoCreate(void);

void ConeGizmoDestroy(ConeGizmo *gizmo);
void PyramidGizmoDestroy(PyramidGizmo *gizmo);
void RadialGizmoDestroy(RadialGizmo *gizmo);

// Rotate Gizmo Functions
RotateGizmo *RotateGizmoCreate(void);
void RotateGizmoDestroy(RotateGizmo* rotGizmo);

void RotateGizmoUpdate(RotateGizmo* rotGizmo);
void RotateGizmoDraw(RotateGizmo* rotGizmo);

void RotateGizmoSetCallbackContext(RotateGizmo* rotGizmo, void *context);
void RotateGizmoSetActivateCallback(RotateGizmo* rotGizmo, GizmoActivateFn activate_fn);
void RotateGizmoSetDeactivateCallback(RotateGizmo* rotGizmo, GizmoDeactivateFn deactivate_fn);
EditLibGizmosToolbar *RotateGizmoGetToolbar(RotateGizmo *rotGizmo);
void RotateGizmoSetToolbar(RotateGizmo *rotGizmo, EditLibGizmosToolbar *toolbar);

void RotateGizmoSetMatrix(RotateGizmo* rotGizmo, const Mat4 startMat);
void RotateGizmoGetMatrix(RotateGizmo* rotGizmo, Mat4 mat);
void RotateGizmoSetAlignedToWorld(RotateGizmo *rotGizmo, bool aligned);
bool RotateGizmoGetAlignedToWorld(RotateGizmo *rotGizmo);
void RotateGizmoResetRotation(RotateGizmo* rotGizmo);
bool RotateGizmoIsActive(RotateGizmo* rotGizmo);
void RotateGizmoSetSnapResolution(RotateGizmo *rotGizmo, int res);
int RotateGizmoGetSnapResolution(RotateGizmo *rotGizmo);
void RotateGizmoEnableSnap(RotateGizmo* rotGizmo, bool enabled);
void RotateGizmoEnableAxis(RotateGizmo* rotGizmo, bool pitchEnabled, bool yawEnabled, bool rollEnabled);
bool RotateGizmoIsSnapEnabled(RotateGizmo* rotGizmo);
void RotateGizmoSetRelativeDrag(RotateGizmo* rotGizmo, bool relDrag);
void RotateGizmoActivateAxis(RotateGizmo *rotGizmo, int axis);

// Auto Snap Gizmo Functions
void AutoSnapGizmoSetTriGetter(AutoSnapTriGetter triF);
void AutoSnapGizmoSetTerrainF(AutoSnapPointGetter terrainF);
void AutoSnapGizmoSetNullF(AutoSnapNullGetter nullF);
void AutoSnapGizmoLockSnapTarget(AutoSnapGizmo *gizmo);
void AutoSnapGizmoUpdate(AutoSnapGizmo *gizmo, EditSpecialSnapMode specSnap);
void AutoSnapGizmoGetMat(AutoSnapGizmo *gizmo, Mat4 mat, bool snapNormal, int snapNormalAxis, bool snapNormalInverse, bool specSnapRestrict);

// Translate Gizmo Functions
TranslateGizmo *TranslateGizmoCreate(void);
void TranslateGizmoDestroy(TranslateGizmo* transGizmo);

void TranslateGizmoUpdate(TranslateGizmo* transGizmo);
void TranslateGizmoDraw(TranslateGizmo* transGizmo);

void TranslateGizmoSetCallbackContext(TranslateGizmo* transGizmo, void *context);
void TranslateGizmoSetActivateCallback(TranslateGizmo* transGizmo, GizmoActivateFn activate_fn);
void TranslateGizmoSetDeactivateCallback(TranslateGizmo* transGizmo, GizmoDeactivateFn deactivate_fn);
EditLibGizmosToolbar *TranslateGizmoGetToolbar(TranslateGizmo *transGizmo);
void TranslateGizmoSetToolbar(TranslateGizmo *transGizmo, EditLibGizmosToolbar *toolbar);
void TranslateGizmoSetTriGetter(TranslateGizmo *transGizmo, AutoSnapTriGetter triF);
void TranslateGizmoSetTerrainF(TranslateGizmo *transGizmo, AutoSnapPointGetter terrainF);
void TranslateGizmoSetMatrix(TranslateGizmo* transGizmo, const Mat4 startMat);
void TranslateGizmoGetMatrix(TranslateGizmo* transGizmo, Mat4 mat);
void TranslateGizmoSetAlignedToWorld(TranslateGizmo *transGizmo, bool aligned);
bool TranslateGizmoGetAlignedToWorld(TranslateGizmo *transGizmo);
void TranslateGizmoSetSpecSnap(TranslateGizmo *transGizmo, EditSpecialSnapMode mode);
void TranslateGizmoSetHideGrid(TranslateGizmo *transGizmo, bool hide);
EditSpecialSnapMode TranslateGizmoGetSpecSnap(TranslateGizmo *transGizmo);
EditSpecialSnapMode TranslateGizmoGetLastSnap(TranslateGizmo *transGizmo);
void TranslateGizmoSetSnapClamp(TranslateGizmo *transGizmo, bool clamp);
bool TranslateGizmoGetSnapClamp(TranslateGizmo *transGizmo);
void TranslateGizmoSetSnapNormal(TranslateGizmo *transGizmo, bool snap);
void TranslateGizmoSetSnapNormalInverse(TranslateGizmo *transGizmo, bool snapInverse);
void TranslateGizmoSetSnapNormalAxis(TranslateGizmo *transGizmo, int axis);
bool TranslateGizmoGetSnapNormal(TranslateGizmo *transGizmo);
bool TranslateGizmoGetSnapNormalInverse(TranslateGizmo *transGizmo);
int TranslateGizmoGetSnapNormalAxis(TranslateGizmo *transGizmo);
void TranslateGizmoSetSnapResolution(TranslateGizmo *transGizmo, int res);
int TranslateGizmoGetSnapResolution(TranslateGizmo *transGizmo);
bool TranslateGizmoIsSnapEnabled(TranslateGizmo* transGizmo);
bool TranslateGizmoIsDragEnabled(TranslateGizmo* transGizmo);
void TranslateGizmoToggleRelativeDrag(TranslateGizmo* transGizmo);
void TranslateGizmoSetRelativeDrag(TranslateGizmo* transGizmo, bool relDrag);
void TranslateGizmoSetAxes(TranslateGizmo* transGizmo, bool axisX, bool axisY, bool axisZ);
void TranslateGizmoToggleAxes(TranslateGizmo* transGizmo, bool toggleX, bool toggleY, bool toggleZ);
void TranslateGizmoCycleAxes(TranslateGizmo* transGizmo);
void TranslateGizmoGetAxesEnabled(TranslateGizmo *transGizmo, bool *x, bool *y, bool *z);
bool TranslateGizmoIsSnapAlongAxes(TranslateGizmo* transGizmo);
void TranslateGizmoDisableAxes(TranslateGizmo* transGizmo);
bool TranslateGizmoIsActive(TranslateGizmo* transGizmo);

// Inert Axes Gizmo 
void InertAxesGizmoDraw( const Mat4 mat, float fAxisLength );
void InertAxesGizmoDrawColored( const Mat4 mat, float fAxisLength, int color);

// Cone Gizmo Functions
void ConeGizmoDraw(ConeGizmo *gizmo, Mat4 mat, float radius, float halfAngle, Color c);
void ConeGizmoHandleInput(ConeGizmo *gizmo, float startAngle);
void ConeGizmoDrag(ConeGizmo *gizmo, float *halfAngle);

void ConeGizmoActivate(ConeGizmo *gizmo, bool active);
bool ConeGizmoGetActive(ConeGizmo *gizmo);
bool ConeGizmoIsSnapEnabled(ConeGizmo *gizmo);
void ConeGizmoEnableSnap(ConeGizmo *gizmo, bool enabled);
int ConeGizmoGetSnapResolution(ConeGizmo *gizmo);
void ConeGizmoSetSnapResolution(ConeGizmo *gizmo, int res);

// Pyramid Gizmo Functions
void PyramidGizmoDraw(PyramidGizmo *gizmo, Mat4 mat, float radius, float halfAngle1, float halfAngle2, Color c);
void PyramidGizmoHandleInput(PyramidGizmo *gizmo, float startAngle1, float startAngle2);
void PyramidGizmoDrag(PyramidGizmo *gizmo, float *halfAngle1, float *halfAngle2);

void PyramidGizmoActivate(PyramidGizmo *gizmo, bool active);
bool PyramidGizmoGetActive(PyramidGizmo *gizmo);
bool PyramidGizmoIsSnapEnabled(PyramidGizmo *gizmo);
void PyramidGizmoEnableSnap(PyramidGizmo *gizmo, bool enabled);
int PyramidGizmoGetSnapResolution(PyramidGizmo *gizmo);
void PyramidGizmoSetSnapResolution(PyramidGizmo *gizmo, int res);

// Radial Gizmo Functions
void RadialGizmoDraw(RadialGizmo *gizmo, Vec3 pos, float radius, Color color, bool drawSphere);
void RadialGizmoHandleInput(RadialGizmo *gizmo, Vec3 pos, float startRadius);
void RadialGizmoDrag(RadialGizmo *gizmo, Vec3 pos, float *radius);

void RadialGizmoActivate(RadialGizmo *gizmo, bool active);
bool RadialGizmoGetActive(RadialGizmo *gizmo);
void RadialGizmoSetRelativeDrag(RadialGizmo *gizmo, bool drag);
void RadialGizmoEnableSnap(RadialGizmo *gizmo, bool enabled);
int RadialGizmoGetSnapResolution(RadialGizmo *gizmo);
void RadialGizmoSetSnapResolution(RadialGizmo *gizmo, int res);

// Scale Min Max Functions
ScaleMinMaxGizmo *ScaleMinMaxGizmoCreate(void);
void ScaleMinMaxGizmoDestroy(ScaleMinMaxGizmo* scaleGizmo);

void ScaleMinMaxGizmoUpdate(ScaleMinMaxGizmo* scaleGizmo);
void ScaleMinMaxGizmoDraw(ScaleMinMaxGizmo* scaleGizmo);

void ScaleMinMaxGizmoSetMatrix(ScaleMinMaxGizmo* scaleGizmo, const Mat4 startMat);
void ScaleMinMaxGizmoSetMinMax(ScaleMinMaxGizmo* scaleGizmo, const Vec3 scaleMin, const Vec3 scaleMax);

void ScaleMinMaxGizmoSetCallbackContext(ScaleMinMaxGizmo* scaleGizmo, void *context);
void ScaleMinMaxGizmoSetActivateCallback(ScaleMinMaxGizmo* scaleGizmo, GizmoActivateScaleMinMaxFn activate_fn);
void ScaleMinMaxGizmoSetDeactivateCallback(ScaleMinMaxGizmo* scaleGizmo, GizmoActivateScaleMinMaxFn deactivate_fn);

void ScaleMinMaxGizmoSetSnapResolution(ScaleMinMaxGizmo *scaleGizmo, int res);
void ScaleMinMaxGizmoSetMirrored(ScaleMinMaxGizmo *scaleGizmo, bool mirrored);

// Useful utility function
int findSphereLineIntersection(Vec3 p1,Vec3 p2,Vec3 center,double radius,Vec3 intersection);

// toolbar gizmo
typedef enum ModelEditToolbarOptions
{
	MET_TIME		= 1<<0,
	MET_WIREFRAME	= 1<<1,
	MET_UNLIT		= 1<<2,
	MET_ORTHO		= 1<<3,
	MET_LIGHTING	= 1<<4,
	MET_SKIES		= 1<<5,
	MET_TINT		= 1<<6,
	MET_ALWAYS_ON_TOP = 1<<7,
	MET_GRID		= 1<<8,		//< currently assumed to always be on
								//< by MET.
	MET_CAMDIST     = 1<<9,		//< currently assumed to always be on
								//< by MET.
	MET_CAMRESET	= 1<<10,	//< currently assumed to always be on
								//< by MET.

	MET_ALL			= 0xffffffff,
} ModelEditToolbarOptions;

SA_RET_NN_VALID ModelOptionsToolbar *motCreateToolbar(ModelEditToolbarOptions options, SA_PARAM_NN_VALID GfxCameraController *camera, SA_PRE_NN_RELEMS(3) const Vec3 min, SA_PRE_NN_RELEMS(3) const Vec3 max, F32 radius, SA_PARAM_OP_STR const char *editor_pref_key);
void motFreeToolbar(SA_PRE_NN_VALID SA_POST_P_FREE ModelOptionsToolbar *toolbar);
void motUpdateAndDraw(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motLostFocus(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motGotFocus(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motSetObjectBounds(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar, SA_PRE_NN_RELEMS(3) const Vec3 min, SA_PRE_NN_RELEMS(3) const Vec3 max, F32 radius);
void motSetTime(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar, F32 time);
void motSetSkyFile(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar, SA_PARAM_OP_STR const char *sky_name);
int motGetWireframeSetting(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
bool motGetUnlitSetting(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motGetTintColor0(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar, SA_PRE_NN_ELEMS(3) Vec3 tint);
U8 motGetTintAlpha(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motGetTintColor1(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar, SA_PRE_NN_ELEMS(3) Vec3 tint);
SA_RET_NN_VALID MaterialNamedConstant **motGetNamedParams(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motSetAlwaysOnTop(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar, bool alwaysOnTop);
SA_ORET_NN_VALID EMToolbar *motGetToolbar(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
F32 motGetCamDist(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motApplyValues(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);
void motResetCamera(SA_PARAM_NN_VALID ModelOptionsToolbar *toolbar);

#endif // NO_EDITORS

#endif // __EDITLIBGIZMOS_H__
