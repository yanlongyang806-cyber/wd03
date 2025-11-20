#include "GfxMaterials.h"
#include "GfxMaterialsOpt.h"
#include "GfxMaterialProfile.h"
#include "GfxMaterialAssembler.h"
#include "GfxTextureTools.h"
#include "GfxPostprocess.h"
#include "GfxConsole.h"
#include "GfxTextures.h"
#include "GfxLightOptions.h"
#include "GraphicsLibPrivate.h"
#include "RdrShader.h"
#include "WorldGrid.h"
#include "../AutoGen/WorldCellEntry_h_ast.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "DynamicCache.h"
#include "RdrState.h"
#include "GfxSky.h"
#include "dynDraw.h"
#include "RdrTexture.h"
#include "wlTerrain.h"

#include "GfxCommonSnap.h"
#include "GfxDebug.h"
#include "GfxEditorIncludes.h"
#include "GfxHeadshot.h"
#include "GfxLoadScreens.h"
#include "GfxTextureTools.h"
#include "SimplygonInterface.h"

#include "autogen/gfxmaterials_h_ast.c"

#include "GenericMeshRemesh.h"

// May also need to increase RDR_SHADER_CACHE_VERSION
#define SHADER_CACHE_VERSION 16 // Increased for bug fix

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););

static void gfxMaterialSetUsageCallback(Material *material, WLUsageFlags usage_flags);

static StashTable stMaterialPerformanceData;
static RdrShaderPerformanceValues **free_performance_data;

// this is currently only used by mapsnaps.  I'm not thrilled with having to put in all these little one-offs, but they seem harmless enough, at this point. [RMARR - 8/16/12]
StashTable g_stTextureOpOverride;

bool shaderDataTypeNeedsMapping(ShaderDataType data_type)
{
	//if (data_type >= SDT_DRAWABLE_START)
	//	return false;
	switch(data_type) {
		//case SDT_TEXTURECUBE:
		case SDT_TEXTURENORMAL_ISDXT5NM:
		case SDT_SCROLL:
		case SDT_SINGLE_SCROLL:
		case SDT_TIMEGRADIENT:
		case SDT_TEXTURE_SCREENCOLOR:
		case SDT_TEXTURE_SCREENCOLORHDR:
		case SDT_TEXTURE_SCREENDEPTH:
		case SDT_TEXTURE_SCREENOUTLINE:
		case SDT_TEXTURE_DIFFUSEWARP:
		case SDT_OSCILLATOR:
		case SDT_ROTATIONMATRIX:
		case SDT_LIGHTBLEEDVALUE:
		case SDT_CHARACTERBACKLIGHTCOLOR:
		case SDT_SKYCLEARCOLOR:
		case SDT_SPECULAREXPONENTRANGE:
		case SDT_FLOORVALUES:
		case SDT_TEXTURE_SCREENCOLOR_BLURRED:
		case SDT_TEXTURE_REFLECTION:
		case SDT_TEXTURE_AMBIENT_CUBE:
		case SDT_PROJECT_SPECIAL:
			return true;
		default:
			return false;
	}
}

AUTO_FIXUPFUNC;
TextParserResult DestroyShaderTemplate(ShaderTemplate* shader_template, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_DESTRUCTOR)
	{
		gfxMaterialsDeinitShaderTemplate(shader_template);
	}
	return PARSERESULT_SUCCESS;
}

bool gfxMaterialsShouldSkipOpInput(const ShaderInput *op_input, const ShaderGraph *graph)
{
	if (!op_input)
		return true;
	if (op_input->data_type == SDT_TEXTURE_DIFFUSEWARP && !gfx_lighting_options.enableDiffuseWarpTex)
		return true;
	if (op_input->data_type == SDT_TEXTURE_AMBIENT_CUBE && !(graph->graph_flags & SGRAPH_USE_AMBIENT_CUBE))
		return true;
	if (op_input->data_type == SDT_TEXTURE_REFLECTION && graph->graph_reflection_type == SGRAPH_REFLECT_NONE)
		return true;
	if (op_input->data_type == SDT_TEXTURE_HEIGHTMAP)
		return true;
	if (op_input->data_type == SDT_HEIGHTMAP_SCALE)
		return true;
	return false;
}

static int clearHandleProcessor(StashElement elem)
{
	ShaderGraphHandleData *handle_data = stashElementGetPointer(elem);
	handle_data->load_state = 0;
	return 1;
}

static __forceinline RoundFloatCountToVec4Count(int num_floats)
{
	return ( num_floats + 3 ) / 4;
}

int RuntimeInputSort( const ShaderRuntimeInput** left, const ShaderRuntimeInput ** right )
{
    int r = (*right)->input_swizzle_count - (*left)->input_swizzle_count;
    if(r)
	    return r;
    return (*right)->input_index - (*left)->input_index;
}

int packRuntimeInputs( ShaderRuntimeInput** runtime_inputs )
{
	int size = eaSize( &runtime_inputs );
	ShaderRuntimeInput** sorted = alloca( sizeof( *sorted ) * size );

	memcpy( sorted, runtime_inputs, sizeof( *sorted ) * size );
	qsort( sorted, size, sizeof( *sorted ), RuntimeInputSort );

	{
		U8* floats_used = alloca( sizeof( U8 ) * size * 4 );
		int it;
		
		memset( floats_used, 0, sizeof( U8 ) * size * 4 );

		for( it = 0; it != size; ++it ) {
			ShaderRuntimeInput* input = sorted[ it ];

			if( input->input_swizzle_count > 4 ) {
				// Since all the multiple-vec inputs are done first,
				// all I need to check for is if any floats are used.
				int freeIt = 0;
				while( floats_used[ freeIt ] != 0 ) {
					++freeIt;
				}

				assert( input->input_swizzle_count % 4 == 0 );

				input->input_register = freeIt;
				input->input_swizzle_start = -1;
				{
					int markIt;
					for( markIt = 0; markIt != input->input_swizzle_count / 4; ++markIt ) {
						assert( floats_used[ freeIt + markIt ] == 0 );
						floats_used[ freeIt + markIt ] = 4;
					}
				}
			} else {
				int freeIt = 0;
				while( floats_used[ freeIt ] + input->input_swizzle_count > 4 ) {
					++freeIt;
				}

				input->input_register = freeIt;
				input->input_swizzle_start = floats_used[ freeIt ];
				floats_used[ freeIt ] += input->input_swizzle_count;
			}
		}

		for( it = 0; it != size * 4; ++it ) {
			if( floats_used[ it ] == 0 ) {
				break;
			}
		}
		return it;
	}
}

static int setupMaterialRenderInfoTex(const MaterialData *material_data, MaterialRenderInfo *render_info, int index, const char *tex_input_name, const char *op_input_name, const ShaderOperation *op, const ShaderInput *op_input)
{
	int ret=1;

	render_info->texture_names[index*2] = allocAddString(op->op_name);
	render_info->texture_names[index*2+1] = allocAddString(op_input_name);
	if (op_input->data_type == SDT_TEXTURE_REFLECTION)
	{
		// Need to bind texture with the right name for world swaps to work
		render_info->textures[index] = texFind(tex_input_name, false);
		if (texFind(tex_input_name, 0) == tex_from_sky_file || !tex_input_name)
		{
			render_info->override_cubemap_texture = render_info->override_spheremap_texture = NULL;
		} else {
			setupMaterialRenderInfoTexReflection(render_info, tex_input_name);
		}
	} else {
		render_info->textures[index] = texFind(tex_input_name, false);
	}
	if (!render_info->textures[index]) {
		render_info->textures[index] = white_tex;
		ret = 0;
	}
	return ret;
}

static void deallocMaterialRenderInfo(MaterialRenderInfo *render_info)
{
	SAFE_FREE(render_info->textures);
}

static void allocMaterialRenderInfoMem(MaterialRenderInfo *render_info)
{
	int tex_count = render_info->rdr_material.tex_count;
	int domain_tex_count = (render_info->rdr_material.flags & RMATERIAL_TESSELLATE?1:0);
	int vec_count = render_info->rdr_material.const_count;
	int const_count = render_info->rdr_material.drawable_const_count;
	size_t tex_size;
	size_t tex_handle_size;
	size_t tessellation_tex_handle_size;
	size_t tex_name_size;
	size_t mapping_size;
	size_t vec_size;
	size_t const_name_size;
	size_t const_size;
	size_t total_size;
	size_t tessellation_material_size;
	size_t tessellation_const_size;
	char *mem=NULL;

	SAFE_FREE(render_info->textures);
	// Allocate a chunk of memory for the texture list, vector list, and texhandle list
	tex_size = tex_count * sizeof(render_info->textures[0]);
	tex_handle_size = tex_count * sizeof(render_info->rdr_material.textures[0]);
	tex_name_size = 2 * tex_count * sizeof(const char *);
	mapping_size = render_info->constant_mapping_count * sizeof(render_info->constant_mapping[0]);
	vec_size = vec_count * sizeof(render_info->rdr_material.constants[0]);
	assert(vec_size == vec_count * sizeof(Vec4));
	const_size = const_count * sizeof(render_info->rdr_material.drawable_constants[0]);
	const_name_size = render_info->rdr_material.const_count * 4 * sizeof(render_info->constant_names);
	tessellation_tex_handle_size = domain_tex_count * sizeof(render_info->rdr_material.tessellation_material->textures[0]);
	tessellation_material_size = (render_info->rdr_material.flags&RMATERIAL_TESSELLATE ? sizeof(RdrNonPixelMaterial):0);
	tessellation_const_size = (render_info->rdr_material.flags&RMATERIAL_TESSELLATE ? sizeof(Vec4):0);
	total_size = tex_size + tex_handle_size + tex_name_size + mapping_size + vec_size + const_name_size + const_size + tessellation_tex_handle_size + tessellation_material_size + tessellation_const_size;// + heightmap_size;
	if (total_size)
		mem = calloc( total_size, 1 );

	render_info->textures = (BasicTexture **)mem; // Store regardless of tex_count, so that it can be freed!
	mem+=tex_size;

	if (tex_name_size)
		render_info->texture_names = (const char **)mem;
	else 
		render_info->texture_names = NULL;
	mem+=tex_name_size;

	if (tex_handle_size)
		render_info->rdr_material.textures = (TexHandle*)mem;
	else 
		render_info->rdr_material.textures = NULL;
	mem+=tex_handle_size;

	if (mapping_size)
		render_info->constant_mapping = (MaterialConstantMapping*)mem;
	else
		render_info->constant_mapping = NULL;
	mem+=mapping_size;

	if (vec_size)
		render_info->rdr_material.constants = (Vec4*)mem;
	else
		render_info->rdr_material.constants = NULL;
	mem+=vec_size;

	if (const_name_size)
		render_info->constant_names = (const char **)mem;
	else
		render_info->constant_names = NULL;

	mem+=const_name_size;

	if (const_size)
		render_info->rdr_material.drawable_constants = (RdrPerDrawableConstantMapping*)mem;
	else
		render_info->rdr_material.drawable_constants = NULL;

	mem+=const_size;

	// Tessellation related
	if (render_info->rdr_material.flags & RMATERIAL_TESSELLATE) {
		render_info->rdr_domain_material = (RdrNonPixelMaterial *)mem;
		render_info->rdr_material.tessellation_material = render_info->rdr_domain_material;
		mem += tessellation_material_size;
		render_info->rdr_material.tessellation_material = render_info->rdr_domain_material;
		if (tessellation_tex_handle_size) {
			render_info->rdr_domain_material->textures = (TexHandle *)mem;
		} else {
			render_info->rdr_material.tessellation_material = NULL;
		}
		mem += tessellation_tex_handle_size;
		render_info->rdr_domain_material->constants = (Vec4 *)mem;
		mem += tessellation_const_size;
	} else {
		render_info->rdr_material.tessellation_material = NULL;
	}
}

// Takes the value(s) specified in the material and stores it in the format that
//   setupDynamicConstants needs later on.
static void setupConstantMapping(ShaderDataType data_type, MaterialConstantMapping *constant_mapping, Vec4 specific_value, int constant_index, int constant_subindex, const ShaderOperationValues *op_values, int last_tex_idx)
{
	constant_mapping->constant_index = constant_index;
	constant_mapping->constant_subindex = constant_subindex;
	constant_mapping->data_type = data_type;
	switch(data_type) {
	xcase SDT_SCROLL:
		copyVec2(specific_value, constant_mapping->scroll.values);

	xcase SDT_SINGLE_SCROLL:
		constant_mapping->scroll.values[0] = specific_value[0];

	xcase SDT_TIMEGRADIENT:
		constant_mapping->timeGradient.start = materialFindOperationSpecificValueFloat(op_values, "Start", 18);
		constant_mapping->timeGradient.startFade = materialFindOperationSpecificValueFloat(op_values, "StartFade", 19);
		if (constant_mapping->timeGradient.startFade < constant_mapping->timeGradient.start)
			constant_mapping->timeGradient.startFade += 24.f;
		constant_mapping->timeGradient.end = materialFindOperationSpecificValueFloat(op_values, "End", 5);
		constant_mapping->timeGradient.endFade = materialFindOperationSpecificValueFloat(op_values, "EndFade", 6);
		if (constant_mapping->timeGradient.endFade < constant_mapping->timeGradient.end)
			constant_mapping->timeGradient.endFade += 24.f;
		constant_mapping->timeGradient.minimum = materialFindOperationSpecificValueFloat(op_values, "Minimum", 0);
		constant_mapping->timeGradient.maximum = materialFindOperationSpecificValueFloat(op_values, "Maximum", 1);

	xcase SDT_OSCILLATOR:
		constant_mapping->oscillator.frequency = materialFindOperationSpecificValueFloat(op_values, "Frequency", 1);
		constant_mapping->oscillator.amplitude = materialFindOperationSpecificValueFloat(op_values, "Amplitude", 1);
		constant_mapping->oscillator.phase = materialFindOperationSpecificValueFloat(op_values, "Phase", 0);

	xcase SDT_LIGHTBLEEDVALUE:
	{
		float bleed_norm = materialFindOperationSpecificValueFloat(op_values, "LightBleed", 1);
		float bleed_rad = bleed_norm * PI/2;
		constant_mapping->lightBleed[0] = CLAMPF32(atan(bleed_rad), -0.999f, 10.0f);
		constant_mapping->lightBleed[1] = 1.f / (1 + constant_mapping->lightBleed[0]);
		constant_mapping->lightBleed[0] *= constant_mapping->lightBleed[1];
	}

	xcase SDT_FLOORVALUES:
	{
		float x = materialFindOperationSpecificValueFloat(op_values, "FloorX", 1);
		float y = materialFindOperationSpecificValueFloat(op_values, "FloorY", 1);
		if (x==0)
			x = 1;
		if (y==0)
			y = 1;
		constant_mapping->floorValues[0] = x;
		constant_mapping->floorValues[1] = 1.f / x;
		constant_mapping->floorValues[2] = y;
		constant_mapping->floorValues[3] = 1.f / y;
	}

	xcase SDT_SPECULAREXPONENTRANGE:
	{
		F32 range_min = materialFindOperationSpecificValueFloatIndexed(op_values, "SpecExpRange", 0, 0.25f);
		F32 range_max = materialFindOperationSpecificValueFloatIndexed(op_values, "SpecExpRange", 1, 128);
		MAX1(range_min, 0.25f);
		MIN1(range_max, 128);
		MIN1(range_min, range_max);
		constant_mapping->specExpRange[0] = range_min / 128.f;
		constant_mapping->specExpRange[1] = (range_max - range_min) / 128.f;
	}

	xcase SDT_PROJECT_SPECIAL:
	xcase SDT_CHARACTERBACKLIGHTCOLOR:
	xcase SDT_SKYCLEARCOLOR:
		// Nothing to setup here

	xcase SDT_VOLUMEFOGMATRIX:
	xcase SDT_INVMODELVIEWMATRIX:
		// No initial setup, but does need a Mat4 reserved

	xcase SDT_SUNDIRECTIONVIEWSPACE:
	xcase SDT_SUNCOLOR:
	xcase SDT_MODELPOSITIONVIEWSPACE:
		// No initial setup, but does need a Vec4 reserved

	xcase SDT_TEXTURE_SCREENCOLOR:
	case SDT_TEXTURE_SCREENCOLORHDR:
	case SDT_TEXTURE_SCREENCOLOR_BLURRED:
	//case SDT_TEXTURECUBE:
		// Not reserving a Vec4/constant.  Just storing a texture.
		assert(!specific_value);


	xcase SDT_TEXTURE_SCREENDEPTH:
	case SDT_TEXTURE_SCREENOUTLINE:
	case SDT_TEXTURE_DIFFUSEWARP:
	case SDT_TEXTURE_REFLECTION:
	case SDT_TEXTURE_AMBIENT_CUBE:
		// Not reserving a Vec4/constant.  Just storing a texture.
		assert(!specific_value);

	xcase SDT_ROTATIONMATRIX:
		constant_mapping->uvrotation.scale[0] = materialFindOperationSpecificValueFloatIndexed(op_values, "Scale", 0, 1);
		constant_mapping->uvrotation.scale[1] = materialFindOperationSpecificValueFloatIndexed(op_values, "Scale", 1, 1);
		constant_mapping->uvrotation.rotation = materialFindOperationSpecificValueFloat(op_values, "Rotation", 1) * (PI/0.5);
		constant_mapping->uvrotation.rotationRate = materialFindOperationSpecificValueFloat(op_values, "RotationRate", 0);

	xcase SDT_TEXTURENORMAL_ISDXT5NM:
		constant_mapping->dxt5nm_index = last_tex_idx; // index to our paired DXT5nm texture

	xdefault:
		assertmsg(0, "Got a unknown ShaderDataType without code mapping");
	}
}

