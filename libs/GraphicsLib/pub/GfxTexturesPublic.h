#ifndef GFXTEXTURESPUBLIC_H
#define GFXTEXTURESSPUBLIC_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "GfxTextureEnums.h"
#include "WorldLibEnums.h"
#include "MaterialEnums.h"
#include "RdrTextureEnums.h"
#include "RdrEnums.h"
#include "WTex.h"

// Enable ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE option to trace various parts of texture lifetime. 
// Part of the logging (gfxTraceTexLifetime, DEBUG_MARK_*, etc) traces usage to textures 
// when they are being loaded or deleted and is targeted more for dynamic (texGen) 
// BasicTextures such as bodysock textures. The other part is detailed logging of 
// state through many parts of code flow, but only for textures marked with 
// bEnableDetailedLifetimeLog. Note that enabling this compile directive (currently) 
// increases BasicTexture size. See texLog and texLogState for more information.
#define ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE 0

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
void gfxTraceTexLifetime(const char * format, ...);
#define DEBUG_MARK_TEXTURE_QUEUED_FOR_DELETE(texture)	(texture->queued_delete = 1)
#define DEBUG_MARK_TEXTURE_QUEUED_FOR_DRAW(texture)	(texture->queued_draw = 1)
#define DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DELETE(texture)	(texture->queued_delete = 0)
#define DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DRAW(texture)	(texture->queued_draw = 0)
#else
#define gfxTraceTexLifetime(fmt, ...)
#define DEBUG_MARK_TEXTURE_QUEUED_FOR_DELETE(texture)
#define DEBUG_MARK_TEXTURE_QUEUED_FOR_DRAW(texture)
#define DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DELETE(texture)
#define DEBUG_CLEAR_TEXTURE_QUEUED_FOR_DRAW(texture)
#endif

typedef struct TexWordParams TexWordParams;
typedef struct TexWordLoadInfo TexWordLoadInfo;
typedef struct TexMemUsage TexMemUsage; // In GfxTextureTools.h
typedef struct RdrSurface RdrSurface;
typedef struct BasicTexture BasicTexture;
typedef struct MemLog MemLog;
typedef U64 TexHandle;

#define RAW_DATA_BITMASK (1<<31)
#define NOT_RAW_DATA_BITMASK (~RAW_DATA_BITMASK)

typedef struct TexReadInfo
{
	U8		*texture_data;
	int		tex_format;				//One of RdrTexFormat or TEXFMT_RAW_DDS
	U16		width,height,depth;		//texture height and width - stores the actual w/h after mip reduction, but isn't used
	U16		level_count;			//number of mip maps for the dds
	U32		size;					//bytes of texture data
	U32		is_low_mip:1;			//whether this is an info for the low mips, or for a full texture
	U32		free_low_mip:1;			//If this is a low mip, whether or not to free the data
	U32		ringbuffer_data:1;		//Indicates texture_data was allocated with rdrTextureLoadAlloc. Otherwise, memrefAlloc.
} TexReadInfo;

typedef enum TexMemIndex
{
	TEX_MEM_VIDEO,
	TEX_MEM_RAW,
	TEX_MEM_LOADING,
	TEX_MEM_MAX,
} TexMemIndex;

typedef struct BasicTextureRareData {
	/* loaded only, raw only	 */ U32		tex_last_used_time_stamp_raw; // Must be first 4 bytes - logic in texRareDataOptionallyRelease

	/* raw only, while loading UI*/ TexReadInfo *bt_rawInfo;
	/* only TexWords			 */ BasicTexture *baseTexture;			// If a texWord, the base texture, for getting low mips

	// Texture compositing
	/* only TexWords			 */ TexWord *texWord;
	/* only dynamic TexWords	 */ TexWordParams *texWordParams;		// For dynamic textures
	/* only TexWords while loading */ TexWordLoadInfo *texWordLoadInfo;

	/* only TexWords			 */ U16		origWidth, origHeight;
	/* only on 3D textures rare	 */ U16		realDepth;					// This is 3d texture depth - placed here for bitpacking
	/* only from rare TexOpt	 */ U8		tex_reduce_scale;			// Scale to be applied to texel density for known scaled textures
	U8		unused;  // needs to be zeroed on init, free for later use
	/* raw only					 */ U32		rawReferenceCount:15;		// Count of texLoadRawData()s outstanding
	/* only dynamic texwords     */ U32		dynamicReferenceCount:15;	// Count of dynamic textures using this TexBind
	/* only bodysocks, rare		 */ U32		dont_free_handle:1;			// For when the handle is taken care of by external code
	/* TexWords only			 */ U32		hasBeenComposited:1;		// If the texture has a texWord*, has it been applied?

} BasicTextureRareData;

