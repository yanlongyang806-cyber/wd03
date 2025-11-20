#include "AutoLOD.h"
#include "wlAutoLOD.h"
#include "WorldLib.h"
#include "wlModel.h"
#include "wlModelLoad.h"
#include "wlModelBinning.h"
#include "wlModelInline.h"
#include "group.h"
#include "bounds.h"

#include "fileutil.h"
#include "StringCache.h"
#include "timing.h"
#include "FolderCache.h"
#include "ResourceInfo.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc););


AUTO_STRUCT AST_ENDTOK("\n");
typedef struct LodScale
{
	char *substring;		AST( STRUCTPARAM )
	F32 scale;				AST( STRUCTPARAM )
} LodScale;

AUTO_STRUCT;
typedef struct LodScaleArray
{
	LodScale **scales;		AST( NAME( LodScale) )
} LodScaleArray;



#include "wlAutoLOD_h_ast.c"
#include "AutoLOD_c_ast.h"

AutoLOD *allocAutoLOD(void)
{
	return StructCreate(parse_AutoLOD);
}

void freeModelLODInfoData(ModelLODInfo *info)
{
	// Note, some of this code duplicated in wlModelBinning for threading reasons (no ref system)
	if (!info)
		return;
	REMOVE_HANDLE(info->lod_template);
	eaDestroyStruct(&info->lods, parse_AutoLOD);
	ZeroStruct(info);
}

AutoLODTemplate *allocAutoLODTemplate(void)
{
	return StructCreate(parse_AutoLODTemplate);
}

static AutoLODTemplate *dupAutoLODTemplate(AutoLODTemplate *lod_template)
{
	AutoLODTemplate *new_template = allocAutoLODTemplate();
	CopyStructs(new_template, lod_template, 1);
	return new_template;
}

static void freeAutoLODTemplate(AutoLODTemplate *lod_template)
{
	StructDestroy(parse_AutoLODTemplate, lod_template);
}

void freeModelLODTemplateData(ModelLODTemplate *t)
{
	if (!t)
		return;
	eaDestroyStruct(&t->lods, parse_AutoLODTemplate);
	ZeroStruct(t);
}


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static LODInfos all_lodinfos;
static StashTable lod_hash;
static char **lodinfo_reloads;
static char **lodtemplate_reloads;
static DictionaryHandle lod_template_dict;
static LodScaleArray all_lodscales;

static void reloadLODInfoProcessor(const char *relpath, int when)
{
	eaPush(&lodinfo_reloads, strdup(relpath));
}

static void reloadLODTemplateProcessor(const char *relpath, int when)
{
	eaPush(&lodtemplate_reloads, strdup(relpath));
}

static const char *getHashedNameFromFileAndModel(const char *filename, const char *modelname);

static LodReloadCallback lodReloadCallback = 0;

void lodinfoSetReloadCallback(LodReloadCallback callback)
{
	lodReloadCallback = callback;
}

static void fillInLODFilenames(ModelLODInfo *lodinfo)
{
	int i;
	for (i = 0; i < eaSize(&lodinfo->lods); i++)
	{
		AutoLOD *lod = lodinfo->lods[i];
		lod->modelname_specified = !!lod->lod_modelname;
// 		if (!lod->lod_filename && lod->lod_modelname)
// 		{
// 			lod->lod_filename = objectLibraryPathFromObjName(lod->lod_modelname);
// 			if (lod->lod_filename)
// 				lod->lod_filename = allocAddFilename(lod->lod_filename);
// 		}
		if (lod->modelname_specified) {
			if (!wlModelHeaderFromNameEx(lod->lod_filename, lod->lod_modelname))
			{
				if (lod->lod_filename)
					ErrorFilenameGroupRetroactivef(lodinfo->parsed_filename, "Art", 14, 8, 5, 2008, "LOD for %s references unknown model: %s / %s", lodinfo->modelname, lod->lod_filename, lod->lod_modelname);
				else
					ErrorFilenameGroupRetroactivef(lodinfo->parsed_filename, "Art", 14, 8, 5, 2008, "LOD for %s references unknown model: %s", lodinfo->modelname, lod->lod_modelname);
			} else {
				//  exists, verify it's sensical
				const char *hashed_name = getHashedNameFromFileAndModel(lod->lod_filename, lod->lod_modelname);
				ModelLODInfo *lod_lodinfo;
				if (stashFindPointer(lod_hash, hashed_name, &lod_lodinfo))
				{
					if (eaSize(&lod_lodinfo->lods) && lod_lodinfo->lods[0]->flags == LOD_ERROR_NULL_MODEL)
					{
						ErrorFilenameGroupRetroactivef(lodinfo->parsed_filename, "Art", 14, 2, 11, 2011, "LOD for %s references model %s / %s which is a Null LOD.  This might work, but it's probably not what you want.", lodinfo->modelname, lod->lod_filename, lod->lod_modelname);
					}
				}
			}
		}

		lod->null_model = (lod->flags == LOD_ERROR_NULL_MODEL);
		// TODO, does do_remesh need to be filled in here, or is it already filled in?
	}
}

