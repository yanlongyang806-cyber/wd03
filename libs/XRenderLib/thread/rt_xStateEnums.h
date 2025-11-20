

// Make sure that all register changes are reflected in rt_xshaderdata.c as well as vs_inc.hlsl
enum
{
	// sky buffer registers	(7 registers)	(c0-c6) (cb0)


	VS_CONSTANT_SKY_DOME_DIRECTION = 0,
	VS_CONSTANT_SKY_DOME_COLOR_FRONT = 1,
	VS_CONSTANT_SKY_DOME_COLOR_BACK = 2,
	VS_CONSTANT_SKY_DOME_COLOR_SIDE = 3,

	VS_CONSTANT_FOGCOLOR_LOW = 4,
	VS_CONSTANT_FOGCOLOR_HIGH = 5,
	VS_CONSTANT_FOGDIST = 6,




	// per frame buffer registers (10 registers)	(c7-c16) (cb1)
	VS_CONSTANT_VIEW_MAT = 7,

	VS_CONSTANT_SCREEN_SIZE = 11,
	VS_CONSTANT_PP_TEX_SIZE = 12,
	VS_CONSTANT_MORPH_AND_VLIGHT = 13,

	VS_CONSTANT_VIEW_TO_WORLD_Y = 14,
	VS_CONSTANT_FOG_HEIGHT_PARAMS = 15,

	VS_CONSTANT_AMBIENT_LIGHT = 16,




	// per object buffer registers (23 registers)	(c17-c39) (cb2)
	VS_CONSTANT_PROJ_MAT = 17,
	VS_CONSTANT_MODELVIEW_MAT = 21,
	VS_CONSTANT_MODEL_MAT = 25,
	VS_CONSTANT_XBOX_INSTANCE_DATA = 29,

	VS_CONSTANT_BASEPOSE_OFFSET = 30,

	VS_CONSTANT_CAMERA_POSITION_VS = 30,


	VS_CONSTANT_COLOR0 = 31,
	VS_CONSTANT_VERTEX_ONLY_LIGHT_PARAMS = 32, // 32-39




	// special per object buffer registers (5 registers)	(c40-c44) (cb3)
	VS_CONSTANT_WORLD_TEX_PARAMS = 40,

	VS_CONSTANT_GLOBAL_INSTANCE_PARAM = 42,

	VS_CONSTANT_WIND_PARAMS = 43, 
	VS_CONSTANT_EXPOSURE_TRANSFORM = 44,






	// special parameters (5 registers)		(c45-c50) (cb4)
	VS_CONSTANT_SPECIAL_PARAMETERS = 45,




























	// fast particle and animation registers (156 registers)	(c50-c205) (cb5)	// This is going on the end because the animation registers may get pushed to the texture buffer
	VS_CONSTANT_FAST_PARTICLE_INFO = 50,



	VS_CONSTANT_CYLINDER_TRAIL = 50,



	VS_CONSTANT_BONE_MATRIX_START = 50,
};

// Domain shader register placement.
enum
{
	DS_CONSTANT_PROJ_MAT = 0,
	DS_CONSTANT_MODELVIEW_MAT = 4,
	DS_CONSTANT_MODEL_MAT = 8,
	DS_CONSTANT_SCALE_OFFSET = 12,
};