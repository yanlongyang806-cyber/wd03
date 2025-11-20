#include "rgb_hsv.h"

#include "GfxTerrain.h"
#include "wlTerrainSource.h"
#include "wlTerrainInline.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

void gfxHeightMapUseVisualizationSoilDepth(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										   TerrainBuffer *buffer, int lod,
										   U8 *data, int display_size)
{
	F32 interp = 1-view_params->view_mode_interp;
	int x, z;
	int lod_diff = (lod-buffer->lod);
	F32 depth;
	Color color;

	//Add color data
	for (z=0; z<display_size; z++)
	{
		for (x=0; x<display_size; x++)
		{
			int sx, sz;
            if (lod_diff >= 0)
            {
                sx = ((x>>2)<<lod_diff);
                sz = ((z>>2)<<lod_diff);
            }
            else
            {
                sx = ((x>>2)>>-lod_diff);
                sz = ((z>>2)>>-lod_diff);
            }

			depth = buffer->data_f32[sx+sz*buffer->size];

			color.b = (depth < 0.1f) ? 255 : 50.0f-(depth*40.0f/MAX_SOIL_DEPTH);
			color.g = (depth < 0.1f) ? 255 : 255.0f-(depth*200.0f/MAX_SOIL_DEPTH);
			color.r = (depth < 0.1f) ? 255 : 10;

			data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0]*color.b/255.0f, color.b, interp);
			data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*color.g/255.0f, color.g, interp);
			data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2]*color.r/255.0f, color.r, interp);
		}
	}
}

void gfxHeightMapUseVisualizationMaterialWeight(TerrainEditorSourceLayer *layer, GfxTerrainViewMode *view_params,
                                                HeightMap *height_map, TerrainBuffer *buffer, 
												int lod, U8 *data, int display_size)
{
	#ifndef NO_EDITORS
	{
		F32 interp = 1-view_params->view_mode_interp;
		int x, z, i;
		U8 material_type = 0xFF;
		int lod_diff = (lod-buffer->lod);
		int mat_idx = -1;
		F32 weight;

		for (i = 0; i < eaiSize(&layer->material_lookup); i++)
		{
			if (layer->material_lookup[i] == view_params->material_type)
			{
				material_type = i;
				break;
			}
		}

		for (i = 0; i < height_map->material_count; i++)
		{
			if (height_map->material_ids[i] == material_type)
			{
				mat_idx = i;
				break;
			}
		}

		//Add color data
		for (z=0; z<display_size; z++)
		{
			for (x=0; x<display_size; x++)
			{
				if (mat_idx != -1)
				{
					weight = buffer->data_material[((x>>2)<<lod_diff)+((z>>2)<<lod_diff)*buffer->size].weights[mat_idx];
					weight = (weight == 0) ? 255 : ((255 - CLAMP(weight, 0, 255)) / 2);
				}
				else
				{
					weight = 255;
				}

				data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0], 255.0f, interp);
				data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*weight/255.0f, weight, interp);
				data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2]*weight/255.0f, weight, interp);
			}
		}
	}
	#endif
}

