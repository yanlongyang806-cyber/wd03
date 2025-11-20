#ifndef MATERIALS_H
#define MATERIALS_H
GCC_SYSTEM

#include "stdtypes.h"
#include "SystemSpecs.h" //< for ShaderGraphFeatures
#include "MaterialEnums.h"
#include "WorldLibEnums.h"
#include "../../Renderers/pub/RdrEnums.h"
#include "components/referencesystem.h"

typedef struct MaterialRenderInfo MaterialRenderInfo;
typedef struct PhysicalProperties PhysicalProperties;
typedef struct ShaderGraphRenderInfo ShaderGraphRenderInfo;
typedef struct StashTableImp* StashTable;
typedef struct MaterialEditor_ShaderOperationSpecificValue MaterialEditor_ShaderOperationSpecificValue;
typedef struct MaterialEditor2_ShaderOperation MaterialEditor2_ShaderOperation;
typedef struct BasicTexture BasicTexture;
typedef const void *DictionaryHandle;
typedef struct BitStream BitStream;
typedef struct PackedStructStream PackedStructStream;
typedef struct SerializablePackedStructStream SerializablePackedStructStream;
typedef struct MaterialDeps MaterialDeps;

//////////////////////////////////////////////////////////////////////////
// Overview:
//
// Operation: A programmer-defined element that can be inserted into a shader
//   graphic.  Contains information on inputs/outputs, and renderer-specific
//   implementations. (Only used by GraphicsLib)
//
// ShaderGraph: An artist defined graph of Operations, this translates into
//   a pixel shader. (Only used by GraphicsLib)
//
// ShaderTemplate: An artist defined collection of ShaderGraphs, with LOD
//   distances for each ShaderGraph.  Also a linking of texture inputs
//   between graphs. (Only used by GraphicsLib)
//
// Material: An artist-defined reference to a ShaderTemplate,
//   plus a set of values/texture names/etc to use as inputs to the various Operations.
//   Additionally includes material properties such as NOCOLL, etc
//   (Used by both GraphicsLib and WorldLib)
//
// General materials: Artists choose a ShaderTemplate, fill in values, saves a
//   Material referencing a ShaderTemplate, with warnings about missing
//   inputs.
// Making a new template: Artist starts with a blank slate or loads an
//   existing ShaderTemplate, drops various Operations, adds additional graphs,
//   sets LOD information, and chooses to save as a ShaderTemplate.
// Making a one-off material:  Artist starts with a blank slate or loads
//   an existing ShaderTemplate or Material.  If the artist has changed the Operation
//   layout when he goes to save, he is prompted to create a new ShaderTemplate, and
//   can choose one-off (in which case it is saved into the same file as the
//   material), or to save it as a ShaderTemplate, in which case it's saved
//   elsewhere.
//
// At load time we should search for and remove duplicate ShaderGraphs (after
//   data error checking).
//  
//////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////
// Structures for defining a shader "Operation"

// Data type: can have special code associated with it
// enum ShaderDataType

AUTO_ENUM;
typedef enum ShaderInputDefaultType {
	SIDT_NODEFAULT,	ENAMES(None)		// Requires an input!
	SIDT_VALUE,		ENAMES(Value)		// Vec, float, string, etc
	SIDT_COLOR,		ENAMES(Color0)		// Color0 register
	SIDT_TEXCOORD0,	ENAMES(TexCoord0)	// Texture coordinate 0 input
	SIDT_TEXCOORD1,	ENAMES(TexCoord1)	// Texture coordinate 1 input
} ShaderInputDefaultType;

AUTO_ENUM;
typedef enum ShaderOperationType {
	SOT_CUSTOM,	    ENAMES(Custom)      // Custom, one-off ops
	SOT_TEXTURE,	ENAMES(Texture)		// Texture operations
	SOT_SIMPLE,     ENAMES(Simple)      // Quick and simple operations
	SOT_ADVANCED,   ENAMES(Advanced)    // More complicated operations
	SOT_SINK,		ENAMES(Sink)		// Just the built-in one: Output

	// This should always be last
	SOT_END
} ShaderOperationType;

// A default value for an input, can include things such as constant values, or
//   references to global inputs such as texture coordinates or constant color registers
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndDefault);
typedef struct ShaderInputDefault {
	ShaderInputDefaultType default_type;	AST( NAME(Type) DEF(SIDT_NODEFAULT) ) // VecX, const0, const1, etc
	F32 *default_floats;					AST( NAME(Floats) )
	char **default_strings;					AST( NAME(Strings) )
} ShaderInputDefault;

