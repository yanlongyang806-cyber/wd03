#include "GfxMaterialProfile.h"
#include "RdrLightAssembly.h"
#include "GfxMaterialProfile_h_ast.c"
#include "RdrShader.h"
#include "fileCache.h"
#include "referencesystem.h"
#include "file.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

typedef struct MaterialProfileLightDefinitions
{
	char *device_id;
	RdrLightDefinition *light_definitions[RDRLIGHT_MASK+1-RDRLIGHT_SHADOWED];
} MaterialProfileLightDefinitions;

AUTO_STRUCT;
typedef struct MaterialProfileData {
	MaterialAssemblerProfile **eaMatProfiles;	AST( NAME(MaterialProfile) )
	StashTable stMaterialProfiles;				NO_AST
	GfxLightingModel **eaLightingModels;		AST( NAME(LightingModel) )
	RdrLightDefinition **eaLightDefintions;		AST( NAME(Light) )
	MaterialProfileLightDefinitions **eaProfileLightDefinitions; NO_AST
} MaterialProfileData;

#include "GfxMaterialProfile_c_ast.c"

static MaterialProfileData material_profile_data;

const MaterialAssemblerProfile *getMaterialProfileForDevice(const char *device_profile_name)
{
	MaterialAssemblerProfile *ret;
	assert(stricmp(device_profile_name, "D3D")!=0); // Should be D3D9 or D3D11
	if (stashFindPointer(material_profile_data.stMaterialProfiles, device_profile_name, &ret))
		return ret;
	assert(0);
	return NULL;
}

const GfxLightingModel *gfxGetLightingModel(const char *device_id, const char *lighting_model_name)
{
	int i;
	assert(stricmp(device_id, "D3D")==0); // Should NOT be D3D9 nor D3D11

	// Could make a stash table if we ever have more than 10 of these
	for (i=eaSize(&material_profile_data.eaLightingModels)-1; i>=0; i--) {
		if (stricmp(material_profile_data.eaLightingModels[i]->device_id, device_id)==0 &&
			stricmp(material_profile_data.eaLightingModels[i]->name, lighting_model_name)==0)
		{
			return material_profile_data.eaLightingModels[i];
		}
	}
	return NULL;
}

const RdrLightDefinition *gfxGetLightDefinition(const char *device_id, RdrLightType light_type)
{
	int i;

	light_type = rdrGetSimpleLightType(light_type);
	// Could make a stash table if we ever have more than 10 of these
	for (i=eaSize(&material_profile_data.eaProfileLightDefinitions)-1; i>=0; i--) {
		if (stricmp(material_profile_data.eaProfileLightDefinitions[i]->device_id, device_id)==0) {
			assertmsg(material_profile_data.eaProfileLightDefinitions[i]->light_definitions, "Light definitions failed to load, unable to continue!");
			return material_profile_data.eaProfileLightDefinitions[i]->light_definitions[light_type];
		}
	}
	return NULL;
}

const RdrLightDefinition **gfxGetLightDefinitionArray(const char *device_id)
{
	int i;

	// Could make a stash table if we ever have more than 10 of these
	for (i=eaSize(&material_profile_data.eaProfileLightDefinitions)-1; i>=0; i--) {
		if (stricmp(material_profile_data.eaProfileLightDefinitions[i]->device_id, device_id)==0) {
			assertmsg(material_profile_data.eaProfileLightDefinitions[i]->light_definitions, "Light definitions failed to load, unable to continue!");
			return material_profile_data.eaProfileLightDefinitions[i]->light_definitions;
		}
	}
	return NULL;
}

static void initMaterialAssemblerProfile(MaterialAssemblerProfile *mat_profile)
{
	// Initialize things that can't be initialized via Parser defaults.
	if (!mat_profile->comment_begin)
		mat_profile->comment_begin = StructAllocString("");
	if (!mat_profile->comment_end)
		mat_profile->comment_end = StructAllocString("");
}