void gfxHeightMapUseVisualizationObjectDensity(TerrainEditorSourceLayer *layer, GfxTerrainViewMode *view_params,
                                               HeightMap *height_map, TerrainBuffer *buffer, 
											   int lod, U8 *data, int display_size)
{
	#ifndef NO_EDITORS
	{
		F32 interp = 1-view_params->view_mode_interp;
		int x, z, i;
		F32 density;
		int lod_diff = (lod-buffer->lod);
		U8 object_type = 0xFF;
		int object_index = -1;
		Vec3 HSVColor, RBGColor;

		for (i = 0; i < eaiSize(&layer->object_lookup); i++)
		{
			if (layer->object_lookup[i] == view_params->object_type)
			{
				object_type = i;
				break;
			}
		}

		//Find object
		for (i = 0; i < eaSize(&buffer->data_objects); i++)
		{
			if(buffer->data_objects[i]->object_type == object_type)
			{
				object_index = i;
				break;
			}
		}

		//Add color data
		HSVColor[2] = 1.f;
		for (z=0; z<display_size; z++)
		{
			for (x=0; x<display_size; x++)
			{
				if (object_index != -1)
				{
					int sx, sz;
					if (lod_diff >= 0)
					{
						sx = ((x>>2)<<lod_diff);
						sz = ((z>>2)<<lod_diff);
					}
					else
					{
						sx = ((x>>2)>>-lod_diff);
						sz = ((z>>2)>>-lod_diff);
					}

					density = buffer->data_objects[object_index]->density[(sx+sz*buffer->size)];
					//300.f goes all the way to purple
					HSVColor[0] = density == 0.f ? 300.f : (1.f-(log(density)/LOG_MAX_OBJECT_DENSITY))*300.f;
					//Remove the first 75 values (purple) and move them to the end so that blue is our first color
					HSVColor[0] -= 75.f;
					if(HSVColor[0] < 0)
						HSVColor[0] += 300.f;
					HSVColor[1] = density == 0.f ? 0.f : 1.f;
				}
				else
				{
					HSVColor[0] = 0.f;
					HSVColor[1] = 0.f;
				}
	
				hsvToRgb(HSVColor, RBGColor);
				data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0]*RBGColor[2], RBGColor[2]*255, interp);
				data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*RBGColor[1], RBGColor[1]*255, interp);
				data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2]*RBGColor[0], RBGColor[0]*255, interp);
			}
		}
	}
	#endif
}

void gfxHeightMapUseVisualizationGrid(GfxTerrainViewMode *view_params, HeightMap *height_map, 
									  int lod, U8 *data, int display_size)
{
	F32 interp = 1-view_params->view_mode_interp;
	int x, z;
	int dark;
	//Add color data
	for (z=0; z<display_size; z++)
	{
		for (x=0; x<display_size; x++)
		{
			dark =	(x == 0 || z == 0 || x == display_size-1 || z == display_size-1) ? 0 :
					((x%4 == 0 || z%4 == 0) ? 75 : 255);

			data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0]*dark/255.0f, dark, interp);
            data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*dark/255.0f, dark, interp);
			data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2]*dark/255.0f, dark, interp);
		}
	}
}

void gfxHeightMapUseVisualizationExtremeAngles(GfxTerrainViewMode *view_params, HeightMap *height_map, 
											   TerrainBuffer *buffer, int lod,
											   U8 *data, int display_size)
{
	F32 interp = 1-view_params->view_mode_interp;
	int x, z;
	int lod_diff = (lod-buffer->lod);
	U8 angle_check = (U8)((cos(view_params->walk_angle*3.14159/180.0)*128.0)+127);
	//Add color data
	for (z=0; z<display_size; z++)
	{
		for (x=0; x<display_size; x++)
		{
            int sx, sz;
			F32 value;
            if (lod_diff >= 0)
            {
                sx = ((x>>2)<<lod_diff);
                sz = ((z>>2)<<lod_diff);
            }
            else
            {
                sx = ((x>>2)>>-lod_diff);
                sz = ((z>>2)>>-lod_diff);
            }

			value = (buffer->data_normal[sx+sz*buffer->size][1] <= angle_check) ? 0.0f : 1.0f;
			data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0]*value, value*255, interp);
			data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*value, value*255, interp);
            data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2], 255.f, interp);
		}
	}
}

