#include "GfxShader.h"
#include "GfxMaterials.h"
#include "GfxMaterialsOpt.h"
#include "GraphicsLibPrivate.h"
#include "GfxMaterialProfile.h"
#include "GfxMaterialAssembler.h"

#include "RdrLightAssembly.h"
#include "RdrState.h"
#include "../XRenderLib/pub/XWrapperInterface.h"
#include "../XRenderLib/pub/XRenderLib.h"
#include "../RdrShaderPrivate.h"

#include "Materials.h"

#include "DynamicCache.h"
#include "structInternals.h"
#include "crypt.h"
#include "endian.h"
#include "GenericPreProcess.h"
#include "GfxMaterialPreload.h"
#include "strings_opt.h"
#include "ContinuousBuilderSupport.h"
#include "ControllerScriptingSupport.h"

#include "GfxLightOptions.h" // for diffuse warp state

#include "pub/GfxLoadScreens.h"

#include "../XRenderLib/thread/rt_xdrawmode.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););

extern RxbxProgramDef vertexShaderDefsMinimal[];
extern RxbxProgramDef vertexShaderDefsSpecial[];
extern RxbxProgramDef pixelShaderDefs[];

PrecompiledShaderData **queuedShaderGraphCompileResults;

bool gfx_disable_auto_force_2_lights;

void gfxShaderUpdateCache(DynamicCache *shaderCache, const char *filename, const char *updb_filename, void *updb_data, int updb_data_size, void *shader_data, int shader_data_size, FileList *file_list)
{
	int updb_filename_size = updb_filename?((int)strlen(updb_filename)+1):0;
	int total_size = sizeof(PrecompiledShaderHeader) + shader_data_size + updb_data_size + updb_filename_size;
	int size_remaining = total_size;
	PrecompiledShaderHeader *header = malloc(total_size);
	char *cursor = (char*)header;
#define ADVANCE(size) cursor += (size); size_remaining -= (size);
	ADVANCE(sizeof(PrecompiledShaderHeader));

	//assert(!updb_filename_size == !updb_data_size); // Should have none or both

	header->updb_filename_size = updb_filename_size;
	if (header->updb_filename_size) {
		memcpy_s(cursor, size_remaining, updb_filename, updb_filename_size);
		ADVANCE(updb_filename_size);
	}

	header->updb_size = updb_data_size;
	if (updb_data_size) {
		memcpy_s(cursor, size_remaining, updb_data, updb_data_size);
		ADVANCE(updb_data_size);
	}

	header->data_size = shader_data_size;
	if (shader_data_size) {
		memcpy_s(cursor, size_remaining, shader_data, shader_data_size);
		ADVANCE(shader_data_size);
	}

	assert(size_remaining == 0);

	if (isBigEndian()) {
		header->updb_filename_size = endianSwapU32(header->updb_filename_size);
		header->updb_size = endianSwapU32(header->updb_size);
		header->data_size = endianSwapU32(header->data_size);
	}

	dynamicCacheUpdateFile(shaderCache, filename, header, total_size, file_list);
	SAFE_FREE(header);
#undef ADVANCE	
}

void gfxVerifyLightCombos(RdrMaterialShader shader_num)
{
	// Validation for light_combos
	if (gfx_state.cclighting && rdrMaxSupportedObjectLights() == MAX_NUM_OBJECT_LIGHTS && !rdr_state.compile_all_shader_types)
	{
		bool bFound=false;
		FOR_EACH_IN_EARRAY(preloaded_light_combos, PreloadedLightCombo, light_combo)
		{
			if (light_combo->light_mask == shader_num.lightMask)
			{
				bFound = true;
			}
		}
		FOR_EACH_END;
		assertmsg(bFound, "Asking for a lighting combination which is not allowed by the light_combos!");
	}
}

static void genLightParameters(
	char **shader_text_with_lights,
	const MaterialAssemblerProfile *mat_profile,
	int first_light_const_index,
	int num_light_vectors,
	int first_light_bool_index,
	int num_light_bools,
	int first_light_tex_index,
	int num_light_textures,
	int **eaComparisonModes)
{
	char buf2[64];
	int i;
	// Generate light parameters, put them at the very top (this might not be valid for GL, in which case we need to insert this at pre-determined location)
	if (mat_profile->light_buffer_name) {
		assert(mat_profile->constant_buffer_start);
		estrConcatf(shader_text_with_lights, FORMAT_OK(mat_profile->constant_buffer_start), mat_profile->light_buffer_name, mat_profile->light_buffer_base_register);
		estrConcatStatic(shader_text_with_lights, "\n");
	}
	if (mat_profile->light_ignore_material_const_offset)	// This is primarily for D3D11.  If we are in D3D11, light parameters are stored within their own buffer, so using an offset based upon the number of material parameters would be detrimental in this case.
		first_light_const_index = 0;
	for (i=0; i<num_light_vectors; i++) {
		char buf1[32];
		sprintf(buf1, "LightParam%02d", i);
		sprintf(buf2, FORMAT_OK(mat_profile->gen_param), buf1, mat_profile->gen_param_offset + first_light_const_index + i);
		estrConcatf(shader_text_with_lights, "%s\n", buf2);
	}
	if (mat_profile->light_buffer_name) {
		assert(mat_profile->constant_buffer_end);
		estrConcatf(shader_text_with_lights, "%s\n", mat_profile->constant_buffer_end);
	}

	// Generate light texture parameters, put them next at the top (this might not be valid for GL, in which case we need to insert this at pre-determined location)
	for (i=0; i<num_light_textures; i++) {
		if (first_light_tex_index + i >= RDR_MAX_TEXTURES)
		{
			printf("Went over texture limit!\n");
			// TODO(CD) error handling
		}
		else
		{
			bool isComparison = eaComparisonModes && *eaComparisonModes && (*eaComparisonModes)[i];

			sprintf(buf2, FORMAT_OK(mat_profile->gen_texsampler2D), "LightSampler", i, first_light_tex_index + i);
			estrConcatf(shader_text_with_lights, "%s\n", buf2);
			if (mat_profile->gen_texsampler_part2)
			{
				// Use the normal sampler state declaration, or a special sampler comparison state declaration (for
				// shadowmaps) depending on whether or not this is a comparison sampler and if the comparison sampler
				// state declaration even exists in this profile.
				const char *part2 = mat_profile->gen_texsampler_part2;
				if(isComparison && mat_profile->gen_texsampler_part2_comparison) {
					part2 = mat_profile->gen_texsampler_part2_comparison;
				}

				sprintf(buf2, FORMAT_OK(part2), "LightSampler", i, first_light_tex_index + i);
				estrConcatf(shader_text_with_lights, "%s\n", buf2);
			}
		}
	}

	// Add boolean registers
	for (i=0; i<num_light_bools; i++) {
		char buf1[32];
		sprintf(buf1, "LightBoolParam%02d", i);
		sprintf(buf2, FORMAT_OK(mat_profile->gen_boolparam), buf1, first_light_bool_index + i);
		estrConcatf(shader_text_with_lights, "%s\n", buf2);
	}
}

