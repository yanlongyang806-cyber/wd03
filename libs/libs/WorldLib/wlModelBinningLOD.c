#include "wlModelBinning.h"
#include "wlModelBinningLOD.h"
#include "wlModelBinningPrivate.h"
#include "GenericMesh.h"
#include "referencesystem.h"
#include "SimplygonInterface.h"

#include "timing.h"
#include "StringCache.h"
#include "Color.h"
#include "ScratchStack.h"
#include "utils.h"
#include "logging.h"

#include "wlState.h"
#include <materials.h>
#include "wlTerrain.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););


typedef struct LodRequest
{
	LodModel2 *lodmodel;
	ModelSource *srcmodel;
	ReductionMethod method;
} LodRequest;

static ModelSource *sourceModelCopyForModification(const ModelSource *basemodel, int tex_count)
{
	int i;
	ModelSource *model = malloc(sizeof(ModelSource));
	eaPush(&basemodel->gld->allocList, model);
	memcpy(model, basemodel, sizeof(ModelSource));

	ZeroStruct(&model->pack);

	model->tex_count = tex_count;
	model->tex_idx = calloc(sizeof(TexID), tex_count);
	eaPush(&basemodel->gld->allocList, model->tex_idx);

	model->lod_models = NULL;
	model->lods = NULL;

	for (i = 0; i < model->tex_count; i++)
		model->tex_idx[i].id = basemodel->tex_idx[i].id;

	model->srcmodel = basemodel;

	//gfxFillModelRenderInfo(model);

	return model;
}

int sourceModelToGMesh(GMesh *mesh, ModelSource *srcmodel, GMeshAttributeUsage ignoredAttributes)
{
	int i, j;
	GTriIdx *meshtri;
	int *modeltri;
	int foundBadTangentBasis = 0;
	GMeshAttributeUsage attributes = USE_ALL_GMESH_ATTRIBUTES & (~ignoredAttributes);

	PERFINFO_AUTO_START_FUNC();
	// should already be unpacked

	gmeshSetUsageBits(mesh, attributes & (USE_POSITIONS |
							(srcmodel->unpack.verts2?USE_POSITIONS2:0) |
							(srcmodel->unpack.norms?USE_NORMALS:0) |
							(srcmodel->unpack.norms2?USE_NORMALS2:0) |
							(srcmodel->unpack.sts?USE_TEX1S:0) |
							(srcmodel->unpack.sts3?USE_TEX2S:0) |
							(srcmodel->unpack.colors?USE_COLORS:0) |
							(srcmodel->unpack.matidxs?USE_BONEWEIGHTS:0)));

	gmeshSetVertCount(mesh, srcmodel->vert_count);
	gmeshSetTriCount(mesh, srcmodel->tri_count);

	memcpy(mesh->positions, srcmodel->unpack.verts, mesh->vert_count * sizeof(Vec3));
	if (srcmodel->unpack.norms && (attributes & USE_NORMALS))
		memcpy(mesh->normals, srcmodel->unpack.norms, mesh->vert_count * sizeof(Vec3));
	if (srcmodel->unpack.norms2 && (attributes & USE_NORMALS2))
		memcpy(mesh->normals2, srcmodel->unpack.norms2, mesh->vert_count * sizeof(Vec3));
	if (srcmodel->unpack.verts2 && (attributes & USE_POSITIONS2))
		memcpy(mesh->positions2, srcmodel->unpack.verts2, mesh->vert_count * sizeof(Vec3));
	if (srcmodel->unpack.sts && (attributes & USE_TEX1S))
		memcpy(mesh->tex1s, srcmodel->unpack.sts, mesh->vert_count * sizeof(Vec2));
	if (srcmodel->unpack.sts3 && (attributes & USE_TEX2S))
		memcpy(mesh->tex2s, srcmodel->unpack.sts3, mesh->vert_count * sizeof(Vec2));
	if (srcmodel->unpack.colors && (attributes & USE_COLORS))
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			mesh->colors[i] = CreateColor(srcmodel->unpack.colors[i*4+0], srcmodel->unpack.colors[i*4+1], srcmodel->unpack.colors[i*4+2], srcmodel->unpack.colors[i*4+3]);
		}
	}
	if (srcmodel->unpack.matidxs && (attributes & USE_BONEWEIGHTS))
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			copyVec4(&srcmodel->unpack.matidxs[i*4], mesh->bonemats[i]);
			scaleVec4(&srcmodel->unpack.weights[i*4], U8TOF32_COLOR, mesh->boneweights[i]);
		}
	}

	meshtri = mesh->tris;
	modeltri = srcmodel->unpack.tris;
	for (i = 0; i < srcmodel->tex_count; i++)
	{
		for (j = 0; j < srcmodel->tex_idx[i].count; j++)
		{
			meshtri->idx[0] = *(modeltri++);
			meshtri->idx[1] = *(modeltri++);
			meshtri->idx[2] = *(modeltri++);
			meshtri->tex_id = srcmodel->tex_idx[i].id;
			meshtri++;
		}
	}

	if (srcmodel->unpack.norms && (attributes & USE_NORMALS))
	{
		for (i = 0; i < mesh->vert_count && !foundBadTangentBasis; ++i)
		{
			if (lengthVec3Squared(srcmodel->unpack.norms[i]) < 1.0e-6f)
				foundBadTangentBasis = 1;
		}
	}
	if (srcmodel->unpack.tangents && (attributes & USE_TANGENTS))
	{
		for (i = 0; i < mesh->vert_count && !foundBadTangentBasis; ++i)
		{
			if (lengthVec3Squared(srcmodel->unpack.tangents[i]) < 1.0e-6f)
				foundBadTangentBasis = 1;
		}
	}
	if (srcmodel->unpack.binorms && !foundBadTangentBasis && (attributes & USE_BINORMALS))
	{
		for (i = 0; i < mesh->vert_count && !foundBadTangentBasis; ++i)
		{
			if (lengthVec3Squared(srcmodel->unpack.binorms[i]) < 1.0e-6f)
				foundBadTangentBasis = 1;
		}
	}

	PERFINFO_AUTO_STOP();

	return foundBadTangentBasis;
}

