#include "Materials.h"
#include "wlModelData.h"
#include "wlPhysicalProperties.h"
#include "wlState.h"
#include "wlModelInline.h"
#include "wlModelLoad.h"
#include "error.h"
#include "structPack.h"
#include "MemoryBudget.h"
#include "WorldCellEntry.h"
#include "WorldGridLoad.h"
#include "dynFxInfo.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "StringCache.h"
#include "gimmeDLLWrapper.h"
#include "EString.h"
#include "sysutil.h"
#include "UnitSpec.h"
#include "StringUtil.h"
#include "SharedMemory.h"
#include "BitStream.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Materials););
AUTO_RUN_ANON(memBudgetAddMapping("SM_Materials", BUDGET_Materials););

#define MATERIAL_DATA_PACKED_NAME "MaterialDataPacked"

#define MAX_MATERIAL_TEXTURES 9 // Bumped from 8 up to 9 when I rolled the spheremap/cubemap into material textures

bool materialErrorOnMissingFallbacks = false;
AUTO_CMD_INT( materialErrorOnMissingFallbacks, materialErrorOnMissingFallbacks ) ACMD_COMMANDLINE;

void materialVerifyDeps(void);
void materialUnpack(MaterialDataInfo *mdi, MaterialData *material_data);
void materialDataInit(MaterialData *material_data);
static int materialValidateStructural(MaterialData* material);
static MaterialData *materialGetDataNonConst(Material *material_header);


//ShaderOperationDef **op_defs;
DictionaryHandle g_hOpDefsDict;

static StructPackMethod material_pack_method = STRUCT_PACK_BITPACK;

MaterialLoadInfo material_load_info;
static MaterialReloadMaterialCallbackFunc material_reload_material_callback;
static MaterialReloadTemplateCallbackFunc material_reload_template_callback;
static MaterialReloadCallbackFunc *material_reload_callbacks;
static MaterialSetUsageCallbackFunc material_set_usage_callback;
static MaterialValidateMaterialDataCallbackFunc material_validate_material_data_callback;
Material *default_material, *invisible_material;
static ShaderTemplate *default_shader_template;
static bool material_show_extra_warnings=false;
bool g_materials_skip_fixup=false;
static bool material_validate_skip_texture_existence=false;

extern bool gbIgnoreTextureErrors;

extern ParseTable parse_ShaderInputOneLine[];
#define TYPE_parse_ShaderInputOneLine ShaderInputOneLine
extern ParseTable parse_ShaderOutputOneLine[];
#define TYPE_parse_ShaderOutputOneLine ShaderOutputOneLine

// Cannot AUTO_ENUM because it's in RenderLib, which is not accessible from WorldLib on the Server, etc
static StaticDefineInt RdrMaterialFlagsEnum[] = {
	DEFINE_INT
	{ "Additive",			RMATERIAL_ADDITIVE },
	{ "Subtractive",		RMATERIAL_SUBTRACTIVE },
	{ "DoubleSided",		RMATERIAL_DOUBLESIDED },
	{ "NoZWrite",			RMATERIAL_NOZWRITE },
	{ "NoZTest",			RMATERIAL_NOZTEST },
	{ "ForceFarDepth",		RMATERIAL_FORCEFARDEPTH },
	{ "NoFog",				RMATERIAL_NOFOG },
	{ "Backface",			RMATERIAL_BACKFACE },
	{ "NoTint",				RMATERIAL_NOTINT },
	{ "NoOutlines",			RMATERIAL_NONDEFERRED },
	{ "DepthBias",			RMATERIAL_DEPTHBIAS },
	{ "WorldTexCoordsXZ",	RMATERIAL_WORLD_TEX_COORDS_XZ },
	{ "Decal",				RMATERIAL_DECAL },
	{ "NoBloom",			RMATERIAL_NOBLOOM },
	{ "ScreenTexCoords",	RMATERIAL_SCREEN_TEX_COORDS },
	{ "WorldTexCoordsXY",	RMATERIAL_WORLD_TEX_COORDS_XY },
	{ "WorldTexCoordsYZ",	RMATERIAL_WORLD_TEX_COORDS_YZ },
	{ "LowResAlpha",		RMATERIAL_LOW_RES_ALPHA },
	{ "StencilMode0",		RMATERIAL_STENCILMODE0 },
	{ "StencilMode1",		RMATERIAL_STENCILMODE1 },
	{ "StencilMode2",		RMATERIAL_STENCILMODE2 },
	{ "NoColorWrite",		RMATERIAL_NOCOLORWRITE },
	{ "Unlit",				RMATERIAL_UNLIT },
	{ "AlphaNoDOF",			RMATERIAL_ALPHA_NO_DOF },
	DEFINE_END
};

AUTO_STRUCT AST_ENDTOK("\n");
typedef struct MaterialModelDep
{
	char *model_name; AST( STRUCTPARAM POOL_STRING )
	char *material_name; AST( STRUCTPARAM POOL_STRING FILENAME )
} MaterialModelDep;

AUTO_STRUCT AST_STRIP_UNDERSCORES AST_STARTTOK("") AST_ENDTOK("EndMaterialDeps");
typedef struct MaterialDeps {
	const char *dep_filename;	AST( CURRENTFILE )
	const char *src_file;		AST( NAME(GeoFile) POOL_STRING FILENAME )
	const char **dependency;	AST( NAME(MaterialDep) POOL_STRING FILENAME )
	MaterialModelDep **model_dependency; AST( NAME(MaterialModelDep) )
} MaterialDeps;


AUTO_STRUCT AST_FIXUPFUNC(fixupMaterialDepsQuick);
typedef struct MaterialDepsQuickList
{
	MaterialDeps **deps_source; AST( NAME(MaterialDeps) )
	const char **deps_strings; AST( POOL_STRING )
} MaterialDepsQuickList;

// Auto struct stuff:
#include "AutoGen/MaterialEnums_h_ast.c"
#include "AutoGen/Materials_h_ast.c"
#include "AutoGen/Materials_c_ast.c"

// Cannot AUTO_STRUCT because of two parse tables both describing the same struct
ParseTable parse_ShaderInputOneLine[] = {
	{ "Name",				TOK_STRUCTPARAM|TOK_POOL_STRING|TOK_STRING(ShaderInput,input_name,0)},
	{ "Description",		TOK_STRUCTPARAM|TOK_STRING(ShaderInput,input_description,0)},
	{ "_TypeDefault",		TOK_INT(ShaderInput,data_type,	SDT_DEFAULT), ShaderDataTypeEnum },
	{ "_FloatDefault",		TOK_INT(ShaderInput,num_floats,	4) },
	{ "_TextureDefault",	TOK_INT(ShaderInput,num_texnames, 0) },
	{ "_DefaultDefault",	TOK_INT(ShaderInput,input_default.default_type,	SIDT_NODEFAULT), ShaderInputDefaultTypeEnum },

	{ "\n",					TOK_END,			0},
	{ "", 0, 0 }
};

// Cannot AUTO_STRUCT because of two parse tables both describing the same struct
ParseTable parse_ShaderOutputOneLine[] = {
	{ "Name",				TOK_STRUCTPARAM|TOK_POOL_STRING|TOK_STRING(ShaderOutput,output_name,0)},
	{ "Description",		TOK_STRUCTPARAM|TOK_STRING(ShaderOutput,output_description,0)},
	{ "_FloatDefault",		TOK_INT(ShaderOutput,num_floats, 4) },
	{ "_TypeDefault",		TOK_INT(ShaderOutput,data_type,	SDT_DEFAULT) },

	{ "\n",					TOK_END,			0},
	{ "", 0, 0 }
};

ParseTable parse_material_deps_list[] = {
	{ "MaterialDeps",		TOK_STRUCT_X | TOK_EARRAY | TOK_INDIRECT, 0, sizeof(MaterialDeps), parse_MaterialDeps},
	{ "", 0, 0 }
};

