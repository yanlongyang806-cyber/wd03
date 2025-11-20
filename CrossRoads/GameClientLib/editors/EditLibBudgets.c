#include "EditLibBudgets.h"
#include "UISprite.h"
#include "UIAutoWidget.h"
#include "GfxTextureTools.h"
#include "GfxDebug.h"
#include "GfxMaterials.h"
#include "GfxClipper.h"
#include "GfxSpriteText.h"
#include "GraphicsLib.h"
#include "sndMemory.h"
#include "WorldLib.h"
#include "dynCloth.h"
#include "Color.h"
#include "StringCache.h"
#include "UnitSpec.h"
#include "file.h"
#include "Prefs.h"
#include "MemoryMonitor.h"
#include "RdrState.h"
#include "gimmeDLLWrapper.h"
#include "MemReport.h"
#include "wlPerf.h"
 
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

#define WARN_CUTOFF 0.85f
#define FATAL_CUTOFF (squishy?1.0f:1.0f)
#define UBER_FATAL_CUTOFF 1.5f

#define NO_BUDGET ((size_t)-1)
#define OBJECT_SCENE_BUDGET 900
#define OBJECT_WORLD_BUDGET 400
#define OBJECT_FX_BUDGET 150
#define OBJECT_CHARACTER_BUDGET 550
#define OBJECT_TOTAL_BUDGET NO_BUDGET
#define WORLD_ANIMATION_BUDGET 100
#define TRIANGLE_BUDGET NO_BUDGET
#define SHADER_GRAPH_BUDGET NO_BUDGET
#define MATERIAL_BUDGET NO_BUDGET
#define SPAWN_BUDGET NO_BUDGET
//#define FSM_BUDGET 1666
#define FSM_BUDGET NO_BUDGET
#define TRIANGLE_MS_BUDGET (budget_data.cur_videmode_is_old?30.f:10.f)
#define TRIANGLE_DETAIL_MS_BUDGET 10.f
#define TRIANGLE_DETAIL_MS_SCALE 0.35f // Rough scale between reported 7800 numbers and expected 8800 speeds
#define TOLERANCE_PERF 2.5f
#define CLOTH_BUDGET 100000

#define CPU_BUDGET_US 33333  // 30 fps
#define MAX_CPU_US 60000

typedef enum DoubleTextType
{
	DoubleText_None,
	DoubleText_InView,
	DoubleText_WorkingSet,
} DoubleTextType;

AUTO_ENUM;
typedef enum BudgetsCategory
{
	BudgetsCategory_World=1<<0,
	BudgetsCategory_Character=1<<1,
	BudgetsCategory_Animation=1<<2,
	BudgetsCategory_FX=1<<3,
	BudgetsCategory_UI=1<<4,
	BudgetsCategory_Design=1<<5,
} BudgetsCategory;
#define BudgetsCategory_All (~0)

typedef enum EBarType
{
	EBarType_Single,
	EBarType_Notch,
	EBarType_Double
} EBarType;

typedef struct BudgetBar
{
	const char *module;
	UISprite *sprite;
	UISprite *sprite_recent;
	UISprite *sprite_recent_bar;
	UILabel *label_text;
	UILabel *label_text_recent;
	DoubleTextType doubleText;

	struct {
		bool updatedOnce;
		F32 recent;
		F32 total;
		F32 budget;
		F32 trafficDelta;
	} last;
} BudgetBar;

typedef struct BudgetWindow
{
	UIWindow *window;
	UIList *list;
	const char *module;
	void **entry_list;
	bool entry_list_needs_freeing;
	bool is_scene_objs;
	ParseTable *table;
	WLUsageFlags flags_to_list;
} BudgetWindow;

AUTO_STRUCT;
typedef struct BudgetData
{
AST_STOP
	UIWindow *window;
	UIRebuildableTree *uirt;
	struct {
		BudgetBar cpu_us;
		BudgetBar cpu_dual_core_effective_us;
		BudgetBar ui_us;
		BudgetBar anim_us;
		BudgetBar cloth_us;
		BudgetBar skel_us;
		BudgetBar fx_us;
		BudgetBar sound_us;
		BudgetBar physics_us;
		BudgetBar draw_us;
		BudgetBar worldtrav_us;
		BudgetBar misc_cpu_us;
		BudgetBar gpu_us;
		BudgetBar gpu_shadows_us;
		BudgetBar gpu_zprepass_us;
		BudgetBar gpu_opaque_onepass_us;
		BudgetBar gpu_shadow_buffer_us;
		BudgetBar gpu_opaque_us;
		BudgetBar gpu_alpha_us;
		BudgetBar gpu_postprocess_us;
		BudgetBar gpu_2d_us;
		BudgetBar gpu_other_us;
		BudgetBar object_scene_count;
		BudgetBar object_world_count;
		BudgetBar object_character_count;
		BudgetBar object_fx_count;
		BudgetBar object_total_count;
		BudgetBar world_animation_count;
		BudgetBar spawn_total_count;
		BudgetBar running_encounters;
		BudgetBar spawned_fsm_cost;
		BudgetBar potential_fsm_cost;
		BudgetBar tri_ms;
		BudgetBar template_count;
		BudgetBar material_count;
		BudgetBar audio;
		BudgetBar cloth_total;
		BudgetBar cloth_collisions;
		BudgetBar cloth_constraints;
		BudgetBar cloth_particles;
		struct {
			BudgetBar total;
			BudgetBar totalOrig;
		} textures;
		struct {
			BudgetBar total;
		} geometry;
	} bar;

	BudgetBar **bars_dynamic;
	BudgetBar **bars_static;

	UIStyleFont *labelFont;

	BudgetWindow **subwindows;

	TexMemUsage tex_usage;
	GeoMemUsage geo_usage;
	U32 geo_high_detail_adjust;
	WLUsageFlags total_art_flags;

	int perf_over_timer;
	int mem_over_timer;
	bool was_perf_over;
	bool was_mem_over;

	bool art_over_mem_full;
	bool art_over_performance;
	bool software_over_mem;

	bool was_under_while_closed;
	bool performance_was_under_while_closed;

	bool force_hide;
	bool cur_videmode_is_old;

	UISkin *detailsSkin;
	UIWindow *whoUsesTextureWindow;
	GeoMemUsageEntry **whoUsesTextureList;
	const char *whoUsesTextureName;
	bool whoUsesTextureCurrentSceneOnly;

AST_START
	F32 alpha; AST( NAME(Opacity) )
	F32 scale; AST( NAME(Scale) )
	BudgetsCategory alert_category; AST( NAME(AlertOn) FLAGS )
} BudgetData;

BudgetData budget_data;

#include "AutoGen/EditLibBudgets_c_ast.c"

void editLibBudgetsUpdate(void);
static void editLibBudgetsUpdateSubWindows(void);
static void editLibBudgetsUpdateArt(void);
void editLibBudgetsShow(void);
static void updateShowObjectsUsingTexture(void);


static UIButton *budgetButton = NULL;


static struct {
	WLUsageFlagsBitIndex index;
	const char *module;
	BudgetsCategory alert_category;
} tex_budget_mapping[] = {
	{WL_FOR_WORLD_BITINDEX, "Textures:World", BudgetsCategory_World},
	{WL_FOR_TERRAIN_BITINDEX, "Textures:Terrain", BudgetsCategory_World},
	{WL_FOR_ENTITY_BITINDEX, "Textures:Character", BudgetsCategory_Character},
	{WL_FOR_FX_BITINDEX, "Textures:FX", BudgetsCategory_FX},
	{WL_FOR_UI_BITINDEX, "Textures:UI", BudgetsCategory_UI},
	{WL_FOR_FONTS_BITINDEX, "Textures:Fonts", BudgetsCategory_UI},
};

static struct {
	WLUsageFlagsBitIndex index;
	const char *module;
	BudgetsCategory alert_category;
} geo_budget_mapping[] = {
	{WL_FOR_WORLD_BITINDEX, "Geometry:World", BudgetsCategory_World},
	{WL_FOR_TERRAIN_BITINDEX, "Geometry:Terrain", BudgetsCategory_World},
	{WL_FOR_ENTITY_BITINDEX, "Geometry:Character", BudgetsCategory_Character},
	{WL_FOR_FX_BITINDEX, "Geometry:FX", BudgetsCategory_FX},
	{WL_FOR_UI_BITINDEX, "Geometry:UI", BudgetsCategory_UI},
	{WL_FOR_FONTS_BITINDEX, "Geometry:Fonts", BudgetsCategory_UI},
};

static BudgetBar *getDynamicBudgetBar(const char *module)
{
	FOR_EACH_IN_EARRAY(budget_data.bars_dynamic, BudgetBar, bar)
		if (bar->module == module)
			return bar;
	FOR_EACH_END;
	return NULL;
}

static BudgetBar *getStaticBudgetBar(const char *module)
{
	FOR_EACH_IN_EARRAY(budget_data.bars_static, BudgetBar, bar)
		if (bar->module == module)
			return bar;
	FOR_EACH_END;
	return NULL;
}

typedef struct EncounterBudgets
{
	U32 numEncounters;
	U32 numRunningEncs;
	U32 numSpawnedEnts;
	U32 spawnedFSMCost;
	U32 potentialFSMCost;
} EncounterBudgets;

static EncounterBudgets encBudgetValues;

// Foces the budgets window not to open
AUTO_CMD_INT(budget_data.force_hide,ForceHideBudget) ACMD_CMDLINE ACMD_CATEGORY(Debug);

void editLibBudgetsStartup(void)
{
	budget_data.total_art_flags = WL_FOR_FX | WL_FOR_ENTITY | WL_FOR_WORLD | WL_FOR_UI | WL_FOR_UTIL | WL_FOR_NOTSURE | WL_FOR_TERRAIN | WL_FOR_PREVIEW_INTERNAL | WL_FOR_FONTS;
	budget_data.scale = GamePrefGetFloat("Budgets_Scale", 0.875f);
	budget_data.alpha = GamePrefGetFloat("Budgets_Opacity", 0.85f);
	budget_data.alert_category = GamePrefGetInt("Budgets_AlertOn", 0);
	if (!budget_data.alert_category)
	{
		// This should, by default, set any categories that might be valid (they can always turn them off).
		// e.g. the UI designers don't have their own group, so all of Design gets UI enabled.
		if (UserIsInGroup("World"))
			budget_data.alert_category |= BudgetsCategory_World;
		if (UserIsInGroup("FX"))
			budget_data.alert_category |= BudgetsCategory_FX;
		if (UserIsInGroup("Character"))
			budget_data.alert_category |= BudgetsCategory_Character;
		if (UserIsInGroup("Animation"))
			budget_data.alert_category |= BudgetsCategory_Animation;
		if (UserIsInGroup("Design"))
			budget_data.alert_category |= BudgetsCategory_Design|BudgetsCategory_UI;
		if (!budget_data.alert_category && UserIsInGroup("Art"))
			budget_data.alert_category |= BudgetsCategory_World|BudgetsCategory_Character|BudgetsCategory_FX|BudgetsCategory_Animation;
		if (!budget_data.alert_category && UserIsInGroup("Software"))
			budget_data.alert_category |= BudgetsCategory_All;
		if (!budget_data.alert_category)
			budget_data.alert_category |= BudgetsCategory_All;
	}
}

void editLibBudgetsReset(void)
{
	budget_data.performance_was_under_while_closed = 1;
	budget_data.was_under_while_closed = 1;
	budget_data.was_perf_over = false;
	budget_data.was_mem_over = false;
	if (budget_data.window && ui_WindowIsVisible(budget_data.window)) {
		ui_WindowHide(budget_data.window);
	}
}

MemoryBudget *getSubBudget(MemoryBudget *budget, const char *name)
{
	assert(eaSize(&budget->subBudgets));
	FOR_EACH_IN_EARRAY(budget->subBudgets, MemoryBudget, subbudget)
	{
		if (stricmp(subbudget->module, name)==0) {
			return subbudget;
		}
	}
	FOR_EACH_END;
	return NULL;
}

