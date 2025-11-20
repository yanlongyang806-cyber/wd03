#ifndef NO_EDITORS

#include "TerrainEditor.h"
#include "TerrainEditorPrivate.h"

#include "GfxPrimitive.h"
#include "GfxTerrain.h"
#include "GfxSpriteText.h"
#include "wlTerrainErode.h"
#include "WorldEditorClientMain.h"

// This file handles the specialized drawing routines for the Terrain Editor

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define max_trace_steps 100
#define max_particle_traces 50

typedef struct ParticleTrace
{
	int length;
	Vec2 points[max_trace_steps];
	F32 sediments[max_trace_steps];
	F32 alpha;
	int granularity;
} ParticleTrace;

static ParticleTrace particle_traces[max_particle_traces];
static int current_trace = 0;

#define ANGLE_90_RAD 1.570795 //pie/2 or 90deg in rad

void erode_draw_lines(const Vec3 p1, const Vec3 p2, int color)
{
	gfxDrawLine3D_2ARGB(p1, p2, color, color);
}

void terEdDrawCursor( TerrainEditorState *state, const Vec3 cursor_world_pos )
{
	TerrainCommonBrushParams* common = (state->selected_brush ? &state->selected_brush->common : &state->multi_brush_common); 
	int print_y = 40;
	Vec3 top, bottom;
	copyVec3(cursor_world_pos, top);
	copyVec3(cursor_world_pos, bottom);
	top[1] += 100;

    if (terrain_state.show_terrain_occlusion)
    {
        Vec3 min, max;
        S32 p1x = (S32)bottom[0];
        F32 y[4];
        S32 p1z = (S32)bottom[2];
        Mat4 mat;
        
        p1x -= (p1x & 63);
        p1z -= (p1z & 63);
		terrainSourceGetInterpolatedHeight(state->source, p1x, p1z, &y[0], NULL);
		terrainSourceGetInterpolatedHeight(state->source, p1x+64, p1z, &y[1], NULL);
		terrainSourceGetInterpolatedHeight(state->source, p1x, p1z+64, &y[2], NULL);
		terrainSourceGetInterpolatedHeight(state->source, p1x+64, p1z+64, &y[3], NULL);

        min[0] = p1x; min[2] = p1z;
        min[1] = MIN(MIN(MIN(y[0], y[1]), y[2]), y[3]);
        max[0] = p1x+64; max[2] = p1z+64;
        max[1] = MAX(MAX(MAX(y[0], y[1]), y[2]), y[3]);
        identityMat4(mat);

        gfxDrawBox3D(min, max, mat, colorFromRGBA(0xffff00ff), 5);

        return;
    }

	//Draw Current Filters
	if(state->selected_brush)
	{
		RefDictIterator it;
		TerrainDefaultBrush *brush;
		Vec2 screen_pos;

		editLibGetScreenPos(top, screen_pos);
		gfxfont_SetColorRGBA(0xFFFF00FF, 0xFFFF00FF);

		RefSystem_InitRefDictIterator(DEFAULT_BRUSH_DICTIONARY, &it);
		while(brush = (TerrainDefaultBrush*)RefSystem_GetNextReferentFromIterator(&it)) 
		{
			if(	brush->default_values.active &&
				(brush->brush_template.bucket == TBK_OptimizedFilter || 
				brush->brush_template.bucket == TBK_RegularFilter		))
			{
				gfxfont_Printf(screen_pos[0]+12, screen_pos[1], 1, 1, 1, 0, "%s", brush->display_name);
				screen_pos[1] += 12;
			}
		}
	}

	//Draw Cursor
	gfxDrawLine3D_2ARGB(top, bottom, 0xffffffff, 0xffff0000);
	if(!state->seleted_image && !state->using_eye_dropper)
	{
		int i;
		F32 radius = common->brush_diameter/2.0f;
		F32 hardness = common->brush_hardness;
		bool square = (common->brush_shape == TBS_Square);
		Vec3 outer_ring[16];
		bool outer_ring_valid[16];
		Vec3 inner_ring[16];
		bool inner_ring_valid[16];
		F32 theta = 0;
		if (square)
		{
			for (i = 0; i < 4; i++)
			{
				outer_ring[i][0] = radius + bottom[0];
				outer_ring[i][2] = radius - (0.5f*radius*i) + bottom[2];
				outer_ring_valid[i] = terrainSourceGetInterpolatedHeight(state->source, outer_ring[i][0], outer_ring[i][2], &(outer_ring[i][1]), NULL);
				outer_ring[i][1] += 2;

				inner_ring[i][0] = radius*hardness + bottom[0];
				inner_ring[i][2] = radius*hardness - (0.5f*radius*hardness*i) + bottom[2];
				inner_ring_valid[i] = terrainSourceGetInterpolatedHeight(state->source, inner_ring[i][0], inner_ring[i][2], &(inner_ring[i][1]), NULL);
				inner_ring[i][1] += 2;

				outer_ring[i+4][0] = radius - (0.5f*radius*i) + bottom[0];
				outer_ring[i+4][2] = -radius + bottom[2];
				outer_ring_valid[i+4] = terrainSourceGetInterpolatedHeight(state->source, outer_ring[i+4][0], outer_ring[i+4][2], &(outer_ring[i+4][1]), NULL);
				outer_ring[i+4][1] += 2;

				inner_ring[i+4][0] = radius*hardness - (0.5f*radius*hardness*i) + bottom[0];
				inner_ring[i+4][2] = -radius*hardness + bottom[2];
				inner_ring_valid[i+4] = terrainSourceGetInterpolatedHeight(state->source, inner_ring[i+4][0], inner_ring[i+4][2], &(inner_ring[i+4][1]), NULL);
				inner_ring[i+4][1] += 2;

				outer_ring[i+8][0] = -radius + bottom[0];
				outer_ring[i+8][2] = -radius + (0.5f*radius*i) + bottom[2];
				outer_ring_valid[i+8] = terrainSourceGetInterpolatedHeight(state->source, outer_ring[i+8][0], outer_ring[i+8][2], &(outer_ring[i+8][1]), NULL);
				outer_ring[i+8][1] += 2;

				inner_ring[i+8][0] = -radius*hardness + bottom[0];
				inner_ring[i+8][2] = -radius*hardness + (0.5f*radius*hardness*i) + bottom[2];
				inner_ring_valid[i+8] = terrainSourceGetInterpolatedHeight(state->source, inner_ring[i+8][0], inner_ring[i+8][2], &(inner_ring[i+8][1]), NULL);
				inner_ring[i+8][1] += 2;

				outer_ring[i+12][0] = -radius + (0.5f*radius*i) + bottom[0];
				outer_ring[i+12][2] = radius + bottom[2];
				outer_ring_valid[i+12] = terrainSourceGetInterpolatedHeight(state->source, outer_ring[i+12][0], outer_ring[i+12][2], &(outer_ring[i+12][1]), NULL);
				outer_ring[i+12][1] += 2;

				inner_ring[i+12][0] = -radius*hardness + (0.5f*radius*hardness*i) + bottom[0];
				inner_ring[i+12][2] = radius*hardness + bottom[2];
				inner_ring_valid[i+12] = terrainSourceGetInterpolatedHeight(state->source, inner_ring[i+12][0], inner_ring[i+12][2], &(inner_ring[i+12][1]), NULL);
				inner_ring[i+12][1] += 2;
			}
		}
		else
		{
			for (i = 0; i < 16; i++)
			{
				outer_ring[i][0] = cosf( theta )*radius + bottom[0];
				outer_ring[i][2] = sinf( theta )*radius + bottom[2];
				inner_ring[i][0] = cosf( theta )*radius*hardness + bottom[0];
				inner_ring[i][2] = sinf( theta )*radius*hardness + bottom[2];
				outer_ring_valid[i] = terrainSourceGetInterpolatedHeight(state->source, outer_ring[i][0], outer_ring[i][2], &(outer_ring[i][1]), NULL);
				outer_ring[i][1] += 2;
				inner_ring_valid[i] = terrainSourceGetInterpolatedHeight(state->source, inner_ring[i][0], inner_ring[i][2], &(inner_ring[i][1]), NULL);
				inner_ring[i][1] += 2;
				theta += TWOPI / 16.0f;
			}
		}
		for (i = 0; i < 16; i++)
		{
			if(outer_ring_valid[i] && outer_ring_valid[(i+1)%16])
				gfxDrawLine3DARGB( outer_ring[i], outer_ring[(i+1)%16], 0xffffffff );
			if(inner_ring_valid[i] && inner_ring_valid[(i+1)%16])
				gfxDrawLine3DARGB( inner_ring[i], inner_ring[(i+1)%16], 0xffffff00 );
		}
		if(	state->selected_brush &&
			stricmp(state->selected_brush->name, "Flatten") == 0)
		{
			for (i = 0; i < 16; i++)
			{
				outer_ring[i][1] = state->selected_brush->default_values.float_1 + 2;
			}
			for (i = 0; i < 16; i++)
			{
				gfxDrawLine3DARGB( outer_ring[i], outer_ring[(i+1)%16], 0xff0000ff );
			}
		}
    }
	else if(!state->seleted_image)
	{
		Vec3 line_start;
		Vec3 line_end;

		line_start[0] = bottom[0] - 10;
		line_start[2] = bottom[2] - 10;
		line_end[0] = bottom[0] + 10;
		line_end[2] = bottom[2] + 10;
		terrainSourceGetInterpolatedHeight(state->source, line_start[0], line_start[2], &(line_start[1]), NULL);
		terrainSourceGetInterpolatedHeight(state->source, line_end[0], line_end[2], &(line_end[1]), NULL);
		line_end[1] += 2;
		line_start[1] += 2;
		gfxDrawLine3D_2ARGB(line_start, bottom, 0xffffffff, 0xffffffff);
		gfxDrawLine3D_2ARGB(bottom, line_end, 0xffffffff, 0xffffffff);
		line_start[0] = bottom[0] - 10;
		line_start[2] = bottom[2] + 10;
		line_end[0] = bottom[0] + 10;
		line_end[2] = bottom[2] - 10;
		terrainSourceGetInterpolatedHeight(state->source, line_start[0], line_start[2], &(line_start[1]), NULL);
		terrainSourceGetInterpolatedHeight(state->source, line_end[0], line_end[2], &(line_end[1]), NULL);
		line_end[1] += 2;
		line_start[1] += 2;
		gfxDrawLine3D_2ARGB(line_start, bottom, 0xffffffff, 0xffffffff);
		gfxDrawLine3D_2ARGB(bottom, line_end, 0xffffffff, 0xffffffff);
	}

	//Draw Erode Lines
	if (state->show_erode &&
		!state->painting &&
		state->selected_brush &&
		stricmp(state->selected_brush->name, "Erode") == 0)
	{
		ErodeBrushData erode_options;
		F32 radius = common->brush_diameter/2.f;
		S32 thermal_rad;

		erode_options.soil_removal_rate	= state->selected_brush->default_values.float_1;
		erode_options.rock_removal_rate = state->selected_brush->default_values.float_2;
		erode_options.deposit_rate		= state->selected_brush->default_values.float_3;
		erode_options.carrying_const	= state->selected_brush->default_values.float_4;
		erode_options.remove_multi		= state->selected_brush->default_values.float_5;
		erode_options.deposit_multi		= state->selected_brush->default_values.float_6;

		thermal_rad = (S32)MAX(radius, 20) >> state->source->editing_lod;
		terrainErosionDrawHydraulicErosion(state->source, state->source->visible_lod, 
									((int)cursor_world_pos[0])>>state->source->editing_lod, ((int)cursor_world_pos[2])>>state->source->editing_lod, 
									thermal_rad, thermal_rad, 
									common->brush_strength, 
									erode_options, 
									erode_draw_lines); 
	}
}

