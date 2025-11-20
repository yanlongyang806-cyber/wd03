#include "GfxDumpToVrml.h"
#include "wininclude.h"
#include "d3dx9.h"
#include "rgb_hsv.h"
#include "GraphicsLib.h"
#include "GfxTextures.h"
#include "GfxDXT.h"
#include "GfxCamera.h"
#include "wlModelInline.h"
#include "../wlModelLoad.h"
#include "../StaticWorld/WorldCell.h"
#include "MemoryBudget.h"
#include "tga.h"
#include "mathutil.h"
#include "wininclude.h"
#include "file.h"
#include "Quat.h"
#include "partition_enums.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

static FILE *vrml_file;
static StashTable vrml_dumped_objects;

static void vrmlDumpCreateModel(void* userPointer, const GeoMeshTempData* td)
{
	int i;
	const Model *model = (const Model*)userPointer;

	// create the materials
	ModelLOD *model_lod = modelGetLOD(model, 0);
// 	for (int i=0; i<model_lod->data->tex_count; i++)
// 	{
// 		Material *material = model_lod->materials[i];
// 		if (!material->beast_created)
// 		{
// 			ILBMaterialHandle beastMat;
// 			beastApiCall(ILBCreateMaterial(scene, material->material_name, &beastMat));
// 
// 			const char *diffuse_texname=NULL;
// 			if (texFind(material->material_name, false))
// 				diffuse_texname = material->material_name;
// 
// 			const MaterialData *mat_data = materialGetData(material);
// 			for (int j=0; j<eaSize(&mat_data->graphic_props.default_fallback.shader_values); j++)
// 			{
// 				const ShaderOperationValues *op_value = mat_data->graphic_props.default_fallback.shader_values[j];
// 				for (int k=0; k<eaSize(&op_value->values); k++)
// 				{
// 					const ShaderOperationSpecificValue *val = op_value->values[k];
// 					if (eaSize(&val->svalues)==1 && texFind(val->svalues[0], false))
// 					{
// 						if (!diffuse_texname && strstri(op_value->op_name, "diffuse"))
// 							diffuse_texname = val->svalues[0];
// 					}
// 				}
// 			}
// 
// 			if (!diffuse_texname)
// 				diffuse_texname = "white";
// 
// 			ILBTextureHandle diffuseTex = beastCreateTexture(diffuse_texname);
// 
// 			beastApiCall(ILBSetMaterialTexture(beastMat, ILB_CC_DIFFUSE, diffuseTex));
// 
// 			ILBLinearRGBA diffuse(.3f, .3f, .3f, 1.0f);
// 			ILBLinearRGBA spec(1.0f, 1.0f, 1.0f, 1.0f);
// 			beastApiCall(ILBSetMaterialColor(beastMat, ILB_CC_DIFFUSE, &diffuse));
// 			beastApiCall(ILBSetMaterialColor(beastMat, ILB_CC_SPECULAR, &spec));
// 			beastApiCall(ILBSetMaterialScale(beastMat, ILB_CC_REFLECTION, 0));
// 			beastApiCall(ILBSetMaterialScale(beastMat, ILB_CC_SHININESS, 15.0f));
// 			beastApiCall(ILBSetChannelUVLayer(beastMat, ILB_CC_DIFFUSE, "uv2"));
// 			beastApiCall(ILBSetAlphaAsTransparency(beastMat, TRUE));
// 
// 			material->beast_created = 1;
// 		}
// 	}


	fprintf(vrml_file,
"      geometry DEF %s-FACES IndexedFaceSet {\n"
"        ccw TRUE\n"
"        solid TRUE\n"
"        coord DEF %s-COORD Coordinate { point [\n"
		, model->name, model->name);
	for (i=0; i<td->vert_count; i++)
	{
		fprintf(vrml_file, "%.10f %.10f %.10f%s%s",
			-td->verts[i][0], td->verts[i][1], td->verts[i][2],
			(i==td->vert_count-1)?"]":", ", ((i==td->vert_count-1)||(i%2))?"\n":"");
	}
	fprintf(vrml_file, 
"        }\n"
"        normal Normal { vector [\n"
		);
	for (i=0; i<td->vert_count; i++)
	{
		fprintf(vrml_file, "%.10f %.10f %.10f%s%s",
			-td->norms[i][0], td->norms[i][1], td->norms[i][2],
			(i==td->vert_count-1)?"]":", ", ((i==td->vert_count-1)||(i%2))?"\n":"");
	}
	fprintf(vrml_file, 
"        }\n"
"        normalPerVertex TRUE\n"
"        coordIndex [\n"
		);
	for (i=0; i<td->tri_count; i++)
	{
		fprintf(vrml_file, "%d, %d, %d, -1%s%s",
			td->tris[i*3+2], td->tris[i*3+1], td->tris[i*3+0],
			(i==td->tri_count-1)?"]":", ", (i%2 || i==td->tri_count-1)?"\n":"");
	}
	fprintf(vrml_file, 
"        normalIndex [\n"
		);
	for (i=0; i<td->tri_count; i++)
	{
		fprintf(vrml_file, "%d, %d, %d, -1%s%s",
			td->tris[i*3+2], td->tris[i*3+1], td->tris[i*3+0],
			(i==td->tri_count-1)?"]":", ", (i%2 || i==td->tri_count-1)?"\n":"");
	}
	fprintf(vrml_file, 
"        }\n"
		);
}

