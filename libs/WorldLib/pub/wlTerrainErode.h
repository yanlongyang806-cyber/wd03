#pragma once
GCC_SYSTEM

typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct HeightMapCache HeightMapCache;

typedef struct ErodeBrushData
{
	F32 rock_removal_rate;		//How quickly to pick up rock //Thermal = What angle to remove rock at
	F32 soil_removal_rate;		//How quickly to pick up soil //Thermal = What angle to remove soil at
	F32 deposit_rate;			//How quickly to deposit soil
	F32 carrying_const;			//Multiplied by the angle to determine how much soil water can hold.  
	F32 remove_multi;			//Suppresses the amount of rock and soil actually removed.  Does not effect amount in water
	F32 deposit_multi;			//Suppresses the amount of soil actually deposited.  Does not effect amount in water
} ErodeBrushData;

typedef void (*draw_line_function)(const Vec3 p1, const Vec3 p2, int color);

#ifndef NO_EDITORS

void terrainErosionDrawHydraulicErosion( TerrainEditorSource *source, U32 lod, S32 x, S32 y, S32 width, S32 height, F32 flow, ErodeBrushData erode_options, draw_line_function draw_cb );
void terrainErosionDrawThermalErosion( TerrainEditorSource *source, S32 x, S32 z, int draw_lod, F32 rate, F32 soil_angle, F32 rock_angle, HeightMapCache *cache);

#endif