// A specific input in a ShaderOperationDef
// e.g. N, "normal direction", 3 floats, SDTF_NORMAL, default: None
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndInput);
typedef struct ShaderInput {
	const char *input_name;				AST( POOL_STRING NAME(Name) )
	char *input_description;			AST( NAME(Description) )	// For tool-tips, etc.  Strip in -createbins mode?
	ShaderDataType data_type;			AST( NAME(Type) DEF(SDT_DEFAULT) )
	int num_floats;						AST( NAME(Float) )	// 3 -> Vec3, 4 -> Vec4
	Vec2 float_range;					AST( NAME(FloatRange) ) // Range that float values are allowed to have
	int num_texnames;					AST( NAME(Texture) )	// 0 or 1 (although a ShaderOperationDef may have multiple Texture inputs)
	bool input_hidden;					AST( NAME(Hidden) )
	bool input_no_auto_connect;			AST( NAME(NoAutoConnect) ) // Do not auto-connect outputs to this input in the MaterialEditor
	bool input_not_for_assembler;		AST( NAME(NotForAssembler) ) // Assembler should ignore this input
	bool input_no_manual_connect;		AST( NAME(NoManualConnect) ) // Do not allow connecting of another node to this input
	ShaderInputDefault input_default;	AST( NAME(Default) )	// Should validate the default values match the num_* here
	// AUTO_STRUCT Note: Also see parse_ShaderInputOneLine
} ShaderInput;

// A specific output in a ShaderOperationDef
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndOutput);
typedef struct ShaderOutput {
	const char *output_name;	AST( POOL_STRING NAME(Name) )
	char *output_description;	AST( NAME(Description) )
	int num_floats;				AST( NAME(Float) )		// 3 -> Vec3, 4 -> Vec4
	ShaderDataType data_type;	AST( NAME(Type) DEF(SDT_DEFAULT) )
	bool output_alpha_mode_and;	AST( NAME(AlphaModeAnd) ) // If true, then we only output alpha if all of the alpha_from members output alpha (defualt OR)
	const char **output_alpha_from;		AST( POOL_STRING NAME(AlphaFrom) )	// Which inputs provide the alphaness of the output
	const char *output_auto_connect;	AST( POOL_STRING NAME(AutoConnect) ) // Auto-connect to only inputs named this
	// AUTO_STRUCT Note: Also see parse_ShaderOutputOneLine
} ShaderOutput;

// A programmer-entered Operation definition (e.g. Refract, or Multiply)
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndOperationDef) AST_IGNORE(OperationDef) AST_FIXUPFUNC(fixupShaderOperationDef);
typedef struct ShaderOperationDef {
	const char *op_type_name;			AST( NAME(Name) KEY POOL_STRING )
	char *op_category;					AST( NAME(Category) ) // All elements of the same category must take the same number of the same semantic of inputs
	char *op_description;				AST( NAME(Description) )
	const char *op_parent_type_name;	AST( NAME(ParentName) POOL_STRING )
	char *op_option_name;				AST( NAME(OptionName) )
	char *op_default_option_name;		AST( NAME(DefaultOptionName) )
	ShaderOperationType op_type;		AST( NAME(Type) ) // For color-coding the UI, and other heuristics?
	ShaderInput **op_inputs;	AST( NAME(Input) REDUNDANT_STRUCT("Input:", parse_ShaderInputOneLine) )
	ShaderOutput **op_outputs;	AST( NAME(Output) REDUNDANT_STRUCT("Output:", parse_ShaderOutputOneLine) )
	const char *filename;				AST( NAME(FN) CURRENTFILE )
	float		min_shader_level;		AST( NAME(MinShader) )
} ShaderOperationDef;

//////////////////////////////////////////////////////////////////////////
// Structures for defining a shader graph (one LOD of a template)

AUTO_ENUM;
typedef enum SwizzleParams {
	SWIZZLE_X,
	SWIZZLE_Y,
	SWIZZLE_Z,
	SWIZZLE_W,
} SwizzleParams;

// A specific input (from another ShaderOperation)
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct ShaderInputEdge {
	const char *input_name;					AST( STRUCTPARAM POOL_STRING NAME(InputName) )		// Name of the input on the ShaderOperationDef that this satisfies
	const char *input_source_name;			AST( STRUCTPARAM POOL_STRING NAME(SourceName) )		// Name of the ShaderOperation to input from
	const char *input_source_output_name;	AST( STRUCTPARAM POOL_STRING NAME(SourceOutputName) )	// Name of the specific output to use (for operations with multiple outputs)
	U8 input_swizzle[4];					AST( STRUCTPARAM SUBTABLE(SwizzleParamsEnum) INDEX(0, SwizzleX)  INDEX(1, SwizzleY) INDEX(2, SwizzleZ) INDEX(3, SwizzleW) ) // 1,1,1,1 -> input.g; 0,1,2,3 -> input.xyzw, etc
} ShaderInputEdge;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndFixedInput");
typedef struct ShaderFixedInput {
	const char *input_name;		AST( STRUCTPARAM POOL_STRING NAME(InputName) )		// Name of the input on the ShaderOperationDef that this satisfies
	F32 *fvalues;				AST( NAME(FValue) )
} ShaderFixedInput;

AUTO_STRUCT;
typedef struct ShaderRuntimeInput {
	const char *input_name;		AST( POOL_STRING )
	S8 input_register;
	S8 input_swizzle_start;
	S8 input_swizzle_count;
	bool input_instance_param; // Could be an int if we instance multiple parameters
    S32 input_index;
} ShaderRuntimeInput;

