#include "RdrDrawList.h"
#include "rt_xdrawlist.h"
#include "../RdrDrawListPrivate.h"
#include "rt_xmodel.h"
#include "rt_xparticle.h"
#include "rt_xprimitive.h"
#include "rt_xgeo.h"

#include "qsortG.h"
#include "EventTimingLog.h"

static int rdrOpaqueCmpVisual(const RdrSortNode **sna_p, const RdrSortNode **snb_p)
{
	const RdrSortNode *sna = *sna_p;
	const RdrSortNode *snb = *snb_p;
	int t;

	t = (int)snb->draw_type - (int)sna->draw_type;
	if (t)
		return t;

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->vtxbuf) - PTR_TO_UINT(sna->vtxbuf);
	if (t)
		return t;
#endif

	t = (int)snb->draw_shader_handle[RDRSHDM_VISUAL] - (int)sna->draw_shader_handle[RDRSHDM_VISUAL];
	if (t)
		return t;

	t = (int)snb->uberlight_shader_num.lightMask - (int)sna->uberlight_shader_num.lightMask;
	if (t)
		return t;

	t = (int)snb->uberlight_shader_num.shaderMask - (int)sna->uberlight_shader_num.shaderMask;
	if (t)
		return t;

	t = (intptr_t)snb->material - (intptr_t)sna->material;
	if (t)
		return t;

#if !SUPPORT_VERTEX_BUFFER_COMBINE
	t = (int)snb->geo_handle_primary - (int)sna->geo_handle_primary;
	if (t)
		return t;
#endif
	
	return (intptr_t)snb->drawable - (intptr_t)sna->drawable;
}

static int rdrOpaqueCmpPrepass(const RdrSortNode **sna_p, const RdrSortNode **snb_p)
{
	const RdrSortNode *sna = *sna_p;
	const RdrSortNode *snb = *snb_p;
	int t;

	t = (int)snb->draw_type - (int)sna->draw_type;
	if (t)
		return t;

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->vtxbuf) - PTR_TO_UINT(sna->vtxbuf);
	if (t)
		return t;
#endif

	t = (int)snb->draw_shader_handle[RDRSHDM_ZPREPASS] - (int)sna->draw_shader_handle[RDRSHDM_ZPREPASS];
	if (t)
		return t;

	t = (intptr_t)snb->material - (intptr_t)sna->material;
	if (t)
		return t;

	t = (int)snb->geo_handle_primary - (int)sna->geo_handle_primary;
	if (t)
		return t;

	return (intptr_t)snb->drawable - (intptr_t)sna->drawable;
}

static int rdrOpaqueCmpShadow(const RdrSortNode **sna_p, const RdrSortNode **snb_p)
{
	const RdrSortNode *sna = *sna_p;
	const RdrSortNode *snb = *snb_p;
	int t;

	t = (int)snb->draw_type - (int)sna->draw_type;
	if (t)
		return t;

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->vtxbuf) - PTR_TO_UINT(sna->vtxbuf);
	if (t)
		return t;
#endif

	t = (int)snb->draw_shader_handle[RDRSHDM_SHADOW] - (int)sna->draw_shader_handle[RDRSHDM_SHADOW];
	if (t)
		return t;

	t = (intptr_t)snb->material - (intptr_t)sna->material;
	if (t)
		return t;

	t = (int)snb->geo_handle_primary - (int)sna->geo_handle_primary;
	if (t)
		return t;

	return (intptr_t)snb->drawable - (intptr_t)sna->drawable;
}

static int rdrOpaqueCmpVisualZBucket(const RdrSortNode **sna_p, const RdrSortNode **snb_p)
{
	const RdrSortNode *sna = *sna_p;
	const RdrSortNode *snb = *snb_p;
	int t;

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->vtxbuf) - PTR_TO_UINT(sna->vtxbuf);
	if (t)
		return t;
#endif

	t = sna->zbucket - snb->zbucket; // sort front to back
	if (t)
		return t;

	t = (int)snb->draw_type - (int)sna->draw_type;
	if (t)
		return t;

	t = (int)snb->draw_shader_handle[RDRSHDM_VISUAL] - (int)sna->draw_shader_handle[RDRSHDM_VISUAL];
	if (t)
		return t;

	t = (intptr_t)snb->material - (intptr_t)sna->material;
	if (t)
		return t;

	t = (int)snb->geo_handle_primary - (int)sna->geo_handle_primary;
	if (t)
		return t;

	return (intptr_t)snb->drawable - (intptr_t)sna->drawable;
}

