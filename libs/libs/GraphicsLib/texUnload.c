#include "texUnload.h"
#include "GfxTextures.h"
#include "GraphicsLibPrivate.h"
#include "GfxDebug.h"
#include "sysutil.h"
#include "memlog.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "MemoryPoolDebug.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Textures_Misc););

static int dynamicUnloadEnabled=0;
static bool tex_unload_just_applied_dynamic;

void texDynamicUnload(TexUnloadMode enable) {
	dynamicUnloadEnabled = enable;
}

TexUnloadMode texDynamicUnloadEnabled(void) {
	return (TexUnloadMode)dynamicUnloadEnabled;
}

static float tex_unload_scale = 1;

// Scale the time necessary to unload a texture.
AUTO_CMD_FLOAT( tex_unload_scale, tex_unload_scale ) ACMD_CMDLINE ACMD_CATEGORY(Debug) ACMD_ACCESSLEVEL(0);

// Ideally, we would never aggressively evict textures, but we do currently so that the art budget is "correct" most of the time.
// We need to fix the budgeting code to never require this.  -1 will mean "use the default for the production mode state"
static int tex_aggressive_evict = -1;
AUTO_CMD_INT( tex_aggressive_evict, tex_aggressive_evict ) ACMD_CMDLINE ACMD_CATEGORY(Graphics);

// Define the age of textures that can be unloaded
#define AGE_MULT ((tex_unload_scale>0?tex_unload_scale:1)*(gfx_state.reduce_mip_world?2:1)) // If we're taking less memory, keep them around longer
#define MAX_AGE_TO_BE_UNLOADED	(timerCpuSpeed()*tex_seconds_before_unload*AGE_MULT)	 // in the same units as last_used_timestamp (CPU ticks)
static int tex_seconds_before_unload = 60; // Number of seconds a texture must not have been drawn before it can be unloaded

static U32 tex_memory_unload_threshold = 384 * 1024 * 1024;
AUTO_CMD_INT(tex_memory_unload_threshold, tex_memory_unload_threshold) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

// Limits the total use of texture memory as a fail-safe against fatal allocation errors.
// Gates loading of new textures when the total memory loaded and in-use for loading is above
// this threshold. This value is initialized gfxStartupPreWorldLib, or by the command-line.
U32 tex_memory_allowed = 0;
AUTO_CMD_INT(tex_memory_allowed, tex_memory_allowed) ACMD_ACCESSLEVEL(0) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

#define RAW_UNLOAD_TIME (MAX_AGE_TO_BE_UNLOADED/4) // Free these fairly aggressively, as they're rarely reused

#define REDUCE_SLACK_TIME_1 10 // seconds before allowing a texture to be reduced again, changing 1 level
#define REDUCE_SLACK_TIME_2 5 // seconds before allowing a texture to be reduced again, 2 levels
#define REDUCE_SLACK_TIME_3 3 // seconds before allowing a texture to be reduced again, 3 or more levels
#define REDUCE_SLACK_TIME_FX_MULT 20 // multiplier to apply if this is FX and down-resing

static U32 ageByTexBind(const BasicTexture *bind) // higher gets unloaded after more time
{
	if (bind->use_category & WL_FOR_PREVIEW_INTERNAL) {
		return timerCpuSpeed()*0.1*AGE_MULT; // Only 0.1 seconds for internal headshot textures
	}
	if (bind->use_category & WL_FOR_UI) {
		if (bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO] >= 256*1024) {
			return timerCpuSpeed()*2*AGE_MULT; // Low tolerance for large UI textures
		}
		return MAX_AGE_TO_BE_UNLOADED;
	}
	if (bind->loaded_data->tex_memory_use[TEX_MEM_VIDEO] >= 1*1024*1024) {
		return MAX_AGE_TO_BE_UNLOADED/2;
	}
	if (bind->use_category & WL_FOR_UTIL) return MAX_AGE_TO_BE_UNLOADED * 10;
	if (bind->use_category & WL_FOR_FX) return MAX_AGE_TO_BE_UNLOADED * 100;
	if (bind->use_category & WL_FOR_NOTSURE) return MAX_AGE_TO_BE_UNLOADED * 2;
	if (bind->use_category & WL_FOR_ENTITY) return MAX_AGE_TO_BE_UNLOADED * 2;
	if (bind->use_category & WL_FOR_WORLD) return MAX_AGE_TO_BE_UNLOADED;
	return MAX_AGE_TO_BE_UNLOADED;
}