bool editLibBudgetsShouldShow(void)
{
	bool squishy = true;
	bool ret=false;
	bool perfOver = false;
	bool perfOverShouldShow = false;
	bool memOver = false;
	bool memOverShouldShow = false;
	int i;
	MemoryBudget *budget_texture, *budget_geometry, *budget_temp;

	gfxGetFrameCountsForModification()->over_budget_mem = 0;
	gfxGetFrameCountsForModification()->over_budget_perf = 0;

#if PLATFORM_CONSOLE
	return false;
#endif

	if (!showDevUI()) {
		budget_data.was_mem_over = false;
		budget_data.was_perf_over = false;
		return false;
	}

	// Note: intentionally running this even if alert_category is 0, otherwise
 	//  profiling on software computers will totally miss this function, which we
	//  need to run on Art computers.

	editLibBudgetsUpdateArt();
	// Specifics for Textures/Geometry
	budget_texture = memBudgetGetBudgetByName(BUDGET_Textures_Art);
	budget_geometry = memBudgetGetBudgetByName(BUDGET_Geometry_Art);
	assert(budget_texture);
	assert(budget_geometry);


	// Specifics for Textures/Geometry
	//////////////////////////////////////////////////////////////////////////
	// Textures
	FOR_EACH_IN_EARRAY(budget_texture->subBudgets, MemoryBudget, subbudget)
	{
		for (i=0; i<ARRAY_SIZE(tex_budget_mapping); i++)
		{
			if (stricmp(subbudget->module, tex_budget_mapping[i].module)==0)
			{
				if (tex_budget_mapping[i].index != WL_FOR_TERRAIN_BITINDEX)
				{
					// Terrain is combined with world and checked elsewhere
					if (budget_data.tex_usage.data.video.loaded[tex_budget_mapping[i].index]
						> FATAL_CUTOFF * subbudget->allowed)
					{
						if (tex_budget_mapping[i].alert_category & budget_data.alert_category)
							memOverShouldShow = true;
						memOver = true;
					}
				}
			}
		}
	}
	FOR_EACH_END;

	//////////////////////////////////////////////////////////////////////////
	// Geometry
	FOR_EACH_IN_EARRAY(budget_geometry->subBudgets, MemoryBudget, subbudget)
	{
		for (i=0; i<ARRAY_SIZE(geo_budget_mapping); i++)
		{
			if (stricmp(subbudget->module, geo_budget_mapping[i].module)==0)
			{
				if (geo_budget_mapping[i].index != WL_FOR_WORLD_BITINDEX &&
					geo_budget_mapping[i].index != WL_FOR_TERRAIN_BITINDEX)
				{
					// World and terrain are combined and checked elsewhere
					if (budget_data.geo_usage.loadedVideo[geo_budget_mapping[i].index]
						//+ budget_data.geo_usage.loadedSystem[geo_budget_mapping[i].index]
						> FATAL_CUTOFF * subbudget->allowed)
					{
						if (geo_budget_mapping[i].alert_category & budget_data.alert_category)
							memOverShouldShow = true;
						memOver = true;
					}
				}
			}
		}
	}
	FOR_EACH_END;


	//////////////////////////////////////////////////////////////////////////
	// Summed world geometry + terrain for world budget
	{
		size_t allowed;
		U32 loadedTexture = 0; //budget_data.tex_usage.data.video.loaded[WL_FOR_WORLD_BITINDEX];
		U32 loadedVideo = budget_data.geo_usage.loadedVideo[WL_FOR_WORLD_BITINDEX];
		U32 loadedSystem = budget_data.geo_usage.loadedSystem[WL_FOR_WORLD_BITINDEX];
		U32 recentGeo = budget_data.geo_usage.recentVideo[WL_FOR_WORLD_BITINDEX];
		loadedTexture +=  budget_data.tex_usage.data.video.loaded[WL_FOR_TERRAIN_BITINDEX];
		loadedVideo += budget_data.geo_usage.loadedVideo[WL_FOR_TERRAIN_BITINDEX];
		loadedSystem += budget_data.geo_usage.loadedSystem[WL_FOR_TERRAIN_BITINDEX];

		budget_temp = getSubBudget(budget_geometry, "Geometry:World");
		allowed = budget_temp->allowed;
		budget_temp = getSubBudget(budget_geometry, "Geometry:Terrain");
		allowed += budget_temp->allowed;
		budget_temp = getSubBudget(budget_texture, "Textures:Terrain");
		allowed += budget_temp->allowed;

		// Not counting system memory anymore
		// Subtracting high detail overcount from world budget
		if (loadedTexture + loadedVideo - budget_data.geo_high_detail_adjust > allowed)
		{
			if (budget_data.alert_category & BudgetsCategory_World)
				memOverShouldShow = true;
			memOver = true;
		}
	}

	{
		FrameCounts counts;
		RdrDrawListPassStats *visual_pass_stats;
		F32 vs_ms;
		gfxGetFrameCounts(&counts);
		visual_pass_stats = &counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL];
#define PERF_OVER(cat) { perfOver = true; if (budget_data.alert_category & (cat)) perfOverShouldShow = true; }
		if (counts.objects_in_scene > OBJECT_SCENE_BUDGET)
			PERF_OVER(BudgetsCategory_World|BudgetsCategory_Character);
		if (visual_pass_stats->objects_drawn[ROC_WORLD] + visual_pass_stats->objects_drawn[ROC_WORLD_HIGH_DETAIL] + visual_pass_stats->objects_drawn[ROC_TERRAIN] + visual_pass_stats->objects_drawn[ROC_SKY] > OBJECT_WORLD_BUDGET)
			PERF_OVER(BudgetsCategory_World);
		if (visual_pass_stats->objects_drawn[ROC_FX] > OBJECT_FX_BUDGET)
			PERF_OVER(BudgetsCategory_World|BudgetsCategory_FX);
		if (visual_pass_stats->objects_drawn[ROC_CHARACTER] > OBJECT_CHARACTER_BUDGET)
			PERF_OVER(BudgetsCategory_Character);
		if (counts.opaque_objects_drawn + counts.alpha_objects_drawn > OBJECT_TOTAL_BUDGET)
			PERF_OVER(BudgetsCategory_World|BudgetsCategory_Character);
		if (counts.world_animation_updates > WORLD_ANIMATION_BUDGET)
			PERF_OVER(BudgetsCategory_Animation);
		if (counts.triangles_in_scene > TRIANGLE_BUDGET)
			PERF_OVER(BudgetsCategory_World|BudgetsCategory_Character);
		vs_ms = gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, false, budget_data.cur_videmode_is_old);
		if (vs_ms > TRIANGLE_MS_BUDGET)
			PERF_OVER(BudgetsCategory_World|BudgetsCategory_Character);
		vs_ms = gfxGetApproxPassVertexShaderTime(visual_pass_stats, NULL, true, false) * TRIANGLE_DETAIL_MS_SCALE;
		if (vs_ms > TRIANGLE_DETAIL_MS_BUDGET)
			PERF_OVER(BudgetsCategory_World);
	}

	if (perfOver) {
		if (budget_data.was_perf_over) {
			if (timerElapsed(budget_data.perf_over_timer) > TOLERANCE_PERF) {
				ret = perfOverShouldShow;
			} else {
				// Nothing
			}
		} else {
			budget_data.was_perf_over = true;
			if (!budget_data.perf_over_timer) {
				budget_data.perf_over_timer = timerAlloc();
			}
			timerStart(budget_data.perf_over_timer);
		}
	} else {
		budget_data.was_perf_over = false;
	}
	if (memOver) {
		if (budget_data.was_mem_over) {
			if (timerElapsed(budget_data.mem_over_timer) > TOLERANCE_PERF) {
				ret = memOverShouldShow;
			} else {
				// Nothing
			}
		} else {
			budget_data.was_mem_over = true;
			if (!budget_data.mem_over_timer) {
				budget_data.mem_over_timer = timerAlloc();
			}
			timerStart(budget_data.mem_over_timer);
		}
	} else {
		budget_data.was_mem_over = false;
	}

	gfxGetFrameCountsForModification()->over_budget_mem = budget_data.was_mem_over;
	gfxGetFrameCountsForModification()->over_budget_perf = budget_data.was_perf_over;

	if (budget_data.force_hide || !gimmeDLLQueryExists()) {
		return false;
	}

	return ret;
}

void editLibBudgetsOncePerFrame(void)
{
	if (budget_data.whoUsesTextureWindow && ui_WindowIsVisible(budget_data.whoUsesTextureWindow)) {
		updateShowObjectsUsingTexture();
	}

	if (budget_data.window && ui_WindowIsVisible(budget_data.window)) {
		PERFINFO_AUTO_START("editLibBudgetsUpdate", 1);
		editLibBudgetsUpdate();
		PERFINFO_AUTO_STOP();
	} else {
		bool bShouldShow;
		PERFINFO_AUTO_START("editLibBudgetsShouldShow", 1);
		if ((bShouldShow=editLibBudgetsShouldShow()) || budget_data.was_perf_over || budget_data.was_mem_over) {
			if (bShouldShow && (!budget_data.window || budget_data.was_under_while_closed))
				editLibBudgetsShow();
			else {
				if (!budget_data.force_hide)
				{
					if (budget_data.was_perf_over)
						gfxDebugPrintfQueue("WARNING: Over performance budget limits.  May not run at desired framerate.");
					if (budget_data.was_mem_over)
						gfxDebugPrintfQueue("WARNING: Over memory budget limits.");
#if !_PS3
					gfxDebugPrintfQueue("  Run the /budgets command to open");
#endif
				}
			}
		}
		if (bShouldShow) {
			budget_data.was_under_while_closed = false;
		} else {
			budget_data.was_under_while_closed = true;
		}
		// Even if main window is closed, still update the subwindows!
		PERFINFO_AUTO_STOP_START("editLibBudgetsUpdateSubWindows", 1);
		editLibBudgetsUpdateSubWindows();
		PERFINFO_AUTO_STOP();
	}
}

static Color colorFromRatio(F32 ratio, bool squishy)
{
	Color ret;
	ret.b = 0;
	ret.a = 255;
	if (ratio < WARN_CUTOFF) {
		F32 r = ratio / WARN_CUTOFF;
		ret.r = 0;
		ret.g = r*127+128;
	} else if (ratio < FATAL_CUTOFF) {
		F32 r = (ratio - WARN_CUTOFF) / (FATAL_CUTOFF - WARN_CUTOFF);
		ret.r = r * 255;
		ret.g = (1-r) * 255;
	} else if (ratio > UBER_FATAL_CUTOFF) {
		F32 r = gfxGetClientLoopTimer();
		r *= 2;
		r -= (int)r;
		r *= 2;
		if (r > 1)
			r = 2 - r;
		ret.r = 128 + 127*r;
		ret.g = 15;
		ret.b = 15;
	} else { // ratio > FATAL_CUTOFF
		F32 r = gfxGetClientLoopTimer();
		r *=2;
		r -=(int)r;
		r *=2;
		if (r > 1)
			r = 2 - r;
		ret.r = 128 + 90*r;
		ret.g = 0;
	}
	return ret;
}

static bool updateBar(BudgetBar *bar, size_t recent, size_t total, size_t budget, size_t trafficDelta, bool squishy, const UnitSpec *spec)
{
	bool ret=false;
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	{
		char buf[64];
		char buf1[64], buf2[64];
		F32 ratio;
		F32 recentRatio;
		if (budget == 0)
		{
			ratio = recentRatio = 0;
		} else {
			ratio = total / (F32)budget;
			recentRatio = recent / (F32)budget;
			MIN1F(recentRatio, 1.0f);
		}
		if (budget == 0 && total == 0)
		{
			ratio = 0;
		}
		if (budget == 0 && recent == 0)
		{
			recentRatio = 0;
		}
		if (bar) {
			bar->sprite->tint = colorFromRatio(ratio, squishy);
			bar->sprite->widget.width = MIN(ratio, 1);
			if (bar->sprite_recent) {
				bar->sprite_recent->tint = colorFromRatio(recentRatio, squishy);
				bar->sprite_recent->widget.width = recentRatio - 0.01f;
			}
			if (bar->sprite_recent_bar) {
				bar->sprite_recent_bar->widget.xPOffset = recentRatio;
				//bar->sprite_recent_bar->widget.width = 0.01f;
			}
			if (recent != bar->last.recent ||
				total != bar->last.total ||
				budget != bar->last.budget ||
				trafficDelta != bar->last.trafficDelta ||
				!bar->last.updatedOnce)
			{
				if (budget == NO_BUDGET) {
					sprintf(buf, "%s", friendlyUnitBuf(spec, total, buf1));
				} else if (ratio >= FATAL_CUTOFF) {
					ret = true;
					sprintf(buf, "OVER! %s / %s", friendlyUnitBuf(spec, total, buf1), friendlyUnitBuf(spec, budget, buf2));
				} else
					sprintf(buf, "  %s / %s", friendlyUnitBuf(spec, total, buf1), friendlyUnitBuf(spec, budget, buf2));
				if (trafficDelta)
					strcatf(buf, " (traffic: %d)", trafficDelta);
				ui_LabelSetText(bar->label_text, buf);

				if (bar->label_text_recent) {
					if (bar->doubleText == DoubleText_InView)
					{
						sprintf(buf, "(%s in view)", friendlyUnitBuf(spec, recent, buf1));
						ui_LabelSetText(bar->label_text_recent, buf);
					} else if (bar->doubleText == DoubleText_WorkingSet) {
						if (recent)
							sprintf(buf, "WorkingSet: %s", friendlyUnitBuf(spec, recent, buf1));
						else
							strcpy(buf, "");
						ui_LabelSetText(bar->label_text_recent, buf);
					}
				}
			} else {
				// Not updating, still update return value
				if (budget == NO_BUDGET) {
				} else if (ratio >= FATAL_CUTOFF) {
					ret = true;
				}
			}
		} else {
			if (ratio >= FATAL_CUTOFF) {
				ret = true;
			}
		}
		devassert(recent <= INT_MAX);
		bar->last.recent = recent;
		bar->last.total = total;
		bar->last.budget = budget;
		bar->last.trafficDelta = trafficDelta;
		bar->last.updatedOnce = true;
	}
	PERFINFO_AUTO_STOP();
	return (budget==NO_BUDGET)?false:ret;
}

