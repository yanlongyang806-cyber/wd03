
#include "WorldCollPrivate.h"

static void wcCopyStoredModelData(	WorldCollStoredModelData* smd,
									const char* name,
									const char* detail,
									const WorldCollStoredModelDataDesc* desc)
{
	S32 i;

	smd->name = strdup(name);
	smd->detail = strdup(detail);
	smd->filename = desc->filename;
	
	copyVec3(desc->min, smd->min);
	copyVec3(desc->max, smd->max);

	if(desc->vert_count)
	{
		smd->vert_count = desc->vert_count;
		smd->verts = callocStructs(Vec3, smd->vert_count);
		CopyStructs(smd->verts, desc->verts, smd->vert_count);

		smd->tri_count = desc->tri_count;
		smd->tris = callocStructs(S32, 3 * smd->tri_count);
		CopyStructs(smd->tris, desc->tris, 3 * smd->tri_count);

		smd->norms = callocStructs(Vec3, smd->tri_count);

		for(i = 0; i < smd->tri_count; i++){
			S32*	tri = smd->tris + i * 3;
			Vec3	v01;
			Vec3	v02;

			// MS: Conor said normals are 01x02 so if that's wrong then not my fault.

			subVec3(smd->verts[tri[1]], smd->verts[tri[0]], v01);
			subVec3(smd->verts[tri[2]], smd->verts[tri[0]], v02);

			crossVec3(v01, v02, smd->norms[i]);

			normalVec3(smd->norms[i]);
		}
	}

	if(desc->map_size)
	{
		smd->map_size = desc->map_size;
		smd->grid_size = desc->grid_size;

		smd->heights = callocStructs(F32, desc->map_size*desc->map_size);
		CopyStructs(smd->heights, desc->heights, desc->map_size*desc->map_size);

		smd->holes = callocStructs(bool, desc->map_size*desc->map_size);
		CopyStructs(smd->holes, desc->holes, desc->map_size*desc->map_size);
	}
}

S32	wcStoredModelDataFind(	const char* name,
							WorldCollStoredModelData** smdOut)
{
	WorldCollStoredModelData *smd;

	if(!wcgState.stStoredModelData){
		return 0;
	}

	if(!stashFindPointer(wcgState.stStoredModelData, name, &smd)){
		return 0;
	}

	if(smdOut){
		*smdOut = smd;
	}

	return !!smd;
}

S32	wcStoredModelDataCreate(const char* name,
							const char* detail,
							const WorldCollStoredModelDataDesc* desc,
							WorldCollStoredModelData** smdOut)
{
	WorldCollStoredModelData* smd;
	
	if(	!name ||
		!desc)
	{
		return 0;
	}

	if(!wcgState.stStoredModelData){
		wcgState.stStoredModelData = stashTableCreateWithStringKeys(	100,
																	StashDefault);
	}

	wcStoredModelDataDestroyByName(name);
	
	smd = callocStruct(WorldCollStoredModelData);
	
	wcCopyStoredModelData(smd, name, detail, desc);
	
	stashAddPointer(wcgState.stStoredModelData, smd->name, smd, true);
	
	if(smdOut){
		*smdOut = smd;
	}
	
	return 1;
}

S32	wcStoredModelDataDestroyByName(const char* name){
	WorldCollStoredModelData* smd;

	if(!stashRemovePointer(wcgState.stStoredModelData, name, &smd)){
		return 0;
	}
	
	return wcStoredModelDataDestroy(&smd);
}

S32 wcStoredModelDataDestroy(WorldCollStoredModelData** smdInOut){
	WorldCollStoredModelData* smd = SAFE_DEREF(smdInOut);

	if(smd){
		*smdInOut = NULL;
		SAFE_FREE(smd->name);
		SAFE_FREE(smd->detail);
		SAFE_FREE(smd->tris);
		SAFE_FREE(smd->verts);
		SAFE_FREE(smd->norms);
		SAFE_FREE(smd->heights);
		SAFE_FREE(smd->holes);
		SAFE_FREE(smd);
	}

	return 1;
}

static void wcStoredModelDataDestroyUnsafe(WorldCollStoredModelData* smd){
	wcStoredModelDataDestroy(&smd);
}

S32 wcStoredModelDataDestroyAll(void){
	stashTableClearEx(	wcgState.stStoredModelData,
						NULL,
						wcStoredModelDataDestroyUnsafe);

	return 1;
}