static bool unloadCriteria(BasicTexture *bind)
{
	U32 delta = gfx_state.client_frame_timestamp - bind->tex_last_used_time_stamp;
	if (delta >= ageByTexBind(bind) || dynamicUnloadEnabled >= TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL)
		return true;
	return false;
}

static bool shouldNotReduce(BasicTexture *bind)
{
	if ((bind->has_rare && (texGetTexWord(bind) || texGetDynamicReferenceCount(bind))) ||
		(bind->flags & (TEX_VOLATILE_TEXGEN|TEX_TEXGEN|TEX_DYNAMIC_TEXWORD|TEX_SCRATCH)) ||
		(bind->bt_texopt_flags & (TEXOPT_NOMIP)) ||
		(bind->use_category & (WL_FOR_FONTS)))
		return true;

	return false;
}

U32 texDesiredLevels(BasicTexture *bind, float distance, float uv_density, const GfxRenderAction *action)
{
	F32 levelsf;
	int max_levels = (int)(gfx_state.no_htex ? bind->base_levels : bind->max_levels);
	int ideal_levels;
	int post_reduce;
	float log_aspect = 0.0f;

	assert(bind->name != (const char*)MEMORYPOOL_SENTINEL_VALUE);

	if (shouldNotReduce(bind)) {
		return max_levels;
	}

	// Calculate the number of levels needed based on the distance, rendertarget/projection, and uv density.
	// Assumes that any run-time modifications to uv_density have already been applied.

	if (bind->width != bind->height) {
		// Non-square textures require adjustment since density is calculated based on uvs, not texels.
		// TODO: maybe cache this? For most textures it will wind up being 0, so is probably a waste to
		// store for every texture.
		log_aspect = log2f(MAX(bind->width, bind->height) / (float) MIN(bind->width, bind->height));
	}

	if (action) {
		levelsf = action->texReduceResolutionScale - uv_density + log_aspect;
		if (action->cameraView->projection_matrix[2][3] != 0.0f && distance > 0.0f) {
			levelsf -= log2f(distance);
		}
	} else {
		levelsf = max_levels;
	}

    ideal_levels = round(ceilf(levelsf));
    ideal_levels = CLAMP(ideal_levels, 1, max_levels);

	// apply any post-clamp reduction to force textures to ignore one or more of their levels
	{
		int settings_reduce = 0;

        // chop off mip levels based on the graphics settings
        if (gfx_state.reduce_mip_override) {
            settings_reduce = gfx_state.reduce_mip_override;
        } else if (bind->use_category & WL_FOR_ENTITY) {
            settings_reduce = gfx_state.reduce_mip_entity;
        } else if (bind->use_category != WL_FOR_UI) {
            // Used *only* by UI (must be !=, so that character emblems, etc, which are both entity and UI are not caught here)
            settings_reduce = gfx_state.reduce_mip_world;
        }
	
        post_reduce = settings_reduce;
	}

	ideal_levels = CLAMP(ideal_levels - post_reduce, 1, max_levels);

	return ideal_levels;
}

static bool unloadCriteriaRaw(BasicTexture *bind)
{
	U32 delta;
	const BasicTextureRareData *rare_data;

	if (!bind->has_rare)
		return false;

	// Must have rare data if raw is loaded
	rare_data = texGetRareDataConst(bind);
	assert(rare_data);

	if (rare_data->rawReferenceCount)
		return false; // Don't unload anything that has an outstanding request on it
	
	delta = gfx_state.client_frame_timestamp - rare_data->tex_last_used_time_stamp_raw; // unsigned math deals with wrapping correctly

	return dynamicUnloadEnabled >= TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL_RAW || (delta >= RAW_UNLOAD_TIME);
}

int texPartialSort(void)
{
	static int first_unloaded_texture_index;
	int num_textures = eaSize(&g_basicTextures);
	int last_loaded_texture_index=0;
	assert(g_basicTextures);
	// Make sure loaded_textures_index is correct
	while (true)
	{
		while (last_loaded_texture_index<num_textures && g_basicTextures[last_loaded_texture_index]->tex_is_loaded)
			last_loaded_texture_index++;
		// last_loaded now pointing at an unloaded texture, or off the array
		while (first_unloaded_texture_index>=0 && !g_basicTextures[first_unloaded_texture_index]->tex_is_loaded)
			first_unloaded_texture_index--;
		// first_unloaded now pointing at a loaded texture, or off the array
		if (last_loaded_texture_index < first_unloaded_texture_index)
		{
			SWAPP(g_basicTextures[first_unloaded_texture_index], g_basicTextures[last_loaded_texture_index]);
		} else {
			break;
		}
	}
	last_loaded_texture_index--;
	first_unloaded_texture_index++;

	// Could replace this whole thing with BasicTextures keeping an index of where they are in the array
	// so that we can swap whenever a texture gets loaded or unloaded automatically
	{
		static int sort_location=0;
		int divisions = tex_unload_just_applied_dynamic?4:60; // Once a second at ideal framerate
		int partial = num_textures / divisions;
		if (sort_location < first_unloaded_texture_index || sort_location >= num_textures)
			sort_location = first_unloaded_texture_index;
		for (; sort_location<num_textures && partial; sort_location++, partial--)
		{
			if (g_basicTextures[sort_location]->tex_is_loaded)
			{
				SWAPP(g_basicTextures[first_unloaded_texture_index], g_basicTextures[sort_location]);
				first_unloaded_texture_index++;
			}
		}
	}
	return first_unloaded_texture_index;
}