static bool updateMsBar(BudgetBar *bar, F32 iCurrent, F32 budget, F32 max, bool bTick)
{
	bool ret=false;
	PERFINFO_AUTO_START(__FUNCTION__, 1);
	{
		char buf[64];
//		char buf1[64], buf2[64];
		F32 fCurrentBudgetRatio, fCurrentMaxRatio, fBudgetRatio, fMaxRatio;
		const bool squishy = false;
		F32 iTotal, iRecent;

		iTotal = MAX(MIN(bar->last.total-400, max), iCurrent);

		if (iCurrent > bar->last.recent)
		{
			iRecent = iCurrent;
		}
		else
		{
			iRecent = iCurrent*0.1f+bar->last.recent*0.9f;
		}

		fCurrentBudgetRatio = iRecent / (F32)budget;
		fCurrentMaxRatio = iRecent / (F32)max;
		fBudgetRatio = iTotal / (F32)budget;
		fMaxRatio = iTotal / (F32)max;

		if (bar) {
			Color color = {0,0,255,255};
			bar->sprite->tint = color;//colorFromRatio(fBudgetRatio, false);
			bar->sprite->widget.width = MINF(fMaxRatio, 1);
			if (bar->sprite_recent) {
				bar->sprite_recent->tint = colorFromRatio(fCurrentBudgetRatio, squishy);
				bar->sprite_recent->widget.width = MINF(fCurrentMaxRatio,1);
			}
			if (bar->sprite_recent_bar) {
				bar->sprite_recent_bar->widget.xPOffset = fCurrentBudgetRatio;
				//bar->sprite_recent_bar->widget.width = 0.01f;
			}
			if (bTick || !bar->last.updatedOnce)
			{

				//something more like this
				sprintf(buf, "%1.1f ms", iTotal*0.001f);

				//sprintf(buf, "  %s / %s", friendlyUnitBuf(spec, current, buf1), friendlyUnitBuf(spec, budget, buf2));
				ui_LabelSetText(bar->label_text, buf);
			} else {
				// Not updating, still update return value
				if (fCurrentBudgetRatio >= FATAL_CUTOFF) {
					ret = true;
				}
			}
		} else {
			if (fBudgetRatio >= FATAL_CUTOFF) {
				ret = true;
			}
		}

		bar->last.total = iTotal;
		devassert(iRecent >= 0);
		bar->last.recent = iRecent;
		bar->last.budget = budget;
		bar->last.updatedOnce = true;
	}
	PERFINFO_AUTO_STOP();
	return (budget==NO_BUDGET)?false:ret;
}

static void editLibBudgetsUpdateArt(void)
{
	PERFINFO_AUTO_START("texGetMemUsage", 1);
	texGetMemUsage(&budget_data.tex_usage, budget_data.total_art_flags);
	PERFINFO_AUTO_STOP_START("gfxGeoGetMemUsage", 1);
#define FOREACH(expr, arg1, arg2)								\
	expr(budget_data.tex_usage.data.video.loaded, arg1, arg2)			\
	expr(budget_data.tex_usage.data.video.recent, arg1, arg2)			\
	/*expr(budget_data.tex_usage.data.halfRes.loaded, arg1, arg2)*/	\
	/*expr(budget_data.tex_usage.data.halfRes.recent, arg1, arg2)*/	\
	expr(budget_data.tex_usage.count, arg1, arg2)			\
	expr(budget_data.geo_usage.countSystem, arg1, arg2)		\
	expr(budget_data.geo_usage.countVideo, arg1, arg2)		\
	expr(budget_data.geo_usage.loadedSystem, arg1, arg2)	\
	expr(budget_data.geo_usage.loadedVideo, arg1, arg2)		\
	expr(budget_data.geo_usage.recentVideo, arg1, arg2)
#define COMBINE_ONE(var, dst, src) var[dst] += var[src];
#define COMBINE(dst, src) FOREACH(COMBINE_ONE, dst, src)
#define REMOVE_ONE(var, dst, src) var##Total -= var[src];
#define REMOVE(src) FOREACH(REMOVE_ONE, unused, src)

	budget_data.geo_high_detail_adjust = gfxModelGetHighDetailAdjust();
	if (budget_data.window && ui_WindowIsVisible(budget_data.window) && !budget_data.window->shaded) {
		// It's open, get more detailed info
		gfxGeoGetMemUsage(&budget_data.geo_usage, budget_data.total_art_flags);
	} else {
		// Quick/instantaneous queries, does not get "recent" stat.
		wlGeoGetMemUsageQuick(&budget_data.geo_usage);
		// Remove terrain from the total
		//REMOVE(WL_FOR_TERRAIN_BITINDEX);
	}
	COMBINE(WL_FOR_WORLD_BITINDEX, WL_FOR_UTIL_BITINDEX); // Merge with World, leave total alone
	COMBINE(WL_FOR_WORLD_BITINDEX, WL_FOR_NOTSURE_BITINDEX); // Merge with World, leave total alone

#ifdef DEBUG_DIFFERENCES
	{
		int i, y;
		y=10;
		gfxGeoGetMemUsage(&budget_data.geo_usage, budget_data.total_art_flags);
		COMBINE(WL_FOR_WORLD_BITINDEX, WL_FOR_UTIL_BITINDEX); // Merge with World, leave total alone
		COMBINE(WL_FOR_WORLD_BITINDEX, WL_FOR_NOTSURE_BITINDEX); // Merge with World, leave total alone
		gfxXYprintf(10, y+=2, "Full sys:  %d", budget_data.geo_usage.loadedSystemTotal);
		gfxXYprintf(10, y+=2, "Full vid:  %d", budget_data.geo_usage.loadedVideoTotal);
// 		gfxGeoGetMemUsage(&budget_data.geo_usage, ~0);
// 		gfxXYprintf(10, y+=2, "Full sys+terrain:  %d", budget_data.geo_usage.loadedSystemTotal);
// 		gfxXYprintf(10, y+=2, "Full vid+terrain:  %d", budget_data.geo_usage.loadedVideoTotal);
		y++;
		for (i=0; i<ARRAY_SIZE(budget_data.geo_usage.loadedSystem); i++) 
			gfxXYprintf(10, y+=2, "Full Sub sys:  %d", budget_data.geo_usage.loadedSystem[i]);
		y++;
		for (i=0; i<ARRAY_SIZE(budget_data.geo_usage.loadedVideo); i++) 
			gfxXYprintf(10, y+=2, "Full Sub vid:  %d", budget_data.geo_usage.loadedVideo[i]);

		y=11;
		wlGeoGetMemUsageQuick(&budget_data.geo_usage);
		COMBINE(WL_FOR_WORLD_BITINDEX, WL_FOR_UTIL_BITINDEX); // Merge with World, leave total alone
		COMBINE(WL_FOR_WORLD_BITINDEX, WL_FOR_NOTSURE_BITINDEX); // Merge with World, leave total alone
		gfxXYprintf(10, y+=2, "Quick sys: %d", budget_data.geo_usage.loadedSystemTotal);
		gfxXYprintf(10, y+=2, "Quick vid: %d", budget_data.geo_usage.loadedVideoTotal);
// 		gfxXYprintf(10, y+=2, "Quick sys+terrain: %d", budget_data.geo_usage.loadedSystemTotal);
// 		gfxXYprintf(10, y+=2, "Quick vid+terrain: %d", budget_data.geo_usage.loadedVideoTotal);
		y++;
		for (i=0; i<ARRAY_SIZE(budget_data.geo_usage.loadedSystem); i++) 
			gfxXYprintf(10, y+=2, "Quick Sub sys: %d", budget_data.geo_usage.loadedSystem[i]);
		y++;
		for (i=0; i<ARRAY_SIZE(budget_data.geo_usage.loadedVideo); i++) 
			gfxXYprintf(10, y+=2, "Quick Sub vid: %d", budget_data.geo_usage.loadedVideo[i]);
	}
#endif
	PERFINFO_AUTO_STOP();
}

static UISkin *skinForError(UISkin *base)
{
	static UISkin *skin;
	int i;
	if (!skin) {
		skin = ui_SkinCreate(base);
	}
	for (i=0; i<ARRAY_SIZE(skin->button); i++) 
		skin->button[i] = colorFromRatio(2.0f, false);
	return skin;
}

static UISkin *skinForDetails(UISkin *base)
{
	if (!budget_data.detailsSkin) {
		budget_data.detailsSkin = ui_SkinCreate(base);
		budget_data.detailsSkin->background[0].a = budget_data.alpha * 255;
	}
	return budget_data.detailsSkin;
}

static UISkin *skinForMain(UISkin *base)
{
	return skinForDetails(base);
}