AUTO_FIXUPFUNC;
TextParserResult initLightingModel(GfxLightingModel *lighting_model, enumTextParserFixupType eFixupType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	switch(eFixupType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			char *s;
			char buf[1024];
			getFileNameNoExt(buf, lighting_model->filename);
			assert(!lighting_model->name);
			lighting_model->name = StructAllocString(buf);
			s = strstri(lighting_model->filename, "shaders/");
			assert(s);
			s+=strlen("shaders/");
			strcpy(buf, s);
			s = strchr(buf, '/');
			assert(s);
			*s = '\0';
			assert(!lighting_model->device_id);
			lighting_model->device_id = StructAllocString(buf);
			if (lighting_model->perlight && strchr(lighting_model->perlight, '%')) {
				ErrorFilenamef(lighting_model->filename, "%s", "PerLight data contains a '%', if you meant this for an MaterialAssembler variable, it will not be replaced, as lights are assembled during a different phase.  Store the desired value into a temp in the main Text instead.");
				ret = PARSERESULT_ERROR;
			}
			if (lighting_model->lightshadowbuffer && strchr(lighting_model->lightshadowbuffer, '%')) {
				ErrorFilenamef(lighting_model->filename, "%s", "LightShadowBuffer data contains a '%', if you meant this for an MaterialAssembler variable, it will not be replaced, as lights are assembled during a different phase.  Store the desired value into a temp in the main Text instead.");
				ret = PARSERESULT_ERROR;
			}
		}
	}
	return ret;
}

static void initLightDefinitionDataFromParams(const char *filename, RdrLightDefinitionData *light_def_data, RdrLightDefinitionParamConstant **def_params)
{
	int i, j, map_idx;

	light_def_data->num_vectors = eaSize(&def_params);
	if (light_def_data->num_vectors > RDR_VERTEX_LIGHT_CONSTANT_COUNT)
	{
		ErrorFilenamef(filename, "Too many constant parameters, max is %d.", RDR_VERTEX_LIGHT_CONSTANT_COUNT);
		light_def_data->num_vectors = RDR_VERTEX_LIGHT_CONSTANT_COUNT;
	}

	light_def_data->num_params = 0;
	for (i = 0; i < light_def_data->num_vectors; ++i)
	{
		int constant_register_length = 0;
		for (j = 0; j < eaSize(&def_params[i]->param_names); ++j)
		{
			const char *param_name = def_params[i]->param_names[j];
			RdrLightParameter param_type = rdrLightParameterFromName(param_name);
			int length;

			if (param_type == -1)
			{
				ErrorFilenamef(filename, "Unrecognized parameter type \"%s\"", param_name);
				break;
			}
			assert(param_type < 64);

			if (param_type > LIGHTPARAM_BEGIN_TEXTURES)
			{
				ErrorFilenamef(filename, "Illegal param type \"%s\"", param_name);
				break;
			}

			if (param_type > LIGHTPARAM_BEGIN_4VECTORS)
				length = 4;
			else if (param_type > LIGHTPARAM_BEGIN_SCALARS)
				length = 1;
			else
				length = 3;

			if (constant_register_length + length > 4)
			{
				ErrorFilenamef(filename, "Too many parameters for one register (\"%s\" goes over the limit)", param_name);
				break;
			}

			constant_register_length += length;
			light_def_data->num_params++;
		}
	}

	light_def_data->mappings = calloc(sizeof(light_def_data->mappings[0]), light_def_data->num_params);
	map_idx = 0;

	for (i = 0; i < light_def_data->num_vectors; ++i)
	{
		int constant_register_length = 0;
		for (j = 0; j < eaSize(&def_params[i]->param_names); ++j)
		{
			const char *param_name = def_params[i]->param_names[j];
			RdrLightParameter param_type = rdrLightParameterFromName(param_name);
			RdrLightDefinitionMap *map = &light_def_data->mappings[map_idx];
			int length;

			if (param_type == -1)
				break;
			assert(param_type < 64);

			if (param_type > LIGHTPARAM_BEGIN_TEXTURES)
				break;

			if (param_type > LIGHTPARAM_BEGIN_4VECTORS)
				length = 4;
			else if (param_type > LIGHTPARAM_BEGIN_SCALARS)
				length = 1;
			else
				length = 3;

			if (constant_register_length + length > 4)
				break;

			map->type = param_type;
			map->relative_index = i;

			if (length == 4)
				setVec4(map->swizzle, 0, 1, 2, 3);
			else if (length == 3)
				setVec4(map->swizzle, constant_register_length + 0, constant_register_length + 1, constant_register_length + 2, 4);
			else if (length == 1)
				setVec4(map->swizzle, constant_register_length, 4, 4, 4);
			else
				assert(0);

			constant_register_length += length;
			++map_idx;
		}
	}
}

