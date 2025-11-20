#pragma once
#include "wlModel.h"
#include "earray.h"
#include "windefinclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););

__forceinline static const char *modelGetMSETForErrors(const ModelLOD *model)
{
	return SAFE_MEMBER2(model, model_parent, msetForErrors);
}

extern U32 wl_frame_timestamp;
extern CRITICAL_SECTION model_lod_init_cs;
extern CRITICAL_SECTION model_unpack_cs;
extern volatile int in_model_unpack_cs;
#define UNPACK_INTERNAL(field, size, unpackStmt)							\
	model->unpacked_last_used_timestamp = wl_frame_timestamp;				\
	if (!model->unpack.field && model->data->pack.field.unpacksize) {		\
		EnterCriticalSection(&model_unpack_cs);								\
		in_model_unpack_cs++;												\
		if (!model->unpack.field) {											\
			void *temp = malloc(size);										\
			unpackStmt;														\
			model->unpack.field = temp;										\
			modelLODUpdateMemUsage(model);									\
		}																	\
		model->unpacked_last_used_timestamp = wl_frame_timestamp;			\
		in_model_unpack_cs--;												\
		LeaveCriticalSection(&model_unpack_cs);								\
	}
#define UNPACK_DELTAS(field, stride, count, type)							\
	UNPACK_INTERNAL(field, sizeof(type) * stride * model->data->count,		\
	geoUnpackDeltas(&model->data->pack.field,temp,stride,model->data->count,PACK_##type,SAFE_MEMBER(model->model_parent,name),SAFE_MEMBER(model->model_parent,header)?model->model_parent->header->filename:SAFE_MEMBER(model->model_parent,name), modelGetMSETForErrors(model)))

__forceinline static void modelLockUnpacked(ModelLOD *model)
{
	EnterCriticalSection(&model_unpack_cs);
	model->unpacked_in_use++;
	LeaveCriticalSection(&model_unpack_cs);								\
}

__forceinline static void modelUnlockUnpacked(ModelLOD *model)
{
	EnterCriticalSection(&model_unpack_cs);
	model->unpacked_in_use--;
	LeaveCriticalSection(&model_unpack_cs);								\
}


__forceinline static const U32 *modelGetTris(ModelLOD *model)
{
	UNPACK_DELTAS(tris, 3, tri_count, U32);
	return model->unpack.tris;
}

__forceinline static const Vec3 *modelGetVerts(ModelLOD *model)
{
	UNPACK_DELTAS(verts, 3, vert_count, F32);
	return (const Vec3*)model->unpack.verts;
}

__forceinline static const Vec3 *modelGetNorms(ModelLOD *model)
{
	UNPACK_DELTAS(norms, 3, vert_count, F32);
	return (const Vec3*)model->unpack.norms;
}

__forceinline static const Vec3 *modelGetBinorms(ModelLOD *model)
{
	UNPACK_DELTAS(binorms, 3, vert_count, F32);
	return (const Vec3*)model->unpack.binorms;
}

__forceinline static const Vec3 *modelGetTangents(ModelLOD *model)
{
	UNPACK_DELTAS(tangents, 3, vert_count, F32);
	return (const Vec3*)model->unpack.tangents;
}

__forceinline static const Vec2 *modelGetSts(ModelLOD *model)
{
	UNPACK_DELTAS(sts, 2, vert_count, F32);
	return (const Vec2*)model->unpack.sts;
}

__forceinline static const Vec2 *modelGetSts3(ModelLOD *model)
{
	UNPACK_DELTAS(sts3, 2, vert_count, F32);
	return (const Vec2*)model->unpack.sts3;
}

__forceinline static const U8 *modelGetColors(ModelLOD *model)
{
	UNPACK_DELTAS(colors, 4, vert_count, U8);
	return model->unpack.colors;
}

__forceinline static const U8 *modelGetMatidxs(ModelLOD *model)
{
	UNPACK_DELTAS(matidxs, 4, vert_count, U8);
	return model->unpack.matidxs;
}

__forceinline static const U8 *modelGetWeights(ModelLOD *model)
{
	UNPACK_DELTAS(weights, 4, vert_count, U8);
	return model->unpack.weights;
}

__forceinline static const Vec3 *modelGetVerts2(ModelLOD *model)
{
	UNPACK_DELTAS(verts2, 3, vert_count, F32);
	return (const Vec3*)model->unpack.verts2;
}

__forceinline static const Vec3 *modelGetNorms2(ModelLOD *model)
{
	UNPACK_DELTAS(norms2, 3, vert_count, F32);
	return (const Vec3*)model->unpack.norms2;
}

__forceinline static int modelHasTris(const ModelLOD *model) { return (model->data && model->data->pack.tris.unpacksize) || (model->unpack.tris); }
__forceinline static int modelHasVerts(const ModelLOD *model) { return (model->data && model->data->pack.verts.unpacksize) || (model->unpack.verts); }
__forceinline static int modelHasNorms(const ModelLOD *model) { return (model->data && model->data->pack.norms.unpacksize) || (model->unpack.norms); }
__forceinline static int modelHasBinorms(const ModelLOD *model) { return (model->data && model->data->pack.binorms.unpacksize) || (model->unpack.binorms); }
__forceinline static int modelHasTangents(const ModelLOD *model) { return (model->data && model->data->pack.tangents.unpacksize) || (model->unpack.tangents); }
__forceinline static int modelHasSts(const ModelLOD *model) { return (model->data && model->data->pack.sts.unpacksize) || (model->unpack.sts); }
__forceinline static int modelHasSts3(const ModelLOD *model) { return (model->data && model->data->pack.sts3.unpacksize) || (model->unpack.sts3); }
__forceinline static int modelHasColors(const ModelLOD *model) { return (model->data && model->data->pack.colors.unpacksize) || (model->unpack.colors); }
__forceinline static int modelHasMatidxs(const ModelLOD *model) { return (model->data && model->data->pack.matidxs.unpacksize) || (model->unpack.matidxs); }
__forceinline static int modelHasWeights(const ModelLOD *model) { return (model->data && model->data->pack.weights.unpacksize) || (model->unpack.weights); }
__forceinline static int modelHasVerts2(const ModelLOD *model) { return (model->data && model->data->pack.verts2.unpacksize) || (model->unpack.verts2); }
__forceinline static int modelHasNorms2(const ModelLOD *model) { return (model->data && model->data->pack.norms2.unpacksize) || (model->unpack.norms2); }


SA_RET_OP_VALID __forceinline static ModelLOD *modelGetLOD(SA_PARAM_OP_VALID const Model *model, int lod_index)
{
	if (!model)
		return NULL;
	if (lod_index>=0 && lod_index<eaSize((ccEArrayHandle*)&model->model_lods))
	{
		ModelLOD *model_lod = model->model_lods[lod_index];
		ModelLOD *actual_lod;
		if (!model_lod)
			return NULL;
		if (!(actual_lod = model_lod->actual_lod))
			return NULL;
		// Fixup anyone referencing a ModelLOD which could not be loaded, reference the next higher LOD
		if (actual_lod->loadstate == GEO_LOADED_NULL_DATA && (actual_lod->lod_index > 0))
		{
			actual_lod = model_lod->actual_lod = actual_lod->model_parent->model_lods[actual_lod->lod_index-1];
			assert(actual_lod);
		}
		return SAFE_MEMBER(model_lod, actual_lod);
	}
	return NULL;
}