void sourceModelFromGMesh(ModelSource *model, const GMesh *mesh, Geo2LoadData *gld)
{
	int i, j, last_tex;
	GTriIdx *meshtri;
	int *modeltri;
	void *data;

	PERFINFO_AUTO_START_FUNC();

	SET_FP_CONTROL_WORD_DEFAULT;

#define mallocWrapped(bytes) ((data = malloc(bytes)), eaPush(&gld->allocList, data), data)
	model->tri_count = mesh->tri_count;
	model->vert_count = mesh->vert_count;

	model->unpack.verts = mallocWrapped(mesh->vert_count * sizeof(Vec3));
	memcpy(model->unpack.verts, mesh->positions, mesh->vert_count * sizeof(Vec3));

	if (mesh->positions2)
	{
		model->unpack.verts2 = mallocWrapped(mesh->vert_count * sizeof(Vec3));
		memcpy(model->unpack.verts2, mesh->positions2, mesh->vert_count * sizeof(Vec3));
	}

	if (mesh->normals)
	{
		model->unpack.norms = mallocWrapped(mesh->vert_count * sizeof(Vec3));
		memcpy(model->unpack.norms, mesh->normals, mesh->vert_count * sizeof(Vec3));
	}

	if (mesh->normals2)
	{
		model->unpack.norms2 = mallocWrapped(mesh->vert_count * sizeof(Vec3));
		memcpy(model->unpack.norms2, mesh->normals2, mesh->vert_count * sizeof(Vec3));
	}

	if (mesh->tangents)
	{
		model->unpack.tangents = mallocWrapped(mesh->vert_count * sizeof(Vec3));
		memcpy(model->unpack.tangents, mesh->tangents, mesh->vert_count * sizeof(Vec3));
	}
	if (mesh->binormals)
	{
		model->unpack.binorms = mallocWrapped(mesh->vert_count * sizeof(Vec3));
		memcpy(model->unpack.binorms, mesh->binormals, mesh->vert_count * sizeof(Vec3));
	}
	if (mesh->tex1s)
	{
		model->unpack.sts = mallocWrapped(mesh->vert_count * sizeof(Vec2));
		memcpy(model->unpack.sts, mesh->tex1s, mesh->vert_count * sizeof(Vec2));
	}
	if (mesh->tex2s)
	{
		model->unpack.sts3 = mallocWrapped(mesh->vert_count * sizeof(Vec2));
		memcpy(model->unpack.sts3, mesh->tex2s, mesh->vert_count * sizeof(Vec2));
	}
	if (mesh->colors)
	{
		model->unpack.colors = mallocWrapped(mesh->vert_count * 4 * sizeof(U8));
		for (i = 0; i < mesh->vert_count; ++i)
		{
			model->unpack.colors[i*4+0] = mesh->colors[i].r;
			model->unpack.colors[i*4+1] = mesh->colors[i].g;
			model->unpack.colors[i*4+2] = mesh->colors[i].b;
			model->unpack.colors[i*4+3] = mesh->colors[i].a;
		}
	}
	if (mesh->bonemats)
	{
		model->unpack.matidxs = mallocWrapped(mesh->vert_count * 4 * sizeof(U8));
		model->unpack.weights = mallocWrapped(mesh->vert_count * 4 * sizeof(U8));
		for (i = 0; i < mesh->vert_count; ++i)
		{
			copyVec4(mesh->bonemats[i], &model->unpack.matidxs[i*4]);
			model->unpack.weights[i*4+0] = round(CLAMPF32(mesh->boneweights[i][0], 0.0f, 1.0f) * 255);
			model->unpack.weights[i*4+1] = round(CLAMPF32(mesh->boneweights[i][1], 0.0f, 1.0f) * 255);
			model->unpack.weights[i*4+2] = round(CLAMPF32(mesh->boneweights[i][2], 0.0f, 1.0f) * 255);
			model->unpack.weights[i*4+3] = round(CLAMPF32(mesh->boneweights[i][3], 0.0f, 1.0f) * 255);
		}
	}

	meshtri = mesh->tris;
	modeltri = model->unpack.tris = mallocWrapped(mesh->tri_count * sizeof(int) * 3);

	last_tex = 0;
	for (i = 0; i < mesh->tri_count; i++)
	{
		for (j = last_tex; j < model->tex_count; j++)
		{
			if (model->tex_idx[j].id == meshtri->tex_id)
			{
				model->tex_idx[j].count++;
				last_tex = j;
				break;
			}
		}
		if (model->tex_count)
			assert(j < model->tex_count);

		*(modeltri++) = meshtri->idx[0];
		*(modeltri++) = meshtri->idx[1];
		*(modeltri++) = meshtri->idx[2];
		meshtri++;
	}

	// remove subobjects with no triangles
	for (i = 0; i < model->tex_count; ++i)
	{
		if (!model->tex_idx[i].count)
		{
			if (i < model->tex_count-1)
				memmove(&model->tex_idx[i], &model->tex_idx[i+1], (model->tex_count - i - 1) * sizeof(TexID));
			--model->tex_count;
			--i;
		}
	}

#undef mallocWrapped
	PERFINFO_AUTO_STOP();
}