static char *initLightDefinitionData(const char *filename, RdrLightDefinitionData *light_def_data, bool simple_lighting)
{
	char *cursor;
	U64 light_params_used_mask=0;
	int light_params_vector_count=0;
	int light_params_vector4_count=0;
	int light_params_scalar_count=0;
	int light_params_texture_count=0;
	S32 text_length_diff=0;
	int i;

	// Search through text and gather up the required variables
	for (cursor = light_def_data->text; *cursor; cursor++) {
		if (*cursor == '%') {
			RdrLightParameter param_type;
			char buf[64];
			char *s;
			int orig_length;
			cursor++;
			s = strchr(cursor, '%');
			if (!s) {
				ErrorFilenamef(filename, "Mismatched '%%'");
				break;
			}
			orig_length = s - cursor;
			strncpy(buf, cursor, orig_length);
			cursor = s; // Advance past the %
			// Look up variable type
			param_type = rdrLightParameterFromName(buf);
			if (param_type == -1) {
				ErrorFilenamef(filename, "Unrecognized parameter type %%%s%%", buf);
				break;
			}
			assert(param_type < 64);

			if (simple_lighting && rdrLightParameterExcludeFromSimple(param_type)) {
				if (param_type > LIGHTPARAM_BEGIN_TEXTURES) {
					assertmsg(0, "Not supported.");
				} else {
					text_length_diff += 1 - (orig_length+2);
				}
				continue;
			}

			if (param_type > LIGHTPARAM_BEGIN_TEXTURES)
				text_length_diff += LIGHTTEX_VARIABLE_LENGTH - (orig_length+2);
			else
				text_length_diff += LIGHTDEF_VARIABLE_LENGTH - (orig_length+2);

			if (!(light_params_used_mask & (1LL<<(U64)param_type))) {
				if (param_type > LIGHTPARAM_BEGIN_TEXTURES) {
					light_params_texture_count++;
				} else if (param_type > LIGHTPARAM_BEGIN_4VECTORS) {
					light_params_vector4_count++;
				} else if (param_type > LIGHTPARAM_BEGIN_SCALARS) {
					light_params_scalar_count++;
				} else {
					light_params_vector_count++;
				}
			}
			light_params_used_mask |= (1LL<<(U64)param_type);
		}
	}
	// Calculate number of vectors required to store these values
	light_def_data->num_params = light_params_vector_count + light_params_scalar_count + light_params_vector4_count + light_params_texture_count;
	if (light_params_vector_count > light_params_scalar_count) {
		light_def_data->num_vectors = light_params_vector_count + light_params_vector4_count;
	} else {
		light_def_data->num_vectors = light_params_vector_count + (light_params_scalar_count - light_params_vector_count + 3) / 4 + light_params_vector4_count;
	}
	light_def_data->num_textures = light_params_texture_count;

	// Allocate storage
	light_def_data->mappings = calloc(sizeof(light_def_data->mappings[0]), light_def_data->num_params);
	// Pack them tightly and generate mappings
	{
		RdrLightParameter param_type=0;
		RdrLightDefinitionMap *map = light_def_data->mappings;
		for (i=0; i<light_params_vector_count; i++)
		{
			while (!((1LL<<(U64)param_type) & light_params_used_mask))
				param_type++;
			light_params_used_mask &= ~(1LL<<(U64)param_type);
			assert(param_type < LIGHTPARAM_BEGIN_SCALARS);
			map->type = param_type;
			map->relative_index = i;
			setVec4(map->swizzle, 0, 1, 2, 4);
			map++;
		}
		for (i=0; i<light_params_scalar_count; i++)
		{
			while (!((1LL<<(U64)param_type) & light_params_used_mask))
				param_type++;
			light_params_used_mask &= ~(1LL<<(U64)param_type);
			assert(param_type > LIGHTPARAM_BEGIN_SCALARS && param_type < LIGHTPARAM_BEGIN_4VECTORS);
			map->type = param_type;
			if (i<light_params_vector_count) {
				// Packed with a vector
				map->relative_index = i;
				setVec4(map->swizzle, 3, 4, 4, 4);
			} else {
				// Scalars packed together
				map->relative_index = light_params_vector_count + (i - light_params_vector_count) / 4;
				setVec4(map->swizzle, (i - light_params_vector_count) % 4, 4, 4, 4);
			}
			map++;
		}
		for (i=0; i<light_params_vector4_count; i++)
		{
			while (!((1LL<<(U64)param_type) & light_params_used_mask))
				param_type++;
			light_params_used_mask &= ~(1LL<<(U64)param_type);
			assert(param_type > LIGHTPARAM_BEGIN_4VECTORS && param_type < LIGHTPARAM_BEGIN_TEXTURES);
			map->type = param_type;
			if (light_params_vector_count > light_params_scalar_count) {
				map->relative_index = light_params_vector_count + i;
			} else {
				map->relative_index = light_params_vector_count + (light_params_scalar_count - light_params_vector_count + 3) / 4 + i;
			}
			setVec4(map->swizzle, 0, 1, 2, 3);
			map++;
		}
		for (i=0; i<light_params_texture_count; i++)
		{
			while (!((1LL<<(U64)param_type) & light_params_used_mask))
				param_type++;
			light_params_used_mask &= ~(1LL<<(U64)param_type);
			assert(param_type > LIGHTPARAM_BEGIN_TEXTURES);
			map->type = param_type;
			map->relative_index = i;
			setVec4(map->swizzle, 4, 4, 4, 4);
			map++;
		}
	}
	// Replace variables in text with new params in the form of $LightParam00.xyzw
	{
		char *newtext;
		char *cursorout;
		light_def_data->text_length = (int)(strlen(light_def_data->text) + text_length_diff);
		cursorout = newtext = StructAllocStringLen("", light_def_data->text_length);

		for (cursor = light_def_data->text; *cursor; cursor++) {
			if (*cursor == '%') {
				RdrLightParameter param_type;
				char buf[64];
				char *s;
				bool foundIt;
				cursor++;
				s = strchr(cursor, '%');
				if (!s) {
					//ErrorFilenamef(light_def_data->filename, "Mismatched '%%'");
					break;
				}
				strncpy(buf, cursor, s - cursor);
				cursor = s; // Advance past the %
				// Look up variable type
				param_type = rdrLightParameterFromName(buf);
				if (param_type == -1) {
					break;
				}
				if (simple_lighting && rdrLightParameterExcludeFromSimple(param_type)) {
					if (param_type > LIGHTPARAM_BEGIN_TEXTURES) {
						assertmsg(0, "Not supported.");
					} else {
						// replace with 0 in simple lighting mode
						memcpy_s(cursorout, light_def_data->text_length + 1 - (cursorout - newtext), "0", 1);
						cursorout+=1;
					}
					continue;
				}
				// Find mapping and insert
				foundIt=false;
				for (i=0; i<light_def_data->num_params; i++) {
					if (light_def_data->mappings[i].type == param_type) {
						char varname[64];
						static const char *swizzlemap = "xyzw "; // Need to get this from the MaterialProfile?
						foundIt = true;
						if (param_type > LIGHTPARAM_BEGIN_TEXTURES) {
							sprintf(varname, "$ightSampler%02d", light_def_data->mappings[i].relative_index);
							memcpy_s(cursorout, light_def_data->text_length + 1 - (cursorout - newtext), varname, LIGHTTEX_VARIABLE_LENGTH);
							cursorout+=LIGHTTEX_VARIABLE_LENGTH;
						} else {
							sprintf(varname, "$ightParam%02d.%c%c%c%c",
								light_def_data->mappings[i].relative_index,
								swizzlemap[light_def_data->mappings[i].swizzle[0]],
								swizzlemap[light_def_data->mappings[i].swizzle[1]],
								swizzlemap[light_def_data->mappings[i].swizzle[2]],
								swizzlemap[light_def_data->mappings[i].swizzle[3]]);
							memcpy_s(cursorout, light_def_data->text_length + 1 - (cursorout - newtext), varname, LIGHTDEF_VARIABLE_LENGTH);
							cursorout+=LIGHTDEF_VARIABLE_LENGTH;
						}
						break;
					}
				}
				assert(foundIt);
			} else {
				*cursorout = *cursor;
				cursorout++;
			}
		}
		*cursorout='\0';
		assert(strlen(newtext) == light_def_data->text_length);
		StructFreeString(light_def_data->text);
		light_def_data->text = newtext;
		return newtext;
	}
}