// Individual node in a shader graph
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndOperation);
typedef struct ShaderOperation {
	const char *op_name;			AST( POOL_STRING STRUCTPARAM NAME(Name) )// Name for matching inputs/outputs, and for displaying in the editor
	const char *group_name;         AST( POOL_STRING NAME(Group) ) // Group name for displaying in the editor
	char *notes;					AST( NAME(Notes) ) // Notes provided by material designer
	//ShaderOperationDef *op_definition; // ParserLink to ShaderOperationDefs
	REF_TO(const ShaderOperationDef) h_op_definition;	AST( REQUIRED NON_NULL_REF NAME(OperationType) REFDICT(OperationDef) )
	ShaderInputEdge **inputs;		AST( NAME(Input) )
	ShaderFixedInput **fixed_inputs; AST( NAME(FixedInput) )
	ShaderRuntimeInput **runtime_inputs; NO_AST // Calculated at runtime for packing info
	
	Vec2 op_pos;					AST( NAME(Position) ) // xy position on screen for the MaterialEditor
	bool op_collapsed;				AST( NAME(Collapsed) )// For MaterialEditor
	bool instance_param;			AST( NAME(InstanceParam) )
	bool op_has_error;				NO_AST // For MaterialEditor
	void* op_editor_data;			NO_AST // For MaterialEditor2 only
} ShaderOperation;

// Note: these get automatically converted into #defines while assembling a material
AUTO_ENUM;
typedef enum ShaderGraphFlags
{
	SGRAPH_DEPRECATED = 0, ENAMES(HAS_BUMP) // Not used anymore, but don't cause parse errors
	SGRAPH_HANDLES_COLOR_TINT = 1 << 0, // Dynamically generated at load time
	SGRAPH_NO_ALPHACUTOUT = 1 << 1, ENAMES(NoAlphaCutout) // Parsed

	SGRAPH_NO_HDR = 1 << 3, ENAMES(NoHDR) // Parsed
	SGRAPH_NO_CACHING = 1 << 4, ENAMES(DoNotParseMe) // Flag set while editing a graph
	SGRAPH_ALPHA_PASS_ONLY = 1 << 5, ENAMES(AlphaPassOnly) // Parsed, set on templates which can only be used for the alpha pass
	SGRAPH_NO_NORMALMAP = 1 << 6, // Dynamically generated at load time
	SGRAPH_USE_AMBIENT_CUBE = 1 << 9, ENAMES(UseAmbientCube) // Use ambient cubemap lighting instead of sky/ground/side ambient
	SGRAPH_NO_TINT_FOR_HDR = 1 << 7, ENAMES(NoTintForHDR) // Do not use tint color when calculating HDR contribution
	SGRAPH_ALLOW_ALPHA_REF = 1 << 8, ENAMES(AllowAlphaRef) // For alpha objects, allow alpha ref to be used
	SGRAPH_ALLOW_REF_MIP_BIASE = 1 << 10, ENAMES(AllowRefMIPBias) // Allow Reflection texture MIP bias
	SGRAPH_ALPHA_TO_COVERAGE = 1 << 11, ENAMES(AlphaToCoverage) // Use AlphaToCoverage
	SGRAPH_BACKLIGHT_IN_SHADOW = 1 << 12, ENAMES(BacklightInShadow)
	SGRAPH_UNLIT_IN_SHADOW = 1 << 13, ENAMES(UnlitInShadow) // unlit only visible in shadow
	SGRAPH_EXCLUDE_FROM_CLUSTER = 1 << 14, ENAMES(ExcludeFromCluster) // unlit only visible in shadow
	SGRAPH_DIFFUSEWARP = 1 << 15, ENAMES(HasDiffuseWarp) // unlit only visible in shadow
	SGRAPH_ANISOTROPIC_SPEC = 1 << 16, ENAMES(AnisotropicSpec) // unlit only visible in shadow
} ShaderGraphFlags;

// A shader graph, values will be overridden
// May be referenced by multiple templates after we remove duplicates at load time
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndShaderGraph);
typedef struct ShaderGraph {
	const char *filename;							AST( NAME(FN) CURRENTFILE ) // Because ShaderGraph duplicates are merged, this may not accurately reflect what file a given material is actually referencing.
	U32 timestamp;									AST( TIMESTAMP NO_TEXT_SAVE )
	ShaderGraphRenderInfo *graph_render_info;		NO_AST
	ShaderOperation **operations;					AST( NAME(Operation) STRUCT(parse_ShaderOperation) )
	ShaderGraphFlags graph_flags;					AST( NAME(Flags) FLAGS ) // Various flags, some set dynamically, some from material editor
	ShaderGraphFeatures graph_features;				AST( NAME(Features) FLAGS SUBTABLE(ShaderGraphFeaturesEnum))
	ShaderGraphFeatures graph_features_overriden;	AST( NAME(FeaturesOverriden) FLAGS SUBTABLE(ShaderGraphFeaturesEnum)) // If set, then the corresponding flag in graph_features should not be automatically updated
	ShaderGraphReflectionType graph_reflection_type; AST( NAME(Reflection) SUBTABLE(ShaderGraphReflectionTypeEnum))
	char **defines;									AST( POOL_STRING NAME(Defines) )
	bool exclude_clustering;						AST( NAME(ExcludeClustering) )
	ShaderGraphQuality graph_quality;				AST(NAME(GraphQuality))
} ShaderGraph;