static const char *getHashedNameFromFileAndModel(const char *filename, const char *modelname)
{
	char buf[MAX_PATH];
	if (0)
	{
		char fname[MAX_PATH];
		getFileNameNoExt(fname, filename);
		sprintf(buf, "%s/%s", fname, modelname);
	}
	else
		strcpy(buf, modelname); // Model names should be unique, and filename shouldn't change (at least in produciton mode), and using the same strings saves a 150k of memory, 400k vs full paths
	//devassert(allocFindString(buf));
	return allocAddString(buf);
}

static const char *getHashedNameFromLODInfo(ModelLODInfo *lodinfo)
{
	return getHashedNameFromFileAndModel(lodinfo->parsed_filename, lodinfo->modelname);
}


static void reloadLODInfo(const char *relpath)
{
	LODInfos temp_infos={0};
	int i;

	relpath = allocAddFilename(relpath);

	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&all_lodinfos.infos); i++)
	{
		ModelLODInfo *lodinfo = all_lodinfos.infos[i];
		if ((!(lodinfo->is_automatic || lodinfo->is_no_lod) || (lodinfo->is_automatic && lodinfo->force_auto)) && relpath == lodinfo->parsed_filename)
		{
			lodinfo->removed = 1;
		}
	}

	if (!ParserLoadFiles(NULL, (char *)relpath, NULL, PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_LODInfos, &temp_infos))
	{
		ErrorFilenamef(relpath, "Error reloading LOD file: %s\n", relpath);
		wlStatusPrintf("Error reloading LOD file: %s.", relpath);
	}
	else
	{
		for (i = 0; i < eaSize(&temp_infos.infos); ++i)
		{
			ModelLODInfo *old_info, *lodinfo = temp_infos.infos[i];
			const char *hashed_name = getHashedNameFromLODInfo(lodinfo);
			fillInLODFilenames(lodinfo);
			if (stashFindPointer(lod_hash, hashed_name, &old_info))
			{
				eaDestroyStruct(&old_info->lods, parse_AutoLOD);
				if (lodinfo->lods)
				{
					eaCopy(&old_info->lods, &lodinfo->lods);
					eaDestroy(&lodinfo->lods);
				}
				old_info->removed = 0;
				old_info->is_automatic = lodinfo->is_automatic;
				old_info->is_no_lod = lodinfo->is_no_lod;
				old_info->force_auto = lodinfo->force_auto;
				old_info->high_detail_high_lod = lodinfo->high_detail_high_lod;
				old_info->prevent_clustering = lodinfo->prevent_clustering;
				lodinfoSetTemplate(old_info, (IS_HANDLE_ACTIVE(lodinfo->lod_template))?(REF_STRING_FROM_HANDLE(lodinfo->lod_template)):NULL);
				freeModelLODInfoData(lodinfo);
			}
			else
			{
				lodinfo->removed = 0;
				stashAddPointer(lod_hash, hashed_name, lodinfo, false);
				eaPush(&all_lodinfos.infos, lodinfo);
			}
		}

		eaDestroy(&temp_infos.infos);

		if (lodReloadCallback)
			lodReloadCallback(relpath);
	}

	PERFINFO_AUTO_STOP();
}

static void reloadLODTemplate(const char *relpath)
{
	ParserReloadFileToDictionary(relpath, lod_template_dict);

	if (lodReloadCallback)
		lodReloadCallback(relpath);
}

static int num_lod_reloads = 0;
void checkLODInfoReload(void)
{
	int need_reload=0;
	char *s;
	while (s = eaPop(&lodinfo_reloads)) {
		if (!eaSize(&lodinfo_reloads) || stricmp(s, lodinfo_reloads[eaSize(&lodinfo_reloads)-1])!=0) {
			// Not the same as the next one
			reloadLODInfo(s);
			need_reload = 1;
		}
		free(s);
	}

	while (s = eaPop(&lodtemplate_reloads)) {
		if (!eaSize(&lodtemplate_reloads) || stricmp(s, lodtemplate_reloads[eaSize(&lodtemplate_reloads)-1])!=0) {
			// Not the same as the next one
			reloadLODTemplate(s);
			need_reload = 1;
		}
		free(s);
	}

	if (need_reload)
	{
		num_lod_reloads++;
		lodinfoReloadPostProcess();
		wlStatusPrintf("LODs reloaded.");
	}
}

int getNumLODReloads(void)
{
	return num_lod_reloads;
}

void lodinfoStartup(void)
{
	lod_template_dict = RefSystem_RegisterSelfDefiningDictionary(LOD_TEMPLATE_DICTIONARY, false, parse_ModelLODTemplate, true, true, NULL);
	lod_hash = stashTableCreateWithStringKeys(4096,StashDefault);
}