static bool insertSpecificLightCode(char **shader_text_with_lights, const MaterialAssemblerProfile *mat_profile, RdrLightType light_type, 
									int *light_vector_index, int *light_texture_index, 
									RdrLightDefinitionType light_def_type)
{
	if (light_type != RDRLIGHT_NONE) {
		const RdrLightDefinition *light_definition = gfxGetLightDefinition(mat_profile->device_id, light_type);
		const RdrLightDefinitionData *light_def_data = rdrGetLightDefinitionData(light_definition, light_def_type);
		if (light_def_data) {
			// Light definition has the parameters for this light packed
			// Parse light shader text, doing replacements from the light_definition
			// Assume pre-process generates Variables in the form of $ightParam00.xyzw
			// Our temps will be named "LightParam00.xyzw", same number of characters,
			int oldindex = estrLength(shader_text_with_lights);
			char *cursor;
			estrConcat(shader_text_with_lights, light_def_data->text, light_def_data->text_length);
			for (cursor = *shader_text_with_lights + oldindex; *cursor; cursor++) {
				// can't call estr functions in here, might lose cursor
				if (*cursor == '$') {
					if (strStartsWith(cursor, "$ightParam")) {
						int relative_index = (cursor[10]-'0') * 10 + (cursor[11]-'0');
						int new_index = *light_vector_index + relative_index;
						*cursor = 'L';
						cursor += 10; // Pointing to the first digit
						cursor[0] = '0' + (new_index / 10);
						cursor[1] = '0' + (new_index % 10);
					} else if (strStartsWith(cursor, "$ightSampler")) {
						int relative_index = (cursor[12]-'0') * 10 + (cursor[13]-'0');
						int new_index = *light_texture_index + relative_index;
						*cursor = 'L';
						cursor += 12; // Pointing to the first digit
						cursor[0] = '0' + (new_index / 10);
						cursor[1] = '0' + (new_index % 10);
					}
				}
			}

			*light_vector_index += light_def_data->num_vectors;
			*light_texture_index += light_def_data->num_textures;

			return true;
		}
	}
	return false;
}

// This function determines which of the samplers are going to be used for shadow maps and need a sampler comparison
// state declaration instead of a normal sampler state declaration.
static void determineComparisonSamplers(const RdrLightDefinitionData *light_def_data, int **texturesToMakeComparison) {

	int j;

	for(j = 0; j < light_def_data->num_params; j++) {
		if(light_def_data->mappings[j].type >= LIGHTPARAM_BEGIN_TEXTURES) {
			if(light_def_data->mappings[j].type == LIGHTPARAM_shadowmap_texture) {

				// This is a comparison sampler for a shadowmap.
				ea32Push(texturesToMakeComparison, 1);

			} else {

				// This is some other light sampler.
				assert(light_def_data->mappings[j].type == LIGHTPARAM_projected_texture ||
					   light_def_data->mappings[j].type == LIGHTPARAM_clouds_texture);

				ea32Push(texturesToMakeComparison, 0);
			}
		}
	}
}

static char *gfxAssembleLightsUniqueShaders(RdrMaterialShader shader_num, const MaterialAssemblerProfile *mat_profile, 
						int first_light_const_index, int first_light_tex_index,
						const char *pre_light_text, int pre_light_text_length,
						const char *per_light_text, int per_light_text_length,
						const char *post_light_text, int post_light_text_length,
						const char *shadow_buffer_text, int shadow_buffer_text_length,
						bool shadow_cast_only,
						FileList *file_list)
{
	char *shader_text_with_lights = NULL;
	int i, num_light_vectors=0, num_light_textures=0, light_text_length=0, light_vector_index, light_texture_index, light_count=0;
	bool simple_lighting = !!(shader_num.shaderMask & MATERIAL_SHADER_SIMPLE_LIGHTING);

	int *eaComparisonModes = NULL;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if (!shadow_cast_only)
		gfxVerifyLightCombos(shader_num);

	// Pass through the light definitions to get the number of light vectors
	for (i = 0; i < MAX_NUM_SHADER_LIGHTS; ++i)
	{
		RdrLightType light_type = rdrGetLightType(shader_num.lightMask, i);

		if (light_type != RDRLIGHT_NONE) {
			const RdrLightDefinition *light_definition = gfxGetLightDefinition(mat_profile->device_id, light_type);
			const RdrLightDefinitionData *light_def_data;

			if (rdrIsShadowedLightType(light_type) && !shadow_cast_only)
			{
				light_def_data = rdrGetLightDefinitionData(light_definition, RLDEFTYPE_SHADOW_TEST);
				if (light_def_data) {
					num_light_vectors += light_def_data->num_vectors;
					num_light_textures += light_def_data->num_textures;
					light_text_length += light_def_data->text_length;

					determineComparisonSamplers(light_def_data, &eaComparisonModes);
				}
			}

			light_def_data = rdrGetLightDefinitionData(light_definition, shadow_cast_only?RLDEFTYPE_SHADOW_TEST:(simple_lighting?RLDEFTYPE_SIMPLE:RLDEFTYPE_NORMAL));
			if (light_def_data) {
				num_light_vectors += light_def_data->num_vectors;
				num_light_textures += light_def_data->num_textures;
				light_text_length += light_def_data->text_length;
				++light_count;

				determineComparisonSamplers(light_def_data, &eaComparisonModes);

			}

			if (light_definition && file_list)
				FileListInsertChecksum(file_list, light_definition->filename, 0);
		}
	}

	// Generate new shader text
	estrStackCreateSize(&shader_text_with_lights, 
					num_light_vectors * 64 + // 64 is estimated upper bound on #characters per temp
					num_light_textures * 64 + // 64 is estimated upper bound on #characters per tex
					pre_light_text_length + 
					per_light_text_length * light_count +
					light_text_length +
					post_light_text_length + 
					64); // Slack, nulls, carriage returns, etc

	genLightParameters(&shader_text_with_lights, mat_profile, first_light_const_index, num_light_vectors, 0, 0, first_light_tex_index, num_light_textures, &eaComparisonModes);
	ea32Destroy(&eaComparisonModes);

	estrConcat(&shader_text_with_lights, pre_light_text, pre_light_text_length);

	light_vector_index = 0;
	light_texture_index = 0;
	for (i = 0; i < MAX_NUM_SHADER_LIGHTS; ++i)
	{
		// Append appropriate code, generating parameters
		RdrLightType light_type = rdrGetLightType(shader_num.lightMask, i);
		bool succeeded = true;

		if (succeeded && rdrIsShadowedLightType(light_type) && !shadow_cast_only) {
			if ((shader_num.shaderMask & MATERIAL_SHADER_SHADOW_BUFFER) && rdrGetMaterialRenderMode(shader_num)!=MATERIAL_RENDERMODE_HAS_ALPHA) {
				// Just increment counts to match the right indices, don't put in any code
				const RdrLightDefinition *light_definition = gfxGetLightDefinition(mat_profile->device_id, light_type);
				const RdrLightDefinitionData *light_def_data = rdrGetLightDefinitionData(light_definition, RLDEFTYPE_SHADOW_TEST);
				light_vector_index += light_def_data->num_vectors;
				light_texture_index += light_def_data->num_textures;
			} else {
				succeeded = insertSpecificLightCode(&shader_text_with_lights, mat_profile, light_type, &light_vector_index, &light_texture_index, RLDEFTYPE_SHADOW_TEST);
			}
		}

		if (succeeded)
			succeeded = insertSpecificLightCode(&shader_text_with_lights, mat_profile, light_type, &light_vector_index, &light_texture_index, shadow_cast_only?RLDEFTYPE_SHADOW_TEST:(simple_lighting?RLDEFTYPE_SIMPLE:RLDEFTYPE_NORMAL));

		if (succeeded && per_light_text)
			estrConcat(&shader_text_with_lights, per_light_text, per_light_text_length);

		if (succeeded && shadow_cast_only)
		{
			estrConcatCharCount(&shader_text_with_lights, '\t', 4);
			estrConcatf(&shader_text_with_lights, "%s\n", mat_profile->shadow_accumulate);
		}
	}

	if (post_light_text)
		estrConcat(&shader_text_with_lights, post_light_text, post_light_text_length);

	PERFINFO_AUTO_STOP();

	return shader_text_with_lights;
}