//////////////////////////////////////////////////////////////////////////
// Structures for defining a shader template (collection of graphs and LOD info)

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndGuide);
typedef struct ShaderGuide {
	Vec2 top_left;			AST( NAME(TL) )
	Vec2 bottom_right;		AST( NAME(BR) )
	void* op_editor_data;	NO_AST // For MaterialEditor2
} ShaderGuide;
	
// A link between two values/textures on two LODs of a template
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndShaderTemplateLink);
typedef struct ShaderTemplateLink {
	int source_lod;				AST( NAME(SourceLOD) )
	const char *source_op_name;	AST( POOL_STRING NAME(SourceOp) )
	int dest_lod;				AST( NAME(DestLOD) ) // MUST be larger than source_lod
	const char *dest_op_name;	AST( POOL_STRING NAME(DestOp) )
} ShaderTemplateLink;

// A shader template
AUTO_STRUCT AST_STARTTOK("") AST_IGNORE(ShaderTemplateLOD) AST_IGNORE(EndShaderTemplateLOD) AST_IGNORE(FarDist) AST_IGNORE(FarFade) AST_ENDTOK(EndShaderTemplate);
typedef struct ShaderTemplate {
	const char *template_name;	AST( POOL_STRING NAME(Name) STRUCTPARAM  )
	const char *filename;		AST( NAME(FN) CURRENTFILE )
	ShaderGraph graph_parser;	AST( NAME(ShaderGraph) ) // Parsed to here, store pointer to either here or to a duplicate one elsewhere 
	ShaderGraph *graph;			NO_AST
	ShaderGuide** guides;		AST( NAME(Guide) )
	ShaderTemplateLink **template_links; AST( NAME(ShaderTemplateLink) )
	bool is_autosave;			AST( NAME(IsAutosave) )
	int score;					AST( DEFAULT( -1 ))
	
	U32	shader_template_clean:1;	NO_AST // Whether or not this ShaderTemplate has been updated since changes by the MaterialEditor
} ShaderTemplate;

//////////////////////////////////////////////////////////////////////////
// Structures for defining a Material (reference to a template + values)
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndSpecificValue);
typedef struct ShaderOperationSpecificValue {
	const char *input_name;	AST( NAME(InputName) POOL_STRING STRUCTPARAM )
	F32 *fvalues;				AST( NAME(FValue) )
	const char **svalues;		AST( NAME(SValue) POOL_STRING )
	MaterialEditor_ShaderOperationSpecificValue *material_editor_data; NO_AST // Cleaned up by a destructor in the material editor
} ShaderOperationSpecificValue;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndOperationValue);
typedef struct ShaderOperationValues {
	const char *op_name;		AST( NAME(OpName) POOL_STRING )	// Which operation this sets the values for
	ShaderOperationSpecificValue **values;	AST( NAME(SpecificValue) )
} ShaderOperationValues;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndInputMapping);
typedef struct ShaderInputMapping {
	const char *op_name;					AST( NAME(OpName) POOL_STRING )
	const char *mapped_op_name;				AST( NAME(MappedOpName) POOL_STRING )
} ShaderInputMapping;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK(EndFallback);
typedef struct MaterialFallback {
	const char *shader_template_name;		AST( POOL_STRING NAME(Template) )
	ShaderInputMapping **input_mappings;	AST( NAME(InputMapping) )
	ShaderOperationValues **shader_values;	AST( NAME(OperationValue) )
	unsigned fallback_changed : 1;			NO_AST //< for Mated2
} MaterialFallback;

// // Graphic-specific properties of a material
// AUTO_STRUCT;
// typedef struct MaterialGraphicProperties {
// 	const char *shader_template_name;		AST( POOL_STRING NAME(Template) )
// 	ShaderTemplate *shader_template;		NO_AST
// 	ShaderOperationValues **shader_values;	AST( NAME(OperationValue) ) // shader_values contain a name and effect only values, not types, inputs, etc
// 	MaterialRenderInfo **render_info;		NO_AST // One per LOD in the ShaderTemplate
// 	RdrMaterialFlags flags;					AST( NAME(GfxFlags) SUBTABLE(RdrMaterialFlagsEnum) FLAGS )
// 	U32 needs_alpha_sort:1;					NO_AST
// 	U32 has_transparency:1;					NO_AST
// 	U32 material_clean:1;					NO_AST // Whether or not this Material has been updated since changes by the MaterialEditor
// 	U32 has_validation_error:1;				NO_AST // This material failed validation
// } MaterialGraphicProperties;

