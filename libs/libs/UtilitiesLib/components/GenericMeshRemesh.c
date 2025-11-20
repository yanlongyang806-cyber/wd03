#include "GenericMesh.h"
#include "GenericMeshRemesh.h"

#include "sysutil.h"
#include "fileutil.h"
#include "error.h"
#include "hoglib.h"
#include "SimplygonInterface.h"
#include "logging.h"
#include "serialize.h"
#include "textparser.h"
#include "ImageTypes.h"
#include "WTex.h"
#include "ReadPNG.h"
#include "StashTable.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););


int simplygonAutoShutdownCount = 0;
AUTO_CMD_INT(simplygonAutoShutdownCount, simplygonAutoShutdownCount) ACMD_CMDLINEORPUBLIC;
int simplygonRemeshCount = 0;

int SIMPLYGON_DEFAULT_RESOLUTION = ClusterGeometryResolution128;
AUTO_CMD_INT(SIMPLYGON_DEFAULT_RESOLUTION, simplygonResolution) ACMD_CMDLINEORPUBLIC;
int simplygonDefaultResolution()
{
	return SIMPLYGON_DEFAULT_RESOLUTION;
}

unsigned int SIMPLYGON_TEXTURE_HEIGHT = ClusterTextureResolution512;
AUTO_CMD_INT(SIMPLYGON_TEXTURE_HEIGHT, simplygonTexHeight) ACMD_CMDLINEORPUBLIC;
unsigned int simplygonTextureHeight()
{
	return SIMPLYGON_TEXTURE_HEIGHT;
}

unsigned int SIMPLYGON_TEXTURE_WIDTH = ClusterTextureResolution512;
AUTO_CMD_INT(SIMPLYGON_TEXTURE_WIDTH, simplygonTexWidth) ACMD_CMDLINEORPUBLIC;
unsigned int simplygonTextureWidth()
{
	return SIMPLYGON_TEXTURE_WIDTH;
}

int SIMPLYGON_DEFAULT_MAX_PIXEL_DEVIATION = 8;
AUTO_CMD_INT(SIMPLYGON_DEFAULT_MAX_PIXEL_DEVIATION, simplygonMaxPixelDeviation) ACMD_CMDLINEORPUBLIC;

int SIMPLYGON_DEFAULT_SUPER_SAMPLING = ClusterTextureSupersample4x;
AUTO_CMD_INT(SIMPLYGON_DEFAULT_SUPER_SAMPLING, simplygonSuperSample) ACMD_CMDLINEORPUBLIC;
int simplygonDefaultSuperSampling()
{
	return SIMPLYGON_DEFAULT_SUPER_SAMPLING;
}

/// Fixup function for WorldClusterStats
TextParserResult fixupWorldClusterStats(WorldClusterStats *worldClusterStats, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_CONSTRUCTOR) {
		int texIndex = 0;
		for (texIndex = 0; texIndex < CLUSTER_MAX_TEXTURES; ++texIndex)
			eaPush(&worldClusterStats->eaCastTextures, StructCreateDefault(TaskProfile));
	}
	return PARSERESULT_SUCCESS;
}

ModelClusterVolume* modelClusterVolumeCreate()
{
	ModelClusterVolume *modelClusterVolume = StructCreateDefault(ModelClusterVolume);

	modelClusterVolume->geo_resolution = simplygonDefaultResolution();
	modelClusterVolume->include_normal = false;
	modelClusterVolume->include_specular = false;
	modelClusterVolume->texture_height = simplygonTextureHeight();
	modelClusterVolume->texture_width = simplygonTextureWidth();
	modelClusterVolume->texture_super_sample = simplygonDefaultSuperSampling();
	modelClusterVolume->lod_max = ClusterMaxLODLevel_Default;
	modelClusterVolume->lod_min = ClusterMinLevel256ft;
	modelClusterVolume->target_lod_level = -1;	// Set to -1 to ensure that these properties will be readily overridden by any overlapping volumes.

	return modelClusterVolume;
}

