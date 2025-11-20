
#include "rt_xprimitive.h"
#include "rt_xdrawmode.h"
#include "rt_xsprite.h"
#include "rt_xdevice.h"
#include "rt_xgeo.h"
#include "ScratchStack.h"
#include "../RdrDrawListPrivate.h"

static const VertexComponentInfo velements_primitive[] = 
{
	{ offsetof(RdrPrimitiveVertex,pos), VPOS },
	{ offsetof(RdrPrimitiveVertex,color), VCOLOR },
	{ 0, VTERMINATE }
};
static const VertexComponentInfo velements_primitive_textured[] = 
{
	{ offsetof(RdrPrimitiveTexturedVertex,pos), VPOS },
	{ offsetof(RdrPrimitiveTexturedVertex,color), VCOLOR },
	{ offsetof(RdrPrimitiveTexturedVertex,texcoord), VTEXCOORD32 },
	{ 0, VTERMINATE }
};

static const VertexComponentInfo velements_quad[] = 
{
	{ offsetof(RdrQuadVertex,point), VPOSSPRITE },
	{ offsetof(RdrQuadVertex,color), VCOLOR },
	{ offsetof(RdrQuadVertex,texcoords), V2TEXCOORD32 },
	{ 0, VTERMINATE }
};

void rxbxInitPrimitiveVertexDecls(RdrDeviceDX *device)
{
	rxbxSetupSpriteVdecl(device, velements_primitive_textured, &device->primitive_vertex_textured_declaration);
	rxbxSetupSpriteVdecl(device, velements_quad, &device->quad_vertex_declaration);
	rxbxSetupPrimitiveVdecl(device, velements_primitive);
}

/// Setup all the primitive draw state that does not actually depend
/// on a particular primitive.
void rxbxSetupPrimitiveDrawState(RdrDeviceDX* device, bool justVertexDecls)
{
	if (!justVertexDecls)
	{
		rxbxSetCullMode(device, CULLMODE_NONE);
		rxbxFogPush(device, 0);
	}
}

/// Cleanup all the primitive draw state that does not actually depend
/// on a particular primitive.
static void rxbxCleanupPrimitiveDrawState( RdrDeviceDX* device )
{
	rxbxSetCullMode(device, CULLMODE_BACK);
	rxbxFogPop(device);
}

/// Are the two primitives, per-primitive draw state incompatible?
static bool rxbxIsPerPrimitiveDrawStateSame( RdrDeviceDX* device, RdrDrawablePrimitive* prim1, RdrDrawablePrimitive* prim2 )
{
	if ( prim1->tex_handle != prim2->tex_handle ) {
		return false;
	}
	
	if ( !prim2->internal_no_change_pixel_shader ) {
		if( prim1->tonemapped != prim2->tonemapped ) {
			return false;
		}
	}

	if ( (prim1->in_3d || !prim1->screen_width_2d || !prim1->screen_height_2d)
		  != (prim2->in_3d || !prim2->screen_width_2d || !prim2->screen_height_2d) ) {
		return false;
	}

	if ( prim1->no_ztest != prim2->no_ztest ) {
		return false;
	}

	if ( (!prim1->filled && prim1->type != RP_LINE)
		  != (!prim2->filled && prim2->type != RP_LINE) ) {
		return false;
	}

	return true;
}

/// Setup the primitive draw state that varies per primitive.
static void rxbxSetupPerPrimitiveDrawState(RdrDeviceDX *device, RdrDrawablePrimitive *primitive)
{
	if (primitive->tex_handle && primitive->type == RP_QUAD)
	{
		rxbxSetupSpriteDrawMode(device, device->primitive_vertex_textured_declaration);
		rxbxBindTexture(device, 0, primitive->tex_handle);
	}
	else
	{
		rxbxSetupPrimitiveDrawMode(device);
		rxbxBindWhiteTexture(device, 0);
	}
	
	if (!primitive->internal_no_change_pixel_shader) {
		if (primitive->tonemapped)
			rxbxSetParticleBlendMode(device);
		else
			rxbxSetPrimitiveBlendMode(device);
	}

	if (primitive->sprite_state)
	{
		rxbxSetSpriteEffectBlendMode(device, RdrSpriteEffect_None);
		rxbxSpriteFlushCurrentEffect();
		rxbxSpriteEffectSetup(device, primitive->sprite_state);
		rxbxSpriteBindBlendSetup(device, primitive->sprite_state, false);
	}

	if (primitive->in_3d || !primitive->screen_width_2d || !primitive->screen_height_2d)
		rxbxSet3DMode(device, 0);
	else
		rxbxSet2DMode(device, primitive->screen_width_2d, primitive->screen_height_2d);

	if (primitive->no_ztest)
	{
		rxbxDepthTest(device, DEPTHTEST_OFF);
		rxbxDepthWritePush(device, FALSE);
	}

	if( !primitive->filled && primitive->type != RP_LINE )
		rxbxSetFillMode(device, D3DFILL_WIREFRAME);
}