// Graphic-specific properties of a material
AUTO_STRUCT AST_STARTTOK("");
typedef struct MaterialGraphicPropertiesLoadTime {
	MaterialFallback default_fallback;		AST( EMBEDDED_FLAT )
	MaterialFallback **fallbacks;			AST( NAME(Fallback) )
	bool fallbacks_overriden;				AST( NAME(FallbacksOverriden) )

    // Active pieces of data
	MaterialFallback *active_fallback;		NO_AST
	ShaderTemplate *shader_template;		NO_AST
	
	RdrMaterialFlags flags;					AST( NAME(GfxFlags) SUBTABLE(RdrMaterialFlagsEnum) FLAGS MINBITS(5) )
	F32 unlit_contribution;					AST( NAME(UnlitContribution) FLOAT_HUNDREDTHS )
	F32 diffuse_contribution;				AST( NAME(DiffuseContribution) FLOAT_HUNDREDTHS )
	F32 specular_contribution;				AST( NAME(SpecularContribution) FLOAT_HUNDREDTHS )
	U32 max_reflect_resolution:4;			AST( NAME(MaxReflectResolution) ) // log2 of it
	U32 has_validation_error:1;				NO_AST // This material failed validation
} MaterialGraphicPropertiesLoadTime;
typedef struct MaterialGraphicPropertiesRunTime {
	MaterialRenderInfo* render_info;
	RdrMaterialFlags flags;
	Vec3 lighting_contribution;
	U32 needs_alpha_sort:1;
	U32 has_transparency:1;
	U32 material_clean:1;				// Whether or not this Material has been updated since changes by the MaterialEditor
} MaterialGraphicPropertiesRunTime;


// Game-specific properties of a material (do not generally affect drawing)
AUTO_STRUCT AST_STARTTOK("") AST_IGNORE(WorldFlags);
typedef struct MaterialWorldPropertiesLoadTime {
	REF_TO(PhysicalProperties) physical_properties;		AST(NAME(PhysicalProperties) NAME(SoundProfile) REFDICT(PhysicalProperties))
} MaterialWorldPropertiesLoadTime;
typedef struct MaterialWorldPropertiesRunTime {
	WLUsageFlags usage_flags;

	REF_TO(PhysicalProperties) physical_properties;
} MaterialWorldPropertiesRunTime;


AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct MaterialNamedConstant {
	const char *name;							AST( STRUCTPARAM POOL_STRING )// Have to be string pooled!
	Vec4 value;									AST( STRUCTPARAM )
} MaterialNamedConstant;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct MaterialNamedTexture {
	const char *op;								AST( STRUCTPARAM POOL_STRING )// Have to be string pooled!
	const char *input;							AST( STRUCTPARAM POOL_STRING )// Have to be string pooled!
	const char *texture_name;					AST( STRUCTPARAM POOL_STRING )
	BasicTexture *texture;						NO_AST
} MaterialNamedTexture;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct MaterialNamedDynamicConstant {
	const char *name;							AST( STRUCTPARAM POOL_STRING )// Have to be string pooled!
	ShaderDataType data_type;					AST( STRUCTPARAM )
	Vec4 value;									AST( STRUCTPARAM )
} MaterialNamedDynamicConstant;

// What is saved per-material by the artists
// Always references a template
// Also contains game-related information (add system for project-specificity?), like NoColl, and surface types
// AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndMaterial") AST_IGNORE_STRUCTPARAM("OldName") AST_FIXUPFUNC(fixupMaterial);
// typedef struct Material {
// 	const char *filename;						AST( NAME(FN) CURRENTFILE ) // Must be first for Reload
// 	char *material_name;						AST( NAME(N) ) // Not parsed
// 	MaterialGraphicProperties graphic_props;	AST( EMBEDDED_FLAT )
// 	MaterialWorldProperties world_props;		AST( EMBEDDED_FLAT )
// } Material;

// Parsed, compressed, freed
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("EndMaterial") AST_IGNORE_STRUCTPARAM("OldName") AST_FIXUPFUNC(fixupMaterialData)  AST_IGNORE(has_validation_error) AST_IGNORE(bad_material_is_mine) AST_IGNORE(disallowed_features);
typedef struct MaterialData {
	const char *filename;						AST( NAME(FN) CURRENTFILE ) // Must be first for Reload
	const char *material_name;					AST( POOL_STRING NAME(N) ) // Not parsed

	MaterialGraphicPropertiesLoadTime graphic_props;	AST( EMBEDDED_FLAT )
	MaterialWorldPropertiesLoadTime world_props;		AST( EMBEDDED_FLAT )
	bool is_autosave;       AST( NAME(IsAutosave) )
	S32 override_fallback_index:8;				NO_AST // Used to force fallbacks, 0 = none, -1 = lowest, otherwise index+1
	U32	from_packed:1;		NO_AST // If not, it's from a reload, or dynamic, and we can't free it, lest the material be unable to be re-built
	U32 incompatible:1;		NO_AST	// The original material is not compatible with this computer's video card.
	ShaderGraphQuality shader_quality;	NO_AST
} MaterialData;

// Referenced when someone needs a Material *
typedef struct Material {
	const char *material_name; // Pooled string
	MaterialData *material_data; // May be NULL
	S32 override_fallback_index:8; // Used to force fallbacks, 0 = none, -1 = lowest, otherwise index+1
	U32 incompatible:1;	// The original material is not compatible with this computer's video card.
	ShaderGraphQuality shader_quality;

	MaterialGraphicPropertiesRunTime graphic_props;
	MaterialWorldPropertiesRunTime world_props;
} Material;

typedef struct MaterialDataInfo {
	U32 data_offset; // Offset into global, packed material data
	Material *material; // Material, if one allocated
} MaterialDataInfo;