TextParserResult fixupModelClusterVolume(ModelClusterVolume *modelClusterVolume, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_CONSTRUCTOR) {
	}

	return PARSERESULT_SUCCESS;
}

/// Fixup function for ModelClusterObject
TextParserResult fixupModelClusterObject(ModelClusterObject *modelClusterObject, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_DESTRUCTOR) {
		if (modelClusterObject->gmesh)
			gmeshFree(modelClusterObject->gmesh);
		if (modelClusterObject->material)
			simplygon_destroyMaterial(modelClusterObject->material);
	}
	return PARSERESULT_SUCCESS;
}

/// Fixup function for ModelClusterSource
TextParserResult fixupModelClusterSource(ModelClusterSource *modelClusterSource, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_DESTRUCTOR) {
		if (modelClusterSource->texture_table)
			stashTableDestroy(modelClusterSource->texture_table);
	}

	if (eType == FIXUPTYPE_CONSTRUCTOR) {
		modelClusterSource->cluster_tool_version = CLUSTER_TOOL_VERSION;
	}

	return PARSERESULT_SUCCESS;
}

ModelClusterSource* modelClusterSourceCreateDefault()
{
	ModelClusterSource *retMCS = StructCreateDefault(ModelClusterSource);

	retMCS->cluster_volumes_condensed = modelClusterVolumeCreate();
	return retMCS;
}

U32 hogFileCopyFileToFileSystem(HogFile * handle, const char * dest_filename, const char * filename)
{
	SimpleBufHandle fileDataBufHandle = SimpleBufOpenRead(filename, handle);
	U32 ouputFileSize = 0;
	if (fileDataBufHandle)
	{
		FILE *outf = fopen(dest_filename, "wb"); // JE: *not* fileOpen because it does a fileLocateWrite which may redirect us to/from Core!
		if (!outf)
			Errorf("Error opening %s for writing!", filename);
		else
		{
			void * fileRawData = SimpleBufGetData(fileDataBufHandle, &ouputFileSize);
			fwrite(fileRawData, 1, ouputFileSize, outf);
			fclose(outf);
		}

		SimpleBufClose(fileDataBufHandle);
	}
	return ouputFileSize;
}

U32 pngDumpFromHogFile(HogFile * handle, const char * dest_filename, const char * filename)
{
	return hogFileCopyFileToFileSystem(handle, dest_filename, filename);
}

// texture_full_path is expected to already have enough space allocated to contain the full path of the texture file.
void dumpModelClusterPngToTempDir(HogFile *clusterHogg, const char* tempDir, const char* texture_file_name, char* texture_full_path, size_t texture_full_path_size)
{
	char pngFileName[MAX_PATH];
	char just_file_name[MAX_PATH];

	fileGetFilename(texture_file_name,just_file_name);
	sprintf(pngFileName,"%s/%s",tempDir,just_file_name);
	pngDumpFromHogFile(clusterHogg, pngFileName, just_file_name);
	strcpy_s(texture_full_path, texture_full_path_size, pngFileName);
}