void editLibBudgetsUpdate(void)
{
	int i;
	budget_data.art_over_performance = false;
	budget_data.art_over_mem_full = false;
	budget_data.software_over_mem = false;

	editLibBudgetsUpdateArt();
	PERFINFO_AUTO_START("Window saving", 1);
		GamePrefStoreInt("Budgets_X", budget_data.window->widget.x);
		GamePrefStoreInt("Budgets_Y", budget_data.window->widget.y);
		GamePrefStoreInt("Budgets_W", budget_data.window->widget.width);
		GamePrefStoreInt("Budgets_H", budget_data.window->widget.height);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("Dynamic budgets", 1);

	// Dynamic budgets
	{
		BudgetBar *bar;
		bool b,dont_reset_traffic;
		MemoryBudget **budgets;
		MemoryBudget *budget_texture, *budget_geometry;
		MemoryBudget *budget_temp;
		MemoryBudget *budget_audio;

		PERFINFO_AUTO_START("Query", 1);
			dont_reset_traffic = updateMemoryBudgets();
			budgets = memBudgetGetBudgets();
		PERFINFO_AUTO_STOP_START("Update", 1);
		{

			FOR_EACH_IN_EARRAY_FORWARDS(budgets, MemoryBudget, budget)
			{
				// Generic update
				bar = getDynamicBudgetBar(budget->module);
				if (bar)
					budget_data.software_over_mem |= updateBar(bar, budget->workingSetSize, budget->current, budget->allowed, budget->traffic - budget->lastTraffic, false, byteSpec);
				if (!dont_reset_traffic)
					budget->lastTraffic = budget->traffic;
			}
			FOR_EACH_END;

			// Update group totals

			// Specifics for Textures/Geometry
			//////////////////////////////////////////////////////////////////////////
			// Textures
			budget_texture = memBudgetGetBudgetByName(BUDGET_Textures_Art);
			assert(budget_texture);
			budget_data.art_over_mem_full |= updateBar(&budget_data.bar.textures.total, budget_data.tex_usage.data.video.recentTotal, budget_data.tex_usage.data.video.loadedTotal, /*budget_texture->allowed*/NO_BUDGET, 0, true, byteSpec);
			budget_data.art_over_mem_full |= updateBar(&budget_data.bar.textures.totalOrig, budget_data.tex_usage.data.original.recentTotal, budget_data.tex_usage.data.original.loadedTotal, /*budget_texture->allowed*/NO_BUDGET, 0, true, byteSpec);
			// iterate children
			FOR_EACH_IN_EARRAY(budget_texture->subBudgets, MemoryBudget, subbudget)
			{
				for (i=0; i<ARRAY_SIZE(tex_budget_mapping); i++) {
					if (stricmp(subbudget->module, tex_budget_mapping[i].module)==0)
					{
						bar = getStaticBudgetBar(tex_budget_mapping[i].module);
						b = updateBar(bar, budget_data.tex_usage.data.video.recent[tex_budget_mapping[i].index], budget_data.tex_usage.data.video.loaded[tex_budget_mapping[i].index], subbudget->allowed, 0, true, byteSpec);
						if (tex_budget_mapping[i].index != WL_FOR_TERRAIN_BITINDEX)
						{
							// Terrain is combined and checked elsewhere
							budget_data.art_over_mem_full |= b;
						}

					}
				}
			}
			FOR_EACH_END;

			//////////////////////////////////////////////////////////////////////////
			// Geometry
			budget_geometry = memBudgetGetBudgetByName(BUDGET_Geometry_Art);
			assert(budget_geometry);
			b = updateBar(&budget_data.bar.geometry.total, budget_data.geo_usage.recentVideoTotal, /*budget_data.geo_usage.loadedSystemTotal + */ budget_data.geo_usage.loadedVideoTotal, /*budget_geometry->allowed*/NO_BUDGET, 0, true, byteSpec);
			budget_data.art_over_mem_full |= b;
			// iterate children
			FOR_EACH_IN_EARRAY(budget_geometry->subBudgets, MemoryBudget, subbudget)
			{
				for (i=0; i<ARRAY_SIZE(geo_budget_mapping); i++) {
					if (stricmp(subbudget->module, geo_budget_mapping[i].module)==0) {
						bar = getStaticBudgetBar(geo_budget_mapping[i].module);
						if (bar) {
							if (geo_budget_mapping[i].index == WL_FOR_WORLD_BITINDEX)
							{
								updateBar(bar,
									budget_data.geo_usage.recentVideo[geo_budget_mapping[i].index],
									budget_data.geo_usage.loadedVideo[geo_budget_mapping[i].index]
									//+ budget_data.geo_usage.loadedSystem[geo_budget_mapping[i].index]
									, NO_BUDGET, 0, true, byteSpec);
								bar = getStaticBudgetBar("w/o HighDetail");
								if (bar)
								{
									updateBar(bar,
										0,
										budget_data.geo_usage.loadedVideo[geo_budget_mapping[i].index]
										- budget_data.geo_high_detail_adjust
										//+ budget_data.geo_usage.loadedSystem[geo_budget_mapping[i].index]
										, subbudget->allowed, 0, true, byteSpec);
								}
							} else {
								b = updateBar(bar,
									budget_data.geo_usage.recentVideo[geo_budget_mapping[i].index],
									budget_data.geo_usage.loadedVideo[geo_budget_mapping[i].index]
									//+ budget_data.geo_usage.loadedSystem[geo_budget_mapping[i].index]
									, subbudget->allowed, 0, true, byteSpec);
								if (geo_budget_mapping[i].index != WL_FOR_WORLD_BITINDEX &&
									geo_budget_mapping[i].index != WL_FOR_TERRAIN_BITINDEX)
								{
									// World and terrain are combined and checked elsewhere
									budget_data.art_over_mem_full |= b;
								}
							}
						}
					}
				}
			}
			FOR_EACH_END;

			//////////////////////////////////////////////////////////////////////////
			// Summed geometry + terrain for World bar
			{
				size_t allowed;
				U32 loadedTexture = 0; //budget_data.tex_usage.data.video.loaded[WL_FOR_WORLD_BITINDEX];
				U32 loadedVideo = budget_data.geo_usage.loadedVideo[WL_FOR_WORLD_BITINDEX];
				U32 loadedSystem = budget_data.geo_usage.loadedSystem[WL_FOR_WORLD_BITINDEX];
				U32 recentTexture = 0; //budget_data.tex_usage.data.video.recent[WL_FOR_WORLD_BITINDEX];
				U32 recentGeo = budget_data.geo_usage.recentVideo[WL_FOR_WORLD_BITINDEX];
				loadedTexture +=  budget_data.tex_usage.data.video.loaded[WL_FOR_TERRAIN_BITINDEX];
				loadedVideo += budget_data.geo_usage.loadedVideo[WL_FOR_TERRAIN_BITINDEX];
				loadedSystem += budget_data.geo_usage.loadedSystem[WL_FOR_TERRAIN_BITINDEX];
				recentTexture += budget_data.tex_usage.data.video.recent[WL_FOR_TERRAIN_BITINDEX];
				recentGeo +=  budget_data.geo_usage.recentVideo[WL_FOR_TERRAIN_BITINDEX];

				budget_temp = getSubBudget(budget_geometry, "Geometry:World");
				allowed = budget_temp->allowed;
				budget_temp = getSubBudget(budget_geometry, "Geometry:Terrain");
				allowed += budget_temp->allowed;
				budget_temp = getSubBudget(budget_texture, "Textures:Terrain");
				allowed += budget_temp->allowed;

				bar = getStaticBudgetBar("World");
				budget_data.art_over_mem_full |= updateBar(bar,
					recentTexture + recentGeo,
					loadedTexture + loadedVideo
					//+ loadedSystem
					- budget_data.geo_high_detail_adjust
					, allowed, 0, true, byteSpec);
			}

			sndMemGetUsage(NULL);
			budget_audio = memBudgetGetBudgetByName(BUDGET_Audio);
			updateBar(&budget_data.bar.audio, 0, sndMemData.totalmem, budget_audio->allowed, 0, false, byteSpec);
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP_START("Static Budgets", 1);

	{
		static const UnitSpec metricSpec2[] = 
		{
			UNITSPEC("",	1,						0),
			UNITSPEC("K",	1000,					(S64)10000),
			UNITSPEC("M",	1000*1000,				(S64)10000*1000),
			UNITSPEC("G",	1000*1000*1000,			(S64)10000*1000*1000),
			{0},
		};
		BudgetBar *bar;
		FrameCounts counts;
		UIExpander *expander;
		VSCost costs[VS_COST_SIZE];
		F32 total_tri_ms;
		F32 total_tri_ms_high_detail;
		char buf[64], buf1[64];
		char triangles_summary[100];
		int total_tri_count;
		RdrDrawListPassStats *visual_pass_stats;
		F32 msPerTick = wlPerfGetMsPerTick();
		F32 usPerTick = msPerTick * 1000.0f;

		gfxGetFrameCounts(&counts);
		
		visual_pass_stats = &counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL];

		{
			static F32 fTick=0.f;
			bool bTick = false;
			F32 fDualCoreEffectiveCpu;
			F32 fCloth_us,fSkel_us;

			fTick -= counts.ms*0.001f;
			if (fTick < 0.0f)
			{
				bTick = true;
				fTick = 1.f;
			}

			fCloth_us = counts.world_perf_counts.time_cloth * usPerTick;
			fSkel_us = counts.world_perf_counts.time_skel * usPerTick;
			fDualCoreEffectiveCpu = MAX(counts.cpu_ms*1000.f,counts.cpu_ms*1000.f + (fCloth_us + fSkel_us)*0.5f - (counts.world_perf_counts.time_anim * usPerTick));
			
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.cpu_us, counts.cpu_ms*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.cpu_dual_core_effective_us, fDualCoreEffectiveCpu, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.ui_us, counts.world_perf_counts.time_ui * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.anim_us, counts.world_perf_counts.time_anim * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.cloth_us, fCloth_us, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.skel_us, fSkel_us, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.fx_us, counts.world_perf_counts.time_fx * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.sound_us, counts.world_perf_counts.time_sound * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.physics_us, counts.world_perf_counts.time_physics * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.draw_us, counts.world_perf_counts.time_draw * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.worldtrav_us, counts.world_perf_counts.time_queue_world * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.misc_cpu_us, counts.world_perf_counts.time_misc * usPerTick, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_us, counts.gpu_ms*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_shadows_us, rdrGfxPerfCounts_Last.gpu_times.fShadows*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_zprepass_us, rdrGfxPerfCounts_Last.gpu_times.fZPrePass*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_opaque_onepass_us, rdrGfxPerfCounts_Last.gpu_times.fOpaqueOnePass*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_shadow_buffer_us, rdrGfxPerfCounts_Last.gpu_times.fShadowBuffer*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_opaque_us, rdrGfxPerfCounts_Last.gpu_times.fOpaque*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_alpha_us, rdrGfxPerfCounts_Last.gpu_times.fAlpha*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_postprocess_us, rdrGfxPerfCounts_Last.gpu_times.fPostProcess*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_2d_us, rdrGfxPerfCounts_Last.gpu_times.f2D*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
			budget_data.art_over_performance |= updateMsBar(&budget_data.bar.gpu_other_us, rdrGfxPerfCounts_Last.gpu_times.fMisc*1000.f, CPU_BUDGET_US, MAX_CPU_US, bTick);
		}

		budget_data.art_over_performance |= updateBar(&budget_data.bar.object_scene_count, 0, counts.objects_in_scene, OBJECT_SCENE_BUDGET, 0, false, metricSpec2);
		budget_data.art_over_performance |= updateBar(&budget_data.bar.object_world_count, 0,
			visual_pass_stats->objects_drawn[ROC_WORLD] + visual_pass_stats->objects_drawn[ROC_WORLD_HIGH_DETAIL] +
			visual_pass_stats->objects_drawn[ROC_TERRAIN] + visual_pass_stats->objects_drawn[ROC_SKY],
			OBJECT_WORLD_BUDGET, 0, false, metricSpec2);
		/*budget_data.art_over_performance |= updateBar(&budget_data.bar.object_fx_count, 0, visual_pass_stats->objects_drawn[ROC_FX], OBJECT_FX_BUDGET, 0, false, metricSpec2);
		budget_data.art_over_performance |= updateBar(&budget_data.bar.object_character_count, 0, visual_pass_stats->objects_drawn[ROC_CHARACTER], OBJECT_CHARACTER_BUDGET, 0, false, metricSpec2);*/
		budget_data.art_over_performance |= updateBar(&budget_data.bar.object_total_count, 0, counts.opaque_objects_drawn + counts.alpha_objects_drawn, OBJECT_TOTAL_BUDGET, 0, false, metricSpec2);
		budget_data.art_over_performance |= updateBar(&budget_data.bar.world_animation_count, 0, counts.world_animation_updates, WORLD_ANIMATION_BUDGET, 0, false, metricSpec2);
		budget_data.art_over_performance |= updateBar(&budget_data.bar.template_count, 0, counts.unique_shader_graphs_referenced, SHADER_GRAPH_BUDGET, 0, false, metricSpec2);
		budget_data.art_over_performance |= updateBar(&budget_data.bar.material_count, 0, counts.unique_materials_referenced, MATERIAL_BUDGET, 0, false, metricSpec2);

		// Triangles
		total_tri_ms = gfxGetApproxPassVertexShaderTime(visual_pass_stats, costs, false, budget_data.cur_videmode_is_old);
		total_tri_count = 0;
		for (i=0; i<ARRAY_SIZE(costs); i++) {
			sprintf(buf, "Triangles:%s", costs[i].name);
			bar = getStaticBudgetBar(allocAddString(buf));
			sprintf(buf, "%s (%1.1f ms)", friendlyUnitBuf(metricSpec2, costs[i].triangles, buf1), costs[i].ms);
			updateBar(bar, 0, 0, NO_BUDGET, 0, false, metricSpec2);
			ui_LabelSetText(bar->label_text, buf);
			total_tri_count += costs[i].triangles;
		}
		sprintf(triangles_summary, "%s (%1.1fms / %1.1fms)",
			friendlyUnitBuf(metricSpec2, total_tri_count, buf1), total_tri_ms, TRIANGLE_MS_BUDGET);
		// High-detail triangles
		total_tri_ms_high_detail = gfxGetApproxPassVertexShaderTime(visual_pass_stats, costs, true, false) * TRIANGLE_DETAIL_MS_SCALE;
		total_tri_count = 0;
		for (i=0; i<ARRAY_SIZE(costs); i++) {
			sprintf(buf, "Triangles:%s", costs[i].name);
			bar = getStaticBudgetBar(allocAddString(buf));
			if (costs[i].triangles) {
				sprintf(buf, "High-Detail: %s (%1.1f ms)", friendlyUnitBuf(metricSpec2, costs[i].triangles, buf1), costs[i].ms * TRIANGLE_DETAIL_MS_SCALE);
				ui_LabelSetText(bar->label_text_recent, buf);
			} else {
				ui_LabelSetText(bar->label_text_recent, "");
			}
			total_tri_count += costs[i].triangles;
		}

		// Whichever is worse
		if (total_tri_ms / TRIANGLE_MS_BUDGET > total_tri_ms_high_detail / TRIANGLE_DETAIL_MS_BUDGET) {
			budget_data.art_over_performance |= updateBar(&budget_data.bar.tri_ms, 0, (size_t)(total_tri_ms * 1000), (size_t)(TRIANGLE_MS_BUDGET*1000), 0, false, metricSpec2);
		} else {
			budget_data.art_over_performance |= updateBar(&budget_data.bar.tri_ms, 0, (size_t)(total_tri_ms_high_detail * 1000), (size_t)(TRIANGLE_DETAIL_MS_BUDGET*1000), 0, false, metricSpec2);
		}

		strcatf(triangles_summary, " High-Detail: %s (%1.1fms / %1.1fms)",
			friendlyUnitBuf(metricSpec2, total_tri_count, buf1), total_tri_ms_high_detail, TRIANGLE_DETAIL_MS_BUDGET);
		ui_LabelSetText(budget_data.bar.tri_ms.label_text, triangles_summary); 

		// Cloth stuff.
		updateBar(&budget_data.bar.cloth_collisions,  0, g_dynClothCounters.iCollisionTests, NO_BUDGET, 0, false, metricSpec2);
		updateBar(&budget_data.bar.cloth_constraints, 0, g_dynClothCounters.iConstraintCalculations, NO_BUDGET, 0, false, metricSpec2);
		updateBar(&budget_data.bar.cloth_particles,   0, g_dynClothCounters.iPhysicsCalculations, NO_BUDGET, 0, false, metricSpec2);
		
		budget_data.art_over_performance |= updateBar(&budget_data.bar.cloth_total, 0,
			g_dynClothCounters.iCollisionTests + 
			g_dynClothCounters.iConstraintCalculations + 
			g_dynClothCounters.iPhysicsCalculations * 10,
			CLOTH_BUDGET, 0, false, metricSpec2);

		dynClothResetCounters();

		// Encounter stuff is really design, not art.
		budget_data.art_over_performance |= updateBar(&budget_data.bar.spawn_total_count, 0, encBudgetValues.numSpawnedEnts, SPAWN_BUDGET, 0, false, metricSpec2);
		updateBar(&budget_data.bar.running_encounters, 0, encBudgetValues.numRunningEncs, encBudgetValues.numEncounters, 0, false, metricSpec2);
		budget_data.art_over_performance |= updateBar(&budget_data.bar.spawned_fsm_cost, 0, encBudgetValues.spawnedFSMCost, FSM_BUDGET, 0, false, metricSpec2);
		budget_data.art_over_performance |= updateBar(&budget_data.bar.potential_fsm_cost, 0, encBudgetValues.potentialFSMCost, FSM_BUDGET, 0, false, metricSpec2);
		expander = (UIExpander*)ui_RebuildableTreeGetWidgetByName(budget_data.uirt, "Performance");
		assert(expander);
		if (!budget_data.art_over_performance && !ui_ExpanderIsOpened(expander)) {
			budget_data.performance_was_under_while_closed = true;
		} else if (budget_data.art_over_performance && !ui_ExpanderIsOpened(expander) && budget_data.performance_was_under_while_closed) {
			// Suddenly we're over
			ui_ExpanderSetOpened(expander, true);
			budget_data.performance_was_under_while_closed = false;
		} else if (ui_ExpanderIsOpened(expander)) {
			budget_data.performance_was_under_while_closed = false;
		}

		if (budget_data.art_over_performance) {
			ui_WidgetSetTextString(UI_WIDGET(expander), "Performance - OVER");
		} else {
			ui_WidgetSetTextString(UI_WIDGET(expander), "Performance");
		}
	}

	PERFINFO_AUTO_STOP_START("editLibBudgetsUpdateSubWindows", 1);
	editLibBudgetsUpdateSubWindows();
	PERFINFO_AUTO_STOP();

	if (budget_data.art_over_mem_full ||
		budget_data.art_over_performance)
		ui_WindowSetTitle(budget_data.window, "Budgets - OVER");
	else
		ui_WindowSetTitle(budget_data.window, "Budgets");

	gfxGetFrameCountsForModification()->over_budget_mem = budget_data.art_over_mem_full;
	gfxGetFrameCountsForModification()->over_budget_perf = budget_data.art_over_performance;
}

#define ALIGN_SPOT 130

static BudgetBar addBar(UIRTNode *group, bool toExpander, const char *name_key, EBarType eBarType, DoubleTextType doubleText, size_t budget)
{
	U32 rgba = 0xFF00FFFF;
	BudgetBar ret={0};
	UISprite *sprite;
	UIPane *pane = ui_PaneCreate(0, 2, 1.0f, 20, UIUnitPercentage, UIUnitFixed, false);
	pane->invisible = true;
	pane->widget.leftPad = toExpander?(ALIGN_SPOT - UIAUTOWIDGET_INDENT - 1):0;
	pane->widget.rightPad = 2;
	pane->widget.uClickThrough = true;

	if (!budget_data.labelFont) {
		budget_data.labelFont = ui_StyleFontCreate("BudgetLabelFont", &g_font_Sans, ColorWhite, false, false, 1);
		RefSystem_AddReferent(g_ui_FontDict, "BudgetLabelFont", budget_data.labelFont);
	}

	sprite = ui_SpriteCreate(0, 0, 1.f, 1.f, "white");
	sprite->widget.widthUnit = UIUnitPercentage;
	sprite->widget.heightUnit = UIUnitPercentage;
	if (budget == NO_BUDGET) {
		sprite->tint = colorFromRGBA(0x7F7F7FFF);
	} else {
		sprite->tint = colorFromRGBA(0x2F0000FF);
	}
	ui_WidgetGroupAdd(&pane->widget.children, UI_WIDGET(sprite));

	ret.sprite = sprite = ui_SpriteCreate(0, 0, 0.5f, 1.f, "white");
	sprite->widget.widthUnit = UIUnitPercentage;
	sprite->widget.heightUnit = UIUnitPercentage;
	sprite->tint = colorFromRGBA(rgba);
	ui_WidgetGroupAdd(&pane->widget.children, UI_WIDGET(sprite));

	if (eBarType == EBarType_Notch) {
		ret.sprite_recent_bar = sprite = ui_SpriteCreate(0, 0, 4, 1.f, "white");
		sprite->widget.widthUnit = UIUnitFixed;
		sprite->widget.heightUnit = UIUnitPercentage;
		sprite->tint = colorFromRGBA(0x0000FFFF);
		ui_WidgetGroupAdd(&pane->widget.children, UI_WIDGET(sprite));
	}
	else if (eBarType == EBarType_Double)
	{
		ret.sprite_recent = sprite = ui_SpriteCreate(0, 0, 0.25f, 1.f, "white");
		sprite->widget.widthUnit = UIUnitPercentage;
		sprite->widget.heightUnit = UIUnitPercentage;
		sprite->tint = colorFromRGBA(rgba);
		ui_WidgetGroupAdd(&pane->widget.children, UI_WIDGET(sprite));
	}

	ret.doubleText = doubleText;
	if (doubleText != DoubleText_None) {
		ret.label_text_recent = ui_LabelCreate("RecentLabel", doubleText==DoubleText_WorkingSet?300:210, 0);
		ret.label_text_recent->widget.pOverrideSkin = NULL;
		ui_LabelSetFont(ret.label_text_recent, budget_data.labelFont);
		ui_WidgetGroupAdd(&pane->widget.children, UI_WIDGET(ret.label_text_recent));
	}

	ret.label_text = ui_LabelCreate("Label", 8, 0);
	ret.label_text->widget.pOverrideSkin = NULL;
	ui_LabelSetFont(ret.label_text, budget_data.labelFont);
	ui_WidgetGroupAdd(&pane->widget.children, UI_WIDGET(ret.label_text));

	if (toExpander)
		ui_ExpanderAddLabel(group->expander, UI_WIDGET(pane));
	else {
		UIAutoWidgetParams params={0};
		params.alignTo = ALIGN_SPOT;
		ui_RebuildableTreeAddWidget(group, UI_WIDGET(pane), NULL, false, name_key, &params);
	}
	ret.module = name_key;
	return ret;
}

static void addBarStatic(UIRTNode *group, const char *name_key, bool toExpander, EBarType eBarType, DoubleTextType doubleText, size_t budget)
{
	BudgetBar *bar = calloc(sizeof(*bar),1);
	*bar = addBar(group, toExpander, name_key, eBarType, doubleText, budget);
	eaPush(&budget_data.bars_static, bar);
}

static bool closeDetails(UIAnyWidget *window, BudgetWindow *subwindow)
{
	if (subwindow->table == parse_ShaderGraphUsageEntry) {
		gfxMaterialSelectByFilename(NULL);
		rdr_state.do_occlusion_queries = 0;
	}
	if (subwindow->table == parse_TexMemUsageEntry) {
		gfxTextureSelect(NULL);
	}
	if (subwindow->is_scene_objs) {
		gfxGeoSelectByRenderInfo(NULL);
	}
	if (subwindow->entry_list_needs_freeing) {
		eaDestroyStructVoid(&subwindow->entry_list, subwindow->table);
		ui_ListSetModel(subwindow->list, NULL, NULL);
	}
	eaFindAndRemoveFast(&budget_data.subwindows, subwindow);
	free(subwindow);
	ui_WidgetQueueFree(window);
	return true;
}

static void updateDetails(BudgetWindow *subwindow)
{
	int i;
	if (!subwindow->table) {
		// Determine appropriate table
		if (strStartsWith(subwindow->module, "Textures") || strStartsWith(subwindow->module, "Geometry")) {
			char *sub = strchr(subwindow->module, ':');
			if (sub)
				sub++;
			subwindow->flags_to_list = 0;
			if (!sub) {
				subwindow->flags_to_list = ~0;
			} else if (stricmp(sub, "World")==0) {
				subwindow->flags_to_list = WL_FOR_WORLD | WL_FOR_UTIL;
			} else if (stricmp(sub, "Character")==0) {
				subwindow->flags_to_list = WL_FOR_ENTITY;
			} else if (stricmp(sub, "FX")==0) {
				subwindow->flags_to_list = WL_FOR_FX;
			} else if (stricmp(sub, "UI")==0) {
				subwindow->flags_to_list = WL_FOR_UI;
			} else if (stricmp(sub, "Terrain")==0) {
				subwindow->flags_to_list = WL_FOR_TERRAIN;
			} else if (stricmp(sub, "Fonts")==0) {
				subwindow->flags_to_list = WL_FOR_FONTS;
			} else if (stricmp(sub, "Util")==0) { // Naming this "Misc" makes it so we can't see the system-memory Textures:Misc usage
				subwindow->flags_to_list = WL_FOR_UTIL;
			}
			if (subwindow->flags_to_list) {
				if (strStartsWith(subwindow->module, "Textures")) {
					g_needTextureBudgetInfo = true;
					subwindow->table = parse_TexMemUsageEntry;
				} else if (strStartsWith(subwindow->module, "Geometry")) {
					subwindow->table = parse_GeoMemUsageEntry;
				} else {
					assert(0);
				}
				subwindow->entry_list_needs_freeing = true;
			}
		} else if (stricmp(subwindow->module, "Scene objs")==0) {
			subwindow->flags_to_list = ~0;
			subwindow->table = parse_GeoMemUsageEntry;
			subwindow->is_scene_objs = true;
			subwindow->entry_list_needs_freeing = true;
		} else if (stricmp(subwindow->module, "Unique Tmplts")==0) {
			subwindow->table = parse_ShaderGraphUsageEntry;
			subwindow->entry_list_needs_freeing = true;
		} else if (stricmp(subwindow->module, "Unique Mtls")==0) {
			subwindow->table = parse_MaterialUsageEntry;
			subwindow->entry_list_needs_freeing = true;
		} else if (stricmp(subwindow->module, "AudioDetail")==0) {
			subwindow->table = parse_AudioMemEntry;
			subwindow->entry_list_needs_freeing = false;
		}
		if (!subwindow->table) {
			subwindow->table = parse_ModuleMemOperationStats;
			assert(!subwindow->entry_list);
		}
	}

	if (subwindow->is_scene_objs) {
		GeoRenderInfo *selected_geo_render_info=0;
		if (subwindow->list) {
			GeoMemUsageEntry *selected = ui_ListGetSelectedObject(subwindow->list);
			if (selected) {
				gfxGeoSelectByRenderInfo(selected_geo_render_info = selected->geo_render_info);
			} else {
				gfxGeoSelectByRenderInfo(NULL);
			}
		}
		gfxGeoGetDrawCallsDetailed(subwindow->flags_to_list, (GeoMemUsageEntry***)&subwindow->entry_list);
		if (subwindow->list && selected_geo_render_info) {
			ui_ListSort(subwindow->list);
			for (i=0; i<eaSize(&subwindow->entry_list); i++) 
				if (((GeoMemUsageEntry*)subwindow->entry_list[i])->geo_render_info == selected_geo_render_info)
					ui_ListSetSelectedRow(subwindow->list, i);
		}
	} else if (subwindow->table == parse_TexMemUsageEntry) {
		const char *selected_filename=NULL;
		bool selectedIsLowMips=false;
		g_needTextureBudgetInfo = true;
		if (subwindow->list) {
			TexMemUsageEntry *selected = ui_ListGetSelectedObject(subwindow->list);
			if (selected) {
				selected_filename = selected->filename;
				selectedIsLowMips = selected->isLowMips;
				gfxTextureSelect(texFind(selected_filename, 0));
			} else {
				gfxTextureSelect(NULL);
			}
		}
		texGetMemUsageDetailed(subwindow->flags_to_list, (TexMemUsageEntry***)&subwindow->entry_list);
		if (subwindow->list && selected_filename) {
			ui_ListSort(subwindow->list);
			for (i=0; i<eaSize(&subwindow->entry_list); i++)
				if (((TexMemUsageEntry*)subwindow->entry_list[i])->filename == selected_filename &&
					((TexMemUsageEntry*)subwindow->entry_list[i])->isLowMips == selectedIsLowMips)
				{
					ui_ListSetSelectedRow(subwindow->list, i);
				}
		}
	} else if (subwindow->table == parse_GeoMemUsageEntry) {
		const char *selected_name=NULL;
		GeoRenderInfo *selected_render_info=NULL;
		if (subwindow->list) {
			GeoMemUsageEntry *selected = ui_ListGetSelectedObject(subwindow->list);
			if (selected) {
				gfxGeoSelectByRenderInfo(selected_render_info = selected->geo_render_info);
				selected_name = selected->name;
			} else {
				gfxGeoSelectByRenderInfo(NULL);
			}
		}
		gfxGeoGetMemUsageDetailed(subwindow->flags_to_list, (GeoMemUsageEntry***)&subwindow->entry_list);
		if (subwindow->list && (selected_render_info || selected_name)) {
			ui_ListSort(subwindow->list);
			for (i=0; i<eaSize(&subwindow->entry_list); i++)
				if (selected_render_info && ((GeoMemUsageEntry*)subwindow->entry_list[i])->geo_render_info == selected_render_info ||
					!selected_render_info && ((GeoMemUsageEntry*)subwindow->entry_list[i])->name == selected_name)
				{
					ui_ListSetSelectedRow(subwindow->list, i);
				}
		}
	} else if (subwindow->table == parse_ShaderGraphUsageEntry) {
		// Do this *before* updating, since that changes the sort order		
		const char *selected_filename=NULL;
		if (subwindow->list) {
			ShaderGraphUsageEntry *selected = ui_ListGetSelectedObject(subwindow->list);
			if (selected) {
				gfxMaterialSelectByFilename(selected_filename = selected->filename);
			} else {
				gfxMaterialSelectByFilename(NULL);
			}
		}
		rdr_state.do_occlusion_queries = 1;
		gfxMaterialGetShaderUsageDetailed((ShaderGraphUsageEntry***)&subwindow->entry_list);
		if (subwindow->list && selected_filename) {
			ui_ListSort(subwindow->list);
			for (i=0; i<eaSize(&subwindow->entry_list); i++)
				if (((ShaderGraphUsageEntry*)subwindow->entry_list[i])->filename == selected_filename)
					ui_ListSetSelectedRow(subwindow->list, i);
		}
	} else if (subwindow->table == parse_MaterialUsageEntry) {
		const char *selected_filename=NULL;
		if (subwindow->list) {
			MaterialUsageEntry *selected = ui_ListGetSelectedObject(subwindow->list);
			if (selected) {
				selected_filename = selected->filename;
			}
		}
		gfxMaterialGetUsageDetailed((MaterialUsageEntry***)&subwindow->entry_list);
		if (subwindow->list && selected_filename) {
			ui_ListSort(subwindow->list);
			for (i=0; i<eaSize(&subwindow->entry_list); i++)
				if (((MaterialUsageEntry*)subwindow->entry_list[i])->filename == selected_filename)
					ui_ListSetSelectedRow(subwindow->list, i);
		}
	} else if (subwindow->table == parse_AudioMemEntry ) {
		sndMemGetUsage((AudioMemEntry ***)&subwindow->entry_list);
	} else {
		// doesn't need dynamic updating
	}
}


static void editLibBudgetsUpdateSubWindows(void)
{
	g_needTextureBudgetInfo = false;
	FOR_EACH_IN_EARRAY(budget_data.subwindows, BudgetWindow, subwindow)
	{
		if (!ui_WindowIsVisible(subwindow->window) || !subwindow->entry_list)
			continue;
		updateDetails(subwindow);
	}
	FOR_EACH_END;
}

static bool closeWhoUsesTexture(UIAnyWidget *window, void *userData_UNUSED)
{
	ui_WindowHide(window);
	budget_data.whoUsesTextureName = NULL;
	return true;
}

static void detailsShowObjectsUsingTextureWindow(void)
{
	char buf[128];
	// Create window and listbox
	if (!budget_data.whoUsesTextureWindow) {
		int i;
		UIWindow *detailsWindow = ui_WindowCreate("Who uses texture", 100, 750*budget_data.scale, 750, 300);
		UIList *list;
		budget_data.whoUsesTextureWindow = detailsWindow;
		detailsWindow->widget.pOverrideSkin = skinForDetails(UI_GET_SKIN(detailsWindow));
		detailsWindow->widget.scale = budget_data.scale;

		list = ui_ListCreate(parse_GeoMemUsageEntry, &budget_data.whoUsesTextureList, 14);
		ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1, 1, UIUnitPercentage, UIUnitPercentage);

		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Filename", (intptr_t)"filename", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Model Name", (intptr_t)"name", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "CountInScene", (intptr_t)"countInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Total Mem", (intptr_t)"total_mem", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Video Mem", (intptr_t)"vid_mem", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SysMem Packed", (intptr_t)"sys_mem_packed", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SysMem Unpacked", (intptr_t)"sys_mem_unpacked", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Tris", (intptr_t)"tris", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Verts", (intptr_t)"verts", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SubObjs", (intptr_t)"sub_objects", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Recent?", (intptr_t)"recent", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Shared?", (intptr_t)"shared", NULL));

		ui_ListSetSortedColumn(list, 2);
		ui_ListSetSortedColumn(list, 2); // Twice to make it descending
		for (i=0; i<eaSize(&list->eaColumns); i++)
		{
			ui_ListColumnSetSortable(list->eaColumns[i], true);
			if (i>=1)
				ui_ListColumnSetWidth(list->eaColumns[i], true, 0);
			else
				ui_ListColumnSetWidth(list->eaColumns[i], false, 75);
		}
		ui_WindowAddChild(detailsWindow, UI_WIDGET(list));
		ui_WindowSetCloseCallback(detailsWindow, closeWhoUsesTexture, NULL);
	}
	ui_WindowShow(budget_data.whoUsesTextureWindow);
	sprintf(buf, "Who uses texture %s", budget_data.whoUsesTextureName);
	ui_WindowSetTitle(budget_data.whoUsesTextureWindow, buf);
}