static ShaderOutput *findOutputByName(const ShaderOperationDef *op_def, const char *output_name)
{
	int i = materialFindOutputIndexByName(op_def, output_name);
	if (i==-1)
		return NULL;
	return op_def->op_outputs[i];
}

// Given a ShaderOp and the name of an input, what alpha value might be returned from it
static int shaderOpInputAlpha(const MaterialData *material, const ShaderTemplate *templ, const ShaderOperation *op, const char *input_name, int alpha_input_channel, int infinite_loop_check)
{
	const MaterialFallback* fallback = materialDataHasShaderTemplate(material, templ->template_name);
	int i;
	const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
	ShaderInput *op_input=NULL;
	const ShaderOperationSpecificValue *value;
	int value_num_floats;

	if( !fallback )
		return 1;

	if (infinite_loop_check<=0)
		return 1;

	if (!op_def)
		return 1;

	// Find the input
	i = materialFindInputIndexByName(op_def,  input_name);
	if (i!=-1) {
		op_input = op_def->op_inputs[i];
	} else {
		// This input doesn't exist!  Error!
		Errorf("Shader alpha graph traversal error");
		return 1; // Assume opaque
	}


	// Find the appropriate input for this operation
	value = materialFindOperationSpecificValue2Const(materialFindOperationValuesConst(material, fallback, op->op_name), op_input->input_name);
	value_num_floats = value?(eafSize(&value->fvalues)):0;

	if (op_input->data_type == SDT_TEXTURE ||
		op_input->data_type == SDT_TEXTURENORMAL ||
		op_input->data_type == SDT_TEXTURENORMALDXT5NM ||
		op_input->data_type == SDT_TEXTURECUBE ||
		op_input->data_type == SDT_TEXTURE3D)
	{
		BasicTexture *texbind=NULL;
		// Look up what texture we're using, and return it's alpha value!

		if (alpha_input_channel < 3)
			return 0; // pulling alpha from a color channel

		// Setup texture inputs
		if (!value) {
			// Use default
			if (eaSize(&op_input->input_default.default_strings)==op_input->num_texnames) {
				assert(op_input->num_texnames==1); // Texture op definitions must have exactly 1 texture name
				texbind = texFind(op_input->input_default.default_strings[0], false);
			}
		} else {
			// Use this value
			if (eaSize(&value->svalues)==op_input->num_texnames) {
				assert(op_input->num_texnames==1); // Texture op definitions must have exactly 1 texture name
				texbind = texFind(value->svalues[0], false);
			}
		}
		if (!texbind)
			return 1; // Assume white texture has no alpha
		// TODO: Actually store an alpha value for TexWords, instead of assuming they have alpha...
		return !(texbind->flags & TEX_ALPHA || texGetTexWord(texbind));
	} else {
		// Assume a color input, if constant or a parameter, look at [3], otherwise recurse

		if (op_input->num_floats) {
			const ShaderInputEdge *input_edge;
			const ShaderFixedInput *fixed_input;
			// Does this ShaderInput have a ShaderInputEdge
			input_edge = materialFindInputEdgeByNameConst(op, op_input->input_name);
			fixed_input = materialFindFixedInputByNameConst(op, op_input->input_name);
			if (!input_edge) {
				if (fixed_input) {
					if (eafSize(&fixed_input->fvalues)==4) {
						return fixed_input->fvalues[3];
					} else if (eafSize(&fixed_input->fvalues)==1) {
						return fixed_input->fvalues[0];
					} else {
						return 1;
					}
				} else if (op_input->input_default.default_type == SIDT_NODEFAULT) {
					// No default value, Material *must* define a value
					if (/*!value || */ value_num_floats!=op_input->num_floats) {
						// Missing input
						// Use 1, 1, 1, 1
						return 1;
					} else {
						// Use the specified value
						if (op_input->num_floats==4) {
							return value->fvalues[3];
						} else if (op_input->num_floats == 1) {
							return value->fvalues[0];
						} else {
							return 1;
						}
					}
				} else {
					// Has a default, this will be compiled into the shader
					if (eafSize(&op_input->input_default.default_floats)==4) {
						return op_input->input_default.default_floats[3];
					} else if (eafSize(&op_input->input_default.default_floats)==1) {
						return op_input->input_default.default_floats[0];
					} else if (op_input->input_default.default_type != SIDT_VALUE) {
						return 0;
					} else {
						return 1;
					}
				}
			} else {
				// Has a specific input edge, recurse!
				// Find corresponding output
				int source_alpha_channel = input_edge->input_swizzle[alpha_input_channel];
				const ShaderOperation *source_op = materialFindOpByName((ShaderGraph*)&templ->graph_parser, input_edge->input_source_name);
				const ShaderOperationDef *source_op_def;
				int bIsAlphaAnded;
				ShaderOutput *source_output;
				int ret;
				if (op_input->num_floats==1) {
					// This input is a single channel, so we don't want to call shaderOpInputAlpha() since it only looks at the alpha channel
					// Assumed any dynamic operation attached to this might generate something other than 1.0 in the R channel
					return 0;
				}
				if (!source_op) {
					// Bad graph
					return 1;
				}

				source_op_def = GET_REF(source_op->h_op_definition);

				// Take product of all inputs that correspond to that output
				source_output = findOutputByName(source_op_def, input_edge->input_source_output_name);
				if (!source_output) {
					// Bad graph
					return 1;
				}

				bIsAlphaAnded = source_output->output_alpha_mode_and;
				ret = !bIsAlphaAnded;

				for (i=eaSize(&source_output->output_alpha_from)-1; i>=0; i--) {
					const char *input_name2 = source_output->output_alpha_from[i];
					int alphaFromOp = shaderOpInputAlpha(material, templ, source_op, input_name2, source_alpha_channel, infinite_loop_check-1);
					if (!bIsAlphaAnded)
						ret *= alphaFromOp;
					else
						ret |= alphaFromOp; // Alpha Anded means "if a and b have alpha (<1), we have alpha (<1)", so OR the alpha values
				}
				return ret;
			}
		} else {
			// This input takes no floats, assume opaque?
			return 1;
		}
	}
	assert(0);
	return 0;
}

static void gfxMaterialCalcAlphaSortFromData(const MaterialData *material_data, const ShaderTemplate *templ, bool *needs_alpha_sort, bool *has_transparency)
{
	ShaderOperation *root_op;
	int alpha_val;
	int num_ops = eaSize(&templ->graph_parser.operations);

	root_op = materialFindOpByType((ShaderGraph*)&templ->graph_parser, SOT_SINK);

	alpha_val = shaderOpInputAlpha(material_data, templ, root_op, "Alpha", 3, eaSize(&templ->graph_parser.operations)+1);

	if (templ->graph_parser.graph_flags & SGRAPH_NO_ALPHACUTOUT)
	{
		*needs_alpha_sort = !alpha_val;
	} else {
		*needs_alpha_sort = 0;
	}
	if (material_data->graphic_props.flags & (RMATERIAL_ADDITIVE|RMATERIAL_SUBTRACTIVE|RMATERIAL_NOZWRITE))
		*needs_alpha_sort = 1;

	*has_transparency = !alpha_val;
}

static void gfxMaterialCalcAlphaSort(Material *material_header, const MaterialData *material_data, const ShaderTemplate *templ)
{
	bool needs_alpha_sort;
	bool has_transparency;

	// Get the values
	gfxMaterialCalcAlphaSortFromData(material_data, templ, &needs_alpha_sort, &has_transparency);

	// Set the values
	material_header->graphic_props.needs_alpha_sort = needs_alpha_sort;
	material_header->graphic_props.has_transparency = has_transparency;
	material_header->graphic_props.render_info->rdr_material.has_transparency = has_transparency;
}

bool gfxMaterialValidateMaterialData(MaterialData *material_data, ShaderTemplate** overrides)
{
	bool needs_alpha_sort;
	bool has_transparency;
	bool ret = true;
	int i;

	for( i = -1; i < eaSize( &material_data->graphic_props.fallbacks ); ++i ) {
		MaterialFallback* fallback;
		ShaderTemplate* shader_template;

		if( i < 0 ) {
			fallback = &material_data->graphic_props.default_fallback;
		} else {
			fallback = material_data->graphic_props.fallbacks[ i ];
		}
		
		shader_template = materialGetTemplateByNameWithOverrides( fallback->shader_template_name, overrides );
		if (!shader_template)
			continue;
			
		if ((shader_template->graph_parser.graph_flags & SGRAPH_ALPHA_PASS_ONLY))
		{
			gfxMaterialCalcAlphaSortFromData(material_data, shader_template, &needs_alpha_sort, &has_transparency);
			if (!needs_alpha_sort)
			{
				ErrorFilenameGroupRetroactivef(material_data->filename, "Art", 14, 1, 28, 2008, "Material %s is using an Alpha-Only template (%s),\n   but it is opaque, performance will be better using a template allowing opaque materials.", material_data->material_name, fallback->shader_template_name);
				ret = false;
			}
		}
	}
	
	return ret;
}


__forceinline static void setupDrawableConstantMapping(RdrPerDrawableConstantMapping *mapping,
													   ShaderDataType data_type, int constant_index, int constant_subindex)
{
	mapping->data_type = shaderDataTypeToRdrDrawableMaterialParam( data_type );
	mapping->constant_index = constant_index;
	mapping->constant_subindex = constant_subindex;
}

__forceinline static void validateMaterialFlags(RdrMaterialFlags *flags)
{
	if (gfx_state.currentDevice) {
		if (!rdrSupportsFeature(gfxGetActiveDevice(), FEATURE_TESSELLATION)) {
			*flags &= ~RMATERIAL_TESSELLATE;
		}
	}
}