ModelClusterSource* modelClusterSourceHoggLoad(HogFile *clusterHogg, const char *tempDir)
{
	int i;
	bool clusterHoggCreated = false;
	ModelClusterSource *mcs = NULL;
	int dirResult = 0;

	dirResult = mkdir(tempDir);	// Ensure that the directory we'll be dumping files into is actually present.
	// Read in ModelClusterSource from the hogg file
	{
		HogFileIndex source_bin_hog_index = HOG_INVALID_INDEX;
		SimpleBufHandle cluster_buf_handle = NULL;

		source_bin_hog_index = hogFileFind(clusterHogg, "Cluster_Source.Mcst");
		if (source_bin_hog_index == HOG_INVALID_INDEX)
			return NULL;

		mcs = StructCreate(parse_ModelClusterSource);

		if(ParserReadTextFileFromHogg("Cluster_Source.Mcst", parse_ModelClusterSource, mcs, clusterHogg) != PARSERESULT_SUCCESS) {
			StructDestroy(parse_ModelClusterSource, mcs);
			return NULL;
		}

	}
	for (i = eaSize(&mcs->cluster_models) - 1; i >= 0; i--) {
		int j;
		ModelClusterObject *mco = mcs->cluster_models[i];

		if (!mco->mesh_filename) {
			// This should probably be wiped from the modelclustersource object.
			eaRemove(&mcs->cluster_models,i);
			StructDestroy(parse_ModelClusterObject,mco);
			continue;
		}
		// take care of gmesh here
		{
			SimpleBufHandle meshHandle;

			meshHandle = SimpleBufOpenRead(mco->mesh_filename,clusterHogg);
			mco->gmesh = gmeshFromBinData(meshHandle);
			SimpleBufClose(meshHandle);
		}

		for (j = 0; j < eaSize(&mco->gmesh_texture_files); j++) {
			ModelClusterTextures *mct = mco->gmesh_texture_files[j];
			// take care of textures here
			if (mct->gmesh_texture_file_d[0]) {
				dumpModelClusterPngToTempDir(clusterHogg, tempDir, mct->gmesh_texture_file_d, SAFESTR(mct->gmesh_texture_file_d));
			}
			if (mct->gmesh_texture_file_s[0]) {
				dumpModelClusterPngToTempDir(clusterHogg, tempDir, mct->gmesh_texture_file_s, SAFESTR(mct->gmesh_texture_file_s));
			}
			if (mct->gmesh_texture_file_n[0]) {
				dumpModelClusterPngToTempDir(clusterHogg, tempDir, mct->gmesh_texture_file_n, SAFESTR(mct->gmesh_texture_file_n));
			}
		}
	}
	return mcs;
}

bool modelClusterMoveRemeshImagesToHogg(HogFile *hog_file, const char *image_name, const char* image_filename,
										TaskProfile *dxtStats, TaskProfile *cacheTexStats, float texture_scale, RdrTexFormat dest_format)
{
	U8 *file_data;
	int file_size;
	int width;
	int height;
	char wtex_filename[MAX_PATH];
	char wtex_name[MAX_PATH];
	char *extension;
	bool bDXTSuccess = false;

	taskStartTimer(cacheTexStats);
	// copy the PNG into the hogg first since we'll need it for hierarchical clustering
	// Include the wtex for now so as not to break the existing code
	file_data = fileAlloc(image_filename, &file_size);
	assert(file_data);
	hogFileModifyUpdateNamed(hog_file, image_name, file_data, file_size, _time32(NULL), NULL);	// do not free file_data since this function frees it for us when done processing.
	taskAttributeReadWriteIO(cacheTexStats, file_size);
	taskStopTimer(cacheTexStats);


	taskStartTimer(dxtStats);
	// read PNG file
	file_data = ReadPNG_FileEx(image_filename,&width,&height, true);
	// write out to a wtex file
	assert(file_data);
	taskAttributeReadIO(dxtStats, file_size);


	Strcpy(wtex_filename,image_filename);
	extension = strstri(wtex_filename,".png\0");
	assert(extension);	// means the filename is not ending with the png extension.
	strcpy_unsafe(extension,".wtex\0");

	Strcpy(wtex_name,image_name);
	extension = strstri(wtex_name,".png\0");
	assert(extension);	// means the filename is not ending with the png extension.
	strcpy_unsafe(extension,".wtex\0");

	taskStopTimer(dxtStats);

	bDXTSuccess = texWriteWtex(file_data, width, height, texture_scale, wtex_filename, dest_format, TEXOPT_CLAMPS | TEXOPT_CLAMPT, QUALITY_LOWEST, dxtStats);
	// move wtex file into hogg
	free(file_data);
	file_data = NULL;

	if (bDXTSuccess)
	{
		taskStartTimer(cacheTexStats);
		file_data = fileAlloc(wtex_filename, &file_size);
		taskAttributeReadWriteIO(cacheTexStats, file_size);
		if (file_data)
			hogFileModifyUpdateNamed(hog_file, wtex_name, file_data, file_size, _time32(NULL), NULL);	// do not free file_data since this function frees it for us when done processing.
		else
			bDXTSuccess = false;
		taskStopTimer(cacheTexStats);
	}
	return bDXTSuccess;
}