static void updateShowObjectsUsingTexture(void)
{
	gfxGeoGetWhoUsesTexture(budget_data.whoUsesTextureName, budget_data.whoUsesTextureCurrentSceneOnly, &budget_data.whoUsesTextureList);
}

static void detailsShowObjectsUsingTextureHere(UIAnyWidget *widget_UNUSED, const char **filename)
{
	budget_data.whoUsesTextureName = allocAddString(*filename);
	budget_data.whoUsesTextureCurrentSceneOnly = true;
	detailsShowObjectsUsingTextureWindow();
	updateShowObjectsUsingTexture();
}

static void detailsShowObjectsUsingTextureAnywhere(UIAnyWidget *widget_UNUSED, const char **filename)
{
	budget_data.whoUsesTextureName = allocAddString(*filename);
	budget_data.whoUsesTextureCurrentSceneOnly = false;
	detailsShowObjectsUsingTextureWindow();
	updateShowObjectsUsingTexture();
}

static void detailsOpenInMaterialEditor(UIAnyWidget *widget_UNUSED, const char **filename)
{
	char fn[MAX_PATH];
	sprintf(fn, "Materials/%s", *filename);
	fileLocateWrite(fn, fn);
	fileOpenWithEditor(fn);
}

static void detailsBreakOnAlloc(UIAnyWidget *widget_UNUSED, const char **filename)
{
	strcpy(memMonitorBreakOnAlloc, *filename);
}

