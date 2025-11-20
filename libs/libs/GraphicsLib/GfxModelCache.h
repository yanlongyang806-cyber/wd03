#ifndef GFXMODELCACHE_H
#define GFXMODELCACHE_H
GCC_SYSTEM

#include "RdrEnums.h"

typedef struct ModelLOD ModelLOD;
typedef struct GeoRenderInfo GeoRenderInfo;

void gfxModelEnterUnpackCS(void);
void gfxModelLeaveUnpackCS(void);

void gfxModelSetupZOTris(ModelLOD *model);
void gfxModelUnpackVertexData(ModelLOD *model);

void gfxModelLODUpdateFromRawGeometry(ModelLOD *model, RdrGeoUsageBits primary_usage, bool load_in_foreground);
void gfxModelLODFromRawGeometry(ModelLOD *model, Vec3 *verts, int tri_count, 
						  Vec2 *texcoords, Vec3 *normals, RdrGeoUsageBits primary_usage, bool load_in_foreground);

#endif