// This array is a look-up table to define a rendering order for 
// each draw-list entry type. This controls the sorting of these elements for
// rendering, before the z-order sort. The sort values are a relative priority;
// larger values draw later.
//
// At this time, tree leaf and leaf mesh geometry has the highest priority, and
// will be drawn first, in depth-first order, then tree billboards, and so on.
static const BYTE draw_type_sort_order[] =
{
	6,//RTYPE_MODEL,
	7,//RTYPE_SKINNED_MODEL,
	7,//RTYPE_CURVED_MODEL,
	5,//RTYPE_TERRAIN,
	8,//RTYPE_PRIMITIVE,
	8,//RTYPE_PRIMITIVE_MESH,
	9,//RTYPE_PARTICLE,
	10,//RTYPE_FASTPARTICLES,
	11,//RTYPE_TRISTRIP,
	12,//RTYPE_CLOTHMESH,
	13,//RTYPE_CYLINDERTRAIL,
	14,//RTYPE_STARFIELD,
};
STATIC_ASSERT(ARRAY_SIZE(draw_type_sort_order) == RTYPE_END);

static int rdrOpaqueCmpVisualStateZBucket(const RdrSortNode **sna_p, const RdrSortNode **snb_p)
{
	const RdrSortNode *sna = *sna_p;
	const RdrSortNode *snb = *snb_p;
	int t;

	t = draw_type_sort_order[ (int)snb->draw_type ] - draw_type_sort_order[ (int)sna->draw_type ];
	if (t)
		return t;

	t = sna->zbucket - snb->zbucket; // sort front to back
	if (t)
		return t;

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->vtxbuf) - PTR_TO_UINT(sna->vtxbuf);
	if (t)
		return t;
#endif

	t = (int)snb->draw_shader_handle[RDRSHDM_VISUAL] - (int)sna->draw_shader_handle[RDRSHDM_VISUAL];
	if (t)
		return t;

	t = (intptr_t)snb->material - (intptr_t)sna->material;
	if (t)
		return t;

	t = (int)snb->geo_handle_primary - (int)sna->geo_handle_primary;
	if (t)
		return t;

	return (intptr_t)snb->drawable - (intptr_t)sna->drawable;
}

static int rdrOpaqueCmpShadowZBucket(const RdrSortNode **sna_p, const RdrSortNode **snb_p)
{
	const RdrSortNode *sna = *sna_p;
	const RdrSortNode *snb = *snb_p;
	int t;

	// CD: this sorts front to back in camera space, not shadowmap space... is that really desired?

	t = sna->zbucket - snb->zbucket; // sort front to back
	if (t)
		return t;

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->vtxbuf) - PTR_TO_UINT(sna->vtxbuf);
	if (t)
		return t;
#endif

	t = (int)snb->draw_type - (int)sna->draw_type;
	if (t)
		return t;

	t = (int)snb->draw_shader_handle[RDRSHDM_SHADOW] - (int)sna->draw_shader_handle[RDRSHDM_SHADOW];
	if (t)
		return t;

	t = (intptr_t)snb->material - (intptr_t)sna->material;
	if (t)
		return t;

	t = (int)snb->geo_handle_primary - (int)sna->geo_handle_primary;
	if (t)
		return t;

	return (intptr_t)snb->drawable - (intptr_t)sna->drawable;
}

static int rdrOpaqueCmpPrepassZBucket(const RdrSortNode **sna_p, const RdrSortNode **snb_p)
{
	const RdrSortNode *sna = *sna_p;
	const RdrSortNode *snb = *snb_p;
	int t;

	t = sna->zbucket - snb->zbucket; // sort front to back
	if (t)
		return t;

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->vtxbuf) - PTR_TO_UINT(sna->vtxbuf);
	if (t)
		return t;