/// Cleanup the primitive draw state that varies per primitive.
static void rxbxCleanupPerPrimitiveDrawState(RdrDeviceDX *device, RdrDrawablePrimitive *primitive)
{
	if (!primitive->filled && primitive->type != RP_LINE)
		rxbxSetFillMode(device, D3DFILL_SOLID);

	if (primitive->no_ztest)
	{
		rxbxDepthTest(device, DEPTHTEST_LEQUAL);
		rxbxDepthWritePop(device);
	}
}

void rxbxDrawVerticesUP(RdrDeviceDX *device, DWORD primitive_topo, int vertex_count, const void * vertices, int stride)
{
	rxbxSetupVBODrawVerticesUP(device, vertex_count, vertices, stride, (D3D11_PRIMITIVE_TOPOLOGY)primitive_topo);
	
	rxbxApplyStatePreDraw(device);
	if (device->d3d11_device)
	{
		ID3D11DeviceContext_IASetPrimitiveTopology(device->d3d11_imm_context, primitive_topo);
		ID3D11DeviceContext_Draw(device->d3d11_imm_context, vertex_count, 0);
	}
	else
	{
		CHECKX(IDirect3DDevice9_DrawVertices(device->d3d_device, primitive_topo, 0, vertex_count));
	}
}

static void drawMergedPrimitives( RdrDeviceDX *device, RdrSortNode **nodes, int node_count )
{
#define PRIM( x ) ((RdrDrawablePrimitive*)nodes[ x ]->drawable)

	rxbxApplyStatePreDraw(device);
	VALIDATE_DEVICE_DEBUG();
	switch (PRIM( 0 )->type)
	{
		xcase RP_LINE:
		{
			RdrPrimitiveVertex* verts = ScratchAlloc( sizeof( RdrPrimitiveVertex ) * node_count * 2 );
			{
				int primIt;
				for( primIt = 0; primIt != node_count; ++primIt ) {
					verts[ primIt * 2 + 0 ] = PRIM( primIt )->vertices[ 0 ].vertex;
					verts[ primIt * 2 + 1 ] = PRIM( primIt )->vertices[ 1 ].vertex;
				}
			}
			rxbxCountQueryDrawCall(device);
			rxbxDrawVerticesUP(device, D3DPT_LINELIST, node_count * 2, verts, sizeof( *verts ));
			INC_DRAW_CALLS( node_count );
			ScratchFree( verts );
		}

		xcase RP_TRI:
		{
			RdrPrimitiveVertex* verts = ScratchAlloc( sizeof( RdrPrimitiveVertex ) * node_count * 3 );
			{
				int primIt;
				for( primIt = 0; primIt != node_count; ++primIt ) {
					verts[ primIt * 3 + 0 ] = PRIM( primIt )->vertices[ 0 ].vertex;
					verts[ primIt * 3 + 1 ] = PRIM( primIt )->vertices[ 1 ].vertex;
					verts[ primIt * 3 + 2 ] = PRIM( primIt )->vertices[ 2 ].vertex;
				}
			}
			rxbxCountQueryDrawCall(device);
			rxbxDrawVerticesUP(device, D3DPT_TRIANGLELIST, node_count * 3, verts, sizeof( *verts ));
			INC_DRAW_CALLS( node_count );
			ScratchFree( verts );
		}

		xcase RP_QUAD:
		{
			if (PRIM( 0 )->tex_handle)
			{
				RdrPrimitiveTexturedVertex* verts = ScratchAlloc( sizeof( RdrPrimitiveTexturedVertex ) * node_count * 6 );
				{
					int primIt;
					for( primIt = 0; primIt != node_count; ++primIt ) {
						if (PRIM(primIt)->has_tex_coords)
						{
							verts[ primIt * 6 + 0 ] = PRIM( primIt )->vertices[0];
							verts[ primIt * 6 + 1 ] = PRIM( primIt )->vertices[1];
							verts[ primIt * 6 + 2 ] = PRIM( primIt )->vertices[2];
							verts[ primIt * 6 + 3 ] = PRIM( primIt )->vertices[0];
							verts[ primIt * 6 + 4 ] = PRIM( primIt )->vertices[2];
							verts[ primIt * 6 + 5 ] = PRIM( primIt )->vertices[3];
						} else {
							copyVec3( PRIM( primIt )->vertices[ 0 ].pos, verts[ primIt * 6 + 0 ].pos );
							copyVec4( PRIM( primIt )->vertices[ 0 ].color, verts[ primIt * 6 + 0 ].color );
							copyVec2( PRIM( primIt )->vertices[ 0 ].texcoord, verts[ primIt * 6 + 0 ].texcoord );
							
							copyVec3( PRIM( primIt )->vertices[ 1 ].pos, verts[ primIt * 6 + 1 ].pos );
							copyVec4( PRIM( primIt )->vertices[ 1 ].color, verts[ primIt * 6 + 1 ].color );
							copyVec2( PRIM( primIt )->vertices[ 1 ].texcoord, verts[ primIt * 6 + 1 ].texcoord );
							
							copyVec3( PRIM( primIt )->vertices[ 2 ].pos, verts[ primIt * 6 + 2 ].pos );
							copyVec4( PRIM( primIt )->vertices[ 2 ].color, verts[ primIt * 6 + 2 ].color );
							copyVec2( PRIM( primIt )->vertices[ 2 ].texcoord, verts[ primIt * 6 + 2 ].texcoord );
							
							copyVec3( PRIM( primIt )->vertices[ 0 ].pos, verts[ primIt * 6 + 3 ].pos );
							copyVec4( PRIM( primIt )->vertices[ 0 ].color, verts[ primIt * 6 + 3 ].color );
							copyVec2( PRIM( primIt )->vertices[ 0 ].texcoord, verts[ primIt * 6 + 3 ].texcoord );
							
							copyVec3( PRIM( primIt )->vertices[ 2 ].pos, verts[ primIt * 6 + 4 ].pos );
							copyVec4( PRIM( primIt )->vertices[ 2 ].color, verts[ primIt * 6 + 4 ].color );
							copyVec2( PRIM( primIt )->vertices[ 2 ].texcoord, verts[ primIt * 6 + 4 ].texcoord );
							
							copyVec3( PRIM( primIt )->vertices[ 3 ].pos, verts[ primIt * 6 + 5 ].pos );
							copyVec4( PRIM( primIt )->vertices[ 3 ].color, verts[ primIt * 6 + 5 ].color );
							copyVec2( PRIM( primIt )->vertices[ 3 ].texcoord, verts[ primIt * 6 + 5 ].texcoord );
						}
					}
				}
				rxbxCountQueryDrawCall(device);
				rxbxDrawVerticesUP(device, D3DPT_TRIANGLELIST, node_count * 6, verts, sizeof( *verts ));
				INC_DRAW_CALLS( node_count * 2 );
				ScratchFree( verts );
			}
			else
			{
				RdrPrimitiveVertex* verts = ScratchAlloc( sizeof( RdrPrimitiveVertex ) * node_count * 6 );
				{
					int primIt;
					for( primIt = 0; primIt != node_count; ++primIt ) {
						verts[ primIt * 6 + 0 ] = PRIM( primIt )->vertices[ 0 ].vertex;
						verts[ primIt * 6 + 1 ] = PRIM( primIt )->vertices[ 1 ].vertex;
						verts[ primIt * 6 + 2 ] = PRIM( primIt )->vertices[ 2 ].vertex;
						verts[ primIt * 6 + 3 ] = PRIM( primIt )->vertices[ 0 ].vertex;
						verts[ primIt * 6 + 4 ] = PRIM( primIt )->vertices[ 2 ].vertex;
						verts[ primIt * 6 + 5 ] = PRIM( primIt )->vertices[ 3 ].vertex;
					}
				}
				rxbxCountQueryDrawCall(device);
				rxbxDrawVerticesUP(device, D3DPT_TRIANGLELIST, node_count * 6, verts, sizeof( *verts ));
				INC_DRAW_CALLS( node_count * 2 );
				ScratchFree( verts );
			}
		}
	}
	// flush stream source since DrawXXXXXUP leaves them indeterminate
	rxbxResetStreamSource(device);

#undef PRIM
}