void gfxHeightMapUseVisualizationSelection(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										   TerrainBuffer *buffer, int lod,
										   U8 *data, int display_size)
{
	F32 interp = 1-view_params->view_mode_interp;
	int x, z;
	int lod_diff = buffer ? (lod-buffer->lod) : 0;
	F32 depth;

	//Add color data
	for (z=0; z<display_size; z++)
	{
		for (x=0; x<display_size; x++)
		{
            if (buffer)
            {
                int sx, sz;
                if (lod_diff >= 0)
                {
                    sx = ((x>>2)<<lod_diff);
                    sz = ((z>>2)<<lod_diff);
                }
                else
                {
                    sx = ((x>>2)>>-lod_diff);
                    sz = ((z>>2)>>-lod_diff);
                }

				depth = buffer->data_f32[sx+sz*buffer->size];
            }
            else
			{
				depth = 0;
			}

            depth = 0.5f + 0.5f * depth;

			data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0]*depth, depth*255.f, interp);
			data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*0.5, 128.f, interp);
			data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2]*depth, depth*255.f, interp);
		}
	}
}


void gfxHeightMapUseVisualizationShadow(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										   TerrainBuffer *buffer, int lod,
										   U8 *data, int display_size)
{
	F32 interp = 1-view_params->view_mode_interp;
	int x, z;
	int lod_diff = lod-buffer->lod;
	U8 depths[4], depth;

	//Add color data
	for (z=0; z<display_size; z++)
	{
		for (x=0; x<display_size; x++)
		{
            int sx, sz;
            F32 px, pz;
            px = (x & 3) * 0.25f;
            pz = (z & 3) * 0.25f;
            if (lod_diff >= 0)
            {
                sx = ((x>>2)<<lod_diff);
                sz = ((z>>2)<<lod_diff);
            }
            else
            {
                sx = ((x>>2)>>-lod_diff);
                sz = ((z>>2)>>-lod_diff);
            }

            depths[0] = buffer->data_byte[sx+sz*buffer->size];
            depths[1] = buffer->data_byte[sx+1+sz*buffer->size];
            depths[2] = buffer->data_byte[sx+(sz+1)*buffer->size];
            depths[3] = buffer->data_byte[sx+1+(sz+1)*buffer->size];
            depth = (U8)((depths[0] * (1-px) + depths[1] * px) * (1-pz) +
                         (depths[2] * (1-px) + depths[3] * px) * pz); 

			data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0]*depth/255.0f,  depth, interp);
			data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*depth/255.0f,  depth, interp);
			data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2]*depth/255.0f,  depth, interp);
		}
	}
}

void gfxHeightMapUseVisualizationDesignAttr(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										   TerrainBuffer *buffer, int lod,
										   U8 *data, int display_size)
{
	F32 interp = 1-view_params->view_mode_interp;
	int x, z;
	int lod_diff = lod-buffer->lod;

	//Add color data
	for (z=0; z<display_size; z++)
	{
		for (x=0; x<display_size; x++)
		{
			U8 attr;
 			Color color;
			Vec3 hsv, rgb;
			int sx, sz;
            if (lod_diff >= 0)
            {
                sx = ((x>>2)<<lod_diff);
                sz = ((z>>2)<<lod_diff);
            }
            else
            {
                sx = ((x>>2)>>-lod_diff);
                sz = ((z>>2)>>-lod_diff);
            }

			attr = buffer->data_byte[sx+sz*buffer->size];

			color.a = 255;
			if(attr == 0)
				setVec3(color.rgb,128,128,128);
			else
			{
				setVec3(hsv, fmod(attr*195.0f - 195.0f, 360.0f), 1.0f, 1.0f);
				hsvToRgb(hsv, rgb);
				scaleVec3(rgb, 255.0f, rgb);
				copyVec3(rgb, color.rgb);
			}

			data[(x+z*display_size)*3+0] = lerp(data[(x+z*display_size)*3+0]*color.b/255.0f, color.b, interp);
			data[(x+z*display_size)*3+1] = lerp(data[(x+z*display_size)*3+1]*color.g/255.0f, color.g, interp);
			data[(x+z*display_size)*3+2] = lerp(data[(x+z*display_size)*3+2]*color.r/255.0f, color.r, interp);
		}
	}
}