static int setupMaterialRenderInfo(Material *material_header, const MaterialData *material_data, int bInitTextures)
{
	const ShaderTemplate *shader_template = material_data->graphic_props.shader_template;
	const MaterialFallback *fallback = material_data->graphic_props.active_fallback;
	MaterialRenderInfo *render_info = material_header->graphic_props.render_info;
	int ret = 1;
	int i, j, k;
	int tex_count;
	int mapping_count;
	int drawable_mapping_count;
	int per_drawable_constant = 0;
	int num_ops;
	int print_extra_errors=0; // Do not print errors about specifying too much, since the "errors" are a side-effect of the MaterialEditor.  TODO: Do a prune pass to prune all extraneous data from materials

	if (!shader_template->graph->graph_render_info->has_been_preloaded && gfx_state.debug.error_on_non_preloaded_materials) {
		//ErrorFilenameGroupRetroactivef(material_data->filename, "OwnerOnly", 14, 11, 13, 2007,
		//	"Material %s referencing disallowed/non-preloaded template %s", material_data->material_name, shader_template->graph_parser.filename);
		printf("File: %s\nMaterial %s referencing disallowed/non-preloaded template %s\nThis is also caused by character or FX assets referencing a world template.\n", material_data->filename, material_data->material_name, shader_template->graph_parser.filename);
		shader_template = materialGetTemplateByName("ErrorTemplate");
		assertmsg(shader_template, "Failed to find \"ErrorTemplate\" ShaderTemplate.");
	}

	num_ops = eaSize(&shader_template->graph->operations);
	render_info->graph_render_info = shader_template->graph->graph_render_info;
	tex_count = render_info->graph_render_info->num_input_texnames;
	mapping_count = render_info->graph_render_info->num_input_mappings;
	drawable_mapping_count = render_info->graph_render_info->num_drawable_mappings;

	render_info->graph_reflection_type = shader_template->graph->graph_reflection_type;
	render_info->rdr_material.tex_count = tex_count;
	render_info->rdr_material.const_count = render_info->graph_render_info->num_input_vectors;
	render_info->rdr_material.drawable_const_count = drawable_mapping_count;
	render_info->constant_mapping_count = mapping_count;
	render_info->rdr_material.instance_param_index = render_info->graph_render_info->instance_param_index;
	// Set flags
	render_info->rdr_material.flags = material_data->graphic_props.flags;
	if (shader_template->graph->graph_flags & SGRAPH_ALPHA_TO_COVERAGE)
		render_info->rdr_material.flags |= RMATERIAL_ALPHA_TO_COVERAGE;
	else
		assert(!(render_info->rdr_material.flags & RMATERIAL_ALPHA_TO_COVERAGE));

	validateMaterialFlags(&render_info->rdr_material.flags);
	render_info->max_reflect_resolution = material_data->graphic_props.max_reflect_resolution;
	// Allocate a chunk of memory for the texture list, vector list, and texhandle list
	allocMaterialRenderInfoMem(render_info);

	for (i=num_ops-1; i>=0; i--) {
		ShaderOperation *op = shader_template->graph->operations[i];
		const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
		const ShaderOperationValues *op_values = materialFindOperationValuesConst(material_data, fallback, op->op_name);
		float op_uv_scale = 0.0f;

		if (!op_def) // BAD!
			continue;
		for (j=eaSize(&op_def->op_inputs)-1; j>=0; j--) // Must go through inputs backwards (at least tex_count logic for DXT5nm depends on it)
		{
			const ShaderInput *op_input = op_def->op_inputs[j];
			// Find the appropriate input for this operation
			const ShaderOperationSpecificValue *value = materialFindOperationSpecificValue2Const(op_values, op_input->input_name);
			int value_num_floats= value?(eafSize(&value->fvalues)):0;

            if (op_input->data_type == SDT_UV_SCALE) {
				float minVal = FLT_MAX;

				for (k = 0; k < value_num_floats; ++k) {
					MIN1F(minVal, fabsf(value->fvalues[k]));
				}
                if (0.0f < minVal && minVal < FLT_MAX) {
                    op_uv_scale += log2f(minVal);
                }
            } else if (op_input->data_type == SDT_TEXTURE_STRETCH) {
				float maxVal = 0.0f;

				for (k = 0; k < value_num_floats; ++k) {
					MAX1(maxVal, fabsf(value->fvalues[k]));
				}
				if (maxVal > 0.0f) {
					op_uv_scale -= log2f(maxVal);
				}
            }

			// Intercept textures for the domain shader.
			if (op_input->data_type == SDT_TEXTURE_HEIGHTMAP) {
				const char* tex_input_name = op_input->input_default.default_strings[0];
				if (!value || (texFind(value->svalues[0], 0) == tex_use_pn_tris)) {
					render_info->heightmap = NULL;
				} else {
					render_info->heightmap = texFind(value->svalues[0], 0);
				}
			}
			if (op_input->data_type == SDT_HEIGHTMAP_SCALE) {
				render_info->heightmap_scale = value?value->fvalues[0]:op_input->input_default.default_floats[0];
			}

			if (gfxMaterialsShouldSkipOpInput(op_input, shader_template->graph))
				continue;


			if (op_input->data_type == SDT_INVMODELVIEWMATRIX || op_input->data_type == SDT_SUNDIRECTIONVIEWSPACE || op_input->data_type == SDT_MODELPOSITIONVIEWSPACE)
				render_info->rdr_material.flags |= RMATERIAL_NOINSTANCE;
			if (op_input->data_type == SDT_UNIQUEOFFSET && !op->instance_param)
				render_info->rdr_material.flags |= RMATERIAL_NOINSTANCE;
			if (bInitTextures) {
				// Setup texture inputs
				if (!value) {
					// Use default
					if (eaSize(&op_input->input_default.default_strings)!=op_input->num_texnames) {
						for (k=op_input->num_texnames-1; k>=0; k--) {
							bool bFound = false;
							tex_count--;
							if (g_stTextureOpOverride)
							{
								// this case is a hack added for mapsnaps [RMARR - 8/16/12]
								char const * pchTextureName;
								if (stashFindPointer(g_stTextureOpOverride, op->op_name, (void **)&pchTextureName))
								{
									ret &= setupMaterialRenderInfoTex(material_data, render_info, tex_count, pchTextureName, op_input->input_name, op, op_input);
									bFound = true;
								}
							}
							if (!bFound)
								ret &= setupMaterialRenderInfoTex(material_data, render_info, tex_count, "white", "dummy", op, op_input);
						}
					} else {
						for (k=op_input->num_texnames-1; k>=0; k--) {
							tex_count--;
							ret &= setupMaterialRenderInfoTex(material_data, render_info, tex_count, op_input->input_default.default_strings[k], op_input->input_name, op, op_input);
						}
					}
				} else {
					// Use this value
					if (print_extra_errors && (eaSize(&value->svalues)!=op_input->num_texnames) ||
						eaSize(&value->svalues)<op_input->num_texnames)
					{
						for (k=op_input->num_texnames-1; k>=0; k--) {
							tex_count--;
							ret &= setupMaterialRenderInfoTex(material_data, render_info, tex_count, "white", "dummy", op, op_input);
						}
					} else {
						for (k=op_input->num_texnames-1; k>=0; k--) {
							tex_count--;
							ret &= setupMaterialRenderInfoTex(material_data, render_info, tex_count, value->svalues[k], op_input->input_name, op, op_input);
						}
					}
				}
			}

			// Setup Constant inputs
			if (op_input->num_floats) {
				ShaderInputEdge *input_edge;
				ShaderFixedInput *fixed_input;
				ShaderRuntimeInput *runtime_input;
				// Does this ShaderInput have a ShaderInputEdge
				input_edge = materialFindInputEdgeByName(op, op_input->input_name);
				fixed_input = materialFindFixedInputByName(op, op_input->input_name);
				runtime_input = materialFindRuntimeInputByName(op, op_input->input_name);
				if (!input_edge) {
					if (fixed_input) {
						// Nothing here!
					} else if (op_input->input_default.default_type == SIDT_NODEFAULT) {
						static const F32 error_values[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
						const F32* source = NULL;
						
						// No default value, Material *must* define a value
						assert( runtime_input );

						if (value_num_floats != op_input->num_floats &&
							shaderDataTypeNeedsDrawableMapping(op_input->data_type))
						{
							// Okay
							source = error_values;
						} else if (print_extra_errors && (value_num_floats!=op_input->num_floats) ||
								   value_num_floats<op_input->num_floats)
						{
							// Error (checked in materialValidate)
							source = error_values;
						} else {
							source = value->fvalues;
						}

						assert( runtime_input );
						switch(op_input->num_floats) {
							xcase 8:
								copyVec4( &source[0], render_info->rdr_material.constants[ runtime_input->input_register + 0 ]);
								copyVec4( &source[4], render_info->rdr_material.constants[ runtime_input->input_register + 1 ]);
							xcase 12:
								copyVec4( &source[0], render_info->rdr_material.constants[ runtime_input->input_register + 0 ]);
								copyVec4( &source[4], render_info->rdr_material.constants[ runtime_input->input_register + 1 ]);
								copyVec4( &source[8], render_info->rdr_material.constants[ runtime_input->input_register + 2 ]);
							xcase 16:
								copyMat44( (const Vec4*)source, &render_info->rdr_material.constants[ runtime_input->input_register ]);
								
							xdefault:
								assert( runtime_input->input_swizzle_start >= 0 );
								memcpy( &render_info->rdr_material.constants[ runtime_input->input_register ][ runtime_input->input_swizzle_start ],
										source,
										runtime_input->input_swizzle_count * sizeof( F32 ));
						}
						render_info->constant_names[runtime_input->input_register*4
													+ MAX(0, runtime_input->input_swizzle_start)]
							= allocAddString(op->op_name);
						
						// Add per-Drawable constant mappings
						if (shaderDataTypeNeedsDrawableMapping(op_input->data_type))
						{
							setupDrawableConstantMapping( 
								render_info->rdr_material.drawable_constants + per_drawable_constant,
								op_input->data_type, runtime_input->input_register, runtime_input->input_swizzle_start
								);
							++per_drawable_constant;
						}
						// Setup special constant mappings
						if (shaderDataTypeNeedsMapping(op_input->data_type)) {
							mapping_count--;
							setupConstantMapping(op_input->data_type, &render_info->constant_mapping[mapping_count], &render_info->rdr_material.constants[runtime_input->input_register][runtime_input->input_swizzle_start], runtime_input->input_register, runtime_input->input_swizzle_start , op_values, tex_count-1);
						}
					} else {
						// Has a default, this will be compiled into the shader
						// Verify that they didn't specify a value!
						if (value_num_floats && print_extra_errors) {
							ErrorFilenamef(material_data->filename, "Material \"%s\" specifies %d numerical inputs for operation \"%s\", input \"%s\", but the operation has a default, and will be compiled to ignore these inputs.",
								material_data->material_name, value_num_floats, op->op_name, op_input->input_name );
						}
					}
				} else {
					// Has a specific input, no constants used!
					// Verify that they didn't specify a value!
					if (value_num_floats && print_extra_errors) {
						ErrorFilenamef(material_data->filename, "Material \"%s\" specifies %d numerical inputs for operation \"%s\", input \"%s\", but the operation has a specific input from another opration, and will be compiled to ignore these inputs.",
							material_data->material_name, value_num_floats, op->op_name, op_input->input_name );
					}
				}
			} else {
				// This input takes no floats, make sure we weren't passed any
				if (value_num_floats && print_extra_errors) {
					ErrorFilenamef(material_data->filename, "Material \"%s\" specifies %d numerical inputs for operation \"%s\", input \"%s\", but the operation requires no numerical inputs.",
						material_data->material_name, value_num_floats, op->op_name, op_input->input_name );
				}
			}
			if (op_input->num_texnames) {
				// Setup special texture mappings
				if (shaderDataTypeNeedsMapping(op_input->data_type)) {
					mapping_count--;
					setupConstantMapping(op_input->data_type, &render_info->constant_mapping[mapping_count], NULL, tex_count, -1, op_values, tex_count);
				}
			}
		}

		MIN1(render_info->uv_scale, op_uv_scale);
	}

	if (bInitTextures) {
		assert(tex_count == 0);
	}
	assert(mapping_count == 0);

	return ret;
}

void gfxMaterialsInitAlphaSort(Material *material_header)
{
	const MaterialData *material_data;
	if (!material_header->graphic_props.render_info)
		return;
	material_data = materialGetData(material_header);
/* 	if (!material_data->graphic_props.shader_template) */
/* 		gfxMaterialsInitMaterial(material_header, false); */
/* 	assert(material_data->graphic_props.shader_template); */

	{
		const ShaderTemplate *shader_template = material_data->graphic_props.shader_template;
		MaterialRenderInfo *render_info = material_header->graphic_props.render_info;
		gfxMaterialCalcAlphaSort(material_header, material_data, shader_template);
		render_info->rdr_material.need_alpha_sort = materialNeedsAlphaSort(material_header);
		render_info->rdr_material.need_texture_screen_color = shader_template->graph->graph_render_info->need_texture_screen_color;
		render_info->rdr_material.need_texture_screen_color_blurred = shader_template->graph->graph_render_info->need_texture_screen_color_blurred;
		render_info->rdr_material.need_texture_screen_depth = shader_template->graph->graph_render_info->need_texture_screen_depth;
		render_info->rdr_material.alpha_pass_only = !!(shader_template->graph->graph_flags & SGRAPH_ALPHA_PASS_ONLY);
		render_info->rdr_material.no_normalmap = !!(shader_template->graph->graph_flags & SGRAPH_NO_NORMALMAP);
		render_info->rdr_material.no_hdr = !!(shader_template->graph->graph_flags & SGRAPH_NO_HDR);
		render_info->rdr_material.no_tint_for_hdr = !!(shader_template->graph->graph_flags & SGRAPH_NO_TINT_FOR_HDR);
		render_info->rdr_material.need_alpha_sort |= render_info->rdr_material.need_texture_screen_color || render_info->rdr_material.need_texture_screen_depth || render_info->rdr_material.alpha_pass_only || render_info->rdr_material.need_texture_screen_color_blurred;
		copyVec3(material_header->graphic_props.lighting_contribution, render_info->rdr_material.lighting_contribution);
	}
}

static void gfxMaterialsCalcAlphaSort(void)
{
	int i;
	for (i=eaSize(&material_load_info.material_headers)-1; i>=0; i--) {
		gfxMaterialsInitAlphaSort(material_load_info.material_headers[i]);
	}
}

void gfxMaterialsInitShaderTemplate(ShaderTemplate *templ)
{
	int i, j;
	ShaderGraphRenderInfo *graph_render_info;
	if (!templ->graph->graph_render_info) {
		graph_render_info = templ->graph->graph_render_info = calloc(sizeof(*templ->graph->graph_render_info), 1);
		graph_render_info->shader_handles = stashTableCreateFixedSize(64,sizeof(S64));
	} else {
		StashTable saved_handles;
		int saved_preloaded;
		graph_render_info = templ->graph->graph_render_info;
		// Zero everything but the shader handles
		saved_handles = graph_render_info->shader_handles;
		saved_preloaded = graph_render_info->has_been_preloaded;
		ZeroStruct(graph_render_info);
		graph_render_info->has_been_preloaded = saved_preloaded;
		graph_render_info->shader_handles = saved_handles;
		stashForEachElement(graph_render_info->shader_handles, clearHandleProcessor);
	}
	graph_render_info->shader_graph = templ->graph;
	graph_render_info->need_texture_screen_color = 0;
	graph_render_info->need_texture_screen_depth = 0;
	graph_render_info->need_texture_screen_color_blurred = 0;
	graph_render_info->instance_param_index = -1;

	// Count number of texture inputs.
	for (i=eaSize(&templ->graph->operations)-1; i>=0; i--) {
		ShaderOperation *op = templ->graph->operations[i];
		const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
		if (!op_def) // BAD!
			continue;
		for (j=eaSize(&op_def->op_inputs)-1; j>=0; j--) {
			const ShaderInput *op_input = op_def->op_inputs[j];
			if (gfxMaterialsShouldSkipOpInput(op_input, templ->graph))
				continue;
			if (op_input->num_texnames) {
				graph_render_info->num_input_texnames+=op_input->num_texnames;
				if (shaderDataTypeNeedsMapping(op_input->data_type))
					graph_render_info->num_input_mappings++;
				if (op_input->data_type == SDT_TEXTURE_SCREENCOLOR ||
					op_input->data_type == SDT_TEXTURE_SCREENCOLORHDR)
					graph_render_info->need_texture_screen_color = 1;
				if (op_input->data_type == SDT_TEXTURE_SCREENCOLOR_BLURRED)
					graph_render_info->need_texture_screen_color_blurred = 1;
				if (op_input->data_type == SDT_TEXTURE_SCREENDEPTH)
					graph_render_info->need_texture_screen_depth = 1;

				// force to late draw bucket when outline is available
				if (op_input->data_type == SDT_TEXTURE_SCREENOUTLINE)
					graph_render_info->need_texture_screen_depth = 1;
			}
		}
	}

	// Count number of vector inputs
	{
		int drawable_const_count = 0;
		ShaderRuntimeInput** runtime_inputs = NULL;
		bool bDidInstanceParam = false;
		
		for (i=0; i<eaSize(&templ->graph->operations); i++) {
			ShaderOperation *op = templ->graph->operations[i];
			const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
			if (!op_def) // BAD!
				continue;
			for (j=0; j<eaSize(&op_def->op_inputs); j++) {
				const ShaderInput *op_input = op_def->op_inputs[j];
				if (gfxMaterialsShouldSkipOpInput(op_input, templ->graph))
					continue;

				if (op_input->num_floats) {
					ShaderInputEdge *input_edge;
					ShaderFixedInput *fixed_input;
					ShaderRuntimeInput* runtime_input;
					// This operation has vector/float inputs
					// Count then as parameters if there is no default nor node in the graph satisfying them.

					// Does this lack a default? (even if it has one, it might use it's SpecificInput)
					if (op_input->input_default.default_type == SIDT_NODEFAULT) {
						// Look for the specific input for this input
						input_edge = materialFindInputEdgeByName(op, op_input->input_name);
						fixed_input = materialFindFixedInputByName(op, op_input->input_name);
						runtime_input = materialFindRuntimeInputByName(op, op_input->input_name);
						if (!input_edge && !fixed_input) {
							if (!runtime_input) {
								// No default, no link in the graph
								runtime_input = calloc( 1, sizeof( *runtime_input ));

								runtime_input->input_name = op_input->input_name;
								runtime_input->input_register = -1;
								runtime_input->input_swizzle_start = -1;
								runtime_input->input_swizzle_count = op_input->num_floats;

								eaPush( &op->runtime_inputs, runtime_input );	
							}

							if (op->instance_param && !bDidInstanceParam)
							{
								bDidInstanceParam = true;
								runtime_input->input_instance_param = true;
							}
							else
							{
								runtime_input->input_instance_param = false;
							}
							
                            runtime_input->input_index = eaSize(&runtime_inputs);
							eaPush( &runtime_inputs, runtime_input );

							if (shaderDataTypeNeedsDrawableMapping(op_input->data_type))
							{
								++drawable_const_count;
							}
							if (shaderDataTypeNeedsMapping(op_input->data_type))
								graph_render_info->num_input_mappings++;
						}
					}
				}
			}
		}

		graph_render_info->num_input_vectors = packRuntimeInputs( runtime_inputs );
		graph_render_info->num_drawable_mappings = drawable_const_count;
		if (bDidInstanceParam)
		{
			// Find index of instanced parameter
			for (i=eaSize(&runtime_inputs)-1; i>=0; i--)
			{
				if (runtime_inputs[i]->input_instance_param)
				{
					graph_render_info->instance_param_index = runtime_inputs[i]->input_register*4 + runtime_inputs[i]->input_swizzle_start;
					graph_render_info->instance_param_size = runtime_inputs[i]->input_swizzle_count;
				}
			}
		}
		eaDestroy(&runtime_inputs);
	}
}

void gfxMaterialsDeinitShaderTemplate(ShaderTemplate *templ)
{
	if (templ->graph_parser.graph_render_info) {
		stashTableDestroyEx(templ->graph_parser.graph_render_info->shader_handles, NULL, destroyShaderGraphHandleData);
		templ->graph_parser.graph_render_info->shader_handles = NULL;
		templ->graph_parser.graph_render_info->cached_last_key.key = 0;
		templ->graph_parser.graph_render_info->cached_last_value = NULL;
		estrDestroy(&templ->graph_parser.graph_render_info->shader_text);
		estrDestroy(&templ->graph_parser.graph_render_info->shader_text_pre_light);
		memset(templ->graph_parser.graph_render_info, 0xf9, sizeof(*templ->graph_parser.graph_render_info));
		SAFE_FREE(templ->graph_parser.graph_render_info);
	}
}


static void gfxMaterialsDeinitMaterialCallback(MaterialRenderInfo *render_info)
{
	deallocMaterialRenderInfo(render_info);
	free(render_info);
}

void gfxMaterialsDeinitMaterial(Material *material)
{
	gfxMaterialsDeinitMaterialCallback(material->graphic_props.render_info);
}

void gfxMaterialsInitMaterial(Material *material, int bInitTextures)
{
	if (!material->graphic_props.render_info)
		material->graphic_props.render_info = calloc(sizeof(*material->graphic_props.render_info), 1);

 	setupMaterialRenderInfo(material, materialGetData(material), bInitTextures);	
	gfxMaterialsInitAlphaSort(material);
	gfxMaterialSetUsageCallback(material, material->world_props.usage_flags);
	materialReleaseData(material);
}

static void gfxMaterialsFillRenderValues(void)
{
	int i;

	// fill in ShaderTemplate::RenderValues fields
	for (i=eaSize(&material_load_info.templates)-1; i>=0; i--) {
		gfxMaterialsInitShaderTemplate(material_load_info.templates[i]);
	}
}

static void gfxMaterialReloadTemplateCallback(ShaderTemplate *shader_template)
{
	gfxMaterialsInitShaderTemplate(shader_template);
}

static void gfxMaterialReloadMaterialCallback(Material *material)
{
	gfx_state.debug.error_on_non_preloaded_materials = 0;
	rdr_state.echoShaderPreloadLog = 0;
	if (rdr_state.showDebugLoadingPixelShader==2)
		rdr_state.showDebugLoadingPixelShader = 0;
	rdr_state.nvidiaLowerOptimization = rdr_state.nvidiaLowerOptimization_default;
	gfxMaterialsInitMaterial(material, true);
}

static void gfxMaterialSetUsageCallback(Material *material, WLUsageFlags usage_flags)
{
	U32 i;
	MaterialRenderInfo *render_info = material->graphic_props.render_info;

	if (!render_info)
		return;
	
	for (i=0; i<render_info->rdr_material.tex_count; i++) {
		if (render_info->textures && render_info->textures[i]) { // This if should only get hit with terrain stuff
			render_info->textures[i]->use_category |= usage_flags;
		}
	}
}

void gfxMaterialsRecalcAlphaSort(void)
{
	gfxMaterialsCalcAlphaSort();
}

void gfxMaterialsFillSpecificRenderValues(ShaderTemplate *shader_template, Material *material)
{
	// For the material editor

	// Check for shader reload
	if (gfx_state.rendererNeedsShaderReload & gfx_state.currentRendererFlag) {
		if (shader_template)
			shader_template->shader_template_clean = 0;
		if (material) {
			material->graphic_props.material_clean = 0;
		}
	}

	// Check to see if it needs it
	if (shader_template && !shader_template->shader_template_clean) {
		gfxMaterialsInitShaderTemplate(shader_template);
		shader_template->shader_template_clean = 1;
	}
	if (material && (!material->graphic_props.material_clean ||
		// Hacky check for reload changing templates out from under temporary materials
		material->material_data && (
			material->material_data->graphic_props.shader_template != shader_template ||
			material->graphic_props.render_info->graph_render_info != material->material_data->graphic_props.shader_template->graph->graph_render_info
		)
		|| material->graphic_props.lighting_contribution[0] != material->graphic_props.render_info->rdr_material.lighting_contribution[0]
		))
	{
		gfxMaterialsInitMaterial(material, true);
		gfxMaterialsInitAlphaSort(material);
		material->graphic_props.material_clean = 1;
	}
}

static int unloadShaderHandle(StashElement elem)
{
	ShaderGraphHandleData *handle_data = stashElementGetPointer(elem);
	if (handle_data && handle_data->load_state) {
		// TODO: Actually free these on all renderers if this is used in production mode
		// Note: right now we re-use this handle
		handle_data->graph_data_last_used_time_stamp = 0; // Force it to get reloaded this frame if requested
		handle_data->load_state = 0;
	}
	return 1;
}

void gfxMaterialTemplateUnloadShaders(ShaderTemplate *templ)
{
	ShaderGraphRenderInfo *graph_render_info;
	if (!templ || !templ->graph)
		return;

	graph_render_info = templ->graph->graph_render_info;
	if (graph_render_info)
	{
		stashForEachElement(graph_render_info->shader_handles, unloadShaderHandle);
		graph_render_info->cached_last_key.key = 0;
		graph_render_info->cached_last_value = NULL;
		estrDestroy(&graph_render_info->shader_text);
		estrDestroy(&graph_render_info->shader_text_pre_light);
		graph_render_info->shader_text_post_light = NULL;
		graph_render_info->shader_text_per_light = NULL;
		graph_render_info->shader_text_shadow_buffer = NULL;
	}
}

void gfxMaterialsReloadShaders(void)
{
	int i;
	for (i=eaSize(&material_load_info.templates)-1; i>=0; i--) {
		gfxMaterialTemplateUnloadShaders(material_load_info.templates[i]);
	}
	dynDrawClearPreSwapped();
	gfx_state.shadersReloadedThisFrame = 2;
}


void gfxMaterialTemplateUnloadShaderText(ShaderTemplate *templ)
{
	ShaderGraphRenderInfo *graph_render_info;
	if (!templ)
		return;

	graph_render_info = templ->graph->graph_render_info;
	if (graph_render_info)
	{
		estrDestroy(&graph_render_info->shader_text);
		estrDestroy(&graph_render_info->shader_text_pre_light);
		graph_render_info->shader_text_post_light = NULL;
		graph_render_info->shader_text_per_light = NULL;
		graph_render_info->shader_text_shadow_buffer = NULL;
	}
}

void gfxMaterialsFreeShaderText(void)
{
	static bool bDoneOnce=false;
	int i;
	for (i=eaSize(&material_load_info.templates)-1; i>=0; i--) {
		gfxMaterialTemplateUnloadShaderText(material_load_info.templates[i]);
	}
	shaderAssemblerDoneAssembling();
	if (!dynamicCacheFreeRAMCache(gfx_state.shaderCache))
	{
		if (!bDoneOnce)
		{
			if (isProductionMode())
			{
				verbose_printf("Warning: ShaderCache was not RAM cached (very bad for startup perf).\n");
			} else {
				verbose_printf("ShaderCache was not RAM cached (expected if editing shaders/materials/just got latest).\n");
			}
		}
	}
	bDoneOnce = true;
}

static int gfxMaterialMaxReflectionChangedCallback(StashElement elem)
{
	ShaderGraphHandleData *handle_data = stashElementGetPointer(elem);
	if (handle_data && handle_data->load_state) {
		ShaderGraphReflectionType desired = CLAMP(handle_data->graph_render_info->shader_graph->graph_reflection_type,
			gfx_state.debug.forceReflectionLevel, gfx_state.settings.maxReflection);
		if (desired != handle_data->reflectionType)
		{
			handle_data->load_state = 0;
		}
	}
	return 1;
}

void gfxMaterialMaxReflectionChanged(void)
{
	int i;
	for (i=eaSize(&material_load_info.templates)-1; i>=0; i--) {
		ShaderTemplate *templ = material_load_info.templates[i];
		ShaderGraphRenderInfo *graph_render_info;
		graph_render_info = templ->graph->graph_render_info;
		if (graph_render_info)
		{
			stashForEachElement(graph_render_info->shader_handles, gfxMaterialMaxReflectionChangedCallback);
		}
	}
}

static void gfxMaterialsReloadShaderCallback(const char *relpath, int when)
{
	if (strEndsWith(relpath, ".tmp"))
		return;

	gfx_state.debug.error_on_non_preloaded_materials = 0;
	rdr_state.echoShaderPreloadLog = 0;
	if (rdr_state.showDebugLoadingPixelShader==2)
		rdr_state.showDebugLoadingPixelShader = 0;
	rdr_state.nvidiaLowerOptimization = rdr_state.nvidiaLowerOptimization_default;
	rdr_state.noErrorfOnShaderErrors = 1;

	if (relpath && relpath[0]) {
		fileWaitForExclusiveAccess(relpath);
		errorLogFileIsBeingReloaded(relpath);
	}
	gfxMaterialProfileReload();
	gfxMaterialsReloadShaders();
	gfxReloadSpecialShaders();
	gfx_state.rendererNeedsShaderReload = gfx_state.allRenderersFlag;
	if (!gfx_state.debug.suppressReloadShadersMessage)
		gfxStatusPrintf("Material shader files reloaded");
}

void gfxMaterialRebuildTextures(void)
{
	gfxMaterialsFillRenderValues();
}

static void freePerformanceData(void *data)
{
	eaPush(&free_performance_data, data);
}

static void gfxMaterialsReloadedCallback(const char *relpath, int when)
{
	gfx_state.debug.error_on_non_preloaded_materials = 0;
	rdr_state.echoShaderPreloadLog = 0;
	if (rdr_state.showDebugLoadingPixelShader==2)
		rdr_state.showDebugLoadingPixelShader = 0;
	rdr_state.nvidiaLowerOptimization = rdr_state.nvidiaLowerOptimization_default;

	//gfxMaterialsFillRenderValues(); //Happens through individual callbacks

	//gfxMaterialsCalcAlphaSort();
	// Need to re-init all loaded materials, since if a template changed, lots of materials might be invalid in various ways
	FOR_EACH_IN_EARRAY(material_load_info.material_headers, Material, material)
	{
		if (material->graphic_props.render_info) {
			materialUpdateFallback(material);
			gfxMaterialsInitMaterial(material, true);
		}
	}
	FOR_EACH_END;

	stashTableClearEx(stMaterialPerformanceData, NULL, freePerformanceData);

	gfxMaterialsReloadShaders();
	if (!gfx_state.debug.suppressReloadShadersMessage)
		gfxStatusPrintf("Material files reloaded");
}

void gfxMaterialsReloadAll(void)
{
	gfxReloadSpecialShaders();
	gfx_state.rendererNeedsShaderReload = gfx_state.allRenderersFlag;
	gfxMaterialsReloadedCallback("", 0);
}

// Disables errors on non-preloaded shaders and reapplies them
AUTO_COMMAND ACMD_NAME(noPink);
void gfxNoErrorOnNonPreloaded(int disable)
{
	if (disable)
		gfx_state.debug.error_on_non_preloaded_materials = 0;
	else
		gfx_state.debug.error_on_non_preloaded_materials = 1;

	FOR_EACH_IN_EARRAY(material_load_info.material_headers, Material, material)
	{
		if (material->graphic_props.render_info)
			gfxMaterialsInitMaterial(material, true);
	}
	FOR_EACH_END;
}

void gfxNoErrorOnNonPreloadedInternal(int disable)
{
	static bool was_preload_log_enabled=false;
	if (disable) {
		gfx_state.debug.error_on_non_preloaded_materials = 0;
		if (rdr_state.echoShaderPreloadLog)
		{
			was_preload_log_enabled = true;
			rdr_state.echoShaderPreloadLog = 0;
		}
	} else {
		gfx_state.debug.error_on_non_preloaded_materials = 1;
		if (was_preload_log_enabled)
		{
			rdr_state.echoShaderPreloadLog = 1;
			was_preload_log_enabled = false;
		}
	}
}


__forceinline static int cmpLightTypes(RdrLightType light1, RdrLightType light2)
{
	if (light2 == RDRLIGHT_NONE)
		return -1;
	if (light1 == RDRLIGHT_NONE)
		return 1;
	if ((light2 & RDRLIGHT_SHADOWED) != (light1 & RDRLIGHT_SHADOWED))
		return (light2 & RDRLIGHT_SHADOWED) - (light1 & RDRLIGHT_SHADOWED);
	return ((int)light1) - ((int)light2);
}

AUTO_FIXUPFUNC;
TextParserResult LightComboFixup(PreloadedLightCombo* light_combo, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_POST_TEXT_READ)
	{
		int i, j;

		for (i = 0; i < ARRAY_SIZE(light_combo->light_type); ++i)
		{
			for (j = i+1; j < ARRAY_SIZE(light_combo->light_type); ++j)
			{
				if (cmpLightTypes(light_combo->light_type[i], light_combo->light_type[j]) > 0)
				{
					RdrLightType temp = light_combo->light_type[i];
					light_combo->light_type[i] = light_combo->light_type[j];
					light_combo->light_type[j] = temp;
					verbose_printf("Warning: misordered light types in LightCombos.txt - %d:%d  %d:%d\n",
						i, j, temp, light_combo->light_type[i]);
				}
			}
		}

		light_combo->light_mask =
			(light_combo->light_type[0] << MATERIAL_SHADER_LIGHT0_OFFSET) |
			(light_combo->light_type[1] << MATERIAL_SHADER_LIGHT1_OFFSET) |
			(light_combo->light_type[2] << MATERIAL_SHADER_LIGHT2_OFFSET) |
			(light_combo->light_type[3] << MATERIAL_SHADER_LIGHT3_OFFSET) |
			(light_combo->light_type[4] << MATERIAL_SHADER_LIGHT4_OFFSET);
	}
	return PARSERESULT_SUCCESS;
}