AUTO_FIXUPFUNC;
TextParserResult initLightDefinition(RdrLightDefinition *light_definition, enumTextParserFixupType eFixupType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	switch(eFixupType)
	{
		xcase FIXUPTYPE_DESTRUCTOR:
		{
			int i;
			for (i = 0; i < RLDEFTYPE_COUNT; ++i)
			{
				SAFE_FREE(light_definition->definitions[i].mappings);
			}
			StructFreeString(light_definition->simple_text);
		}
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			char *s;
			const char *cs;
			char buf[1024];
			getFileNameNoExt(buf, light_definition->filename);
			light_definition->light_type = rdrLightTypeFromName(buf);
			if (!light_definition->light_type) {
				ErrorFilenamef(light_definition->filename, "Unable to determine light type from filename.");
				ret = PARSERESULT_ERROR;
			}
			cs = strstri(light_definition->filename, "shaders/");
			assert(cs);
			cs+=strlen("shaders/");
			strcpy(buf, cs);
			s = strchr(buf, '/');
			assert(s);
			*s = '\0';
			assert(!light_definition->device_id);
			light_definition->device_id = StructAllocString(buf);
		}
		xcase FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
		{
			int i;

			for (i = eaSize(&material_profile_data.eaProfileLightDefinitions) - 1; i >= 0; --i) {
				if (stricmp(material_profile_data.eaProfileLightDefinitions[i]->device_id, light_definition->device_id)==0) {
					material_profile_data.eaProfileLightDefinitions[i]->light_definitions[light_definition->light_type] = light_definition;
					break;
				}
			}
			if (i < 0) {
				MaterialProfileLightDefinitions *profile_defs = calloc(1, sizeof(MaterialProfileLightDefinitions));
				eaPush(&material_profile_data.eaProfileLightDefinitions, profile_defs);
				profile_defs->device_id = strdup(light_definition->device_id);
				profile_defs->light_definitions[light_definition->light_type] = light_definition;
			}

			light_definition->simple_text = StructAllocString(light_definition->normal_text);

			light_definition->definitions[RLDEFTYPE_NORMAL].text = light_definition->normal_text;
			light_definition->normal_text = initLightDefinitionData(light_definition->filename, &light_definition->definitions[RLDEFTYPE_NORMAL], false);

			light_definition->definitions[RLDEFTYPE_SIMPLE].text = light_definition->simple_text;
			light_definition->simple_text = initLightDefinitionData(light_definition->filename, &light_definition->definitions[RLDEFTYPE_SIMPLE], true);

			light_definition->definitions[RLDEFTYPE_SHADOW_TEST].text = light_definition->shadow_test_text;
			light_definition->shadow_test_text = initLightDefinitionData(light_definition->filename, &light_definition->definitions[RLDEFTYPE_SHADOW_TEST], false);

			initLightDefinitionDataFromParams(light_definition->filename, &light_definition->definitions[RLDEFTYPE_VERTEX_LIGHTING], light_definition->vertex_light_params.params);

			initLightDefinitionDataFromParams(light_definition->filename, &light_definition->definitions[RLDEFTYPE_SINGLE_DIR_LIGHT], light_definition->single_dir_light_params.params);
		}
	}
	return ret;
}


