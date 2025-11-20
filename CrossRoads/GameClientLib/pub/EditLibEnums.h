#pragma once

typedef enum
{
	EditTranslateGizmo = 0,
	EditRotateGizmo,
	EditScaleMin,
	EditScale,
	EditMinCone,
	EditMaxCone,
	EDITTRANSFORMMODECOUNT,
} EditTransformMode;

typedef enum
{
	EditTracker,
	EditPointLight,
	EditSpotLight,
	EditProjectorLight,
} EditTrackerType;
