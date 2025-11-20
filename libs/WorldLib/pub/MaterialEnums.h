#ifndef MATERIALENUMS_H
#define MATERIALENUMS_H

AUTO_ENUM;
typedef enum 
{
	// You may ONLY add to the end of this list, it is binned, but not dependency-checked
	// If you remove one, you must replace it with an unused element
	SDT_DEFAULT=0,
	SDT_NORMAL=1, // Normal, as in normal mapping
	SDT_TEXCOORD=2,
	SDT_TEXTURE=3, // Name of a texture
	SDT_TEXTURENORMAL=4, // Name of a normalmap texture
	SDT_TEXTURENORMAL_ISDXT5NM=5, // Field that needs to be set if associated normal texture is a DXT5nm texture
	SDT_TEXTURE3D=6, // 3D texture
	SDT_TEXTURECUBE=7, // cubemap texture
	SDT_SCROLL=8, // texture scroll, needs the .xy component multiplied by the
				//  timestep and then shifted to -1 ... 1
	SDT_TIMEGRADIENT=9, // Uses various parameters to convert time of day into 0..1
	SDT_TEXTURE_SCREENCOLOR=10, // Texture which is a grab of the screen, for Refraction effect
	SDT_TEXTURE_SCREENCOLORHDR=11, // Texture which is a grab of the screen, for Refraction effect
	SDT_TEXTURE_SCREENDEPTH=12, // Texture which is a grab of the screen, for Refraction effect
	SDT_TEXTURE_SCREENOUTLINE=13, // Texture containing the outlining weights, for effects
	SDT_TEXTURE_DIFFUSEWARP=14, // Appropriate diffuse warping texture
	SDT_OSCILLATOR=15,
	SDT_ROTATIONMATRIX=16, // Takes a scale and rotation value for rotating UVs
	SDT_LIGHTBLEEDVALUE=17, // Transforms x into atan(x)/(1+atan(x)), 1/(1+atan(x)) for light bleeding
	SDT_CHARACTERBACKLIGHTCOLOR=18, // Pulled from the sky file
	SDT_SKYCLEARCOLOR=19, // Pulled from the sky file
	SDT_SPECULAREXPONENTRANGE=20,
	SDT_FLOORVALUES=21, // x, 1/x, y, 1/y for FLOOR operation
	SDT_TEXTURE_SCREENCOLOR_BLURRED=22,
	SDT_TEXTURE_REFLECTION=23, // Special reflection cubemap/spheremap texture
	SDT_TEXTURE_AMBIENT_CUBE=24, // Ambient cube texture from sky
	SDT_PROJECT_SPECIAL=25, // Project-defined special global
	SDT_TEXTURENORMALDXT5NM=26, // DXT5nm texture only
	SDT_TEXTURE_HEIGHTMAP=27, // Heightmap texture
	SDT_HEIGHTMAP_SCALE=28, // Heightmap scale
	SDT_UV_SCALE=29,		// Multiplier on the uvs
	SDT_TEXTURE_STRETCH=30, // Inverse of uv scale
	SDT_SINGLE_SCROLL=31, // Same as SDT_SCROLL with only the .x component

	// Second part of the list, must only add to the end of this if it is a drawable constant
	SDT_NON_DRAWABLE_END,
	// Note, any after this one are special (get draw-time, per-object mapping instead of per-material)
#define SDT_DRAWABLE_START 100 // If you modify this, you must modify MaterialConstantMappingFake to cause world cells to rebin - BAD!
	SDT_INVMODELVIEWMATRIX = SDT_DRAWABLE_START,
	SDT_VOLUMEFOGMATRIX=101,
	SDT_SUNDIRECTIONVIEWSPACE=102,
	SDT_SUNCOLOR=103,
	SDT_MODELPOSITIONVIEWSPACE=104,
	SDT_UNIQUEOFFSET=105,
#define SDT_DRAWABLE_END 105	// This MUST be set to the highest value contained within this enumeration.
} ShaderDataType;

STATIC_ASSERT(SDT_DRAWABLE_START >= SDT_NON_DRAWABLE_END);

__forceinline static bool shaderDataTypeNeedsDrawableMapping(ShaderDataType data_type)
{
	return (data_type >= SDT_DRAWABLE_START);
}

AUTO_ENUM;
typedef enum ShaderGraphReflectionType
{
	SGRAPH_REFLECT_NONE,
	SGRAPH_REFLECT_SIMPLE,
	SGRAPH_REFLECT_CUBEMAP,
} ShaderGraphReflectionType;
extern StaticDefineInt ShaderGraphReflectionTypeEnum[];

// List must be contiguous
AUTO_ENUM;
typedef enum ShaderGraphQuality
{
	SGRAPH_QUALITY_MIN,
	SGRAPH_QUALITY_MIN1,
	SGRAPH_QUALITY_MIN2,
	SGRAPH_QUALITY_MIN3,
	SGRAPH_QUALITY_LOW,
	SGRAPH_QUALITY_LOW1,
	SGRAPH_QUALITY_LOW2,
	SGRAPH_QUALITY_LOW3,
	SGRAPH_QUALITY_MID,
	SGRAPH_QUALITY_MID1,
	SGRAPH_QUALITY_MID2,
	SGRAPH_QUALITY_MID3,
	SGRAPH_QUALITY_HIGH,
	SGRAPH_QUALITY_HIGH1,
	SGRAPH_QUALITY_HIGH2,
	SGRAPH_QUALITY_HIGH3,
	SGRAPH_QUALITY_VERY_HIGH,
	SGRAPH_QUALITY_VERY_HIGH1,
	SGRAPH_QUALITY_VERY_HIGH2,
	SGRAPH_QUALITY_VERY_HIGH3,
	SGRAPH_QUALITY_MAX,
} ShaderGraphQuality;
extern StaticDefineInt ShaderGraphQualityEnum[];

#define SGRAPH_QUALITY_INTERVAL_SIZE 4
#define SGRAPH_QUALITY_OLD_MAX 3

// set to be equal to the last value in the list.
#define SGRAPH_QUALITY_MAX_VALUE SGRAPH_QUALITY_MAX

#endif