void lodinfoLoad(void)
{
	loadstart_printf("Loading LOD infos...");

	lodinfoStartup();

	ParserLoadFilesToDictionary("environment/LodTemplates", ".lodtemplate", "lodtemplates.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, lod_template_dict);

	ParserLoadFiles("object_library;character_library", ".lodinfo", "lods.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_LODInfos, &all_lodinfos);

	ParserLoadFiles(NULL, CHARLIB_LOD_SCALE_FILENAME, "charlodscale.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_LodScaleArray, &all_lodscales);

	lodinfoLoadPostProcess();

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "object_library/*.lodinfo", reloadLODInfoProcessor);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "character_library/*.lodinfo", reloadLODInfoProcessor);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "environment/LodTemplates/*.lodtemplate", reloadLODTemplateProcessor);

	loadend_printf(" done (%d LOD infos, %d LOD templates, %d char LOD scales).", eaSize(&all_lodinfos.infos), RefSystem_GetDictionaryNumberOfReferents(lod_template_dict), eaSize(&all_lodscales.scales));
}

void lodinfoVerify(void)
{
	int i;
	for (i = 0; i < eaSize(&all_lodinfos.infos); i++)
	{
		ModelLODInfo *lodinfo = all_lodinfos.infos[i];
		fillInLODFilenames(lodinfo);
	}
}

void lodinfoLoadPostProcess(void)
{
	int i;
	
	for (i = 0; i < eaSize(&all_lodinfos.infos); i++)
	{
		ModelLODInfo *lodinfo = all_lodinfos.infos[i];
		const char *hashed_name = getHashedNameFromLODInfo(lodinfo);
		if (!stashAddPointer(lod_hash, hashed_name, lodinfo, false))
		{
			ModelLODInfo *dup_lodinfo = NULL;
			stashFindPointer(lod_hash, hashed_name, &dup_lodinfo );
			ErrorFilenameDup(lodinfo->parsed_filename, dup_lodinfo->parsed_filename, hashed_name, "LOD Info");
		}
	}
}

void lodinfoReloadPostProcess(void)
{
}


//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

void lodinfoSetTemplate(ModelLODInfo *info, const char *template_name)
{
	REMOVE_HANDLE(info->lod_template);
	if (template_name && template_name[0])
		SET_HANDLE_FROM_STRING(lod_template_dict, template_name, info->lod_template);
}

int lodsDifferent(ModelLODInfo *info1, ModelLODInfo *info2)
{
	int i, j, k;
	int auto1 = info1->force_auto || info1->is_automatic;
	int auto2 = info2->force_auto || info2->is_automatic;
	ModelLODTemplate *temp1 = GET_REF(info1->lod_template);
	ModelLODTemplate *temp2 = GET_REF(info2->lod_template);

	assertmsg(stricmp(info1->modelname, info2->modelname)==0, "lodsDifferent is for comparing lodinfos for the same model!");

	if (auto1 != auto2)
		return 1;

	if (auto1 && auto2)
		return 0; // both automatic, they are the same

	if (temp1 != temp2)
		return 1;

	if (info1->high_detail_high_lod != info2->high_detail_high_lod)
		return 1;

	if (info1->prevent_clustering != info2->prevent_clustering)
		return 1;

	if (eaSize(&info1->lods) != eaSize(&info2->lods))
		return 1;

	for (i = 0; i < eaSize(&info1->lods); ++i)
	{
		AutoLOD *lod1 = info1->lods[i];
		AutoLOD *lod2 = info2->lods[i];

		if (lod1->use_fallback_materials != lod2->use_fallback_materials)
			return 1;
		if (!temp1 && !nearSameF32(lod1->lod_near, lod2->lod_near))
			return 1;
		if (!temp1 && !nearSameF32(lod1->lod_far, lod2->lod_far))
			return 1;
		if (!nearSameF32(lod1->lod_farmorph, lod2->lod_farmorph))
			return 1;
		if (!nearSameF32(lod1->max_error, lod2->max_error))
			return 1;
		if (!nearSameF32(lod1->upscale_amount, lod2->upscale_amount))
			return 1;
		if (lod1->flags != lod2->flags)
			return 1;
		if (lod1->modelname_specified != lod2->modelname_specified)
			return 1;
		if (lod1->null_model != lod2->null_model)
			return 1;
		if (lod1->do_remesh != lod2->do_remesh)
			return 1;
		if (lod1->modelname_specified && (stricmp(lod1->lod_modelname, lod2->lod_modelname) != 0 || stricmp(lod1->lod_filename, lod2->lod_filename) != 0))
			return 1;
		if (eaSize(&lod1->material_swaps) != eaSize(&lod2->material_swaps))
			return 1;
		if (eaSize(&lod1->texture_swaps) != eaSize(&lod2->texture_swaps))
			return 1;
		for (j = 0; j < eaSize(&lod1->material_swaps); ++j)
		{
			for (k = 0; k < eaSize(&lod2->material_swaps); ++k)
			{
				if (stricmp(lod1->material_swaps[j]->orig_name, lod2->material_swaps[k]->orig_name) == 0)
					break;
			}
			if (k == eaSize(&lod2->material_swaps))
				return 1; // no matching material swap
			if (stricmp(lod1->material_swaps[j]->replace_name, lod2->material_swaps[k]->replace_name) != 0)
				return 1; // matching material swap has different replace material
		}
		for (j = 0; j < eaSize(&lod1->texture_swaps); ++j)
		{
			for (k = 0; k < eaSize(&lod2->texture_swaps); ++k)
			{
				if (stricmp(lod1->texture_swaps[j]->orig_name, lod2->texture_swaps[k]->orig_name) == 0)
					break;
			}
			if (k == eaSize(&lod2->texture_swaps))
				return 1; // no matching texture swap
			if (stricmp(lod1->texture_swaps[j]->replace_name, lod2->texture_swaps[k]->replace_name) != 0)
				return 1; // matching texture swap has different replace texture
		}
	}

	// same!
	return 0;
}