static void addConditionalBranch(const MaterialAssemblerProfile *mat_profile, char **estr, int tabs)
{
	estrConcatCharCount(estr, '\t', tabs);
	estrConcatf(estr, "#ifdef _XBOX\n");
	estrConcatCharCount(estr, '\t', tabs+1);
	estrConcatf(estr, "%s\n", mat_profile->gen_conditional_branch);
	estrConcatCharCount(estr, '\t', tabs);
	estrConcatf(estr, "#endif\n");
}

static char *gfxAssembleLightsUberShader(const MaterialAssemblerProfile *mat_profile,
									 int first_light_const_index, int first_light_bool_index, int first_light_tex_index,
									 const char *pre_light_text, int pre_light_text_length,
									 const char *per_light_text, int per_light_text_length,
									 const char *post_light_text, int post_light_text_length,
									 const char *shadow_buffer_text, int shadow_buffer_text_length,
									 bool uber_shadows, bool shadow_first_light, bool force_2_lights, FileList *file_list)
{
	RdrUberLightParameters *uber_params = &rdr_uber_params[(uber_shadows?UBER_SHADOWS:UBER_LIGHTING) + (force_2_lights?UBER_SHADOWS_2LIGHTS-UBER_SHADOWS:0)];
	RdrLightDefinitionType light_def_type = uber_shadows?RLDEFTYPE_SHADOW_TEST:(uber_params->simple_lighting?RLDEFTYPE_SIMPLE:RLDEFTYPE_NORMAL);
	char *shader_text_with_lights = NULL;
	int lightIndex;
	int boolIndex=1+first_light_bool_index;
	char boolname[64];
	int num_chunk_light_vectors = uber_params->chunk_const_count * uber_params->max_lights;
	int num_light_vectors = num_chunk_light_vectors + uber_params->shadow_test_const_count;
	int num_light_textures = uber_params->max_textures_total + uber_params->shadow_test_tex_count;
	int num_light_bools = 1+UBERLIGHT_BITS_PER_LIGHT*uber_params->max_lights;
	int light_text_length=0;
	RdrLightType lightType;

	for (lightType = 0; lightType<RDRLIGHT_TYPE_MAX; lightType++)
	{
		if (lightType != RDRLIGHT_NONE) {
			const RdrLightDefinition *light_definition = gfxGetLightDefinition(mat_profile->device_id, lightType);
			const RdrLightDefinitionData *light_def_data = rdrGetLightDefinitionData(light_definition, light_def_type);
			if (light_def_data) {
				light_text_length += light_def_data->text_length * uber_params->max_lights;
			}
			if (light_definition && file_list)
				FileListInsertChecksum(file_list, light_definition->filename, 0);
		}
	}

	// Generate new shader text
	estrStackCreateSize(&shader_text_with_lights, 
		num_light_vectors * 64 + // 64 is estimated upper bound on #characters per temp
		num_light_textures * 64 + // 64 is estimated upper bound on #characters per tex
		pre_light_text_length + 
		per_light_text_length * uber_params->max_lights +
		light_text_length +
		post_light_text_length + 
		uber_params->max_lights * RDRLIGHT_TYPE_MAX * 35 + // if/else/endif/whitespace
		64); // Slack, nulls, carriage returns, etc

	genLightParameters(&shader_text_with_lights, mat_profile, first_light_const_index, num_light_vectors, first_light_bool_index, num_light_bools, first_light_tex_index, num_light_textures, NULL);

	estrConcat(&shader_text_with_lights, pre_light_text, pre_light_text_length);
	for (lightIndex=0; lightIndex<uber_params->max_lights; lightIndex++)
	{
		char buf1[64];
		int bitIndex;
		bool debugLighting=lightIndex < 3;

		if (lightIndex == 1 && !uber_shadows)
		{
			// insert LightShadowBuffer text from lighting model
			estrConcat(&shader_text_with_lights, shadow_buffer_text, shadow_buffer_text_length);
		}

		STATIC_INFUNC_ASSERT(RDRLIGHT_TYPE_MAX == 5); // These next lines are hardcoded to work for this

		addConditionalBranch(mat_profile, &shader_text_with_lights, 3);

		sprintf(buf1, FORMAT_OK(mat_profile->gen_conditional_if), quick_sprintf_returnbuf(SAFESTR(boolname), "LightBoolParam%02d", boolIndex+2));
		estrConcatf(&shader_text_with_lights, "\n\t\t\t%s\n", buf1);

		//sprintf(buf1, mat_profile->gen_conditional_if, quick_sprintf_returnbuf(SAFESTR(boolname), "LightBoolParam%02d", boolIndex+1));
		//estrConcatf(&shader_text_with_lights, "\t\t\t\t%s\n", buf1);
		//estrConcatf(&shader_text_with_lights, "\t\t\t\t%s\n", mat_profile->gen_conditional_else);
		estrConcatStatic(&shader_text_with_lights, "\t\t\t\t{\n"); // hack

		//sprintf(buf1, mat_profile->gen_conditional_if, quick_sprintf_returnbuf(SAFESTR(boolname), "LightBoolParam%02d", boolIndex));
		//estrConcatf(&shader_text_with_lights, "\t\t\t\t\t%s\n", buf1);

		for (lightType = RDRLIGHT_TYPE_MAX-1; lightType>=0; lightType--) {
			int lastValue = lightType+1;
			int nextValue = lightType-1;
			for (bitIndex=UBERLIGHT_BITS_PER_LIGHT; bitIndex>=0; bitIndex--) {
				int bit = 1<<bitIndex;
				if ((bit & lightType) != (bit & lastValue)) {
					if (bit & lightType) {

						addConditionalBranch(mat_profile, &shader_text_with_lights, 2 + UBERLIGHT_BITS_PER_LIGHT - bitIndex);

						sprintf(buf1, FORMAT_OK(mat_profile->gen_conditional_if), quick_sprintf_returnbuf(SAFESTR(boolname), "LightBoolParam%02d", boolIndex + bitIndex));
						estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT - bitIndex);
						estrConcatf(&shader_text_with_lights, "%s\n", buf1);
					} else {
						estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT - bitIndex);
						if (lightType == RDRLIGHT_TYPE_MAX-1) {
							estrConcatStatic(&shader_text_with_lights, "{\n"); // hack
						} else {
							estrConcatf(&shader_text_with_lights, "%s\n", mat_profile->gen_conditional_else);
						}
					}
				}
			}

			// Insert lighting code
			estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT+1);
			estrConcatf(&shader_text_with_lights, "// Light type %d\n", lightType);


			if (debugLighting)
				estrConcatStatic(&shader_text_with_lights, "#ifndef UBERLIGHT_DEBUG\n");
			if (lightType != RDRLIGHT_NONE) {
				const RdrLightDefinition *light_definition = gfxGetLightDefinition(mat_profile->device_id, lightType);
				const RdrLightDefinitionData *light_def_data = rdrGetLightDefinitionData(light_definition, light_def_type);
				int chunk_count1 = round(ceil(light_def_data->num_textures / (F32)uber_params->chunk_tex_count));
				int chunk_count2 = round(ceil(light_def_data->num_vectors / (F32)uber_params->chunk_const_count));
				int chunk_count = MAX(chunk_count1, chunk_count2);

				if ((chunk_count > 1 && lightIndex + chunk_count > uber_params->max_lights) || 
					(light_def_data->num_textures && uber_params->chunk_tex_count * lightIndex + light_def_data->num_textures > uber_params->max_textures_total))
				{
					estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT+1);
					estrConcatf(&shader_text_with_lights, "// Will not fit\n");
				}
				else
				{
					int light_vector_index = uber_params->chunk_const_count * lightIndex;
					int light_texture_index = uber_params->chunk_tex_count * lightIndex;
					if (lightIndex == 0 && !uber_shadows && shadow_first_light)
					{
						int shadow_vector_index = num_chunk_light_vectors;
						int shadow_texture_index = uber_params->max_textures_total;

						// needs shadow test if

						addConditionalBranch(mat_profile, &shader_text_with_lights, 2 + UBERLIGHT_BITS_PER_LIGHT+1);

						estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT+1);
						estrConcatf(&shader_text_with_lights, FORMAT_OK(mat_profile->gen_conditional_if), quick_sprintf_returnbuf(SAFESTR(boolname), "LightBoolParam%02d", first_light_bool_index));
						estrConcatChar(&shader_text_with_lights, '\n');

						// insert shadowing code
						insertSpecificLightCode(&shader_text_with_lights, mat_profile, lightType, &shadow_vector_index, &shadow_texture_index, RLDEFTYPE_SHADOW_TEST);

						// end if
						estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT+1);
						estrConcatf(&shader_text_with_lights, "%s\n", mat_profile->gen_conditional_end);
					}
					
					insertSpecificLightCode(&shader_text_with_lights, mat_profile, lightType, &light_vector_index, &light_texture_index, light_def_type);

					if (uber_shadows)
					{
						estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT+1);
						estrConcatf(&shader_text_with_lights, "%s\n", mat_profile->shadow_accumulate);
					}
				}
			}

			if (debugLighting) {
				// Debug lighting
				estrConcatStatic(&shader_text_with_lights, "#else // UBERLIGHT_DEBUG\n");

				estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT+1);
				estrConcatf(&shader_text_with_lights, "outColor.%c = %f;\n", "xyz"[lightIndex], lightType/(float)RDRLIGHT_TYPE_MAX);

				estrConcatStatic(&shader_text_with_lights, "#endif // UBERLIGHT_DEBUG\n");
			}

			// Endif stuff
			for (bitIndex=0; bitIndex<UBERLIGHT_BITS_PER_LIGHT; bitIndex++) {
				int bit = 1<<bitIndex;
				if ((bit & lightType) != (bit & nextValue)) {
					if (!(bit & lightType)) {
						estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT - bitIndex);
						estrConcatf(&shader_text_with_lights, "%s\n", mat_profile->gen_conditional_end);
					}
				}
			}
		}

		if (per_light_text) {
			if (0) {
				sprintf(buf1, FORMAT_OK(mat_profile->gen_conditional_if), quick_sprintf_returnbuf(SAFESTR(boolname), "LightBoolParam%02d || LightBoolParam%02d || LightBoolParam%02d", boolIndex, boolIndex+1, boolIndex+2));
				estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT - bitIndex);
				estrConcatf(&shader_text_with_lights, "%s\n", buf1);
			}
			estrConcat(&shader_text_with_lights, per_light_text, per_light_text_length);
			if (0) {
				estrConcatCharCount(&shader_text_with_lights, '\t', 2 + UBERLIGHT_BITS_PER_LIGHT - bitIndex);
				estrConcatf(&shader_text_with_lights, "%s\n", mat_profile->gen_conditional_end);
			}
		}

		boolIndex+=UBERLIGHT_BITS_PER_LIGHT;
	}

	//printf("%s", shader_text_with_lights);

	if (post_light_text)
		estrConcat(&shader_text_with_lights, post_light_text, post_light_text_length);
	return shader_text_with_lights;
}