static void detailsContextCallback(UIList *list, BudgetWindow *subwindow)
{
	if (subwindow->table == parse_ModuleMemOperationStats) {
		ModuleMemOperationStats *entry = ui_ListGetSelectedObject(list);
		if (entry) {
			static UIMenu *detailsPopupMenu;
			static const char *detailsPopupMenuData;
			if (!detailsPopupMenu) {
				detailsPopupMenu = ui_MenuCreateWithItems("Details Context Menu",
					ui_MenuItemCreate("Break on next alloc", UIMenuCallback, detailsBreakOnAlloc, (void*)&detailsPopupMenuData, NULL),
					NULL);
			}
			detailsPopupMenuData = entry->moduleName;
			ui_MenuPopupAtCursor(detailsPopupMenu);
		}
	} else if (subwindow->table == parse_TexMemUsageEntry) {
		TexMemUsageEntry *entry = ui_ListGetSelectedObject(list);
		if (entry) {
			static UIMenu *detailsPopupMenu;
			static const char *detailsPopupMenuData;
			if (!detailsPopupMenu) {
				detailsPopupMenu = ui_MenuCreateWithItems("Details Context Menu",
					ui_MenuItemCreate("Show objects who use this texture in this scene", UIMenuCallback, detailsShowObjectsUsingTextureHere, (void*)&detailsPopupMenuData, NULL),
					ui_MenuItemCreate("Show objects who use this texture anywhere", UIMenuCallback, detailsShowObjectsUsingTextureAnywhere, (void*)&detailsPopupMenuData, NULL),
					NULL);
			}
			detailsPopupMenuData = entry->filename;
			ui_MenuPopupAtCursor(detailsPopupMenu);
		}
	} else if (subwindow->table == parse_ShaderGraphUsageEntry) {
		ShaderGraphUsageEntry *entry = ui_ListGetSelectedObject(list);
		if (entry) {
			static UIMenu *detailsPopupMenu;
			static const char *detailsPopupMenuData;
			if (!detailsPopupMenu) {
				detailsPopupMenu = ui_MenuCreateWithItems("Details Context Menu",
					ui_MenuItemCreate("Open in Material Editor", UIMenuCallback, detailsOpenInMaterialEditor, (void*)&detailsPopupMenuData, NULL),
					NULL);
			}
			detailsPopupMenuData = entry->filename;
			ui_MenuPopupAtCursor(detailsPopupMenu);
		}
	}
}

static void showDetails(UIAnyWidget *widget_UNUSED, const char *module)
{
	UIWindow *detailsWindow = ui_WindowCreate(module, 100, 100, 750, 600);
	MemoryBudget *budget = memBudgetGetBudgetByName(module);
	UIList *list;
	ModuleMemOperationStats ***file_list = budget?&budget->stats:NULL;
	BudgetWindow *subwindow = calloc(sizeof(*subwindow),1);
	int i;
	int startAutoSizingAt=1;

	detailsWindow->widget.pOverrideSkin = skinForDetails(UI_GET_SKIN(detailsWindow));
	detailsWindow->widget.scale = budget_data.scale;
	subwindow->window = detailsWindow;
	subwindow->module = module;

	updateDetails(subwindow);

	if (!subwindow->entry_list) {
		// Using pointer to static earray in memory budgets code. Don't copy it
		//  here, unless we save it with an additional level of indirection.
		subwindow->entry_list_needs_freeing = false;
	}

	assert(subwindow->table);
	subwindow->list = list = ui_ListCreate(subwindow->table, subwindow->entry_list?&subwindow->entry_list:file_list, 14);
	ui_WidgetSetDimensionsEx(UI_WIDGET(list), 1, 1, UIUnitPercentage, UIUnitPercentage);

	if (subwindow->table == parse_ModuleMemOperationStats) {

		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Module", (intptr_t)"moduleName", NULL));

		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Memory Use", (intptr_t)"size", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Count", (intptr_t)"Count", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Traffic", (intptr_t)"countTraffic", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "WorkingSet Size", (intptr_t)"workingSetSize", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "WorkingSet Count", (intptr_t)"workingSetCount", NULL));

		ui_ListSetSortedColumn(list, 1);
		ui_ListSetSortedColumn(list, 1); // Twice to make it descending

		startAutoSizingAt = 0;
	} else if (subwindow->table == parse_TexMemUsageEntry) {
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Folder", (intptr_t)"directory", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Filename", (intptr_t)"Filename", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Memory Use (reduced)", (intptr_t)"memory_use", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Width", (intptr_t)"Width", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Height", (intptr_t)"Height", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Dist", (intptr_t)"Dist", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "TexelDensity", (intptr_t)"uv_density", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Recent?", (intptr_t)"recent", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "LowMips?", (intptr_t)"isLowMips", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Shared?", (intptr_t)"shared", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "CountInScene", (intptr_t)"CountInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Reduced", (intptr_t)"Reduced", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Memory Use (original)", (intptr_t)"memory_use_original", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "OrigWidth", (intptr_t)"OrigWidth", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "OrigHeight", (intptr_t)"OrigHeight", NULL));

		ui_ListSetSortedColumn(list, 2);
		ui_ListSetSortedColumn(list, 2); // Twice to make it descending

	} else if (subwindow->is_scene_objs) {
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Filename", (intptr_t)"filename", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Model Name", (intptr_t)"name", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "CountInScene", (intptr_t)"countInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "DrawCalls", (intptr_t)"uniqueInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SubObjs", (intptr_t)"sub_objects", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Total Mem", (intptr_t)"total_mem", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Video Mem", (intptr_t)"vid_mem", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SysMem Packed", (intptr_t)"sys_mem_packed", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SysMem Unpacked", (intptr_t)"sys_mem_unpacked", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Tris", (intptr_t)"tris", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Verts", (intptr_t)"verts", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "TrisInScene", (intptr_t)"trisInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "VisDist", (intptr_t)"far_dist", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "TexelDensity", (intptr_t)"uv_density", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "LOD Template", (intptr_t)"lod_template_name", NULL));

		ui_ListSetSortedColumn(list, 2);
		ui_ListSetSortedColumn(list, 2); // Twice to make it descending
	} else if (subwindow->table == parse_GeoMemUsageEntry) {
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Filename", (intptr_t)"filename", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Model Name", (intptr_t)"name", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Total Mem", (intptr_t)"total_mem", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Video Mem", (intptr_t)"vid_mem", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SysMem Packed", (intptr_t)"sys_mem_packed", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SysMem Unpacked", (intptr_t)"sys_mem_unpacked", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Tris", (intptr_t)"tris", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Verts", (intptr_t)"verts", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "SubObjs", (intptr_t)"sub_objects", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Recent?", (intptr_t)"recent", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Shared?", (intptr_t)"shared", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "CountInScene", (intptr_t)"countInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "LOD Template", (intptr_t)"lod_template_name", NULL));

		ui_ListSetSortedColumn(list, 2);
		ui_ListSetSortedColumn(list, 2); // Twice to make it descending
	} else if (subwindow->table == parse_ShaderGraphUsageEntry) {
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Filename", (intptr_t)"Filename", NULL));
		ui_ListColumnSetWidth(list->eaColumns[0], false, 300);
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "CountInScene", (intptr_t)"CountInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Raw Score", (intptr_t)"rawMaterialScore", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Actual Score", (intptr_t)"materialScore", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "ALU Inst", (intptr_t)"instruction_count", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Textures", (intptr_t)"texture_fetch_count", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Parameters", (intptr_t)"dynamic_constant_count", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Variations", (intptr_t)"variationCount", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Tris", (intptr_t)"tricountInScene", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Pixels", (intptr_t)"pixels", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Est Total Cost", (intptr_t)"factor", NULL));

		ui_ListSetSortedColumn(list, 1);
		ui_ListSetSortedColumn(list, 1); // Twice to make it descending
	} else if (subwindow->table == parse_MaterialUsageEntry) {
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Filename", (intptr_t)"Filename", NULL));
		ui_ListColumnSetWidth(list->eaColumns[0], false, 300);
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "CountInScene", (intptr_t)"CountInScene", NULL));
		ui_ListSetSortedColumn(list, 1);
		ui_ListSetSortedColumn(list, 1); // Twice to make it descending
	} else if (subwindow->table == parse_AudioMemEntry) {
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Desc", (intptr_t)"desc_name", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "File Name", (intptr_t)"file_name", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Origin", (intptr_t)"orig_name", NULL));
		ui_ListAppendColumn(list, ui_ListColumnCreate(UIListPTName, "Total", (intptr_t)"total_mem", NULL));

		ui_ListSetSortedColumn(list, 1);
		ui_ListSetSortedColumn(list, 1); // Twice to make it descending
	}
	ui_ListSetContextCallback(list, detailsContextCallback, subwindow);

	for (i=0; i<eaSize(&list->eaColumns); i++)
	{
		ui_ListColumnSetSortable(list->eaColumns[i], true);
		if (i >= startAutoSizingAt) {
			ui_ListColumnSetWidth(list->eaColumns[i], true, 0);
		} else {
			if (list->eaColumns[i]->fWidth == 200.f) // Was it the default size?
				ui_ListColumnSetWidth(list->eaColumns[i], false, 75);
		}
	}

	ui_WindowAddChild(detailsWindow, UI_WIDGET(list));

	ui_WindowSetCloseCallback(detailsWindow, closeDetails, subwindow);

	ui_WindowShow(detailsWindow);

	eaPush(&budget_data.subwindows, subwindow);
}