F32 loddistFromLODInfo(Model *model, ModelLODInfo *lod_info, AutoLODTemplate **lod_override_distances, F32 lod_scale, const Vec3 model_scale, bool use_buffer, F32 *near_lod_near_dist, F32 *far_lod_near_dist)
{
	if (model && !lod_info)
		lod_info = model->lod_info;
	if (lod_info && eaSize(&lod_info->lods))
	{
		ModelLODTemplate *lod_template = GET_REF(lod_info->lod_template);
		F32 model_radius = SAFE_MEMBER(model, radius);
		
		if (model_scale && model && !sameVec3(model_scale, unitvec3))
		{
			Vec3 model_min, model_max, model_mid;
			mulVecVec3(model->min, model_scale, model_min);
			mulVecVec3(model->max, model_scale, model_max);
			model_radius = boxCalcMid(model_min, model_max, model_mid);
		}

		// if there is a model scale applied, find the best lod template for the scaled radius
		if (!lod_override_distances && model && !nearSameF32Tol(model_radius, model->radius, 0.25f))
			lod_template = lodinfoGetTemplateForRadius(lod_info, model_radius);

		if (!lod_override_distances && lod_template)
			lod_override_distances = lod_template->lods;

		if (lod_override_distances)
		{
			int last = eaSize(&lod_override_distances) - 1;
			F32 far_lod_far_dist = lod_override_distances[last]->lod_far * lod_scale + ((use_buffer && !lod_override_distances[last]->no_fade)?FADE_DIST_BUFFER:0);

			if (near_lod_near_dist)
			{
				int first;
				for (first = 0; first < last && first < eaSize(&lod_info->lods); ++first)
				{
					if (!lod_info->lods[first]->null_model)
						break;
				}
				*near_lod_near_dist = lod_override_distances[first]->lod_near * lod_scale - ((use_buffer && !lod_override_distances[first]->no_fade)?FADE_DIST_BUFFER:0);
				MAX1(*near_lod_near_dist, 0);
			}

			if (far_lod_near_dist)
				*far_lod_near_dist = lod_override_distances[last]->lod_near * lod_scale;

			return far_lod_far_dist;
		}
		else
		{
			int last = eaSize(&lod_info->lods) - 1;
			F32 far_lod_far_dist = lod_info->lods[last]->lod_far * lod_scale + ((use_buffer && !lod_info->lods[last]->no_fade)?FADE_DIST_BUFFER:0);

			if (near_lod_near_dist)
			{
				int first;
				for (first = 0; first < last; ++first)
				{
					if (!lod_info->lods[first]->null_model)
						break;
				}
				*near_lod_near_dist = lod_info->lods[first]->lod_near * lod_scale - ((use_buffer && !lod_info->lods[first]->no_fade)?FADE_DIST_BUFFER:0);
				MAX1(*near_lod_near_dist, 0);
			}

			if (far_lod_near_dist)
				*far_lod_near_dist = lod_info->lods[last]->lod_near * lod_scale;

			return far_lod_far_dist;
		}
	}

	if (near_lod_near_dist)
		*near_lod_near_dist = 0;
	if (far_lod_near_dist)
		*far_lod_near_dist = 0;

	return 0;
}