static void reorderTris(GMesh *mesh, ModelSource *model)
{
	int		i,j,tricount=0;
	GTriIdx	*temp_tris;

	temp_tris = ScratchAlloc(sizeof(GTriIdx) * mesh->tri_count);

	for(i=0;i<model->tex_count;i++)
	{
		for(j=0;j<mesh->tri_count;j++)
		{
			if (model->tex_idx[i].id == mesh->tris[j].tex_id)
			{
				temp_tris[tricount++] = mesh->tris[j];
			}
		}
	}
	assert(tricount == mesh->tri_count);

	memcpy(mesh->tris,temp_tris,sizeof(GTriIdx) * mesh->tri_count);
	ScratchFree(temp_tris);
}


static void wlBinningLODAModel(LodRequest *lod)
{
	LodModel2 *lodmodel = lod->lodmodel;

	ModelSource *model = lodmodel->model;
	ModelSource *srcmodel;
	GMesh mesh={0};
	GMesh reducedmesh={0};
	F32 target_error;
	int target_tricount;
	bool is_char_lib = strStartsWith(model->gld->filename, "character_library/");

	srcmodel = lod->srcmodel;

	//////////////////////////////////////////////////////////////////////////
	// convert model to mesh and remove tangent space
	if (sourceModelToGMesh(&mesh, srcmodel, 0))
		log_printf(LOG_ERRORS, "Source model has bad tangent basis %s\n", model->gld->filename);
	gmeshSetUsageBits(&mesh, mesh.usagebits & ~(USE_TANGENTS|USE_BINORMALS));

	//////////////////////////////////////////////////////////////////////////
	// reduce mesh
	PERFINFO_AUTO_START("gmeshReduce",1);
	target_error = lodmodel->error*0.01f;
	target_tricount = (int)(mesh.tri_count * (1.f - target_error));
	
	// TODO REMESH LOD
	//  need to decide exactly how to handle textures and materials here
	//if (remesh)
	//{
	//	ModelClusterSource mcs;
	//	modelClusterSourceAddSourceGMesh(&mcs, unitmat, &mesh, );
	//	processModelClusterToSimplygonMesh(&mcs, &reducedmesh, );
	//}
	//else
	if (is_char_lib)
		lodmodel->error = 100.f * gmeshReduceDebug(&reducedmesh, &mesh, target_error, target_tricount, lod->method, 0.6f, lodmodel->upscale, false, true, false, false, model->gld->filename, model->gld->filename);
	else
		lodmodel->error = 100.f * gmeshReduceDebug(&reducedmesh, &mesh, target_error, target_tricount, lod->method, 0.1f, lodmodel->upscale, true, true, false, false, model->gld->filename, model->gld->filename);

	if (lodmodel->error < 0 && target_error == 0)
	{
		// We failed to reduce it, but it looks like we didn't want to reduce it, so it's fine.
		// This probably only happens when the high LOD is replaced with a hand-build model instead
		lodmodel->error = 0;
	}
	assert(lodmodel->error >= 0);
	gmeshFreeData(&mesh);
	PERFINFO_AUTO_STOP();

	//////////////////////////////////////////////////////////////////////////
	// calculate new tangent space and convert mesh to model
	// Note: we do not allow wind on reduced LODs, only the highest detail model
	gmeshAddTangentSpace(&reducedmesh, false, model->gld->filename, LOG_ERRORS);
	reorderTris(&reducedmesh, model);
	sourceModelFromGMesh(model, &reducedmesh, srcmodel->gld);
	gmeshFreeData(&reducedmesh);

	//////////////////////////////////////////////////////////////////////////
	// final data fixup
	lodmodel->tri_percent = 100.f * ((F32)model->tri_count) / srcmodel->tri_count;

	free(lod);
}