static void updateWorkingSet(UIAnyWidget *widget_UNUSED, void *userData)
{
	memTrackUpdateWorkingSet();
}


static struct {
	const char *mode;
	const char *label;
	int xpos;
	bool old;
	UIButton *button;
} vidmodes[] = {
	{"HighEnd", "High End", 100, false},
	{"8800", "Mid (8800)", 177, false},
	{"6800", "Low (6800)", 259, false},
	{"LowEndIntel", "Low End Intel", 351, true},
	{"LowEndATI", "Low End ATI", 460, true},
};

static void setVideoMode(UIAnyWidget *widget_UNUSED, void *userData)
{
	int i;
	int mode = (intptr_t)userData;
	gfxEmulateCard(vidmodes[mode].mode);
	budget_data.cur_videmode_is_old = vidmodes[mode].old;
	for (i=0; i<ARRAY_SIZE(vidmodes); i++)
	{
		char effLabel[1024];
		if (i==mode)
			sprintf(effLabel, "X %s", vidmodes[i].label);
		else
			strcpy(effLabel, vidmodes[i].label);
		ui_ButtonSetTextAndResize(vidmodes[i].button, effLabel);
	}
}

static void editLibBudgetsOpenCloseExpanders(void)
{
	UIExpander *expander;
	bool bArtOver = budget_data.art_over_mem_full;

	expander = (UIExpander*)ui_RebuildableTreeGetWidgetByName(budget_data.uirt, "Performance");
	assert(expander);
	ui_ExpanderSetOpened(expander, budget_data.art_over_performance);

	expander = (UIExpander*)ui_RebuildableTreeGetWidgetByName(budget_data.uirt, "Memory");
	assert(expander);
	ui_ExpanderSetOpened(expander, bArtOver || budget_data.software_over_mem);

// 	expander = (UIExpander*)ui_RebuildableTreeGetWidgetByName(budget_data.uirt, "Memory/Textures");
// 	assert(expander);
// 	ui_ExpanderSetOpened(expander, bArtOver);
// 
// 	expander = (UIExpander*)ui_RebuildableTreeGetWidgetByName(budget_data.uirt, "Memory/Geometry");
// 	assert(expander);
// 	ui_ExpanderSetOpened(expander, bArtOver);
}

void elbOnDataChanged(UIRTNode *node, UserData userData)
{
	if (budget_data.detailsSkin) {
		budget_data.detailsSkin->background[0].a = budget_data.alpha * 255;
	}
	budget_data.scale = CLAMP(budget_data.scale, 0.25f, 2.0f);
	FOR_EACH_IN_EARRAY(budget_data.subwindows, BudgetWindow, subwindow)
	{
		subwindow->window->widget.scale = budget_data.scale;
	}
	FOR_EACH_END;
	if (budget_data.whoUsesTextureWindow)
		budget_data.whoUsesTextureWindow->widget.scale = budget_data.scale;

	GamePrefStoreFloat("Budgets_Scale", budget_data.scale);
	GamePrefStoreFloat("Budgets_Opacity", budget_data.alpha);
	GamePrefStoreInt("Budgets_AlertOn", budget_data.alert_category);
}

void budgetsHelp(UIAnyWidget *widget_UNUSED, UserData userdata_UNUSED)
{
	openCrypticWikiPage("Core/Budget+Meters");
}