bool lodinfoGetDistances(Model *model, AutoLODTemplate **lod_override_distances, F32 lod_scale, const Vec3 model_scale, int i, F32 *lod_near, F32 *lod_far)
{
	ModelLODInfo *lod_info = model->lod_info;
	ModelLODTemplate *lod_template = GET_REF(lod_info->lod_template);
	bool no_fade = false;
	F32 model_radius = model->radius;

	if (model_scale && !sameVec3(model_scale, unitvec3))
	{
		Vec3 model_min, model_max, model_mid;
		mulVecVec3(model->min, model_scale, model_min);
		mulVecVec3(model->max, model_scale, model_max);
		model_radius = boxCalcMid(model_min, model_max, model_mid);
	}

	// if there is a model scale applied, find the best lod template for the scaled radius
	if(!gConf.bDontOverrideLODOnScaledUnlessUsingLODTemplate)
	{
		// JE: Maybe only do this if we're already using an lod_template, this messes up objects with
		//   custom LODs that are scaled slightly
		//   But, we can't change this now since lots of maps already have large lod_scales set
		//   in the world editor to compensate for this.
		if (!lod_override_distances && !nearSameF32Tol(model_radius, model->radius, 0.25f))
			lod_template = lodinfoGetTemplateForRadius(lod_info, model_radius);
	}
	else
	{
		if (lod_template && !lod_override_distances && !nearSameF32Tol(model_radius, model->radius, 0.25f))
			lod_template = lodinfoGetTemplateForRadius(lod_info, model_radius);
	}

	if (!lod_override_distances && lod_template)
		lod_override_distances = lod_template->lods;

	if (lod_override_distances)
	{
		int max_template_lod_idx = eaSize(&lod_override_distances) - 1;
		int num_lods = eaSize(&lod_info->lods);
		if (i >= max_template_lod_idx)
		{
			int delta = i - max_template_lod_idx;
			F32 deltadist = (lod_override_distances[max_template_lod_idx]->lod_far - lod_override_distances[max_template_lod_idx]->lod_near) / (num_lods - max_template_lod_idx);
			*lod_near = lod_scale * (lod_override_distances[max_template_lod_idx]->lod_near + delta * deltadist);
			*lod_far = *lod_near + lod_scale * deltadist;
			no_fade = lod_override_distances[max_template_lod_idx]->no_fade;
		}
		else
		{
			if (num_lods <= max_template_lod_idx && i >= num_lods-1)
			{
				*lod_near = lod_scale * lod_override_distances[num_lods-1]->lod_near;
				*lod_far = lod_scale * lod_override_distances[max_template_lod_idx]->lod_far;
				no_fade = lod_override_distances[max_template_lod_idx]->no_fade;
			}
			else
			{
				*lod_near = lod_scale * lod_override_distances[i]->lod_near;
				*lod_far = lod_scale * lod_override_distances[i]->lod_far;
				no_fade = lod_override_distances[i]->no_fade;
			}
		}
	}
	else
	{
		*lod_near = lod_scale * lod_info->lods[i]->lod_near;
		*lod_far = lod_scale * lod_info->lods[i]->lod_far;
		no_fade = lod_info->lods[i]->no_fade;
	}

	if (*lod_far < *lod_near + 10)
		*lod_far = *lod_near + 10;

	return !!no_fade;
}

static void getAutoLodModelName(const char *modelname, int lod_num, char *buf, int buf_size, bool is_char_lib)
{
	int len;

	buf[0] = 0;

	if (!modelname || !modelname[0]) {
		assert(0);
		return;
	}

	len = (int)strlen(modelname);
	if (strEndsWith(modelname, "_L0"))
		len -= 3;

	if (lod_num && modelname[0] != '_' && !is_char_lib)
		strcat_s(SAFESTR2(buf), "_");
	strncat_s(SAFESTR2(buf), modelname, len);
	strcatf_s(SAFESTR2(buf), "_L%d", lod_num);
}

const char *getLODFileName(const char *geo_fname, bool full_path)
{
	static char dirbuf[MAX_PATH];
	if (full_path) {
		fileLocateWrite(geo_fname, dirbuf);
		changeFileExt(dirbuf, ".lodinfo", dirbuf);
	} else {
		changeFileExt(geo_fname, ".lodinfo", dirbuf);
	}
	return dirbuf;
}

void lodGetTemplateNames(char ***name_array)
{
	DictionaryEArrayStruct *arraystruct = resDictGetEArrayStruct(lod_template_dict);
	int i;

	for (i = 0; i < eaSize(&arraystruct->ppReferents); ++i)
	{
		ModelLODTemplate *lod_template = arraystruct->ppReferents[i];
		eaPush(name_array, strdup(lod_template->template_name));
	}
}

