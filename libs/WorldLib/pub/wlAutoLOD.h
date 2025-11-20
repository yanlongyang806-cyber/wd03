#pragma once
GCC_SYSTEM

#include "referencesystem.h"
#include "earray.h"

// Public interface to AutoLOD system needed by GraphicsLib

typedef struct Model Model;
typedef struct ModelSource ModelSource;
typedef struct TextureSwap TextureSwap;
typedef struct MaterialSwap MaterialSwap;

#define LOD_TEMPLATE_DICTIONARY "LODTemplate"

#define CHARLIB_LOD_SCALE_FILENAME "character_library/lod_scales.txt"

// allow models to draw 20 feet beyond their far distance to allow time to fade out
#define FADE_DIST_BUFFER 20

AUTO_ENUM;
// Having "ERROR" on the front of these enums seems to be a misnomer
typedef enum AutoLODFlags
{
	LOD_ERROR_TRICOUNT			= 1 << 0,	ENAMES(ErrorTriCount)
	LOD_ERROR_NULL_MODEL		= 1 << 1,	ENAMES(NullModel)
	LOD_ERROR_REMESH			= 1 << 2,   ENAMES(Remesh)
	// ADD ONLY AFTER HERE
} AutoLODFlags;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_IGNORE(LodNearFade) AST_IGNORE(LodFarFade);
typedef struct AutoLODTemplate
{
	F32			lod_near;					AST( NAME(LodNear) WIKI("Near distance.") )
	F32			lod_far;					AST( NAME(LodFar) WIKI("Far distance.") )
	bool		no_fade;					AST( NAME(NoFade) WIKI("Disables fading.") )
} AutoLODTemplate;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") WIKI("LODTemplate");
typedef struct ModelLODTemplate
{
	char *template_name;					AST( NAME("") KEY STRUCTPARAM WIKI("A unique name for this template.") )
	F32 default_radius;						AST( NAME(Radius) WIKI("If the radius is greater than zero, this template should be applied to all automatic lods for models less than this radius.") )
	AutoLODTemplate **lods;					AST( NAME(AutoLODTemplate) WIKI("The distance settings for each LOD in this template.") )
	const char *filename;					AST( NAME(Filename) CURRENTFILE )
	bool prevent_clustering;				AST( NAME(PreventClustering) BOOLFLAG WIKI("If set this model will NEVER become part of a simplygon LOD cluster") )
} ModelLODTemplate;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") AST_IGNORE(LodNearFade) AST_IGNORE(LodFarFade);
typedef struct AutoLOD
{
	F32			max_error;					AST( NAME(AllowedError) WIKI("Triangle decimation percentage (0-1).") )
	F32			upscale_amount;				AST( NAME(UpScale) WIKI("Amount to scale up the verts in this LOD (only if error is not equal to 0).") )
	F32			lod_near;					AST( NAME(LodNear) WIKI("Near distance.") )
	F32			lod_far;					AST( NAME(LodFar) WIKI("Far distance.") )
	F32			lod_farmorph;				AST( NAME(LodFarMorph) WIKI("Morph distance.") )
	AutoLODFlags flags;						AST( NAME(LodFlags) WIKI("Flags.") )
	const char	*lod_modelname;				AST( POOL_STRING NAME(ModelName) WIKI("LOD model name.") )
	const char	*lod_filename;				AST( POOL_STRING FILENAME NAME(GeoFileName) WIKI("LOD geo filename.") )
	TextureSwap	**texture_swaps;			AST( NAME(TextureSwap) WIKI("Texture swaps.") )
	MaterialSwap **material_swaps;			AST( NAME(MaterialSwap) WIKI("Material swaps.") )
	U32			no_fade:1;					AST( NAME(NoFade) WIKI("Disables fading.") )
	U32			use_fallback_materials:1;	AST( NAME(UseFallbackMaterials) WIKI("Indicates this LOD should use the fallback versions of its materials.") )

	AST_STOP
	U32			modelname_specified : 1;
	U32			null_model : 1;
	U32			do_remesh : 1;
	AST_START
} AutoLOD;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("End") WIKI("LODInfo");
typedef struct ModelLODInfo
{
	const char *modelname;					AST( NAME("") POOL_STRING STRUCTPARAM WIKI("The model name these LOD settings apply to.") )
	AutoLOD **lods;							AST( NAME(AutoLOD) WIKI("The distance settings for each LOD for this model.") )
	const char *parsed_filename;			AST( NAME(Filename) CURRENTFILE)
	REF_TO(ModelLODTemplate) lod_template;	AST( NAME(LODTemplate) WIKI("The LOD template to use for this model.") )
	U32 force_auto:1;						AST( NAME(ForceAutomatic) WIKI("Use automatic settings.") )
	U32 high_detail_high_lod:1;				AST( NAME(HighDetailHighLod) WIKI("If set the high LOD will only draw when high detail objects are turned on.  The second LOD will be used for collision.") )
	U32 prevent_clustering : 1;				AST( NAME(PreventClustering) WIKI("If set this model will NEVER become part of a simplygon LOD cluster") )

	AST_STOP
	U32 is_automatic : 1;
	U32 is_no_lod : 1;
	U32 removed : 1;
	U32 is_in_dictionary : 1;
	AST_START
} ModelLODInfo;

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct LODInfos
{
	ModelLODInfo **infos;					AST( NAME(LODInfo) )
} LODInfos;

SA_RET_NN_VALID ModelLODInfo *lodinfoFromModelSource(SA_PARAM_NN_VALID ModelSource *model, SA_PARAM_NN_STR const char *geoFilename);
SA_RET_NN_VALID ModelLODInfo *lodinfoFromModel(SA_PARAM_NN_VALID Model *model);

typedef void (*LodReloadCallback)(const char *path);
void lodinfoSetReloadCallback(LodReloadCallback callback);

void lodinfoSetTemplate(SA_PARAM_NN_VALID ModelLODInfo *info, SA_PARAM_OP_STR const char *template_name);

void lodGetTemplateNames(char ***name_array);

F32 loddistFromLODInfo(SA_PARAM_OP_VALID Model *model, SA_PARAM_OP_VALID ModelLODInfo *lod_info, AutoLODTemplate **lod_override_distances, F32 lod_scale, const Vec3 model_scale, bool use_buffer, F32 *near_lod_near_dist, F32 *far_lod_near_dist);
bool lodinfoGetDistances(Model *model, AutoLODTemplate **lod_override_distances, F32 lod_scale, const Vec3 model_scale, int i, F32 *far_lod_near_dist, F32 *far_lod_far_dist);

ModelLODTemplate *lodinfoGetTemplateForRadius(SA_PARAM_NN_VALID const ModelLODInfo *lod_info, F32 radius);

extern ParseTable parse_AutoLOD[];
#define TYPE_parse_AutoLOD AutoLOD
extern ParseTable parse_ModelLODInfo[];
#define TYPE_parse_ModelLODInfo ModelLODInfo
extern ParseTable parse_AutoLODTemplate[];
#define TYPE_parse_AutoLODTemplate AutoLODTemplate



AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Misc););