AUTO_RUN;
void initParseMaterialDepsList(void)
{
	ParserSetTableInfo(parse_material_deps_list, sizeof(void *), "MaterialDepsList", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_ShaderInputOneLine, sizeof(ShaderInput), "ShaderInputOneLine", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
	ParserSetTableInfo(parse_ShaderOutputOneLine, sizeof(ShaderOutput), "ShaderOutputOneLine", NULL, __FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}

void materialShowExtraWarnings(bool show)
{
	material_show_extra_warnings = show;
}

void materialEnableDiffuseWarp(void)
{
	wl_state.enableDiffuseWarpTex = 1;
}


TextParserResult fixupShaderOperationDef(ShaderOperationDef *op_def, enumTextParserFixupType eFixupType, void *pExtraData)
{
	TextParserResult ret = PARSERESULT_SUCCESS;
	switch (eFixupType)
	{
	xcase FIXUPTYPE_POST_TEXT_READ:
		FOR_EACH_IN_EARRAY(op_def->op_outputs, ShaderOutput, op_output)
		{
			// It's valid not to have an AlphaFrom, it just means that the alpha is always 1.
			// Although, it's good to enable this from time to time to see if anything is
			// missing one that should have one!
			//if (!eaSize(&op_output->output_alpha_from)) {
			//	ErrorFilenamef(op_def->filename, "Output %s is missing AlphaFrom directive", op_output->output_name);
			//	ret = PARSERESULT_ERROR;
			//}
			FOR_EACH_IN_EARRAY(op_output->output_alpha_from, const char , alpha_from)
			{
				bool bFoundOne=false;
				FOR_EACH_IN_EARRAY(op_def->op_inputs, ShaderInput, op_input)
				{
					if (stricmp(op_input->input_name, alpha_from)==0) {
						bFoundOne = true;
						break;
					}
				}
				FOR_EACH_END;
				if (!bFoundOne) {
					ErrorFilenamef(op_def->filename, "Output %s's AlphaFrom specifies input %s which does not exist", op_output->output_name, alpha_from);
					ret = PARSERESULT_ERROR;
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	return ret;
}


static void materialLoadOps(void)
{
	g_hOpDefsDict = RefSystem_RegisterSelfDefiningDictionary("OperationDef", false, parse_ShaderOperationDef, true, true, NULL);

	ParserLoadFilesSharedToDictionary("SM_ShaderOps", "shaders/operations", ".op", "ShaderOps.bin", PARSER_BINS_ARE_SHARED|PARSER_USE_CRCS, g_hOpDefsDict);
}

ShaderTemplate *materialGetTemplateByName(const char *name)
{
	ShaderTemplate *ret=NULL;

	// Check the overrides first.  This table will be smaller, and generally empty
	if (stashFindPointer(material_load_info.stTemplateOverrides, name, &ret))
		return ret;

	if (stashFindPointer(material_load_info.stTemplates, name, &ret))
		return ret;
	return NULL;
}

ShaderTemplate *materialGetTemplateByNameWithOverrides(const char *name, ShaderTemplate **overrides)
{
	int it;
	for( it = 0; it != eaSize( &overrides ); ++it ) {
		if( stricmp( name, overrides[ it ]->template_name ) == 0 ) {
			return overrides[ it ];
		}
	}

	return materialGetTemplateByName( name );
}

int cmpShaderGraph(const ShaderGraph **graph1, const ShaderGraph **graph2)
{
	return StructCompare(parse_ShaderGraph, (void*)*graph1, (void*)*graph2, 0, 0, 0);
}

// Setup ShaderGraph pointers, have duplicates point to the same ones
static void materialRemoveDuplicateShaderGraphs(SA_PARAM_NN_VALID MaterialLoadInfo* load_info)
{
	int num_dup_graphs=0;
	int i;
	ShaderGraph **graphs=NULL;
	for (i=eaSize(&load_info->templates)-1; i>=0; i--) {
		ShaderTemplate *templ = load_info->templates[i];
		eaPush(&graphs, &templ->graph_parser);
	}
	eaQSort(graphs, cmpShaderGraph);
	ANALYSIS_ASSUME(graphs); // this should be correct
	// For each ShaderTemplate, find the same ShaderGraph everyone else is using
	for (i=eaSize(&load_info->templates)-1; i>=0; i--) {
		ShaderTemplate *templ = load_info->templates[i];
		
		ShaderGraph *search_for_me = &templ->graph_parser;
		ShaderGraph **result = eaBSearch(graphs, cmpShaderGraph, search_for_me);
		templ->graph = *result;
		if (templ->graph != &templ->graph_parser) {
			num_dup_graphs++;
		}
	}
	eaDestroy(&graphs);
	//if (num_dup_graphs)
	//	printf("%d reused ShaderGraph%s.\n", num_dup_graphs, (num_dup_graphs==1)?"":"s");
}

static void buildTemplateHash(SA_PARAM_NN_VALID MaterialLoadInfo *load_info)
{
	int i;
	assert(load_info->stTemplates);
	stashTableClear(load_info->stTemplates);
	for (i=eaSize(&load_info->templates)-1; i>=0; i--) {
		ShaderTemplate *templ = load_info->templates[i];
		bool added = stashAddPointer(load_info->stTemplates, templ->template_name, templ, false);
		if (!added) {
			// Duplicate!
			ShaderTemplate *templ_dup=NULL;
			stashFindPointer(load_info->stTemplates, templ->template_name, &templ_dup);
			ErrorFilenameDup(templ_dup->filename, templ->filename, templ->template_name, "ShaderTemplate");
		}
	}
}

// static void buildMaterialHash(SA_PARAM_NN_VALID MaterialLoadInfo *load_info)
// {
// 	int i;
// 	assert(load_info->stMaterials);
// 	stashTableClear(load_info->stMaterials);
// 	for (i=eaSize(&load_info->materials)-1; i>=0; i--) {
// 		Material *material = load_info->materials[i];
// 		bool added = stashAddPointer(load_info->stMaterials, material->material_name, material, false);
// 		if (!added) {
// 			// Duplicate!
// 			Material *material_dup=NULL;
// 			stashFindPointer(load_info->stMaterials, material->material_name, &material_dup);
// 			ErrorFilenameDup(material_dup->filename, material->filename, material->material_name, "Material");
// 		}
// 	}
// }

void materialPruneOperationValues(MaterialData *material) // Prunes any values that don't reference something in the graph
{
	int fallback_it;
	MaterialFallback* fallback;
	for( fallback_it = -1; fallback_it != eaSize( &material->graphic_props.fallbacks ); ++fallback_it ) {
		int i, j;
		ShaderTemplate *shader_template;
		
		if( fallback_it < 0 ) {
			fallback = &material->graphic_props.default_fallback;
		} else {
			fallback = material->graphic_props.fallbacks[ fallback_it ];
		}
		
		shader_template = materialGetTemplateByName( fallback->shader_template_name );
		if (!shader_template)
			continue;
		
		for (i=eaSize(&fallback->shader_values)-1; i>=0; i--) {
			ShaderOperationValues *op_values = fallback->shader_values[i];
			bool pruneit=false;
			ShaderGraph *graph;
			ShaderOperation *op;

			graph = &shader_template->graph_parser;
			op = materialFindOpByName(graph, op_values->op_name);

			if (!op) {
				// The operation this refers to does not exist
				pruneit = true;
			} else {
				for (j=eaSize(&op_values->values)-1; j>=0; j--) {
					ShaderOperationSpecificValue *spec_value = op_values->values[j];
					ShaderInput *op_input = materialFindShaderInputByName(op, spec_value->input_name);
					bool pruneSpecValue = false;
					if (!op_input)
					{
						// This input does not even exist
						// Prune the specific value
						pruneSpecValue = true;
					} else if (op_input->input_default.default_type != SIDT_NODEFAULT &&
							   !op_input->input_not_for_assembler)
					{
						// Input has a default value, specified will be ignored
						pruneSpecValue = true;
					} else {
						// Input exists, is it satisfied in the graph already?
						ShaderInputEdge *input_edge = materialFindInputEdgeByName(op, spec_value->input_name);
						ShaderFixedInput *fixed_input = materialFindFixedInputByName(op, spec_value->input_name);
						if (input_edge || fixed_input) {
							// An input edge connects to this input, we don't need a specific value
							pruneSpecValue = true;
						}
					}
					if (pruneSpecValue) {
						eaRemoveFast(&op_values->values, j);
						StructDestroy(parse_ShaderOperationSpecificValue, spec_value);
					}
				}
			}
			if (pruneit || eaSize(&op_values->values)==0) {
				eaRemoveFast(&fallback->shader_values, i);
				StructDestroy(parse_ShaderOperationValues, op_values);
			}
		}
	}
}


TextParserResult fixupMaterialData(MaterialData *material, enumTextParserFixupType eFixupType, void *pExtraData)
{
	char name[MAX_PATH];

	switch (eFixupType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		//hack for writing to text and back if doing STRUCT_PACK_ZIP
		//if (material->material_name)
		//	break;
		getFileNameNoExt(name, material->filename);
		if (strStartsWith(material->filename, "materials/templates")) {
			strcat(name, "_default");
		} else if (fileIsAbsolutePath(material->filename) && strstriConst(material->filename, "materials/templates")) {
			strcat(name, "_default");
		}
		material->material_name = allocAddString(name);
	}

	return PARSERESULT_SUCCESS;

}

static void shaderTemplateSetGraphFlags(ShaderTemplate *shader_template)
{
	// Determine if we need to add/remove the HandlesColorTint flag
	bool bNeedsHCTFlag = false;
	bool bNeedsNNMFlag = true;
	ShaderGraph *shader_graph = &shader_template->graph_parser;
	FOR_EACH_IN_EARRAY(shader_graph->operations, const ShaderOperation, op)
	{
		const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
		if (op_def) {
			FOR_EACH_IN_EARRAY(op_def->op_inputs, const ShaderInput, op_input)
			{
				if (op_input->input_default.default_type == SIDT_COLOR) {
					if (!materialFindInputEdgeByNameConst(op, op_input->input_name))
						bNeedsHCTFlag = true; // Color input, with no edge overriding it
				}
			}
			FOR_EACH_END;
			FOR_EACH_IN_EARRAY(op_def->op_outputs, const ShaderOutput, op_output)
			{
				if (op_output->data_type == SDT_NORMAL)
					bNeedsNNMFlag = false;
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
	if (bNeedsHCTFlag) {
		shader_template->graph_parser.graph_flags |= SGRAPH_HANDLES_COLOR_TINT;
	} else {
		shader_template->graph_parser.graph_flags &=~SGRAPH_HANDLES_COLOR_TINT;
	}
	if (bNeedsNNMFlag) {
		shader_template->graph_parser.graph_flags |= SGRAPH_NO_NORMALMAP;
	} else {
		shader_template->graph_parser.graph_flags &=~SGRAPH_NO_NORMALMAP;
	}
}

// Called on all text file reads
static TextParserResult materialTextProcessor(ParseTable pti[], SA_PARAM_NN_VALID MaterialLoadInfo* load_info)
{
	int ret=1;
	FOR_EACH_IN_EARRAY(load_info->templates, ShaderTemplate, shader_template)
	{
		char buf[MAX_PATH];
		char template_name[MAX_PATH];
		getFileNameNoExt(buf, shader_template->filename);
		strcpy(template_name, shader_template->template_name);
		if (strEndsWith(template_name, "_OneOff"))
			template_name[strlen(template_name) - 7] ='\0';
		if (stricmp(buf, template_name)!=0) {
			ErrorFilenamef(shader_template->filename, "ERROR: Template named %s in file named %s.  You must rename templates or one-off materials through the Material Editor.", shader_template->template_name, buf);
			ret = 0;
		}
	}
	FOR_EACH_END;
	
	return ret;
}

// Called immediately before binning
static TextParserResult materialPreProcessor(ParseTable pti[], SA_PARAM_NN_VALID MaterialLoadInfo* load_info)
{
	int i;
	int ret=PARSERESULT_SUCCESS;
	MaterialDataInfoSerialize *data_info_serialize;
	StashTable dupCheck;
	int packed_size;

	assert(!load_info->stTemplates);
	load_info->stTemplates = stashTableCreateWithStringKeys(eaSize(&load_info->templates)*3/2, StashDefault);

	// Fill in hashtable (may need to be re-filled in later after moving to shared
	//   memory, but we need it for validating materials
	buildTemplateHash(load_info);
	for (i=eaSize(&load_info->templates)-1; i>=0; i--) {
		ShaderTemplate *shader_template = load_info->templates[i];
		if (!materialShaderTemplateValidate(shader_template, false))
			ret = PARSERESULT_ERROR;
	}

	// fill in Material::Template pointers
	default_shader_template = materialGetTemplateByName("DefaultTemplate");
	assertmsg(default_shader_template, "Failed to find \"DefaultTemplate\" ShaderTemplate.");

	// Validate all MaterialDatas
	material_validate_skip_texture_existence = true;
	for (i=eaSize(&load_info->material_datas)-1; i>=0; i--) {
		MaterialData *material = load_info->material_datas[i];
		materialDataInit(material);
		
		// unfixable errors go here
		if (!materialValidateStructural( material ))
			ret = PARSERESULT_ERROR;
		
		if (!materialValidate(material, false, NULL))
			ret = PARSERESULT_ERROR;
		materialPruneOperationValues(material); // This should really be moved into the pre-processor, so that the .bin files are smaller, and take less memory?
	}
	material_validate_skip_texture_existence = false;

	// Pack them and make MaterialDataInfos
	assert(!load_info->packed_data);
	loadend_printf("done.");
	loadstart_printf("Packing Materials...");
	// Allocate packed data stream
	load_info->packed_data = calloc(sizeof(*load_info->packed_data), 1);
	PackedStructStreamInit(load_info->packed_data, material_pack_method);
	// Allocate headers
	eaSetCapacity(&load_info->data_infos_serialize, eaSize(&load_info->material_datas));
	dupCheck = stashTableCreateWithStringKeys(eaSize(&load_info->material_datas)*3/2, StashDefault);
	for (i=0; i<eaSize(&load_info->material_datas); i++) {
		MaterialData *material_data = load_info->material_datas[i];
		MaterialData *material_dup_data;
		// Pack the data and store the offset in the header
		data_info_serialize = StructAlloc(parse_MaterialDataInfoSerialize);
		data_info_serialize->data_offset = StructPack_dbg(parse_MaterialData, material_data, load_info->packed_data, MATERIAL_DATA_PACKED_NAME, __LINE__);
		data_info_serialize->material_name = material_data->material_name;
		data_info_serialize->filename = material_data->filename;
		// Add this to a separate table of the packed structures
		eaPush(&load_info->data_infos_serialize, data_info_serialize);

		if (stashFindPointer(dupCheck, material_data->material_name, &material_dup_data)) {
			// Duplicate checking
			ErrorFilenameDup(material_dup_data->filename, material_data->filename, material_data->material_name, "Material");
		} else {
			if (!stashAddPointer(dupCheck, material_data->material_name, material_data, false)) {
				assert(0); // It wasn't in the table, but we failed to add to it?!
			}
		}

		// Save dependencies
		{
			int fallbackIt;
			for( fallbackIt = -1; fallbackIt != eaSize( &material_data->graphic_props.fallbacks ); ++fallbackIt ) {
				MaterialFallback *fallback;
				if( fallbackIt < 0 ) {
					fallback = &material_data->graphic_props.default_fallback;
				} else {
					fallback = material_data->graphic_props.fallbacks[ fallbackIt ];
				}
				
				FOR_EACH_IN_EARRAY(material_data->graphic_props.default_fallback.shader_values, ShaderOperationValues, op_values)
				{
					FOR_EACH_IN_EARRAY(op_values->values, ShaderOperationSpecificValue, spec_value)
					{
						FOR_EACH_IN_EARRAY(spec_value->svalues, const char, texname)
						{
							eaPush(&data_info_serialize->texture_deps, texname);
						}
						FOR_EACH_END;
					}
					FOR_EACH_END;
				}
				FOR_EACH_END;
			}
		}
	}
	stashTableDestroy(dupCheck);
	// This reallocs the bitstream to the appropriate size and does whatever finalizing is needed
	PackedStructStreamFinalize(load_info->packed_data);
	// Destroy the array and all source data
	eaDestroyStruct(&load_info->material_datas, parse_MaterialData);

	// Store in serializable form, free packed data
	load_info->packed_data_serialize = PackedStructStreamSerialize(load_info->packed_data);

	packed_size = PackedStructStreamGetSize(load_info->packed_data);
	PackedStructStreamDeinit(load_info->packed_data);
	SAFE_FREE(load_info->packed_data);

	loadend_printf("done (%s).", friendlyBytes(packed_size));

	// Clean up temporary data
	stashTableDestroy(load_info->stTemplates);
	load_info->stTemplates = NULL;

	loadstart_printf("Writing .bin...");

	return ret;
}

static TextParserResult materialPostProcessor(ParseTable pti[], SA_PARAM_NN_VALID MaterialLoadInfo* load_info)
{
	// Ran before copying to shared memory
	// Allocate memory for HashTables
	assert(!load_info->stMaterialDataInfos);
	assert(!load_info->stTemplates);

	// Assume that half of the material datas will get fallbacks
	// referenced and that we want a 66% fill
	//
	// 1.5 / (2/3) = 9/4
	load_info->stMaterialDataInfos = stashTableCreateWithStringKeys(MAX(eaSize(&load_info->material_datas), eaSize(&load_info->data_infos_serialize))*9/4, StashDefault);
	load_info->stTemplates = stashTableCreateWithStringKeys(eaSize(&load_info->templates)*3/2, StashDefault);
	return PARSERESULT_SUCCESS;
}



static bool last_error_template_error=true;
bool materialValidateLastErrorWasTemplateError(void) // Hack for MaterialEditor
{
	return last_error_template_error;
}

__forceinline static bool shaderDataIsDynamic(ShaderDataType data_type)
{
	bool dynamic = false;
	if (data_type== SDT_TEXTURE_SCREENDEPTH ||
		data_type == SDT_TEXTURE_SCREENOUTLINE ||
		data_type == SDT_TEXTURE_SCREENCOLOR ||
		data_type == SDT_TEXTURE_SCREENCOLORHDR ||
		data_type == SDT_TEXTURE_SCREENCOLOR_BLURRED ||
		data_type == SDT_TEXTURE_DIFFUSEWARP ||
		data_type == SDT_TEXTURE_REFLECTION ||
		data_type == SDT_TEXTURE_AMBIENT_CUBE ||
		data_type == SDT_TEXTURE_HEIGHTMAP ||
		//data_type == SDT_HEIGHTMAP_SCALE ||
		data_type >= SDT_DRAWABLE_START)
		dynamic = true;
	return dynamic;
}

int materialValidateStructural(MaterialData* material)
{
	int ret = 1;
	
	if( strstri( material->filename, "/Templates/" )) {
		char templateName[ 256 ];
		char materialName[ 256 ];
		getFileNameNoExt(templateName, material->filename);
		sprintf( materialName, "%s_default", templateName );

		if( stricmp( materialName, material->material_name ) != 0 ) {
			ErrorFilenamef( material->filename, "Material should be name %s but is named %s.",
							materialName, material->material_name );
			ret = 0;
		}
		if( stricmp( templateName, material->graphic_props.default_fallback.shader_template_name ) != 0 ) {
			ErrorFilenamef( material->filename, "Material should reference template %s but actually references %s.",
							templateName, material->graphic_props.default_fallback.shader_template_name );
			ret = 0;
		}
	}

	return ret;
}

// Returns 0 if bad
//
// Overrides allow you to specify a specific ShaderTemplate to use
// instead of what materialGetTemplateByName would return by default,
// which is needed by the MaterialEditor.
int materialValidate(MaterialData *material, bool bRepair, ShaderTemplate** overrides)
{
	char shortNamePrefix[32];
	int ret=1;
	int j, k, ii, jj;
	bool inHiddenDir = (strstri( material->filename, "/_" ) != NULL);
	last_error_template_error = true;
	material->graphic_props.has_validation_error = 0;

	sprintf(shortNamePrefix, "%s:", GetShortProductName());

	if( !inHiddenDir && materialErrorOnMissingFallbacks && !materialDataHasRequiredFallbacks( material, overrides )) {
		ErrorFilenamef( material->filename,
						"Material \"%s\" does not have every required "
						"fallback.  Each material and template should have a "
						"SM2 fallback.",
						material->material_name );
		ret = 0;
	}

	if (eaSize(&material->graphic_props.default_fallback.input_mappings))
	{
		ErrorFilenamef(material->filename, "Material \"%s\" has input mappings outside of a fallback.", material->material_name);
		ret = 0;
	}
	
	
	{
		int fallback_it = -1;
		MaterialFallback* default_fallback = &material->graphic_props.default_fallback;

		for( fallback_it = -1; fallback_it < eaSize( &material->graphic_props.fallbacks ); ++fallback_it ) {
			MaterialFallback* fallback;
			ShaderTemplate *shader_template;
			
			if( fallback_it < 0 ) {
				fallback = &material->graphic_props.default_fallback;
			} else {
				fallback = material->graphic_props.fallbacks[ fallback_it ];
			}
			
			shader_template = materialGetTemplateByNameWithOverrides( fallback->shader_template_name, overrides );

			if( !shader_template ) {
				ErrorFilenamef(material->filename, "Material \"%s\" references unknown ShaderTemplate \"%s\"", material->material_name, fallback->shader_template_name);
				ret = 0;
				continue;
			}

			if ((shader_template->graph_parser.graph_flags & SGRAPH_ALLOW_REF_MIP_BIASE) &&
				material->graphic_props.max_reflect_resolution)
			{
				ErrorFilenamef(material->filename, "Material \"%s\" specifies a MaxReflectionResolution but references a ShaderTemplate (\"%s\") with AllowReflectionMIPBias which will cause this to be ignored.", material->material_name, fallback->shader_template_name);
				ret = 0;
			}
			
			for (j=eaSize(&shader_template->graph_parser.operations)-1; j>=0; j--)
			{
				ShaderOperation *op = shader_template->graph_parser.operations[j];
				const ShaderOperationDef *op_def = GET_REF(op->h_op_definition);
				ShaderOperationValues *this_values=NULL;
				ShaderInputMapping *this_mapping=NULL;
				int num_floats=0;
				int num_strings=0;
				bool opRepair = (bRepair || strStartsWith(op->op_name, shortNamePrefix));

				if (!op_def)
					continue;

				// Find the Values list in the material for this operation
				for (k=eaSize(&fallback->shader_values)-1; k>=0; k--) {
					ShaderOperationValues *values = fallback->shader_values[k];
					if (stricmp(values->op_name, op->op_name)!=0)
						continue;
					// This is the set of values for this operation
					this_values = values;
					break;
				}

				// Or, there may be an input mapping...
				for (k=eaSize(&fallback->input_mappings)-1; k>=0; k--) {
					ShaderInputMapping *mapping = fallback->input_mappings[k];
					if (stricmp(mapping->mapped_op_name, op->op_name)!=0)
						continue;
					// This is the input mapping for this operation
					this_mapping = mapping;
					break;
				}

				if (this_values && this_mapping)
				{
					ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nOp \"%s\" has an InputMapping and a SpecificValue.",
								   fallback->shader_template_name, op->op_name);
					ret = 0;
				}

				if (this_mapping)
				{
					bool foundSource = false;
					// Make sure each mapping has a source in the default fallback
					for (k=eaSize(&default_fallback->shader_values)-1; k>=0; k--)
					{
						ShaderOperationValues *values = default_fallback->shader_values[k];
						if (stricmp(values->op_name, this_mapping->op_name)!=0)
							continue;
						foundSource = true;
						break;
					}

					if (!foundSource)
					{
						ret = 0;
						ErrorFilenamef( material->filename, "Fallback: \"%s\"\n\nInput Mapping from \"%s\" has no source in the default material.",
										fallback->shader_template_name, this_mapping->op_name );
					}
				}
				else
				{
					// Make sure all inputs to this operation either have a default, a specific input in the graph, or
					//  a specific value input
					for (k=eaSize(&op_def->op_inputs)-1; k>=0; k--)
					{
						ShaderInput *op_input = op_def->op_inputs[k];
						bool bFoundGoodInput=false;
						bool bWarned=false;
						ShaderFixedInput *fixed_input = materialFindFixedInputByName(op, op_input->input_name);
						if (op_input->data_type == SDT_TIMEGRADIENT) {
							// Special validation for this type
							F32 start, startFade, end, endFade;
							start = materialFindOperationSpecificValueFloat(this_values, "Start", 18);
							startFade = materialFindOperationSpecificValueFloat(this_values, "StartFade", 19);
							if (startFade < start)
								startFade += 24.f;
							end = materialFindOperationSpecificValueFloat(this_values, "End", 5);
							while (end < start)
								end += 24.f;
							endFade = materialFindOperationSpecificValueFloat(this_values, "EndFade", 6);
							while (endFade < start)
								endFade += 24.f;
							// Check if end or endFade are between start and startFade
							if (end > start && end < startFade) {
								ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nTimeGradient inputs have a End value between its Start and StartFade values.", fallback->shader_template_name);
								ret = 0;
							} else if (endFade > start && endFade < startFade) {
								ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nTimeGradient inputs have a EndFade value between its Start and StartFade values.", fallback->shader_template_name);
								ret = 0;
							} else {
								// Check if start or startFade are between end and endFade
								end += 48.f;
								while (endFade < end)
									endFade += 24.f;
								while (start < end)
									start += 24.f;
								while (startFade < end)
									startFade += 24.f;
								if (start > end && start < endFade) {
									ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nTimeGradient inputs have a Start value between its End and EndFade values.", fallback->shader_template_name);
									ret = 0;
								} else if (startFade > end && startFade < endFade) {
									ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nTimeGradient inputs have a StartFade value between its End and EndFade values.", fallback->shader_template_name);
									ret = 0;
								}
							}
						}

						if (op_input->input_default.default_type != SIDT_NODEFAULT &&
							!op_input->input_not_for_assembler)
							continue; // Has a default
						if (shaderDataIsDynamic(op_input->data_type) && op_input->data_type != SDT_TEXTURE_REFLECTION && op_input->data_type != SDT_TEXTURE_DIFFUSEWARP && op_input->data_type != SDT_TEXTURE_HEIGHTMAP)
							continue; // Auto-filled in, for special cases, if specified, must exist
						if (fixed_input)
							continue; // Fixed input in template

						// Look for specific input in the graph
						for (ii=eaSize(&op->inputs)-1; ii>=0 && !bFoundGoodInput; ii--)
						{
							ShaderInputEdge *input_edge = op->inputs[ii];
							if (stricmp(input_edge->input_name, op_input->input_name)==0)
								bFoundGoodInput = true;
						}
						// Look for specific value input
						if (this_values) {
							bool bFoundValues=false;
							for (ii=eaSize(&this_values->values)-1; ii>=0 && !bFoundGoodInput; ii--) {
								ShaderOperationSpecificValue *value = this_values->values[ii];
								if (stricmp(value->input_name, op_input->input_name)==0) {
									bFoundValues = true;
									bFoundGoodInput = true;
									if (eaSize(&value->svalues)<op_input->num_texnames) {
										ret = 0;
										ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nMaterial \"%s\" specified %d texture names for operation \"%s\", input \"%s\" which requires %d texture names.",
													   fallback->shader_template_name, material->material_name, eaSize(&value->svalues), op->op_name, op_input->input_name, op_input->num_texnames);
										bWarned = true;
										bFoundGoodInput = false;
									} else if (eaSize(&value->svalues)) {
										// Specifies values, make sure they're good!
										if (wl_state.tex_find_func) {
											for (jj=0; jj<eaSize(&value->svalues); jj++) {
												BasicTexture *tex_bind;
												const char *texname = value->svalues[jj];
												if (!(tex_bind=wl_state.tex_find_func(texname, false, 0))) {
													if (!material_validate_skip_texture_existence) {
														ret = 0;
														ErrorFilenameGroupf(material->filename, "OwnerOnly", 14,
																					   //ErrorFilenamef(material->filename,
																					   "Fallback: \"%s\"\n\nMaterial \"%s\" references non-existent texture \"%s\"",
																					   fallback->shader_template_name, material->material_name, texname);
													} else {
														// Texture dependencies checked later
													}
												} else {

													if (op_input->data_type == SDT_TEXTURE_REFLECTION) {
														char temp[MAX_PATH];
														char *s;
														strcpy(temp, texname);
														if (s=strchr(temp, '.'))
															*s = '\0';
														if (eaSize(&op_input->input_default.default_strings) &&
															stricmp(temp, op_input->input_default.default_strings[0])==0)
														{
															// okay, references 0_from_sky_file
														}
														else if (!(strEndsWith(temp, "_cube") || strEndsWith(temp, "_spheremap")))
														{
															ret = 0;
															ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses non-cubemap/spheremap texture \"%s\" for a reflection texture.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
														}
													} else {
														if (op_input->data_type == SDT_TEXTURENORMAL) {// Check for SDT_TEXTURE_HEIGHTMAP should be combined here
															if (!wl_state.tex_is_normalmap_func(tex_bind)) {
																ret = 0;
																ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses non-normalmap/bumpmap texture \"%s\" for a normal map.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
															}
														} else if (op_input->data_type == SDT_TEXTURENORMALDXT5NM) {
															if (!wl_state.tex_is_dxt5nm_func(tex_bind)) {
																ret = 0;
																ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses non-DXT5nm texture \"%s\" for a DXT5nm normal map.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
															}
														} else {
															if (wl_state.tex_is_normalmap_func(tex_bind)) {
																ret = 0;
																ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses normalmap/bumpmap texture \"%s\" for a regular texture.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
															}
														}

														if (op_input->data_type == SDT_TEXTURECUBE) {
															if (!wl_state.tex_is_cubemap_func(tex_bind)) {
																ret = 0;
																ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses non-cubemap texture \"%s\" for a cubemap.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
															}
														} else {
															if (wl_state.tex_is_cubemap_func(tex_bind)) {
																ret = 0;
																ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses cubemap texture \"%s\" for a regular texture.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
															}
														}

														if (op_input->data_type == SDT_TEXTURE3D) {
															if (!wl_state.tex_is_volume_func(tex_bind)) {
																ret = 0;
																ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses non-3D texture \"%s\" for a 3D texture.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
															}
														} else {
															if (wl_state.tex_is_volume_func(tex_bind)) {
																ret = 0;
																ErrorFilenameGroupf(material->filename, "AuthorOnly", 14, "Fallback: \"%s\"\n\nMaterial \"%s\" uses 3D texture \"%s\" for a regular texture.  This is not allowed.", fallback->shader_template_name, material->material_name, texname);
															}
														}
													}
												}
											}
										}
									}

									if (eafSize(&value->fvalues)!=op_input->num_floats &&
										shaderDataTypeNeedsDrawableMapping(op_input->data_type))
									{
										// Okay
									} else if (eafSize(&value->fvalues)<op_input->num_floats)
									{
										// Error
										ret = 0;
										ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nMaterial \"%s\" did not specify the appropriate number of numerical inputs for operation \"%s\", input \"%s\", and the operation default does not have default values.\nExpected: %d\nReceived: %d",
													   fallback->shader_template_name, material->material_name, op->op_name, op_input->input_name, op_input->num_floats, eafSize(&value->fvalues));
										bWarned = true;
										bFoundGoodInput = false;
									}
								}
							}
							if (!bFoundValues) {
								if (eaSize(&op_input->input_default.default_strings)!=op_input->num_texnames) {
									if (!shaderDataIsDynamic(op_input->data_type))
									{
										ret = 0;
										ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nMaterial \"%s\" did not specify any texture names for operation \"%s\", input \"%s\", and the operation default does not have default texture names.",
													   fallback->shader_template_name, material->material_name, op->op_name, op_input->input_name);
										bWarned = true;
									}
								}
							}
						}
						if (!bFoundGoodInput &&
							!shaderDataIsDynamic(op_input->data_type)) // These are filled in if they are not specified
						{
							if (!opRepair) {
								// Too explicit: ErrorFilenamef(material->filename, "Material \"%s\", Operation \"%s\" in Template \"%s\" references OperationDef \"%s\", with an input \"%s\" which is lacking any values.",
								//	material->material_name, op->op_name, material->graphic_props.shader_template->template_name, op_def->op_type_name, input->input_name);
								if (!bWarned &&
									op_input->input_default.default_type == SIDT_NODEFAULT &&
									(!op_input->input_hidden && op_input->data_type != SDT_DEFAULT) // JE: since all hidden fields should be a dynamically calculated value?
									)
								{
									ret = 0;
									ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nOperation \"%s\" has an input \"%s\" which is lacking any values.",
												   fallback->shader_template_name, op->op_name, op_input->input_name);
									bWarned = true;
								}
							} else { // if (opRepair)
								// Repair/add one
								ShaderOperationSpecificValue *value = materialAddOperationSpecificValue(material, fallback, op->op_name, op_input->input_name);
								char buffer[ 256 ];
								const MaterialData* templateData;
								bool defaultFound = false;

								sprintf( buffer, "%s_default", shader_template->template_name );
								templateData = materialFindData( buffer );

								if (templateData && templateData != material)
								{
									const MaterialFallback* templateDefault = &templateData->graphic_props.default_fallback;
									const ShaderOperationSpecificValue* defaultValue = materialFindOperationSpecificValue2Const(
											materialFindOperationValuesConst(templateData, templateDefault, op->op_name),
											op_input->input_name );

									if( defaultValue ) {
										StructCopyFields( parse_ShaderOperationSpecificValue, defaultValue, value, 0, 0 );
										defaultFound = true;
									}
								}
								
								if (!defaultFound)
								{
									for (ii=0; ii<op_input->num_floats; ii++) {
										if (ii < eafSize(&op_input->input_default.default_floats)) {
											eafPush(&value->fvalues, eafGet(&op_input->input_default.default_floats, ii));
										} else {
											eafPush(&value->fvalues, 1.f);
										}
									}
									for (ii=0; ii<op_input->num_texnames; ii++) {
										if (eaSize(&op_input->input_default.default_strings)>ii)
											eaPush(&value->svalues, allocAddString(op_input->input_default.default_strings[ii]));
										else
											eaPush(&value->svalues, allocAddString("Default"));
									}
								}
							}
						}
					}
				}
				
				// Make sure all of these values reference valid inputs
				for (ii=eaSize(&op->inputs)-1; ii>=0; ii--) {
					ShaderInputEdge *input_edge = op->inputs[ii];
					// Check that it's target input is valid
					bool bGoodValue;
					k = materialFindInputIndexByName(op_def, input_edge->input_name);
					if (k==-1) {
						ret = 0;
						ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nMaterial \"%s\", Operation \"%s\" has a value for an input named \"%s\" which does not exist.",
									   fallback->shader_template_name, material->material_name, op->op_name, input_edge->input_name);
					}
					// Check that it's source exists
					bGoodValue = false;
					{
						ShaderOperation *op2 = materialFindOpByName(&shader_template->graph_parser, input_edge->input_source_name);
						if (op2 && GET_REF(op2->h_op_definition)) {
							// Check that the named output exists
							jj = materialFindOutputIndexByName(GET_REF(op2->h_op_definition), input_edge->input_source_output_name);
							if (jj!=-1)
							{
								bGoodValue = true;
							}
						}
					}
					if (!bGoodValue) {
						ret = 0;
						ErrorFilenamef(material->filename, "Fallback: \"%s\"\n\nMaterial \"%s\", Operation \"%s\" in Template \"%s\", input \"%s\" wants to get its value from \"%s\".\"%s\" which does not exist",
							SAFE_MEMBER(fallback, shader_template_name), material->material_name, SAFE_MEMBER(op, op_name), SAFE_MEMBER(material->graphic_props.shader_template, template_name), SAFE_MEMBER(input_edge, input_name), SAFE_MEMBER(input_edge, input_source_name), SAFE_MEMBER(input_edge, input_source_output_name));
					}
				}
			}
		}
	}

	// TerrainEditor validation:
	if (  stricmp(material->graphic_props.default_fallback.shader_template_name, "TerrainMaterial") == 0
		  && eaSize(&material->graphic_props.fallbacks) > 1) {
		ret = 0;
		ErrorFilenamef(material->filename, "Material \"%s\" uses Template \"%s\" as just a storage container, its fallbacks will not be rendered, but it has %d.",
					   material->material_name, material->graphic_props.default_fallback.shader_template_name, eaSize(&material->graphic_props.fallbacks));
	}

	// GraphicsLib's validation
	if (material_validate_material_data_callback) {
		if (!material_validate_material_data_callback(material, overrides))
			ret = 0;
	}

	if (material->graphic_props.flags & RMATERIAL_SCREEN_TEX_COORDS &&
		material->graphic_props.flags & (RMATERIAL_WORLD_TEX_COORDS_XZ|RMATERIAL_WORLD_TEX_COORDS_XY|RMATERIAL_WORLD_TEX_COORDS_YZ))
	{
		ErrorFilenamef(material->filename, "Material \"%s\" has both ScreenTexCoords and WorldTexCoords set, which is not allowed, choose only one TexCoords generation method.", material->material_name);
	}

	// Sound Profile validation.
	if(!GET_REF(material->world_props.physical_properties) && !strEndsWith(material->material_name, "_default")) // Templates don't need them
	{
		ErrorFilenameGroupf(material->filename, "Art", 14, "Missing or invalid PhysicalProperties specified on material.  You must specify one in the MaterialEditor.");
		if (ret) {
			last_error_template_error = false; // Only error is this one
			ret = 0;
		}
		material->graphic_props.has_validation_error = 1;
	}
	if(  GET_REF(material->world_props.physical_properties)
	     && stricmp("Default", GET_REF(material->world_props.physical_properties)->name_key) == 0
		 && !strEndsWith(material->material_name, "_default")) // Templates don't need them
	{
#if 0
		ret = 0;
		ErrorFilenameGroupRetroactivef(
				material->filename, "AuthorOnly", 14, 7, 3, 2008,
				"Material has Default physical property.  You must specify "
				"some other physical property in the MaterialEditor." );
#endif
	}

	// Additional verification that could be done:
	//  Look for two ShaderOperationValues who have the identical op_name
	//  Look for two ShaderOperationSpecificValue on the same op that have the same input_name
	// Verification done in GfxMaterials (could be moved here):
	//  Look for a ShaderOperationSpecificValue which is an input for something which has a satisfactory input in the graph
	if (!ret)
		material->graphic_props.has_validation_error = 1;
	return ret;
}

// Returns true if there's a loop (bad)
static bool loopCheck(ShaderTemplate *shader_template, ShaderOperation *op, int num_left)
{
	int j;
	if (num_left<=0)
		return true;
	for (j=eaSize(&op->inputs)-1; j>=0; j--) {
		ShaderOperation *op2 = materialFindOpByName(&shader_template->graph_parser, op->inputs[j]->input_source_name);
		if (op2) {
			if (loopCheck(shader_template, op2, num_left-1))
				return true;
		}
	}
	return false;
}

AUTO_COMMAND ACMD_CMDLINE;
void forceEnableAmbientCube(int on)
{
	if(on)
	{
		char path[MAX_PATH];
		gConf.bForceEnableAmbientCube = true;
		fileLocateWrite("bin/Materials.bin", path);
		unlink(path);
	}
}

// Returns 0 if bad
static int materialShaderTemplateValidate1(ShaderTemplate *shader_template)
{
	int ret=1;
	int num_sinks=0;
	int i, j, k;
	int num_ops = eaSize(&shader_template->graph_parser.operations);
	bool infinite_loop=false;
	int texture_count=0;
	const char *instance_param_node=NULL;

	if (gConf.bForceEnableAmbientCube)
		shader_template->graph_parser.graph_flags |= SGRAPH_USE_AMBIENT_CUBE;

	for (i=num_ops-1; i>=0; i--) {
		if (!shader_template->graph_parser.operations[i]) {
			eaRemove(&shader_template->graph_parser.operations, i);
			// assertmsg(0, "Parser error!");
		}
	}

	num_ops = eaSize(&shader_template->graph_parser.operations);

	for (i=num_ops-1; i>=0; i--) {
		bool outputs_used=false;
		const ShaderOperationDef *op_def;
		ShaderOperation *op = shader_template->graph_parser.operations[i];
		assert(op);
		op_def = GET_REF(op->h_op_definition);

		if (!op_def) {
			ErrorFilenamef(shader_template->filename, "Template references unknown operation.  May crash if referenced.");
			continue;
		}

		op->op_has_error = 0;

		if (op->instance_param)
		{
			if (instance_param_node)
			{
				ErrorFilenamef(shader_template->filename, "Has more than one node with an instanced param : \"%s\" and \"%s\"", instance_param_node, op->op_name);
				ret = 0;
				op->instance_param = false; // Repair it so it doesn't crash
			} else {
				instance_param_node = op->op_name;
			}
		}

		if (op_def->op_type == SOT_SINK) {
			num_sinks++;
			outputs_used=true; // No outputs
		}

		for (j=eaSize(&op_def->op_inputs)-1; j>=0; j--) {
			const ShaderInput *shader_input = op_def->op_inputs[j];
			if (shader_input->data_type == SDT_TEXTURE_DIFFUSEWARP && !wl_state.enableDiffuseWarpTex)
				continue;
			if (shader_input->data_type == SDT_TEXTURE_REFLECTION && shader_template->graph_parser.graph_reflection_type == SGRAPH_REFLECT_NONE)
				continue;
			if (shader_input->data_type == SDT_TEXTURE_AMBIENT_CUBE && !(shader_template->graph_parser.graph_flags & SGRAPH_USE_AMBIENT_CUBE))
				continue;
			if (shader_input->data_type == SDT_TEXTURE_HEIGHTMAP)	// BTH: Texture is only used in the domain shader and so shouldn't go against the texture count.  Also only used in D3D11.
				continue;
			texture_count += shader_input->num_texnames;
		}

		// Verify that the inputs refer to existing ops
		for (j=eaSize(&op->inputs)-1; j>=0; j--) {
			ShaderOperation *op2 = materialFindOpByName(&shader_template->graph_parser, op->inputs[j]->input_source_name);
			if (!op2) {
				op->op_has_error = 1;
				ErrorFilenamef(shader_template->filename, "Operation \"%s\" references output \"%s\" on non-existent operation \"%s\"", op->op_name, op->inputs[j]->input_source_output_name, op->inputs[j]->input_source_name);
			}
		}

		// Check that someone uses one of my outputs
		for (j=num_ops-1; j>=0; j--) {
			ShaderOperation *op2 = shader_template->graph_parser.operations[j];
			for (k=eaSize(&op2->inputs)-1; k>=0; k--) {
				if (stricmp(op2->inputs[k]->input_source_name, op->op_name)==0)
					outputs_used=true;
			}
		}
		if (!outputs_used) {
			op->op_has_error = 1;
			ErrorFilenamef(shader_template->filename, "None of the outputs from operation \"%s\" are used", op->op_name);
			ret = false;
		}

		// Check for loops
		if (loopCheck(shader_template, op, num_ops+1)) {
			infinite_loop = true;
		}
	}

	if (infinite_loop) {
		ErrorFilenamef(shader_template->filename, "Infinite loop detected");
		ret = 0;
	}

	// Verify exactly 1 output operation
	if (num_sinks!=1) {
		ErrorFilenamef(shader_template->filename, "Has %d output (sink) operations, expected 1", num_sinks);
		ret = 0;
	}

	if (texture_count>MAX_MATERIAL_TEXTURES) {
		int max_textures = MAX_MATERIAL_TEXTURES;
		if (strstri(shader_template->filename, "terrain"))
			max_textures = MAX_MATERIAL_TEXTURES+1;
		if (texture_count > max_textures) {
			ErrorFilenamef(shader_template->filename, "Has %d texture inputs.  The limit is %d.", texture_count, max_textures);
			ret = 0;
		}
	}

	shaderTemplateSetGraphFlags(shader_template);

	return ret;
}

int materialShaderTemplateValidate(ShaderTemplate *shader_template, bool ignore_autosave_error)
{
	int ret=1;
	if (!shader_template)
		return 0;

	if (!materialShaderTemplateValidate1(shader_template))
		ret = 0;
	if (!shader_template->graph) { // For reload (regular load does pruneDuplicateShaderGraphs to set this up)
		shader_template->graph = &shader_template->graph_parser;
	}
	if (!ignore_autosave_error && shader_template->is_autosave) {
		ErrorFilenamef(shader_template->filename, "ERROR: This file is an autosave file.  Please save the file in the MaterialEditor once." );
	}

	return ret;
}

extern int bDynListAllUnusedTexturesAndGeometry;

static TextParserResult materialPointerPostProcessor(ParseTable pti[], SA_PARAM_NN_VALID MaterialLoadInfo* load_info)
{
	int i;
	TextParserResult ret = PARSERESULT_SUCCESS;

	memBudgetAddStructMapping(MATERIAL_DATA_PACKED_NAME, __FILE__);
	allocAddStringMapRecentMemory("SerializablePackedStructStream", MATERIAL_DATA_PACKED_NAME, __LINE__);

	// Some of this already filled in if from text files, but not if from .bin or shared memory, so re-run this

	assert(load_info->stMaterialDataInfos);
	assert(load_info->stTemplates);

	// Fill in hashtable
	buildTemplateHash(load_info);

	// fill in Material::Template pointers
	default_shader_template = materialGetTemplateByName("DefaultTemplate");
	assertmsg(default_shader_template, "Failed to find \"DefaultTemplate\" ShaderTemplate.");

	// Setup Graph pointers
	materialRemoveDuplicateShaderGraphs(load_info);

	loadend_printf("done."); // matches either "Writing .bin" or "Loading Materials"
	loadstart_printf("Setting up material run-time data...");

	// Setup packed material data in memory
	assert(!load_info->packed_data);
	assert(load_info->packed_data_serialize);
	load_info->packed_data = calloc(sizeof(*load_info->packed_data), 1);
	PackedStructStreamDeserialize(load_info->packed_data, load_info->packed_data_serialize);
	// Quick hack to get this into shared memory, since it's 90% of the data size
	//   a better solution would be to put the entire thing in shared memory and
	//   skip the whole loading of the .bin file, but because of the way the unpacking
	//   works this is a significant and scarier and server-only change.
	{
		SharedMemoryHandle *handle=NULL;
		SM_AcquireResult result = stringCacheSharingEnabled()?stringCacheSharedMemoryAcquire(&handle, "SM_Materials", NULL):SMAR_Error;
		if (result == SMAR_FirstCaller)
		{
			// Put bitstream in here
			unsigned int bitl = bsGetBitLength(load_info->packed_data->bs);
			unsigned int bytel = (bitl + 7)/8;
			unsigned char *data = sharedMemorySetSize(handle, bytel+4);
			unsigned char *oldData = bsGetDataPtr(load_info->packed_data->bs);
			*(U32*)data = bitl;
			memcpy(data+4, oldData, bytel);
			sharedMemoryUnlock(handle);
			free(oldData);
			bsSetNewMaxSizeAndDataPtr(load_info->packed_data->bs, bytel, data+4);
		}
		else if (result == SMAR_DataAcquired)
		{
			unsigned char *data = sharedMemoryGetDataPtr(handle);
			// Get bitstream and free local one
			free(bsGetDataPtr(load_info->packed_data->bs));
			bsSetNewMaxSizeAndDataPtr(load_info->packed_data->bs, (*(U32*)data+7)/8, data + 4);
		}
		else
		{
			// Error, do nothing
		}
	}
	StructDestroySafe(parse_SerializablePackedStructStream, &load_info->packed_data_serialize);

	// Allocate headers
	load_info->data_infos = calloc(sizeof(load_info->data_infos[0]), eaSize(&load_info->data_infos_serialize));

	// Make MaterialDataInfos
	for (i=0; i<eaSize(&load_info->data_infos_serialize); i++) {
		MaterialDataInfoSerialize *data_serialized = load_info->data_infos_serialize[i];
		// Pack the data and store the offset in the header
		load_info->data_infos[i].data_offset = data_serialized->data_offset;
		// This pointer is where we'll store the unpacked version later
		load_info->data_infos[i].material = NULL;
		// Add this to a separate table of the packed structures
		if (!stashAddPointer(load_info->stMaterialDataInfos, data_serialized->material_name, &load_info->data_infos[i], false)) {
			// Duplicate!
			// Find the first one
			MaterialDataInfo *mat_dup=NULL;
			int j;
			bool bFoundOne=false;
			stashFindPointer(load_info->stMaterialDataInfos, data_serialized->material_name, &mat_dup);
			for (j=0; j<i; j++) {
				if (load_info->data_infos_serialize[j]->data_offset == mat_dup->data_offset) {
					ErrorFilenameDup(load_info->data_infos_serialize[j]->filename, data_serialized->filename, data_serialized->material_name, "Material");
					bFoundOne = true;
				}
			}
			assert(bFoundOne);
		}
		// Verify texture dependencies
		if (wl_state.tex_find_func)
		{
			FOR_EACH_IN_EARRAY(data_serialized->texture_deps, const char, texname)
			{
				if (!wl_state.tex_find_func(texname, false, 0)) {
					//ret = PARSERESULT_ERROR; // What would returning an error from here do?
					if(!gbIgnoreTextureErrors)
						ErrorFilenamef(data_serialized->filename,
							"Material \"%s\" references non-existent texture \"%s\"",
							data_serialized->material_name, texname);
				}
				// Used to find unreferenced textures in the fx system, for purging
				if (bDynListAllUnusedTexturesAndGeometry)
					dynFxMarkTextureAsUsed(texname);
			}
			FOR_EACH_END;
		}
		if (isDevelopmentMode())
		{
			// Set up the meta data
			ResourceDictionaryInfo *pDictInfo = resDictGetInfo(MATERIAL_DICT);

			if (pDictInfo)
			{	
				char *scope;
				char temp[CRYPTIC_MAX_PATH];
				ResourceInfo *pResInfo;
				pResInfo = resGetOrCreateInfo(pDictInfo, data_serialized->material_name);

				strcpy(temp, data_serialized->filename);
				getDirectoryName(temp);
				scope = strstri(temp, "materials/");
				if (scope)
				{
					scope = scope + strlen("materials/");
				}
				else
				{
					scope = temp;
				}

				FOR_EACH_IN_EARRAY(data_serialized->texture_deps, const char, texname)
				{
					resInfoGetOrCreateReference(pResInfo, "Texture", texname, NULL, REFTYPE_REFERENCE_TO, NULL);
				}
				FOR_EACH_END;

				pResInfo->resourceLocation = allocAddFilename(data_serialized->filename);
				resHandleChangedInfo(pResInfo->resourceDict, pResInfo->resourceName, false);
			}
		}
	}


	// Destroy the serialized source data
	eaDestroyStruct(&load_info->data_infos_serialize, parse_MaterialDataInfoSerialize);

	loadend_printf("done.");

	return ret;
}

static TextParserResult materialReloadPointerPostProcessor(ParseTable pti[], SA_PARAM_NN_VALID MaterialLoadInfo* load_info)
{
	int i;
	TextParserResult ret = PARSERESULT_SUCCESS;
	// Templates update in per-struct update

	// Validate all newly loaded MaterialDatas
	for (i=eaSize(&load_info->material_datas)-1; i>=0; i--) {
		MaterialData *material = load_info->material_datas[i];
		materialDataUpdateFallback(material);

		// unfixable errors go here
		if (!materialValidateStructural( material ))
			ret = PARSERESULT_ERROR;
		
		if (!materialValidate(material, false, NULL))
			ret = PARSERESULT_ERROR;
		materialPruneOperationValues(material); // This should really be moved into the pre-processor, so that the .bin files are smaller, and take less memory?
	}

	// Not packing reloaded ones, but still need to make MaterialDataInfos
	for (i=0; i<eaSize(&load_info->material_datas); i++) {
		MaterialData *material_data = load_info->material_datas[i];
		MaterialDataInfo *mdi;
		if (!stashFindPointer(load_info->stMaterialDataInfos, material_data->material_name, &mdi)) {
			// New material!
			mdi = calloc(sizeof(*mdi), 1);
			mdi->data_offset = (U32)-1;
		}
		materialUnpack(mdi, material_data);
		if (!stashAddPointer(load_info->stMaterialDataInfos, material_data->material_name, mdi, false)) {
			Material *material_dup;
			const MaterialData *material_dup_data;
			material_dup = materialFind(material_data->material_name, 0);
			assert(material_dup);
			if (material_dup != mdi->material) {
				material_dup_data = materialGetData(material_dup);
				ErrorFilenameDup(material_dup_data->filename, material_data->filename, material_data->material_name, "Material");
			}
		}
		if (isDevelopmentMode())
		{
			// Set up the meta data
			ResourceDictionaryInfo *pDictInfo = resDictGetInfo(MATERIAL_DICT);

			if (pDictInfo)
			{	
				char *scope;
				char temp[CRYPTIC_MAX_PATH];
				ResourceInfo *pResInfo;
				pResInfo = resGetOrCreateInfo(pDictInfo, material_data->material_name);

				strcpy(temp, material_data->filename);
				getDirectoryName(temp);
				scope = strstri(temp, "materials/");
				if (scope)
				{
					scope = scope + strlen("materials/");
				}
				else
				{
					scope = temp;
				}
				
				pResInfo->resourceScope = allocAddString(scope);
				pResInfo->resourceLocation = allocAddFilename(material_data->filename);
				resHandleChangedInfo(pResInfo->resourceDict, pResInfo->resourceName, false);
			}
		}
	}
	eaDestroy(&load_info->material_datas);

	return ret;
}

static int materialReloadTemplate(ShaderTemplate *shader_template, const char *expected_filename)
{
	int ret = 1;
	if (shader_template) {
		if (!materialShaderTemplateValidate(shader_template, false))
			ret = 0;
		if (material_reload_template_callback)
			material_reload_template_callback(shader_template);
	}
	return ret;
}

static int materialReloadMaterial(Material *material_header, const char *expected_filename)
{
	int ret = 1;
	if (material_header) {
		MaterialData *material_data;
		// There will be a material_data here because we called ParserReloadFile before calling this,
		//    unless we're looking at the wrong Material.
		if (!material_header->material_data || material_header->material_data->from_packed)
			return 0; // Can happen if a file in Core is touched, and we try to reload the one in the project?
		if (stricmp(material_header->material_data->filename, expected_filename)!=0)
			return 0;
		
		material_data = materialGetDataNonConst(material_header);
		assert(!material_data->from_packed);
		
		// unfixable errors go here
		if (!materialValidateStructural( material_data ))
			ret = PARSERESULT_ERROR;
		
		if (!materialValidate(material_data, false, NULL))
			ret = 0;
		materialPruneOperationValues(material_data); // This should really be moved into the pre-processor, so that the .bin files are smaller, and take less memory?
		if (material_reload_material_callback)
			material_reload_material_callback(material_header);
	}
	return ret;
}

static int materialReloadSpecific(const char *filename)
{
	int ret=1;
	char buf[256];
	char buf2[256];
	getFileNameNoExt(buf, filename);

	strcpy(buf2, buf);
	ret &= materialReloadTemplate(materialGetTemplateByName(buf2), filename);

	sprintf(buf2, "%s_OneOff", buf);
	ret &= materialReloadTemplate(materialGetTemplateByName(buf2), filename);

	strcpy(buf2, buf);
	ret &= materialReloadMaterial(materialFindNoDefault(buf2, 0), filename);

	sprintf(buf2, "%s_default", buf);
	ret &= materialReloadMaterial(materialFindNoDefault(buf2, 0), filename);

	return ret;
}

AUTO_RUN;
void RegisterMaterialDict(void)
{
	resRegisterIndexOnlyDictionary(MATERIAL_DICT, RESCATEGORY_ART);
}


static void materialLoadTemplatesAndMaterials(void)
{
// Shared memory doesn't work with reloading because of stashtable:
	ParserLoadFiles("materials", ".Material", "Materials.bin", PARSER_ALLOW_BINS_WITH_ERRORS_AND_RELOADING|PARSER_BINS_ARE_SHARED, parse_MaterialLoadInfo, &material_load_info);
}

static int materialReloadSubStructCallback(void *structPtr, void *oldStructPtr, ParseTable *tpi, eParseReloadCallbackType reason)
{
	bool bNeedTemplateRehash=false;
	if (tpi == parse_ShaderTemplate) {
		if (reason == eParseReloadCallbackType_Add) {
			bNeedTemplateRehash = true;
		} else if (reason == eParseReloadCallbackType_Delete) {
			bNeedTemplateRehash = true;
		} else if (reason == eParseReloadCallbackType_Update) {
			bNeedTemplateRehash = true; // Need to do this on Updates because these stash tables are shallow copies
			// HACK: Nulling this out, so it doesn't get freed twice
			((ShaderTemplate*)oldStructPtr)->graph_parser.graph_render_info = NULL;
		}
			
	} else if (tpi == parse_MaterialData) {
		if (reason == eParseReloadCallbackType_Add) {
			// This can be either a new one, or an update
			// We move it to the appropriate place later on
		} else if (reason == eParseReloadCallbackType_Delete) {
			assert(0); // Only adds *new* MaterialDatas as far as the low-level stuff is concerned
		} else if (reason == eParseReloadCallbackType_Update) {
			assert(0); // Only adds *new* MaterialDatas as far as the low-level stuff is concerned
		}
	} else {
		assertmsg(0, "Invalid tpi passed in");
	}
	if (bNeedTemplateRehash)
		buildTemplateHash(&material_load_info);
	return true;
}

// static int opReloadSubStructCallback(ShaderOperationDef *structPtr, ShaderOperationDef *oldStructPtr, ParseTable *tpi, eParseReloadCallbackType reason)
// {
// 	return true;
// }

static void opReloadCallback(const char *relpath, int when)
{
	int i;
	if (strEndsWith(relpath, ".tmp"))
		return;
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	ParserReloadFileToDictionary(relpath, g_hOpDefsDict);
	// TODO: Verify all materials
	for (i=0; i<eaSizeUnsafe(&material_reload_callbacks); i++) 
		material_reload_callbacks[i](relpath, when);
}

static WLUsageFlags getUsageFlags(const char *relpath)
{
	WLUsageFlags ret=0;
	char name[MAX_PATH];
	char name2[MAX_PATH];
	getFileNameNoExt(name, relpath);
	strcpy(name2, name);
	strcat(name2, "_default");
	FOR_EACH_IN_EARRAY(material_load_info.material_headers, Material, material_header)
	{
		if (stricmp(material_header->material_name, name)==0 ||
			stricmp(material_header->material_name, name2)==0)
		{
			ret |= material_header->world_props.usage_flags;
		}
	}
	FOR_EACH_END;
	return ret;
}

static void setUsageFlags(const char *relpath, WLUsageFlags usage_flags)
{
	char name[MAX_PATH];
	char name2[MAX_PATH];
	getFileNameNoExt(name, relpath);
	strcpy(name2, name);
	strcat(name2, "_default");
	FOR_EACH_IN_EARRAY(material_load_info.material_headers, Material, material_header)
	{
		if (stricmp(material_header->material_name, name)==0 ||
			stricmp(material_header->material_name, name2)==0)
		{
			material_header->world_props.usage_flags |= usage_flags;
		}
	}
	FOR_EACH_END;
}

static void materialReloadCallback(const char *relpath, int when)
{
	int i;
	WLUsageFlags old_usage_flags;
	if (strEndsWith(relpath, ".tmp"))
		return;

	EnterCriticalSection(&model_lod_init_cs); // Other threads access the material hashtable while in this CS

	old_usage_flags = getUsageFlags(relpath);
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	sharedMemoryUnshare(bsGetDataPtr(material_load_info.packed_data->bs));
	ParserReloadFile(relpath, parse_MaterialLoadInfo, &material_load_info, materialReloadSubStructCallback, 0);

	// Setup graph pointers and verify material (just the one that was reloaded)
	materialReloadSpecific(relpath);
	setUsageFlags(relpath, old_usage_flags);
	// Fix any models who wanted these materials
	if (modelRebuildMaterials())
	{
		// TODO: do something to cause swaps to be re-evaluated, this works once: reloadFileLayer(NULL); // Need to re-bin/re-swap/etc
	}
	for (i=0; i<eaSizeUnsafe(&material_reload_callbacks); i++) 
		material_reload_callbacks[i](relpath, when);

	LeaveCriticalSection(&model_lod_init_cs);
}

void materialAddCallbacks(MaterialReloadCallbackFunc func, MaterialReloadMaterialCallbackFunc funcMaterial, MaterialReloadTemplateCallbackFunc funcTemplate, MaterialSetUsageCallbackFunc funcSetUsage, MaterialValidateMaterialDataCallbackFunc funcValidateMaterialData)
{
	if (func) {
		eaPush((cEArrayHandle*)&material_reload_callbacks, func);
	}
	if (funcMaterial) {
		assert(!material_reload_material_callback);
		material_reload_material_callback = funcMaterial;
	}
	if (funcTemplate ) {
		assert(!material_reload_template_callback);
		material_reload_template_callback = funcTemplate;
	}
	if (funcSetUsage) {
		assert(!material_set_usage_callback);
		material_set_usage_callback = funcSetUsage;
	}
	if (funcValidateMaterialData) {
		assert(!material_validate_material_data_callback);
		material_validate_material_data_callback = funcValidateMaterialData;
	}
}

static int materialCountOneOffs(void)
{
	int i;
	int ret=0;
	for (i=eaSize(&material_load_info.templates)-1; i>=0; i--)
		if (strEndsWith(material_load_info.templates[i]->template_name, "_OneOff"))
			ret++;
	return ret;
}

static __time32_t materialTimestampFunc(const char *relpath)
{
	char buf[MAX_PATH];
	ShaderTemplate *shader_template;
	if (!strStartsWith(relpath, "materials/"))
		return 0;
	getFileNameNoExt(buf, relpath);
	if (shader_template=materialGetTemplateByName(buf)) {
		return shader_template->graph->timestamp;
	}
	return 0;
}

static void materialNameParse_s(SA_PARAM_NN_STR const char* materialName, SA_PRE_GOOD SA_POST_NN_STR char* outName, int outName_size, SA_PRE_NN_FREE SA_POST_NN_VALID bool* outIsFallback, SA_PRE_NN_FREE SA_POST_NN_VALID int *fallback_index )
{
	const char* colon;
	colon = strchr( materialName, ':' );
	
	*outIsFallback = false;
	
	if( colon ) {
		const char* flags = colon + 1;
		strncpy_s( SAFESTR2( outName ), materialName, colon - materialName );

		if( stricmp( flags, "Fallback" ) == 0 ) {
			*outIsFallback = true;
			*fallback_index = -1;
		} else if (strStartsWith(flags, "Fallback:")) {
			*outIsFallback = true;
			*fallback_index = atoi(flags + 9 /*strlen("Fallback:")*/);
		} else {
			Errorf( "Invalid material flags: %s", flags );
		}
	} else {
		strcpy_s( SAFESTR2( outName ), materialName );
	}
}

#define materialNameParse(materialName,outName,outIsFallback, outFallbackIndex)			\
	materialNameParse_s((materialName), SAFESTR(outName), (outIsFallback), (outFallbackIndex))

static char* materialFullName_s(SA_PARAM_NN_VALID Material *material, char* buffer, int buffer_size)
{
	if( material->override_fallback_index ) {
		
		if (material->override_fallback_index == -1)
			sprintf_s( SAFESTR2( buffer ), "%s:Fallback", material->material_name );
		else
			sprintf_s( SAFESTR2( buffer ), "%s:Fallback:%d", material->material_name, material->override_fallback_index );
	} else {
		strcpy_s( SAFESTR2( buffer ), material->material_name );
	}

	return buffer;
}

#define materialFullName(material, buffer)		\
	materialFullName_s(material, SAFESTR(buffer))

void materialLoad(void) // Loads material definitions
{
	loadstart_printf("Materials startup...");

	if (isProductionMode())
		fileRegisterLastChangedNonExistentFunction(materialTimestampFunc);

	loadstart_printf("Loading Materials..."); // Matched in postprocess callback
	materialLoadOps(); // ~ 0.02s
	materialLoadTemplatesAndMaterials();

	default_material = materialFind("default", WL_FOR_UTIL);
	assertmsg(default_material, "Failed to find default material.");
	invisible_material = materialFind("invisible", WL_FOR_UTIL);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "materials/*.Material", materialReloadCallback);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "shaders/operations/*.Op", opReloadCallback);

	loadend_printf("Materials startup done (%d Tmplts, %d One-offs, %d Mtls).",
		eaSize(&material_load_info.templates) - materialCountOneOffs(),
		materialCountOneOffs(),
		stashGetCount(material_load_info.stMaterialDataInfos));
}

void materialCheckDependencies(void)
{
#if !PLATFORM_CONSOLE // At least for now, no one does primary development on Xbox, and this step is especially slow over the network share
	if (isDevelopmentMode()) // Don't even want to load these files in production mode
	{
		loadstart_printf("Verifying Material dependencies...");
		materialVerifyDeps();
		loadend_printf(" done.");
	}
#endif
}

void materialSetUsage(Material *material, WLUsageFlags use_flags)
{
	if (!(material->world_props.usage_flags & use_flags)) {
		material->world_props.usage_flags |= use_flags;
		if (material_set_usage_callback)
			material_set_usage_callback(material, use_flags);
	}
}

bool materialExists(const char *name)
{
	char realName[ MAX_PATH ];
	bool isFallback;
	int fallback_index;

	MaterialDataInfo *mdi;

	materialNameParse(name, realName, &isFallback, &fallback_index );
	
	return stashFindPointer(material_load_info.stMaterialDataInfos, realName, &mdi);
}

void materialReleaseData(Material *material_header)
{
	if (!material_header->material_data)
		return;
	if (!material_header->material_data->from_packed)
		return;
	StructDestroy(parse_MaterialData, material_header->material_data);
	material_header->material_data = NULL;
}

// Releases all MaterialDatas (will unpack on the fly as needed)
AUTO_COMMAND ACMD_CATEGORY(Debug);
void materialDataReleaseAll(void)
{
	FOR_EACH_IN_EARRAY(material_load_info.material_headers, Material, material)
		materialReleaseData(material);
	FOR_EACH_END
}

// Whenever Unpack is called, the data will ALWAYS point to the fallback being referred to by the index, regardless of quality.
// After the material is unpacked, you MUST call materialDataUpdateFallback in some fashion
// (such as calling materialUpdateFallback which will call materialDataUpdateFallback for you!) with the material's override_fallback_index
// set to 0 if you wish the material to use the fallback expected due to compatibility or quality unless you specifically want to use that shader.
// The material will then need to be updated to reflect the new material data (such as calling gfxMaterialsInitMaterial).
void materialUnpack(MaterialDataInfo *mdi, MaterialData *material_data)
{
	Material *material;
	PhysicalProperties *physicalProperties;
	
	if (!(material = mdi->material)) {
		// Create a new Material from the packed data
		mdi->material = material = calloc(sizeof(*material), 1);
		eaPush(&material_load_info.material_headers, material);
	}
	if (!material_data) {
		if (material->override_fallback_index) {
			// Based on an existing material
			const MaterialData* oldMaterialData = materialFindData(material->material_name);
			assert(oldMaterialData);
			material_data = StructClone(parse_MaterialData, oldMaterialData);
			assert(material_data);
			material_data->override_fallback_index = material->override_fallback_index + 1;
			material_data->from_packed = false;
			materialDataInit(material_data);
		} else {
			// Unpack the MaterialData
			assert(mdi->data_offset != (U32)-1);
			material_data = StructUnpack(parse_MaterialData, material_load_info.packed_data, mdi->data_offset);
			material_data->override_fallback_index = 1;
			material_data->from_packed = 1;
			materialDataInit(material_data);
			//printf("unpacked %s\n", material_data->material_name);;
		}
		--material_data->override_fallback_index;
	}
	// Re-copying a number of fields even if we have a Material already, because this is used for reload
	material->material_name = material_data->material_name;
	if (material->material_data) {
		if (!material->material_data->from_packed) {
			// From a previous reload
			StructDestroy(parse_MaterialData, material->material_data);
		} else {
			materialReleaseData(material);
		}
	}
	material->material_data = material_data;
	material->incompatible = material_data->incompatible;
	material->shader_quality = material_data->shader_quality;
	material->graphic_props.flags = material_data->graphic_props.flags;
	material->graphic_props.lighting_contribution[0] = material_data->graphic_props.unlit_contribution;
	material->graphic_props.lighting_contribution[1] = material_data->graphic_props.diffuse_contribution;
	material->graphic_props.lighting_contribution[2] = material_data->graphic_props.specular_contribution;
	if ((physicalProperties = GET_REF(material_data->world_props.physical_properties))) {
		// Copy PhysicalProperties reference
		char buf[MAX_PATH];
		bool bResult = true;
		getFileNameNoExt(buf, physicalProperties->filename);
		bResult = physicalPropertiesFindByName(buf, &material->world_props.physical_properties.__handle_INTERNAL);
		assert(bResult);
	}
}

void materialUpdateFromData(Material *material_header, MaterialData *material_data)
{
	MaterialDataInfo mdi = {0};
	mdi.material = material_header;
	mdi.data_offset = (U32)-1;
	materialUnpack(&mdi, material_data);
}

void materialDestroy(Material *material_header)
{
	assert(!material_header->material_data);  // Otherwise a dangling reference somewhere
	if (material_header->material_name) {
		MaterialDataInfo *mdi;
		if (stashFindPointer(material_load_info.stMaterialDataInfos, material_header->material_name, &mdi)) {
			assert(mdi->material != material_header);
		}
	}
	REMOVE_HANDLE(material_header->world_props.physical_properties);
	free(material_header);
}

Material *materialFindNoDefaultEx(const char *name, WLUsageFlags use_flags, bool no_override)
{
	MaterialDataInfo *mdi;

	// The static checks should catch this :( 
	if (!name)
	{
		return NULL;
	}

	if( wl_state.matte_materials ) {
		name = allocAddString("black");
	}

	if (stashFindPointer(material_load_info.stMaterialDataInfos, name, &mdi)) {
		if (!mdi->material) {
			materialUnpack(mdi, NULL);
			if (no_override)
			{
				materialUpdateFallback(mdi->material);	// AND HERE'S THE PROBLEM SECTION!!!!!!!!!!!!!!!!!!!!  Can only be done in main thread.
			}
		}
		materialSetUsage(mdi->material, use_flags);
		return mdi->material;
	} else {
		// might be based on an existing material
		char realName[ MAX_PATH ];
		bool isFallback;
		int fallback_index=0;

		materialNameParse( name, realName, &isFallback, &fallback_index );
		if (isFallback && stashFindPointer(material_load_info.stMaterialDataInfos, realName, &mdi)) {
			MaterialDataInfo* newMdi;

			assert(fallback_index); // Should be -1 or index+1

			if (!mdi->material) {
				materialUnpack(mdi, NULL);
				assert( mdi->material );
			}

			newMdi = calloc(1, sizeof(*newMdi));
			assert( newMdi->material == NULL );
			newMdi->material = calloc(1, sizeof(*newMdi->material));
			newMdi->data_offset = -1;
			
			newMdi->material->material_name = mdi->material->material_name;
			newMdi->material->override_fallback_index = fallback_index;

			stashAddPointer(material_load_info.stMaterialDataInfos, allocAddString(name), newMdi, true);
			materialSetUsage( newMdi->material, use_flags );
			return newMdi->material;
		} else {
			return NULL;
		}
	}
}

Material *materialFindEx(const char *name, WLUsageFlags use_flags, bool no_override)
{
	Material *ret;
	if (ret = materialFindNoDefaultEx(name, use_flags, no_override)) {
		return ret;
	} else {
		return default_material;
	}
}

bool shaderTemplateIsSupportedEx(const ShaderTemplate *shader_template, ShaderGraphFeatures disabled_features)
{
	ShaderGraphFeatures effective_supported_features;
	const ShaderGraph *graph;
	
	if (!shader_template)
		return false;

	if (!(graph = shader_template->graph))
		graph = &shader_template->graph_parser;

	effective_supported_features = (systemSpecsMaterialSupportedFeatures() & ~disabled_features);
	return ((graph->graph_features & effective_supported_features)
			== graph->graph_features);
}

bool shaderTemplateIsSupported(const ShaderTemplate *shader_template)
{
	return shaderTemplateIsSupportedEx(shader_template, 0);
}

static char* shaderGraphFeaturesString( ShaderGraphFeatures features, char* buffer, int buffer_size )
{
	assert( buffer_size > 0 );

	if( features )
	{
		bool isFirst = true;
		int it;

		buffer[ 0 ] = '\0';
	
		for( it = 0; it < 32; ++it ) {
			if( (1 << it) & features ) {
				if( !isFirst ) {
					strcat_s( SAFESTR2( buffer ), " " );
				}
				strcat_s( SAFESTR2( buffer ), StaticDefineIntRevLookup( ShaderGraphFeaturesEnum, 1 << it ));
				isFirst = false;
			}
		}
	}
	else
	{
		sprintf_s( SAFESTR2( buffer ), "NONE" );
	}

	return buffer;
}

__forceinline void materialDataSetupFallback(MaterialData *material_data, ShaderTemplate **shader_template, MaterialFallback **fallback, int index)
{
	ANALYSIS_ASSUME(material_data->graphic_props.fallbacks);
	*fallback = material_data->graphic_props.fallbacks[ index ];
	*shader_template = materialGetTemplateByName( (*fallback)->shader_template_name );
	if (!shaderTemplateIsSupported(*shader_template))
		material_data->incompatible = 1;
}

// Return true if we should attempt to move on to the next fallback due to the current index pointing to a shader that is either
// incompatible with the video card or has a quality setting that is higher than the desired quality maximum.
static __forceinline bool materialDataTryNextFallback(ShaderTemplate *shader_template, bool forceOverride)
{
	U32 graph_quality;

	if (!shader_template)
		return true;

	graph_quality = shader_template->graph ? shader_template->graph->graph_quality : 0;

	return
		(
			(!forceOverride) &&	// This should only be set when unpacking the material.
			(
				// test compatibility
				(!shaderTemplateIsSupported( shader_template ) ) ||
				// we are compatible, now test to see if we even care about quality
				(
					(shader_template && gConf.bEnableShaderQualitySlider == SHADER_QUALITY_SLIDER_LABEL) &&
					// we care, so test for quality
					(graph_quality > wl_state.desired_quality)
				)
			)
		);
}

void materialDataUpdateFallback(MaterialData *material_data)
{
	if( wlIsClient() ) {
		MaterialFallback *fallback = &material_data->graphic_props.default_fallback;
		ShaderTemplate *shader_template = materialGetTemplateByName( fallback->shader_template_name );
		S32 fallback_it = 0;
		S32 fallback_count = eaSize(&material_data->graphic_props.fallbacks);
		ShaderGraphQuality original_quality;
		material_data->incompatible = 0;

		if (!shader_template)
			shader_template = default_shader_template; // If we don't set this, is_fallback gets set, and world binning dies because the root template is a fallback!

		original_quality = shader_template->graph ? shader_template->graph->graph_quality : 0;

		if (material_data->override_fallback_index==-1 && fallback_count)
		{
			fallback_it = fallback_count - 1;
			shader_template = NULL;
		} else if (material_data->override_fallback_index > 0)
		{
			fallback_it = material_data->override_fallback_index - 2;
			if (fallback_it < 0)
				fallback_it = 0;
			else
				shader_template = NULL;
		} else if (!shaderTemplateIsSupported(shader_template))
			material_data->incompatible = 1;


		// this function may be called before shader_template->graph gets set...
		if (shader_template && !shader_template->graph)
			shader_template->graph = &shader_template->graph_parser;

		while ( !shaderTemplateIsSupported(shader_template)) {
			if( fallback_it == fallback_count) {
				if (!shaderTemplateIsSupported(shader_template)) {
					char buffer[1024];
					ErrorFilenamef( material_data->filename,
						"No supported template in any of its fallbacks.\n"
						"Supported Features: %s",
						shaderGraphFeaturesString( systemSpecsMaterialSupportedFeatures(),
						SAFESTR( buffer )));
					fallback = NULL;
					shader_template = materialGetTemplateByName( "DefaultTemplate" );
					assert( shader_template );
				} else {
					// Leaving code in place because we may want this error to show up in the future.
					//ErrorFilenamef( material_data->filename,
					//	"No fallbacks with a quality setting low enough to match desired quality.\n");
					//assert( shader_template );
				}
				break;
			}
			materialDataSetupFallback(material_data, &shader_template, &fallback, fallback_it);
			if (!shaderTemplateIsSupported(shader_template))
				material_data->incompatible = 1;
			++fallback_it;
		}

		if (gConf.bEnableShaderQualitySlider == SHADER_QUALITY_SLIDER_LABEL && (!material_data->override_fallback_index))
		{
			while(((U32)shader_template->graph->graph_quality > wl_state.desired_quality) && (fallback_it < fallback_count)) {
				materialDataSetupFallback(material_data, &shader_template, &fallback, fallback_it);
				++fallback_it;
				if (!shaderTemplateIsSupported(shader_template))
				{
					materialDataSetupFallback(material_data, &shader_template, &fallback, fallback_it-1);
					break;
				}
			}
		}

		if (!shader_template)
			shader_template = materialGetTemplateByName( "DefaultTemplate" );

		if (gConf.bEnableShaderQualitySlider == SHADER_QUALITY_SLIDER_LABEL)
		{
			if (material_data->override_fallback_index)
				material_data->shader_quality = shader_template->graph->graph_quality;
			else
				material_data->shader_quality = original_quality;
		}
		else
		{
			material_data->shader_quality = 0;
		}

		material_data->graphic_props.shader_template = shader_template;
		material_data->graphic_props.active_fallback = fallback;
	}
}

void materialUpdateFallback(Material* material)
{
	materialDataUpdateFallback(materialGetDataNonConst(material));
}

void materialDataInit(MaterialData *material_data)
{
	materialDataUpdateFallback(material_data);
}

const MaterialData *materialGetData(const Material *material_header)
{
	return materialGetDataNonConst((Material*)material_header);
}

static MaterialData *materialGetDataNonConst(Material *material_header)
{
	char buffer[ MAX_PATH ];
	
	MaterialDataInfo *mdi;
	if (material_header->material_data)
		return material_header->material_data;
	if (stashFindPointer(material_load_info.stMaterialDataInfos, materialFullName(material_header, buffer), &mdi)) {
		materialUnpack(mdi, NULL);
	}
	assert(material_header->material_data);
	return material_header->material_data;
}

const MaterialData *materialFindData(const char *material_name)
{
	Material *material = materialFindNoDefault(material_name, 0);
	if (material)
		return materialGetData(material);
	return NULL;
}

bool materialNeedsAlphaSort(Material *material)
{
	return material->graphic_props.needs_alpha_sort;
}

bool materialHasTransparency(Material *material)
{
	return material->graphic_props.has_transparency;
}

F32 materialFindOperationSpecificValueFloatIndexed(const ShaderOperationValues *op_values, const char *input_name, int index, F32 default_value)
{
	const ShaderOperationSpecificValue *op_value = materialFindOperationSpecificValue2Const(op_values, input_name);
	if (!op_value || index >= eafSize(&op_value->fvalues)) {
		// Find default?
		return default_value;
	}
	ANALYSIS_ASSUME(op_value->fvalues);
	return op_value->fvalues[index];
}

ShaderInputMapping* materialFindInputMapping(MaterialFallback *fallback, const char *op_name)
{
	int i;

	if (!fallback)
		return NULL;
	
	for (i=eaSize(&fallback->input_mappings)-1; i>=0; i--) {
		ShaderInputMapping *input_mapping = fallback->input_mappings[i];
		if (stricmp(input_mapping->mapped_op_name, op_name)==0) {
			return input_mapping;
		}
	}
	
	return NULL;
}

F32 materialFindOperationSpecificValueFloat(const ShaderOperationValues *op_values, const char *input_name, F32 default_value)
{
	return materialFindOperationSpecificValueFloatIndexed(op_values, input_name, 0, default_value);
}

void materialGetOpNames(ShaderGraph *graph, const char ***ops)
{
	int i;
	for (i=eaSize(&graph->operations)-1; i>=0; i--)
		eaPush(ops, graph->operations[i]->op_name);
}

ShaderOperation *materialFindOpByName(ShaderGraph *graph, const char *op_name)
{
	int i;
	for (i=eaSize(&graph->operations)-1; i>=0; i--) {
		if (stricmp(op_name, graph->operations[i]->op_name)==0) {
			return graph->operations[i];
		}
	}
	return NULL;
}

void materialForEachMaterialName(ForEachMaterialNameCallback callback, void *userData)
{
	StashTableIterator iter = {0};
	StashElement elem;
	stashGetIterator(material_load_info.stMaterialDataInfos, &iter);
	while (stashGetNextElement(&iter, &elem)) {
		callback(userData, stashElementGetStringKey(elem));
	}
}

const char *materialGetFilename(Material *material)
{
	const MaterialData *material_data;
	if (!material)
		return NULL;
	material_data = materialGetData(material);
	return material_data->filename;
}


ShaderOperation *materialFindOpByType(ShaderGraph *shader_graph, ShaderOperationType op_type) // Really only useful for SOT_SINK
{
	int i;
	for (i=eaSize(&shader_graph->operations)-1; i>=0; i--) {
		const ShaderOperationDef *op_def = GET_REF(shader_graph->operations[i]->h_op_definition);
		if (op_def && op_def->op_type == op_type) {
			return shader_graph->operations[i];
		}
	}
	return NULL;
}

ShaderInputEdge *materialFindInputEdgeByName(ShaderOperation *op, const char *input_name)
{
	int i;
	for (i=eaSize(&op->inputs)-1; i>=0; i--) {
		ShaderInputEdge *input_edge = op->inputs[i];
		if (stricmp(input_edge->input_name, input_name)==0)
			return input_edge;
	}
	return NULL;
}

const ShaderInputEdge *materialFindInputEdgeByNameConst(const ShaderOperation *op, const char *input_name)
{
	int i;
	for (i=eaSize(&op->inputs)-1; i>=0; i--) {
		const ShaderInputEdge *input_edge = op->inputs[i];
		if (stricmp(input_edge->input_name, input_name)==0)
			return input_edge;
	}
	return NULL;
}

ShaderFixedInput *materialFindFixedInputByName(ShaderOperation *op, const char *input_name)
{
	int i;
	for (i=eaSize(&op->fixed_inputs)-1; i>=0; i--) {
		ShaderFixedInput *fixed_input = op->fixed_inputs[i];
		if (stricmp(fixed_input->input_name, input_name)==0) {
			assert(eafSize(&fixed_input->fvalues));
			return fixed_input;
		}
	}
	return NULL;
}

const ShaderFixedInput *materialFindFixedInputByNameConst(const ShaderOperation *op, const char *input_name)
{
	int i;
	for (i=eaSize(&op->fixed_inputs)-1; i>=0; i--) {
		const ShaderFixedInput *fixed_input = op->fixed_inputs[i];
		if (stricmp(fixed_input->input_name, input_name)==0) {
			assert(eafSize(&fixed_input->fvalues));
			return fixed_input;
		}
	}
	return NULL;
}

ShaderRuntimeInput *materialFindRuntimeInputByName(ShaderOperation *op, const char *input_name)
{
	int i;
	for (i=eaSize(&op->runtime_inputs)-1; i>=0; i--) {
		ShaderRuntimeInput *runtime_input = op->runtime_inputs[i];
		if (stricmp(runtime_input->input_name, input_name)==0) {
			return runtime_input;
		}
	}
	return NULL;
}

const ShaderRuntimeInput *materialFindRuntimeInputByNameConst(const ShaderOperation *op, const char *input_name)
{
	// safe to cast away const, since it comes back here
	return materialFindRuntimeInputByName((ShaderOperation*)op, input_name);
}


ShaderInput *materialFindShaderInputByName(ShaderOperation *op, const char *input_name)
{
	int i;
	const ShaderOperationDef *op_definition;
	if (!(op_definition = GET_REF(op->h_op_definition)))
		return NULL;
	for (i=eaSize(&(op_definition)->op_inputs)-1; i>=0; i--) {
		ShaderInput *shader_input = (op_definition)->op_inputs[i];
		if (stricmp(shader_input->input_name, input_name)==0)
			return shader_input;
	}
	return NULL;
}

ShaderOperationValues *materialFindOperationValues(MaterialData *material, const MaterialFallback *fallback, const char *op_name)
{
	int i;

	if (!fallback)
		return NULL;
	
	for (i=eaSize(&fallback->input_mappings)-1; i>=0; i--) {
		ShaderInputMapping *input_mapping = fallback->input_mappings[i];
		if (stricmp(input_mapping->mapped_op_name, op_name)==0) {
			return materialFindOperationValues(material, &material->graphic_props.default_fallback, input_mapping->op_name);
		}
	}
	
	for (i=eaSize(&fallback->shader_values)-1; i>=0; i--) {
		ShaderOperationValues *op_values = fallback->shader_values[i];
		if (stricmp(op_values->op_name, op_name)==0) {
			return op_values;
		}
	}
	return NULL;
}

const ShaderOperationValues *materialFindOperationValuesConst(const MaterialData *material, const MaterialFallback *fallback, const char *op_name)
{
	return materialFindOperationValues((MaterialData*)material, fallback, op_name);
}

ShaderOperationSpecificValue *materialFindOperationSpecificValue2(ShaderOperationValues *op_values, const char *input_name)
{
	int i;
	if (!op_values)
		return NULL;
	for (i=eaSize(&op_values->values)-1; i>=0; i--) {
		if (op_values->values[i]->input_name && stricmp(op_values->values[i]->input_name, input_name)==0)
			return op_values->values[i];
	}
	return NULL;
}

const ShaderOperationSpecificValue *materialFindOperationSpecificValue2Const(const ShaderOperationValues *op_values, const char *input_name)
{
	int i;
	if (!op_values)
		return NULL;
	for (i=eaSize(&op_values->values)-1; i>=0; i--) {
		if (op_values->values[i]->input_name && stricmp(op_values->values[i]->input_name, input_name)==0)
			return op_values->values[i];
	}
	return NULL;
}

ShaderOperationSpecificValue *materialFindOperationSpecificValue(MaterialData *material, const MaterialFallback *fallback, const char *op_name, const char *input_name)
{
	return materialFindOperationSpecificValue2(materialFindOperationValues(material, fallback, op_name), input_name);
}

ShaderOperationSpecificValue *materialAddOperationSpecificValue(MaterialData *material, MaterialFallback *fallback, const char *op_name, const char *input_name)
{
	int i;
	ShaderOperationSpecificValue *ret;
	ShaderOperationValues *op_values;

	op_values = materialFindOperationValues(material, fallback, op_name);
	if (!op_values) {
		op_values = StructAlloc(parse_ShaderOperationValues);
		eaPush(&fallback->shader_values, op_values);
		op_values->op_name = allocAddString(op_name);
	}
	for (i=eaSize(&op_values->values)-1; i>=0; i--) {
		if (stricmp(op_values->values[i]->input_name, input_name)==0)
			return op_values->values[i];
	}
	ret = StructAlloc(parse_ShaderOperationSpecificValue);
	ret->input_name = allocAddString(input_name);
	eaPush(&op_values->values, ret);
	return ret;
}


int materialFindInputIndexByName(const ShaderOperationDef *op_def, const char *input_name)
{
	int i;
	for (i=eaSize(&op_def->op_inputs)-1; i>=0; i--)
	{
		const ShaderInput *shader_input = op_def->op_inputs[i];
		if (stricmp(input_name, shader_input->input_name)==0)
			return i;
	}
	return -1;
}

int materialFindOutputIndexByName(const ShaderOperationDef *op_def, const char *output_name)
{
	int i;
	for (i=eaSize(&op_def->op_outputs)-1; i>=0; i--)
	{
		const ShaderOutput *shader_output = op_def->op_outputs[i];
		if (stricmp(output_name, shader_output->output_name)==0)
			return i;
	}
	return -1;
}

static void verifyDeps(MaterialDeps **deps)
{
	int i;
	int j;
	for (i=0; i<eaSize(&deps); i++) {
		MaterialDeps *dep = deps[i];
		for (j=0; j<eaSize(&dep->dependency); j++) {
			const char *material_name = dep->dependency[j];
			if (!materialExists(material_name)) {
				if (!fileExists(dep->src_file))
				{
					char buf[MAX_PATH];
					strcpy(buf, dep->dep_filename);
					changeFileExt(buf, ".geo2", buf);
					if (fileExists(buf))
						dep->src_file = allocAddString(buf);
				}
				ErrorFilenameGroupf(dep->src_file, "Art", 30, "Geometry references non-existent material named \"%s\".", material_name);
			}
		}
	}
}

TextParserResult fixupMaterialDepsQuick(MaterialDepsQuickList *deps, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (eFixupType == FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES)
	{
		// Make an erray of just the unique names, and free the big list for quicker validation
		int i, j;
		StashTable stTemp = stashTableCreateWithStringKeys(1024, StashDefault);
		for (i=0; i<eaSize(&deps->deps_source); i++) {
			MaterialDeps *dep = deps->deps_source[i];
			for (j=0; j<eaSize(&dep->dependency); j++) {
				const char *material_name = dep->dependency[j];
				
				// [COR-3742]
				//
				// An empty material dep... this shouldn't ever happen, but
				// it can and there is no special meaning.
				if (!material_name || !material_name[0])
					continue;
				
				if (stashAddPointer(stTemp, material_name, material_name, false))
					eaPush(&deps->deps_strings, material_name);
			}
		}
		stashTableDestroy(stTemp);
		eaDestroyStruct(&deps->deps_source, parse_MaterialDeps);
	}
	return PARSERESULT_SUCCESS;
}



bool materialVerifyDepsQuick(void)
{
	MaterialDepsQuickList deps = {0};
	bool bRet=true;
	int i;
	ParserLoadFiles("object_library", ".MaterialDeps", "MaterialDepsOLQuick.bin", PARSER_BINS_ARE_SHARED, parse_MaterialDepsQuickList, &deps);
	for (i=0; bRet && i<eaSize(&deps.deps_strings); i++) 
		if (!materialExists(deps.deps_strings[i]))
			bRet = false;
	StructDeInit(parse_MaterialDepsQuickList, &deps);
	if (bRet) {
		ParserLoadFiles("character_library", ".MaterialDeps", "MaterialDepsCLQuick.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_MaterialDepsQuickList, &deps);
		for (i=0; bRet && i<eaSize(&deps.deps_strings); i++) 
			if (!materialExists(deps.deps_strings[i]))
				bRet = false;
		StructDeInit(parse_MaterialDepsQuickList, &deps);
	}

	return bRet;
}

void materialVerifyDepsSlow(void)
{
	MaterialDeps **deps=NULL;

	ParserLoadFiles("object_library", ".MaterialDeps", "MaterialDepsOLSlow.bin", PARSER_BINS_ARE_SHARED, parse_material_deps_list, &deps);
	verifyDeps(deps);
	// BZ- I really want to add these to the object library dependencies, but they're per-file instead of per-model
	StructDeInitVoid(parse_material_deps_list, &deps);
	// optional for outsource build
	ParserLoadFiles("character_library", ".MaterialDeps", "MaterialDepsCLSlow.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_material_deps_list, &deps);
	verifyDeps(deps);
	StructDeInitVoid(parse_material_deps_list, &deps);
	assert(deps == NULL);
}

void materialVerifyDeps(void)
{
	// Two step verification for slight speed-up: first load a smaller .bin file which contains just the material names
	//   depended upon (no source information), and check that, and if any of those fail, load the
	//   detailed information to get a person to blame.
	//if (!materialVerifyDepsQuick() || gbMakeBinsAndExit)
	// BZ- No longer do this. The slow one is basically exactly as fast as the fast one when there are no errors, and is FASTER when there are.
	materialVerifyDepsSlow();
}

static MaterialDeps **vomdff_deps=NULL;
static const char *vomdff_pcModelNameCache;
static const char *vomdff_model_header_filename;

void materialVerifyObjectMaterialDepsForFxDone(void)
{
	StructDeInitVoid(parse_material_deps_list, &vomdff_deps);
	vomdff_pcModelNameCache = NULL;
}

bool materialVerifyObjectMaterialDepsForFx(const char *pcFileName, const char *pcModelName)
{
	bool bRet=true;
	if (isDevelopmentMode() && wl_state.material_validate_for_fx)
	{
		if (vomdff_pcModelNameCache != pcModelName)
		{
			ModelHeader *model_header = wlModelHeaderFromName(pcModelName);
			char depsfile[MAX_PATH];
			extern ParseTable parse_material_deps_list[];
			changeFileExt(model_header->filename, ".MaterialDeps", depsfile);
			StructDeInitVoid(parse_material_deps_list, &vomdff_deps);
			if (fileExists(depsfile))
				ParserLoadFiles(NULL, depsfile, NULL, 0, parse_material_deps_list, &vomdff_deps);
			vomdff_pcModelNameCache = pcModelName;
			vomdff_model_header_filename = model_header->filename;
		}
		FOR_EACH_IN_EARRAY(vomdff_deps, MaterialDeps, dep)
		{
			FOR_EACH_IN_EARRAY(dep->dependency, const char, pcMaterialName)
			{
				const char *pcTemplateName = NULL;
				Material *material = materialFindNoDefault(pcMaterialName, 0);
				if (material && !wl_state.material_validate_for_fx(material, &pcTemplateName, false))
				{
					ErrorFilenameGroupf(pcFileName, "FX", 3,
						"FX references geometry in file \"%s\" which references material \"%s\" referencing non-preloaded/non-FX template \"%s\"",
						vomdff_model_header_filename,
						pcMaterialName,
						pcTemplateName);
					bRet = false;
				}
			}
			FOR_EACH_END;
		}
		FOR_EACH_END;
	}
	return bRet;
}

static void printObjectsWhoUseMe(char **buf, MaterialDeps **deps, ShaderTemplate *templ)
{
	int i;
	int j;
	int count = 0;

	estrConcatf(buf, "\r- Object Dependencies -\r");
	for (i=0; i<eaSize(&deps); i++) {
		MaterialDeps *dep = deps[i];
		for (j=0; j<eaSize(&dep->dependency); j++) {
			const char *material_name = dep->dependency[j];
			Material *material;
			if (material = materialFindNoDefault(material_name, 0)) {
				const MaterialData *material_data = materialGetData(material);
				if (materialDataHasShaderTemplate(material_data, templ->template_name)) {
					//printf("%s\n", dep->src_file);
					estrConcatf(buf, "%s\r", dep->src_file);
					count++;
				}
			}
		}
	}
	estrConcatf(buf, "Total object references:	%d\r", count);
}

void materialPrintObjectsWhoUseMe(ShaderTemplate *templ)
{
	MaterialDeps **deps=NULL;
	size_t len = 0;

	if (!templ) {
		Errorf("Couldn't find template!");
	} else {
		char *buf=NULL;
		estrStackCreate(&buf);
		ParserLoadFiles("object_library", ".MaterialDeps", "MaterialDepsOL.bin", PARSER_BINS_ARE_SHARED, parse_material_deps_list, &deps);
		printObjectsWhoUseMe(&buf, deps, templ);
		StructDeInitVoid(parse_material_deps_list, &deps);
		// optional for outsource build
		ParserLoadFiles("character_library", ".MaterialDeps", "MaterialDepsCL.bin", PARSER_BINS_ARE_SHARED|PARSER_OPTIONALFLAG, parse_material_deps_list, &deps);
		printObjectsWhoUseMe(&buf, deps, templ);
		StructDeInitVoid(parse_material_deps_list, &deps);
		winCopyToClipboard(buf);
		estrDestroy(&buf);
	}
}

void materialGetMapsWhoUseMe(ShaderTemplate *templ, char **buf)
{
	MaterialDeps **deps=NULL;
	size_t len = 0;

	if (!templ) {
		Errorf("Couldn't find template!");
	} else {
		int i;
		int j;
		int count = 0;
		WorldDependenciesList list = {0};

		ParserLoadFiles("bin/geobin", ".deps", NULL, 0, parse_WorldDependenciesList, &list);

		for (i=0; i<eaSize(&list.deps); i++) {
			WorldDependenciesParsed *dep = list.deps[i];
			for (j=0; j<eaSize(&dep->material_deps); j++) {
				const char *material_name = dep->material_deps[j];
				Material *material;
				if (material = materialFindNoDefault(material_name, 0)) {
					const MaterialData *material_data = materialGetData(material);
					if (materialDataHasShaderTemplate(material_data, templ->template_name)) {
						//printf("%s\n", dep->src_file);
						estrConcatf(buf, "%s\n", dep->filename);
						count++;
						break;
					}
				}
			}
		}

		StructDeInit(parse_WorldDependenciesList, &list);

		estrConcatf(buf, "Total map references:	%d\n", count);
	}
}

void materialGetMaterialNamedTextures(Material *material_header, MaterialNamedTexture ***texture_names)
{
	int i, j, k, fallback_it;
	const MaterialData *material;

	if (!material_header || !texture_names)
		return;

	// TODO: Can this just use the MaterialRenderInfo?
	material = materialGetData(material_header);

	for (fallback_it = -1; fallback_it < eaSize(&material->graphic_props.fallbacks); ++fallback_it)
	{
		const MaterialFallback* fallback;
		if (fallback_it < 0)
			fallback = &material->graphic_props.default_fallback;
		else
			fallback = material->graphic_props.fallbacks[fallback_it];

		for (i = 0; i < eaSize(&fallback->shader_values); ++i)
		{
			ShaderOperationValues *value = fallback->shader_values[i];
			for (j = 0; j < eaSize(&value->values); ++j)
			{
				ShaderOperationSpecificValue *specific_value = value->values[j];
				for (k = 0; k < eaSize(&specific_value->svalues); ++k)
				{
					const char *tex_name = specific_value->svalues[k];
					MaterialNamedTexture *matNamedTexture = calloc(1,sizeof(MaterialNamedTexture));

					matNamedTexture->op = allocAddString(value->op_name);
					matNamedTexture->input = allocAddString(specific_value->input_name);
					matNamedTexture->texture_name = allocAddString(specific_value->svalues[0]);
					eaPush(texture_names, matNamedTexture);
				}
			}
		}
	}
}

void materialGetTextureNames(const Material *material_header, StashTable texture_names, char **texture_swaps)
{
	int i, j, k, l, fallback_it;
	const MaterialData *material;

	if (!material_header || !texture_names)
		return;

	// TODO: Can this just use the MaterialRenderInfo?
	material = materialGetData(material_header);

	for (fallback_it = -1; fallback_it < eaSize(&material->graphic_props.fallbacks); ++fallback_it)
	{
		const MaterialFallback* fallback;
		if (fallback_it < 0)
			fallback = &material->graphic_props.default_fallback;
		else
			fallback = material->graphic_props.fallbacks[fallback_it];
		
		for (i = 0; i < eaSize(&fallback->shader_values); ++i)
		{
			ShaderOperationValues *value = fallback->shader_values[i];
			for (j = 0; j < eaSize(&value->values); ++j)
			{
				ShaderOperationSpecificValue *specific_value = value->values[j];
				for (k = 0; k < eaSize(&specific_value->svalues); ++k)
				{
					const char *tex_name = specific_value->svalues[k];

					for (l = eaSize(&texture_swaps) - 2; l >= 0; l -= 2)
					{
						// TODO: If texture swaps are also allocAddString'd, we can just use pointer comparison
						//   Also, this stash table need not deep copy keys (if it is)
						if (stricmp(texture_swaps[l], tex_name)==0)
							tex_name = texture_swaps[l+1];
					}

					// TODO: Need to hook into TexWords and get texture names from there too?

					stashAddPointer(texture_names, tex_name, (char*)tex_name, false);
				}
			}
		}
	}
}

// Also accepts a file name instead of a name
bool materialSetPhysicalPropertiesByName(Material *material, const char *physical_properties_name_or_filename)
{
	char buf[MAX_PATH];
	bool bResult = true;
	assert(material->material_data);
	getFileNameNoExt(buf, physical_properties_name_or_filename);
	bResult = physicalPropertiesFindByName(buf, &material->material_data->world_props.physical_properties.__handle_INTERNAL);
	bResult = physicalPropertiesFindByName(buf, &material->world_props.physical_properties.__handle_INTERNAL);
	return bResult;
}

MaterialWorldPropertiesRunTime* materialGetWorldProperties(Material *material)
{
	return &material->world_props;
}

MaterialWorldPropertiesRunTime* materialGetWorldPropertiesEx(Material *material, Vec3 world_point)
{
	return &material->world_props;
}

TextParserResult fixupMaterialLoadInfo(MaterialLoadInfo *pMaterialLoadInfo, enumTextParserFixupType eFixupType, void *pExtraData)
{
	if (g_materials_skip_fixup)
		return 1;
	switch (eFixupType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		return materialTextProcessor(parse_MaterialLoadInfo, pMaterialLoadInfo);

	case FIXUPTYPE_POST_ALL_TEXT_READING_AND_INHERITANCE_DURING_LOADFILES:
		return materialPreProcessor(parse_MaterialLoadInfo, pMaterialLoadInfo);

	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
		// Just allocates memory early in case of shared memory
		return materialPostProcessor(parse_MaterialLoadInfo, pMaterialLoadInfo);

	case FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION:
		return materialPointerPostProcessor(parse_MaterialLoadInfo, pMaterialLoadInfo);

	case FIXUPTYPE_POST_RELOAD:
		return materialReloadPointerPostProcessor(parse_MaterialLoadInfo, pMaterialLoadInfo);

	}

	return 1;
}

MaterialNamedConstant **eaStaticTintColorArray=NULL;
MaterialNamedConstant staticTintColorData;
AUTO_RUN;
void staticTintColorDataInit(void) {
	staticTintColorData.name = allocAddStaticString("Color1");
	setVec4same(staticTintColorData.value, 1);
	eaPush(&eaStaticTintColorArray, &staticTintColorData);
}

const MaterialFallback* materialDataHasShaderTemplate(const MaterialData* material_data, const char* shader_template)
{ 
	const MaterialFallback* fallback = &material_data->graphic_props.default_fallback;

	if( stricmp( shader_template, fallback->shader_template_name ) == 0 ) {
		return fallback;
	} else {
		int fallback_it;

		for( fallback_it = 0; fallback_it != eaSize( &material_data->graphic_props.fallbacks ); ++fallback_it ) {
			fallback = material_data->graphic_props.fallbacks[ fallback_it ];
			if( stricmp( shader_template, fallback->shader_template_name ) == 0 ) {
				return fallback;
			}
		}
	}

	return NULL;
}

bool materialDataHasRequiredFallbacks( const MaterialData* materialData, ShaderTemplate** overrides )
{
	const MaterialGraphicPropertiesLoadTime* gfxProps = &materialData->graphic_props;
	int it;

	for( it = -1; it != eaSize( &gfxProps->fallbacks ); ++it ) {
		ShaderTemplate* templ;

		if( it < 0 ) {
			templ = materialGetTemplateByNameWithOverrides( gfxProps->default_fallback.shader_template_name, overrides );
		} else {
			templ = materialGetTemplateByNameWithOverrides( gfxProps->fallbacks[ it ]->shader_template_name, overrides );
		}

		if( !templ ) {
			continue;
		}

		// This function can get called before the template's graph
		// pointer gets set, due to reload nastiness.
		if( templ->graph ) {
			if( (templ->graph->graph_features & ~SGFEAT_SM20) == 0 ) {
				return true;
			}
		} else {
			if( (templ->graph_parser.graph_features & ~SGFEAT_SM20) == 0 ) {
				return true;
			}
		}
		
	}

	return false;
}

bool materialGetVecValue(Material *mat, const char *value_name, Vec4 value_out)
{
	ShaderOperationSpecificValue *val = NULL;
	const MaterialData *matData = NULL;
	int j;

	zeroVec4(value_out);

	if (!mat || !value_name)
		return false;

	matData = materialGetData(mat);
	if (matData)
	{
		ShaderOperationValues *value = matData->graphic_props.default_fallback.shader_values[0];
		for (j = 0; j < eaSize(&value->values); ++j)
		{
			ShaderOperationSpecificValue *specific_value = value->values[j];
			if (specific_value->input_name == value_name)
			{
				int it;
				for( it = 0; it < MIN( 4, eafSize( &specific_value->fvalues )); ++it ) {
					value_out[it] = specific_value->fvalues[it];
				}
				return true;
			}
		}
	}

	return false;
}

const char *materialGetStringValue(Material *mat, const char *value_name)
{
	const MaterialData *matData=NULL;
	int j;

	if (!mat || !value_name)
		return NULL;
	
	matData = materialGetData(mat);
	if (matData)
	{
		ShaderOperationValues *value = matData->graphic_props.default_fallback.shader_values[0];
		for (j = 0; j < eaSize(&value->values); ++j)
		{
			ShaderOperationSpecificValue *specific_value = value->values[j];
			if (specific_value->input_name == value_name)
				return specific_value->svalues[0];
		}
	}
	return NULL;
}

U32 materialGetDesiredQuality()
{
	return wl_state.desired_quality;
}