ModelLODTemplate *lodinfoGetTemplateForRadius(const ModelLODInfo *lod_info, F32 radius)
{
	DictionaryEArrayStruct *arraystruct = resDictGetEArrayStruct(lod_template_dict);
	ModelLODTemplate *best_template = NULL;
	int i;

	for (i = 0; i < eaSize(&arraystruct->ppReferents); ++i)
	{
		ModelLODTemplate *lod_template = arraystruct->ppReferents[i];
		if (radius < lod_template->default_radius)
		{
			if (!best_template)
			{
				best_template = lod_template;
			}
			else
			{
				// check if radius is closer
				if (lod_template->default_radius < best_template->default_radius)
				{
					best_template = lod_template;
				}
				else if (lod_template->default_radius == best_template->default_radius)
				{
					// check if lod count is closer
					int prevdiff = eaSize(&lod_info->lods) - eaSize(&best_template->lods);
					int diff = eaSize(&lod_info->lods) - eaSize(&lod_template->lods);
					if (prevdiff > 0)
					{
						if (diff < prevdiff)
							best_template = lod_template;
					}
					else if (abs(diff) < abs(prevdiff))
					{
						best_template = lod_template;
					}
				}
			}
		}
	}

	if (!best_template)
	{
		// didn't find a template with a larger radius than we are looking for, 
		// so find the one with the biggest radius
		for (i = 0; i < eaSize(&arraystruct->ppReferents); ++i)
		{
			ModelLODTemplate *lod_template = arraystruct->ppReferents[i];
			if (!best_template)
			{
				best_template = lod_template;
			}
			else
			{
				// check if radius is closer
				if (lod_template->default_radius > best_template->default_radius)
				{
					best_template = lod_template;
				}
				else if (lod_template->default_radius == best_template->default_radius)
				{
					// check if lod count is closer
					int prevdiff = eaSize(&lod_info->lods) - eaSize(&best_template->lods);
					int diff = eaSize(&lod_info->lods) - eaSize(&lod_template->lods);
					if (prevdiff > 0)
					{
						if (diff < prevdiff)
							best_template = lod_template;
					}
					else if (abs(diff) < abs(prevdiff))
					{
						best_template = lod_template;
					}
				}
			}
		}
	}

	return best_template;
}

#define HIGH_POLY_CUTOFF2 2000
#define LOW_LOD_CUTOFF 200
#define DIST_OFFSET 75

static void setAutoDist(AutoLOD *prev_lod, AutoLOD *lod, int err_dist)
{
	if (err_dist < prev_lod->lod_near)
		err_dist = prev_lod->lod_near + 10;

	lod->lod_far = prev_lod->lod_far;
	prev_lod->lod_far = lod->lod_near = err_dist;
}

// Only ran at run-time, not bin-time
static void setAutoFarmorph(ModelLODInfo *lod_info)
{
	int d;
	for (d = 1; d < eaSize(&lod_info->lods); ++d)
	{
		char lodmodelname[MAX_PATH];
		AutoLOD *lod = lod_info->lods[d];
		ModelHeader *lodmodelheader;

		getAutoLodModelName(lod_info->modelname, d, SAFESTR(lodmodelname), false);
		lodmodelheader = wlModelHeaderFromName(lodmodelname);
		if (lodmodelheader && lodmodelheader->has_verts2)
		{
			// from handbuilt
			lod->lod_farmorph = 15;
		}
	}
}

static F32 tricount_error_for_lod[] = 
{
	50.f,
	80.f,
	90.f
};

#define MAX_LOD_COUNT	(ARRAY_SIZE(tricount_error_for_lod))
#define LOD_COUNT		(MAX_LOD_COUNT - 1)
#define CHAR_LOD_COUNT	1

static F32 getFarDistanceForLOD(F32 radius, int lod, int lod_count)
{
	return 10.f * (radius * 0.75f + 5.f) * ((lod)/((F32)(lod_count+1)));
}