ModelSource *wlModelBinningGenerateLOD(ModelSource *srcmodel, F32 error, F32 upscale, ReductionMethod method, int lod_index)
{
	LodModel2 *lodmodel;
	ModelSource *model;
	F32 tri_percent = 100.f - error;
	LodRequest *lodrequest;

	if (!error)
		return srcmodel;

	model = sourceModelCopyForModification(srcmodel, srcmodel->tex_count);

	// Generate name
	{
		char name[256];
		sprintf(name, "%s (LOD)", srcmodel->name);
		model->name = allocAddString(name);
	}

	//////////////////////////////////////////////////////////////////////////
	// add to lod model list
	lodmodel = callocStruct(LodModel2);
	eaPush(&srcmodel->gld->allocList, lodmodel);
	lodmodel->index = lod_index;
	lodmodel->tri_percent = tri_percent;
	lodmodel->error = error;
	lodmodel->upscale = upscale;
	lodmodel->model = model;
	eaPush(&srcmodel->lod_models, lodmodel);


	//////////////////////////////////////////////////////////////////////////
	// add to background loader queue
	lodrequest = calloc(sizeof(*lodrequest), 1);
	lodrequest->lodmodel = lodmodel;
	lodrequest->srcmodel = srcmodel;
	lodrequest->method = method;

	wlBinningLODAModel(lodrequest);

	return model;
}

SimplygonMesh *wlSimplygonMeshFromModelAndMaterial(
	const char *modelName,
	Material **eaMaterialList,
	TextureSwap **eaTextureSwaps,
	SimplygonMaterialTable *materialTable,
	const char *tempDir,
	int *totalTexSize) {

	int *materialMapping = NULL;
	REF_TO(ModelHeader) hInfo = {0};
	ModelHeader *mh;
	FileList fileList = {0};
	Geo2LoadData *gld = NULL;
	SimplygonMesh *sgmesh = NULL;
	ModelLOD *lod = NULL;
	Material **eaMyMaterialList = NULL;

	assert(wl_state.get_simplygon_material_id_from_table);

	SET_HANDLE_FROM_STRING("ModelHeader", modelName, hInfo);
	if(REF_IS_VALID(hInfo)) {

		int i;

		mh = GET_REF(hInfo);
		if(mh) {

			Model *model;

			gld = geo2LoadModelFromSource(mh, &fileList);

			model = modelFind(modelName, true, WL_FOR_WORLD);
			lod = modelLODWaitForLoad(model, 0);

			if(lod && materialTable) {

				// Fill up as much as we can from the input material list.
				for(i = 0; i < eaSize(&eaMaterialList); i++) {
					eaPush(&eaMyMaterialList, eaMaterialList[i]);
				}

				// If there's no input material list or it's incomplete, fill in the rest with defaults.
				for(i = eaSize(&eaMaterialList); i < lod->data->tex_count; i++) {
					eaPush(&eaMyMaterialList, lod->materials[i]);
				}

				if(eaMyMaterialList) {

					// If the number of materials on the model doesn't match the number of material inputs, then we have a
					// problem.
					assert(eaSize(&eaMyMaterialList) == lod->data->tex_count);

					materialMapping = calloc(eaSize(&eaMyMaterialList), sizeof(int));

					for(i = 0; i < eaSize(&eaMyMaterialList); i++) {
						materialMapping[i] =
							wl_state.get_simplygon_material_id_from_table(
								materialTable,
								eaMyMaterialList[i],
								eaTextureSwaps,
								tempDir, NULL, NULL);
					}
				}

			}

			for(i = 0; i < eaSize(&gld->models); i++) {

				if(stricmp(gld->models[i]->name, modelName) == 0) {

					GMesh gmesh = {0};

					sourceModelToGMesh(&gmesh, gld->models[i], USE_COLORS);

					// Add tangent space data if the model doesn't already have it.
					if(!(gmesh.usagebits & (USE_BINORMALS | USE_TANGENTS))) {
						gmeshAddTangentSpace(
							&gmesh, false,
							gld->filename,
							LOG_ERRORS);
					}

					// TODO: If we ever want to use the remshing on characters, we're going to need some more elegant
					// handling of usagebits here than just excluding boneweights. -Cliff
					sgmesh = simplygonMeshFromGMesh(
						&gmesh, materialMapping,
						eaSize(&eaMyMaterialList),
						NULL, modelName, ~USE_BONEWEIGHTS);

					gmeshFreeData(&gmesh);

					break;
				}
			}

			geo2Destroy(gld);
		}
	}
	REMOVE_HANDLE(hInfo);

	if(eaMyMaterialList) {
		eaDestroy(&eaMyMaterialList);
	}

	eaDestroyEx(&fileList, NULL);

	if (materialMapping)
	{
		ANALYSIS_ASSUME(materialMapping);
		free(materialMapping);
	}
	return sgmesh;
}

