#pragma once

#include"RdrState.h"

//some common functions used by gfxMapSnap.c and gfxObjectsnap.c
//over time more of those might move into here. 

//setup all the options requested by game & process specific setup functions:
//@param hideMapSnapObsurring: if true, don't draw objects that have been tagged map_snap_hidden
//@param drawOutlines: forces allow outlining.  Also requires outlining in skyfile.
//@param overrideMaterials: does a generic material override.  App specific material overrides 
//			should be setup in OVERRIDE_LATELINK_gfxMapSnapSetupOptions, which should call this.
//			example:
//			stashAddPointer(material_load_info.stTemplateOverrides,"Terrain_1Detail",
//							materialGetTemplateByName("Mapsnap_Terrain_1Detail"),false);
//after snapping is done, call gfxSnapUndoOptions() to restore old options. 
void gfxSnapApplyOptions(bool hideMapSnapObscuring, bool drawOutlines, bool overrideMaterials);


//restores settings to where they were before gfxSnapApplyOptions()
void gfxSnapUndoOptions();

//@return: whether outlines are allowed.  They also must be setup in skyfiles.
bool gfxSnapGetOutliningEnabled();

//@return: whether to draw objects artists have tagged as "map-snap-hidden"
bool gfxSnapGetHidingMapSnapObscuringObjects();

extern char g_strSimpleMaterials[256];
extern char g_strSimpleAlphaMaterials[256];

/// Structure to hold all state flags used
typedef struct GfxSnapSavedState {
	float maxInactiveFps;
	bool noFadeIn;
	float overrideTime;
	float clientLoopTimer;
	bool disableIncrementalTex;
	bool dynWindEnabled;
	bool drawHighDetail;
	bool drawHighFillDetail;
	bool disableTerrainCollision;
	int headshotFramesToWait;
	bool headshotDoBadImageChecks;
	RdrDebugTypeFlags rdrDbgTypeFlags;
	bool disableTexReduceAutoScale;
	bool gfxHeadshotDisableValidateSize;
} GfxSnapSavedState;

void gfxSnapOverrideState( GfxSnapSavedState* origState );
void gfxSnapRestoreState( const GfxSnapSavedState* origState );