void rxbxDrawPrimitivesDirect(RdrDeviceDX *device, RdrSortNode **nodes, int node_count, 
							  RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	if (rdr_state.dbg_type_flags & RDRTYPE_PRIMITIVE)
		return;

	PERFINFO_AUTO_START_FUNC();

	rxbxSetupPrimitiveDrawState( device, false );

	{
		int mergedStartIt = 0;
		int it;
		RdrSortNode **primitive_nodes = node_count ? ScratchAlloc(node_count * sizeof(RdrSortNode*)) : NULL;

		// Previously, this system was primarily used for debugging, but now it must function in a similar fashion to the sprite system when in 3D mode.
		// Which means the list order needs to be flipped in order to ensure that the layering is correct.
		for ( it = 0; it < node_count; it++)
		{
			primitive_nodes[it] = nodes[node_count - it - 1];
		}

		for( it = 0; it < node_count; ++it ) {
			RdrDrawablePrimitive* mergedDrawStart = (RdrDrawablePrimitive *)primitive_nodes[ mergedStartIt ]->drawable;
			RdrDrawablePrimitive* draw = (RdrDrawablePrimitive *)primitive_nodes[ it ]->drawable;
			
			if ( rxbxIsPerPrimitiveDrawStateSame( device, mergedDrawStart, draw )
					&& mergedDrawStart->type == draw->type ) {
				// can merge them -- don't change
			}
			else {
				rxbxSetupPerPrimitiveDrawState( device, mergedDrawStart );
				drawMergedPrimitives( device, &primitive_nodes[ mergedStartIt ], it - mergedStartIt );
				rxbxCleanupPerPrimitiveDrawState( device, mergedDrawStart );

				mergedStartIt = it;
			}
		}

		if ( node_count ) {
			RdrDrawablePrimitive* mergedDrawStart = (RdrDrawablePrimitive *)primitive_nodes[ mergedStartIt ]->drawable;

			rxbxSetupPerPrimitiveDrawState( device, mergedDrawStart );
			drawMergedPrimitives( device, &primitive_nodes[ mergedStartIt ], node_count - mergedStartIt );
			rxbxCleanupPerPrimitiveDrawState( device, mergedDrawStart );
		}
		ScratchFree(primitive_nodes);
	}

	rxbxCleanupPrimitiveDrawState( device );

	PERFINFO_AUTO_STOP();
}

