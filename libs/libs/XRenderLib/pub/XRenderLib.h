#pragma once

// Changes to this file also need to be reflected in d3d11_ps_constants.hlsl as well as d3d9_ps_constants.hlsl

// So the material editor can get access to the pixel shader
// constants, these offsets are public.
enum
{
	// MANAGED BLOCK : To be able to set parameters per-tile, we must manage an aligned block of 4 constants  (b0)
	PS_START_VIEWPF_CONSTANTS				= 0,
	PS_CONSTANT_INV_SCREEN_PARAMS			= 0,
	PS_CONSTANT_PROJ_MAT_Z					= 1,
	PS_CONSTANT_PROJ_MAT_W					= 2,
	PS_CONSTANT_DEPTH_RANGE					= 3,
	PS_END_VIEWPF_CONSTANTS					= 4,

	// cbuffer for pixel shader constants containing sky information  (b4)
	// These values need to be moved to the END of what is allowed for PS2 so it can be combined with PS_CONSTANT_AMBIENT_LIGHT
	PS_CONSTANT_SKY_OFFSET					= 4,	// This is used in DX9 to ensure that any sky buffer parameter will receive the proper offset in the constant buffer

	PS_CONSTANT_SKY_DOME_COLOR_FRONT		= 0,
	PS_CONSTANT_SKY_DOME_COLOR_BACK			= 1,
	PS_CONSTANT_SKY_DOME_COLOR_SIDE			= 2,

	// cbuffer for pixel shader constants containing sky information  (b1)
	PS_CONSTANT_FOG_COLOR_LOW				= 3,
	PS_CONSTANT_FOG_COLOR_HIGH				= 4,
	PS_CONSTANT_FOG_DIST					= 5,

	PS_CONSTANT_EXPOSURE_TRANSFORM			= 6,

	// constants 11 to 123  (b2 and b3)	Will have to be split in to two different constant buffers.  One for material inputs and one for light parameters.
	PS_CONSTANT_MATERIAL_PARAM_OFFSET		= 11, 	// This is used in DX9 to ensure that any material and light buffer parameter will receive the proper offset in the constant buffer

	PS_CONSTANT_MATERIAL_PARAM_MAX			= 123,

	// SM30 parameters only
	// cbuffer (b4). notice how this is the same buffer number as the sky_dome_color buffer!  This is because all values for this buffer are set in rxbxSetupAmbientLight
	PS_CONSTANT_MISCPF_OFFSET	= 124,
	PS_CONSTANT_AMBIENT_LIGHT				= 0,

	// cbuffer (b5)
	PS_CONSTANT_INVVIEW_MAT					= 1, // 1-3
	
	PS_CONSTANT_SCREEN_RESOLUTION			= 4,
	// using constants 32 and above requires PS 3.0
};


void rxbxAddIntrinsicDefinesForXbox(void); // To get at Xbox defines when running on PC
void rxbxAddIntrinsicDefinesForPS3(void); // To get at PS3 defines when running on PC