static void terEdUIInfoWinBrushName(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char buf[256];
	char *name;

	if((name = terEdGetSelectedBrushName()) != NULL)
		sprintf(buf, "%s", name);
	else
		sprintf(buf, "N/A");

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

static void terEdUIInfoWinBrushPos(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char buf[256];
	TerrainDoc *doc = terEdGetDoc();

	if (doc && doc->state.last_cursor_heightmap)
		sprintf(buf, "<%d, %d>", (int)doc->state.last_cursor_position[0], (int)doc->state.last_cursor_position[2]);
	else
		sprintf(buf, "N/A");

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

static void terEdUIInfoWinHeight(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char buf[256];
	TerrainDoc *doc = terEdGetDoc();

	if (doc && doc->state.last_cursor_heightmap)
		sprintf(buf, "%f ft", doc->state.last_cursor_position[1]);
	else
		sprintf(buf, "N/A");

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}


static void terEdUIInfoWinSelection(const char *indexed_name, EMInfoWinText ***text_lines)
{
	TerrainDoc *doc = terEdGetDoc();
	char buf[256];
	F32 selection;

	if (doc && doc->state.last_cursor_heightmap &&
		terrainSourceGetSelection( doc->state.source, 
		doc->state.last_cursor_position[0], 
		doc->state.last_cursor_position[2], 
		&selection, NULL))
	{
		sprintf(buf, "%f", selection);
	}
	else
	{
		sprintf(buf, "N/A");
	}

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

extern int iTerrainMemoryUsage;

static void terEdUIInfoWinUndo(const char *indexed_name, EMInfoWinText ***text_lines)
{
	char buf[256];
    if (iTerrainMemoryUsage > 1024*1024)
		sprintf(buf, "%0.02f MB", ((float)iTerrainMemoryUsage)/(1024*1024));
    else if (iTerrainMemoryUsage > 1024)
        sprintf(buf, "%0.02f KB", ((float)iTerrainMemoryUsage)/(1024));
    else
        sprintf(buf, "%d B", iTerrainMemoryUsage);
	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

static void terEdUIInfoWinBlock(const char *indexed_name, EMInfoWinText ***text_lines)
{
    TerrainDoc *doc = terEdGetDoc();
	char buf[256];

	if (doc && doc->state.last_cursor_heightmap)
	{
        TerrainBlockRange *range = terrainSourceGetBlockAt(doc->state.source, doc->state.last_cursor_position[0],
													doc->state.last_cursor_position[2]);
        if (range)
			sprintf(buf, "%s", terrainBlockGetName(range));
        else
			sprintf(buf, "N/A");
	}
	else
	{
		sprintf(buf, "N/A");
	}

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

static void terEdUIInfoWinAngle(const char *indexed_name, EMInfoWinText ***text_lines)
{
	TerrainDoc *doc = terEdGetDoc();
	char buf[256];
	U8 normal[3];

	if (doc && doc->state.last_cursor_heightmap &&
		terrainSourceGetNormal( doc->state.source, 
		doc->state.last_cursor_position[0], 
		doc->state.last_cursor_position[2], 
		normal, NULL))
	{
		F32 angle = acos((normal[1]-128.0)/127.0)*180.0/3.14159;
		sprintf(buf, "%f", angle);
	}
	else
	{
		sprintf(buf, "N/A");
	}

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

static void terEdUIInfoWinColor(const char *indexed_name, EMInfoWinText ***text_lines)
{
    TerrainDoc *doc = terEdGetDoc();
	char buf[256];
	Color color;

	if (doc && doc->state.last_cursor_heightmap &&
		terrainSourceGetColor( doc->state.source, 
                              doc->state.last_cursor_position[0], 
                              doc->state.last_cursor_position[2], 
                              &color, NULL))
	{
		sprintf(buf, "%d %d %d", color.r, color.g, color.b);
	}
	else
	{
		sprintf(buf, "N/A");
	}

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

static void terEdUIInfoWinSoilDepth(const char *indexed_name, EMInfoWinText ***text_lines)
{
    TerrainDoc *doc = terEdGetDoc();
	char buf[256];
	F32 soil_depth;

	if (doc && doc->state.last_cursor_heightmap &&
		terrainSourceGetSoilDepth( doc->state.source, 
                                   doc->state.last_cursor_position[0], 
                                   doc->state.last_cursor_position[2], 
                                   &soil_depth, NULL))
	{
		sprintf(buf, "%f", soil_depth);
	}
	else
	{
		sprintf(buf, "N/A");
	}

	eaPush(text_lines, emInfoWinCreateTextLine(buf));
}

static void terEdUIInfoWinMaterials(const char *indexed_name, EMInfoWinText ***text_lines)
{
    TerrainDoc *doc = terEdGetDoc();
	int k;
	char buf[256];
	int material_count=0;
	const char *material_names[NUM_MATERIALS];
	TerrainMaterialWeight *mat_weights;

	if (doc && doc->state.last_cursor_heightmap &&
		(mat_weights = terrainSourceGetMaterialWeights( doc->state.source,
                                                        doc->state.last_cursor_position[0], 
                                                        doc->state.last_cursor_position[2], 
                                                        material_names, &material_count)))
	{
		for(k=0; k < NUM_MATERIALS; k++)
		{
			if (material_names[k]) 
			{
				if(k < material_count)
				{
					sprintf(buf, "%d %s: %f",  k+1, material_names[k], mat_weights->weights[k]);
					eaPush(text_lines, emInfoWinCreateTextLine(buf));
				}
				else
				{
					sprintf(buf, "%d <ERROR CORRUPT DATA>%s: %f", k+1, material_names[k], mat_weights->weights[k]);
					eaPush(text_lines, emInfoWinCreateTextLineWithColor(buf, 0xFF0000FF));
				}
			}
			else if(mat_weights->weights[k] != 0. || k < material_count)
			{
				sprintf(buf, "%d <ERROR MISSING MATERIAL NAME>: %f", k+1, mat_weights->weights[k]);
				eaPush(text_lines, emInfoWinCreateTextLineWithColor(buf, 0xFF0000FF));
			}
		}
	}
	else
	{
		sprintf(buf, "N/A");
		eaPush(text_lines, emInfoWinCreateTextLine(buf));
    }
}

static void terEdUIInfoWinObjects(const char *indexed_name, EMInfoWinText ***text_lines)
{
    TerrainDoc *doc = terEdGetDoc();
	int k;
	char buf[256];
    TerrainObjectEntry **entries = NULL;
    U32 *densities = NULL;

	if (doc && doc->state.last_cursor_heightmap)
    {
		terrainSourceGetObjectDensities(doc->state.source, 
                                        doc->state.last_cursor_position[0], 
                                        doc->state.last_cursor_position[2], 
                                        &entries, &densities);
        for (k = 0; k < eaSize(&entries); k++)
        {
			if(entries[k])
			{
				sprintf(buf, "%d %s: %d",  k+1, layerGetTerrainObjectEntryName(entries[k]), densities[k]);
				eaPush(text_lines, emInfoWinCreateTextLine(buf));
			}
			else
			{
				sprintf(buf, "%d <ERROR MISSING OBJECT NAME>: ", k+1);
				eaPush(text_lines, emInfoWinCreateTextLineWithColor(buf, 0xFF0000FF));
			}
		}
        if (eaSize(&entries) == 0)
        {
            sprintf(buf, "N/A");
            eaPush(text_lines, emInfoWinCreateTextLine(buf));
        }
	}
	else
	{
		sprintf(buf, "N/A");
		eaPush(text_lines, emInfoWinCreateTextLine(buf));
    }

	eaDestroy(&entries);
	eaiDestroy(&densities);
}

void terEdUIRegisterInfoWinEntries(EMEditor *editor)
{
	emInfoWinEntryRegister(editor, "terbrushname",	"Brush Name",			terEdUIInfoWinBrushName);
	emInfoWinEntryRegister(editor, "tercursor",		"Brush Position",		terEdUIInfoWinBrushPos);
	emInfoWinEntryRegister(editor, "terheight",		"Height",				terEdUIInfoWinHeight);
	emInfoWinEntryRegister(editor, "terselection",	"Terrain Selection",	terEdUIInfoWinSelection);
	emInfoWinEntryRegister(editor, "terblock",		"Tile Group",			terEdUIInfoWinBlock);
	emInfoWinEntryRegister(editor, "tercolor",		"Color",				terEdUIInfoWinColor);
	emInfoWinEntryRegister(editor, "terangle",		"Angle",				terEdUIInfoWinAngle);
	emInfoWinEntryRegister(editor, "tersoil",		"Soil Depth",			terEdUIInfoWinSoilDepth);
	emInfoWinEntryRegister(editor, "termaterial",	"Material List",		terEdUIInfoWinMaterials);
	emInfoWinEntryRegister(editor, "terobjects",	"Terrain Objects",		terEdUIInfoWinObjects);
	emInfoWinEntryRegister(editor, "termemusage",	"Terrain Undo Size",	terEdUIInfoWinUndo);
}

void terEdDrawNewBlock(const Vec3 pos, F32 dims[4], bool valid)
{
	Vec3 v1, v2;
	Mat4 mat;
	Color col;
	col.r = col.a = 255;
	col.g = col.b = valid ? 255 : 0;

	setVec3( v1, pos[0]+dims[0], pos[1]-500, pos[2]+dims[1]);
	setVec3( v2, pos[0]+(dims[0]+dims[2]), pos[1]+500, pos[2]+(dims[1]+dims[3]));
	identityMat4(mat);
	gfxDrawBox3D( v1, v2, mat, col, 5);
}

void terEdDrawNewBlockSelection(const Vec3 start_pos, const Vec3 end_pos, bool valid)
{
	Mat4 mat;
	Color col;
	col.r = 127;
	col.a = 255;
	col.g = col.b = valid ? 127 : 0;

	identityMat4(mat);
	gfxDrawBox3D( start_pos, end_pos, mat, col, 0);
}

#endif