void lodinfoFillInAutoData(ModelLODInfo *lod_info, bool no_lod, const char *geoFilename, int source_tri_count, 
						   F32 radius, bool processed_high_detail_high_lod, ExistsCallback existsCallback, void *userData, 
						   bool has_verts2, bool bUseDictionaries)
{
	int tri_count = source_tri_count;
	int d, handbuilt_count = 0;
	char lodmodelname[MAX_PATH];
	bool is_char_model = strStartsWith(geoFilename, "character_library/");
	ModelLODTemplate *best_template;
	int lod_count = is_char_model ? CHAR_LOD_COUNT : LOD_COUNT;

	assert(!eaSize(&lod_info->lods));

	// automatic LOD values
	lod_info->is_automatic = 1;

	lod_info->high_detail_high_lod = processed_high_detail_high_lod;

	REMOVE_HANDLE(lod_info->lod_template);

	eaPush(&lod_info->lods, allocAutoLOD());
	lod_info->lods[0]->flags = LOD_ERROR_TRICOUNT;
	lod_info->lods[0]->max_error = 0;
	lod_info->lods[0]->upscale_amount = 0;
	lod_info->lods[0]->lod_near = 0;
	if (is_char_model)
		lod_info->lods[0]->lod_far = 500.f;
	else
		lod_info->lods[0]->lod_far = getFarDistanceForLOD(radius, lod_count+1, lod_count);

	if (has_verts2)
	{
		lod_info->lods[0]->lod_farmorph = 15;
		handbuilt_count = 1; // don't automatically use reductions
	}

	if (no_lod)
		return;

	// check for handbuilt lods
	for (d = 1; d < (is_char_model ? 4 : (lod_count+1)); ++d)
	{
		getAutoLodModelName(lod_info->modelname, d, SAFESTR(lodmodelname), is_char_model);
		if (existsCallback(userData, lodmodelname))
			handbuilt_count = d+1;
	}

	if (handbuilt_count)
	{
		int h;
		for (h = 1; h < handbuilt_count; ++h)
		{
			AutoLOD *prev_lod = lod_info->lods[h-1];
			bool handbuilt_lod_exists;

			d = h-1;

			assert(eaSize(&lod_info->lods) == h);

			getAutoLodModelName(lod_info->modelname, h, SAFESTR(lodmodelname), is_char_model);
			handbuilt_lod_exists = existsCallback(userData, lodmodelname);
			if (handbuilt_lod_exists)
			{
				// from handbuilt
				AutoLOD *lod = allocAutoLOD();
				eaPush(&lod_info->lods, lod);

				lod->modelname_specified = 1;
				lod->lod_modelname = allocAddString(lodmodelname);
				lod->lod_filename = allocAddFilename(geoFilename);

				setAutoDist(prev_lod, lod, (d+3)*radius);
			}
			else
			{
				// from reductions
				F32 err_dist = getFarDistanceForLOD(radius, h, MAX(lod_count, handbuilt_count));
				AutoLOD *lod = allocAutoLOD();
				eaPush(&lod_info->lods, lod);

				// TODO this is kinda crappy

				lod->flags = LOD_ERROR_TRICOUNT;
				lod->max_error = tricount_error_for_lod[MIN(d,MAX_LOD_COUNT)];

				setAutoDist(prev_lod, lod, err_dist);
			}
		}
	}
	else
	{
		F32 upscale_amount = 0;

		if (is_char_model)
		{
			for (d = 0; d < eaSize(&all_lodscales.scales); ++d)
			{
				if (strstri(geoFilename, all_lodscales.scales[d]->substring))
				{
					upscale_amount = all_lodscales.scales[d]->scale;
					break;
				}
			}
		}

		// from reductions
		for (d = 0; d < lod_count && (tri_count > LOW_LOD_CUTOFF || upscale_amount); ++d)
		{
			F32 err_dist = getFarDistanceForLOD(radius, d+1, lod_count);
			AutoLOD *prev_lod = lod_info->lods[eaSize(&lod_info->lods)-1];
			F32 tricount_error = 1 - (tricount_error_for_lod[d] * 0.01f);

			tri_count = (int)(tricount_error * source_tri_count);

			if (is_char_model)
				err_dist = prev_lod->lod_near + DIST_OFFSET;

			if (!upscale_amount && (err_dist > (prev_lod->lod_far - DIST_OFFSET) || err_dist < 0))
			{
				// the reduction is not worth it
				break;
			}
			else if (err_dist >= (prev_lod->lod_near + DIST_OFFSET) && (tricount_error <= 0.5f || tri_count <= HIGH_POLY_CUTOFF2))
			{
				AutoLOD *lod = allocAutoLOD();
				eaPush(&lod_info->lods, lod);

				lod->flags = LOD_ERROR_TRICOUNT;
				lod->max_error = tricount_error_for_lod[d];
				lod->upscale_amount = upscale_amount;

				setAutoDist(prev_lod, lod, err_dist);
			}
			else if (upscale_amount)
			{
				AutoLOD *lod = allocAutoLOD();
				eaPush(&lod_info->lods, lod);

				lod->flags = LOD_ERROR_TRICOUNT;
				lod->max_error = tricount_error_for_lod[d] * 0.5f;
				lod->upscale_amount = upscale_amount;

				setAutoDist(prev_lod, lod, err_dist);
			}
		}

		// TODO REMESH LOD
		//  figure out how to how to squeeze remesh into the LOD template system
	}

	lod_info->prevent_clustering = 0;
	if (bUseDictionaries)
	{
		best_template = lodinfoGetTemplateForRadius(lod_info, radius);
		if (best_template)
		{
			SET_HANDLE_FROM_REFERENT(lod_template_dict, best_template, lod_info->lod_template);
			lod_info->prevent_clustering = best_template->prevent_clustering;
		}
	}
}