char *gfxAssembleLights(RdrMaterialShader shader_num, const MaterialAssemblerProfile *mat_profile, 
						int first_light_const_index, int first_light_bool_index, int first_light_tex_index,
						const char *pre_light_text, int pre_light_text_length,
						const char *per_light_text, int per_light_text_length,
						const char *post_light_text, int post_light_text_length,
						const char *shadow_buffer_text, int shadow_buffer_text_length,
						bool shadow_cast_only,
						FileList *file_list)
{
	shadow_cast_only = shadow_cast_only || shader_num.shaderMask & MATERIAL_SHADER_UBERSHADOW;

	if (shader_num.shaderMask & (MATERIAL_SHADER_UBERLIGHT|MATERIAL_SHADER_UBERSHADOW)) {
		return gfxAssembleLightsUberShader(mat_profile,
			first_light_const_index, first_light_bool_index, first_light_tex_index,
			pre_light_text, pre_light_text_length,
			per_light_text, per_light_text_length,
			post_light_text, post_light_text_length,
			shadow_buffer_text, shadow_buffer_text_length,
			shadow_cast_only, !(rdrGetMaterialRenderMode(shader_num)==MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE && (shader_num.shaderMask & MATERIAL_SHADER_SHADOW_BUFFER)),
			(shader_num.shaderMask & MATERIAL_SHADER_FORCE_2_LIGHTS) != 0, file_list);
	} else {
		return gfxAssembleLightsUniqueShaders(shader_num, mat_profile,
			first_light_const_index, first_light_tex_index,
			pre_light_text, pre_light_text_length,
			per_light_text, per_light_text_length,
			post_light_text, post_light_text_length,
			shadow_buffer_text, shadow_buffer_text_length,
			shadow_cast_only, file_list);
	}
}

void gfxShaderAddDefines(ShaderGraphRenderInfo *graph_render_info, RdrMaterialShader shader_num,
						 ShaderGraphReflectionType graph_reflection_type,
						 const char *special_defines[], int special_defines_size,
						 int *special_defines_count)
{
	int flag=1, allflags;
	const char *temp_s;
	int i;

	// Generate list of defines
#define ADD_DEFINE(s) if (temp_s=s) { assert(*special_defines_count<special_defines_size); special_defines[(*special_defines_count)++] = allocAddCaseSensitiveString(temp_s); }

	*special_defines_count=0;
	flag=1;

	if (graph_render_info)
	{
		for (allflags = graph_render_info->shader_graph->graph_flags; allflags; flag<<=1, allflags>>=1) {
			if ((shader_num.shaderMask & MATERIAL_SHADER_COVERAGE_DISABLE) && (flag == SGRAPH_ALPHA_TO_COVERAGE))
				continue;
			if (!gfx_lighting_options.enableDiffuseWarpTex && (flag == SGRAPH_DIFFUSEWARP))
				continue;
			if (allflags & 1)
			{
				ADD_DEFINE(StaticDefineIntRevLookup(ShaderGraphFlagsEnum, flag));
			}
		}

		if (graph_render_info->shader_graph->graph_reflection_type == SGRAPH_REFLECT_NONE &&
			graph_reflection_type > 0) // Must be forced
		{
			ADD_DEFINE("ReflectionValueForced");
		}
	}

	// Reflection type doesn't matter if we're doing a depth-only shader
	if (rdrGetMaterialRenderMode(shader_num) == MATERIAL_RENDERMODE_DEPTH_ONLY)
		graph_reflection_type = SGRAPH_REFLECT_NONE;

	switch (graph_reflection_type)
	{
		case SGRAPH_REFLECT_NONE:
			ADD_DEFINE("ReflectionNone");
			break;
		case SGRAPH_REFLECT_SIMPLE:
			ADD_DEFINE("ReflectionSimple");
			break;
		case SGRAPH_REFLECT_CUBEMAP:
		{
			if (shader_num.shaderMask & MATERIAL_SHADER_FORCE_SM20)
			{
				ADD_DEFINE("ReflectionSimple");
			}
			else
			{
				ADD_DEFINE("ReflectionCubemap");
			}
		}
	}

	if (graph_render_info)
	{
		for (i = 0; i < eaSize(&graph_render_info->shader_graph->defines); ++i)
		{
			ADD_DEFINE(graph_render_info->shader_graph->defines[i]);
		}
	}

	// other shader properties
	if (shader_num.shaderMask & MATERIAL_SHADER_NOALPHAKILL)
	{
		ADD_DEFINE("NOALPHAKILL");
	}
	if (shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT)
	{
		ADD_DEFINE("UBERLIGHTING");
	}
	if (shader_num.shaderMask & MATERIAL_SHADER_HAS_HDR_TEXTURE)
	{
		ADD_DEFINE("HAS_HDR_TEXTURE");
	}
	if (gfx_state.volumeFog)
	{
		ADD_DEFINE("VOLUME_FOG");
	}
	if (rdrIsSingleDirectionalLight(shader_num.lightMask))
	{
		ADD_DEFINE("SINGLE_DIRLIGHT");
	}
	if (shader_num.shaderMask & MATERIAL_SHADER_VERTEX_ONLY_LIGHTING)
	{
		ADD_DEFINE("VERTEX_ONLY_LIGHTING");
	}
	if (shader_num.shaderMask & MATERIAL_SHADER_FORCE_SM20)
	{
		ADD_DEFINE("FORCE_SM20");
	}
	if (shader_num.shaderMask & MATERIAL_SHADER_STEREOSCOPIC)
	{
		ADD_DEFINE("STEREOSCOPIC");
	}

	if (shader_num.shaderMask & MATERIAL_SHADER_SIMPLE_LIGHTING ||
		shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT)
	{
		ADD_DEFINE("NO_CCLIGHTING");
	}
	
	if (shader_num.shaderMask & MATERIAL_SHADER_MANUAL_DEPTH_TEST)
	{
		ADD_DEFINE("MANUAL_DEPTH_TEST");
	}
	else if (shader_num.shaderMask & MATERIAL_SHADER_SHADOW_BUFFER)
	{
		ADD_DEFINE("SHADOW_BUFFER");
	}

	// render modes
	switch (rdrGetMaterialRenderMode(shader_num))
	{
		xcase MATERIAL_RENDERMODE_DEPTH_ONLY:
			ADD_DEFINE("DEPTH_ONLY");
		xcase MATERIAL_RENDERMODE_NOLIGHTS:
			ADD_DEFINE("DisableLights");
		xcase MATERIAL_RENDERMODE_HAS_ALPHA:
			ADD_DEFINE("HAS_ALPHA"); // Default behavior, not referenced
		xcase MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE:
			ADD_DEFINE("OPAQUE_AFTER_ZPRE");
	}
#undef ADD_DEFINE

}