static bool LightCombosListValidate(PreloadedLightCombosList* light_combos);

AUTO_FIXUPFUNC;
TextParserResult LightCombosListFixup(PreloadedLightCombosList* light_combos, enumTextParserFixupType eType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	if (eType == FIXUPTYPE_POST_TEXT_READ)
	{
		if (IsClient())
		{
			if (!LightCombosListValidate(light_combos))
				ret = PARSERESULT_ERROR;
		}
	}
	return ret;
}

static bool LightCombosListValidate(PreloadedLightCombosList* light_combos)
{
	// Verify all light types match some entry in LightCombos!
	int i, j;
	RdrLightType light_types[MAX_NUM_OBJECT_LIGHTS];
	RdrLight lights[MAX_NUM_OBJECT_LIGHTS];
	RdrLight *light_list[MAX_NUM_OBJECT_LIGHTS];
	bool bErrored=false;
	bool bSuccess=true;
	for (i=0; i<ARRAY_SIZE(lights); i++)
	{
		light_list[i] = &lights[i];
		light_types[i] = 0;
	}

	do 
	{
		// increment lights
		bool bFoundRegular=false;
		bool bFoundShadowBuffer=false;
		bool bBreak=false;
		int idx=0;
		RdrLightType newbase;
		bool bFiveProjectors=false;
		int iLightTypeMax = RDRLIGHT_TYPE_MAX;
		if (!gfx_lighting_options.bRequireProjectorLights)
			iLightTypeMax = RDRLIGHT_SPOT;

		while (light_types[idx] == ((iLightTypeMax - 1) | RDRLIGHT_SHADOWED))
		{
			// roll over
			idx++;
			if (idx == ARRAY_SIZE(lights))
			{
				// All at max
				bBreak = true;
				break;
			}
		}
		if (bBreak)
			break;
		if (light_types[idx] == iLightTypeMax-1)
			newbase = light_types[idx] = RDRLIGHT_DIRECTIONAL|RDRLIGHT_SHADOWED;
		else
			newbase = ++light_types[idx];

		while (idx--)
			light_types[idx] = newbase;

		// We know 5 projector lights plus a shadowed will not be supported (not a possible shader, too many textures)
		{
			int iProj=0;
			int iShadow=0;
			for (i=0; i<ARRAY_SIZE(lights); i++)
			{
				if (rdrGetSimpleLightType(light_types[i]) == RDRLIGHT_PROJECTOR)
					iProj++;
				if (rdrIsShadowedLightType(light_types[i]))
					iShadow++;
			}
			if (iProj == 5 && iShadow)
				bFiveProjectors = true;
		}

		if (bFiveProjectors)
			continue;

		// Check this light type
		for (i=0; i<ARRAY_SIZE(lights); i++)
		{
			if (light_types[i])
			{
				lights[i].light_type = light_types[i];
				light_list[i] = &lights[i];
			} else
				light_list[i] = NULL;
		}
		// Sort lights
		for (i=0; i<ARRAY_SIZE(lights); i++)
		{
			for (j=i+1; j<ARRAY_SIZE(lights); j++)
			{
				if (light_list[j])
				{
					if (cmpLightTypes(light_list[i]->light_type, light_list[j]->light_type) > 0)
					{
						SWAPP(light_list[i], light_list[j]);
					}
				}
			}
		}

		FOR_EACH_IN_EARRAY(light_combos->combos, PreloadedLightCombo, light_combo)
		{
			if (rdrDrawListLightsFit(light_list, light_combo, false))
				bFoundRegular = true;
			if (rdrDrawListLightsFit(light_list, light_combo, true))
				bFoundShadowBuffer = true;

			if (bFoundRegular && bFoundShadowBuffer)
				break;
		}
		FOR_EACH_END;
		if (!bFoundRegular || !bFoundShadowBuffer)
		{
			if (!bErrored)
			{
				bErrored = true;
				ErrorFilenamef("client/LightCombos.txt", "LightCombos does not specify combinations that handle all light types.  See console output for uncovered light types.");
				bSuccess = false;
			}
			printf("Uncovered combination: ");
			for (i=0; i<ARRAY_SIZE(lights); i++)
			{
				if (light_list[i] && light_list[i]->light_type)
				{
					printf("L%d:%s%s ", i, StaticDefineIntRevLookup(RdrLightTypeEnum, rdrGetSimpleLightType(light_list[i]->light_type)),
						(light_list[i]->light_type & RDRLIGHT_SHADOWED)?" Shadowed":"");
				}
			}
			printf("\n");
		}
			
	} while (true);

	return bSuccess;
}

