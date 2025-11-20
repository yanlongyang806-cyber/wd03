/***************************************************************************



***************************************************************************/

#ifndef _GFXLIGHTSHADOWSEARCH_H_
#define _GFXLIGHTSHADOWSEARCH_H_
GCC_SYSTEM

#include "MemoryPool.h"


typedef struct GfxLight GfxLight;
typedef struct Room Room;
typedef struct RoomPortal RoomPortal;
typedef struct AStarSearchData AStarSearchData;
typedef struct Frustum Frustum;

typedef struct ShadowSearchNode ShadowSearchNode;


typedef struct ShadowSearchEdge
{
	F32 length;
	ShadowSearchNode* other_node;
	int other_direction_idx; //the index of the other direction of this edge on other_node
} ShadowSearchEdge;

typedef struct ShadowSearchNode
{
	GfxLight* light; //it's either a light, a portal, or neither (for the camera)
	RoomPortal* portal;

	struct  
	{
		ShadowSearchEdge* data;
		int count;
		int size;
	} dyn_edges;

	void* astar_info;
	bool visit_flag;
} ShadowSearchNode;

typedef struct ShadowSearchData
{
	AStarSearchData* astar_data;
	MP_DEFINE_MEMBER(ShadowSearchNode);
	ShadowSearchNode* camera_node;
	Room** ea_camera_rooms;
	Vec3 camera_pos, camera_vec, camera_foc;
	int debug_max_visits, debug_visit_count;
} ShadowSearchData;

void gfxInitShadowSearchData(const Vec3 camera_focal_pt, const Vec3 camera_pos, const Vec3 camera_vec);
void gfxClearShadowSearchDataVisitFlags();
F32	 gfxComputeShadowSearchLightDistance(GfxLight* light);
void gfxSortShadowcasters(const Frustum *camera_frustum, const Vec3 camera_focal_pt);


#endif//_GFXLIGHTSHADOWSEARCH_H_