void texUnloadTexturesToFitMemory(void)
{
	int removed_count, removed_raw_count, remove_cursor, num_textures;
	BasicTexture * bind;
	int remove_start=0, remove_end;
	int first_unloaded_texture_index;
	bool bAggressive, overMemory;
	num_textures = eaSize(&g_basicTextures);

	if (tex_aggressive_evict >= 0)
		bAggressive = tex_aggressive_evict;
	else
	{
		if (isProductionMode())
			bAggressive = 0;
		else
			bAggressive = 1;
	}

	if (dynamicUnloadEnabled == TEXUNLOAD_DISABLE)
		return;

	PERFINFO_AUTO_START("texUnloadTexturesToFitMemory", 1);

	overMemory = texOverMemory(0);
	if (overMemory)
		texUnloadAllNotUsedThisFrame();

	ANALYSIS_ASSUME(g_basicTextures);

	tex_unload_just_applied_dynamic = gfxSettingsJustAppliedDynamic();

	// Sort some loaded textures to the right location
	first_unloaded_texture_index = texPartialSort();

	// Only scan through the first first_unloaded_texture_index textures
	//  Roughly 1/30th of all textures are loaded in MIL, so we still want to do partial checking so we don't do that many at once
	// TODO: also need to update query for budgets to be smarter now that the basictexture list is moving around
	{
		static int unload_location=0;
		int divisions = tex_unload_just_applied_dynamic?1:8; // Only doing part of the important part of the list - will often take one more frame than this value
		// Just look through some each frame, ideally this should be split such that
		//  each texture gets looked at on the order of the amount of time a minimum
		//  unload timeout can occur.
		int count = first_unloaded_texture_index / divisions;
		remove_start = unload_location;
		remove_end = remove_start + count;
		if (remove_end >= first_unloaded_texture_index)
		{
			remove_end = first_unloaded_texture_index;
			unload_location = 0;
		} else
			unload_location = remove_end;
	}

	// Free textures that meet the criteria of being old or relatively old and large
	removed_count = 0;
	removed_raw_count = 0;

	if (dynamicUnloadEnabled >= TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL_RAW)
	{
		remove_start = 0;
		remove_end = eaSize(&g_basicTextures);
	}

	for (remove_cursor = remove_start; remove_cursor < remove_end; remove_cursor++)
	{
		U32 cur_levels, ideal_levels, allowedTimeDiff;
		int leveldiff;
		bool bAllowUnloads = true;

		if (!bAggressive)
		{
			if (texMemoryUsage[TEX_MEM_VIDEO] <= tex_memory_unload_threshold)
				bAllowUnloads = false;
		}

		bind = g_basicTextures[remove_cursor];

		if (bind == white_tex) // Never free white
			continue;
		if (!bind->loaded_data)
			continue;
		if (bind->loaded_data->loading)
			continue;
		if (gfx_state.client_frame_timestamp - bind->loaded_data->tex_reduce_last_time < REDUCE_SLACK_TIME_1) {
			continue; // Leave it alone
		}

        cur_levels = bind->loaded_data->levels_loaded;
        ideal_levels = bind->max_desired_levels;
        leveldiff = (int)ideal_levels - (int)cur_levels;

		allowedTimeDiff = timerCpuSpeed()*((ABS(leveldiff)==1)?REDUCE_SLACK_TIME_1:
			(leveldiff==2)?REDUCE_SLACK_TIME_2:
			REDUCE_SLACK_TIME_3);
		if (tex_unload_just_applied_dynamic)
			allowedTimeDiff = timerCpuSpeed()*1;

		// ideal is different, reset it to current unless an amount of time has expired
		if (ideal_levels < cur_levels && (bind->use_category & WL_FOR_FX))
		{
			// down-resing and it's FX, usea much longer delay
			allowedTimeDiff *= REDUCE_SLACK_TIME_FX_MULT;
		}

        if (gfx_state.client_frame_timestamp - bind->loaded_data->tex_reduce_last_time < allowedTimeDiff) {
            continue; // Leave it alone
        }
		bind->loaded_data->tex_reduce_last_time = gfx_state.client_frame_timestamp;

		if (bind->tex_is_loaded & RAW_DATA_BITMASK)
		{
			assert(bind->loaded_data);
			// This texture has raw data loaded
			if (bAllowUnloads && unloadCriteriaRaw(bind))
			{
				memlog_printf(texGetMemLog(), "%u: Unloading RAW texture %45s, age of %5.2f, cat %d", gfx_state.client_frame_timestamp, bind->name, (float)(gfx_state.client_frame_timestamp - texGetRareDataConst(bind)->tex_last_used_time_stamp_raw)/timerCpuSpeed(), bind->use_category);
				texFree(bind, 1);
				removed_raw_count++;
			}
		} else if (bind->tex_is_loaded & NOT_RAW_DATA_BITMASK) {
			assert(bind->loaded_data);
			if (unloadCriteria(bind))
			{
				// unload case
				if (bAllowUnloads && dynamicUnloadEnabled != TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL_RAW)
				{
					memlog_printf(texGetMemLog(), "%u: Unloading texture %45s, age of %5.2f, cat %d", gfx_state.client_frame_timestamp, bind->name, (float)(gfx_state.client_frame_timestamp - bind->tex_last_used_time_stamp)/timerCpuSpeed(), bind->use_category);
					texFree(bind, 0);
					removed_count++;
				}
			}
			else if (leveldiff && texLoadsPending(true) <= 20) // Don't change reduction levels if we're busy loading other things!
			{
				// reduce or up-res case
				// NOT shouldNotReduce means "may reduce"
				bool bAllowReduce = bAllowUnloads && !shouldNotReduce(bind);
				if ((bAllowReduce || leveldiff > 0) && (!bind->has_rare || !texGetTexWord(bind)))
				{
					memlog_printf(texGetMemLog(), "%u: Changing loaded texture %45s's resolution to %d from %d density = %1.2f, distance = %1.2f",
                        gfx_state.client_frame_timestamp, bind->name, ideal_levels, bind->loaded_data->levels_loaded, bind->loaded_data->uv_density, bind->loaded_data->min_draw_dist);

					// this can do a load/increase as well as a reduce
					gfxTexReduce(bind, ideal_levels);
				}
			}
        }
		else if (dynamicUnloadEnabled < TEXUNLOAD_ENABLE_FORCE_UNLOAD_ALL_RAW)
		{
			assert(!bind->loaded_data);
			// // All textures from here on out are not loaded, because we sorted them
			// break;
		}
	}

	if (removed_count)
		memlog_printf(texGetMemLog(), "Dynamically freed %d textures, %d RAW textures", removed_count, removed_raw_count);

	PERFINFO_AUTO_STOP();
}