#endif

	t = (int)snb->draw_type - (int)sna->draw_type;
	if (t)
		return t;

	t = (int)snb->draw_shader_handle[RDRSHDM_ZPREPASS] - (int)sna->draw_shader_handle[RDRSHDM_ZPREPASS];
	if (t)
		return t;

	t = (intptr_t)snb->material - (intptr_t)sna->material;
	if (t)
		return t;

	t = (int)snb->geo_handle_primary - (int)sna->geo_handle_primary;
	if (t)
		return t;

	return (intptr_t)snb->drawable - (intptr_t)sna->drawable;
}

static int rdrAlphaCmp(const RdrAlphaSortNode **sna_p, const RdrAlphaSortNode **snb_p)
{
	const RdrAlphaSortNode *sna = *sna_p;
	const RdrAlphaSortNode *snb = *snb_p;
	int t;
	F32 f;

	f = snb->zdist - sna->zdist; // sort back to front
	if (f)
		return SIGN(f);

#if SUPPORT_VERTEX_BUFFER_COMBINE
	t = PTR_TO_UINT(snb->base_sort_node.vtxbuf) - PTR_TO_UINT(sna->base_sort_node.vtxbuf);
	if (t)
		return t;
#endif

	t = (int)snb->alpha - (int)sna->alpha;
	if (t)
		return t;

	t = (int)snb->base_sort_node.draw_shader_handle[RDRSHDM_VISUAL] - (int)sna->base_sort_node.draw_shader_handle[RDRSHDM_VISUAL];
	if (t)
		return t;

	t = (intptr_t)snb->base_sort_node.material - (intptr_t)sna->base_sort_node.material;
	if (t)
		return t;

	t = (int)snb->base_sort_node.geo_handle_primary - (int)sna->base_sort_node.geo_handle_primary;
	if (t)
		return t;

	return (intptr_t)snb->base_sort_node.drawable - (intptr_t)sna->base_sort_node.drawable;
}

// TODO(CD) removeme once the crash is fixed
static void validateInstanceLinkList(RdrInstanceLinkList *instance_link_list)
{
	int count = SAFE_MEMBER(instance_link_list, count);

	while (instance_link_list)
	{
		assert(count == instance_link_list->count);
		instance_link_list = instance_link_list->next;
		count--;
	}

	assert(!count);
}