void rxbxDrawMeshPrimitiveDirect(RdrDeviceDX *device, RdrDrawableMeshPrimitive *mesh, RdrSortNode *sort_node, RdrSortBucketType sort_bucket_type, RdrDrawListPassData *pass_data)
{
	RdrInstanceLinkList *instance_link;
	U32 i;

	PERFINFO_AUTO_START_FUNC();

	if (!(rdr_state.dbg_type_flags & RDRTYPE_PRIMITIVE))
	{
		rxbxSetupPrimitiveDrawState( device, false );

		rxbxSetupPrimitiveDrawMode(device);
		rxbxBindWhiteTexture(device, 0);
		rxbxSet3DMode(device, 0);

		for (instance_link = sort_node->instances; instance_link; instance_link = instance_link->next)
		{
			RdrInstance *instance = instance_link->instance;

			if (mesh->tonemapped)
				rxbxSetParticleBlendMode(device);
			else
				rxbxSetPrimitiveBlendMode(device);

			rxbxPushModelMatrix(device, instance->world_matrix, false, false);

			if (mesh->no_ztest)
			{
				rxbxDepthTest(device, DEPTHTEST_OFF);
				rxbxDepthWritePush(device, FALSE);
			}
			else if (mesh->no_zwrite)
			{
				rxbxDepthWritePush(device, FALSE);
			}

			for (i=0; i<mesh->num_strips; ++i)
			{
				RdrDrawableMeshPrimitiveStrip* strip = &mesh->strips[i];
				switch (strip->type)
				{
					xcase RP_TRILIST:
						rxbxDrawIndexedTrianglesUP(device, strip->num_indices/3, strip->indices, mesh->num_verts, mesh->verts, sizeof(RdrPrimitiveVertex));
					xcase RP_TRISTRIP:
						rxbxDrawIndexedTriangleStripUP(device, strip->num_indices, strip->indices, mesh->num_verts, mesh->verts, sizeof(RdrPrimitiveVertex));
				}
			}

			if (mesh->no_ztest)
			{
				rxbxDepthTest(device, DEPTHTEST_LEQUAL);
				rxbxDepthWritePop(device);
			}
			else if (mesh->no_zwrite)
			{
				rxbxDepthWritePop(device);
			}

			rxbxPopModelMatrix(device);
		}

		rxbxCleanupPrimitiveDrawState( device );
	}

	if (mesh->owns_verts)
	{
		free(mesh->verts);
		mesh->verts = NULL;
		mesh->owns_verts = false;
	}

	PERFINFO_AUTO_STOP();
}