AUTO_COMMAND ACMD_NAME(budgets);
void editLibBudgetsShow(void)
{
	int i;
	UIRTNode *group;
	if (budget_data.window) {
		ui_WindowShow(budget_data.window);
		editLibBudgetsUpdate();
		// Update appropriate expander states
		editLibBudgetsOpenCloseExpanders();
		return;
	}
	budget_data.window = ui_WindowCreate("Budgets",
		GamePrefGetInt("Budgets_X", 0),
		GamePrefGetInt("Budgets_Y", 100),
		GamePrefGetInt("Budgets_W", 525),
		GamePrefGetInt("Budgets_H", 264));
	budget_data.window->widget.pOverrideSkin = skinForMain(UI_GET_SKIN(budget_data.window));
	ui_WindowShow(budget_data.window);
	budget_data.uirt = ui_RebuildableTreeCreate();
	ui_RebuildableTreeInit(budget_data.uirt, &budget_data.window->widget.children, 0, 0, UIRTOptions_YScroll);

	group = ui_RebuildableTreeAddGroup(budget_data.uirt->root, "Performance", "Performance", true, NULL);
	{
		UIButton *button;

		for (i=0; i<ARRAY_SIZE(vidmodes); i++)
		{
			vidmodes[i].button = ui_ButtonCreate(vidmodes[i].label, vidmodes[i].xpos, 0, setVideoMode, (UserData)(intptr_t)i);
			ui_ExpanderAddLabel(group->expander, UI_WIDGET(vidmodes[i].button));
		}

		button = ui_ButtonCreate("?", 100, 0, budgetsHelp, (UserData)0);
		ui_ExpanderAddLabel(group->expander, UI_WIDGET(button));
		ui_WidgetSetPositionEx(UI_WIDGET(button), 0, 0, 0, 0, UITopRight);

		ui_RebuildableTreeAddLabel(group, "CPU", NULL, true);
		
		{
			UIRTNode * pCPUGroupNode,*pAnimGroupNode;
			// CPU group
			pCPUGroupNode = ui_RebuildableTreeAddGroup(group, "Total (Real)", "CPU", false, NULL);
			budget_data.bar.cpu_us = addBar(pCPUGroupNode, true, "CPU_Real", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pCPUGroupNode, "Total (Dual-core effective)", NULL, true);
			budget_data.bar.cpu_dual_core_effective_us = addBar(pCPUGroupNode, false, "CPU_Dual", EBarType_Double, DoubleText_None, MAX_CPU_US);

			ui_RebuildableTreeAddLabel(pCPUGroupNode, "UI", NULL, true);
			budget_data.bar.ui_us = addBar(pCPUGroupNode, false, "UI", EBarType_Double, DoubleText_None, MAX_CPU_US);
			//ui_RebuildableTreeAddLabel(pCPUGroupNode, "Real Anim", NULL, true);
			pAnimGroupNode = ui_RebuildableTreeAddGroup(pCPUGroupNode, "Real Anim", "Anim", true, NULL);
			budget_data.bar.anim_us = addBar(pAnimGroupNode, true, "Anim", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pAnimGroupNode, "Total Cloth", NULL, true);
			budget_data.bar.cloth_us = addBar(pAnimGroupNode, false, "Cloth", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pAnimGroupNode, "Total Skel", NULL, true);
			budget_data.bar.skel_us = addBar(pAnimGroupNode, false, "Skel", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pCPUGroupNode, "FX", NULL, true);
			budget_data.bar.fx_us = addBar(pCPUGroupNode, false, "FX", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pCPUGroupNode, "Sound", NULL, true);
			budget_data.bar.sound_us = addBar(pCPUGroupNode, false, "Sound", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pCPUGroupNode, "Physics", NULL, true);
			budget_data.bar.physics_us = addBar(pCPUGroupNode, false, "Physics", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pCPUGroupNode, "Draw", NULL, true);
			budget_data.bar.draw_us = addBar(pCPUGroupNode, false, "Draw", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pCPUGroupNode, "WorldTrav", NULL, true);
			budget_data.bar.worldtrav_us = addBar(pCPUGroupNode, false, "WorldTrav", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pCPUGroupNode, "Other", NULL, true);
			budget_data.bar.misc_cpu_us = addBar(pCPUGroupNode, false, "Other", EBarType_Double, DoubleText_None, MAX_CPU_US);
		}

		ui_RebuildableTreeAddLabel(group, "GPU", NULL, true);
		{
			UIRTNode * pGPUGroupNode;
			// GPU group
			pGPUGroupNode = ui_RebuildableTreeAddGroup(group, "Total", "GPU", false, NULL);
			budget_data.bar.gpu_us = addBar(pGPUGroupNode, true, "GPU", EBarType_Double, DoubleText_None, MAX_CPU_US);

			ui_RebuildableTreeAddLabel(pGPUGroupNode, "Shadows", NULL, true);
			budget_data.bar.gpu_shadows_us = addBar(pGPUGroupNode, false, "Shadows", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "ZPrePass", NULL, true);
			budget_data.bar.gpu_zprepass_us = addBar(pGPUGroupNode, false, "ZPrePass", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "Opaque One-Pass", NULL, true);
			budget_data.bar.gpu_opaque_onepass_us = addBar(pGPUGroupNode, false, "Opaque One-Pass", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "Shadow Buffer (ssao)", NULL, true);
			budget_data.bar.gpu_shadow_buffer_us = addBar(pGPUGroupNode, false, "Shadow Buffer (ssao)", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "Opaque", NULL, true);
			budget_data.bar.gpu_opaque_us = addBar(pGPUGroupNode, false, "Opaque", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "Alpha", NULL, true);
			budget_data.bar.gpu_alpha_us = addBar(pGPUGroupNode, false, "Alpha", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "PostProcess", NULL, true);
			budget_data.bar.gpu_postprocess_us = addBar(pGPUGroupNode, false, "PostProcess", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "2D", NULL, true);
			budget_data.bar.gpu_2d_us = addBar(pGPUGroupNode, false, "2D", EBarType_Double, DoubleText_None, MAX_CPU_US);
			ui_RebuildableTreeAddLabel(pGPUGroupNode, "Other", NULL, true);
			budget_data.bar.gpu_other_us = addBar(pGPUGroupNode, false, "Other", EBarType_Double, DoubleText_None, MAX_CPU_US);
		}

		ui_AutoWidgetAddButton(group, "?", showDetails, (UserData)"Scene Objs", true, "Show detailed information for this module", NULL);
		ui_RebuildableTreeAddLabel(group, "Scene Objs", NULL, false);
		budget_data.bar.object_scene_count = addBar(group, false, "Scene Objs", false, DoubleText_None, OBJECT_SCENE_BUDGET);
		ui_RebuildableTreeAddLabel(group, "World Objs", NULL, true);
		budget_data.bar.object_world_count = addBar(group, false, "World Objs", false, DoubleText_None, OBJECT_WORLD_BUDGET);
	/*	ui_RebuildableTreeAddLabel(group, "Character Objs", NULL, true);
		budget_data.bar.object_character_count = addBar(group, false, "Character Objs", false, DoubleText_None, OBJECT_CHARACTER_BUDGET);
		ui_RebuildableTreeAddLabel(group, "FX Objs", NULL, true);
		budget_data.bar.object_fx_count = addBar(group, false, "FX Objs", false, DoubleText_None, OBJECT_FX_BUDGET);*/

		{
			UIRTNode *triangles;
			VSCost costs[VS_COST_SIZE];
			FrameCounts counts;
			// Triangles group
			triangles = ui_RebuildableTreeAddGroup(group, "Triangles", "Triangles", false, NULL);
			budget_data.bar.tri_ms = addBar(triangles, true, "TriMS", EBarType_Single, DoubleText_None, 0);
		
			gfxGetFrameCounts(&counts);
			gfxGetApproxPassVertexShaderTime(&counts.draw_list_stats.pass_stats[RDRSHDM_VISUAL], costs, false, budget_data.cur_videmode_is_old);

			for (i=0; i<ARRAY_SIZE(costs); i++) {
				char buf[64];
				ui_RebuildableTreeAddLabel(triangles, costs[i].name, NULL, true);
				sprintf(buf, "Triangles:%s", costs[i].name);
				addBarStatic(triangles, allocAddString(buf), false, EBarType_Single, DoubleText_InView, NO_BUDGET);
			}
		}
		ui_RebuildableTreeAddLabel(group, "Total Objs Drawn", NULL, true);
		budget_data.bar.object_total_count = addBar(group, false, "Total Objs Drawn", EBarType_Single, DoubleText_None, OBJECT_TOTAL_BUDGET);
		ui_RebuildableTreeAddLabel(group, "World Animations", NULL, true);
		budget_data.bar.world_animation_count = addBar(group, false, "World Animations", EBarType_Single, DoubleText_None, WORLD_ANIMATION_BUDGET);

		{
			// Cloth group.
			UIRTNode *cloth = ui_RebuildableTreeAddGroup(group, "Cloth", "Cloth", false, NULL);
			budget_data.bar.cloth_total = addBar(cloth, true, "ClothTotal", EBarType_Single, DoubleText_None, CLOTH_BUDGET);

			ui_RebuildableTreeAddLabel(cloth, "Collisions", NULL, true);
			budget_data.bar.cloth_collisions  = addBar(cloth, false, "Collisions", EBarType_Single, DoubleText_None, NO_BUDGET);

			ui_RebuildableTreeAddLabel(cloth, "Constraints", NULL, true);
			budget_data.bar.cloth_constraints = addBar(cloth, false, "Constraints", EBarType_Single, DoubleText_None, NO_BUDGET);

			ui_RebuildableTreeAddLabel(cloth, "Particles", NULL, true);
			budget_data.bar.cloth_particles   = addBar(cloth, false, "Particles",  EBarType_Single, DoubleText_None, NO_BUDGET);
		}

		ui_AutoWidgetAddButton(group, "?", showDetails, (UserData)"Unique Tmplts", true, "Show detailed information for this module", NULL);
		ui_RebuildableTreeAddLabel(group, "Unique Tmplts", NULL, false);
		budget_data.bar.template_count = addBar(group, false, "Unique Tmplts", EBarType_Single, DoubleText_None, SHADER_GRAPH_BUDGET);
		ui_AutoWidgetAddButton(group, "?", showDetails, (UserData)"Unique Mtls", true, "Show detailed information for this module", NULL);
		ui_RebuildableTreeAddLabel(group, "Unique Mtls", NULL, false);
		budget_data.bar.material_count = addBar(group, false, "Unique Mtls", EBarType_Single, DoubleText_None, MATERIAL_BUDGET);

	}
	group = ui_RebuildableTreeAddGroup(budget_data.uirt->root, "Memory", "Memory", true, NULL);
	{
		UIRTNode *world, *audio;

#define ADD_BAR_SAVE(bar, parent, cat)  \
			ui_AutoWidgetAddButton(parent, "?", showDetails, (UserData)cat, true, "Show detailed information for this module", NULL);	\
			ui_RebuildableTreeAddLabel(parent, cat, NULL, false);	\
			bar = addBar(parent, false, cat, EBarType_Notch, DoubleText_InView, 0);
#define ADD_BAR_NAMED(parent, prefix, cat, name)	\
			ui_AutoWidgetAddButton(parent, "?", showDetails, (UserData)prefix ":" cat, true, "Show detailed information for this module", NULL);	\
			ui_RebuildableTreeAddLabel(parent, name, NULL, false);	\
			addBarStatic(parent, prefix ":" cat, false, EBarType_Notch, DoubleText_InView, 0);
#define ADD_BAR(parent, prefix, cat) ADD_BAR_NAMED(parent, prefix, cat, prefix)

		ui_AutoWidgetAddButton(group, "?", showDetails, (UserData)"Textures", true, "Show detailed information for this module", NULL);
		ui_RebuildableTreeAddLabel(group, "Textures (reduced)", NULL, false);
		budget_data.bar.textures.total = addBar(group, false, "TexturesReduced", EBarType_Notch, DoubleText_InView, 0);
		ui_AutoWidgetAddButton(group, "?", showDetails, (UserData)"Textures", true, "Show detailed information for this module", NULL);
		ui_RebuildableTreeAddLabel(group, "Textures (original)", NULL, false);
		budget_data.bar.textures.totalOrig = addBar(group, false, "TexturesOriginal", EBarType_Notch, DoubleText_InView, 0);
		ui_AutoWidgetAddButton(group, "?", showDetails, (UserData)"Geometry", true, "Show detailed information for this module", NULL);
		ui_RebuildableTreeAddLabel(group, "Geometry", NULL, false);
		budget_data.bar.geometry.total = addBar(group, false, "Geometry", EBarType_Notch, DoubleText_InView, 0);

		world = ui_RebuildableTreeAddGroup(group, "World Geo", "World", false, NULL);
		{
			addBarStatic(world, "World", true, EBarType_Notch, DoubleText_InView, 0);
			ADD_BAR_NAMED(world, "Geometry", "World", "World Geo");
			ui_RebuildableTreeAddLabel(world, "w/o HighDetail", NULL, true);
			addBarStatic(world, "w/o HighDetail", false, EBarType_Single, DoubleText_None, 0);
			ADD_BAR_NAMED(world, "Textures", "Terrain", "TerrainTex");
			ADD_BAR_NAMED(world, "Geometry", "Terrain", "TerrainGeo");
		}
		ADD_BAR_NAMED(group, "Textures", "World", "World Tex");

		ADD_BAR_NAMED(group, "Textures", "Character", "Character Tex");
		ADD_BAR_NAMED(group, "Geometry", "Character", "Character Geo");

		ADD_BAR_NAMED(group, "Textures", "FX", "FX Tex");
		ADD_BAR_NAMED(group, "Geometry", "FX", "FX Geo");

		ADD_BAR_NAMED(group, "Textures", "UI", "UI Tex");
		ADD_BAR_NAMED(group, "Textures", "Fonts", "Font Tex");

		audio = ui_RebuildableTreeAddGroup(group, "Audio", "Audio", false, NULL);
		{
			ADD_BAR_SAVE(budget_data.bar.audio, audio, "AudioDetail");
		}
	}

	// Dynamic budgets
	{
		UIRTNode *other;
		MemoryBudget **budgets = memBudgetGetBudgets();
		UIButton *button=NULL;

		other = ui_RebuildableTreeAddGroup(group, "Global", "Global", false, NULL);

#if !PLATFORM_CONSOLE
		button = ui_ButtonCreate("Update Working Set", 100, 0, updateWorkingSet, (UserData)0);
		ui_ExpanderAddLabel(other->expander, UI_WIDGET(button));
#endif


		FOR_EACH_IN_EARRAY_FORWARDS(budgets, MemoryBudget, budget)
		{
			BudgetBar *bar;
			ui_AutoWidgetAddButton(other, "?", showDetails, (UserData)budget->module, true, "Show detailed information for this module", NULL);
			if (stricmp(budget->module, "Textures:Misc")==0)
				ui_AutoWidgetAddButton(other, "?", showDetails, (UserData)"Textures:Util", false, "Show detailed information for this module", NULL);
			ui_RebuildableTreeAddLabel(other, budget->module, NULL, false);
			bar = calloc(sizeof(*bar),1);
			*bar = addBar(other, false, budget->module, false, DoubleText_WorkingSet, 0);
			eaPush(&budget_data.bars_dynamic, bar);
		}
		FOR_EACH_END;
	}
	group = ui_RebuildableTreeAddGroup(budget_data.uirt->root, "Design", "Design", true, NULL);
	{
		ui_RebuildableTreeAddLabel(group, "Entities Spawned", NULL, true);
		budget_data.bar.spawn_total_count = addBar(group, false, "Entities Spawned", EBarType_Single, DoubleText_None, SPAWN_BUDGET);
		ui_RebuildableTreeAddLabel(group, "Active Encounters", NULL, true);
		budget_data.bar.running_encounters = addBar(group, false, "Active Encounters", EBarType_Single, DoubleText_None, NO_BUDGET);
		ui_RebuildableTreeAddLabel(group, "Spawned FSM Cost", NULL, true);
		budget_data.bar.spawned_fsm_cost = addBar(group, false, "Spawned FSM Cost", EBarType_Single, DoubleText_None, FSM_BUDGET);
		ui_RebuildableTreeAddLabel(group, "Placed FSM Cost", NULL, true);
		budget_data.bar.potential_fsm_cost = addBar(group, false, "Placed FSM Cost", EBarType_Single, DoubleText_None, FSM_BUDGET);
	}
	{
		UIRTNode *options;
		UIAutoWidgetParams params = {0};

		options = ui_RebuildableTreeAddGroup(budget_data.uirt->root, "Options", "Options", false, NULL);
		params.type = AWT_Slider;
		ui_AutoWidgetAdd(options, parse_BudgetData, "Opacity", &budget_data, true, elbOnDataChanged, NULL, &params, NULL);
		ui_RebuildableTreeAddLabel(options, "Details ", NULL, true);
		params.min[0] = 0.25f;
		params.max[0] = 2.0f;
		ui_AutoWidgetAdd(options, parse_BudgetData, "Scale", &budget_data, false, elbOnDataChanged, NULL, &params, NULL);

		ui_AutoWidgetAddAllFlags(options, parse_BudgetData, "AlertOn", &budget_data, true, elbOnDataChanged, NULL, NULL, NULL);
	}


	ui_RebuildableTreeDoneBuilding(budget_data.uirt);

	editLibBudgetsUpdate();
	editLibBudgetsOpenCloseExpanders();
}

void editLibSetEncounterBudgets(U32 numSpawnedEnts, U32 numActiveEncs, U32 numTotalEncs, U32 spawnedFSMCost, U32 potentialFSMCost)
{
	encBudgetValues.numRunningEncs = numActiveEncs;
	encBudgetValues.numEncounters = numTotalEncs;
	encBudgetValues.numSpawnedEnts = numSpawnedEnts;
	encBudgetValues.spawnedFSMCost = spawnedFSMCost;
	encBudgetValues.potentialFSMCost = potentialFSMCost;
}

static void showBudgetWindow(UIAnyWidget *widget_UNUSED, const char *module) {
	editLibBudgetsShow();
}

void ui_BudgetButtonDraw(UIButton *button, UI_PARENT_ARGS)
{
	UI_GET_COORDINATES(button);
	UIDrawingDescription desc = { 0 };
	const char* buttonText = ui_WidgetGetText( UI_WIDGET( button ));
	Color c;
	FrameCounts counts;

	desc.styleBorderNameUsingLegacyColor = "Default_Capsule_Filled";
	UI_DRAW_EARLY(button);
	gfxGetFrameCounts(&counts);
	c = colorFromRatio(counts.objects_in_scene / OBJECT_SCENE_BUDGET, true);

	ui_DrawingDescriptionDraw( &desc, &box, scale, z, 255, c, ColorBlack );

	if (buttonText)
	{
		UIStyleFont *pFont = ui_WidgetGetFont(UI_WIDGET(button));
		ui_StyleFontUse(pFont, false, UI_WIDGET(button)->state);
		gfxfont_Printf(x + w/2, y + h/2, z + 0.01f, scale, scale, CENTER_XY, "%s", buttonText);
	}
	UI_DRAW_LATE(button);
}

AUTO_COMMAND ACMD_NAME(budget_Button);
void editLibSmallBudgetShow(void)
{
	if (budgetButton && UI_WIDGET(budgetButton)->group) {
		ui_WidgetRemoveFromGroup(UI_WIDGET(budgetButton));
		return;
	}

	if (!budgetButton)
		budgetButton = ui_ButtonCreate("Budget",1370,25,showBudgetWindow,NULL);
	UI_WIDGET(budgetButton)->drawF = ui_BudgetButtonDraw;
	ui_WidgetAddToDevice(UI_WIDGET(budgetButton),NULL);
}