// model->name, model->filename, model->radius, model->tri_count
static ModelLODInfo *lodinfoFromModelInternal(const char *geoFilename, const char *modelName, bool has_verts2, int tri_count, F32 radius, bool processed_high_detail_high_lod, ExistsCallback existsCallback, void *userData, bool bUseDictionaries)
{
	bool no_lod = false, exists;
	ModelLODInfo *lod_info = NULL;
	const char *hashed_name;
	char outputname[MAX_PATH];

	strcpy(outputname, getLODFileName(geoFilename, false));
	hashed_name = getHashedNameFromFileAndModel(geoFilename, modelName);

	exists = stashFindPointer(lod_hash, hashed_name, &lod_info);
	if (exists && !lod_info->removed)
	{
		if (!(lod_info->force_auto && !lod_info->lods)) {
			lod_info->is_in_dictionary = 1;
			return lod_info;
		}
	}

	if (lod_info && lod_info->removed && bUseDictionaries)
	{
		freeModelLODInfoData(lod_info);
		lod_info->modelname = allocAddString(modelName);
		lod_info->parsed_filename = allocAddFilename(outputname);
	}
	else if (!lod_info || !bUseDictionaries)
	{
		// Either it's a new one, or it's an existing one, but it wants to use AutoLOD stuff
		if (bUseDictionaries)
			lod_info = StructCreate(parse_ModelLODInfo);
		else
			lod_info = calloc(sizeof(ModelLODInfo), 1); // Can't call StructDestroy in a thread later
		lod_info->modelname = allocAddString(modelName);
		lod_info->parsed_filename = allocAddFilename(outputname);
	}

	{
		char basename[MAX_PATH], buf[MAX_PATH];
		char *bs, *fs, *s;
		int i;

		fs = strrchr(geoFilename, '/'); 
		bs = strrchr(geoFilename, '\\');
		s = fs > bs ? fs : bs;
		if(s)
			strcpy(basename, s + 1);
		else
			strcpy(basename, geoFilename);
		s = strrchr(basename,'.'); 
		if (s)
			*s = 0;

		// capes can't be LODed
		no_lod = !!strEndsWith(basename, "_cape");

		// don't LOD handbuilt LODs
		for (i = 1; i < MAX_LOD_COUNT; ++i)
		{
			if (no_lod)
				break;

			sprintf(buf, "_L%d", i);
			no_lod = !!strEndsWith(modelName, buf);
		}
	}

	lodinfoFillInAutoData(lod_info, no_lod, geoFilename, tri_count, radius, processed_high_detail_high_lod, existsCallback, userData, has_verts2, bUseDictionaries);

	if (bUseDictionaries)
	{
		if (!lod_info->force_auto && !exists)
		{
			lod_info->is_in_dictionary = 1;
			stashAddPointer(lod_hash, hashed_name, lod_info, false);
			eaPush(&all_lodinfos.infos, lod_info);
		}
	}

	return lod_info;
}

static bool modelSourceExistsWrapper(void *userData, const char *modelName)
{
	return !!modelSourceFindSibling(userData, modelName);
}

ModelLODInfo *lodinfoFromModelSource(ModelSource *model, const char *geoFilename)
{
	char relPath[MAX_PATH];
	fileRelativePath(geoFilename, relPath);
	return lodinfoFromModelInternal(relPath, modelSourceGetName(model),
		modelSourceHasVerts2(model),
		modelSourceGetTricount(model), modelSourceGetRadius(model),
		!!(modelSourceGetProcessTimeFlags(model) & MODEL_PROCESSED_HIGH_DETAIL_HIGH_LOD), 
		modelSourceExistsWrapper, model, false);
}

bool modelExistsWrapper(void *unused, const char *modelName)
{
	return modelExists(modelName);
}

ModelLODInfo *lodinfoFromModel(Model *model)
{
	ModelLODInfo *ret;
	ret = lodinfoFromModelInternal(model->header->filename, model->name,
		model->header->has_verts2,
		model->header->tri_count, model->header->radius,
		model->header->high_detail_high_lod, 
		modelExistsWrapper, NULL, true);
	setAutoFarmorph(ret);
	return ret;
}


void writeLODInfo(ModelLODInfo *info, const char *geoname)
{
	char outputname[MAX_PATH];
	int info_pushed=0;
	LODInfos infos={0};
	AutoLOD ***lods = 0;
	int i;

	assertmsg(info->modelname, "Invalid LOD info passed to writeLODInfo.");

	strcpy(outputname, getLODFileName(geoname, false));
	info->parsed_filename = allocAddFilename(outputname);
	for (i = 0; i < eaSize(&all_lodinfos.infos); i++)
	{
		ModelLODInfo *lodinfo = all_lodinfos.infos[i];
		if (info->parsed_filename == lodinfo->parsed_filename)
		{
			if ((!(lodinfo->is_automatic || lodinfo->is_no_lod) || (lodinfo->is_automatic && lodinfo->force_auto)) && !lodinfo->removed)
			{
				if (stricmp(info->modelname, lodinfo->modelname)!=0)
				{
					eaPush(&infos.infos, lodinfo);
					eaPush((EArrayHandle *)&lods, lodinfo->lods);
					if (lodinfo->force_auto)
						lodinfo->lods = 0;
				}
				else if (!info_pushed)
				{
					eaPush(&infos.infos, info);
					eaPush((EArrayHandle *)&lods, info->lods);
					if (info->force_auto)
						info->lods = 0;
					info_pushed = 1;
				}
			}
		}
	}
	if (!info_pushed)
	{
		eaPush(&infos.infos, info);
		eaPush((EArrayHandle *)&lods, info->lods);
		if (info->force_auto)
			info->lods = 0;
	}
	ParserWriteTextFile(getLODFileName(geoname, true), parse_LODInfos, &infos, 0, 0);
	for (i = 0; i < eaSize(&infos.infos); i++)
		infos.infos[i]->lods = lods[i];
	eaDestroy(&infos.infos);
	eaDestroy((EArrayHandle *)&lods);
	reloadLODInfo(getLODFileName(geoname, true));
}

#include "AutoLOD_c_ast.c"