void vrmlDumpSub(const Model *model, Mat4 world_mat, Vec3 model_scale, Vec3 color, void *uid)
{
	Quat q;
	Vec4 aa;
	bool bNeedGeo;
	char instanceName[64];

	if (!model)
		return;

	if (strstri(model->name, "_vista"))
		return;

	if (!stashFindInt(vrml_dumped_objects, model->name, NULL))
	{
		strcpy(instanceName, model->name);
		// dump geometry
		bNeedGeo = true;
	} else {
		sprintf(instanceName, "%s_%p", model->name, uid);
		// dump reference to earlier faces
		bNeedGeo = false;
	}

	mat3ToQuat(world_mat, q);
	quatToAxisAngle(q, aa, &aa[3]);

	fprintf(vrml_file,
"DEF %s Transform {\n"
"  translation %.10f %.10f %.10f\n"
"  rotation %.10f %.10f %.10f %.10f\n"
"  scale %.10f %.10f %.10f\n"
"  children [\n"
"    Shape {\n"
"      appearance Appearance {\n"
"        material Material {\n"
"          diffuseColor %f %f %f\n"
"          ambientIntensity 1.0\n"
"          specularColor 0 0 0\n"
"          shininess 0.15\n"
"          transparency 0\n"
"        }\n"
"        texture ImageTexture {\n"
"          url \"../maps/white\"\n"
"        }\n"
"      }\n"
			, instanceName,
			-world_mat[3][0], world_mat[3][1], world_mat[3][2],
			-aa[0], aa[1], aa[2], aa[3],
			model_scale[0], model_scale[1], model_scale[2],
			color[0], color[1], color[2]
			// TODO: material texture?
			);
	if (bNeedGeo)
	{
		// TODO
		geoProcessTempData(vrmlDumpCreateModel,
			(void*)model,
			model,
			0,
			unitvec3,
			1,
			1,
			0,
			1,
			NULL);
		verify(stashAddInt(vrml_dumped_objects, model->name, 1, false));
	} else {
		fprintf(vrml_file,
"    geometry USE %s-FACES\n"
			, model->name);
	}
	fprintf(vrml_file,
"    }\n"
"  ]\n"
"}\n\n"
		);
}

static void vrmlDumpDoit(WorldCell *cell)
{
	int i;
	if (!cell)
		return;
	for (i=0; i<8; i++)
	{
		vrmlDumpDoit(cell->children[i]);
	}

	for (i=0; i<eaSize(&cell->drawable.drawable_entries); i++)
	{
		WorldDrawableEntry *entry = cell->drawable.drawable_entries[i];

		if (entry->draw_list && entry->draw_list->drawable_lods)
		{
			const Model *model = entry->draw_list->drawable_lods->subobjects[0]->model->model;
			assert(entry->draw_list->drawable_lods->subobject_count);
			assert(model);

			vrmlDumpSub(model, entry->base_entry.bounds.world_matrix, entry->base_entry.shared_bounds->model_scale, entry->color, entry);
		}
	}
}

bool vrmlDumpStart(const char *filename)
{
	makeDirectoriesForFile(filename);
	vrml_file = fileOpen(filename, "w");
	if (!vrml_file)
	{
		Errorf("Error opening %s for writing", filename);
		return false;
	}
	fprintf(vrml_file,
		"#VRML V2.0 utf8\n"
		"\n"
		"#Produced by /dumpSceneToWrl\n"
		"\n");

	vrml_dumped_objects = stashTableCreateWithStringKeys(16, StashDefault);
	return true;
}

void vrmlDumpFinish(void)
{
	fclose(vrml_file);

	stashTableDestroy(vrml_dumped_objects);
}


AUTO_COMMAND;
void dumpSceneToWrl(const char *filename)
{
	int i;
	WorldRegion **zmap_regions;

	if (vrmlDumpStart(filename))
	{

		// walk through all objects and instantiate them
		zmap_regions = zmapInfoGetWorldRegions(NULL);
		for (i = 0; i < eaSize(&zmap_regions); ++i)
		{
			WorldRegion *region = zmap_regions[i];
			if (!worldRegionIsEditorRegion(region))
			{
				Mat4 gfx_cammat;
				gfxGetActiveCameraMatrix(gfx_cammat);
				worldGridOpenAllCellsForCameraPos(PARTITION_CLIENT, region, gfx_cammat[3], 1, NULL, true, false, true, true, 0, 1.0f);
				vrmlDumpDoit(region->root_world_cell);
			}
		}

		vrmlDumpFinish();
	}
}