void rxbxSortDrawObjectsDirect(RdrDeviceDX *device, RdrDrawListSortCmd *cmd, WTCmdPacket *packet)
{
	RdrDrawListData *draw_data = cmd->draw_data;
	RdrDrawListPassData *pass_data = &cmd->draw_data->pass_data[cmd->pass_num];
	int i;

	assert(draw_data->device);
	assert(pass_data->owned_by_thread);

	etlAddEvent(device->device_base.event_timer, "Sort draw list", ELT_CODE, ELTT_BEGIN);

	if (pass_data->shader_mode == RDRSHDM_VISUAL || pass_data->shader_mode == RDRSHDM_VISUAL_HDR)
	{
		for (i = 0; i < RSBT_COUNT; ++i)
		{
#if SUPPORT_VERTEX_BUFFER_COMBINE
			int s, sortNodeMax;
			for (s = 0, sortNodeMax = eaSize(&pass_data->sort_node_buckets[i].sort_nodes); s < sortNodeMax; ++s)
			{
				RdrSortNode * sort_node = pass_data->sort_node_buckets[i].sort_nodes[s];
				if (sort_node->drawable->draw_type == RTYPE_MODEL)
				{
					RdrDrawable *draw = sort_node->drawable;
					RdrGeometryDataDX *geo_data_primary;
					if (sort_node->geo_handle_primary != -1)
					{
						geo_data_primary = rxbxGetGeoDataDirect(device, sort_node->geo_handle_primary);
						sort_node->vtxbuf = geo_data_primary->vertex_buffer_coalesced ? 
							geo_data_primary->vertex_buffer_coalesced : geo_data_primary->vertex_buffer;
					}
				}
			}
#endif

			if (i == RSBT_ALPHA || i == RSBT_ALPHA_NEED_GRAB_PRE_DOF || i == RSBT_ALPHA_NEED_GRAB || i == RSBT_ALPHA_PRE_DOF || i == RSBT_ALPHA_LATE || i == RSBT_ALPHA_NEED_GRAB_LATE || i == RSBT_ALPHA_LOW_RES_ALPHA || i == RSBT_ALPHA_LOW_RES_ADDITIVE || i == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
			{
				int j, k;
				eaQSortG(pass_data->sort_node_buckets[i].sort_nodes, rdrAlphaCmp);

				// instance contiguous alpha nodes with the same parameters
				for (j = 0; j < eaSize(&pass_data->sort_node_buckets[i].sort_nodes); ++j)
				{
					RdrSortNode *sort_node1 = pass_data->sort_node_buckets[i].sort_nodes[j];

					if (sort_node1->do_instancing && sort_node1->instances)
					{
						RdrInstanceLinkList *instance_link_end = sort_node1->instances;
						int instance_count = instance_link_end->count;

						sort_node1->disable_instance_sorting = 1;

						validateInstanceLinkList(sort_node1->instances);

						for (k = j+1; k < eaSize(&pass_data->sort_node_buckets[i].sort_nodes); ++k)
						{
							RdrSortNode *sort_node2 = pass_data->sort_node_buckets[i].sort_nodes[k];

							if (!sort_node2->instances || !sort_node2->do_instancing || 
								sort_node1->drawable != sort_node2->drawable || 
								sort_node1->material != sort_node2->material || 
								!rdrSortNodesCanInstance(sort_node1, sort_node2))
							{
								break;
							}

							instance_link_end->next = sort_node2->instances;
							instance_link_end = instance_link_end->next;
							sort_node2->instances = NULL; // just for debugging purposes; sort_node2 should never get traversed again
							++instance_count;
						}

						if (instance_count > 1)
						{
							int counter = instance_count;
							for (instance_link_end = sort_node1->instances; instance_link_end; instance_link_end = instance_link_end->next)
								instance_link_end->count = counter--;

							assert(counter == 0);
							validateInstanceLinkList(sort_node1->instances);

							eaRemoveRange(&pass_data->sort_node_buckets[i].sort_nodes, j+1, instance_count-1);
							draw_data->stats.alpha_instanced_objects += instance_count-1;
							sort_node1->tri_count *= instance_count;
						}
					}
				}
			}
			else
			{
				if (rdr_state.sortOpaqueAppliesToVisual && draw_data->sort_opaque)
					eaQSortG(pass_data->sort_node_buckets[i].sort_nodes, rdr_state.sortOpaqueAppliesToVisual == 2 ? rdrOpaqueCmpVisualStateZBucket : rdrOpaqueCmpVisualZBucket);
				else
					eaQSortG(pass_data->sort_node_buckets[i].sort_nodes, rdrOpaqueCmpVisual);
			}
		}
	}
	else if (pass_data->shader_mode == RDRSHDM_ZPREPASS)
	{
		eaQSortG(pass_data->sort_node_buckets[RSBT_ZPREPASS].sort_nodes, draw_data->sort_opaque ? rdrOpaqueCmpPrepassZBucket : rdrOpaqueCmpPrepass);
		eaQSortG(pass_data->sort_node_buckets[RSBT_ZPREPASS_NO_OUTLINE].sort_nodes, draw_data->sort_opaque ? rdrOpaqueCmpPrepassZBucket : rdrOpaqueCmpPrepass);
	}
	else
	{
		eaQSortG(pass_data->sort_node_buckets[RSBT_SHADOWMAP].sort_nodes, rdr_state.sortShadowmapsByState ? rdrOpaqueCmpShadow : rdrOpaqueCmpShadowZBucket);
	}

	etlAddEvent(device->device_base.event_timer, "Sort draw list", ELT_CODE, ELTT_END);
}

void rxbxDrawSortNodes(RdrDeviceDX *device, RdrDrawListDrawCmd *cmd, WTCmdPacket *packet)
{
	RdrSortBucketType sort_bucket_type = cmd->sort_bucket_type;
	RdrDrawListPassData *pass_data = &cmd->draw_data->pass_data[cmd->pass_num];
	int i, count;
	RdrSortNodeList *nodelist;
	RdrDrawListPassStats *pass_stats = &cmd->draw_data->stats.pass_stats[pass_data->shader_mode];
	
	int merged_drawable_start = -1;
	RdrGeometryType merged_drawable_type = RTYPE_END;

	assert(pass_data->depth_only || rdrIsVisualShaderMode(pass_data->shader_mode));
	assert(pass_data->owned_by_thread);

	nodelist = &pass_data->sort_node_buckets[sort_bucket_type];
	count = eaSize(&nodelist->sort_nodes);

	if (sort_bucket_type >= RSBT_ALPHA_PRE_DOF)
	{
		pass_stats->alpha_objects_drawn += count;
		pass_stats->alpha_triangles_drawn += nodelist->total_tri_count;
	}
	else
	{
		pass_stats->opaque_objects_drawn += count;
		pass_stats->opaque_triangles_drawn += nodelist->total_tri_count;
	}

	for (i = 0; i < count; ++i)
	{
		RdrSortNode *sort_node = nodelist->sort_nodes[i];
		RdrDrawable *draw = sort_node->drawable;

		pass_stats->objects_drawn[sort_node->category]++;
		pass_stats->triangles_drawn[sort_node->category] += sort_node->tri_count;

		if (draw->draw_type == merged_drawable_type)
		{
			// nothing to do -- still merging
		}
		else
		{
			if (merged_drawable_start >= 0)
			{
				switch (merged_drawable_type)
				{
					xcase RTYPE_PARTICLE:
					{
						PERFINFO_AUTO_START("rxbxDrawParticlesDirect", 1);
						rxbxDrawParticlesDirect(device, nodelist->sort_nodes + merged_drawable_start, i - merged_drawable_start, sort_bucket_type, pass_data, cmd->is_low_res_edge_pass, cmd->manual_depth_test);
						PERFINFO_AUTO_STOP();
					}

					xcase RTYPE_PRIMITIVE:
					{
						PERFINFO_AUTO_START("rxbxDrawPrimitivesDirect", 1);
						rxbxDrawPrimitivesDirect(device, nodelist->sort_nodes + merged_drawable_start, i - merged_drawable_start, sort_bucket_type, pass_data);
						PERFINFO_AUTO_STOP();
					}

					xdefault:
					{
						assertmsg(0, "Unknown render type!");
					}
				}

				merged_drawable_start = -1;
				merged_drawable_type = RTYPE_END;
			}

			if (draw->draw_type != RTYPE_SKINNED_MODEL && draw->draw_type != RTYPE_CURVED_MODEL &&
				draw->draw_type != RTYPE_MODEL)
				rxbxUnbindTessellationShaders(device);

			switch (draw->draw_type)
			{
			// mergable types
			xcase RTYPE_PARTICLE:
			case RTYPE_PRIMITIVE:
				merged_drawable_start = i;
				merged_drawable_type = draw->draw_type;

			// unmergable types
			xcase RTYPE_MODEL:
				PERFINFO_AUTO_START("rxbxDrawModelDirect", 1);
				validateInstanceLinkList(sort_node->instances);
				rxbxDrawModelDirect(device, (RdrDrawableGeo *)draw, sort_node, sort_bucket_type, pass_data, cmd->manual_depth_test);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_SKINNED_MODEL:
			case RTYPE_CURVED_MODEL:
				PERFINFO_AUTO_START("rxbxDrawSkinnedModelDirect", 1);
				rxbxDrawSkinnedModelDirect(device, (RdrDrawableSkinnedModel *)draw, sort_node, sort_bucket_type, pass_data, cmd->manual_depth_test);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_TERRAIN:
				PERFINFO_AUTO_START("rxbxDrawHeightMapDirect", 1);
				rxbxDrawHeightMapDirect(device, (RdrDrawableGeo *)draw, sort_node, sort_bucket_type, pass_data);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_FASTPARTICLES:
				PERFINFO_AUTO_START("rxbxDrawFastParticlesDirect", 1);
				rxbxDrawFastParticlesDirect(device, (RdrDrawableFastParticles *)draw, sort_node, sort_bucket_type, cmd->is_low_res_edge_pass, cmd->manual_depth_test, pass_data->is_underexposed_pass);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_TRISTRIP:
				PERFINFO_AUTO_START("rxbxDrawTriStripDirect", 1);
				rxbxDrawTriStripDirect(device, (RdrDrawableTriStrip *)draw, sort_node, sort_bucket_type, pass_data);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_CLOTHMESH:
				PERFINFO_AUTO_START("rxbxDrawClothMeshDirect", 1);
				rxbxDrawClothMeshDirect(device, (RdrDrawableClothMesh*)draw, sort_node, sort_bucket_type, pass_data);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_PRIMITIVE_MESH:
				PERFINFO_AUTO_START("rxbxDrawMeshPrimitiveDirect", 1);
				rxbxDrawMeshPrimitiveDirect(device, (RdrDrawableMeshPrimitive*)draw, sort_node, sort_bucket_type, pass_data);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_CYLINDERTRAIL:
				PERFINFO_AUTO_START("rxbxDrawCylinderTrailDirect", 1);
				rxbxDrawCylinderTrailDirect(device, (RdrDrawableCylinderTrail *)draw, sort_node, sort_bucket_type, pass_data);
				PERFINFO_AUTO_STOP();

			xcase RTYPE_STARFIELD:
				PERFINFO_AUTO_START("rxbxDrawStarFieldDirect", 1);
				rxbxDrawStarFieldDirect(device, (RdrDrawableGeo *)draw, sort_node, sort_bucket_type, pass_data);
				PERFINFO_AUTO_STOP();

			xdefault:
				assertmsg(0, "Unknown render type!");
			}
		}
	}

	// after geo is drawn to screen, ensure that the hull shader is no longer attached.
	rxbxUnbindTessellationShaders(device);
	
	if (merged_drawable_start >= 0)
	{
		switch (merged_drawable_type)
		{
			xcase RTYPE_PARTICLE:
			{
				PERFINFO_AUTO_START("rxbxDrawParticlesDirect", 1);
				rxbxDrawParticlesDirect(device, nodelist->sort_nodes + merged_drawable_start, i - merged_drawable_start, sort_bucket_type, pass_data, cmd->is_low_res_edge_pass, cmd->manual_depth_test);
				PERFINFO_AUTO_STOP();
			}

			xcase RTYPE_PRIMITIVE:
			{
				PERFINFO_AUTO_START("rxbxDrawPrimitivesDirect", 1);
				rxbxDrawPrimitivesDirect(device, nodelist->sort_nodes + merged_drawable_start, i - merged_drawable_start, sort_bucket_type, pass_data);
				PERFINFO_AUTO_STOP();
			}

			xdefault:
			{
				assertmsg(0, "Unknown render type!");
			}
		}

		merged_drawable_start = -1;
		merged_drawable_type = RTYPE_END;
	}
}

void rxbxSetDebugBuffer(RdrDeviceDX *device, Vec4 *parm, WTCmdPacket *packet)
{
	rxbxApplyDebugBuffer(device,parm);
}

void rxbxDrawObjectsDirect(RdrDeviceDX *device, RdrDrawListDrawCmd *cmd, WTCmdPacket *packet)
{
	int count;
	RdrSortBucketType sort_bucket_type = cmd->sort_bucket_type;
	RdrDrawListPassData *pass_data = &cmd->draw_data->pass_data[cmd->pass_num];
	RdrSortNodeList *nodelist;
	bool twoPassDepthAlpha;

	nodelist = &pass_data->sort_node_buckets[sort_bucket_type];
	if (eaSize(&nodelist->sort_nodes)==0)
		return;

	assert(cmd->draw_data->device);
	assert(pass_data->depth_only || rdrIsVisualShaderMode(pass_data->shader_mode));
	assert(pass_data->owned_by_thread);

	etlAddEvent(device->device_base.event_timer, "Draw objects", ELT_CODE, ELTT_BEGIN);

	twoPassDepthAlpha = !rdr_state.disable_two_pass_depth_alpha && (sort_bucket_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || sort_bucket_type == RSBT_ALPHA_NEED_GRAB || sort_bucket_type == RSBT_ALPHA_NEED_GRAB_LATE) && (pass_data->shader_mode == RDRSHDM_VISUAL || pass_data->shader_mode == RDRSHDM_VISUAL_HDR);

	if (pass_data->disable_opaque_depth_writes && (sort_bucket_type == RSBT_OPAQUE_PRE_GRAB || sort_bucket_type == RSBT_OPAQUE_POST_GRAB_IN_ZPRE))
	{
		rxbxDepthWrite(device, FALSE);
		rxbxDepthTest(device, DEPTHTEST_EQUAL);
	}
	else if (twoPassDepthAlpha)
	{
		// Just color the first time (uses depth information)
		rxbxDepthWritePush(device, FALSE);
		rxbxColorWritePush(device, TRUE);
	}
	else if (sort_bucket_type == RSBT_DECAL_PRE_GRAB || sort_bucket_type == RSBT_DECAL_POST_GRAB || sort_bucket_type == RSBT_ALPHA_LOW_RES_ALPHA || sort_bucket_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_bucket_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
	{
		rxbxDepthWrite(device, FALSE);
	}
	else
	{
		rxbxDepthWrite(device, TRUE);
	}
#if _XBOX
	// Removes PIX warnings, does not seem to stall, not sure if it actually helps anything either...
	D3DDevice_FlushHiZStencil(device->d3d_device, D3DFHZS_ASYNCHRONOUS); // D3DFHZS_SYNCHRONOUS
#endif

	rxbxDepthBiasPush(device, pass_data->depth_bias, pass_data->slope_scale_depth_bias);

	if (sort_bucket_type != rdr_state.skipSortBucket)
	{
		// setup blend mode
		if (sort_bucket_type == RSBT_ALPHA || sort_bucket_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || sort_bucket_type == RSBT_ALPHA_NEED_GRAB ||
			sort_bucket_type == RSBT_ALPHA_PRE_DOF || sort_bucket_type == RSBT_DECAL_PRE_GRAB ||
			sort_bucket_type == RSBT_DECAL_POST_GRAB || sort_bucket_type == RSBT_ALPHA_LATE ||
			sort_bucket_type == RSBT_ALPHA_NEED_GRAB_LATE)
			rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		else if (sort_bucket_type == RSBT_ALPHA_LOW_RES_ALPHA)
			rxbxBlendFuncSeparate(device, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD,
								  D3DBLEND_ZERO, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
		else if (sort_bucket_type == RSBT_ALPHA_LOW_RES_ADDITIVE || sort_bucket_type == RSBT_ALPHA_LOW_RES_SUBTRACTIVE)
			rxbxBlendFuncSeparate(device, D3DBLEND_SRCALPHA, D3DBLEND_ONE, D3DBLENDOP_ADD,
								  D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD);
		else
			rxbxBlendFunc(device, false, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD);

		count = eaSize(&nodelist->sort_nodes);
		if ((sort_bucket_type == RSBT_ALPHA_NEED_GRAB_PRE_DOF || sort_bucket_type == RSBT_ALPHA_NEED_GRAB || sort_bucket_type == RSBT_ALPHA_NEED_GRAB_LATE) && !rdr_state.disable_two_pass_depth_alpha)
			count *= 2;

		PERFINFO_AUTO_START("Draw objects",count);
		rxbxDrawSortNodes(device, cmd, packet);
		
		if (twoPassDepthAlpha)
		{
			RdrShaderMode orig_shader_mode = pass_data->shader_mode;
			bool orig_depth_only = pass_data->depth_only;

			// draw a second pass of the alpha objects that use the depth as input, (e.g. water)
			// to avoid the conflict between reading/writing the depth buffer, but
			// still prevent alpha popping through refractive/water surfaces
			// Note: disabled by default on Xbox, the Xbox doesn't read from and write to the same depth buffer

			rxbxDepthWrite(device, TRUE);
			rxbxColorWrite(device, FALSE);

			// temporarily override the shader mode
			pass_data->shader_mode = RDRSHDM_ZPREPASS;
			pass_data->depth_only = 1;

			rxbxDrawSortNodes(device, cmd, packet);

			// restore original shader mode
			pass_data->shader_mode = orig_shader_mode;
			pass_data->depth_only = orig_depth_only;
		}

		PERFINFO_AUTO_STOP();
	}

	rxbxDepthBiasPop(device);
	rxbxDepthTest(device, DEPTHTEST_LEQUAL);
	if (twoPassDepthAlpha)
	{
		rxbxDepthWritePop(device);
		rxbxColorWritePop(device);
	}

	etlAddEvent(device->device_base.event_timer, "Draw objects", ELT_CODE, ELTT_END);

	assert(cmd->draw_data->device);
}