SA_RET_NN_VALID Material *materialFindEx(SA_PARAM_NN_STR const char *name, WLUsageFlags use_flags, bool no_override); // Will return a valid material even if not found
#define  materialFind( name, use_flags) materialFindEx( name, use_flags, true)
SA_RET_OP_VALID Material *materialFindNoDefaultEx(SA_PARAM_NN_STR const char *name, WLUsageFlags use_flags, bool no_override); // Will return NULL if not found
#define materialFindNoDefault(name, use_flags) materialFindNoDefaultEx(name, use_flags, true)
bool materialExists(SA_PARAM_NN_STR const char *name);

SA_ORET_NN_VALID const MaterialData *materialGetData(SA_PARAM_NN_VALID const Material *material);
SA_RET_OP_VALID const MaterialData *materialFindData(SA_PARAM_NN_STR const char *material_name);

bool materialNeedsAlphaSort(Material *material);
bool materialHasTransparency(Material *material); // Doesn't necessarily need alpha sort

extern Material *default_material, *invisible_material;

AUTO_STRUCT;
typedef struct MaterialDataInfoSerialize
{
	const char *material_name; AST( POOL_STRING )
	U32 data_offset;
	const char *filename; AST( POOL_STRING )
	const char **texture_deps; AST( POOL_STRING )
} MaterialDataInfoSerialize;

// Singleton struct that contains all the info about loaded materials
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("") AST_FIXUPFUNC(fixupMaterialLoadInfo);
typedef struct MaterialLoadInfo {
	// Parsed fields
	ShaderTemplate **templates;	AST( NAME(ShaderTemplate) )
	StashTable stTemplates;

	// This is generally not used.  It's for external systems that want to do funny business, like the map snaps
	StashTable stTemplateOverrides;

	MaterialData **material_datas;	AST( NAME(Material) ) // Only for parsing
	
	// Fields only written to .bin, read in, converted and freed
	SerializablePackedStructStream *packed_data_serialize;	AST( NO_TEXT_SAVE )
	MaterialDataInfoSerialize **data_infos_serialize;		AST( NO_TEXT_SAVE )

	// Run-time fields
	PackedStructStream *packed_data;	NO_AST
	StashTable stMaterialDataInfos;		NO_AST
	MaterialDataInfo *data_infos;		NO_AST // Not referenced, just hanging onto the pointer to free it later

	Material **material_headers;	NO_AST // Headers currently referenced somewhere
} MaterialLoadInfo;


#define MAX_MATERIAL_CONSTANT_MAPPING_VALUES 6

typedef struct MaterialScrollValues {
	Vec2 values;
	Vec2 lastValues;
	bool isIncremental;
} MaterialScrollValues;

typedef struct MaterialGradientValues {
	F32 start;
	F32 startFade;
	F32 end;
	F32 endFade;
	F32 minimum;
	F32 maximum;
} MaterialGradientValues;

typedef struct MaterialOscillatorValues {
	F32 frequency;
	F32 amplitude;
	F32 phase;
} MaterialOscillatorValues;

typedef struct MaterialUVRotationValues {
	Vec2 scale;
	F32 rotation;
	F32 rotationRate;
} MaterialUVRotationValues;

typedef struct MaterialConstantMapping {
	// Must be kept in sync with MaterialConstantMappingFake
	ShaderDataType data_type;
	int constant_index; // Or texture index
	int constant_subindex;
	U32 last_updated_timestamp;									NO_AST
	U32 last_updated_action_idx;								NO_AST

	// If this data type has any associated values (like the scroll amount for a texture scroll)
	union {
		F32 values[MAX_MATERIAL_CONSTANT_MAPPING_VALUES];
		MaterialScrollValues scroll;							NO_AST
		MaterialGradientValues timeGradient;					NO_AST
		MaterialOscillatorValues oscillator;					NO_AST
		MaterialUVRotationValues uvrotation;					NO_AST
		Vec2 lightBleed;										NO_AST
		Vec2 specExpRange;										NO_AST
		Vec4 floorValues;										NO_AST
		U32 dxt5nm_index;										NO_AST
	};
} MaterialConstantMapping;

STATIC_ASSERT(sizeof(MaterialConstantMapping) == sizeof(F32)*MAX_MATERIAL_CONSTANT_MAPPING_VALUES+5*4); // values[] must be the right size

// Fake structure which holds the above values but is used for binning (don't care about enum additions)
AUTO_STRUCT;
typedef struct MaterialConstantMappingFake
{
	U32 data_type;
	int constant_index; // Or texture index
	int constant_subindex;
	U32 last_updated_timestamp;									NO_AST
	U32 last_updated_action_idx;								NO_AST
	F32 values[MAX_MATERIAL_CONSTANT_MAPPING_VALUES];
} MaterialConstantMappingFake;
STATIC_ASSERT(sizeof(MaterialConstantMapping) == sizeof(MaterialConstantMappingFake));


