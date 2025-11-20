#include "RdrSurface.h"
#include "RdrSurface_h_ast.c"

U32 rdrGetAvailableSurfaceSnapshotIndex(RdrSurface *surface)
{
	U32 i, max_bits = baGetCapacity(surface->used_snapshot_indices);
	for (i = 1; i < max_bits; ++i)
	{
		if (!baIsSetInline(surface->used_snapshot_indices, i))
		{
			baSetBit(surface->used_snapshot_indices, i);
			return i;
		}
	}

	baSizeToFit(&surface->used_snapshot_indices, max_bits * 2);
	baSetBit(surface->used_snapshot_indices, max_bits);
	return max_bits;
}

void rdrReleaseSurfaceSnapshotIndex(RdrSurface *surface, U32 snapshot_index)
{
	if (!snapshot_index)
		return;

	baClearBit(surface->used_snapshot_indices, snapshot_index);
}

RdrTexFlags rdrGetDefaultTexFlagsForSurfaceBufferType(RdrSurfaceBufferType type)
{
	switch (type & SBT_TYPE_MASK)
	{
		case SBT_RGBA:
		case SBT_RGBA10:
		case SBT_RGBA_FIXED:
		case SBT_RGB16:
		case SBT_BGRA:
			return RTF_CLAMP_U|RTF_CLAMP_V;

		case SBT_RGBA_FLOAT:
		case SBT_RGBA_FLOAT32:
		case SBT_RG_FIXED:
		case SBT_RG_FLOAT:
		case SBT_FLOAT:
			// DX11TODO don't need to force point sample on float textures under DX10+, DX11, and should check caps bits on DX9?
			return RTF_CLAMP_U|RTF_CLAMP_V|RTF_MAG_POINT|RTF_MIN_POINT;
	}

	assertmsg(0, "Unknown type!");
	return 0;
}

void rdrSetDefaultTexFlagsForSurfaceParams(RdrSurfaceParams *params)
{
	int i, num_buffers;

	if (params->flags & SF_MRT4)
		num_buffers = 4;
	else if (params->flags & SF_MRT2)
		num_buffers = 2;
	else
		num_buffers = 1;

	for (i = 0; i < num_buffers; ++i)
		params->buffer_default_flags[i] = rdrGetDefaultTexFlagsForSurfaceBufferType(params->buffer_types[i] & SBT_TYPE_MASK);
}

const char * rdrGetSurfaceBufferTypeNameString(RdrSurfaceBufferType type)
{
	return StaticDefineIntRevLookup(RdrSurfaceBufferTypeEnum, type & SBT_TYPE_MASK);
}