void texUnloadAllNotUsedThisFrame(void)
{
	int num_textures = eaSize(&g_basicTextures);
	int remove_cursor;
	for (remove_cursor = 0; remove_cursor < num_textures; remove_cursor++)
	{
		BasicTexture *bind = g_basicTextures[remove_cursor];
		if (bind == white_tex) // Never free white
			continue;
		if (bind->tex_is_loaded & RAW_DATA_BITMASK)
		{
			if (texGetRareDataConst(bind)->tex_last_used_time_stamp_raw != gfx_state.client_frame_timestamp)
			{
				memlog_printf(texGetMemLog(), "%u: Unloading RAW texture %45s, age of %5.2f, cat %d", gfx_state.client_frame_timestamp, bind->name, (float)(gfx_state.client_frame_timestamp - texGetRareDataConst(bind)->tex_last_used_time_stamp_raw)/timerCpuSpeed(), bind->use_category);
				texFree(bind, 1);
			}
		}
		if (bind->tex_is_loaded & NOT_RAW_DATA_BITMASK) {
			if (bind->tex_last_used_time_stamp != gfx_state.client_frame_timestamp)
			{
				memlog_printf(texGetMemLog(), "%u: Unloading texture %45s, age of %5.2f, cat %d", gfx_state.client_frame_timestamp, bind->name, (float)(gfx_state.client_frame_timestamp - bind->tex_last_used_time_stamp)/timerCpuSpeed(), bind->use_category);
				texFree(bind, 0);
			}
		}
	}
}