STATIC_ASSERT(sizeof(MaterialScrollValues) <= MAX_MATERIAL_CONSTANT_MAPPING_VALUES * sizeof(F32));
STATIC_ASSERT(sizeof(MaterialGradientValues) <= MAX_MATERIAL_CONSTANT_MAPPING_VALUES * sizeof(F32));
STATIC_ASSERT(sizeof(MaterialOscillatorValues) <= MAX_MATERIAL_CONSTANT_MAPPING_VALUES * sizeof(F32));
STATIC_ASSERT(sizeof(MaterialUVRotationValues) <= MAX_MATERIAL_CONSTANT_MAPPING_VALUES * sizeof(F32));

#define MATERIAL_DICT "Material"

extern ParseTable parse_MaterialConstantMappingFake[];
#define TYPE_parse_MaterialConstantMappingFake MaterialConstantMappingFake
extern ParseTable parse_MaterialData[];
#define TYPE_parse_MaterialData MaterialData
extern ParseTable parse_MaterialFallback[];
#define TYPE_parse_MaterialFallback MaterialFallback
extern ParseTable parse_MaterialGraphicPropertiesLoadTime[];
#define TYPE_parse_MaterialGraphicPropertiesLoadTime MaterialGraphicPropertiesLoadTime
extern ParseTable parse_MaterialLoadInfo[];
#define TYPE_parse_MaterialLoadInfo MaterialLoadInfo
extern ParseTable parse_MaterialNamedConstant[];
#define TYPE_parse_MaterialNamedConstant MaterialNamedConstant
extern ParseTable parse_MaterialNamedTexture[];
#define TYPE_parse_MaterialNamedTexture MaterialNamedTexture
extern ParseTable parse_MaterialWorldPropertiesLoadTime[];
#define TYPE_parse_MaterialWorldPropertiesLoadTime MaterialWorldPropertiesLoadTime
extern ParseTable parse_ShaderFixedInput[];
#define TYPE_parse_ShaderFixedInput ShaderFixedInput
extern ParseTable parse_ShaderGraph[];
#define TYPE_parse_ShaderGraph ShaderGraph
extern ParseTable parse_ShaderGuide[];
#define TYPE_parse_ShaderGuide ShaderGuide
extern ParseTable parse_ShaderInputEdge[];
#define TYPE_parse_ShaderInputEdge ShaderInputEdge
extern ParseTable parse_ShaderInputMapping[];
#define TYPE_parse_ShaderInputMapping ShaderInputMapping
extern ParseTable parse_ShaderOperationDef[];
#define TYPE_parse_ShaderOperationDef ShaderOperationDef
extern ParseTable parse_ShaderOperationSpecificValue[];
#define TYPE_parse_ShaderOperationSpecificValue ShaderOperationSpecificValue
extern ParseTable parse_ShaderOperationValues[];
#define TYPE_parse_ShaderOperationValues ShaderOperationValues
extern ParseTable parse_ShaderOperation[];
#define TYPE_parse_ShaderOperation ShaderOperation
extern ParseTable parse_ShaderTemplate[];
#define TYPE_parse_ShaderTemplate ShaderTemplate
extern StaticDefineInt ShaderGraphFeaturesEnum[];
extern StaticDefineInt ShaderGraphFlagsEnum[];
extern StaticDefineInt ShaderOperationTypeEnum[];

extern MaterialLoadInfo material_load_info;
extern DictionaryHandle g_hOpDefsDict;


typedef void (*MaterialReloadMaterialCallbackFunc)(Material *material);
typedef void (*MaterialReloadTemplateCallbackFunc)(ShaderTemplate *shader_template);
typedef void (*MaterialReloadCallbackFunc)(const char *relpath, int when);
typedef void (*MaterialSetUsageCallbackFunc)(Material *material, WLUsageFlags usage_flags);
typedef bool (*MaterialValidateMaterialDataCallbackFunc)(MaterialData *material_data, ShaderTemplate **overrides);
void materialAddCallbacks(MaterialReloadCallbackFunc func, MaterialReloadMaterialCallbackFunc funcMaterial, MaterialReloadTemplateCallbackFunc funcTemplate, MaterialSetUsageCallbackFunc funcSetUsage, MaterialValidateMaterialDataCallbackFunc funcValidateMaterialData);