typedef struct BasicTextureLoadedData
{
	/* loaded only				 */ U32		tex_is_loading_for;			// Which renderer needs this data when it's loaded.  Bit mask, 31 = raw, 0...30 = renderers
	/* loaded only, 64 bits		 */ TexHandle mip_handle;
	/* loaded only				 */ U32		tex_reduce_last_time;
	/* loaded only				 */ U32		mip_bound_on;				// Bitmask of which render has has the low mip bound on it
	/* loaded only				 */ F32		uv_density;					// LOD selection factor computed from the mesh texel density
	/* loaded only				 */ F32		min_draw_dist;				// Smallest distance at which this texture is used on the last few frames

	/* loaded only				 */ U32		tex_memory_use[TEX_MEM_MAX];		// 0 is video memory size, 1 is raw size, 2 is -reducemip 1, 3 is original size, see TEX_MEM_*

	/* loaded only, budgets only */ U32		tex_last_used_count:16;	// Count of uses since the last time this was reset (by budget meters window)
	/* loaded only				 */ U32		mip_loading:1;			// The low mip has been queued for loading to all renderers
	/* loaded only				 */ U32		loading:1;				// Has a request been sent to the loading thread to load non-raw data
	/* loaded only				 */ U32		tex_is_unloading:1;		// Has been queued up to be unloaded on all devices.  Do not start a load if this is set
	/* loaded only				 */ U32		levels_loaded:4;		// Number of texture levels currently loaded off disk, or in queues for loading
} BasicTextureLoadedData;

#pragma pack(push, 4)
AUTO_STRUCT;
typedef struct BasicTexture
{
	const char	*name;

	AST_STOP
	BasicTexture *actualTexture; // This may be set to something different if the locale has swapped textures, or used in TexWords

	U16		width, height;			// Width and height on disk
	U16		realWidth, realHeight;	// Size of the texture actually in memory. May be larger for NPOT, smaller for mipmapped

	BasicTextureRareData *rare; // Rarely used members.  Do not reference this member directly use texGet/AllocRareData()
	BasicTextureLoadedData *loaded_data; // Members only used while the texture is loaded

	TexHandle tex_handle;				// RenderLib handle
	U32		tex_last_used_time_stamp;
	U32		tex_is_loaded;				// Which renderer has been sent this data.  Bit mask, 31 = raw, 0...30 = renderers

	// Flags and bits
	U32		rdr_format:RdrTexFormat_NumBits;
	U32		file_pos:10;							//Offset of the texture data from file start
	TexFlags flags:TexFlags_NumBits;				//texture flags (TEX_ALPHA,TEX_JPEG, etc)
	U32		use_category:WLUsageFlags_NumBits;		//what is this texture used for?
	U32		has_rare:1;

	U32		mip_type:2;
	U32		mip_data_index:16; // Indexes up to 2MB of low MIPs
	U32		max_levels:4;
	U32		base_levels:4;			// Number of levels in the base .wtex file (rest are in htex)
	U32		max_desired_levels:4;	// Most levels requested this frame

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
	U32		queued_draw:1;
	U32		queued_delete:1;
	// Trace out the state of the BasicTexture and the BasicTextureLoadedData throughout entire
	// use cycle of the texture. See texLog and texLogState.
	U32		bEnableDetailedLifetimeLog:1;
#endif

	// Rarely accessed data at the end
	/* only some needed (12 bits) hard to filter/pack */ TexOptFlags bt_texopt_flags;
	const char	*fullname;

	AST_START
} BasicTexture;
#pragma pack(pop)

// dynamic textures
BasicTexture *texRegisterDynamic(const char *texturepath);
void texUnregisterDynamic(const char *texturepath);

bool texIsNormalmap(const BasicTexture *texBind);
bool texIsDXT5nm(const BasicTexture *texBind);
bool texIsCubemap(const BasicTexture *texBind);
bool texIsVolume(const BasicTexture *texBind);

__forceinline void texEnableDetailedLifetimeLogIfAvailable(BasicTexture * bind)
{
#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
	bind->bEnableDetailedLifetimeLog = true;
#endif
}

#endif
