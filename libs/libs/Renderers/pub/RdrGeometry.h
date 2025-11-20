#ifndef _RDRGEOMETRY_H_
#define _RDRGEOMETRY_H_
GCC_SYSTEM

#include "RdrDevice.h"
#include "RdrEnums.h"

typedef int GeoHandle;

// There is a massive combinatoric array of these.  They will not correspond 1:1 to DX vertex declarations, as DX vertex declarations
// are significantly more powerful (multiple streams), and the combinatorics would really get out of control
typedef struct RdrVertexDeclaration
{
	U16 position_offset;
	U16 normal_offset;
	U16 tangent_offset;
	U16 binormal_offset;
	U16 texcoord_offset;
	U16 texcoord2_offset;
	U16 boneweight_offset;
	U16 boneid_offset;
	U16 color_offset;
	U16 stride;
} RdrVertexDeclaration;

typedef struct RdrGeoUsage
{
	// If we get to where we are doing full de-interleaving, this will be a little silly, because each field would be 1 bit.  I chose to do this
	// because it worked conveniently with the existing RdrVertexDeclaration.  If we switch to a more DX-like vertex description, that table would go away
	RdrGeoUsageBits bits[4];
	// a unique signature to be used as a key to a stash table of platform vertex declarations.  Does double duty as a "generic" descriptor, since
	// we don't know which "bits" field a component will be in
	RdrGeoUsageBits key;
	S8				iNumPrimaryStreams; // If we have morph data, we will have one extra stream (and one extra "bits")
	bool			bHasSecondary;		// This is what serves as an abstraction in our engine for a second channel of positions (and normals)
} RdrGeoUsage;

// This struct exists only for the duration of a geometry update command.  It is the transfer mechanism to the render thread.
// It's also a base member of RdrGeometryDataDX.  The temporary struct is copied into the DX one
typedef struct RdrGeometryData
{
	GeoHandle handle;

	// triangle data
	int				tri_count;
	U16				*tris;			

	// subobject data
	int				subobject_count;
	int				*subobject_tri_counts;
	int				*subobject_tri_bases;

	// vertex data
	int				vert_count;
	RdrGeoUsage		vert_usage;
	U8				*vert_data;

	const char * debug_name;
} RdrGeometryData;

__forceinline static RdrGeometryData *rdrStartUpdateGeometry(RdrDevice *device, GeoHandle handle, RdrGeoUsage usage, U32 extra_bytes)
{
	RdrGeometryData *data = (RdrGeometryData *)wtAllocCmd(device->worker_thread, RDRCMD_UPDATEGEOMETRY, sizeof(RdrGeometryData) + extra_bytes);
	if (data)
	{
		ZeroStruct(data);
		data->handle = handle;
		data->vert_usage = usage;
	}
	return data;
}
__forceinline static void rdrEndUpdateGeometry(RdrDevice *device) { wtSendCmd(device->worker_thread); }

__forceinline static void rdrFreeGeometry(RdrDevice *device, GeoHandle handle) { wtQueueCmd(device->worker_thread, RDRCMD_FREEGEOMETRY, &handle, sizeof(handle)); }
__forceinline static void rdrFreeAllGeometry(RdrDevice *device) { wtQueueCmd(device->worker_thread, RDRCMD_FREEALLGEOMETRY, 0, 0); }

__inline static RdrVertexDeclaration *rdrGetVertexDeclaration(RdrGeoUsageBits usage)
{
	extern RdrVertexDeclaration rdr_vertex_declarations[RUSE_NUM_COMBINATIONS];
	return &rdr_vertex_declarations[usage & RUSE_MASK];
}

// I am using RdrVertexDeclaration as the mechanism for knowing stride and offset for each of the streams, since that is
// essentially what it does.  However, it is not clear that going forward, that this is how we should handle it.
__inline int rdrGeoGetTotalVertSize(RdrGeoUsage * pUsage)
{
	int i;
	int iBytes = 0;
	for (i=0;i<pUsage->iNumPrimaryStreams;i++)
	{
		iBytes += rdrGetVertexDeclaration(pUsage->bits[i])->stride;
	}

	return iBytes;
}

#endif //_RDRGEOMETRY_H_