// Things probably only needed for the MaterialEditor/low level stuff:
ShaderTemplate *materialGetTemplateByName(const char *name);
ShaderTemplate *materialGetTemplateByNameWithOverrides(const char *name, ShaderTemplate** overrides);
ShaderOperation *materialFindOpByName(ShaderGraph *shader_graph, const char *op_name);
ShaderOperation *materialFindOpByType(ShaderGraph *shader_graph, ShaderOperationType op_type); // Really only useful for SOT_SINK
ShaderInputEdge *materialFindInputEdgeByName(ShaderOperation *op, const char *input_name);
const ShaderInputEdge *materialFindInputEdgeByNameConst(const ShaderOperation *op, const char *input_name);
ShaderFixedInput *materialFindFixedInputByName(ShaderOperation *op, const char *input_name);
const ShaderFixedInput *materialFindFixedInputByNameConst(const ShaderOperation *op, const char *input_name);
ShaderRuntimeInput *materialFindRuntimeInputByName(ShaderOperation *op, const char *input_name);
const ShaderRuntimeInput *materialFindRuntimeInputByNameConst(const ShaderOperation *op, const char *input_name);
ShaderInput *materialFindShaderInputByName(ShaderOperation *op, const char *input_name);
ShaderOperationValues *materialFindOperationValues(MaterialData *material, const MaterialFallback *fallback, const char *op_name);
const ShaderOperationValues *materialFindOperationValuesConst(const MaterialData *material, const MaterialFallback *fallback, const char *op_name);
ShaderOperationSpecificValue *materialFindOperationSpecificValue2(ShaderOperationValues *op_values, const char *input_name);
const ShaderOperationSpecificValue *materialFindOperationSpecificValue2Const(const ShaderOperationValues *op_values, const char *input_name);
ShaderOperationSpecificValue *materialFindOperationSpecificValue(MaterialData *material, const MaterialFallback *fallback, const char *op_name, const char *input_name);
ShaderOperationSpecificValue *materialAddOperationSpecificValue(MaterialData *material, MaterialFallback *fallback, const char *op_name, const char *input_name);
int materialFindInputIndexByName(const ShaderOperationDef *op_def, const char *input_name);
int materialFindOutputIndexByName(const ShaderOperationDef *op_def, const char *output_name);
F32 materialFindOperationSpecificValueFloat(const ShaderOperationValues *op_values, const char *input_name, F32 default_value);
F32 materialFindOperationSpecificValueFloatIndexed(const ShaderOperationValues *op_values, const char *input_name, int index, F32 default_value);
ShaderInputMapping* materialFindInputMapping(MaterialFallback *fallback, const char *op_name);

bool materialHasNamedConstant(Material *material, const char *name);
bool materialGetNamedConstantValue(Material *material, const char *name, Vec4 value);
typedef void (*ForEachMaterialNameCallback)(void *userData, const char *material_name);
int materialConstantSwizzleCount( MaterialRenderInfo* renderInfo, int index );
void materialForEachMaterialName(ForEachMaterialNameCallback callback, void *userData);

const char *materialGetFilename(Material *material);

void materialUpdateFromData(Material *material_header, MaterialData *material_data);
void materialDestroy(Material *material_header);

void materialDataUpdateFallback(MaterialData *material_data);
void materialUpdateFallback(Material *material);
const MaterialFallback* materialDataHasShaderTemplate(const MaterialData* material_data, const char* shader_template);
bool materialDataHasRequiredFallbacks( const MaterialData* materialData, ShaderTemplate** overrides );

int materialValidate(MaterialData *material, bool bRepair, ShaderTemplate** overrides); // Returns 0 if bad, assumes shader_template pointer is set
bool materialValidateLastErrorWasTemplateError(void); // Hack for MaterialEditor
void materialPruneOperationValues(MaterialData *material); // Prunes any values that don't reference something in the graph
int materialShaderTemplateValidate(ShaderTemplate *shader_template, bool ignore_autosave_error);
bool shaderTemplateIsSupported(const ShaderTemplate *shader_template);
bool shaderTemplateIsSupportedEx(const ShaderTemplate *shader_template, ShaderGraphFeatures disabled_features);

void materialGetOpNames(ShaderGraph *graph, const char ***ops);
void materialGetMaterialNamedTextures(Material *material_header, MaterialNamedTexture ***texture_names);
void materialGetTextureNames(const Material *material, StashTable texture_names, char **texture_swaps);

MaterialWorldPropertiesRunTime* materialGetWorldProperties(Material *material);
MaterialWorldPropertiesRunTime* materialGetWorldPropertiesEx(Material *material, Vec3 world_point);

bool materialSetPhysicalPropertiesByName(Material *material, const char *physical_properties_name_or_filename);

void materialSetUsage(Material *material, WLUsageFlags use_flags);

void materialShowExtraWarnings(bool show);
void materialEnableDiffuseWarp(void);

void materialPrintObjectsWhoUseMe(ShaderTemplate *templ);
void materialGetMapsWhoUseMe(ShaderTemplate *templ, char **buf);

void materialDataReleaseAll(void);
void materialReleaseData(Material *material_header);

bool materialGetVecValue(Material *mat, const char *value_name, Vec4 value_out);
const char *materialGetStringValue(Material *mat, const char *value_name);

bool materialVerifyObjectMaterialDepsForFx(const char *pcFileName, const char *pcModelName);
void materialVerifyObjectMaterialDepsForFxDone(void);

U32 materialGetDesiredQuality();

__forceinline static RdrDrawableMaterialParam shaderDataTypeToRdrDrawableMaterialParam(ShaderDataType data_type)
{
	return (RdrDrawableMaterialParam)( data_type - SDT_DRAWABLE_START );
}

extern bool g_materials_skip_fixup; // Hack for MaterialEditor getting around callbacks
extern bool materialErrorOnMissingFallbacks;

extern MaterialNamedConstant **eaStaticTintColorArray;
extern MaterialNamedConstant staticTintColorData;
// Generates a static EArray of 1 element for tint color 1
__forceinline static MaterialNamedConstant **gfxMaterialStaticTintColorArray(const Vec3 color1) {
	staticTintColorData.value[0] = color1[0]; staticTintColorData.value[1] = color1[1]; staticTintColorData.value[2] = color1[2];
	return eaStaticTintColorArray;
}

#endif