// Don't load from .bin if the source exists in production mode
#define LIGHTCOMBOS_TXT "client/LightCombos.txt"
#define LIGHTCOMBOS_BIN ((isProductionMode() && fileExists(LIGHTCOMBOS_TXT))?NULL:"LightCombos.bin")

void gfxMaterialsReloadLightCombosCallback(const char *relpath, int when)
{
	extern void lightComboClearCache(void);
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);

	StructDeInitVoid(parse_PreloadedLightCombosList, &preloaded_light_combos);
	ParserLoadFiles(NULL, LIGHTCOMBOS_TXT, LIGHTCOMBOS_BIN, 0, parse_PreloadedLightCombosList, &preloaded_light_combos);
	lightComboClearCache();

	gfxStatusPrintf("LightCombos.txt reloaded");
}

void gfxLoadMaterials(void) // Material definitions were loaded in WorldLib, fill in render stuff
{
	char filename[MAX_PATH];
	int merged=0;
	loadstart_printf("Initializing Materials...");

	materialVerifyObjectMaterialDepsForFxDone(); // Dynamics should have been loaded before this, free the cache
	materialAddCallbacks(gfxMaterialsReloadedCallback, gfxMaterialReloadMaterialCallback, gfxMaterialReloadTemplateCallback, gfxMaterialSetUsageCallback, NULL); // Last one already set, cannot set twice
	gfxMaterialsFillRenderValues();
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "shaders/*", gfxMaterialsReloadShaderCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "shaders_override/*", gfxMaterialsReloadShaderCallback);

	assert(!gfx_state.shaderCache);
	assert(!gfx_state.shaderCacheXboxOnPC);

	ParserLoadFiles(NULL, LIGHTCOMBOS_TXT, LIGHTCOMBOS_BIN, 0, parse_PreloadedLightCombosList, &preloaded_light_combos);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, LIGHTCOMBOS_TXT, gfxMaterialsReloadLightCombosCallback);

	loadstart_printf("Initializing shader cache...");

    // Xbox still writes to "shaderCache" locally in it's cache, but merges in from "shaderCacheXbox"
	sprintf(filename, "%s/shaderCache.hogg", fileCacheDir());
	if (rdr_state.wipeShaderCache)
		fileForceRemove(filename);

#if _XBOX
    #define SRC_CACHE "platformbin/Xbox/shaderbin/shaderCache.hogg"
#else
    #define SRC_CACHE "platformbin/PC/shaderbin/shaderCache.hogg"
#endif