char *gfxShaderGetTypeName(RdrMaterialShader shader_num)
{
	static char type_name[1024];

	type_name[0] = 0;

	if (shader_num.shaderMask & MATERIAL_SHADER_D3D11)
		strcat(type_name, "_d3d11");
	if (shader_num.shaderMask & MATERIAL_SHADER_NOALPHAKILL)
		strcat(type_name, "_!a");
	if (shader_num.shaderMask & MATERIAL_SHADER_SHADOW_BUFFER)
		strcat(type_name, "_sb");
	if (shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT)
		strcat(type_name, "_uber");
	if (shader_num.shaderMask & MATERIAL_SHADER_HAS_HDR_TEXTURE)
		strcat(type_name, "_hqb");
	if (shader_num.shaderMask & MATERIAL_SHADER_FORCE_2_LIGHTS)
		strcat(type_name, "_2l");
	if (shader_num.shaderMask & MATERIAL_SHADER_SIMPLE_LIGHTING)
		strcat(type_name, "_sl");
	if (shader_num.shaderMask & MATERIAL_SHADER_VERTEX_ONLY_LIGHTING)
		strcat(type_name, "_vo");
	if (shader_num.shaderMask & MATERIAL_SHADER_FORCE_SM20)
		strcat(type_name, "_f20");
	if (shader_num.shaderMask & MATERIAL_SHADER_MANUAL_DEPTH_TEST)
		strcat(type_name, "_mdt");
	if (shader_num.shaderMask & MATERIAL_SHADER_COVERAGE_DISABLE)
		strcat(type_name, "_!cvrg");
	if (shader_num.shaderMask & MATERIAL_SHADER_STEREOSCOPIC)
		strcat(type_name, "_stereo");
	if (gfx_state.volumeFog)
		strcat(type_name, "_vf");
	if (rdrIsSingleDirectionalLight(shader_num.lightMask))
		strcat(type_name, "_sdl");

	switch (rdrGetMaterialRenderMode(shader_num))
	{
		xcase MATERIAL_RENDERMODE_DEPTH_ONLY:
			strcat(type_name, "_do");

		xcase MATERIAL_RENDERMODE_NOLIGHTS:
			strcat(type_name, "_!l");

		xdefault:
		{
			RdrLightType light_type;
			int i;

			if (rdrGetMaterialRenderMode(shader_num) == MATERIAL_RENDERMODE_HAS_ALPHA)
				strcat(type_name, "_alp");
			else if (rdrGetMaterialRenderMode(shader_num) == MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE)
				strcat(type_name, "_opp");
			else
				assert(0);

			for (i = 0; i < MAX_NUM_SHADER_LIGHTS; ++i)
			{
				if (light_type = rdrGetLightType(shader_num.lightMask, i))
				{
					if (i > 0)
						devassertmsg(rdrGetLightType(shader_num.lightMask, i-1), "Invalid shader number/light combination"); // If this goes off, light 0 is off, but light 1 is on?!  Will cache to the wrong filename (identical to light0 on and light 1 off)

					if (rdrIsShadowedLightType(light_type))
						strcat(type_name, "_S");
					else
						strcat(type_name, "_");

					light_type = rdrGetSimpleLightType(light_type);
					switch(light_type) {
					case RDRLIGHT_DIRECTIONAL:
						strcat(type_name, "Ld");
						break;
					case RDRLIGHT_POINT:
						strcat(type_name, "Lpt");
						break;
					case RDRLIGHT_SPOT:
						strcat(type_name, "Ls");
						break;
					case RDRLIGHT_PROJECTOR:
						strcat(type_name, "Lpr");
						break;
					default:
						assertmsg(0, "Unknown light type used during shader assembly.");
					}
				}
			}
		}
	}

	return type_name;
}

const char *gfxShaderGetDebugName(const char *graph_filename, RdrMaterialShader shader_type)
{
	char buf[1024];
	char *s;

	strcpy(buf, graph_filename);
	s = strrchr(buf, '.');
	if (s)
	{
		*s = 0;
		strcatf_s(buf, ARRAY_SIZE_CHECKED(buf), ".material_%s", gfxShaderGetTypeName(shader_type));
	}
	return allocAddFilename(buf);
}

static void gfxShaderGetCacheName(char *cache_filename, size_t cache_filename_size, const char *shader_debug_name, const MaterialAssemblerProfile *mat_profile, const char *special_defines[], int special_defines_count, const char *intrinsic_defines)
{
	int i;
	U32 crc;
	char all_defines_buf[2048];
	char *last=NULL;
    all_defines_buf[0] = 0;

	//strcpy_s(SAFESTR2(cache_filename), shader_debug_name);
	// construct path without full folder names (file name must be unique)

	STR_COMBINE_BEGIN_S(cache_filename, cache_filename_size);
	// Concat and truncate paths
	// skip first slash, should be materials/
	for (s=strchr(shader_debug_name, '/'); *s; )
	{
		if (*s == '/') {
			if (last) {
				c = last+2;
				last = c;
			} else {
				s++;
				last = c-1;
			}
		} else if (*s == '.') {
			if (strStartsWith(s, ".Material"))
				s += 9; //strlen(".Material");
		}
		*c++=*s++;
	}
	STR_COMBINE_END(cache_filename);


	strcatf_s(SAFESTR2(cache_filename), "_%s", strrchr(intrinsic_defines, ' ')+1);
	//strcatf_s(SAFESTR2(cache_filename), "_%s", mat_profile->profile_name);
	cryptAdler32Init();
	cryptAdler32Update(mat_profile->profile_name, (int)strlen(mat_profile->profile_name));
	for (i=0; i<special_defines_count; i++)
	{
		strcat(all_defines_buf, " ");
		strcat(all_defines_buf, special_defines[i]);
		cryptAdler32Update(special_defines[i], (int)strlen(special_defines[i]));
	}
	strcat(all_defines_buf, intrinsic_defines);
	cryptAdler32Update(intrinsic_defines, (int)strlen(intrinsic_defines));
	strcatf_s(SAFESTR2(cache_filename), "__%08x", (crc=cryptAdler32Final()));
	if (rdr_state.compile_all_shader_types)
	{
		static StashTable stCacheCRCVerify;
		const char *oldstring;
		if (!stCacheCRCVerify)
			stCacheCRCVerify = stashTableCreateInt(30);
		if (stashIntFindPointerConst(stCacheCRCVerify, crc, &oldstring))
		{
			assert(stricmp(oldstring, all_defines_buf)==0); // Otherwise hash collision!
		} else {
			stashIntAddPointer(stCacheCRCVerify, crc, strdup(all_defines_buf), false);
		}
	}
}


