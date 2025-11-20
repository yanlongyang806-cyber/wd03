#include "wlTerrainSource.h"
#include "wlTerrainBrush.h"
#include "wlTerrainErode.h"
#include "wlTerrainQueue.h"

#include "WorldLib.h"
#include "WorldGrid.h"

#include "earray.h"
#include "rand.h"
#include "tga.h"
#include "timing.h"
#include "ScratchStack.h"
#include "LineDist.h"
#include "FolderCache.h"
#include "MemAlloc.h"
#include "wlState.h"
#include "StringCache.h"
#include "sysutil.h"

#if !PLATFORM_CONSOLE
#include <psapi.h>
#endif

#include "wlTerrainBrush_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#ifndef NO_EDITORS

typedef struct TerrainBrushFilterCache
{
	bool has_angle : 1;
	bool has_path : 1;
	bool has_blob : 1;
	F32 angle;
	F32 path_min_distance;
	F32 path_min_strength;
	F32 blob_min_distance;
} TerrainBrushFilterCache;

U32 terrainGetProcessMemory()
{
	return (U32)getProcessPageFileUsage();
}

void **terrain_leaked_buffers = NULL;

AUTO_COMMAND ACMD_HIDE;
void terrainIntentionallyLeakMemory(int size)
{
	void *buf;
	int count = 0;
	S64 alloc = ((S64)size)*1024*1024 - (S64)terrainGetProcessMemory();
	if (alloc > 0)
	{
		while (alloc > 100*1024*1024 && (buf = calloc_canfail(1, 100*1024*1024)))
		{
			count += 100;
			alloc -= 100*1024*1024;
			eaPush(&terrain_leaked_buffers, buf);
		}
		while (alloc > 10*1024*1024 && (buf = calloc_canfail(1, 10*1024*1024)))
		{
			count += 10;
			alloc -= 10*1024*1024;
			eaPush(&terrain_leaked_buffers, buf);
		}
		while (alloc > 1*1024*1024 && (buf = calloc_canfail(1, 1*1024*1024)))
		{
			count += 1;
			alloc -= 1*1024*1024;
			eaPush(&terrain_leaked_buffers, buf);
		}
		printf("Leaked %d megs of memory\n", count);
	}
}

AUTO_COMMAND ACMD_HIDE;
void terrainIntentionallyLeakAllMemory(int buffer_size)
{
	int count = 0;
	void *buf, *buffer = calloc_canfail(1, buffer_size*1024*1024);
	if (!buffer)
	{
		Alertf("Failed to allocate buffer!");
		return;
	}
	while (buf = calloc_canfail(1, 100*1024*1024))
	{
		count += 100;
		eaPush(&terrain_leaked_buffers, buf);
	}
	while (buf = calloc_canfail(1, 10*1024*1024))
	{
		count += 10;
		eaPush(&terrain_leaked_buffers, buf);
	}
	while (buf = calloc_canfail(1, 1024*1024))
	{
		count += 1;
		eaPush(&terrain_leaked_buffers, buf);
	}
	SAFE_FREE(buffer);
	printf("Leaked %d megs of memory\n", count);
}

AUTO_COMMAND ACMD_HIDE;
void terrainIntentiallyFreeLeakedMemory(void)
{
	eaDestroyEx(&terrain_leaked_buffers, NULL);
}

#define PERLIN_CNT 256
static U8 perlin_permutation[PERLIN_CNT];
static F32 perlin_gradients[PERLIN_CNT][2];
static bool perlin_inited = false;
static bool allow_long_ramps = false;

AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Terrain.AllowLongRamps");
void terrainAllowLongRamps(bool allow)
{
	allow_long_ramps = allow;
}

//Fills in the filename_no_path of the sky during load
AUTO_FIXUPFUNC;
TextParserResult terrainMultiBrushFixupFunc(TerrainMultiBrush *brush, enumTextParserFixupType eType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];
			getFileNameNoExt(name, brush->filename);
			brush->name = allocAddString(name);
		}
	}
	return ret;
}