typedef struct RdrQuadVertexNormal
{
	Vec3	point;
	S16		normal_packed[4];
	Vec2	texcoords;
	Vec4	color;
} RdrQuadVertexNormal;

void rxbxDrawQuadDirect(RdrDeviceDX *device, RdrQuadDrawable *quad, WTCmdPacket *packet)
{
	RdrSurfaceStateDX* current_state = rxbxGetCurrentState( device );
	Vec4 oldViewport;
	RdrQuadVertexNormal normal_verts[4];
	
	CHECKTHREAD;
	CHECKDEVICELOCK(device);

	if (rdr_state.dbg_type_flags & RDRTYPE_PRIMITIVE)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (quad->use_normal_vertex_shader)
	{
		int draw_bits;
		RdrGeoUsage usage = {0};
		int i;

		for (i = 0; i < 4; ++i)
		{
			copyVec2(quad->vertices[i].point, normal_verts[i].point);
			setVec4same(normal_verts[i].normal_packed, 0);
			copyVec2(quad->vertices[i].texcoords, normal_verts[i].texcoords);
			copyVec4(quad->vertices[i].color, normal_verts[i].color);
		}

		draw_bits = DRAWBIT_VERTEX_COLORS | DRAWBIT_NO_NORMALMAP;
		usage.bits[0] = usage.key = RUSE_POSITIONS | RUSE_NORMALS | RUSE_TEXCOORDS | RUSE_COLORS | RUSE_TEXCOORDS_HI_FLAG | RUSE_KEY_VERTEX_LIGHTS;
		usage.iNumPrimaryStreams = 1;
		usage.bHasSecondary = false;
		if (!rxbxSetupNormalDrawMode(device, &usage, draw_bits))
			return;
	}
	else
		rxbxSetupSpriteDrawMode(device, device->quad_vertex_declaration);

	// Quads are always drawn without viewports
	copyVec4( current_state->viewport, oldViewport );
	setVec4( current_state->viewport, 0, 1, 0, 1 );
	rxbxResetViewport( device, device->active_surface->width_thread, device->active_surface->height_thread );

	rxbxSetCullMode(device, CULLMODE_NONE);

	if (quad->alpha_blend)
		rxbxBlendFunc(device, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD);
	else
		rxbxBlendFunc(device, false, D3DBLEND_ONE, D3DBLEND_ZERO, D3DBLENDOP_ADD);
	
	rxbxDepthTest(device, DEPTHTEST_OFF);
	rxbxDepthWritePush(device, FALSE);

	rxbxBindMaterial(device, &quad->material, NULL, RLCT_WORLD, quad->shader_handle, getRdrMaterialShaderByKey(0), false, false, false);
	rxbxSet2DMode(device, device->active_surface->width_thread, device->active_surface->height_thread);

	VALIDATE_DEVICE_DEBUG();
	rxbxCountQueryDrawCall(device);
	rxbxDrawVerticesUP(device, D3DPT_TRIANGLESTRIP, 4, quad->use_normal_vertex_shader ? (const void*)normal_verts : (const void*)quad->vertices, 
		quad->use_normal_vertex_shader ? sizeof(normal_verts[0]) : sizeof(quad->vertices[0]));
	INC_DRAW_CALLS(2);

	rxbxSetCullMode(device, CULLMODE_BACK);
	rxbxDepthTest(device, DEPTHTEST_LEQUAL);
	rxbxDepthWritePop(device);

	// Quads are always draw without viewports
	copyVec4( oldViewport, current_state->viewport );
	rxbxResetViewport( device, device->active_surface->width_thread, device->active_surface->height_thread );

	PERFINFO_AUTO_STOP();
}