#include "group.h"

AUTO_COMMAND ACMD_CATEGORY(Debug);
void simplygonRemeshTest(
	ACMD_NAMELIST("ModelHeader", REFDICTIONARY) const char *modelName,
	bool testMaterialSwap) {

	int i;
	SimplygonScene *scene = NULL;
	SimplygonNode *rootNode = NULL;

	SimplygonNode **nodesToCleanUp = NULL;
	SimplygonMesh *remeshedObject = NULL;

	Material **eaMaterials = NULL;

	SimplygonMaterialTable *sgMatTable;
	TextureSwap **eaTextureSwaps = NULL;

	TextureSwap newSwap;

	srand(0);

	simplygonSetupPath();

	sgMatTable = simplygon_createMaterialTable();

	scene = simplygon_createScene();
	rootNode = simplygon_sceneGetRootNode(scene);
	eaPush(&nodesToCleanUp, rootNode);

	if(testMaterialSwap) {

		newSwap.orig_name = allocAddString("Default_Alpha");
		newSwap.replace_name = allocAddString("Checker64");
		eaPush(&eaTextureSwaps, &newSwap);
	}

	for(i = 0; i < 100; i++) {

		SimplygonMesh *sgmesh;
		SimplygonNode *node;
		Mat44 transform;

		copyMat44(unitmat44, transform);

		// transform[3][0] = (float)(rand() % 50);
		// transform[3][2] = (float)(rand() % 50);
		transform[3][0] = (i % 10) * 50;
		transform[3][2] = (i / 10) * 50;

		sgmesh = wlSimplygonMeshFromModelAndMaterial(modelName, eaMaterials, eaTextureSwaps, sgMatTable, "c:\\testModels", NULL);

		if(sgmesh) {
			node = simplygon_createSceneNodeFromMesh(sgmesh);
			eaPush(&nodesToCleanUp, node);
			simplygon_nodeSetMatrix(node, (float*)transform);
			simplygon_nodeAddChild(rootNode, node);

			simplygon_destroyMesh(sgmesh);
		}
	}

	printf("----------------------------------------------------------------------\n");
	printf("Scene graph before remeshing...\n");
	printf("----------------------------------------------------------------------\n");
	simplygon_dumpSceneNodeHierarchy(rootNode);

	srand(0);
	remeshedObject = simplygon_doRemesh(
		scene, 512, 1024, 1024, 8, sgMatTable, NULL, NULL, NULL, NULL, NULL);

	printf("----------------------------------------------------------------------\n");
	printf("Scene graph after remeshing...\n");
	printf("----------------------------------------------------------------------\n");
	simplygon_dumpSceneNodeHierarchy(rootNode);

	eaDestroy(&eaTextureSwaps);
	eaDestroy(&eaMaterials);
	eaDestroyEx(&nodesToCleanUp, simplygon_destroySceneNode);
	simplygon_destroyScene(scene);
	simplygon_destroyMesh(remeshedObject);
	simplygon_destroyMaterialTable(sgMatTable);
	simplygon_assertFreedAll();
	simplygon_shutdown();

}