char *gfxShaderGetText(ShaderGraphRenderInfo *graph_render_info, const MaterialAssemblerProfile *mat_profile, RdrMaterialShader shader_num, FileList *file_list, bool force_get)
{
	char *shader_text=NULL;
	if (!graph_render_info->shader_text || !graph_render_info->shader_text[0] ||
		!graph_render_info->shader_text_pre_light || !graph_render_info->shader_text_pre_light[0] || 
		graph_render_info->mat_profile != mat_profile || force_get)
	{
		PERFINFO_AUTO_START("assembleShaderFromGraph", 1);
		if (!rdrShaderPreloadSkip(graph_render_info->shader_graph->filename))
			rdrShaderPreloadLog("Assembling new shader %s", graph_render_info->shader_graph->filename);
		assembleShaderFromGraph(graph_render_info->shader_graph, mat_profile);
		assert(graph_render_info->source_files);
		PERFINFO_AUTO_STOP();
	}

	// render modes
	switch (rdrGetMaterialRenderMode(shader_num))
	{
		xcase MATERIAL_RENDERMODE_DEPTH_ONLY:
		case MATERIAL_RENDERMODE_NOLIGHTS:
			shader_text = graph_render_info->shader_text;

		xdefault:
			// forward lighting
			shader_text = gfxAssembleLights(shader_num, mat_profile, 
				graph_render_info->num_input_vectors, 0, graph_render_info->num_textures,
				graph_render_info->shader_text_pre_light, graph_render_info->shader_text_pre_light_length,
				graph_render_info->shader_text_per_light, graph_render_info->shader_text_per_light_length,
				graph_render_info->shader_text_post_light, graph_render_info->shader_text_post_light_length,
				graph_render_info->shader_text_shadow_buffer, graph_render_info->shader_text_shadow_buffer_length,
				false, file_list);
	}
	return shader_text;
}


void shaderCacheFinishedCompiling(RdrShaderFinishedData *data)
{
	ShaderGraphHandleData *handle_data = data->userData->handle_data;
	if (handle_data)
	{
		// Regular shader
		assert(handle_data->sent_to_renderer);
		assert(!handle_data->sent_to_dcache);
		handle_data->sent_to_renderer = 0;
		if (handle_data->freeme) {
			destroyShaderGraphHandleData(handle_data);
		} else {
			data->userData->handle_data->loading = 0;
		}
	} else {
		// Postprocessing shader or material shader compiled for other platforms
	}
	if (data->compilationFailed) {
		// Do nothing
		// We have an error shader bound instead.
	} else {
		gfxShaderUpdateCache(gfx_state.shaderCache, data->userData->filename, data->updb_filename, data->updb_data, data->updb_data_size, data->shader_data, data->shader_data_size, &data->file_list);
	}
	FileListDestroy(&data->file_list);
	SAFE_FREE(data->shader_data);
	SAFE_FREE(data->updb_data);
	SAFE_FREE(data->updb_filename);
	SAFE_FREE(data);
}

static bool shaderCacheLoaded(DynamicCacheElement* elem, ShaderGraphHandleData *handle_data)
{
	bool bRet=false;
	assert(!handle_data->sent_to_renderer);
	assert(handle_data->sent_to_dcache);
	handle_data->sent_to_dcache = 0;
	if (handle_data->freeme) {
		destroyShaderGraphHandleData(handle_data);
		return true; // Loaded fine, but we're not gonna use it
	} else {
		U32 buffer_size = dceGetDataSize(elem);
		PrecompiledShaderData *data = calloc(sizeof(*data),1);
		// Send pre-compiled data to appropriate device (queue it and let it happen in gfxDemandLoadShaderGraphs)
		data->precompiled_shader = dceGetDataAndAcquireOwnership(elem);
		data->precompiled_shader->updb_filename_size = endianSwapIfBig(U32, data->precompiled_shader->updb_filename_size);
		data->precompiled_shader->updb_size = endianSwapIfBig(U32, data->precompiled_shader->updb_size);
		data->precompiled_shader->data_size = endianSwapIfBig(U32, data->precompiled_shader->data_size);
		if (sizeof(PrecompiledShaderHeader) +
			data->precompiled_shader->data_size + 
			data->precompiled_shader->updb_filename_size +
			data->precompiled_shader->updb_size != buffer_size)
		{
			verbose_printf("Corrupt precompiled shader data detected.\n");
			SAFE_FREE(data);
			bRet = false;
		} else {
			data->handle_data = handle_data;
			data->rendererIndex = handle_data->rendererIndex;
			eaPush(&queuedShaderGraphCompileResults, data);
			bRet = true;
		}

		if (!bRet) {
			// This failed, we're sending it back to the DynamicCache to call our failed callback
			handle_data->sent_to_dcache = 1;
		}
		return bRet;
	}
}

static void shaderCacheFailed(DynamicCacheElement* elem, ShaderGraphHandleData *handle_data)
{
	assert(!handle_data->sent_to_renderer);
	assert(handle_data->sent_to_dcache);
	handle_data->sent_to_dcache = 0;
	handle_data->loading = 0;
	if (handle_data->freeme) {
		destroyShaderGraphHandleData(handle_data);
	} else {
		gfxDemandLoadShaderGraphInternal(handle_data); // Start it loading again (this time not through the cache)
	}
}

