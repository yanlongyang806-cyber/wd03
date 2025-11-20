#pragma once

typedef enum TerrainBufferType
{
	TERRAIN_BUFFER_HEIGHT = 0,		// F32 (editable)
	TERRAIN_BUFFER_COLOR,			// U8Vec3 (BGR, editable)
	TERRAIN_BUFFER_MATERIAL,		// TerrainMaterialWeight (editable)
	TERRAIN_BUFFER_NORMAL,			// TerrainCompressedNormal
	TERRAIN_BUFFER_SOIL_DEPTH,		// F32
	TERRAIN_BUFFER_OBJECTS,			// TerrainObjectBuffer *(editable)
	TERRAIN_BUFFER_ALPHA,			// F32 (editable)
    TERRAIN_BUFFER_OCCLUSION,		// U8
    TERRAIN_BUFFER_SELECTION,		// F32
    TERRAIN_BUFFER_SHADOW,			// U8
	TERRAIN_BUFFER_ATTR,			// U8
	TERRAIN_BUFFER_NUM_TYPES,
} TerrainBufferType;
