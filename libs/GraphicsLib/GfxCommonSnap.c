//some common functions used by gfxMapSnap.c and gfxObjectsnap.c
//over time more of those might move into here. 

#include "GfxCommonSnap.h"
#include "GfxHeadshot.h"
#include "GfxMaterials.h"
#include "GfxTerrain.h"
#include "GraphicsLibPrivate.h"
#include "Materials.h"
#include "StashTable.h"
#include "dynWind.h"
#include "GlobalTypes.h"

//causes map-snap--hidden artist-tagged objects to not draw.
//use accessor function.
static bool g_bEnableHideInSnap = 0;

//causes outlines to be drawn.
//use accessor function.
static bool g_bOutlining = 0;


//used for material overrides
char g_strSimpleMaterials[256];
char g_strSimpleAlphaMaterials[256];

//for restoring previous state:
static bool g_bOldEnableHideInSnap = 0;
static bool g_bOldOutlining = 0;
static int g_iOldFeaturesAllowed = 0;
static int g_iOldFeaturesDesired = 0;
static bool g_bGfxSnapOptionsSaved = 0;

static char g_strOrig_simple_materials[64];
static char g_strOrig_simple_alpha_materials[64];

void gfxSnapApplyOptions(bool enableHideInSnap, bool drawOutlines, bool overrideMaterials){
	//save old options:
	if( ! g_bGfxSnapOptionsSaved){
		//this is in case this gets called twice before gfxSnapUndoOptions.
		g_bGfxSnapOptionsSaved = true;
		g_bOldEnableHideInSnap = gfxSnapGetHidingMapSnapObscuringObjects();
		g_bOldOutlining = g_bOutlining;
		g_iOldFeaturesAllowed = gfx_state.features_allowed;
		g_iOldFeaturesDesired = gfx_state.settings.features_desired;
		strcpy(g_strOrig_simple_materials, gfx_state.debug.simpleMaterials);
		strcpy(g_strOrig_simple_alpha_materials, gfx_state.debug.simpleAlphaMaterials);
	}
	//map snap hidden:
	g_bEnableHideInSnap = enableHideInSnap;
	//outlines:
	if (drawOutlines){
		gfx_state.features_allowed |= GFEATURE_OUTLINING;
		gfx_state.settings.features_desired |= GFEATURE_OUTLINING;
		g_bOutlining = true;
	}else{
		gfx_state.features_allowed &= ~GFEATURE_OUTLINING;
		gfx_state.settings.features_desired &= ~GFEATURE_OUTLINING;
		g_bOutlining = false;
	}

	//material overrides:
	if (overrideMaterials){
		strcpy(gfx_state.debug.simpleMaterials, g_strSimpleMaterials);
		strcpy(gfx_state.debug.simpleAlphaMaterials, g_strSimpleAlphaMaterials);
	}else{
		strcpy(gfx_state.debug.simpleMaterials, g_strOrig_simple_materials);
		strcpy(gfx_state.debug.simpleAlphaMaterials, g_strOrig_simple_alpha_materials);
	}
}

void gfxSnapUndoOptions(){

	static bool bDontClear = false;
	g_bEnableHideInSnap = g_bOldEnableHideInSnap;
	g_bOutlining = g_bOldOutlining;
	gfx_state.features_allowed = g_iOldFeaturesAllowed;
	gfx_state.settings.features_desired = g_iOldFeaturesDesired;
	
	strcpy(gfx_state.debug.simpleMaterials, g_strOrig_simple_materials);
	strcpy(gfx_state.debug.simpleAlphaMaterials, g_strOrig_simple_alpha_materials);

	// clear any overrides that the app-specific setup introduced for map-snaps
	if (!bDontClear && material_load_info.stTemplateOverrides)
	{
		stashTableDestroy(material_load_info.stTemplateOverrides);
		material_load_info.stTemplateOverrides = NULL;
		stashTableDestroy(g_stTextureOpOverride);
		g_stTextureOpOverride = NULL;
		gfxMaterialsReloadAll();
	}
	g_pchColorTexOverride = NULL;
	g_bGfxSnapOptionsSaved = 0;
}

bool gfxSnapGetOutliningEnabled(){
	return g_bOutlining;
}

bool gfxSnapGetHidingMapSnapObscuringObjects(){
	return g_bEnableHideInSnap;
}

void gfxSnapOverrideState( GfxSnapSavedState* origState )
{
	// MJF TODO: There has got to be a better way than this to access
	// disable_terrain_collision.
	extern bool disable_terrain_collision;
	
	origState->maxInactiveFps			 = gfx_state.settings.maxInactiveFps;
	origState->noFadeIn					 = gfx_state.debug.no_fade_in;
	origState->overrideTime				 = gfx_state.debug.overrideTime;
	origState->clientLoopTimer			 = gfx_state.client_loop_timer;
	origState->disableIncrementalTex	 = gfx_state.debug.disableIncrementalTex;
	origState->dynWindEnabled			 = dynWindGetEnabled();
	origState->drawHighDetail			 = gfx_state.settings.draw_high_detail;
	origState->drawHighFillDetail		 = gfx_state.settings.draw_high_fill_detail;
	origState->disableTerrainCollision	 = disable_terrain_collision;
	origState->headshotDoBadImageChecks	 = gfxHeadshotDoBadImageChecks;
	origState->headshotFramesToWait		 = gfxHeadshotFramesToWait;
	origState->rdrDbgTypeFlags			 = rdr_state.dbg_type_flags;
	origState->gfxHeadshotDisableValidateSize = gfxHeadshotDisableValidateSize;

	gfx_state.settings.maxInactiveFps		   = 0;
	gfx_state.debug.no_fade_in				   = true;
	gfx_state.debug.overrideTime			   = 12;
	gfx_state.client_loop_timer				   = 0;
	gfx_state.debug.disableIncrementalTex	   = true;
	dynWindSetEnabled( false );
	gfx_state.settings.draw_high_detail		   = false;
	gfx_state.settings.draw_high_fill_detail   = false;
	disable_terrain_collision				   = true;
	gfxHeadshotDoBadImageChecks				   = true;
	gfxHeadshotFramesToWait					   = 4;
	rdr_state.dbg_type_flags 				  &= ~RDRTYPE_PARTICLE;
	gfxHeadshotDisableValidateSize			   = true;
}

void gfxSnapRestoreState( const GfxSnapSavedState* origState )
{
	// MJF TODO: There has got to be a better way than this to access
	// disable_terrain_collision.
	extern bool disable_terrain_collision;

	gfx_state.settings.maxInactiveFps 		  = origState->maxInactiveFps;
	gfx_state.debug.no_fade_in				  = origState->noFadeIn;
	gfx_state.debug.overrideTime			  = origState->overrideTime;
	gfx_state.client_loop_timer				  = origState->clientLoopTimer;
	gfx_state.debug.disableIncrementalTex	  = origState->disableIncrementalTex;
	dynWindSetEnabled( origState->dynWindEnabled );
	gfx_state.settings.draw_high_detail		  = origState->drawHighDetail;
	gfx_state.settings.draw_high_fill_detail  = origState->drawHighFillDetail;
	disable_terrain_collision				  = origState->disableTerrainCollision;
	gfxHeadshotDoBadImageChecks				  = origState->headshotDoBadImageChecks;
	gfxHeadshotFramesToWait					  = origState->headshotFramesToWait;
	rdr_state.dbg_type_flags				  = origState->rdrDbgTypeFlags;
	gfxHeadshotDisableValidateSize			  = origState->gfxHeadshotDisableValidateSize;
}