void gfxCacheShaderGraphForOtherTarget(ShaderGraphRenderInfo *graph_render_info, ShaderGraphReflectionType reflectionType,
									   RdrMaterialShader shader_num, const char *intrinsic_defines, const char *shader_model,
									   const MaterialAssemblerProfile *mat_profile)
{
	char error_filename[MAX_PATH];
	RdrShaderParams *shader_params;
	const char *special_defines[30];
	int special_defines_count=0;
	char *shader_text;
	const char *shader_debug_name;
	char cache_filename[MAX_PATH];
	FileList file_list=NULL;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	shader_num = gfxShaderFilterShaderNum(shader_num);

	gfxShaderAddDefines(graph_render_info, shader_num, reflectionType, special_defines, ARRAY_SIZE(special_defines), &special_defines_count);

	shader_debug_name = gfxShaderGetDebugName(graph_render_info->shader_graph->filename, shader_num);
	gfxShaderGetCacheName(SAFESTR(cache_filename), shader_debug_name, mat_profile, special_defines, special_defines_count, intrinsic_defines);

	// Check for a cached version of the shader
	PERFINFO_AUTO_START("dynamicCache check", 1);

	if (!rdr_state.disableShaderCache && dynamicCacheIsFileUpToDateSync_WillStall(gfx_state.shaderCache, cache_filename)) {
		// Already compiled, need to touch timestamps
		dynamicCacheTouchFile(gfx_state.shaderCache, cache_filename);

		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
		return;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("middle", 1);

	shader_text = gfxShaderGetText(graph_render_info, mat_profile, shader_num, &file_list, false);

	FOR_EACH_IN_EARRAY(graph_render_info->source_files, FileEntry, file_entry)
		FileListInsertInternal(&file_list, file_entry->path, file_entry->date);
	FOR_EACH_END;

	shader_params = rdrStartUpdateShader(gfx_state.currentDevice->rdr_device, 0, SPT_FRAGMENT, special_defines, special_defines_count, shader_text);
	shader_params->shader_debug_name = shader_debug_name;
    sprintf(error_filename, FORMAT_OK(mat_profile->gen_shader_path), "error");
	shader_params->shader_error_filename = allocAddFilename(error_filename);
	shader_params->intrinsic_defines = intrinsic_defines;
	shader_params->override_shader_model = shader_model;

	shader_params->finishedCallbackData = calloc(sizeof(*shader_params->finishedCallbackData) + sizeof(*shader_params->finishedCallbackData->userData), 1);
	shader_params->finishedCallbackData->finishedCallback = shaderCacheFinishedCompiling;
	shader_params->finishedCallbackData->userData = (GfxShaderFinishedData*)(shader_params->finishedCallbackData+1);
	shader_params->finishedCallbackData->userData->filename = allocAddFilename(cache_filename);
	shader_params->finishedCallbackData->userData->handle_data = NULL;
	shader_params->finishedCallbackData->file_list = file_list;
	file_list = NULL;

	assert(!file_list);
	PERFINFO_AUTO_STOP_START("rdrEndUpdateShader", 1);
	rdrEndUpdateShader(gfx_state.currentDevice->rdr_device);
	PERFINFO_AUTO_STOP();

	if (shader_text != graph_render_info->shader_text)
		estrDestroy(&shader_text);

	PERFINFO_AUTO_STOP();
}


void gfxDemandLoadShaderGraphInternal(ShaderGraphHandleData *handle_data)
{
	ShaderGraphRenderInfo *graph_render_info = handle_data->graph_render_info;
	char error_filename[MAX_PATH];
	RdrShaderParams *shader_params;
	const MaterialAssemblerProfile *mat_profile;
	const char *special_defines[30];
	int special_defines_count=0;
	char *shader_text;
	const char *shader_debug_name;
	char cache_filename[MAX_PATH];
	FileList file_list=NULL;
	bool bDoCaching;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	assert(!handle_data->sent_to_dcache);

	bDoCaching = !(rdr_state.disableShaderCache || (graph_render_info->shader_graph->graph_flags & SGRAPH_NO_CACHING));

	if (bDoCaching && handle_data->sent_to_renderer) {
		// This shader handle is already on it's way to or from a renderer (presumably
		// a different device than the one requesting this load)
		// Do nothing until it finishes.
		PERFINFO_AUTO_STOP();
		return;
	}

	if (handle_data->shader_num.shaderMask & MATERIAL_SHADER_FORCE_SM20)
	{
		// force back to the DX9 profile
		mat_profile = getMaterialProfileForDevice("D3D9");
		// Do NOT cache the shader if we can't run it!
		if (gfx_state.currentDevice->rdr_device->device_xbox->d3d11_device)
			bDoCaching = false;
	}
	else
	{
		mat_profile = getMaterialProfileForDevice(rdrGetDeviceProfileName(gfx_state.currentDevice->rdr_device));
	}

	handle_data->reflectionType = CLAMP(graph_render_info->shader_graph->graph_reflection_type, gfx_state.debug.forceReflectionLevel, gfx_state.settings.maxReflection);
	gfxShaderAddDefines(graph_render_info, handle_data->shader_num, handle_data->reflectionType, special_defines, ARRAY_SIZE(special_defines), &special_defines_count);

	shader_debug_name = gfxShaderGetDebugName(graph_render_info->shader_graph->filename, handle_data->shader_num);

	// Check for a cached version of the shader
	if (bDoCaching)
	{
		const char *intrinsic_defines;
		PERFINFO_AUTO_START("dynamicCache check", 1);
		intrinsic_defines = rdrGetIntrinsicDefines(gfx_state.currentDevice->rdr_device);
		gfxShaderGetCacheName(SAFESTR(cache_filename), shader_debug_name, mat_profile, special_defines, special_defines_count, intrinsic_defines);

		if (shader_dynamicCacheFileExists(gfx_state.shaderCache, cache_filename)) {
			// Send pre-compiled data if up to date
			handle_data->loading = 1; // Do not have this called over and over again while we're waiting for the load!
			handle_data->sent_to_dcache = 1;
			handle_data->rendererIndex = gfx_state.currentRendererIndex;
			// TODO: Don't send invalid shader handles to the renderer while this is loading?
			if (!rdrShaderPreloadSkip(cache_filename))
				rdrShaderPreloadLog("Queuing disk read for cached shader %s", cache_filename);
			if (!rdr_state.backgroundShaderCompile)
			{
				dynamicCacheGetSync2(gfx_state.shaderCache, cache_filename, shaderCacheLoaded, shaderCacheFailed, handle_data);
			} else {
				dynamicCacheGetAsync(gfx_state.shaderCache, cache_filename, shaderCacheLoaded, shaderCacheFailed, handle_data);
			}

			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return;
		}
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_START("middle", 1);

	shader_text = gfxShaderGetText(graph_render_info, mat_profile, handle_data->shader_num, bDoCaching?&file_list:NULL, false);

	if (bDoCaching) {
		FOR_EACH_IN_EARRAY(graph_render_info->source_files, FileEntry, file_entry)
			FileListInsertInternal(&file_list, file_entry->path, file_entry->date);
		FOR_EACH_END;
	}

	shader_params = rdrStartUpdateShader(gfx_state.currentDevice->rdr_device, handle_data->handle, SPT_FRAGMENT, special_defines, special_defines_count, shader_text);
	shader_params->shader_debug_name = shader_debug_name;
    sprintf(error_filename, FORMAT_OK(mat_profile->gen_shader_path), "error");

	shader_params->shader_error_filename = allocAddFilename(error_filename);
	if (handle_data->shader_num.shaderMask & MATERIAL_SHADER_FORCE_SM20)
	{
		shader_params->intrinsic_defines = "SM20 DEPTH_TEXTURE";
		shader_params->override_shader_model = "ps_2_0";
		shader_params->hideCompileErrors = 1;
	}
	if (bDoCaching)
	{
		shader_params->finishedCallbackData = calloc(sizeof(*shader_params->finishedCallbackData) + sizeof(*shader_params->finishedCallbackData->userData), 1);
		shader_params->finishedCallbackData->finishedCallback = shaderCacheFinishedCompiling;
		shader_params->finishedCallbackData->userData = (GfxShaderFinishedData*)(shader_params->finishedCallbackData+1);
		shader_params->finishedCallbackData->userData->filename = allocAddFilename(cache_filename);
		shader_params->finishedCallbackData->userData->handle_data = handle_data;
		shader_params->finishedCallbackData->file_list = file_list;
		file_list = NULL;
	}
	assert(!file_list);
	assert(!handle_data->sent_to_renderer);
	if (bDoCaching)
		handle_data->sent_to_renderer = 1;
	PERFINFO_AUTO_STOP_START("rdrEndUpdateShader", 1);
	rdrEndUpdateShader(gfx_state.currentDevice->rdr_device);
	PERFINFO_AUTO_STOP();

	if (shader_text != graph_render_info->shader_text)
		estrDestroy(&shader_text);

	handle_data->load_state |= gfx_state.currentRendererFlag;

	PERFINFO_AUTO_STOP();
}


void gfxLoadPrecompiledShaderInternal(PrecompiledShaderHeader *psh, ShaderHandle handle, const char *shader_debug_name)
{
	RdrShaderParams *shader_params;
	char *cursor = (char*)(psh + 1);
	char *updb_filename=NULL;
	void *updb_data=NULL;
	void *shader_data=NULL;

	// Extract pointers
	if (psh->updb_filename_size) {
		updb_filename = cursor;
		cursor += psh->updb_filename_size;
	}
	if (psh->updb_size) {
		updb_data = cursor;
		cursor += psh->updb_size;
	}
	if (psh->data_size) {
		shader_data = cursor;
		cursor+= psh->data_size;
	}
	assert(shader_data);
	assert(!updb_data == !updb_filename);

#if _XBOX
	// Xbox: Save UPDB
	if (updb_filename && isDevelopmentMode())
	{
		char file_to_write[MAX_PATH];
		FILE *updb_file;
		strcpy(file_to_write, updb_filename);
		strstriReplace(file_to_write, "ShaderDumpxe:\\", "devkit:\\");
		strstriReplace(file_to_write, "xe:\\", "devkit:\\");
		if (rdr_state.noWriteCachedUPDBs || rdr_state.quickUPDB && fileSize(file_to_write) == psh->updb_size)
		{
			// Do nothing, assume the file which is there is the right one
		} else {
			makeDirectoriesForFile(file_to_write);
			updb_file = fileOpen(file_to_write, "wb");
			if (updb_file) {
				fwrite(updb_data, 1, psh->updb_size, updb_file);
				fileClose(updb_file);
			} else {
				printf("Error opening UPDB file: %s\n", file_to_write);
			}
		}
	}
#else
	// PC should never load a shader with updbs.  Hash conflict?
	assert(!updb_filename);
#endif

	// Send to renderer
	shader_params = rdrStartUpdateShaderPrecompiled(gfx_state.currentDevice->rdr_device, handle, SPT_FRAGMENT, shader_data, psh->data_size);
	shader_params->shader_debug_name = shader_debug_name;
	rdrEndUpdateShader(gfx_state.currentDevice->rdr_device);
}

void gfxDemandLoadPrecompiled(PrecompiledShaderData *psd)
{
	const char *shader_debug_name;
	ShaderGraphHandleData *handle_data = psd->handle_data;
	ShaderGraphRenderInfo *graph_render_info = psd->handle_data->graph_render_info;

	shader_debug_name = gfxShaderGetDebugName(graph_render_info->shader_graph->filename, handle_data->shader_num);
	gfxLoadPrecompiledShaderInternal(psd->precompiled_shader, handle_data->handle, shader_debug_name);
		
	handle_data->load_state |= gfx_state.currentRendererFlag;
	handle_data->loading = 0;
	SAFE_FREE(psd->precompiled_shader);
	SAFE_FREE(psd);
}


void gfxShaderCompileForXbox(ShaderGraphRenderInfo *graph_render_info, RdrMaterialShader shader_num)
{
#if PLATFORM_CONSOLE
	assert(0);
#else
	const char *special_defines[30];
	int special_defines_count;
	const char *shader_debug_name;
	char cache_filename[MAX_PATH];
	const MaterialAssemblerProfile *mat_profile = getMaterialProfileForDevice("D3D9");
	FileList file_list = NULL;
	int i;
	char *shader_text;
	const char *intrinsic_defines;
	int timer;

	assert(stricmp(rdrGetDeviceIdentifier(gfx_state.currentDevice->rdr_device), "D3D")==0);  // Must be PC DX renderer

	// gfxDemandLoadShaderGraphAtQueueTime:
	if (shader_num.shaderMask & MATERIAL_SHADER_UBERLIGHT) {
		shader_num.lightMask = 0;
	}
	// gfxDemandLoadShaderGraphInternal:
	if (rdr_state.disableShaderCache)
		return;

	timer = timerAlloc();

	gfxShaderAddDefines(graph_render_info, shader_num, graph_render_info->shader_graph->graph_reflection_type, special_defines, ARRAY_SIZE(special_defines), &special_defines_count);

	shader_debug_name = gfxShaderGetDebugName(graph_render_info->shader_graph->filename, shader_num);
	// Use Xbox defines
	intrinsic_defines = gfx_state.currentDevice->rdr_device->getIntrinsicDefines(DEVICE_XBOX);
	gfxShaderGetCacheName(SAFESTR(cache_filename), shader_debug_name, mat_profile, special_defines, special_defines_count, intrinsic_defines);

	if (dynamicCacheIsFileUpToDateSync_WillStall(gfx_state.shaderCacheXboxOnPC, cache_filename))
	{
		// Already up to date, just touch the timestamp
		dynamicCacheTouchFile(gfx_state.shaderCacheXboxOnPC, cache_filename);
		timerFree(timer);
		return;
	}

	shader_text = gfxShaderGetText(graph_render_info, mat_profile, shader_num, &file_list, true);

	FOR_EACH_IN_EARRAY(graph_render_info->source_files, FileEntry, file_entry)
		FileListInsertInternal(&file_list, file_entry->path, file_entry->date);
	FOR_EACH_END;

	// Compile and save result
	{
		char *programText = strdup(shader_text);
		U32 new_crc = 0;
		XWrapperCompileShaderData data = {0};
		char error_buffer[1024];
		char updb_buffer[MAX_PATH];
		char debug_fn[MAX_PATH];
		char debug_header[1000];
		char compiled_fn[MAX_PATH];
		char tmpFilePath[MAX_PATH];

		error_buffer[0] = 0;

		rdrShaderEnterCriticalSection();
		rdrShaderResetCache(false);

		// Preprocess
		for (i=0; i<special_defines_count; i++)
			genericPreProcAddDefine(special_defines[i]);

		rxbxAddIntrinsicDefinesForXbox();
		rdrPreProcessShader(&programText, "shaders/D3D", shader_debug_name, ".phl", "//", 0, &new_crc, &file_list, debug_fn, debug_header);
		rdrShaderResetCache(false);
		rdrShaderLeaveCriticalSection();

		//createPixelShader(programText, "main_output");
		data.programText = programText;
		data.programTextLen = (int)strlen(programText);
		data.entryPointName = "main_output";
		data.shaderModel = "ps_3_0";
		data.updbPath = updb_buffer;
		data.updbPath_size = ARRAY_SIZE(updb_buffer);
		data.errorBuffer = error_buffer;
		data.errorBuffer_size = ARRAY_SIZE(error_buffer);

		if (rdr_state.writeCompiledShaders)
		{
			if (!strStartsWith(shader_debug_name, "shaders_processed"))
				sprintf(compiled_fn, "shaders_processed/%s.asm.phl", shader_debug_name);
			else
				sprintf(compiled_fn, "%s.asm.phl", shader_debug_name);
			fileLocateWrite(compiled_fn, compiled_fn);
			mkdirtree(compiled_fn);
			data.writeDisassembledPath = compiled_fn;
		}

        fileSpecialDir("shaders_errors/xbox/", SAFESTR(tmpFilePath));
        strcat(tmpFilePath, cache_filename);
        data.errDumpLocation = tmpFilePath;
        mkdirtree(tmpFilePath);

		if (XWrapperCompileShader(&data)) {
			// Success!
			gfxShaderUpdateCache(gfx_state.shaderCacheXboxOnPC, cache_filename, data.updbPath, data.updbData, data.updbDataLen, data.compiledResult, data.compiledResultLen, &file_list);
		} else {
			Errorf("Error compiling Xbox shader %s:\n%s", cache_filename, data.errorBuffer);
		}
		FileListDestroy(&file_list);
		SAFE_FREE(data.compiledResult);
		SAFE_FREE(data.updbData);
		SAFE_FREE(programText);
	}

	assert(!file_list);
	if (shader_text != graph_render_info->shader_text)
		estrDestroy(&shader_text);

	{
		float elapsed = timerElapsed(timer);
		if (elapsed > 1.5f) {
			verbose_printf("Slow shader compile (%1.2fs) on %s\n", elapsed, shader_debug_name);
		}
	}

	timerFree(timer);
#endif
}