static void loadUpdateMemStatus()
{
	unsigned long availVirtualMemoryBytes = 0;
	getPhysicalMemoryEx(NULL, NULL, &availVirtualMemoryBytes);
	availVirtualMemoryBytes = 2048 - availVirtualMemoryBytes / (1024 * 1024);
	loadupdate_printf("%d MB used", availVirtualMemoryBytes);
}

// geoLoadData MUST be destroyed after this has run its course.
static SimplygonMesh *processModelCluster(ModelClusterSource *modelCluster, const int resolution, 
	const unsigned int textureHeight, int textureWidth, const int maxPixelDeviation, const char * image_tempdir, 
	const char* image_prefix, WorldClusterStats * remeshStats)
{
	int i;
	SimplygonScene *scene = NULL;
	SimplygonNode *rootNode = NULL;
	SimplygonNode **nodesToCleanUp = NULL;
	SimplygonMesh *simpleResult;
	int clusterSize = eaSize(&modelCluster->cluster_models);
	SimplygonRemeshStats sgRemeshStats = { 0 };
	RemeshSettings remeshSettings = { 0 };

	SimplygonMaterialTable *sgMatTable;

	char simplygon_imagename_D[MAX_PATH];
	char simplygon_imagename_S[MAX_PATH];
	char simplygon_imagename_N[MAX_PATH];
	int totalTexSize = 0;

	ModelClusterVolume *mcv = modelCluster->cluster_volumes_condensed;
	if (!mcv->include_normal)
		remeshSettings |= REMESH_DISABLE_NORMALS;
	if (!mcv->include_specular)
		remeshSettings |= REMESH_DISABLE_SPECULAR;

	if (image_prefix) {
		sprintf(simplygon_imagename_D,"%s\\%s_D.png",image_tempdir,image_prefix);
		backSlashes(simplygon_imagename_D);
		sprintf(simplygon_imagename_S,"%s\\%s_S.png",image_tempdir,image_prefix);
		backSlashes(simplygon_imagename_S);
		sprintf(simplygon_imagename_N,"%s\\%s_N.png",image_tempdir,image_prefix);
		backSlashes(simplygon_imagename_N);
	}

	simplygonSetupPath();

	sgMatTable = simplygon_createMaterialTable();

	if (!modelCluster)
		return NULL;

	scene = simplygon_createScene();
	rootNode = simplygon_sceneGetRootNode(scene);

	eaPush(&nodesToCleanUp, rootNode);
	loadstart_printf("Sending %d models to the scene for Simplygon to remesh\n",clusterSize);

	taskStartTimer(&remeshStats->convertGeo);
	for (i = 0; i < clusterSize; i++) {
		ModelClusterObject * source_model = modelCluster->cluster_models[i];
		SimplygonMesh *sgmesh;

		assert(source_model->gmesh);
		{
			bool oversized = false;
			int sgInputTextures = eaSize(&source_model->gmesh_texture_files);
			int * sgMaterialSetMap = calloc(sgInputTextures, sizeof(int));
			GMeshAttributeUsage exclusionFlags = USE_EMPTY;
			SimplygonMaterial * sg_texture_material;
			int j;
			char uniqueTexComboName[MAX_PATH * 3];

			for (j = 0; j < sgInputTextures; j++) {
				const ModelClusterTextures * source_tex = source_model->gmesh_texture_files[j];
				int existingMaterialIndex = -1;

				sprintf(uniqueTexComboName, "%s_%s_%s", source_tex->gmesh_texture_file_d, 
					source_tex->gmesh_texture_file_n, source_tex->gmesh_texture_file_s);

				existingMaterialIndex = simplygon_materialTableGetMaterialId(sgMatTable, uniqueTexComboName);
				if (existingMaterialIndex == -1)
				{
					sg_texture_material = simplygon_createMaterial();
					if (strlen(source_tex->gmesh_texture_file_d))
						simplygon_setMaterialTexture(sg_texture_material, SIMPLYGON_MATERIAL_TEXTURE_DIFFUSE, source_tex->gmesh_texture_file_d, false);
					if (mcv->include_normal && strlen(source_tex->gmesh_texture_file_n))
						simplygon_setMaterialTexture(sg_texture_material, SIMPLYGON_MATERIAL_TEXTURE_NORMALS, source_tex->gmesh_texture_file_n, false);
					if (mcv->include_specular && strlen(source_tex->gmesh_texture_file_s))
						simplygon_setMaterialTexture(sg_texture_material, SIMPLYGON_MATERIAL_TEXTURE_SPECULAR, source_tex->gmesh_texture_file_s, false);
					existingMaterialIndex = simplygon_materialTableAddMaterial(sgMatTable, sg_texture_material, uniqueTexComboName, true);
				}
				sgMaterialSetMap[j] = existingMaterialIndex;
			}

			if (!mcv->include_normal)
				exclusionFlags |= USE_BINORMALS | USE_TANGENTS;
			sgmesh = simplygonMeshFromGMesh(source_model->gmesh, sgMaterialSetMap, sgInputTextures, &oversized, "cluster", ~USE_BONEWEIGHTS & ~exclusionFlags);
			free(sgMaterialSetMap);
		}

		if (sgmesh) {
			Mat44 transform;
			SimplygonNode *node;

			node = simplygon_createSceneNodeFromMesh(sgmesh);
			copyMat44(source_model->model_matrix, transform);
			eaPush(&nodesToCleanUp, node);
			simplygon_nodeSetMatrix(node, (float*)transform);

			simplygon_nodeAddChild(rootNode, node);
			simplygon_destroyMesh(sgmesh);
		}
	}
	taskStopTimer(&remeshStats->convertGeo);

	log_printf(LOG_SIMPLYGON,"\t\tTotal texture size: %d bytes", totalTexSize);

	loadUpdateMemStatus();

	FP_NO_EXCEPTIONS_BEGIN;
	// TODO: Specify paths for normal map and specular map (and emissive?)
	simpleResult = simplygon_doRemesh(scene, resolution, textureHeight, textureWidth, maxPixelDeviation, sgMatTable,
		image_prefix ? simplygon_imagename_D : NULL,
		image_prefix ? simplygon_imagename_N : NULL,
		image_prefix ? simplygon_imagename_S : NULL,
		&sgRemeshStats, &remeshSettings);
	FP_NO_EXCEPTIONS_END;

	if (remeshStats)
	{
		remeshStats->remeshGeo.totalTaskTime.totalTimeAccum = sgRemeshStats.timeRemesh;
		remeshStats->eaCastTextures[CLUSTER_DIFFUSE_TEXTURE_CHANNEL]->totalTaskTime.totalTimeAccum = sgRemeshStats.timeCastDiffuse;
		remeshStats->eaCastTextures[CLUSTER_SPECULAR_TEXTURE_CHANNEL]->totalTaskTime.totalTimeAccum = sgRemeshStats.timeCastSpecular;
		remeshStats->eaCastTextures[CLUSTER_NORMALMAP_TEXTURE_CHANNEL]->totalTaskTime.totalTimeAccum = sgRemeshStats.timeCastNormal;
	}

	eaDestroyEx(&nodesToCleanUp, simplygon_destroySceneNode);

	simplygon_destroyMaterialTable(sgMatTable);
	simplygon_destroyScene(scene);

	loadUpdateMemStatus();
	loadend_printf(" done.");

	return simpleResult;
}