void terrainBrushInit()
{
	U32 brush_count = 0;

	if (RefSystem_GetDictionaryHandleFromNameOrHandle(DEFAULT_BRUSH_DICTIONARY) == NULL)
	{
		//Load Default Brushes
		DictionaryHandle default_brush_dict;
		loadstart_printf("Loading Default Brushes...");
		default_brush_dict = RefSystem_RegisterSelfDefiningDictionary( DEFAULT_BRUSH_DICTIONARY, false, parse_TerrainDefaultBrush, true, false, NULL );
		ParserLoadFilesToDictionary( "editors/terrain", ".dbrush", "DefaultBrushes.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, default_brush_dict );
		brush_count = RefSystem_GetDictionaryNumberOfReferents( default_brush_dict );
		loadend_printf( "done (%d Brushes).", brush_count );
	}

	if (RefSystem_GetDictionaryHandleFromNameOrHandle(MULTI_BRUSH_DICTIONARY) == NULL)
	{
		//Load Multi Brushes
		DictionaryHandle multi_brush_dict;
		loadstart_printf("Loading Multi Brushes...");
		multi_brush_dict = RefSystem_RegisterSelfDefiningDictionary( MULTI_BRUSH_DICTIONARY, false, parse_TerrainMultiBrush, true, false, NULL );
		resDictMaintainInfoIndex(multi_brush_dict, ".name", NULL, NULL, NULL, NULL);
		ParserLoadFilesToDictionary( "editors/terrain/brushes", ".brush", "MultiBrushes.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, multi_brush_dict );
		brush_count = RefSystem_GetDictionaryNumberOfReferents( multi_brush_dict );
		loadend_printf( "done (%d Brushes).", brush_count );
	}
}

// Brush falloff function

__forceinline static F32 getBrushFalloff( bool square, F32 dist, F32 dx, F32 dy, F32 size, TerrainBrushFalloffTypes falloff_type)
{
	if (square)
		dist = MAX(fabs(dx), fabs(dy));
	
	if (dist >= 1.f) 
		return 0.f;
	if (dist > size)
	{
		F32 value = ((dist - size)/(1.0f - size));
		switch(falloff_type)
		{
		case TE_FALLOFF_SCURVE:
			return cosf( value * PI )*0.5 + 0.5;
		case TE_FALLOFF_LINEAR:
			return 1.0f - value;
		case TE_FALLOFF_CONVEX:
			return 1.0f - SQR(value);
		case TE_FALLOFF_CONCAVE:
			return SQR(1.0f - value);
		}
	}
	return 1.0f;
}

// 1D Gaussian filter
// function is clamped to zero outside of three sigma

__forceinline static F32 getSmoothingWeight( F32 x, F32 radius )
{
	if (fabsf(x) < radius)
		return powf(2.71828, -SQR(x) / (2* SQR(radius/3)));
	else
		return 0;
}

// BRUSH FUNCTIONS

void terEdRotateAndPositionImage(TerrainBrushState* state, TerrainBrushValues *values, int x, int z)
{
	bool move_to_cursor = values->bool_3;
	bool random_rotate = values->bool_4;
	F32 scale = values->float_7;
	S32 width;
	S32 height;

	if(!values->image_ref || !values->image_ref->buffer)
		return;

	width = values->image_ref->width;
	height = values->image_ref->height;

	if(move_to_cursor)
	{
		values->float_1 = x;
		values->float_3 = z;
	}

	if(random_rotate)
	{
		values->float_5 = (2*PI * (state->per_draw_frame_rand_val%360)/360.0f) - PI;
	}
}

bool terEdGetBrushImageColor(TerrainBrushValues *values, Color *color, int x, int y, int z)
{
	F32 pos_x = values->float_1;
	F32 pos_y = values->float_2;
	F32 pos_z = values->float_3;
	F32 rot_p = values->float_4;
	F32 rot_y = values->float_5;
	F32 rot_r = values->float_6;
	F32 scale = values->float_7;
	S32 width;
	S32 height;
	bool tile = values->bool_1;
//	bool ignore_alpha = values->bool_2;
	U8 *image_point;
	Vec3 dot_pos;
	Vec3 temp_vec;
	IVec2 image_pos;
	Mat4 mat;
	Vec3 pyr;

	if(	!values->image_ref || !values->image_ref->buffer)
		return false;

	width = values->image_ref->width;
	height = values->image_ref->height;
	if(	scale == 0.f ||
		width <= 0 ||
		height <= 0	)
		return false;
	
	setVec3(pyr, rot_p, rot_y, rot_r);
	createMat3YPR(mat, pyr);

	setVec3(dot_pos, pos_x, pos_y, pos_z);
	scaleVec3(mat[0], -(width*scale/2.0f), temp_vec);
	addToVec3(temp_vec, dot_pos);
	scaleVec3(mat[2], -(height*scale/2.0f), temp_vec);
	addToVec3(temp_vec, dot_pos);
	dot_pos[0] = x - dot_pos[0];
	dot_pos[1] = y - dot_pos[1];
	dot_pos[2] = z - dot_pos[2];

	if(tile)
	{
		image_pos[0] = (int)(dotVec3(dot_pos, mat[0]) / scale) % width;
		image_pos[1] = (int)(dotVec3(dot_pos, mat[2]) / scale) % height;
		if(image_pos[0] < 0)
			image_pos[0] = width + image_pos[0];
		if(image_pos[1] < 0)
			image_pos[1] = height + image_pos[1];
	}
	else
	{
		image_pos[0] = (int)(dotVec3(dot_pos, mat[0]) / scale);
		image_pos[1] = (int)(dotVec3(dot_pos, mat[2]) / scale);
		if(	image_pos[0] >= width  ||
			image_pos[1] >= height ||
			image_pos[0] < 0  ||
			image_pos[1] < 0  )
			return false;
	}

	image_point = values->image_ref->buffer + ((image_pos[0] + image_pos[1]*width)*4);
	color->r = image_point[0];
	color->g = image_point[1];
	color->b = image_point[2];
	color->a = image_point[3];
	return true;
}


/* Public functions */

void terEdMultiFilterValue(TerrainBrushFilterBuffer *filter, S32 i, S32 j, U32 lod, F32 val) 
{
	U32 lod_diff = (GET_COLOR_LOD(lod) - filter->lod);
	assert(filter->buffer);
	if (filter->invert)
		val = 1.0f - val;
	filter->buffer[(i<<lod_diff) + (j<<lod_diff)*(filter->width)] *= val;
}

F32 terEdGetFilterValue(TerrainBrushFilterBuffer *filter, S32 i, S32 j, U32 lod) 
{
	U32 lod_diff = (GET_COLOR_LOD(lod) - filter->lod);
	return filter->buffer ? filter->buffer[(i<<lod_diff) + (j<<lod_diff)*(filter->width)] : 1.f;
}

void terEdApplyHeightAdd(OPTIMIZED_BRUSH_PARAMS)
{
	F32 height = values->float_1;
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;
	
	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	if(channel == TBC_Soil)
		terrainSourceDrawSoilDepth( source, x, z, state->visible_lod, (reverse ? -1.0 : 1.0)*height*filter_value*fall_off, cache );
	else
        terrainSourceDrawHeight(source, x, z, state->visible_lod, (reverse ? -1.0 : 1.0)*height*filter_value*fall_off, cache );
}

void terEdApplyHeightFlatten(OPTIMIZED_BRUSH_PARAMS)
{
    F32 height = values->float_1;
    F32 old_height;
    F32 filter_value;
    if(i%4 != 0 || j%4 != 0)
        return;

    if (reverse && start)
    {
        height = values->float_1 = state->brush_center_height;
    }
    
    filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
    if(channel == TBC_Soil)
    {
        if (terrainSourceGetSoilDepth( source, x, z, &old_height, cache))
            terrainSourceDrawSoilDepth( source, x, z, state->visible_lod, (height - old_height)*filter_value*fall_off, cache );
    }
    else
    {
        if (terrainSourceGetHeight( source, x, z, &old_height, cache))
            terrainSourceDrawHeight(source, x, z, state->visible_lod, (height - old_height)*filter_value*fall_off, cache );
    }
}

void terEdApplyHeightWeather(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	terrainErosionDrawThermalErosion( source, x, z, state->visible_lod, 0.02*filter_value*fall_off, values->float_1, values->float_2, cache);
}

void terEdApplyHeightGrab(OPTIMIZED_BRUSH_PARAMS)
{
	F32 height;
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;

	if(start)
	{
		source->lock_cursor_position = true;
		return;
	}

    height = state->vertical_offset;
	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	terrainSourceDrawHeight(source, x, z, state->visible_lod, height*filter_value*fall_off, cache );
}

void terEdApplyHeightSmudge(OPTIMIZED_BRUSH_PARAMS)
{
	int granularity = GET_COLOR_LOD(state->visible_lod);
	int si, sj;
	int sx, sz;
	static F32 *height_samples=NULL;
	static S32 sample_width=0;
	static S32 sample_height=0;
	static S32 last_x_offset=0;
	static S32 last_y_offset=0;
	F32 filter_value;
	F32 height;
	F32 old_height;

	if(i%4 != 0 || j%4 != 0)
		return;

	if(start && frame_start)
	{
		sample_width = ((filter->width)>>COLOR_LOD_DIFF) + 1;
		sample_height = ((filter->height)>>COLOR_LOD_DIFF) + 1;

		if(height_samples)
			free(height_samples);
		height_samples = terrainAlloc(sample_width*sample_height, sizeof(F32));
		if (!height_samples)
			return;

		for(sj=0; sj < sample_height ; sj++)
		{
			for(si=0; si < sample_width; si++)
			{
				sx = filter->x_offset + (si<<(granularity+COLOR_LOD_DIFF));
				sz = filter->y_offset + (sj<<(granularity+COLOR_LOD_DIFF));
				if (terrainSourceGetHeight( source, sx, sz, &height, cache))
					height_samples[si + sj*sample_width] = height;
			}
		}
		last_x_offset = filter->x_offset;
		last_y_offset = filter->y_offset;
		return;
	}
	else if(start || 
			sample_width  != ((filter->width )>>COLOR_LOD_DIFF) + 1 ||
			sample_height != ((filter->height)>>COLOR_LOD_DIFF) + 1 )
	{
		return;
	}

	assert(height_samples);
	
	if(frame_start)
	{
		F32 dist;
		Vec2 mov_vec;
		mov_vec[0] = filter->x_offset - last_x_offset;
		mov_vec[1] = filter->y_offset - last_y_offset;
		dist = lengthVec2(mov_vec);
		if(dist != 0)
		{
			//We do not want to sample from too far away
			if(dist > 20.0f)
			{
				last_x_offset = filter->x_offset - (mov_vec[0]*20.0f/dist);
				last_y_offset = filter->y_offset - (mov_vec[1]*20.0f/dist);
			}
			for(sj=0; sj < sample_height ; sj++)
			{
				for(si=0; si < sample_width; si++)
				{
					sx = last_x_offset + (si<<(granularity+COLOR_LOD_DIFF));
					sz = last_y_offset + (sj<<(granularity+COLOR_LOD_DIFF));
					if (terrainSourceGetHeight( source, sx, sz, &height, cache))
						height_samples[si + sj*sample_width] = height;
				}
			}
		}
		last_x_offset = filter->x_offset;
		last_y_offset = filter->y_offset;
	}

	si = i>>COLOR_LOD_DIFF;
	sj = j>>COLOR_LOD_DIFF;
	assert(si < sample_width && sj < sample_height);

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	height = height_samples[si + sj*sample_width];
	if (terrainSourceGetHeight( source, x, z, &old_height, cache))
		terrainSourceDrawHeight(source, x, z, state->visible_lod, (height - old_height)*filter_value*fall_off, cache );
}

void terEdApplyHeightRoughen(OPTIMIZED_BRUSH_PARAMS)
{
	F32 noise_size = values->float_1;
	F32 noise;
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	noise = noiseGetBand2D( x, z, noise_size );
	if(channel == TBC_Soil)
		terrainSourceDrawSoilDepth( source, x, z, state->visible_lod, noise*0.5*filter_value*fall_off, cache );
	else
        terrainSourceDrawHeight(source, x, z, state->visible_lod, noise*0.5*filter_value*fall_off, cache );
}

static F32 terEdGetTerraceHeight(S32 idx, F32 terrace_spacing, F32 terrace_randomization, F32 vertical_bias)
{
	return (idx * terrace_spacing) + randomF32Seeded(&idx, RandType_BLORN_Static)*terrace_randomization - vertical_bias;
}

void terEdApplyHeightTerrace(OPTIMIZED_BRUSH_PARAMS)
{
	F32 terrace_spacing = (values->float_1+values->float_2)*0.5f; // average, in feet
	F32 terrace_randomization = (values->float_2-values->float_1)*0.25f; // in feet
	F32 power = 0.25+0.75f*values->float_3; // 0-1 -> 0.25-1
	F32 x_delta = values->float_4;
	F32 z_delta = values->float_5;
	F32 vertical_bias;
	F32 filter_value, old_height;
	if(i%4 != 0 || j%4 != 0)
		return;

	vertical_bias = x*x_delta + z*z_delta;

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	if (terrainSourceGetHeight( source, x, z, &old_height, cache))
	{
		S32 height_idx;
		F32 current_height, next_height, dest_height, delta, delta_factor;

		height_idx = floor((old_height+vertical_bias+terrace_randomization)/terrace_spacing);
		current_height = terEdGetTerraceHeight(height_idx, terrace_spacing, terrace_randomization, vertical_bias);
		if (old_height >= current_height-0.01f)
		{
				dest_height = current_height;
				next_height = terEdGetTerraceHeight(height_idx+1, terrace_spacing, terrace_randomization, vertical_bias);
		}
		else
		{
				next_height = current_height;
				dest_height = terEdGetTerraceHeight(height_idx-1, terrace_spacing, terrace_randomization, vertical_bias);
		}
		delta_factor = (old_height-dest_height)/(next_height-dest_height);
		delta_factor = delta_factor * (1.f-delta_factor);
		delta = (dest_height-old_height)*delta_factor;
		terrainSourceDrawHeight(source, x, z, state->visible_lod, delta*filter_value*fall_off, cache );
	}
}

void terEdApplyColorSet(OPTIMIZED_BRUSH_PARAMS)
{
	Color color1 = values->color_1;
	Color color2 = values->color_2;
	bool use_inverse = values->bool_1;
	F32 filter_value;
	
	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	if(use_inverse)
	{		
		color1.r = interpF32(filter_value, color2.r, color1.r);
		color1.g = interpF32(filter_value, color2.g, color1.g);
		color1.b = interpF32(filter_value, color2.b, color1.b);
		terrainSourceDrawColor( source, x, z, state->visible_lod, color1, fall_off, cache );
	}
	else
	{
		terrainSourceDrawColor( source, x, z, state->visible_lod, color1, filter_value*fall_off, cache );
    }
}

void terEdApplyColorImage(OPTIMIZED_BRUSH_PARAMS)
{
	Color color;
	F32 alpha;
	F32 height;
	F32 filter_value;
	bool ignore_alpha = values->bool_2;

	if(start && frame_start)
		terEdRotateAndPositionImage(state, values, cx, cz);

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	if(	terrainSourceGetInterpolatedHeight( source, x, z, &height, cache) &&
		terEdGetBrushImageColor(values, &color, x, height, z))
	{
		alpha = (ignore_alpha ? 1.0f : color.a/255.0f);
		terrainSourceDrawColor( source, x, z, state->visible_lod, color, alpha*filter_value*fall_off, cache );
    }
}


void terEdApplyMaterialSet(OPTIMIZED_BRUSH_PARAMS)
{
	int mat = terrainSourceGetMaterialIndex(source, values->string_1, true);
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;

	if(mat != UNDEFINED_MAT)
	{
		filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
		terrainSourceDrawMaterial( source, x, z, state->visible_lod, mat, filter_value*fall_off, cache );
    }
}

void terEdApplyMaterialReplace(OPTIMIZED_BRUSH_PARAMS)
{
	int mat_old = terrainSourceGetMaterialIndex(source, values->string_2, true);
	int mat_new = terrainSourceGetMaterialIndex(source, values->string_1, true);
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;

	if(mat_old != UNDEFINED_MAT && mat_new != UNDEFINED_MAT)
	{
		filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
		terrainSourceReplaceMaterial( source, x, z, state->visible_lod, mat_old, mat_new, filter_value*fall_off, cache );
	}
}

void terEdApplyObjectSet(OPTIMIZED_BRUSH_PARAMS)
{
	int obj = terrainSourceGetObjectIndex(source, values->object_1.name_uid, -1, true);
	F32 density = (reverse ? 0.0f : values->float_1);
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;

	if(obj != UNDEFINED_OBJ)
	{
		filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
		terrainSourceDrawObjects( source, x, z, state->visible_lod, obj, density, filter_value*fall_off, cache );
    }
}

void terEdApplyObjectEraseAll(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod) * fall_off;
	if(filter_value > 0.01f)
		terrainSourceDrawAllExistingObjects( source, x, z, state->visible_lod, 0, cache );
}

void terEdApplyAlphaCut(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_value;
	if(state->visible_lod != 2)
		return;
	if(i%4 != 0 || j%4 != 0)
		return;

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	if((filter_value*fall_off) > 0.01f)
        terrainSourceDrawAlpha( source, x, z, state->visible_lod, (reverse ? 255 : 0));
}

void terEdApplySelect(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_value;
	if(i%4 != 0 || j%4 != 0)
		return;
	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	if((filter_value*fall_off) > 0.01f)
	    terrainSourceDrawSelection(source, x, z, state->visible_lod, (reverse ? -1.0 : 1.0)*filter_value*fall_off, cache );
}

F32 terEdGetBasicMinMaxFilterValue(F32 value, F32 min_value, F32 max_value, F32 filter_falloff)
{
	F32 filter_value = 1;

	if(min_value < max_value)
	{
		if(value < min_value)
		{
			if(filter_falloff > 0.0f)
				filter_value = 1 - ((min_value - value) / filter_falloff);
			else
				filter_value = 0;
		}
		else if(value > max_value)
		{
			if(filter_falloff > 0.0f)
				filter_value = 1 - ((value - max_value) / filter_falloff);
			else
				filter_value = 0;
		}
	}
	else if(min_value > max_value)
	{
		F32 temp = min_value;
		min_value = max_value;
		max_value = temp;
		if(value >= min_value && value <= max_value)
		{
			F32 mid_point = (min_value + max_value)/2.0f;
			if(value > mid_point)
			{
				if(filter_falloff > 0.0f)
					filter_value = 1 - ((max_value - value) / filter_falloff);
				else
					filter_value = 0;
			}
			else
			{
				if(filter_falloff > 0.0f)
					filter_value = 1 - ((value - min_value) / filter_falloff);
				else
					filter_value = 0;
			}
		}
		//else min_value == max_value do nothing
	}

	return filter_value;
}

void terEdApplyFilterAngle(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	F32 min_angle = values->float_1;
	F32 max_angle = values->float_2;
	F32 filter_falloff = values->float_3;
	U8Vec3 normal;
	F32 filter_value = 1;

	if (filter->optimized_cache && filter->optimized_cache->has_angle)
	{
		filter_value = terEdGetBasicMinMaxFilterValue(filter->optimized_cache->angle, min_angle, max_angle, filter_falloff);
	}
	else if(terrainSourceGetInterpolatedNormal(source, x, z, source->editing_lod, normal, cache))
	{
		F32 angle = acos((normal[1]-128.0)/127.0)*180.0/3.14159;
		if (filter->optimized_cache)
		{
			filter->optimized_cache->has_angle = 1;
			filter->optimized_cache->angle = angle;
		}
		filter_value = terEdGetBasicMinMaxFilterValue(angle, min_angle, max_angle, filter_falloff);
	}
	filter_value = CLAMP(filter_value, 0.0f, 1.0f);
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFilterAltitude(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	F32 min_height = values->float_1;
	F32 max_height = values->float_2;
	F32 filter_falloff = values->float_3;
	F32 height;
	F32 filter_value = 1;

	if(	(channel == TBC_Soil && terrainSourceGetInterpolatedSoilDepth(source, x, z, &height, cache)) || 
		(channel != TBC_Soil && terrainSourceGetInterpolatedHeight(source, x, z, &height, cache)))
	{
		filter_value = terEdGetBasicMinMaxFilterValue(height, min_height, max_height, filter_falloff);
	}
	filter_value = CLAMP(filter_value, 0.0f, 1.0f);
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFilterObject(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	int obj = terrainSourceGetObjectIndex(source, values->object_1.name_uid, -1, false);
	F32 object_density;
	F32 min_density = values->float_1;
	F32 max_density = values->float_2;
	F32 filter_falloff = values->float_3;
	F32 filter_value = 1;

	if(obj == UNDEFINED_OBJ)
		return;

	object_density = terrainSourceGetObjectDensity(source, x, z, obj);
	filter_value = terEdGetBasicMinMaxFilterValue(object_density, min_density, max_density, filter_falloff);
	filter_value = CLAMP(filter_value, 0.0f, 1.0f);
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFilterMaterial(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	int mat = terrainSourceGetMaterialIndex(source, values->string_1, false);
	F32 mat_weight;
	F32 min_weight = values->float_1;
	F32 max_weight = values->float_2;
	F32 filter_falloff = values->float_3;
	F32 filter_value = 1;

	if(mat == UNDEFINED_MAT)
		return;

	mat_weight = terrainSourceGetMaterialWeight(source, x, z, mat);
	filter_value = terEdGetBasicMinMaxFilterValue(mat_weight, min_weight, max_weight, filter_falloff);
	filter_value = CLAMP(filter_value, 0.0f, 1.0f);
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

F32 terEdInterpolatedNoise(F32 x, F32 y)
{
	U32 index;
	U32 integer_X    = floor(x);
	F32 fractional_X = x - floor(x);
	U32 integer_Y    = floor(y);
	F32 fractional_Y = y - floor(y);
	Vec2 s_noise, t_noise, u_noise, v_noise;
	Vec2 s_vec, t_vec, u_vec, v_vec;
	F32 s, t, u, v;
	F32 a, b, ease;
	
	//Get rand unit vecs
	index = (integer_X + perlin_permutation[integer_Y%PERLIN_CNT])%PERLIN_CNT;
	copyVec2(perlin_gradients[index], s_noise);
	index = ((integer_X+1) + perlin_permutation[integer_Y%PERLIN_CNT])%PERLIN_CNT;
	copyVec2(perlin_gradients[index], t_noise);
	index = (integer_X + perlin_permutation[(integer_Y+1)%PERLIN_CNT])%PERLIN_CNT;
	copyVec2(perlin_gradients[index], u_noise);
	index = ((integer_X+1) + perlin_permutation[(integer_Y+1)%PERLIN_CNT])%PERLIN_CNT;
	copyVec2(perlin_gradients[index], v_noise);
	
	//Get vecs to center point
	s_vec[0] = fractional_X;
	s_vec[1] = fractional_Y;
	t_vec[0] = -(1.0-fractional_X);
	t_vec[1] = fractional_Y;
	u_vec[0] = fractional_X;
	u_vec[1] = -(1.0-fractional_Y);
	v_vec[0] = -(1.0-fractional_X);
	v_vec[1] = -(1.0-fractional_Y);

	//Dot the two
	s = dotVec2(s_noise, s_vec);
	t = dotVec2(t_noise, t_vec);
	u = dotVec2(u_noise, u_vec);
	v = dotVec2(v_noise, v_vec);

	//Interpolate with a bias to the edges
	ease = CUBE(fractional_X)*(6.0*SQR(fractional_X) - 15.0*fractional_X + 10.0);
	a = s + ease*(t - s);
	b = u + ease*(v - u);
	ease = CUBE(fractional_Y)*(6.0*SQR(fractional_Y) - 15.0*fractional_Y + 10.0);
	return a + ease*(b - a);
}

//Buffer the random unit vectors
void terEdInitPerlinNoise()
{
	int i;
	MersenneTable *table;
	table = mersenneTableCreate(1);
	for(i=0; i < PERLIN_CNT; i++)
	{
		F32 length;

		perlin_permutation[i] = i;

		perlin_gradients[i][0] = randomMersenneF32(table);
		perlin_gradients[i][1] = randomMersenneF32(table);
		length = sqrt(	perlin_gradients[i][0]*perlin_gradients[i][0] +
						perlin_gradients[i][1]*perlin_gradients[i][1]	);
		perlin_gradients[i][0] /= length;
		perlin_gradients[i][1] /= length;
	}

	for(i=0; i < PERLIN_CNT; i++)
	{
		U8 temp = perlin_permutation[i];
		U8 rand_pos = (U8)randomMersenneU32(table);
		perlin_permutation[i] = perlin_permutation[rand_pos];
		perlin_permutation[rand_pos] = temp;
	}
	mersenneTableFree(table);
	perlin_inited = true;
}

F32 terEdPerlinNoise(F32 x, F32 y)
{
	F32 total = 0;
	F32 p = 0.812;//persistence;
	F32 n = 2;//Number of octaves
	int i;

	if(!perlin_inited)
		terEdInitPerlinNoise();

	for(i=0; i <= n ;i++)
	{
		int frequency = 1<<i;
		float amplitude = pow(p,i);

		total = total + terEdInterpolatedNoise((x) * frequency, (y) * frequency) * amplitude;	
	}

	//Push values closer to extremes
	total = ((total > 0) ? 1-((total+1)*(total+1)) : ((total-1)*(total-1))-1);
	return CLAMP(total, -1.0, 1.0);
}

F32 terEdFBMNoise(F32 x, F32 y, int octaves)
{
	int i;
	F32 total = 0;
	F32 frequency = 1.0f;
	F32 weight = 1.0f;

	if(!perlin_inited)
		terEdInitPerlinNoise();

	for ( i=0; i < octaves; i++ )
	{  
		F32 noise = terEdInterpolatedNoise(x*frequency, y * frequency);

		total += noise*weight;

		frequency *= 2.0f;
		weight /= 2.0f;
	}

	return CLAMP(total, -1.0, 1.0);  
}

F32 terEdRidgedMultifractalNoise(F32 x, F32 y, F32 gain)
{
	int i;
	F32 total = 0;
	F32 amplitude = 0.5;
	F32 frequency = 1.0;
	F32 previous = 1.0; 
	int octaves = 7;
	F32 persistence = 2;
	F32 offset = 0.75f;

	if(!perlin_inited)
		terEdInitPerlinNoise();

	for ( i=0; i < octaves; i++ )
	{  
		F32 noise = terEdInterpolatedNoise(x*frequency, y * frequency);
		noise = fabs(noise);
		noise = offset - noise;  
		noise = SQR(noise);  

		total += noise * amplitude * previous;  

		previous = noise;
		frequency *= persistence;  
		amplitude *= gain;  
	}  

	return CLAMP(total*2, 0.0, 1.0);  
}

void terEdApplyFilterPerlinNoise(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	F32 perlin_scale = 0.25f-(values->float_1/4.0f);
	F32 filter_value = (terEdPerlinNoise(x*perlin_scale,z*perlin_scale)+1)*0.5;
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFBMNoise(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	F32 scale = (1.0f/((values->float_1+0.001f)*1000.0f));
	int octaves = values->float_2;
	F32 rand_offset = values->int_1;
	F32 filter_value = (terEdFBMNoise((x+rand_offset)*scale, (z+rand_offset)*scale, octaves)+1)*0.5;
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFilterRidgedMultifractalNoise(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	F32 scale = (1.0f/((values->float_1+0.001f)*1000.0f));
	F32 gain = (1.0f - values->float_2);
	F32 rand_offset = values->int_1;
	F32 filter_value = terEdRidgedMultifractalNoise((x+rand_offset)*scale, (z+rand_offset)*scale, gain);
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFilterImage(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	Color color;
	F32 height;
	F32 filter_value = 0.0f;
	bool use_alpha = values->bool_2;

	if(start && frame_start)
		terEdRotateAndPositionImage(state, values, cx, cz);

	if(	terrainSourceGetInterpolatedHeight( source, x, z, &height, cache) &&
		terEdGetBrushImageColor(values, &color, x, height, z))
	{
		if(use_alpha)
			filter_value = color.a/255.0f;
		else
			filter_value = color.r/255.0f;

	}
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFilterSelection(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
    F32 filter_value = 0.f;
    terrainSourceGetSelection( source, x, z, &filter_value, cache);
    terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyFilterShadow(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
    F32 filter_value = 0.f;
    terrainSourceGetShadow( source, x, z, &filter_value, cache);
    terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, 1.f-filter_value, filter_strength));
}

bool terEdPathFilterTraverseCallback(TerrainBrushCurveList *list, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	if (def->property_structs.curve && def->property_structs.curve->terrain_filter && def->property_structs.curve->spline.spline_widths)
	{
		Spline *new_spline = StructClone(parse_Spline, &def->property_structs.curve->spline);
		splineTransformMatrix(new_spline, info->curve_matrix);
		eafPush(&list->lengths, splineGetTotalLength(new_spline));
		eaPush(&list->curves, new_spline);
	}
	return true;
}

void terEdApplyFilterPath(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	int ii, jj;
	F32 filter_value = 0.f;
	F32 inner_width = values->float_1;
	F32 outer_width = values->float_2;
	F32 falloff_dist = MAX(0.01,values->float_3);
	F32 end_falloff = values->float_4;
	F32 min_distance = 1e8;
	F32 min_strength = 1;

	if (!state->curve_list)
	{
		state->curve_list = calloc(1, sizeof(TerrainBrushCurveList));
		for (ii = zmapGetLayerCount(NULL)-1; ii >= 0; --ii)
		{
			ZoneMapLayer *layer = zmapGetLayer(NULL, ii);
			layerGroupTreeTraverse(layer, terEdPathFilterTraverseCallback, state->curve_list, false, false);
		}
	}

	if (eaSize(&state->curve_list->curves) == 0)
	{
		terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, 0.f, filter_strength));
		return;
	}

	if (filter->optimized_cache && filter->optimized_cache->has_path)
	{
		min_distance = filter->optimized_cache->path_min_distance;
		min_strength = filter->optimized_cache->path_min_strength;
	}
	else
	{
		for (ii = 0; ii < eaSize(&state->curve_list->curves); ii++)
		{
			Spline *spline = state->curve_list->curves[ii];
			F32 total_length = state->curve_list->lengths[ii];
			Vec3 in_point = { x, 0, z }, out_point;
			F32 max_width = 0;
			for (jj = 0; jj < eafSize(&spline->spline_widths); jj++)
				if (spline->spline_widths[jj] > max_width)
					max_width = spline->spline_widths[jj];
			if (terrainSourceGetHeight( source, x, z, &in_point[1], cache))
			{
				S32 index;
				F32 t;
				F32 point;
				F32 distance = splineGetNearestPoint(spline, in_point, out_point, &index, &t, max_width*(outer_width+falloff_dist), false);
				F32 width = splineGetWidth(spline, index, t);

				if (index < 0 ||
					(index == 0 && t < 0.001f) ||
					index == eafSize(&spline->spline_points)-3 ||
					(index == eafSize(&spline->spline_points)-6 && t > 0.999f))
				{
					continue;
				}

				distance /= width;
				if (distance < min_distance)
				{
					min_distance = MIN(distance, min_distance);
					point = total_length * (index*0.333f + t) / (eafSize(&spline->spline_points)/3-1);
					if (end_falloff > 0)
					{
						min_strength = MIN(CLAMP(point/end_falloff, 0, 1), CLAMP((total_length-point)/end_falloff, 0, 1));
					}
					else
					{
						min_strength = 1;
					}
				}
			}
		}
		if (filter->optimized_cache)
		{
			filter->optimized_cache->has_path = 1;
			filter->optimized_cache->path_min_distance = min_distance;
			filter->optimized_cache->path_min_strength = min_strength;
		}
	}
	{
			F32 value_1 = 1.f-(MAX(0,min_distance-outer_width)/falloff_dist);
			F32 value_2 = 1.f-(MAX(0,inner_width-min_distance)/falloff_dist);
			filter_value = MAX(0,MIN(value_1, value_2));
	}
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value*min_strength, filter_strength));
}

typedef struct TerrainFilterBlobSphere
{
	Vec3 pos;
	F32 radius;
	const char *name;
	F32 node_height;
} TerrainFilterBlobSphere;

bool terEdBlobFilterTraverseCallback(TerrainFilterBlobSphere ***blobs, GroupDef *def, GroupInfo *info, GroupInheritedInfo *inherited_info, bool needs_entry)
{
	if (groupIsVolumeType(def, "TerrainFilter"))
	{
		TerrainFilterBlobSphere *new_obj = calloc(1, sizeof(TerrainFilterBlobSphere));
		copyVec3(info->world_matrix[3], new_obj->pos);
		if(def->property_structs.volume && def->property_structs.volume->eShape == GVS_Sphere)
			new_obj->radius = def->property_structs.volume->fSphereRadius;
		new_obj->name = allocAddString(def->property_structs.terrain_properties.pcVolumeName);
		new_obj->node_height = info->node_height;
		eaPush(blobs, new_obj);
	}
	return true;
}

void terEdApplyHeightFlattenBlob(OPTIMIZED_BRUSH_PARAMS)
{
	F32 old_height;
	F32 filter_value;
	int ii;
	F32 inner_width = values->float_1;
	F32 outer_width = values->float_2;
	F32 falloff_dist = MAX(0.01,values->float_3);

	if(i%4 != 0 || j%4 != 0)
		return;

	if (!state->blob_list_inited)
	{
		for (ii = zmapGetLayerCount(NULL)-1; ii >= 0; --ii)
		{
			ZoneMapLayer *layer = zmapGetLayer(NULL, ii);
			layerGroupTreeTraverse(layer, terEdBlobFilterTraverseCallback, &state->blob_list, false, false);
		}
		state->blob_list_inited = true;
	}

	if (eaSize(&state->blob_list) == 0)
	{
		return;
	}

	filter_value = terEdGetFilterValue(filter, i, j, state->visible_lod);
	if (terrainSourceGetHeight( source, x, z, &old_height, cache))
	{
		F32 target_height = old_height;

		for (ii = 0; ii < eaSize(&state->blob_list); ii++)
			if (state->blob_list[ii]->radius > 0)
			{
				TerrainFilterBlobSphere *sphere = state->blob_list[ii];
				F32 value_1, value_2;
				F32 dist = (sqrtf(SQR(sphere->pos[0]-x) + SQR(sphere->pos[2]-z)) - sphere->radius) / sphere->radius;
				dist = MAX(0, dist);
				if (dist < 0.001f)
				{
					target_height = sphere->node_height;
					break;
				}
				value_1 = 1.f-(MAX(0,dist-outer_width)/falloff_dist);
				value_2 = 1.f-(MAX(0,inner_width-dist)/falloff_dist);
				target_height = lerp(target_height, sphere->node_height, MAX(0,MIN(value_1, value_2)));
			}

		terrainSourceDrawHeight(source, x, z, state->visible_lod, (target_height - old_height)*filter_value*fall_off, cache );
	}
}

void terEdApplyFilterVolumeBlob(OPTIMIZED_BRUSH_PARAMS)
{
	F32 filter_strength = values->strength;
	int ii;
	F32 filter_value = 0.f;
	F32 inner_width = values->float_1;
	F32 outer_width = values->float_2;
	const char *name_str = (values->string_1 && values->string_1[0]) ? allocAddString(values->string_1) : NULL;
	F32 falloff_dist = MAX(0.01,values->float_3);
	F32 min_distance = 0;
	bool found_zero = false;

	if (!state->blob_list_inited)
	{
		for (ii = zmapGetLayerCount(NULL)-1; ii >= 0; --ii)
		{
			ZoneMapLayer *layer = zmapGetLayer(NULL, ii);
			layerGroupTreeTraverse(layer, terEdBlobFilterTraverseCallback, &state->blob_list, false, false);
		}
		state->blob_list_inited = true;
	}

	if (eaSize(&state->blob_list) == 0)
	{
		terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, 0.f, filter_strength));
		return;
	}

	if (!name_str && filter->optimized_cache && filter->optimized_cache->has_blob)
	{
		min_distance = filter->optimized_cache->blob_min_distance;
	}
	else
	{
		for (ii = 0; ii < eaSize(&state->blob_list); ii++)
			if (state->blob_list[ii]->radius > 0 &&
				(!name_str || name_str == state->blob_list[ii]->name))
			{
				TerrainFilterBlobSphere *sphere = state->blob_list[ii];
				F32 dist = (sqrtf(SQR(sphere->pos[0]-x) + SQR(sphere->pos[2]-z)) - sphere->radius) / sphere->radius;
				dist = MAX(0, dist);
				if (dist < 0.001f)
				{
					found_zero = true;
					break;
				}
				min_distance += 1.f/SQR(dist);
			}
			if (found_zero || min_distance == 0)
				min_distance = 0;
			else
				min_distance = 1.f/min_distance;
		if (!name_str && filter->optimized_cache)
		{
			filter->optimized_cache->has_blob = 1;
			filter->optimized_cache->blob_min_distance = min_distance;
		}
	}

	{
			F32 value_1 = 1.f-(MAX(0,min_distance-outer_width)/falloff_dist);
			F32 value_2 = 1.f-(MAX(0,inner_width-min_distance)/falloff_dist);
			filter_value = MAX(0,MIN(value_1, value_2));
	}
	terEdMultiFilterValue(filter, i, j, state->visible_lod, lerp(1.0f, filter_value, filter_strength));
}

void terEdApplyOptimizedBrush( TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledBrushOp **brush_ops, TerrainBrushFilterBuffer *filter, 
								TerrainBrushFalloff *falloff_values, F32 cx, F32 cz, bool square, TerrainBrushFalloffTypes falloff_type, 
								bool reverse, bool start, bool uses_color, bool filter_pass)
{
	int granularity = GET_COLOR_LOD(state->visible_lod);
	F32 radius = falloff_values->diameter_multi/2.f;
	F32 hardness = falloff_values->hardness_multi;
	F32 strength = falloff_values->strength_multi;
	HeightMapCache cache = { 0 };
	F32 dist_to_center;
	F32 fall_off = 1;
	F32 dx, dy;
	F32 x, y;
	S32 i, j;
	int k;

	for(j=0; j < filter->height ; j++)
	{
		for(i=0; i < filter->width; i++)
		{
			if(!uses_color && (i%4 != 0 || j%4 != 0))
				continue;

			x = filter->x_offset + (i<<granularity);
			y = filter->y_offset + (j<<granularity);

			if(!filter_pass)
			{
				dx = ((i<<granularity)-filter->rel_x_cntr)/radius;
				dy = ((j<<granularity)-filter->rel_y_cntr)/radius;
				dist_to_center = fsqrt(SQR(dx) + SQR(dy));

				if (dist_to_center < 2.0f)
					fall_off = getBrushFalloff(square, dist_to_center, dx, dy, hardness, falloff_type);
				else
					fall_off = 0.0f;

				fall_off *= strength;
			}

			for(k = 0; k < eaSize(&brush_ops); k++)
			{
				if(!filter_pass && brush_ops[k]->channel != TBC_Color && (i%4 != 0 || j%4 != 0))
					continue;
				((terrainOptimizedBrushFunction)(brush_ops[k]->draw_func))(source, state, brush_ops[k]->values_copy, filter, brush_ops[k]->channel, x, y, i, j, cx, cz, fall_off, &cache, reverse, start, (i==0 && j==0), dist_to_center, uses_color);
			}
		}
#if !PLATFORM_CONSOLE
        Sleep(0);
#endif
	}
}

F32 terEdGetFilterAndFalloffValue(TerrainEditorSource *source, TerrainBrushState *state, TerrainBrushFilterBuffer *filter, 
								  F32 x, F32 y, S32 i, S32 j, U32 lod, F32 radius, F32 hardness, bool square, TerrainBrushFalloffTypes falloff_type)
{
	int granularity = GET_COLOR_LOD(state->visible_lod);
	F32 dx = ((i<<granularity)-filter->rel_x_cntr)/radius;
	F32 dy = ((j<<granularity)-filter->rel_y_cntr)/radius;
	F32 dist_to_center = fsqrt(SQR(dx) + SQR(dy));
	F32 fall_off;

	if (dist_to_center < 2.0f)
		fall_off = getBrushFalloff(square, dist_to_center, dx, dy, hardness, falloff_type);
	else
		return 0.0f;

	return (fall_off * terEdGetFilterValue(filter, i, j, lod));
}

void terEdApplyHeightErode(REGULAR_BRUSH_PARAMS)
{
	ErodeBrushData erode_options;
	int granularity = state->visible_lod;
	F32 radius = falloff_values->diameter_multi/2.f;
	S32 thermal_rad, sx, sy;

	erode_options.soil_removal_rate	= values->float_1;
	erode_options.rock_removal_rate = values->float_2;
	erode_options.deposit_rate		= values->float_3;
	erode_options.carrying_const	= values->float_4;
	erode_options.remove_multi		= values->float_5;
	erode_options.deposit_multi		= values->float_6;

	thermal_rad = (S32)(radius) >> source->editing_lod;
	sx = (S32)(cx) >> source->editing_lod;
	sy = (S32)(cz) >> source->editing_lod;
	terrainErosionDrawHydraulicErosion(source, state->visible_lod, sx, sy, thermal_rad, thermal_rad, falloff_values->strength_multi, erode_options, NULL );
}

void terEdApplyHeightSlope(REGULAR_BRUSH_PARAMS)
{
}

bool terEdDoesBrushHaveSlope(TerrainCompiledMultiBrush *multibrush)
{
    int i, j;

	if(!multibrush)
		return false;

    for (i = 0; i < eaSize(&multibrush->brushes); i++)
    {
        for (j = 0; j < eaSize(&multibrush->brushes[i]->bucket[TBK_RegularBrush].brush_ops); j++)
        {
            if (multibrush->brushes[i]->bucket[TBK_RegularBrush].brush_ops[j]->draw_func == terEdApplyHeightSlope)
                return true;
        }
    }
    return false;
}

void terEdApplyHeightSlopeBrushUp(TerrainEditorSource *source, TerrainBrushState *state, TerrainSlopeBrushParams *params, TerrainCommonBrushParams *falloff_values, int ter_type)
{
	int i, j;
	F32 old_height;
	HeightMapCache cache = { 0 };
	F32 radius = falloff_values->brush_diameter/2.f;
	F32 hardness = falloff_values->brush_hardness;
	F32 strength = falloff_values->brush_strength;
	TerrainBrushFalloffTypes falloff_type = falloff_values->falloff_type;
	int granularity = state->visible_lod;
	S32 scale = (1 << granularity);
	S32 begin_x, end_x;
	S32 begin_z, end_z;
	F32 road_x=0, road_y=0;
	F32 road_length;
	Vec2 road_vec;
	Vec2 inv_road_vec;
	Vec2 point;
	//point # dotted with normal # 
	//p1 is bottom left of box while p2 is upper right if road moves from left to right
	//n1 is in the direction of the road and n2 is the inverse of n2
	F32 p1n1, p1n2, p2n1, p2n2; 

	//Get normals
	road_vec[0] = params->brush_end_pos[0] - params->brush_start_pos[0];
	road_vec[1] = params->brush_end_pos[2] - params->brush_start_pos[2];
	road_length = normalVec2(road_vec);
	if(	road_length == 0.0f ||
		(!allow_long_ramps && road_length > 2000.0f))
    {
		return;
    }
	inv_road_vec[0] = -road_vec[1];
	inv_road_vec[1] =  road_vec[0];

	//Do so some setup math
	point[0] = params->brush_start_pos[0] + inv_road_vec[0]*radius;
	point[1] = params->brush_start_pos[2] + inv_road_vec[1]*radius;
	p1n1 = dotVec2(point, road_vec);
	p1n2 = dotVec2(point, inv_road_vec);
	point[0] = params->brush_end_pos[0] - inv_road_vec[0]*radius;
	point[1] = params->brush_end_pos[2] - inv_road_vec[1]*radius;
	p2n1 = dotVec2(point, road_vec);
	p2n2 = dotVec2(point, inv_road_vec);

	//Calculate rough bounding box
	begin_x = (S32)(params->brush_start_pos[0] < params->brush_end_pos[0] ? params->brush_start_pos[0] : params->brush_end_pos[0]);
	end_x   = (S32)(params->brush_start_pos[0] < params->brush_end_pos[0] ? params->brush_end_pos[0]   : params->brush_start_pos[0]);
	begin_z = (S32)(params->brush_start_pos[2] < params->brush_end_pos[2] ? params->brush_start_pos[2] : params->brush_end_pos[2]);
	end_z   = (S32)(params->brush_start_pos[2] < params->brush_end_pos[2] ? params->brush_end_pos[2]   : params->brush_start_pos[2]);
	begin_x -= radius;
	end_x   += radius + scale-1;
	begin_z -= radius;
	end_z   += radius + scale-1;
	begin_x -= begin_x & (scale-1);
	end_x   -= end_x   & (scale-1);
	begin_z -= begin_z & (scale-1);
	end_z   -= end_z   & (scale-1);

	for( i=begin_x; i < end_x; i+=scale )
	{
		for( j=begin_z; j < end_z; j+=scale )
		{
			Vec2 q;
			F32 qn1subp1n1, qn1subp2n1, qn2subp1n2, qn2subp2n2;
			setVec2(q, i, j);
			//q dot n# minus p# dot n# which is the same as (q - p#) dot n#
			//q and p are points, n is a normal
			qn1subp1n1 = dotVec2(q, road_vec) - p1n1;
			qn1subp2n1 = dotVec2(q, road_vec) - p2n1;
			qn2subp1n2 = dotVec2(q, inv_road_vec) - p1n2;
			qn2subp2n2 = dotVec2(q, inv_road_vec) - p2n2;

			if(	(qn1subp1n1 < 0 && qn1subp2n1 > 0) || (qn1subp1n1 > 0 && qn1subp2n1 < 0) &&
				(qn2subp1n2 < 0 && qn2subp2n2 > 0) || (qn2subp1n2 > 0 && qn2subp2n2 < 0) )
			{
				F32 dist_to_line =fsqrt(PointLineDist2DSquared(i, j, params->brush_start_pos[0], params->brush_start_pos[2], road_vec[0]*road_length, road_vec[1]*road_length, &road_x, &road_y));
				F32 fall_off = getBrushFalloff(false, dist_to_line / radius, 0, 0, hardness, falloff_type);
				F32 ratio = fsqrt(SQR(road_x - params->brush_start_pos[0]) + SQR(road_y - params->brush_start_pos[2])) / road_length;
				F32 height = lerp(params->brush_start_pos[1], params->brush_end_pos[1], ratio);

				if (terrainSourceGetHeight( source, i, j, &old_height, &cache))
					terrainSourceDrawHeight(source, i, j, state->visible_lod, (height - old_height)*strength*fall_off, &cache );
				if(ter_type >= 0)
					terrainSourceDrawTerrainType(source, i, j, state->visible_lod, ter_type, 1.0f*fall_off, &cache );
			}
		}
	}
}

#define INVALID_PATCH_VALUE -8e8
void terEdApplyHeightSmooth(REGULAR_BRUSH_PARAMS)
{
	int granularity = state->visible_lod;
	F32 radius = falloff_values->diameter_multi/2.f;
	F32 hardness = falloff_values->hardness_multi;
	F32 smooth_amount = values->float_1 + 1;
	S32 kernel_sradius;
	F32* patch;
	S32 patch_width;
	S32 patch_height;
	HeightMapCache cache = { 0 };
	F32 filter_value;
	F32 x, y;
	S32 patch_i, patch_j;
	S32 i, j, k;
	S32 dir;
	F32 height;

	//Init Patch
	kernel_sradius = ((S32)(smooth_amount) + 3) / 4;
	patch_width = ((filter->width+3)/4) + (2*kernel_sradius);
	patch_height = ((filter->height+3)/4) + (2*kernel_sradius);
	patch = ScratchAlloc(patch_width*patch_height*sizeof(F32));
	for(j=0; j < patch_height ; j++)
	{
		for(i=0; i < patch_width; i++)
		{
			x = filter->x_offset - (kernel_sradius<<granularity) + (i<<granularity);
			y = filter->y_offset - (kernel_sradius<<granularity) + (j<<granularity);
		
			if(channel == TBC_Soil)
			{
				if(terrainSourceGetSoilDepth( source, x, y, &height, &cache))
					patch[i + j*patch_width] = height;
				else
					patch[i + j*patch_width] = INVALID_PATCH_VALUE;
			}
			else if(channel == TBC_Select)
			{
				if(terrainSourceGetSelection( source, x, y, &height, &cache))
					patch[i + j*patch_width] = height;
				else
					patch[i + j*patch_width] = INVALID_PATCH_VALUE;
			}
			else
			{
				if(terrainSourceGetHeight( source, x, y, &height, &cache))
					patch[i + j*patch_width] = height;
				else
					patch[i + j*patch_width] = INVALID_PATCH_VALUE;
			}
		}
#if !PLATFORM_CONSOLE
        Sleep(0);
#endif
	}

	//Smooth Patch
	//First in the x direction and then in the y direction
	for (dir = 0; dir < 2; dir++)
	{
		for(j=0; j < patch_height ; j++)
		{
			for(i=0; i < patch_width; i++)
			{
				if (patch[i + j*patch_width] > INVALID_PATCH_VALUE)
				{
					F32 weight_sum = 0;
					F32 value_sum = 0;
					if (dir == 0)
					{
						// Smooth in x direction
						for (k = (S32)(i - kernel_sradius); k < (S32)(i + kernel_sradius + 1); k++)
						{
							if (k >= (S32)0 && k < (S32)(patch_width) && patch[k + j*patch_width] > INVALID_PATCH_VALUE)
							{
								F32 smoothed_val = getSmoothingWeight( k - i, kernel_sradius );
								weight_sum += smoothed_val;
								value_sum += smoothed_val * patch[k + j*patch_width];
							}
						}
					}
					else
					{
						// Smooth in y direction
						for (k = (S32)(j - kernel_sradius); k < (S32)(j + kernel_sradius + 1); k++)
						{
							if (k >= (S32)0 && k < (S32)(patch_height) && patch[i + k*patch_width] > INVALID_PATCH_VALUE)
							{
								F32 smoothed_val = getSmoothingWeight( k - j, kernel_sradius );
								weight_sum += smoothed_val;
								value_sum += smoothed_val * patch[i + k*patch_width];
							}
						}
					}
					patch[i + j*patch_width] = value_sum / weight_sum;
				}
			}
#if !PLATFORM_CONSOLE
	        Sleep(0);
#endif
		}	
	}

	//Apply Patch
	for(j=0, patch_j=kernel_sradius; j < (filter->height>>2) && patch_j < patch_height; j++, patch_j++)
	{
		for(i=0, patch_i=kernel_sradius; i < (filter->width>>2) && patch_i < patch_width; i++, patch_i++)
		{
			bool found = false;
			x = filter->x_offset + (i<<granularity);
			y = filter->y_offset + (j<<granularity);
		
			if(channel == TBC_Soil)
				found = terrainSourceGetSoilDepth( source, x, y, &height, &cache);
			else if(channel == TBC_Select)
				found = terrainSourceGetSelection( source, x, y, &height, &cache);
			else
				found = terrainSourceGetHeight( source, x, y, &height, &cache);

			if(found)
			{
				F32 smoothed_height = patch[patch_i + patch_j*patch_width];
				if(smoothed_height > INVALID_PATCH_VALUE)
				{
					filter_value = terEdGetFilterAndFalloffValue(source, state, filter, x, y, i<<2, j<<2, state->visible_lod, radius, hardness, square, falloff_type);
					if(channel == TBC_Soil)
						terrainSourceDrawSoilDepth( source, x, y, state->visible_lod, (smoothed_height-height)*falloff_values->strength_multi*filter_value, &cache );
					else if(channel == TBC_Select)
						terrainSourceDrawSelection( source, x, y, state->visible_lod, (smoothed_height-height)*falloff_values->strength_multi*filter_value, &cache );
					else
						terrainSourceDrawHeight(source, x, y, state->visible_lod, (smoothed_height-height)*falloff_values->strength_multi*filter_value, &cache );
				}
			}
		}
#if !PLATFORM_CONSOLE
        Sleep(0);
#endif
	}
	ScratchFree(patch);
}


void terEdApplyColorBlend(REGULAR_BRUSH_PARAMS)
{
	int granularity = GET_COLOR_LOD(state->visible_lod);
	F32 radius = falloff_values->diameter_multi/2.f;
	F32 hardness = falloff_values->hardness_multi;
	F32 smooth_amount = values->float_1 + 1;
	S32 kernel_sradius;
	F32* patch;
	S32 patch_width;
	S32 patch_height;
	HeightMapCache cache = { 0 };
	F32 filter_value;
	F32 x, y;
	S32 patch_i, patch_j;
	S32 i, j, k, c;
	S32 dir;
	Color color;

	//Init Patch
	kernel_sradius = (S32)(smooth_amount);
	patch_width = filter->width + (2*kernel_sradius);
	patch_height = filter->height + (2*kernel_sradius);
	patch = ScratchAlloc(patch_width*patch_height*sizeof(F32)*3);
	for(j=0; j < patch_height ; j++)
	{
		for(i=0; i < patch_width; i++)
		{
			x = filter->x_offset - (kernel_sradius<<granularity) + (i<<granularity);
			y = filter->y_offset - (kernel_sradius<<granularity) + (j<<granularity);
			if(terrainSourceGetColor( source, x, y, &color, &cache))
			{
				patch[(i + j*patch_width)*3 + 0] = color.r;
				patch[(i + j*patch_width)*3 + 1] = color.g;
				patch[(i + j*patch_width)*3 + 2] = color.b;
			}
			else
			{
				patch[(i + j*patch_width)*3 + 0] = INVALID_PATCH_VALUE;
				patch[(i + j*patch_width)*3 + 1] = INVALID_PATCH_VALUE;
				patch[(i + j*patch_width)*3 + 2] = INVALID_PATCH_VALUE;
			}
		}
#if !PLATFORM_CONSOLE
        Sleep(0);
#endif
	}

	//Smooth Patch
	//First in the x direction and then in the y direction
	for (dir = 0; dir < 2; dir++)
	{
		for(j=0; j < patch_height ; j++)
		{
			for(i=0; i < patch_width; i++)
			{
				if (patch[(i + j*patch_width)*3] > INVALID_PATCH_VALUE)
				{
					F32 weight_sum[3] = {0,0,0};
					F32 value_sum[3] = {0,0,0};
					if (dir == 0)
					{
						// Smooth in x direction
						for (k = (S32)(i - kernel_sradius); k < (S32)(i + kernel_sradius + 1); k++)
						{
							if (k >= (S32)0 && k < (S32)(patch_width) && patch[(k + j*patch_width)*3] > INVALID_PATCH_VALUE)
							{
								F32 smoothed_val = getSmoothingWeight( k - i, kernel_sradius );
								for(c=0; c<3; c++)
								{
									weight_sum[c] += smoothed_val;
									value_sum[c] += smoothed_val * patch[(k + j*patch_width)*3 + c];
								}
							}
						}
					}
					else
					{
						// Smooth in y direction
						for (k = (S32)(j - kernel_sradius); k < (S32)(j + kernel_sradius + 1); k++)
						{
							if (k >= (S32)0 && k < (S32)(patch_height) && patch[(i + k*patch_width)*3] > INVALID_PATCH_VALUE)
							{
								F32 smoothed_val = getSmoothingWeight( k - j, kernel_sradius );
								for(c=0; c<3; c++)
								{
									weight_sum[c] += smoothed_val;
									value_sum[c] += smoothed_val * patch[(i + k*patch_width)*3 + c];
								}
							}
						}
					}
					for(c=0; c<3; c++)
					{
						patch[(i + j*patch_width)*3 + c] = value_sum[c] / weight_sum[c];
					}
				}
			}
#if !PLATFORM_CONSOLE
			Sleep(0);
#endif
		}	
	}

	//Apply Patch
	for(j=0, patch_j=kernel_sradius; j < filter->height && patch_j < patch_height; j++, patch_j++)
	{
		for(i=0, patch_i=kernel_sradius; i < filter->width && patch_i < patch_width; i++, patch_i++)
		{
			x = filter->x_offset + (i<<granularity);
			y = filter->y_offset + (j<<granularity);
		
			for(c=0; c<3; c++)
			{
				color.rgb[c] = patch[(patch_i + patch_j*patch_width)*3 + c ];
			}

			if(color.r > INVALID_PATCH_VALUE)
			{
				filter_value = terEdGetFilterAndFalloffValue(source, state, filter, x, y, i, j, state->visible_lod, radius, hardness, square, falloff_type);
				terrainSourceDrawColor( source, x, y, state->visible_lod, color, falloff_values->strength_multi*filter_value, &cache  );
			}
		}
#if !PLATFORM_CONSOLE
        Sleep(0);
#endif
	}
	ScratchFree(patch);
}

bool terEdCreateFilterBuffer(TerrainBrushState *state, F32 x, F32 y, TerrainBrushFilterBuffer *filter, bool alloc_filter, F32 brush_diameter, bool uses_color)
{
	int i;
	int granularity = GET_COLOR_LOD(state->visible_lod);
	S32 scale = (1 << state->visible_lod);
	F32 radius = brush_diameter/2.f;

	filter->x_offset = (S32)(x-radius);
	filter->y_offset = (S32)(y-radius);
	filter->x_offset -= filter->x_offset & (scale-1);
	filter->y_offset -= filter->y_offset & (scale-1);

	filter->width  = (S32)(brush_diameter + (scale-1)*2);
	filter->height = (S32)(brush_diameter + (scale-1)*2);
	filter->width >>= granularity;
	filter->height >>= granularity;

	filter->rel_x_cntr = x - filter->x_offset;
	filter->rel_y_cntr = y - filter->y_offset;

	filter->lod = granularity;
	if (alloc_filter)
	{
		filter->buffer = terrainAlloc(filter->width * filter->height, sizeof(F32));
		if (!filter->buffer)
			return false;

		for (i = 0; i < filter->width*filter->height; i++)
		{
			filter->buffer[i] = 1.0f;
		}
	}
	else
	{
		filter->buffer = NULL;
	}
	return true;
}

void terEdUseBrush( TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledMultiBrush *multibrush, TerrainCommonBrushParams *common_params, F32 x, F32 z, bool reverse, bool start)
{
	int i, j;

	terrainSetLockedEdges(source, common_params->lock_edges);
    terrainSourceGetHeight(source, x, z, &state->brush_center_height, NULL);
    state->per_draw_frame_rand_val = rand();

	for(i=0; i<eaSize(&multibrush->brushes); i++)
	{
		TerrainCompiledBrushOp **brush_ops;
		TerrainBrushFilterBuffer filter = { 0 };
		TerrainBrushFalloff falloff_values;
		bool uses_color = multibrush->brushes[i]->uses_color;

		falloff_values.diameter_multi = multibrush->brushes[i]->falloff_values.diameter_multi *
										common_params->brush_diameter;
		falloff_values.hardness_multi = multibrush->brushes[i]->falloff_values.hardness_multi *
										common_params->brush_hardness;
		falloff_values.strength_multi = multibrush->brushes[i]->falloff_values.strength_multi *
										common_params->brush_strength;
		filter.invert = !common_params->invert_filters != !multibrush->brushes[i]->falloff_values.invert_filters; // Logical XOR

		if (!terEdCreateFilterBuffer(state, x, z, &filter, 
								(eaSize(&multibrush->brushes[i]->bucket[TBK_OptimizedFilter].brush_ops) > 0 ||
								eaSize(&multibrush->brushes[i]->bucket[TBK_RegularFilter].brush_ops) > 0),
								common_params->brush_diameter, uses_color))
			return;

		//Do Optimized Filters
		brush_ops = multibrush->brushes[i]->bucket[TBK_OptimizedFilter].brush_ops;
		if(eaSize(&brush_ops) > 0)
			terEdApplyOptimizedBrush(source, state, brush_ops, &filter, &falloff_values, x, z, common_params->brush_shape, common_params->falloff_type, reverse, start, uses_color, true);
		
		//Do Normal Filters
		brush_ops = multibrush->brushes[i]->bucket[TBK_RegularFilter].brush_ops;
		for(j=0; j < eaSize(&brush_ops); j++)
		{
			((terrainRegularBrushFunction)(brush_ops[j]->draw_func))(source, state, brush_ops[j]->values_copy, &filter, brush_ops[j]->channel, &falloff_values, x, z, common_params->brush_shape, common_params->falloff_type, reverse, start, uses_color);	
		}

		//Do Optimized Brushes
		brush_ops = multibrush->brushes[i]->bucket[TBK_OptimizedBrush].brush_ops;
		if(eaSize(&brush_ops) > 0)
			terEdApplyOptimizedBrush(source, state, brush_ops, &filter, &falloff_values, x, z, common_params->brush_shape, common_params->falloff_type, reverse, start, uses_color, false);

		//Do Normal Brushes
		brush_ops = multibrush->brushes[i]->bucket[TBK_RegularBrush].brush_ops;
		for(j=0; j < eaSize(&brush_ops); j++)
		{
			((terrainRegularBrushFunction)(brush_ops[j]->draw_func))(source, state, brush_ops[j]->values_copy, &filter, brush_ops[j]->channel, &falloff_values, x, z, common_params->brush_shape, common_params->falloff_type, reverse, start, uses_color);	
		}

		SAFE_FREE(filter.buffer);
	}

	if (state->curve_list)
	{
		eaDestroyEx(&state->curve_list->curves, splineDestroy);
		eafDestroy(&state->curve_list->lengths);
		SAFE_FREE(state->curve_list);
		state->curve_list = NULL;
	}
	if (state->blob_list_inited)
	{
		eaDestroyEx(&state->blob_list, NULL);
		state->blob_list_inited = false;
	}
}

void terrainGetEditableBounds(TerrainEditorSource *source, Vec2 min_pos, Vec3 max_pos)
{
	int i, j;
	setVec2(min_pos,  1e8,  1e8);
	setVec2(max_pos, -1e8, -1e8);

    for (i = 0; i < eaSize(&source->layers); i++)
        if (source->layers[i]->effective_mode == LAYER_MODE_EDITABLE ||
			zmapInfoHasGenesisData(zmapGetInfo(layerGetZoneMap(source->layers[i]->layer))))
        {
            TerrainEditorSourceLayer *layer = source->layers[i]; 
            for (j = 0; j < eaSize(&layer->blocks); j++)
            {
                IVec2 min_block, max_block;
                terrainBlockGetExtents(layer->blocks[j], min_block, max_block);
                if (min_block[0] < min_pos[0])
                    min_pos[0] = min_block[0]; 
                if (min_block[1] < min_pos[1])
                    min_pos[1] = min_block[1]; 
                if (max_block[0] > max_pos[0])
                    max_pos[0] = max_block[0]; 
                if (max_block[1] > max_pos[1])
                    max_pos[1] = max_block[1]; 
            }
        }
    min_pos[0] *= GRID_BLOCK_SIZE;
    min_pos[1] *= GRID_BLOCK_SIZE;
    max_pos[0] = (max_pos[0]+1) * GRID_BLOCK_SIZE;
    max_pos[1] = (max_pos[1]+1) * GRID_BLOCK_SIZE;
}

void terEdUseBrushFill( TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledMultiBrush *multibrush, F32 brush_strength, bool invert_filters, bool lock_edges, bool reverse)
{
	int i, j;
    F32 x, z;
    Vec2 max_pos;
    Vec2 min_pos;
    TerrainBrushFalloff falloff_values;

	terrainSetLockedEdges(source, lock_edges);
	terrainGetEditableBounds(source, min_pos, max_pos);
    if (max_pos[0] < min_pos[0] || max_pos[1] < max_pos[1])
        return;

    x = (min_pos[0] + max_pos[0])*0.5f;
    z = (min_pos[1] + max_pos[1])*0.5f;
    falloff_values.diameter_multi = MAX((max_pos[0]-min_pos[0]), (max_pos[1]-min_pos[1])) + 1;
    falloff_values.hardness_multi = 1.f;
    
	for(i=0; i<eaSize(&multibrush->brushes); i++)
	{
		TerrainCompiledBrushOp **brush_ops;
		TerrainBrushFilterBuffer filter = { 0 };
		bool uses_color = multibrush->brushes[i]->uses_color;

		falloff_values.strength_multi = multibrush->brushes[i]->falloff_values.strength_multi *
							            brush_strength;
		filter.invert = !invert_filters != !multibrush->brushes[i]->falloff_values.invert_filters; // Logical XOR

		if (!terEdCreateFilterBuffer(state, x, z, &filter, 
								(eaSize(&multibrush->brushes[i]->bucket[TBK_OptimizedFilter].brush_ops) > 0 ||
								eaSize(&multibrush->brushes[i]->bucket[TBK_RegularFilter].brush_ops) > 0),
								falloff_values.diameter_multi, uses_color))
			return;

		if (state->cancel_action > 1)
			break;

        //Do Optimized Filters
        brush_ops = multibrush->brushes[i]->bucket[TBK_OptimizedFilter].brush_ops;
        if(eaSize(&brush_ops) > 0)
            terEdApplyOptimizedBrush(source, state, brush_ops, &filter, &falloff_values, x, z, TBS_Square, TE_FALLOFF_SCURVE, reverse, true, uses_color, true);

		if (state->cancel_action > 1)
			break;

        //Do Normal Filters
        brush_ops = multibrush->brushes[i]->bucket[TBK_RegularFilter].brush_ops;
        for(j=0; j < eaSize(&brush_ops); j++)
        {
            ((terrainRegularBrushFunction)(brush_ops[j]->draw_func))(source, state, brush_ops[j]->values_copy, &filter, brush_ops[j]->channel, &falloff_values, x, z, TBS_Square, TE_FALLOFF_SCURVE, reverse, true, uses_color);	
        }

		if (state->cancel_action > 1)
			break;

        //Do Optimized Brushes
        brush_ops = multibrush->brushes[i]->bucket[TBK_OptimizedBrush].brush_ops;
        if(eaSize(&brush_ops) > 0)
            terEdApplyOptimizedBrush(source, state, brush_ops, &filter, &falloff_values, x, z, TBS_Square, TE_FALLOFF_SCURVE, reverse, true, uses_color, false);

		if (state->cancel_action > 1)
			break;

		//Do Normal Brushes
        brush_ops = multibrush->brushes[i]->bucket[TBK_RegularBrush].brush_ops;
        for(j=0; j < eaSize(&brush_ops); j++)
        {
            ((terrainRegularBrushFunction)(brush_ops[j]->draw_func))(source, state, brush_ops[j]->values_copy, &filter, brush_ops[j]->channel, &falloff_values, x, z, TBS_Square, TE_FALLOFF_SCURVE, reverse, true, uses_color);	
        }

		SAFE_FREE(filter.buffer);

		if (state->cancel_action > 1)
			break;
	}

	if (state->curve_list)
	{
		eaDestroyEx(&state->curve_list->curves, splineDestroy);
		eafDestroy(&state->curve_list->lengths);
		SAFE_FREE(state->curve_list);
		state->curve_list = NULL;
	}
}

void* terEdGetFunctionFromName(TerrainFunctionName name)
{
	switch(name)
	{	
	case TFN_HeightAdd:
		return (void*)terEdApplyHeightAdd;
	case TFN_HeightFlatten:
		return (void*)terEdApplyHeightFlatten;
	case TFN_HeightFlattenBlob:
		return (void*)terEdApplyHeightFlattenBlob;
	case TFN_HeightWeather:
		return (void*)terEdApplyHeightWeather;
	case TFN_HeightErode:
		return (void*)terEdApplyHeightErode;
	case TFN_HeightSmooth:
		return (void*)terEdApplyHeightSmooth;
	case TFN_HeightRoughen:
		return (void*)terEdApplyHeightRoughen;
	case TFN_HeightGrab:
		return (void*)terEdApplyHeightGrab;
	case TFN_HeightSmudge:
		return (void*)terEdApplyHeightSmudge;
	case TFN_HeightSlope:
		return (void*)terEdApplyHeightSlope;
	case TFN_HeightTerrace:
		return (void*)terEdApplyHeightTerrace;
	case TFN_ColorSet:
		return (void*)terEdApplyColorSet;
	case TFN_ColorBlend:
		return (void*)terEdApplyColorBlend;
	case TFN_ColorImage:
		return (void*)terEdApplyColorImage;
	case TFN_MaterialSet:
		return (void*)terEdApplyMaterialSet;
	case TFN_MaterialReplace:
		return (void*)terEdApplyMaterialReplace;
	case TFN_ObjectSet:
		return (void*)terEdApplyObjectSet;
	case TFN_ObjectEraseAll:
		return (void*)terEdApplyObjectEraseAll;
	case TFN_AlphaCut:
		return (void*)terEdApplyAlphaCut;
	case TFN_Select:
		return (void*)terEdApplySelect;
	case TFN_FilterAngle:
		return (void*)terEdApplyFilterAngle;
	case TFN_FilterAltitude:
		return (void*)terEdApplyFilterAltitude;
	case TFN_FilterObject:
		return (void*)terEdApplyFilterObject;
	case TFN_FilterMaterial:
		return (void*)terEdApplyFilterMaterial;
	case TFN_FilterPerlinNoise:
		return (void*)terEdApplyFilterPerlinNoise;
	case TFN_FilterRidgedNoise:
		return (void*)terEdApplyFilterRidgedMultifractalNoise;
	case TFN_FilterFBMNoise:
		return (void*)terEdApplyFBMNoise;
	case TFN_FilterImage:
		return (void*)terEdApplyFilterImage;
	case TFN_FilterSelection:
		return (void*)terEdApplyFilterSelection;
	case TFN_FilterShadow:
		return (void*)terEdApplyFilterShadow;
	case TFN_FilterPath:
		return (void*)terEdApplyFilterPath;
	case TFN_FilterVolumeBlob:
		return (void*)terEdApplyFilterVolumeBlob;
	}
	return NULL;
}

void terrainBrushApplyOptimized(TerrainEditorSource *source, TerrainBrushState *state, S32 fx, S32 fz, int color_step,
								TerrainBrushFilterBuffer *filter, HeightMapCache *cache,
								TerrainCompiledMultiBrush *multibrush, F32 brush_strength, 
								bool invert_filters, bool reverse)
{
	int k;
	filter->optimized_cache = calloc(1, sizeof(TerrainBrushFilterCache));
	for( k=0; k < eaSize(&multibrush->filter_list); k++ )
	{
		TerrainCompiledBrushOp *brush_op = multibrush->filter_list[k]->op_with_cache;
		bool uses_color = brush_op->cached_uses_color;
		if (!uses_color && (fx%color_step != 0 || fz%color_step != 0))
		{
			brush_op->cached_value = 1.0f;
			continue;
		}
		*filter->buffer = 1.f;
		filter->invert = false;
		((terrainOptimizedBrushFunction)(brush_op->draw_func))(source, state, brush_op->values_copy, filter, brush_op->channel, fx, fz, 0, 0, fx, fz, 1.0f, cache, reverse, true, false, 0.f, uses_color);
		brush_op->cached_value = *filter->buffer;
	}
    for (k = 0; k < eaSize(&multibrush->brushes); k++)
    {
		bool can_early_out = true;
        //F32 radius = diameter/2.f;
        F32 strength;
        int fk;
        TerrainCompiledBrushOp **brush_ops;
        bool uses_color = multibrush->brushes[k]->uses_color;

        if (!uses_color && (fx%color_step != 0 || fz%color_step != 0))
            continue;

        *filter->buffer = 1.f;

        strength = multibrush->brushes[k]->falloff_values.strength_multi * brush_strength;
        filter->invert = !invert_filters != !multibrush->brushes[k]->falloff_values.invert_filters; // Logical XOR

		brush_ops = multibrush->brushes[k]->bucket[TBK_OptimizedBrush].brush_ops;
		for(fk = 0; fk < eaSize(&brush_ops); fk++)
		{
			if(	brush_ops[fk]->draw_func == terEdGetFunctionFromName(TFN_ColorSet) &&
				brush_ops[fk]->values_copy->bool_1 == true)
			{
				can_early_out = false;
				break;
			}
		}

        brush_ops = multibrush->brushes[k]->bucket[TBK_OptimizedFilter].brush_ops;
        for(fk = 0; fk < eaSize(&brush_ops); fk++)
        {
			if(brush_ops[fk]->op_with_cache)
				terEdMultiFilterValue(filter, 0, 0, state->visible_lod, brush_ops[fk]->op_with_cache->cached_value);
			else
				((terrainOptimizedBrushFunction)(brush_ops[fk]->draw_func))(source, state, brush_ops[fk]->values_copy, filter, brush_ops[fk]->channel, fx, fz, 0, 0, fx, fz, strength, cache, reverse, true, false, 0.f, uses_color);
            if (can_early_out && *filter->buffer < 0.0001f)
                break;
        }
        if (can_early_out && *filter->buffer < 0.0001f)
            continue;

        brush_ops = multibrush->brushes[k]->bucket[TBK_OptimizedBrush].brush_ops;
        for(fk = 0; fk < eaSize(&brush_ops); fk++)
        {
			if(brush_ops[fk]->channel != TBC_Color && (fx%color_step != 0 || fz%color_step != 0))
				continue;
            ((terrainOptimizedBrushFunction)(brush_ops[fk]->draw_func))(source, state, brush_ops[fk]->values_copy, filter, brush_ops[fk]->channel, fx, fz, 0, 0, fx, fz, strength, cache, reverse, true, false, 0.f, uses_color);
        }
    }
	SAFE_FREE(filter->optimized_cache);
	filter->optimized_cache = NULL;
}

void terEdUseBrushFillOptimized( TerrainEditorSource *source, TerrainBrushState *state, TerrainCompiledMultiBrush *multibrush, F32 brush_strength, bool invert_filters, bool lock_edges, bool reverse)
{
    F32 x, z;
    Vec2 max_pos;
    Vec2 min_pos;
    F32 diameter;
    F32 filter_value;
	TerrainBrushFilterBuffer filter = { 0 };
    int granularity = GET_COLOR_LOD(state->visible_lod);
    int step = 1<<granularity;
    int color_step = 4<<granularity;
    S32 fx, fz, px, pz;
    HeightMapCache cache = { 0 };
    
	terrainSetLockedEdges(source, lock_edges);
	terrainGetEditableBounds(source, min_pos, max_pos);
    if (max_pos[0] < min_pos[0] || max_pos[1] < max_pos[1])
        return;

	x = (min_pos[0] + max_pos[0])*0.5f;
    z = (min_pos[1] + max_pos[1])*0.5f;
    diameter = MAX((max_pos[0]-min_pos[0]), (max_pos[1]-min_pos[1])) + 1;
    
    filter.buffer = &filter_value;
	filter.height = 1;
	filter.width = 1;
	filter.x_offset = x;
	filter.y_offset = z;
	filter.rel_x_cntr = x;
	filter.rel_y_cntr = z;
	filter.lod = GET_COLOR_LOD(source->visible_lod);        

    for (pz = min_pos[1]; pz < max_pos[1]; pz += GRID_BLOCK_SIZE)
    {
        S32 fz_begin = (pz == min_pos[1]) ? pz : (pz + step);
        for (px = min_pos[0]; px < max_pos[0]; px += GRID_BLOCK_SIZE)
        {
            S32 fx_begin = (px == min_pos[0]) ? px : (px + step);
            for (fz = fz_begin; fz <= pz + GRID_BLOCK_SIZE; fz += step)
            {
                for(fx = fx_begin; fx <= px + GRID_BLOCK_SIZE; fx += step)
                {
					terrainBrushApplyOptimized(source, state, fx, fz, color_step,
								&filter, &cache, multibrush, brush_strength, invert_filters, reverse);
                }
#if !PLATFORM_CONSOLE
                Sleep(0);
#endif
            }
			if (state->cancel_action > 1)
				break;
        }
		if (state->cancel_action > 1)
			break;
    }
}

void terEdDestroyCompiledBrushOp(TerrainCompiledBrushOp *brush_op)
{
	if (brush_op->values_copy->image_ref)
		terrainFreeBrushImage(brush_op->values_copy->image_ref);
    StructDestroy(parse_TerrainBrushValues, brush_op->values_copy);
	SAFE_FREE(brush_op);
}

void terEdDestroyCompiledBrush(TerrainCompiledBrush *brush)
{
	int i;
	for(i=0; i < TBK_NUM_BRUSH_BUCKETS; i++)
	{
		eaDestroyEx(&brush->bucket[i].brush_ops, terEdDestroyCompiledBrushOp);
	}
	SAFE_FREE(brush);
}

void terEdClearCompiledMultiBrush(TerrainCompiledMultiBrush *compiled_multibrush)
{
	if (compiled_multibrush->alloced_filter_list)
		eaDestroyEx(&compiled_multibrush->filter_list, terEdDestroyCompiledBrushOp);
	else
		eaDestroy(&compiled_multibrush->filter_list);
	eaDestroyEx(&compiled_multibrush->brushes, terEdDestroyCompiledBrush);
	compiled_multibrush->brushes = NULL;
}

void terEdDestroyCompiledMultiBrush(TerrainCompiledMultiBrush *multibrush)
{
	terEdClearCompiledMultiBrush(multibrush);
	SAFE_FREE(multibrush);
}

TerrainCompiledMultiBrush *terEdCopyCompiledMultiBrush(TerrainCompiledMultiBrush *multibrush)
{
    int i, j, k, l;
	TerrainCompiledMultiBrush *ret = terrainAlloc(1, sizeof(TerrainCompiledMultiBrush));
	if (!ret)
		return NULL;
	for (i = 0; i < eaSize(&multibrush->filter_list); i++)
	{
		TerrainCompiledBrushOp *op = terrainAlloc(1, sizeof(TerrainCompiledBrushOp));
		op->draw_func = multibrush->filter_list[i]->draw_func;
		op->channel = multibrush->filter_list[i]->channel;
		op->op_with_cache = op;
		op->cached_uses_color = multibrush->filter_list[i]->cached_uses_color;
		op->values_copy = StructClone(parse_TerrainBrushValues, multibrush->filter_list[i]->values_copy);
		if (op->values_copy->image_ref)
		{
			terrainRefBrushImage(op->values_copy->image_ref);
		}
		eaPush(&ret->filter_list, op);
	}
	ret->alloced_filter_list = true;
    for (i = 0; i < eaSize(&multibrush->brushes); i++)
    {
        TerrainCompiledBrush *brush = terrainAlloc(1, sizeof(TerrainCompiledBrush));
		if (brush)
		{
			memcpy(brush, multibrush->brushes[i], sizeof(TerrainCompiledBrush));
			for (j = 0; j < TBK_NUM_BRUSH_BUCKETS; j++)
			{
				brush->bucket[j].brush_ops = NULL;
				for (k = 0; k < eaSize(&multibrush->brushes[i]->bucket[j].brush_ops); k++)
				{
					TerrainCompiledBrushOp *op = terrainAlloc(1, sizeof(TerrainCompiledBrushOp));
					if (op)
					{
						TerrainCompiledBrushOp *op_src = multibrush->brushes[i]->bucket[j].brush_ops[k];  
						op->draw_func = op_src->draw_func;
						op->channel = op_src->channel;
						op->values_copy = StructCreate(parse_TerrainBrushValues);
						op->cached_uses_color = op_src->cached_uses_color;
						for (l = 0; l < eaSize(&multibrush->filter_list); l++)
							if (op_src->op_with_cache == multibrush->filter_list[l])
							{
								op->op_with_cache = ret->filter_list[l];
								break;
							}
						StructCopyAll(parse_TerrainBrushValues, op_src->values_copy, op->values_copy); 
						if (op->values_copy->image_ref)
							terrainRefBrushImage(op->values_copy->image_ref);
						eaPush(&brush->bucket[j].brush_ops, op);
					}
					else
						return ret;
				}
			}
			eaPush(&ret->brushes, brush);
		}
		else
		{
			return ret;
		}
    }
    return ret;
}

void terEdCompileDefaultBrush(TerrainCompiledMultiBrush *compiled_multibrush, TerrainDefaultBrush *selected_brush)
{
    RefDictIterator it;
    TerrainDefaultBrush *brush;
    TerrainCompiledBrush *new_brush = terrainAlloc(1, sizeof(TerrainCompiledBrush));
    TerrainCompiledBrushOp *new_brush_op = terrainAlloc(1, sizeof(TerrainCompiledBrushOp));

	if (!new_brush || !new_brush_op)
		return;

    terEdClearCompiledMultiBrush(compiled_multibrush);
    
    new_brush->uses_color = (selected_brush->brush_template.channel == TBC_Color);
    new_brush->falloff_values.diameter_multi = 1.0f;
    new_brush->falloff_values.hardness_multi = 1.0f;
    new_brush->falloff_values.strength_multi = 1.0f;
    new_brush->falloff_values.invert_filters = false;
    new_brush_op->draw_func = terEdGetFunctionFromName(selected_brush->brush_template.function);
    new_brush_op->channel = selected_brush->brush_template.channel;
    new_brush_op->values_copy = StructCreate(parse_TerrainBrushValues);
    StructCopyAll(parse_TerrainBrushValues, &selected_brush->default_values, new_brush_op->values_copy);
	if (new_brush_op->values_copy->image_ref)
		terrainRefBrushImage(new_brush_op->values_copy->image_ref);
    eaPush(&new_brush->bucket[selected_brush->brush_template.bucket].brush_ops, new_brush_op);

    //For each filter
    RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
    while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
    {
        if(	brush->default_values.active &&
            (brush->brush_template.bucket == TBK_OptimizedFilter || 
             brush->brush_template.bucket == TBK_RegularFilter		))
        {
            new_brush_op = terrainAlloc(1, sizeof(TerrainCompiledBrushOp));
			if (new_brush_op)
			{
				new_brush_op->draw_func = terEdGetFunctionFromName(brush->brush_template.function);
				new_brush_op->channel = brush->brush_template.channel;
				new_brush_op->values_copy = StructCreate(parse_TerrainBrushValues);
				StructCopyAll(parse_TerrainBrushValues, &brush->default_values, new_brush_op->values_copy);
				if (new_brush_op->values_copy->image_ref)
					terrainRefBrushImage(new_brush_op->values_copy->image_ref);
				eaPush(&new_brush->bucket[brush->brush_template.bucket].brush_ops, new_brush_op);
			}
			else
			{
				return;
			}
        }
    }
    eaPush(&compiled_multibrush->brushes, new_brush);
}

void terEdCompileMultiBrush(TerrainCompiledMultiBrush *compiled_multibrush, TerrainMultiBrush *multi_brush, bool clear)
{
    int i, j, k;
    
	if (clear)
		terEdClearCompiledMultiBrush(compiled_multibrush);

    //For each brush
    for(i=0; i < eaSize(&multi_brush->brushes); i++)
    {
        TerrainBrushOp *brush_op;
        TerrainDefaultBrush *brush_base;
		TerrainCompiledBrush *new_brush;
		if(multi_brush->brushes[i]->disabled)
			continue;
        new_brush = terrainAlloc(1, sizeof(TerrainCompiledBrush));
		if (!new_brush)
			return;
        new_brush->uses_color = false;
        new_brush->falloff_values.diameter_multi = multi_brush->brushes[i]->falloff_values.diameter_multi;
        new_brush->falloff_values.hardness_multi = multi_brush->brushes[i]->falloff_values.hardness_multi;
        new_brush->falloff_values.strength_multi = multi_brush->brushes[i]->falloff_values.strength_multi;
        new_brush->falloff_values.invert_filters = multi_brush->brushes[i]->falloff_values.invert_filters;

        //For each operation
        for(j=0; j < eaSize(&multi_brush->brushes[i]->ops); j++)
        {
            brush_op = multi_brush->brushes[i]->ops[j];
            brush_base = GET_REF(brush_op->brush_base);

            if (brush_op->values.active)
            {
                TerrainCompiledBrushOp *new_brush_op = terrainAlloc(1, sizeof(TerrainCompiledBrushOp));

				if (!new_brush_op)
				{
					SAFE_FREE(new_brush);
					return;
				}

                new_brush_op->draw_func = terEdGetFunctionFromName(brush_base->brush_template.function);
                new_brush_op->channel = brush_base->brush_template.channel;
                new_brush_op->values_copy = StructCreate(parse_TerrainBrushValues);
                StructCopyAll(parse_TerrainBrushValues, &brush_op->values, new_brush_op->values_copy);
				if (new_brush_op->values_copy->image_ref)
					terrainRefBrushImage(new_brush_op->values_copy->image_ref);
                if(	brush_base->brush_template.channel == TBC_Color &&
                    ( brush_base->brush_template.bucket == TBK_OptimizedBrush || 
                      brush_base->brush_template.bucket == TBK_RegularBrush ))
                {
                    new_brush->uses_color = true;
                }
                eaPush(&new_brush->bucket[brush_base->brush_template.bucket].brush_ops, new_brush_op);
            }
        }
		//For all the new filters
		for( j=0; j < eaSize(&new_brush->bucket[TBK_OptimizedFilter].brush_ops); j++ )
		{
			bool found = false;
			TerrainCompiledBrushOp *new_compiled_op = new_brush->bucket[TBK_OptimizedFilter].brush_ops[j];
			//Check against existing filters
			for( k=0; k < eaSize(&compiled_multibrush->filter_list); k++ )
			{
				TerrainCompiledBrushOp *compiled_op = compiled_multibrush->filter_list[k];
				//If they are the same
				if(	compiled_op->channel == new_compiled_op->channel &&
					compiled_op->draw_func == new_compiled_op->draw_func &&
					StructCompare(parse_TerrainBrushValues, compiled_op->values_copy, new_compiled_op->values_copy,0,0,0) == 0 )
				{
					found = true;
					if(new_brush->uses_color)
						compiled_op->cached_uses_color = true;
					new_compiled_op->op_with_cache = compiled_op;
					break;
				}
			}
			//Add if not found
			if(!found)
			{
				new_compiled_op->cached_uses_color = new_brush->uses_color;
				new_compiled_op->op_with_cache = new_compiled_op;
				eaPush(&compiled_multibrush->filter_list, new_compiled_op);
			}
		}
        eaPush(&compiled_multibrush->brushes, new_brush);
    }
}

void terrainBrushCompile(TerrainCompiledMultiBrush *compiled_multibrush, TerrainDefaultBrush *selected_brush, TerrainMultiBrush *expanded_multi_brush)
{
    compiled_multibrush->brush_version++;

	//Default Brush
	if (selected_brush)
	{
        terEdCompileDefaultBrush(compiled_multibrush, selected_brush);
	}
	//Multi Brush
	else if (expanded_multi_brush)
	{
        terEdCompileMultiBrush(compiled_multibrush, expanded_multi_brush, true);
	}
}

U32 terrainBrushGetCompiledMemory(TerrainCompiledMultiBrush *multibrush)
{
    int i, j;
    U32 size = sizeof(TerrainCompiledMultiBrush);
    for (i = 0; i < eaSize(&multibrush->brushes); i++)
    {
        size += sizeof(TerrainCompiledBrush);
        for (j = 0; j < TBK_NUM_BRUSH_BUCKETS; j++)
        {
            size += eaSize(&multibrush->brushes[i]->bucket[j].brush_ops) *
                (sizeof(TerrainCompiledBrushOp) + sizeof(TerrainBrushValues));
        }
    }
    return size;
}

void terrainFreeBrushImage(TerrainImageBuffer *image_ref)
{
	TerrainEditorSource *source;
	if (!image_ref)
		return;

	source = image_ref->source;

	terrainQueueLock();
	if (image_ref->ref_count > 1)
	{
		image_ref->ref_count--;
		terrainQueueUnlock();
		return;
	}
	if(image_ref->needs_reload)
	{
		terrainQueueUnlock();
		return;
	}
	eaFindAndRemove(&source->brush_images, image_ref);
	terrainQueueUnlock();

	StructFreeString(image_ref->file_name);
	SAFE_FREE(image_ref->buffer);
	free(image_ref);
}

static TerrainImageBuffer* terrainGetBrushImage(TerrainEditorSource *source, const char *img_path)
{
	int i;
	TerrainImageBuffer *brush_image;
	terrainQueueLock();
	for( i=0; i < eaSize(&source->brush_images) ; i++ )
	{
		brush_image = source->brush_images[i];
		if(stricmp(img_path, brush_image->file_name) == 0)
		{
			brush_image->ref_count++;
			brush_image->needs_reload++;
			terrainQueueUnlock();
			return brush_image;
		}
	}
	terrainQueueUnlock();
	return NULL;
}

static bool terrainReloadBrushImage(TerrainImageBuffer *brush_image)
{
	int i, j;
	int width, height;
	U8 *temp_buf;

	temp_buf = tgaLoadFromFname(brush_image->file_name, &width, &height);
	if(!temp_buf)
		return false;

	SAFE_FREE(brush_image->buffer);
	brush_image->buffer = (U8*)terrainAlloc(width*height*4, sizeof(U8));
	if (!brush_image->buffer)
	{
		brush_image->width = 0;
		brush_image->height = 0;
		SAFE_FREE(temp_buf);
		return false;
	}

	brush_image->width = width;
	brush_image->height = height;

	//Image comes in flipped in the y
	for(j=0; j < height; j++)
	{
		for(i=0; i < width; i++)
		{
			brush_image->buffer[(i+j*width)*4 + 0] = temp_buf[(i+(height-j-1)*width)*4 + 0];
			brush_image->buffer[(i+j*width)*4 + 1] = temp_buf[(i+(height-j-1)*width)*4 + 1];
			brush_image->buffer[(i+j*width)*4 + 2] = temp_buf[(i+(height-j-1)*width)*4 + 2];
			brush_image->buffer[(i+j*width)*4 + 3] = temp_buf[(i+(height-j-1)*width)*4 + 3];
		}
	}

	SAFE_FREE(temp_buf);
	return true;
}

//Must only be called inside a lock
int terrainCheckReloadBrushImages(TerrainEditorSource *source)
{
	int i, count = 0;
	if(!source)
		return 0;
	for( i=0; i < eaSize(&source->brush_images) ; i++ )
	{
		TerrainImageBuffer *image_ref = source->brush_images[i];
		if(image_ref->needs_reload)
		{
			image_ref->needs_reload = 0;
			if(image_ref->ref_count == 0)
			{
				terrainFreeBrushImage(image_ref);
				i--;
				continue;
			}
			terrainReloadBrushImage(image_ref);
			count++;
		}
	}
	return count;
}

bool terrainLoadBrushImage(TerrainEditorSource *source, TerrainBrushValues *values)
{
	char *value_string;
	char img_path[CRYPTIC_MAX_PATH];
	TerrainImageBuffer *brush_image;

	if(values->image_ref)
		terrainFreeBrushImage(values->image_ref);
	values->image_ref = NULL;

	if(!values->string_1)
		return false;

	value_string = values->string_1;
	//If saved in the really old format, convert to the new format
	if(fileIsAbsolutePath(value_string))
	{
		while(value_string[0] != '\0' && !strStartsWith(value_string, "src"))
			value_string++;
		if(value_string[0] == '\0')
			return false;
		value_string += 3;
	}
	//If saved in the old format, convert to the new format
	if(strStartsWith(value_string, "/texture_library/editor/Terrain_Editor/"))
		value_string += strlen("/texture_library/editor/Terrain_Editor/");
	if(strStartsWith(value_string, "editor/terrain/brush_images/"))
		value_string += strlen("editor/terrain/brush_images/");

	sprintf(img_path, "%s%s", "editors/terrain/brush_images/", value_string);

	values->image_ref = terrainGetBrushImage(source, img_path);
	if(values->image_ref)
		return true;

	brush_image = (TerrainImageBuffer*)terrainAlloc(1, sizeof(TerrainImageBuffer));
	if (!brush_image)
		return false;

	brush_image->buffer = NULL;
	brush_image->source = source;
	values->image_ref = brush_image;
	brush_image->ref_count = 1;
	brush_image->file_name = StructAllocString(img_path);
	brush_image->needs_reload = 1;
	terrainQueueLock();
	eaPush(&source->brush_images, brush_image);
	terrainQueueUnlock();
	return true;
}

void terrainRefBrushImage(TerrainImageBuffer *image_ref)
{
	image_ref->ref_count++;
}

TerrainMultiBrush *terrainGetMultiBrushByName(TerrainEditorSource *source, const char *name)
{
	RefDictIterator it;
	TerrainMultiBrush *brush;

	RefSystem_InitRefDictIterator(MULTI_BRUSH_DICTIONARY, &it);
	while(brush = (TerrainMultiBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
	{
		char buff[256];
		getFileNameNoExt(buff, brush->filename);
		if(buff && stricmp(name, buff)==0)
		{
            int i, j;
            TerrainMultiBrush *ret = StructCreate(parse_TerrainMultiBrush);
            StructCopyAll(parse_TerrainMultiBrush, brush, ret);
            for (i = 0; i < eaSize(&ret->brushes); i++)
            {
                for (j = 0; j < eaSize(&ret->brushes[i]->ops); j++)
                {
                    TerrainBrushOp *op = ret->brushes[i]->ops[j];
                    TerrainDefaultBrush *def = GET_REF(op->brush_base);
                    if (def && source &&
                        (def->brush_template.function == TFN_ColorImage ||
                         def->brush_template.function == TFN_FilterImage))
                    {
                        terrainLoadBrushImage(source, &op->values);
                    }
					op->values.active = true;                    
                }
            }
			return ret;
        }
    }
    return NULL;
}

void terrainDestroyTerrainBrushOp(TerrainBrushOp *brush_op)
{
	REMOVE_HANDLE(brush_op->brush_base);
	eaDestroy(&brush_op->widgets);
	eaDestroy(&brush_op->storage_data);
	terrainFreeBrushImage(brush_op->values.image_ref);
	StructDestroy(parse_TerrainBrushOp, brush_op);
}

void terrainDestroyTerrainBrush(TerrainBrush *brush)
{
	eaDestroy(&brush->storage_data);
	eaDestroyEx(&brush->ops, terrainDestroyTerrainBrushOp);
	StructDestroy(parse_TerrainBrush, brush);
}

void terrainDestroyMultiBrush(TerrainMultiBrush *multi_brush)
{
	eaDestroyEx(&multi_brush->brushes, terrainDestroyTerrainBrush);
	StructDestroy(parse_TerrainMultiBrush, multi_brush);
}

TerrainDefaultBrush *terEdSetFilterEnabled(const char *filter, bool enabled)
{
	TerrainDefaultBrush *brush = RefSystem_ReferentFromString(DEFAULT_BRUSH_DICTIONARY, filter);
	if (!brush)
		return NULL;
	brush->default_values.active = enabled;
	return brush;
}

#endif