#ifdef SLOW_CACHE_MERGE
	gfx_state.shaderCache = dynamicCacheCreate(filename,
		SHADER_CACHE_VERSION, 48*1024*1024, 64*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
	merged = dynamicCacheMergePrecise(gfx_state.shaderCache, SRC_CACHE, false);
#else
	if (!rdr_state.compile_all_shader_types)
	{
		gfx_state.shaderCache = dynamicCacheMergeQuick(filename, SRC_CACHE, &merged,
			SHADER_CACHE_VERSION, -1, -1, -1, DYNAMIC_CACHE_RAM_CACHED);
	} else {
		gfx_state.shaderCache = dynamicCacheMergeQuick(filename, SRC_CACHE, &merged,
			SHADER_CACHE_VERSION, 48*1024*1024, 64*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
	}
#endif

	if (rdr_state.compile_all_shader_types & CompileShaderType_Xbox)
	{
		// Also open the XboxOnPC cache
		sprintf(filename, "%s/shaderCacheXbox.hogg", fileCacheDir());
		if (rdr_state.wipeShaderCache)
			fileForceRemove(filename);

#ifdef SLOW_CACHE_MERGE
		gfx_state.shaderCacheXboxOnPC = dynamicCacheCreate(filename, SHADER_CACHE_VERSION, 48*1024*1024, 64*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
		merged += dynamicCacheMergePrecise(gfx_state.shaderCacheXboxOnPC, "platformbin/Xbox/shaderbin/shaderCache.hogg", false);
#else
		gfx_state.shaderCacheXboxOnPC = dynamicCacheMergeQuick(filename, "platformbin/Xbox/shaderbin/shaderCache.hogg", &merged,
			SHADER_CACHE_VERSION, 48*1024*1024, 64*1024*1024, 1*3600, DYNAMIC_CACHE_DEFAULT);
#endif
    }

    if (rdr_state.compile_all_shader_types) {

        // Save a timestamp, and make sure that all shaders actually referenced are older than this timestamp
	    gfx_state.compile_all_shader_types_timestamp = time(NULL);
	    while (gfx_state.compile_all_shader_types_timestamp == time(NULL))
		    Sleep(1);
    }

	loadend_printf(" done (%d cached shaders, %d merged).", dynamicCacheNumEntries(gfx_state.shaderCache) + 
															(gfx_state.shaderCacheXboxOnPC?dynamicCacheNumEntries(gfx_state.shaderCacheXboxOnPC):0), 
															merged);

	loadend_printf(" done.");
}

// Adds a new global named material parameter to override any set programmatically
AUTO_COMMAND ACMD_NAME(materialParam) ACMD_CATEGORY(Debug) ACMD_CMDLINE;
void gfxMaterialsGlobalParameterAdd(const char *name, const Vec4 value)
{
	bool foundIt=false;
	FOR_EACH_IN_EARRAY(gfx_state.debug.eaNamedConstantOverrides, MaterialNamedConstant, mnc)
	{
		if (stricmp(mnc->name, name)==0) {
			copyVec4(value, mnc->value);
			conPrintf("Updated %s : %f %f %f %f", mnc->name, mnc->value[0], mnc->value[1], mnc->value[2], mnc->value[3]);
			foundIt=true;
			break;
		}
	}
	FOR_EACH_END;
	if (!foundIt) {
		MaterialNamedConstant *mnc = calloc(sizeof(*mnc),1);
		mnc->name=allocAddString(name);
		copyVec4(value, mnc->value);
		eaPush(&gfx_state.debug.eaNamedConstantOverrides, mnc);
		conPrintf("Added %s : %f %f %f %f", mnc->name, mnc->value[0], mnc->value[1], mnc->value[2], mnc->value[3]);
	}
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(materialParam);
void gfxMaterialsGlobalParameterList(void)
{
	conPrintf("Global Material Parameters:");
	FOR_EACH_IN_EARRAY(gfx_state.debug.eaNamedConstantOverrides, MaterialNamedConstant, mnc)
		conPrintf("%s : %f %f %f %f", mnc->name, mnc->value[0], mnc->value[1], mnc->value[2], mnc->value[3]);
	FOR_EACH_END;
	conPrintf("Add with /materialParam Name R G B A (0.0 - 1.0)");
	conPrintf("Remove with /materialParamRemove Name");
}

// Removes a global named material parameter
AUTO_COMMAND ACMD_NAME(materialParamRemove) ACMD_CATEGORY(Debug);
void gfxMaterialsGlobalParameterRemove(const char *name)
{
	bool foundIt=false;
	FOR_EACH_IN_EARRAY(gfx_state.debug.eaNamedConstantOverrides, MaterialNamedConstant, mnc)
	{
		if (stricmp(mnc->name, name)==0) {
			eaFindAndRemoveFast(&gfx_state.debug.eaNamedConstantOverrides, mnc);
			free(mnc);
			conPrintf("Removed %s", name);
			foundIt=true;
			break;
		}
	}
	FOR_EACH_END;
	if (!foundIt) {
		conPrintf("Parameter named %s not found", name);
	}
}

void gfxMaterialsHandleNewDevice(void)
{
	int i;

	// JE: This is probably not needed now because of the gfx_state.client_frame_timestamp++ in gfxHandleNewDevice()
	// Need to change the last_used_timestamps because a shader graph may have been queued
	// for loading when only one device existed, loaded, then removed from the queue, and then
	// a second device was added, which may want to use that shader graph, but will not try to
	// add it to the load queue because that already happened this frame.
	if (gfx_state.client_frame_timestamp==1)
		return; // Call before a frame.
	for (i=eaSize(&material_load_info.templates)-1; i>=0; i--) {
		ShaderTemplate *shader_template = material_load_info.templates[i];
		if (shader_template->graph->graph_render_info) {
			if (gfx_state.client_frame_timestamp ==
				shader_template->graph->graph_render_info->graph_last_updated_time_stamp)
			{
				shader_template->graph->graph_render_info->graph_last_updated_time_stamp--;
			}
		}
	}
}

void gfxMaterialsGetPerformanceValuesEx(ShaderGraphRenderInfo *graph_render_info, RdrShaderPerformanceValues *perf_values, RdrMaterialShader shader_num, bool synch)
{
	if (graph_render_info) {
		bool compilationIsNotFinished;
		bool locked=false;
		if (!gfx_state.currentDevice->rdr_device->is_locked_nonthread)
		{
			locked = true;
			gfxLockActiveDevice();
		}
		do {
			perf_values->shader_handle = gfxMaterialFillShader(graph_render_info, shader_num, 0);
			perf_values->shader_type = SPT_FRAGMENT;
			if (synch)
				gfxDemandLoadMaterials(true);

			compilationIsNotFinished = (rdrShaderGetBackgroundShaderCompileCount() != 0);
			rdrQueryShaderPerf(gfx_state.currentDevice->rdr_device, perf_values);
			perf_values->dynamic_constant_count = graph_render_info->num_input_vectors;
			if (synch)
			{
				rdrFlush(gfx_state.currentDevice->rdr_device, false);
				Sleep(0);

				if( locked ) {
					gfxUnlockActiveDeviceEx( false, true, false );
					gfxLockActiveDeviceEx( true );
				} else {
					gfxLockActiveDeviceEx( true );
					gfxUnlockActiveDeviceEx( false, true, false );
				}
			}
		} while( synch && (compilationIsNotFinished || perf_values->instruction_count == 0) );
		if (locked)
		{
			gfxUnlockActiveDevice();
		}
	}
}


void gfxMaterialsGetPerformanceValues(ShaderGraphRenderInfo *graph_render_info, RdrShaderPerformanceValues *perf_values)
{
	rdr_state.echoShaderPreloadLog = 0;
	gfxMaterialsGetPerformanceValuesEx(graph_render_info, perf_values, getRdrMaterialShader(0, rdrGetMaterialShaderType( RDRLIGHT_DIRECTIONAL, 0 )), false);
}

void gfxMaterialsGetPerformanceValuesSynchronous(ShaderGraphRenderInfo *graph_render_info, RdrShaderPerformanceValues *perf_values)
{
	rdr_state.echoShaderPreloadLog = 0;
	gfxMaterialsGetPerformanceValuesEx(graph_render_info, perf_values, getRdrMaterialShader(0, rdrGetMaterialShaderType( RDRLIGHT_DIRECTIONAL, 0 )), true);
}

void gfxMaterialsGetMemoryUsage(Material *material, U32 *totalMem, U32 *sharedMem)
{
	U32 total=0;
	U32 shared=0;
	int i, j, k;
	const MaterialData *material_data = materialGetData(material);

	if (material_data->graphic_props.active_fallback)
	{
		// TODO: Just change this to look at the MaterialRenderInfo?
		for (i=eaSize(&material_data->graphic_props.active_fallback->shader_values)-1; i>=0; i--) {
			const ShaderOperationValues *op_values = material_data->graphic_props.active_fallback->shader_values[i];
			for (j=eaSize(&op_values->values)-1; j>=0; j--) {
				const ShaderOperationSpecificValue *op_value = op_values->values[j];
				for (k=eaSize(&op_value->svalues)-1; k>=0; k--) {
					const BasicTexture *texbind = texFind(op_value->svalues[k], 0);
					if (texbind && texbind->loaded_data) {
						total += texbind->loaded_data->tex_memory_use[TEX_MEM_LOADING] + texbind->loaded_data->tex_memory_use[TEX_MEM_VIDEO];
						if (strStartsWith(texbind->fullname, "system") || strstri(texbind->fullname, "cubemaps")) {
							shared += texbind->loaded_data->tex_memory_use[TEX_MEM_LOADING] + texbind->loaded_data->tex_memory_use[TEX_MEM_VIDEO];
						}
					}
				}
			}
		}
	}
	*totalMem = total;
	*sharedMem = shared;
}

static int unsetShaderHandleLoadState(void *flagptr, StashElement elem)
{
	ShaderGraphHandleData *handle_data = stashElementGetPointer(elem);
	if (handle_data) {
		handle_data->load_state &= *((int *)flagptr);
	}
	return 1;
}

void gfxMaterialsClearAllForDevice(int rendererIndex)
{
	int i;
	int flag = ~(1<<rendererIndex);
	for (i=eaSize(&material_load_info.templates)-1; i>=0; i--) {
		ShaderTemplate *templ = material_load_info.templates[i];
		if (templ->graph->graph_render_info) {
			stashForEachElementEx(templ->graph->graph_render_info->shader_handles, unsetShaderHandleLoadState, &flag);
		}
	}
}

bool materialHasNamedConstant(Material *material, const char *name)
{
	MaterialRenderInfo *render_info;
	int i;
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	render_info = material->graphic_props.render_info;
	assert(render_info);

	for(i=render_info->rdr_material.const_count*4-1; i>=0; --i) {
		if (render_info->constant_names[i] &&
			(stricmp(name,render_info->constant_names[i]) == 0)) {
			return true;
		}
	}
	
	return false;
}

int materialConstantSwizzleCount( MaterialRenderInfo* renderInfo, int index )
{
	unsigned int it;
	
	assert( renderInfo->constant_names[index]);

	it = index + 1;
	while( it < renderInfo->rdr_material.const_count*4
		   && renderInfo->constant_names[it] == NULL ) {
		++it;
	}
	return it - index;
}

bool materialGetNamedConstantValue(Material *material, const char *name, Vec4 value)
{
	MaterialRenderInfo *render_info;
	int i;
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	render_info = material->graphic_props.render_info;
	assert(render_info);

	for(i=render_info->rdr_material.const_count*4-1; i>=0; --i) {
		if (render_info->constant_names[i] &&
			(stricmp(name,render_info->constant_names[i]) == 0)) {
			int const_idx = i/4;
			int const_swizzle = i%4;
			int const_swizzle_count = materialConstantSwizzleCount( render_info, i );
			
			setVec4(value, 0, 0, 0, 1);
			memcpy(value, &((F32*)render_info->rdr_material.constants)[i], sizeof(F32) * const_swizzle_count);
			return true;
		}
	}
	
	return false;
}

bool gfxMaterialUsesTexture(Material *material, const char *texturename)
{
	MaterialRenderInfo *render_info;
	int i;
	assert(texturename == allocAddString(texturename));
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	render_info = material->graphic_props.render_info;

	for (i=0; i<(int)render_info->rdr_material.tex_count; i++) {
		if (render_info->textures[i]->name == texturename) {
			return true;
		}
	}
	
	return false;
}


//////////////////////////////////////////////////////////////////////////
// preview material

static ShaderInputEdge *graphOpIsInput(ShaderGraph *shader_graph, ShaderOperation *search_op, ShaderOperation *sink_op)
{
	int num_ops = eaSize(&shader_graph->operations);
	int i;

	// Find root if no sink
	if (!sink_op)
		sink_op = materialFindOpByType(shader_graph, SOT_SINK);
	if (!sink_op)
		return NULL;

	// Recurse through each edge
	for (i=0; i<eaSize(&sink_op->inputs); i++) {
		ShaderInputEdge *input_edge = sink_op->inputs[i];
		ShaderOperation *source_op = materialFindOpByName(shader_graph, input_edge->input_source_name);
		if (source_op) {
			ShaderInputEdge *input_edge2;
			if (search_op == source_op) {
				return input_edge;
			}
			// Recurse
			input_edge2 = graphOpIsInput(shader_graph, search_op, source_op);
			if (input_edge2) {
				return input_edge;
			}
		}
	}
	return NULL;
}

static void gfxMatPreviewFreeMaterial(MaterialPreview *preview)
{
	if (preview->preview_material == preview->main_material) {
		// It's just pointing to the main one
	} else {
		// This is a copy, free it
		if (preview->preview_material) {
			gfxMaterialsDeinitMaterial(preview->preview_material);
			StructDestroy(parse_MaterialData, preview->preview_material->material_data);
			preview->preview_material->material_data = NULL;
			materialDestroy(preview->preview_material);
		}
	}
	preview->preview_material = NULL;
}

static void gfxMatPreviewGenMaterial(MaterialPreview *preview, Material *material)
{
	MaterialData *material_data;
 	gfxMatPreviewFreeMaterial(preview);
 	preview->main_material = material;

	preview->preview_material = calloc(sizeof(*preview->preview_material), 1);
	material_data = StructAlloc(parse_MaterialData);
	StructCopyFields(parse_MaterialData, materialGetData(preview->main_material), material_data, 0, 0);

	material_data->graphic_props.shader_template = preview->preview_template;
	material_data->graphic_props.active_fallback = &material_data->graphic_props.default_fallback;
	{
		int it;
		for( it = 0; it != eaSize( &material_data->graphic_props.fallbacks ); ++it ) {
			MaterialFallback* fallback = material_data->graphic_props.fallbacks[ it ];

			if( stricmp( fallback->shader_template_name, preview->fallback_name ) == 0 ) {
				material_data->graphic_props.active_fallback = fallback;
				break;
			}
			
		}
	}
	
	materialUpdateFromData(preview->preview_material, material_data);
	gfxMaterialsFillSpecificRenderValues(preview->preview_template,preview->preview_material);
	materialSetUsage(preview->preview_material, WL_FOR_UTIL);
}


static void gfxMatPreviewFreeTemplate(MaterialPreview *preview)
{
	if (preview->preview_template == preview->main_template) {
		// It's just pointing to the main one
	} else {
		// This is a copy, free it
		if (preview->preview_template) {
			gfxMaterialsDeinitShaderTemplate(preview->preview_template);
			StructDestroy(parse_ShaderTemplate, preview->preview_template);
		}
	}
	preview->preview_template = NULL;
}

static void gfxMatPreviewGenTemplate(MaterialPreview *preview, Material *material, ShaderTemplate *shader_template, ShaderGraph *current_graph, const char *op_name, const char *fallback_name, bool use_lit)
{
	ShaderOperation *selected_op=NULL;

	// Generate a template which causes the selected operation to be output, or
	// just use the default template if the selected operation is a sink.

	// Clean up previous one
	gfxMatPreviewFreeTemplate(preview);
	gfxMatPreviewFreeMaterial(preview);
	preview->main_template = shader_template;
	preview->main_material = material;

	if (!current_graph)
		return;
	
	estrCopy2(&preview->op_name, op_name);
	estrCopy2(&preview->fallback_name, fallback_name);

	selected_op = materialFindOpByName(current_graph, op_name);
	if (!selected_op || !GET_REF(selected_op->h_op_definition) || GET_REF(selected_op->h_op_definition)->op_type == SOT_SINK) {
		preview->preview_template = preview->main_template;
		preview->preview_material = preview->main_material;

		if (!shader_template->shader_template_clean) {
			gfxMaterialsInitShaderTemplate(shader_template);
			shader_template->shader_template_clean = 1;
		}
		return;
	}

	// Build a new one!
	// Find out whether the selected Op should output to Lit or Unlit (anything other than Lit)
	if (use_lit)
	{
		ShaderInputEdge *input_edge = graphOpIsInput(current_graph, selected_op, NULL);
		if (input_edge && stricmp(input_edge->input_name, "LitColor")==0) {
			use_lit = true;
			//printf("LIT\n");
		} else {
			use_lit = false;
			//printf("UNLIT\n");
		}
	}
	// Build new template with a link from the selected op to the output
	{
		ShaderGraph *shader_graph;
		ShaderOperation *op_sink;
		int i;
		preview->preview_template = StructAlloc(parse_ShaderTemplate);
		StructCopyFields(parse_ShaderTemplate, preview->main_template, preview->preview_template, 0, 0);
		shader_graph = preview->preview_template->graph = &preview->preview_template->graph_parser;
		shader_graph->graph_flags |= SGRAPH_NO_CACHING;
		// Find sink
		op_sink = materialFindOpByType(shader_graph, SOT_SINK);
		// Clear previous input edges
		for (i=eaSize(&op_sink->inputs)-1; i>=0; i--) {
			StructDestroy(parse_ShaderInputEdge, op_sink->inputs[i]);
		}
		eaSetSize(&op_sink->inputs, 0);
		// Add the one input edge we want
		eaPush(&op_sink->inputs, StructAlloc(parse_ShaderInputEdge));
		setVec4(op_sink->inputs[0]->input_swizzle, 0, 1, 2, 3);
		op_sink->inputs[0]->input_name = allocAddString(use_lit?"LitColor":"UnlitColor");
		op_sink->inputs[0]->input_source_name = allocAddString(selected_op->op_name);
		op_sink->inputs[0]->input_source_output_name = allocAddString(GET_REF(selected_op->h_op_definition)->op_outputs[0]->output_name);
	}
	if (!shader_template->shader_template_clean) {
		gfxMaterialsInitShaderTemplate(shader_template);
		shader_template->shader_template_clean = 1;
	}
	
	// Build new material pointing to this template
	gfxMatPreviewGenMaterial(preview, material);
}

void gfxMatPreviewUpdate(MaterialPreview *preview, Material *material, ShaderTemplate *shader_template, ShaderGraph *shader_graph, const char *op_name, const char *fallback_name, bool use_lit)
{
	if (gfx_state.shadersReloadedThisFrame)
		shader_template->shader_template_clean = 0;
	if ((shader_template && !shader_template->shader_template_clean) ||
		!preview->op_name ||
		stricmp(op_name, preview->op_name)!=0 ||
		!preview->fallback_name ||
		stricmp(fallback_name, preview->fallback_name))
	{
		// Something's changed, generate new preview shader
		gfxMatPreviewGenTemplate(preview, material, shader_template, shader_graph, op_name, fallback_name, use_lit);
	}
	else if (material && !material->graphic_props.material_clean)
	{
		gfxMatPreviewGenMaterial(preview, material);
	}
	gfxMaterialsFillSpecificRenderValues(shader_template, material);
}

void gfxMatPreviewFreeData(MaterialPreview *preview)
{
	gfxMatPreviewFreeTemplate(preview);
	gfxMatPreviewFreeMaterial(preview);
	if (preview->op_name)
		estrClear(&preview->op_name);
	ZeroStruct(preview);
}

//////////////////////////////////////////////////////////////////////////

bool gfxMaterialCanOcclude(Material *material)
{
	MaterialRenderInfo *render_info;

	// TODO? Move this so it happens as a callback for materialFind?  Or materialGetData?
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	render_info = material->graphic_props.render_info;
	assert(render_info);

	return !render_info->rdr_material.has_transparency && !render_info->rdr_material.need_alpha_sort && !render_info->rdr_material.need_texture_screen_color && !render_info->rdr_material.need_texture_screen_depth && !render_info->rdr_material.need_texture_screen_color_blurred;
}

bool gfxMaterialCheckSwaps(Material *material, const char **eaTextureSwaps, 
						   const MaterialNamedConstant **eaNamedConstants, 
						   const MaterialNamedTexture **eaNamedTextures, 
						   const MaterialNamedDynamicConstant **eaNamedDynamicConstants, 
						   int *texturesNeeded, int *constantsNeeded, int *constantMappingsNeeded,
						   Vec4 instance_param)
{
	int i, num;
	bool ret = false;
	U8 j;
	bool bFoundInstanceParam=false;

	MaterialRenderInfo *render_info;

	// TODO? Move this so it happens as a callback for materialFind?  Or materialGetData?
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	render_info = material->graphic_props.render_info;
	assert(render_info);

	*texturesNeeded = 0;
	*constantsNeeded = 0;
	*constantMappingsNeeded = 0;

	if (render_info->graph_render_info->instance_param_index==-1)
	{
		bFoundInstanceParam = true;
		setVec4(instance_param, 1, 1, 0, 1); // Should not be used anywhere
	}

	num = eaSize(&eaTextureSwaps);
	for (i = num-2; i >= 0; i -= 2)
	{
		char fixedname[MAX_PATH];
		texFixName(eaTextureSwaps[i], SAFESTR(fixedname));
		for (j = 0; j < render_info->rdr_material.tex_count; j++)
		{
			if (stricmp(fixedname, render_info->textures[j]->name)==0)
			{
				*texturesNeeded = render_info->rdr_material.tex_count;
				ret = true;
				i = -1;
				break;
			}
		}
	}

	num = eaSize(&eaNamedTextures);
	for (i = num-1; i >= 0; --i)
	{
		for (j=0; j<render_info->rdr_material.tex_count; j++)
		{
			if (render_info->texture_names[j*2] == eaNamedTextures[i]->op && 
				render_info->texture_names[j*2+1] == eaNamedTextures[i]->input &&
				eaNamedTextures[i]->texture != render_info->textures[j])
			{
				*texturesNeeded = render_info->rdr_material.tex_count;
				ret = true;
				i = -1;
				break;
			}
		}
	}

	if (!(render_info->rdr_material.flags & RMATERIAL_NOTINT))
	{
		int instance_index = render_info->graph_render_info->instance_param_index;
		num = eaSize(&eaNamedConstants);
		for (i = num-1; i >= 0; --i)
		{
			// Check for constants with this name
			for (j=0; j<render_info->rdr_material.const_count*4; j++)
			{
				if (render_info->constant_names[j] == eaNamedConstants[i]->name)
				{
					const F32* namedValue = eaNamedConstants[i]->value;
					const F32* constValue = &((F32*)render_info->rdr_material.constants)[j];
					int swizzleCount = materialConstantSwizzleCount( render_info, j );

					if (j == instance_index)
					{
						// This is the instanced parameter
						// Extract it
						memcpy(instance_param, namedValue, sizeof(F32)*swizzleCount);
						bFoundInstanceParam = true;
					} else {
						if (!sameVecN( namedValue, constValue, swizzleCount ))
						{
							*constantsNeeded = render_info->rdr_material.const_count;
							ret = true;
						}
					}
				}
			}
			// Check for special dynamic constants with this name
			for (j=0; j<render_info->constant_mapping_count && !*constantMappingsNeeded; j++) {
				if (render_info->constant_mapping[j].constant_subindex!=-1 &&
					render_info->constant_names[render_info->constant_mapping[j].constant_index*4+render_info->constant_mapping[j].constant_subindex] == eaNamedConstants[i]->name)
				{
					if (render_info->constant_mapping[j].data_type == SDT_ROTATIONMATRIX)
					{
						*constantMappingsNeeded = render_info->constant_mapping_count;
						ret = true;
					}
				}
			}
		}
	}

	num = eaSize(&eaNamedDynamicConstants);
	for (i = num-1; i >= 0; --i)
	{
		for (j=0; j<render_info->constant_mapping_count; j++)
		{
			if (render_info->constant_mapping[j].data_type == eaNamedDynamicConstants[i]->data_type)
			{
				*constantMappingsNeeded = render_info->constant_mapping_count;
				ret = true;
				i = -1;
				break;
			}
		}
	}

	if (!bFoundInstanceParam)
	{
		// Extract it here for non-swapped materials
		int instance_index = render_info->graph_render_info->instance_param_index;
		const F32* constValue = &((F32*)render_info->rdr_material.constants)[instance_index];
		int swizzleCount = materialConstantSwizzleCount( render_info, instance_index );
		bool bSkipIt=false;
		for (i=0; i<(int)render_info->constant_mapping_count; i++)
		{
			if (render_info->constant_mapping[i].constant_subindex!=-1 &&
				render_info->constant_mapping[i].constant_index*4+render_info->constant_mapping[i].constant_subindex ==
					instance_index)
			{
				// This is a dynamic constant, just use zeroes.
				bSkipIt = true;
			}
		}
		if (!bSkipIt)
		{
			memcpy(instance_param, constValue, sizeof(F32)*swizzleCount);
			for (; swizzleCount<4; swizzleCount++)
				assert(instance_param[swizzleCount] == 0);
		} else {
			assert(sameVec4(instance_param, zerovec4));
		}
	}

	return ret;
}

static const char *cms_scroll, *cms_start, *cms_startFade, *cms_end, *cms_endFade, *cms_minimum, *cms_maximum, *cms_frequency, *cms_amplitude, *cms_phase;
static const char *cms_LightBleed, *cms_SpecularExponentRange, *cms_FloorValues;
const char *cms_scale, *cms_rotation, *cms_rotationRate;

AUTO_RUN;
void setupConstantMappingStrings(void)
{
	cms_scroll = allocAddStaticString("Scroll");
	cms_start = allocAddStaticString("Start");
	cms_startFade = allocAddStaticString("StartFade");
	cms_end = allocAddStaticString("End");
	cms_endFade = allocAddStaticString("EndFade");
	cms_minimum = allocAddStaticString("Minimum");
	cms_maximum = allocAddStaticString("Maximum");
	cms_frequency = allocAddStaticString("Frequency");
	cms_amplitude = allocAddStaticString("Amplitude");
	cms_phase = allocAddStaticString("Phase");

	cms_LightBleed = allocAddStaticString("LightBleed");
	cms_FloorValues = allocAddStaticString("FloorValues");

	cms_SpecularExponentRange = allocAddStaticString("SpecularExponentRange");

	cms_scale = allocAddStaticString("Scale");
	cms_rotation = allocAddStaticString("Rotation");
	cms_rotationRate = allocAddStaticString("RotationRate");
}

void setConstantMapping(MaterialConstantMapping *constant_mapping, const char *constant_name, const Vec4 new_value)
{
	switch (constant_mapping->data_type)
	{
		xcase SDT_SCROLL:
			if (constant_name == cms_scroll)
				copyVec2(new_value, constant_mapping->scroll.values);

		xcase SDT_SINGLE_SCROLL:
			if (constant_name == cms_scroll)
				constant_mapping->scroll.values[0] = new_value[0];

		xcase SDT_TIMEGRADIENT:
			if (constant_name == cms_start)
			{
				constant_mapping->timeGradient.start = new_value[0];
			}
			else if (constant_name == cms_startFade)
			{
				constant_mapping->timeGradient.startFade = new_value[0];
				if (constant_mapping->timeGradient.startFade < constant_mapping->timeGradient.start)
					constant_mapping->timeGradient.startFade += 24.f;
			}
			else if (constant_name == cms_end)
			{
				constant_mapping->timeGradient.end = new_value[0];
			}
			else if (constant_name == cms_endFade)
			{
				constant_mapping->timeGradient.endFade = new_value[0];
				if (constant_mapping->timeGradient.endFade < constant_mapping->timeGradient.end)
					constant_mapping->timeGradient.endFade += 24.f;
			}
			else if (constant_name == cms_minimum)
			{
				constant_mapping->timeGradient.minimum = new_value[0];
			}
			else if (constant_name == cms_maximum)
			{
				constant_mapping->timeGradient.maximum = new_value[0];
			}

		xcase SDT_OSCILLATOR:
			if (constant_name == cms_frequency)
				constant_mapping->oscillator.frequency = new_value[0];
			else if (constant_name == cms_amplitude)
				constant_mapping->oscillator.amplitude = new_value[0];
			else if (constant_name == cms_phase)
				constant_mapping->oscillator.phase = new_value[0];

		xcase SDT_ROTATIONMATRIX:
			if (constant_name == cms_scale)
				copyVec2(new_value, constant_mapping->uvrotation.scale);
			else if (constant_name == cms_rotation)
				constant_mapping->uvrotation.rotation = new_value[0] * (PI/0.5);
			else if (constant_name == cms_rotationRate)
				constant_mapping->uvrotation.rotationRate = new_value[0];

		xcase SDT_LIGHTBLEEDVALUE:
			if (constant_name == cms_LightBleed)
			{
				float bleed_rad = new_value[0] * PI/2;
				constant_mapping->lightBleed[0] = CLAMPF32(atan(bleed_rad), -0.999f, 10.0f);
				constant_mapping->lightBleed[1] = 1.f / (1 + constant_mapping->lightBleed[0]);
				constant_mapping->lightBleed[0] *= constant_mapping->lightBleed[1];
			}

		xcase SDT_FLOORVALUES:
			if (constant_name == cms_FloorValues)
			{
				float x = new_value[0];
				float y = new_value[1];
				if (x==0)
					x = 1;
				if (y==0)
					y = 1;
				constant_mapping->floorValues[0] = x;
				constant_mapping->floorValues[1] = 1.f / x;
				constant_mapping->floorValues[2] = y;
				constant_mapping->floorValues[3] = 1.f / y;
			}

		xcase SDT_SPECULAREXPONENTRANGE:
			if (constant_name == cms_SpecularExponentRange)
			{
				F32 range_min = new_value[0];
				F32 range_max = new_value[1];
				MAX1(range_min, 0.25f);
				MIN1(range_max, 128);
				MIN1(range_min, range_max);
				constant_mapping->specExpRange[0] = range_min / 128.f;
				constant_mapping->specExpRange[1] = (range_max - range_min) / 128.f;
			}
	}
}

// This function is allowed to modify the passed in MaterialDraw
void gfxMaterialApplySwaps(MaterialDraw *draw_material_const, Material *material, const char **eaTextureSwaps, 
						   const MaterialNamedConstant **eaNamedConstants, 
						   const MaterialNamedTexture **eaNamedTextures, 
						   const MaterialNamedDynamicConstant **eaNamedDynamicConstants, 
						   WLUsageFlags use_category)
{
	int i, swaps_applied = 0, num;
	U8 j;
	NOCONST(MaterialDraw)* draw_material = STRUCT_NOCONST(MaterialDraw, draw_material_const);

	MaterialRenderInfo *render_info;

	// TODO? Move this so it happens as a callback for materialFind?  Or materialGetData?
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);

	render_info = material->graphic_props.render_info;
	assert(render_info);

	draw_material->material = material;

	if (draw_material->textures)
	{
		for (j = 0; j < render_info->rdr_material.tex_count; ++j)
			draw_material->textures[j] = render_info->textures[j];

		num = eaSize(&eaNamedTextures);
		for (i = num-1; i >= 0; --i)
		{
			for (j=0; j<render_info->rdr_material.tex_count; j++)
			{
				if (render_info->texture_names[j*2] == eaNamedTextures[i]->op && 
					render_info->texture_names[j*2+1] == eaNamedTextures[i]->input &&
					eaNamedTextures[i]->texture != draw_material->textures[j])
				{
					draw_material->textures[j] = eaNamedTextures[i]->texture;
				}
			}
		}

		num = eaSize(&eaTextureSwaps);
		for (i = num-2; i >= 0; i -= 2)
		{
			char fixedname[MAX_PATH];
			texFixName(eaTextureSwaps[i], SAFESTR(fixedname));
			for (j = 0; j < render_info->rdr_material.tex_count; j++)
			{
				if (draw_material->textures[j] && stricmp(fixedname, draw_material->textures[j]->name)==0)
				{
					draw_material->textures[j] = texFindAndFlag(eaTextureSwaps[i+1], 1, use_category);
				}
			}
		}
	}

	if (draw_material->constants)
	{
		memcpy(draw_material->constants, render_info->rdr_material.constants, render_info->rdr_material.const_count * sizeof(Vec4));

		num = eaSize(&eaNamedConstants);
		for (i = num-1; i >= 0; --i)
		{
			// Check for constants with this name
			for (j=0; j<render_info->rdr_material.const_count*4; j++)
			{
				if (render_info->constant_names[j] == eaNamedConstants[i]->name)
				{
					int swizzleCount = materialConstantSwizzleCount(render_info,j);
					memcpy(&((F32*)draw_material->constants)[j], eaNamedConstants[i]->value,
						   swizzleCount * sizeof( F32 ));
				}
			}
		}

		// Zero out instanced parameter
		if (render_info->graph_render_info->instance_param_index!=-1)
		{
			memset(&((F32*)draw_material->constants)[render_info->graph_render_info->instance_param_index],
				0, render_info->graph_render_info->instance_param_size*sizeof(F32));
		}

		// Zero out all dynamic constants
		for (i=0; i<(int)render_info->constant_mapping_count; i++)
		{
			if (render_info->constant_mapping[i].constant_subindex!=-1)
			{
				int index = render_info->constant_mapping[i].constant_index*4+render_info->constant_mapping[i].constant_subindex;
				int swizzleCount = materialConstantSwizzleCount( render_info, index );
				memset(&(((F32*)draw_material->constants)[index]), 0, swizzleCount * sizeof(F32));
			}
		}
	}

	if (draw_material->constant_mappings)
	{
		memcpy(draw_material->constant_mappings, render_info->constant_mapping, render_info->constant_mapping_count * sizeof(MaterialConstantMapping));

		num = eaSize(&eaNamedConstants);
		for (i = num-1; i >= 0; --i)
		{
			// Check for special dynamic constants with this name
			for (j=0; j<render_info->constant_mapping_count; j++) {
				if (render_info->constant_mapping[j].constant_subindex!=-1 &&
					render_info->constant_names[render_info->constant_mapping[j].constant_index*4+render_info->constant_mapping[j].constant_subindex] == eaNamedConstants[i]->name)
				{
					if (render_info->constant_mapping[j].data_type == SDT_ROTATIONMATRIX)
					{
						setConstantMapping(&draw_material->constant_mappings[j], cms_scale, eaNamedConstants[i]->value);
						setConstantMapping(&draw_material->constant_mappings[j], cms_rotation, &eaNamedConstants[i]->value[2]);
						setConstantMapping(&draw_material->constant_mappings[j], cms_rotationRate, &eaNamedConstants[i]->value[3]);
					}
				}
			}
		}

		num = eaSize(&eaNamedDynamicConstants);
		for (i = num-1; i >= 0; --i)
		{
			for (j=0; j<render_info->constant_mapping_count; j++)
			{
				if (render_info->constant_mapping[j].data_type == eaNamedDynamicConstants[i]->data_type)
					setConstantMapping(&draw_material->constant_mappings[j], eaNamedDynamicConstants[i]->name, eaNamedDynamicConstants[i]->value);
			}
		}
	}

	// TODO(CD) make this affected by the swaps
	draw_material->is_occluder = gfxMaterialCanOcclude(material);
}

static U32 __forceinline gfxShaderGraphGetLastUsedCount(ShaderGraphRenderInfo *graph_render_info) {
	if (!graph_render_info)
		return 0;
	if (graph_render_info->graph_last_used_swap_frame != gfx_state.frame_count) {
		graph_render_info->graph_last_used_count_swapped = (graph_render_info->graph_last_used_swap_frame == gfx_state.frame_count - 1)?
			graph_render_info->graph_last_used_count:
			0;
		graph_render_info->graph_last_used_tricount_swapped = (graph_render_info->graph_last_used_swap_frame == gfx_state.frame_count - 1)?
			graph_render_info->graph_last_used_tricount:
			0;
		graph_render_info->graph_last_used_count = 0;
		graph_render_info->graph_last_used_tricount = 0;
		graph_render_info->graph_last_used_swap_frame = gfx_state.frame_count;
	}
	return graph_render_info->graph_last_used_count_swapped;
}


U32 gfxMaterialScoreFromValuesRaw(U32 tex_count, U32 alu_count, U32 temp_count, U32 dynamic_constant_count)
{
#define MAGIC_RATIO 8
#define UNDER_RATIO_RATE_TEX 1
#define UNDER_RATIO_RATE_ALU 1
#define OVER_RATIO_RATE_TEX 40
#define OVER_RATIO_RATE_ALU (OVER_RATIO_RATE_TEX/MAGIC_RATIO)
#define PARAMETER_RATE 6

#define TEMPORARIES_RATE 4
	U32 tex_cost = tex_count * OVER_RATIO_RATE_TEX;
	U32 alu_cost = alu_count * OVER_RATIO_RATE_ALU;
	U32 over_cost = MAX(alu_cost, tex_cost);
	if (alu_count == 0)
		return 0;
	return over_cost + tex_count * UNDER_RATIO_RATE_TEX + alu_count * UNDER_RATIO_RATE_ALU + temp_count * TEMPORARIES_RATE + dynamic_constant_count * PARAMETER_RATE;
}

U32 gfxMaterialScoreFromValues(const RdrShaderPerformanceValues *perf_values)
{
	return gfxMaterialScoreFromValuesRaw(perf_values->texture_fetch_count, perf_values->instruction_count - perf_values->texture_fetch_count, perf_values->temporaries_count, perf_values->dynamic_constant_count);
}

// Returns a relative score in the 0 to 1 range
// This function is allowed to modify the material
F32 gfxMaterialGetPerformanceScore(const Material *material)
{
	F32 ret;
	StashElement element;
	RdrShaderPerformanceValues *perf_values;
	ShaderGraphRenderInfo *graph_render_info;
	if (!stMaterialPerformanceData) {
		stMaterialPerformanceData = stashTableCreateAddress(64);
	}
	rdr_state.disableShaderProfiling = 0;
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial((Material*)material, true); // This modifies the material
	if (!material->graphic_props.render_info)
		return 0;
	if (!(graph_render_info = material->graphic_props.render_info->graph_render_info))
		return 0;
	if (!stashFindElement(stMaterialPerformanceData, graph_render_info, &element)) {
		perf_values = eaPop(&free_performance_data);
		if (!perf_values)
			perf_values = malloc(sizeof(*perf_values));
		ZeroStruct(perf_values);
		stashAddressAddPointer(stMaterialPerformanceData, graph_render_info, perf_values, false);
	} else {
		perf_values = stashElementGetPointer(element);
	}
	assert(perf_values);
	if (perf_values->instruction_count == 0) {
		gfxMaterialsGetPerformanceValuesEx(graph_render_info, perf_values, getRdrMaterialShader(MATERIAL_RENDERMODE_OPAQUE_AFTER_ZPRE | MATERIAL_SHADER_SHADOW_BUFFER, rdrGetMaterialShaderType(RDRLIGHT_DIRECTIONAL, 0)) , false);
	}
	if (gfx_state.debug.show_material_cost == 2) {
		ret = perf_values->instruction_count / 120.f;
	} else if (gfx_state.debug.show_material_cost == 3) {
		ret = perf_values->texture_fetch_count / 12.f;
	} else {
		ret = gfxMaterialScoreFromValues(perf_values) / 1000.f;
	}
	ret = CLAMPF32(ret, 0, 1);
	return ret;
}

typedef struct ShaderUsageDetailed
{
	int count;
	ShaderGraphUsageEntry ***list;
	U32 recent_time;
} ShaderUsageDetailed;

typedef struct ShaderUsageDetailedPair
{
	ShaderUsageDetailed *usage;
	ShaderGraphUsageEntry *entry;
} ShaderUsageDetailedPair;


static int getPerfCallback(ShaderUsageDetailedPair *pair, StashElement element)
{
	ShaderGraphHandleData *handle_data = stashElementGetPointer(element);
	U32 pixels=0;
	if (gfx_state.client_frame_timestamp - handle_data->graph_data_last_used_time_stamp > pair->usage->recent_time) {
		return 1;
	}
	if (!handle_data->performance_values) {
		handle_data->performance_values = calloc(sizeof(*handle_data->performance_values), 1);
	}
	gfxMaterialsGetPerformanceValuesEx(handle_data->graph_render_info->shader_graph->graph_render_info, handle_data->performance_values, handle_data->shader_num, false);
	pair->entry->variationCount++;
	if (handle_data->performance_values->pixel_count) {
		pixels = handle_data->performance_values->pixel_count;
		pair->entry->instruction_count += handle_data->performance_values->instruction_count * pixels;
		pair->entry->texture_fetch_count += handle_data->performance_values->texture_fetch_count * pixels;
		pair->entry->temporaries_count += handle_data->performance_values->temporaries_count * pixels;
		pair->entry->dynamic_constant_count += handle_data->performance_values->dynamic_constant_count * pixels;
		pair->entry->pixels += pixels;
	}
	return 1;
}

static void gfxMaterialGetShaderUsageDetailedCallback(ShaderUsageDetailed *usage, ShaderGraph *shader_graph)
{
	ShaderGraphUsageEntry *entry;
	ShaderUsageDetailedPair pair = {0};
	U32 count;
	if (!shader_graph->graph_render_info)
		return;
	count = gfxShaderGraphGetLastUsedCount(shader_graph->graph_render_info);
	if (!count)
		return;
	if (usage->count < eaSize(usage->list)) {
		entry = eaGet(usage->list, usage->count);
	} else {
		entry = StructCreate(parse_ShaderGraphUsageEntry);
		eaPush(usage->list, entry);
	}
	usage->count++;

	ZeroStructForce(entry);
	entry->countInScene = count;
	entry->filename = shader_graph->filename + sizeof("materials/") - 1;
	pair.entry = entry;
	pair.usage = usage;
	stashForEachElementEx(shader_graph->graph_render_info->shader_handles, getPerfCallback, &pair);
	if (entry->pixels) {
		entry->instruction_count /= entry->pixels;
		entry->texture_fetch_count /= entry->pixels;
		entry->temporaries_count /= entry->pixels;
		entry->dynamic_constant_count /= entry->pixels;
		entry->materialScore = gfxMaterialScoreFromValuesRaw(entry->texture_fetch_count, entry->instruction_count - entry->texture_fetch_count, entry->temporaries_count, entry->dynamic_constant_count);
	}
	entry->tricountInScene = shader_graph->graph_render_info->graph_last_used_tricount_swapped;

	entry->factor = (U64)((F32)entry->pixels * (F32)entry->materialScore + entry->tricountInScene * 800.f);

	if (!shader_graph->graph_render_info->performance_values) {
		shader_graph->graph_render_info->performance_values = calloc(sizeof(*shader_graph->graph_render_info->performance_values), 1);
	}
	if (!shader_graph->graph_render_info->performance_values->instruction_count) {
		gfxMaterialsGetPerformanceValues(shader_graph->graph_render_info, shader_graph->graph_render_info->performance_values);
	}
	entry->rawMaterialScore = gfxMaterialScoreFromValues(shader_graph->graph_render_info->performance_values);

}

#define RECENT_TIME	timerCpuSpeed()*1 // how many ticks to look at in order to be considered "recent" for profiling
void gfxMaterialGetShaderUsageDetailed(ShaderGraphUsageEntry ***entry_list)
{
	ShaderUsageDetailed usage = {0};
	usage.count = 0;
	usage.recent_time = RECENT_TIME;
	usage.list = entry_list;
	FOR_EACH_IN_EARRAY(material_load_info.templates, ShaderTemplate, templ)
	{
		if (templ->graph == &templ->graph_parser) { // Not a duplicate
			gfxMaterialGetShaderUsageDetailedCallback(&usage, templ->graph);
		}
	}
	FOR_EACH_END;

	assert(eaSize(usage.list) >= usage.count);
	while (eaSize(usage.list) != usage.count)
		StructDestroy(parse_ShaderGraphUsageEntry, eaPop(usage.list));
}


typedef struct MaterialUsageDetailed
{
	int count;
	MaterialUsageEntry ***list;
	U32 recent_time;
} MaterialUsageDetailed;

static void gfxMaterialGetUsageDetailedCallback(MaterialUsageDetailed *usage, Material *material)
{
	MaterialUsageEntry *entry;
	U32 count = 0;
	if (!material->graphic_props.render_info)
		return;
	if (gfx_state.client_frame_timestamp - material->graphic_props.render_info->material_last_used_time_stamp > usage->recent_time)
		return;
	count = material->graphic_props.render_info->material_last_used_count + 1;
	if (!count)
		return;
	if (usage->count < eaSize(usage->list)) {
		entry = eaGet(usage->list, usage->count);
	} else {
		entry = StructCreate(parse_MaterialUsageEntry);
		eaPush(usage->list, entry);
	}
	usage->count++;

	ZeroStructForce(entry);
	entry->countInScene = count;
	entry->filename = material->material_name;
}

static void gfxMaterialGetShaderUsageDetailedDumy(MaterialUsageDetailed *usage)
{
	MaterialUsageEntry *entry;
	int count = gfx_state.debug.last_frame_counts.unique_materials_referenced - usage->count;
	if (count<=0)
		return;
	if (usage->count < eaSize(usage->list)) {
		entry = eaGet(usage->list, usage->count);
	} else {
		entry = StructCreate(parse_MaterialUsageEntry);
		eaPush(usage->list, entry);
	}
	usage->count++;

	ZeroStructForce(entry);
	entry->countInScene = count;
	entry->filename = "Swapped";
}

void gfxMaterialGetUsageDetailed(MaterialUsageEntry ***entry_list)
{
	MaterialUsageDetailed usage = {0};
	usage.count = 0;
	usage.recent_time = RECENT_TIME;
	usage.list = entry_list;
	FOR_EACH_IN_EARRAY(material_load_info.material_headers, Material, material)
	{
		gfxMaterialGetUsageDetailedCallback(&usage, material);
	}
	FOR_EACH_END;
	gfxMaterialGetShaderUsageDetailedDumy(&usage);

	assert(eaSize(usage.list) >= usage.count);
	while (eaSize(usage.list) != usage.count)
		StructDestroy(parse_MaterialUsageEntry, eaPop(usage.list));
}

void gfxMaterialSelectByFilename(const char *filename)
{
	char fixedname[MAX_PATH];
	ShaderTemplate *templ = NULL;
	if (filename) {
		getFileNameNoExt(fixedname, filename);
		templ = materialGetTemplateByName(fixedname);
	}
	if (templ) {
		rdrDrawListSelectByShaderGraph(templ->graph->graph_render_info);
	} else {
		rdrDrawListSelectByShaderGraph(NULL);
	}
}

static void materialGetTemplatesUsedByMap_checkMaterial(StashTable stInList, ShaderTemplate ***ret, const char *material_name)
{
	Material *material = materialFind(material_name, 0);
	const MaterialData *data = materialGetData(material);
	if (stashAddInt(stInList, data->graphic_props.shader_template, 1, false))
	{
		eaPush(ret, data->graphic_props.shader_template);
	}
	materialReleaseData(material); // Should we be doing this?  Probably gets released later anyway
}

ShaderTemplate **materialGetTemplatesUsedByMap(const char *zmap)
{
	char deps_file[MAX_PATH];
	WorldDependenciesList list = {0};
	StashTable stInList = stashTableCreateAddress(64);
	ShaderTemplate **ret=NULL;
	char base_dir[MAX_PATH];
	worldGetClientBaseDir(zmap, SAFESTR(base_dir));
	sprintf(deps_file, "%s/world_cells.deps", base_dir);
	ParserLoadFiles(NULL, deps_file, NULL, 0, parse_WorldDependenciesList, &list);

	FOR_EACH_IN_EARRAY(list.deps, WorldDependenciesParsed, deps)
	{
		FOR_EACH_IN_EARRAY(deps->material_deps, char, material_name)
		{
			materialGetTemplatesUsedByMap_checkMaterial(stInList, &ret, material_name);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(deps->sky_deps, char, sky_name)
		{
			SkyInfo *sky_info = gfxSkyFindSky(sky_name);
			if (sky_info)
			{
				FOR_EACH_IN_EARRAY(sky_info->skyDomes, SkyDome, sky_dome)
				{
					if (sky_dome->star_field)
					{
						materialGetTemplatesUsedByMap_checkMaterial(stInList, &ret, sky_dome->name);
					} else {
						Model *model = modelFind(sky_dome->name, false, WL_FOR_WORLD);
						if (model)
						{
							ModelLOD *model_lod = modelLODLoadAndMaybeWait(model, 0, true);
							if (model_lod)
							{
								int i;
								for(i=0; i < model_lod->data->tex_count; i++)
								{
									materialGetTemplatesUsedByMap_checkMaterial(stInList, &ret, model_lod->materials[i]->material_name);
								}
							}
						}
					}
				}
				FOR_EACH_END;
			}
		}
		FOR_EACH_END;
	}
	FOR_EACH_END;

	StructDeInit(parse_WorldDependenciesList, &list);
	stashTableDestroy(stInList);
	return ret;
}

// This function is allowed to modify the passed in MaterialDraw
void gfxMaterialDrawFixup(MaterialDraw *draw_const)
{
	NOCONST(MaterialDraw)* draw = STRUCT_NOCONST(MaterialDraw, draw_const);

	// VAS 11/23/10: draw->material could be NULL if this is a test client in world-but-not-graphics-loading mode
	if (draw->tex_count && draw->material && !draw->material->incompatible)
	{
		int new_tex_count;
		if (!draw->material->graphic_props.render_info)
			gfxMaterialsInitMaterial(draw->material, true);
		assert(draw->material->graphic_props.render_info);
		new_tex_count = draw->material->graphic_props.render_info->rdr_material.tex_count;
		if (new_tex_count != draw->tex_count)
		{
			// This MaterialDraw was binned before we moved reflection textures into the material,
			// Add in a dummy reflection texture
			assert(new_tex_count == draw->tex_count + 1);
		}
	}
}


// ----------------------------------------------------------------------
// Material baking for Simplygon reduction stuff
// ----------------------------------------------------------------------

// This just creates a unique string to identify a material and texture swap combination.
static char *createStringForMaterialAndSwaps(Material *material, TextureSwap **eaTextureSwaps) {
	char *es = NULL;
	char *ret = NULL;
	int i = 0;
	estrPrintf(&es, "%s", material->material_name);
	if(eaTextureSwaps) {
		for(i = 0; i < eaSize(&eaTextureSwaps); i++) {
			estrAppend2(&es, "_");
			estrAppend2(&es, eaTextureSwaps[i]->orig_name);
			estrAppend2(&es, "_");
			estrAppend2(&es, eaTextureSwaps[i]->replace_name);
		}
	}
	ret = strdup(es);
	estrDestroy(&es);
	return ret;
}

/// This gets an index into a Simplygon material table for a given material and texture swap combination. It'll create
/// and bake the material if necessary.
int gfxGetSimplygonMaterialIdFromTable(
	SimplygonMaterialTable *table, Material *material,
	TextureSwap **eaTextureSwaps, const char *tempDir, TaskProfile *texExport, 
	WorldClusterGeoStats *inputGeoStats) {

	char *name = createStringForMaterialAndSwaps(material, eaTextureSwaps);
	int id = simplygon_materialTableGetMaterialId(table, name);

	if(id == -1) {
		SimplygonMaterial *sgMat = gfxSimplygonMaterialFromMaterial(material, eaTextureSwaps, NULL, name, tempDir, NULL, NULL);
		id = simplygon_materialTableAddMaterial(
			table, sgMat,
			name, true);
	}

	free(name);

	return id;
}

/// Bake out material and texture swap information for the ModelClusterTextures to be used in a Simplygon remeshing
void gfxMaterialClusterTexturesFromMaterial(
	Material *material,
	RemeshAssetSwap **eaTextureSwaps,
	RemeshAssetSwap **eaMaterialSwaps,
	const char *tempDir,
	ModelClusterTextures ***mcTextures,
	StashTable materialTextures,
	TaskProfile *texExport, 
	WorldClusterGeoStats *inputGeoStats) {

	int j;
	int k;
	int i;
	const MaterialData *data = materialFindData(material->material_name);

	// Handle material swaps here.
	for (i = 0; i < eaSize(&eaMaterialSwaps); i++) {
		if (strcmp(material->material_name,eaMaterialSwaps[i]->orig_name)==0) {
			data = materialFindData(eaMaterialSwaps[i]->replace_name);
			break;
		}
	}

	if(data) {

		const MaterialFallback* defaultFallback = &data->graphic_props.default_fallback;
		ModelClusterTextures *mcTex = calloc(1,sizeof(ModelClusterTextures));

		eaPush(mcTextures,mcTex);

		if(defaultFallback) {

			const ShaderTemplate *shaderTemplate = materialGetTemplateByName(defaultFallback->shader_template_name);

			for (i = 0; i < eaSize(&defaultFallback->shader_values); i++) {

				ShaderOperationValues *values = defaultFallback->shader_values[i];

				for (j = 0; j < eaSize(&defaultFallback->shader_values[i]->values); j++) {

					ShaderOperationSpecificValue *specificVal = defaultFallback->shader_values[i]->values[j];

					for (k = 0; k < eaSize(&specificVal->svalues); k++) {
						BasicTexture *tex;
						char fname[MAX_PATH];
						const char *texName = specificVal->svalues[k];
						int l;

						// Handle texture swaps.
						for (l = 0; l < eaSize(&eaTextureSwaps); l++) {
							if (!strcmp(eaTextureSwaps[l]->orig_name, texName)) {
								texName = eaTextureSwaps[l]->replace_name;
								break;
							}
						}

						if(tempDir) {
							sprintf(fname, "%s\\%s.png", tempDir, texName);
						} else {
							sprintf(fname, "c:\\testModels\\%s.png", texName);
						}

						++inputGeoStats->numTextures;
						// Quick and dirty translation between our material input names and Simplygon's material
						// texture names.
						if (!strcmp(values->op_name, "Diffusemap") ||
							!strcmp(values->op_name, "Texture1_Diffuse")) {
								strcpy(mcTex->gmesh_texture_file_d,fname);
						} else if (
							!strcmp(values->op_name, "Normalmap") ||
							!strcmp(values->op_name, "Texture1_Normal")) {
								strcpy(mcTex->gmesh_texture_file_n,fname);
						} else if (
							!strcmp(values->op_name, "SpecularWithValue") ||
							!strcmp(values->op_name, "Specular")) {
								strcpy(mcTex->gmesh_texture_file_s,fname);
						} else {
							continue;
						}
						mkdirtree(fname);

						tex = texFindAndFlag(texName, true, WL_FOR_WORLD);
						if (materialTextures) {
							texName = allocAddString(texName);
							stashAddPointer(materialTextures,texName,texName,false);	// store the textures to save them out later.
						} else {
							gfxSaveTextureAsPNG(texName, fname, true, texExport);		// If there isn't a way to store the names of the textures, spit them out
						}
					}
				}
			}
		}
	}
}

static RemeshAssetSwap** ConvertMaterialSwapConvertsToRemeshAssetSwap(MaterialSwap **eaMaterialSwaps)
{
	int i;
	RemeshAssetSwap **eaAssetSwap = NULL;

	for (i = 0; i < eaSize(&eaMaterialSwaps); i++) {
		RemeshAssetSwap *assetSwap = calloc(1,sizeof(RemeshAssetSwap));
		assetSwap->orig_name = eaMaterialSwaps[i]->orig_name;
		assetSwap->replace_name = eaMaterialSwaps[i]->replace_name;
		eaPush(&eaAssetSwap,assetSwap);
	}
	return eaAssetSwap;
}

static RemeshAssetSwap** ConvertTextureSwapConvertsToRemeshAssetSwap(TextureSwap **eaTextureSwaps)
{
	int i;
	RemeshAssetSwap **eaAssetSwap = NULL;

	for (i = 0; i < eaSize(&eaTextureSwaps); i++) {
		RemeshAssetSwap *assetSwap = calloc(1,sizeof(RemeshAssetSwap));
		assetSwap->orig_name = eaTextureSwaps[i]->orig_name;
		assetSwap->replace_name = eaTextureSwaps[i]->replace_name;
		eaPush(&eaAssetSwap,assetSwap);
	}
	return eaAssetSwap;
}

/// Convert a material and texture swap combination to a Simplygon material (baking and saving textures for Simplygon to
/// refer to).
SimplygonMaterial *gfxSimplygonMaterialFromMaterial(
	Material *material, TextureSwap **eaTextureSwaps, MaterialSwap **eaMaterialSwaps,
	const char *uniqueName, const char *tempDir,
	int *totalTextureSize, StashTable materialTextures) {

	SimplygonMaterial *sgMat = NULL;
	ModelClusterTextures **mcTextures = NULL;
	RemeshAssetSwap **eaRemeshTexSwaps = ConvertTextureSwapConvertsToRemeshAssetSwap(eaTextureSwaps);
	RemeshAssetSwap **eaRemeshMatSwaps = ConvertMaterialSwapConvertsToRemeshAssetSwap(eaMaterialSwaps);

	sgMat = simplygon_createMaterial();
	gfxMaterialClusterTexturesFromMaterial(material,eaRemeshTexSwaps,eaRemeshMatSwaps,tempDir, &mcTextures,materialTextures, NULL, NULL);
	if (mcTextures) {
		// Referencing indices directly because the above function can only ever create one set of ModelClusterTextures at a time.
		if (mcTextures[0]->gmesh_texture_file_d[0]) {
			simplygon_setMaterialTexture(sgMat, "Diffuse", mcTextures[0]->gmesh_texture_file_d, true);
		}
		if (mcTextures[0]->gmesh_texture_file_s[0]) {
			simplygon_setMaterialTexture(sgMat, "Specular", mcTextures[0]->gmesh_texture_file_s, true);
		}
		if (mcTextures[0]->gmesh_texture_file_n[0]) {
			simplygon_setMaterialTexture(sgMat, "Normal", mcTextures[0]->gmesh_texture_file_n, true);
		}
		eaDestroyEx(&mcTextures,NULL);
	}
	eaDestroyEx(&eaRemeshMatSwaps,NULL);
	eaDestroyEx(&eaRemeshTexSwaps,NULL);

	return sgMat;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void simplygonTestMaterial(ACMD_NAMELIST("Material", REFDICTIONARY) const char *materialName) {

	Material *material = materialFindNoDefault(materialName, WL_FOR_NOTSURE);

	if(material) {

		char *name = createStringForMaterialAndSwaps(material, NULL);
		SimplygonMaterial *mat = gfxSimplygonMaterialFromMaterial(material, NULL, NULL, name, "c:\\testModels", NULL, NULL);

		simplygon_destroyMaterial(mat);

		free(name);
	}
}

bool gfxMaterialHasTransparency(Material *material)
{
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);
	return material->graphic_props.render_info->rdr_material.has_transparency;
}

int gfxMaterialGetTextures(Material* material, const BasicTexture ***textureHolder)
{
	if (!material->graphic_props.render_info)
		gfxMaterialsInitMaterial(material, true);
	*textureHolder = material->graphic_props.render_info->textures;
	return material->graphic_props.render_info->rdr_material.tex_count;
}