void exportGmeshToObj(GMesh *gmesh, char *fileoutName) {
	SimplygonMesh *sgmesh;
	bool oversized;

	sgmesh = simplygonMeshFromGMesh(gmesh,NULL,0,&oversized,fileoutName, ~0);

	simplygon_obj_export(sgmesh, fileoutName);
	simplygon_destroyMesh(sgmesh);
}

void processModelClusterToObj(ModelClusterSource *modelCluster, const char* fileoutName) {
	SimplygonMesh *sgmesh;
	
	sgmesh = processModelCluster(modelCluster, modelCluster->cluster_volumes_condensed->geo_resolution,
		modelCluster->cluster_volumes_condensed->texture_height * modelCluster->cluster_volumes_condensed->texture_super_sample,
		modelCluster->cluster_volumes_condensed->texture_width * modelCluster->cluster_volumes_condensed->texture_super_sample,
		SIMPLYGON_DEFAULT_MAX_PIXEL_DEVIATION, "c:\\testModels", NULL, NULL);

	simplygon_obj_export(sgmesh, fileoutName);
	simplygon_destroyMesh(sgmesh);
}

int remeshModels(ModelClusterSource *modelCluster, GMesh * gmesh, const char * image_tempdir, const char* image_prefix, WorldClusterStats * remeshStats) {
	SimplygonMesh *sgmesh;
	GMeshAttributeUsage exclusionFlags = USE_EMPTY;

	assert(modelCluster);

	sgmesh = processModelCluster(modelCluster, modelCluster->cluster_volumes_condensed->geo_resolution,
		modelCluster->cluster_volumes_condensed->texture_height * modelCluster->cluster_volumes_condensed->texture_super_sample,
		modelCluster->cluster_volumes_condensed->texture_width * modelCluster->cluster_volumes_condensed->texture_super_sample,
		SIMPLYGON_DEFAULT_MAX_PIXEL_DEVIATION, image_tempdir, image_prefix, remeshStats);

	if (!sgmesh)
	{
		log_printf(LOG_ERRORS, "Error: remeshing model cluster failed.\n");
		return 0;
	}

	if (!modelCluster->cluster_volumes_condensed->include_normal)
		exclusionFlags |= USE_TANGENTS | USE_BINORMALS;

	// We could mask out the bone weights on the output, but I'd rather have verification that no bone weights are even
	// making it into the input. If it hits this assert, some bone weights are making it in. Check the usage bit masks
	// for the inputs for this operation. -Cliff
	simplygonMeshToGMesh(sgmesh,gmesh, ~exclusionFlags);
	assert(!(gmesh->usagebits & USE_BONEWEIGHTS));

	// adding tangent basis only if SG did not return one
	if (gmesh && modelCluster->cluster_volumes_condensed->include_normal)
	{
		GMeshTangentSpaceState tangent_space_state = gmeshGetTangentBasisAttributeState(gmesh);
		if (tangent_space_state == TANGENT_SPACE_INCOMPLETE)
			log_printf(LOG_ERRORS, "Warning: SG returned partial tangent space attributes\n");
		if (tangent_space_state != TANGENT_SPACE_COMPLETE)
			gmeshAddTangentSpace(gmesh, false, "WorldCluster.msh", LOG_ERRORS);
	}
	simplygon_destroyMesh(sgmesh);
	simplygon_assertFreedAll();

	++simplygonRemeshCount;

	if ((simplygonAutoShutdownCount > 0) && (simplygonRemeshCount >= simplygonAutoShutdownCount))
	{
		simplygonRemeshCount = 0;
		simplygon_shutdown();
	}

	loadUpdateMemStatus();

	return 1;
}

#include "GenericMeshRemesh_h_ast.c"