void gfxLoadMaterialAssemblerProfiles(void)
{
	// Load from source if it exists in production mode
	const char *bin_name = (isProductionMode() && fileExists("shaders/D3D/LightingModels/Standard.LightingModel")) ? NULL : "MaterialProfileData.bin";
	int i;
	int reload = !!material_profile_data.stMaterialProfiles;
	if (!reload) {
		loadstart_printf("Loading MaterialProfiles...");
	}
	if (material_profile_data.stMaterialProfiles)
		stashTableClear(material_profile_data.stMaterialProfiles);
	else
		material_profile_data.stMaterialProfiles = stashTableCreateWithStringKeys(4, StashDefault);
	StructDeInit(parse_MaterialProfileData, &material_profile_data);
	// Can't use shared memory because we're doing a custom allocation and parsing of RdrLightDefinition::mappings
	ParserLoadFiles("shaders/", ".LightingModel;.MaterialProfile;.Light", bin_name, PARSER_BINS_ARE_SHARED|PARSER_USE_CRCS, parse_MaterialProfileData, &material_profile_data);
	for (i=eaSize(&material_profile_data.eaMatProfiles)-1; i>=0; i--) {
		initMaterialAssemblerProfile(material_profile_data.eaMatProfiles[i]);
		if (!stashAddPointer(material_profile_data.stMaterialProfiles, material_profile_data.eaMatProfiles[i]->profile_name, material_profile_data.eaMatProfiles[i], false)) {
			FatalErrorf("Duplicate material profiles defined for DeviceID: %s", material_profile_data.eaMatProfiles[i]->profile_name);
		}
	}
	if (!reload) {
		loadend_printf(" done (%d MaterialProfiles).", eaSize(&material_profile_data.eaMatProfiles));
	} else {
		// Fixups
	}
}

const char *getCodeForOperation(const MaterialAssemblerProfile *profile, const char *op_type_name, char **filename_ptr)
{
	static char filename_ret[MAX_PATH]; // Non-thread safe.  For debugging
	char filename[MAX_PATH];
	const char *ret;

	sprintf(filename, FORMAT_OK(profile->gen_shader_path), op_type_name);

	ret = fileCachedData(filename, NULL);
	if (!ret) {
		ErrorFilenamef(filename, "File not found while getting code for shaders");
		ret = strdup("");
	}
	if (filename_ptr) {
		Strcpy(filename_ret, filename);
		*filename_ptr = filename_ret;
	}
	return ret;
}

static void freeFunc(char *str)
{
	free(str);
}
void gfxMaterialProfileReload(void)
{
	gfxLoadMaterialAssemblerProfiles();
}
