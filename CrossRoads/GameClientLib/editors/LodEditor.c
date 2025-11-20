/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
GCC_SYSTEM

#include "FolderCache.h"
#include "TimedCallback.h"
#include "qsortG.h"
#include "StringCache.h"
#include "cmdparse.h"

#include "ObjectLibrary.h"
#include "wlAutoLOD.h"
#include "wlEditorIncludes.h"
#include "Materials.h"
#include "WorldEditor.h"
#include "WorldEditorAppearanceAttributes.h"
#include "wlModelInline.h"
#include "wlModelBinning.h"
#include "structInternals.h"

#include "GfxHeadshot.h"
#include "GraphicsLib.h"

#include "EditorManager.h"
#include "EditorManagerUtils.h"

#include "GfxClipper.h"
#include "GfxPrimitive.h"
#include "EditLibGizmos.h"

#ifndef NO_EDITORS

#include "LodEditor_c_ast.h"

#define MAX_EDIT_LODS 4

#define DIST_MIN 10
#define DIST_MAX 20000

#define MORPH_MIN 0
#define MORPH_MAX 500

#define SLIDER_START 70
#define TEXTENTRY_WIDTH 60

#endif

typedef struct LODEditDoc LODEditDoc;

AUTO_STRUCT;
typedef struct LODParams
{
	F32 distance;			AST(NAME(Distance))
	F32 morph_distance;		AST(NAME(MorphDist))

	bool use_modelname;		NO_AST
	bool null_model;		NO_AST
	bool do_remesh;			NO_AST
	F32 tri_percent;		NO_AST
	F32 upscale;			NO_AST
	char modelname[256];	NO_AST
	char geoname[256];		NO_AST
	TextureSwap **texture_swaps; NO_AST
	MaterialSwap **material_swaps; NO_AST
	bool use_fallback_materials; NO_AST

	LODEditDoc *doc;		NO_AST
} LODParams;

#ifndef NO_EDITORS

typedef struct LODGraph
{
	UIWidget widget;
	LODEditDoc *doc;
} LODGraph;

typedef struct LODEditDoc
{
	EMEditorDoc base_doc;
	Model *model;

	struct
	{
		bool automatic;
		bool high_detail_high_lod;
		bool prevent_clustering;
		int num_lods;
		char template_name[1024];
		LODParams lods[MAX_EDIT_LODS];
	} info;

	struct
	{
		LODGraph *lod_graph;
		UILabel *cam_lod_dist;
		UICheckButton *wait_to_update;
		UICheckButton *automatic;
		UICheckButton *high_detail_high_lod;
		UICheckButton *prevent_clustering;
		UIComboBox *template_name;
		UISlider *num_lods;
		UILabel *num_lods_label;
		UILabel *num_lods_label2;
		UITabGroup *current_lods;

		UISlider *lod_distances;

		struct 
		{
			UITab *tab;

			UIRebuildableTree *ui_auto_widget;
			
			UIRadioButton *remesh_method;
			UIRadioButton *decimation_method;
			UIRadioButton *specified_method;
			UIRadioButton *empty_method;

			UILabel *tri_percent_label;
			UISlider *tri_percent;
			UITextEntry *tri_percent_text;

			UILabel *upscale_label;
			UISlider *upscale;
			UITextEntry *upscale_text;

			UIButton *add_model_button;
			UILabel *model_label;

			UILabel *info_label;

			WleAESwapUI swaps;

			UICheckButton *use_fallback_materials;

		} all_lods[MAX_EDIT_LODS];
	} ui_controls;

	ModelLODInfo *new_lod_info;

	bool show_selected_lod;

	U32 changed:1;
	U32 doing_update:1;
	U32 needs_model_info_update:1;
	U32 update_pending:1;
	U32 wait_to_update:1;

	int current_lod_display;
	ModelLoadTracker model_tracker;

	char **lod_template_names;

	ModelOptionsToolbar *toolbar;

	Geo2LoadData * gld;
	FileList gld_filelist;

} LODEditDoc;

#endif
typedef struct ModelLibraryFolder ModelLibraryFolder;
typedef struct ModelLibraryGeoFile ModelLibraryGeoFile;

AUTO_STRUCT;
typedef struct ModelLibraryEntry
{
	const char *name;				AST(POOL_STRING)
	ModelLibraryGeoFile *file;		NO_AST
	int flag;						NO_AST
} ModelLibraryEntry;

AUTO_STRUCT;
typedef struct ModelLibraryGeoFile
{
	const char *name;				AST(POOL_STRING)
	const char *filename;			AST(CURRENTFILE)
	ModelLibraryEntry **entries;
} ModelLibraryGeoFile;

AUTO_STRUCT;
typedef struct ModelLibraryFolder
{
	const char *name;				AST(POOL_STRING)
	ModelLibraryFolder **folders;
	ModelLibraryGeoFile **files;
} ModelLibraryFolder;

#ifndef NO_EDITORS

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


static EMEditor lod_editor;
static EMPicker modellib_picker;
static ModelLibraryFolder modellib_root;
static StashTable modellib_textures;
static int last_obj_lib_time = 0;

static void rebuildLODSliders(LODEditDoc *doc, int i);

static void lodedSetModelInfoText(LODEditDoc *doc)
{
	char buf[1024];
	F32 dist = 0;
	int i;

	doc->needs_model_info_update = false;

	for (i = 0; i < doc->info.num_lods; ++i)
	{
		StashTable materials = stashTableCreateWithStringKeys(16, StashDefault);
		StashTable textures = stashTableCreateWithStringKeys(16, StashDefault);
		ModelLOD *lod_model = modelLoadLOD(doc->model, i);

		if (lod_model)
		{
			sprintf(buf, "LOD %d: % 4d tris, % 5.2f - % 5.2f ft", 
				i, 
				lod_model->tri_count, 
				dist, 
				dist + doc->info.lods[i].distance);

			if (lod_model->loadstate != GEO_LOADED && lod_model->loadstate != GEO_LOADED_NULL_DATA)
				doc->needs_model_info_update = true;
			else
			{
				MaterialTextureAssoc **matTexAssocs = NULL;
				int j, k;

				wleGetModelTexMats(doc->model, i, &matTexAssocs, false);
				for (j = 0; j < eaSize(&matTexAssocs); j++)
				{
					stashAddPointer(materials, matTexAssocs[j]->orig_name, matTexAssocs[j]->replace_name, true);
					for (k = 0; k < eaSize(&matTexAssocs[j]->textureSwaps); k++)
						stashAddPointer(textures, matTexAssocs[j]->textureSwaps[k]->orig_name, matTexAssocs[j]->textureSwaps[k]->replace_name, true);
				}
				eaDestroyStruct(&matTexAssocs, parse_MaterialTextureAssoc);
			}
		}
		else
		{
			sprintf(buf, "LOD %d: empty, % 5.2f - % 5.2f ft", 
				i, 
				dist, 
				dist + doc->info.lods[i].distance);
		}
		ui_LabelSetText(doc->ui_controls.all_lods[i].info_label, buf);

		dist += doc->info.lods[i].distance;
		ui_FloatSliderSetValueAndCallbackEx(doc->ui_controls.lod_distances, i, dist, 0);

		wleAESwapsRebuildUI(&doc->ui_controls.all_lods[i].swaps, materials, textures);
		stashTableDestroy(materials);
		stashTableDestroy(textures);
	}
}

static void lodedUpdateUI(LODEditDoc *doc)
{
	F64 separation = 0;
	char buf[1024];
	int i;

	if (!doc->ui_controls.automatic)
		return;

	if (doc->doing_update)
		return;

	doc->doing_update = 1;

	doc->ui_controls.automatic->state = doc->info.automatic;

	doc->ui_controls.high_detail_high_lod->state = doc->info.high_detail_high_lod;
	ui_SetActive(UI_WIDGET(doc->ui_controls.high_detail_high_lod), !doc->info.automatic);

	doc->ui_controls.prevent_clustering->state = doc->info.prevent_clustering;
	ui_SetActive(UI_WIDGET(doc->ui_controls.prevent_clustering), !doc->info.automatic);

	ui_ComboBoxSetSelected(doc->ui_controls.template_name, 0);
	for (i = 0; i < eaSize(&doc->lod_template_names); ++i)
	{
		if (stricmp(doc->lod_template_names[i], doc->info.template_name)==0)
			ui_ComboBoxSetSelected(doc->ui_controls.template_name, i);
	}
	ui_SetActive(UI_WIDGET(doc->ui_controls.template_name), !doc->info.automatic);

	ui_IntSliderSetValueAndCallback(doc->ui_controls.num_lods, doc->info.num_lods);
	ui_SetActive(UI_WIDGET(doc->ui_controls.num_lods), !doc->info.automatic);
	ui_SetActive(UI_WIDGET(doc->ui_controls.num_lods_label), !doc->info.automatic);

	sprintf(buf, "%d", doc->info.num_lods);
	ui_LabelSetText(doc->ui_controls.num_lods_label2, buf);
	ui_SetActive(UI_WIDGET(doc->ui_controls.num_lods_label2), !doc->info.automatic);

	separation = 10;
	ui_SliderSetCount(doc->ui_controls.lod_distances, doc->info.num_lods, separation);
	ui_SetActive(UI_WIDGET(doc->ui_controls.lod_distances), !doc->info.automatic && !doc->info.template_name[0]);

	ui_TabGroupRemoveAllTabs(doc->ui_controls.current_lods);

	lodedSetModelInfoText(doc);

	for (i = 0; i < doc->info.num_lods; ++i)
	{
		// method
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].add_model_button);
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].model_label);
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].tri_percent_label);
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].tri_percent);
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].tri_percent_text);
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].upscale_label);
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].upscale);
		ui_TabRemoveChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].upscale_text);

		ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].specified_method), !doc->info.automatic);
		ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].remesh_method), !doc->info.automatic);
		ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].decimation_method), !doc->info.automatic);
		ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].empty_method), !doc->info.automatic);

		if (doc->info.lods[i].null_model)
		{
			ui_RadioButtonActivate(doc->ui_controls.all_lods[i].empty_method);
		}
		else
		{
			if (doc->info.lods[i].use_modelname)
			{
				ui_RadioButtonActivate(doc->ui_controls.all_lods[i].specified_method);
				sprintf(buf, "Model: %s", doc->info.lods[i].modelname);
				ui_LabelSetText(doc->ui_controls.all_lods[i].model_label, buf);
				ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].add_model_button), !doc->info.automatic);
				ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].model_label), !doc->info.automatic);
				ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].add_model_button);
				ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].model_label);
			}
			else
			{
				if (doc->info.lods[i].do_remesh)
				{
					ui_RadioButtonActivate(doc->ui_controls.all_lods[i].remesh_method);
				}
				else
				{
					int num_tris = round(doc->info.lods[i].tri_percent * doc->model->header->tri_count * 0.01f);

					ui_RadioButtonActivate(doc->ui_controls.all_lods[i].decimation_method);

					ui_IntSliderSetValueAndCallback(doc->ui_controls.all_lods[i].tri_percent, num_tris);
					ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].tri_percent), !doc->info.automatic);
					ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].tri_percent_label), !doc->info.automatic);

					sprintf(buf, "%d", num_tris);
					ui_TextEntrySetTextAndCallback(doc->ui_controls.all_lods[i].tri_percent_text, buf);
					ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].tri_percent_text), !doc->info.automatic);

					ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].tri_percent_label);
					ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].tri_percent);
					ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].tri_percent_text);

					ui_FloatSliderSetValueAndCallback(doc->ui_controls.all_lods[i].upscale, doc->info.lods[i].upscale);
					ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].upscale), !doc->info.automatic);
					ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].upscale_label), !doc->info.automatic);

					sprintf(buf, "%.3f", doc->info.lods[i].upscale);
					ui_TextEntrySetTextAndCallback(doc->ui_controls.all_lods[i].upscale_text, buf);
					ui_SetActive(UI_WIDGET(doc->ui_controls.all_lods[i].upscale_text), !doc->info.automatic);

					ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].upscale_label);
					ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].upscale);
					ui_TabAddChild(doc->ui_controls.all_lods[i].tab, doc->ui_controls.all_lods[i].upscale_text);
				}
			}
		}

		ui_TabGroupAddTab(doc->ui_controls.current_lods, doc->ui_controls.all_lods[i].tab);
	}

	for (i = doc->info.num_lods; i < MAX_EDIT_LODS; ++i)
	{
		ui_LabelSetText(doc->ui_controls.all_lods[i].info_label, "");
	}

	ui_TabGroupSetActiveIndex(doc->ui_controls.current_lods, doc->current_lod_display);
	ui_SetActive(UI_WIDGET(doc->ui_controls.current_lods), !doc->info.automatic);

	for (i = 0; i < doc->info.num_lods; ++i)
		rebuildLODSliders(doc, i);

	doc->doing_update = 0;
	doc->changed = 0;
}

static void lodedFillLODInfo(LODEditDoc *doc)
{
	ModelLODInfo *lod_info = doc->model->lod_info;
	const char *template_name = (IS_HANDLE_ACTIVE(lod_info->lod_template))?(REF_STRING_FROM_HANDLE(lod_info->lod_template)):NULL;
	ModelLODTemplate *lod_template = GET_REF(lod_info->lod_template);
	int i;

	doc->info.automatic = lod_info->is_automatic || lod_info->force_auto;
	doc->info.high_detail_high_lod = lod_info->high_detail_high_lod;
	doc->info.prevent_clustering = (lod_info->is_automatic || lod_info->force_auto) ? lod_template->prevent_clustering : lod_info->prevent_clustering;
	doc->info.num_lods = MIN(eaSize(&lod_info->lods), MAX_EDIT_LODS);
	strcpy(doc->info.template_name, template_name?template_name:"");

	for (i = 0; i < doc->info.num_lods; ++i)
	{
		AutoLOD *lod = lod_info->lods[i];
		F32 lod_near, lod_far;
		lodinfoGetDistances(doc->model, NULL, 1, NULL, i, &lod_near, &lod_far);
		doc->info.lods[i].distance = lod_far - lod_near;
		assert(doc->info.lods[i].distance > 0);
		doc->info.lods[i].morph_distance = lod->lod_farmorph;
		if (lod->null_model)
		{
			doc->info.lods[i].use_modelname = 0;
			doc->info.lods[i].tri_percent = 100;
			doc->info.lods[i].upscale = 0;
			doc->info.lods[i].null_model = 1;
			doc->info.lods[i].do_remesh = 0;
		}
		else if (lod->modelname_specified)
		{
			doc->info.lods[i].use_modelname = 1;
			strcpy(doc->info.lods[i].modelname, lod->lod_modelname);
			strcpy(doc->info.lods[i].geoname, lod->lod_filename);
			doc->info.lods[i].tri_percent = 100;
			doc->info.lods[i].upscale = 0;
			doc->info.lods[i].null_model = 0;
			doc->info.lods[i].do_remesh = 0;
		}
		else if (lod->do_remesh)
		{
			doc->info.lods[i].use_modelname = 0;
			doc->info.lods[i].tri_percent = 100;
			doc->info.lods[i].upscale = 0;
			doc->info.lods[i].null_model = 0;
			doc->info.lods[i].do_remesh = 1;
		}
		// last two options are decimation
		else if (lod->flags & LOD_ERROR_TRICOUNT || lod->max_error == 0)
		{
			doc->info.lods[i].use_modelname = 0;
			doc->info.lods[i].tri_percent = 100 - lod->max_error;
			doc->info.lods[i].upscale = lod->upscale_amount;
			doc->info.lods[i].null_model = 0;
			doc->info.lods[i].do_remesh = 0;
		}
		else
		{
			doc->info.lods[i].use_modelname = 0;
			doc->info.lods[i].tri_percent = 100;
			doc->info.lods[i].upscale = 0;
			doc->info.lods[i].null_model = 0;
			doc->info.lods[i].do_remesh = 0;
		}
		eaDestroyStruct(&doc->info.lods[i].texture_swaps, parse_TextureSwap);
		eaDestroyStruct(&doc->info.lods[i].material_swaps, parse_MaterialSwap);
		eaCopyStructs(&lod->texture_swaps, &doc->info.lods[i].texture_swaps, parse_TextureSwap);
		eaCopyStructs(&lod->material_swaps, &doc->info.lods[i].material_swaps, parse_MaterialSwap);
		doc->info.lods[i].use_fallback_materials = lod->use_fallback_materials;
	}

	if (doc->current_lod_display >= doc->info.num_lods)
		doc->current_lod_display = doc->info.num_lods-1;
}

static void lodedUpdateLODs(LODEditDoc *doc)
{
	float near_dist = 0;
	int i;

	if (doc->doing_update)
		return;

	if(doc->wait_to_update) {
		doc->changed = 1;
		doc->update_pending = 1;
		return;
	}
	doc->update_pending = 0;

	doc->doing_update = 1;

	if (doc->new_lod_info)
		freeModelLODInfoData(doc->new_lod_info);
	else
		doc->new_lod_info = StructCreate(parse_ModelLODInfo);

	doc->new_lod_info->modelname = allocAddString(doc->model->name);
	doc->new_lod_info->force_auto = !!doc->info.automatic;
	doc->new_lod_info->high_detail_high_lod = !!doc->info.high_detail_high_lod;
	doc->new_lod_info->prevent_clustering = !!doc->info.prevent_clustering;

	if (!doc->info.automatic)
	{
		lodinfoSetTemplate(doc->new_lod_info, doc->info.template_name);
		for (i = 0; i < doc->info.num_lods; ++i)
		{
			AutoLOD *lod = allocAutoLOD();
			lod->lod_farmorph = doc->info.lods[i].morph_distance;
			lod->lod_near = near_dist;
			near_dist += doc->info.lods[i].distance;
			lod->lod_far = near_dist;
			if (doc->info.lods[i].null_model)
			{
				lod->flags = LOD_ERROR_NULL_MODEL;
				lod->null_model = 1;
			}
			else if (doc->info.lods[i].use_modelname)
			{
				lod->modelname_specified = 1;
				lod->lod_modelname = allocAddString(doc->info.lods[i].modelname);
				lod->lod_filename = allocAddFilename(doc->info.lods[i].geoname);
			}
			else if (doc->info.lods[i].do_remesh)
			{
				lod->flags = LOD_ERROR_REMESH;
				lod->do_remesh = 1;
			}
			else
			{
				lod->flags = LOD_ERROR_TRICOUNT;
				lod->max_error = 100.f - doc->info.lods[i].tri_percent;
				lod->upscale_amount = CLAMP(doc->info.lods[i].upscale, -1, 1);
			}
			eaCopyStructs(&doc->info.lods[i].texture_swaps, &lod->texture_swaps, parse_TextureSwap);
			eaCopyStructs(&doc->info.lods[i].material_swaps, &lod->material_swaps, parse_MaterialSwap);
			lod->use_fallback_materials = doc->info.lods[i].use_fallback_materials;
			eaPush(&doc->new_lod_info->lods, lod);
		}
	}
	else
	{
		lodinfoSetTemplate(doc->new_lod_info, NULL);
		lodinfoFillInAutoData(doc->new_lod_info, false, doc->model->header->filename, doc->model->header->tri_count, 
							  doc->model->header->radius, doc->model->header->high_detail_high_lod, 
							  modelExistsWrapper, NULL, 
							  doc->model->header->has_verts2, true);
	}

	if (lodsDifferent(doc->new_lod_info, lodinfoFromModel(doc->model)))
		emSetDocUnsaved(&doc->base_doc, false);

	doc->model->lod_info = doc->new_lod_info;
	modelBinningLODInfoOverride(doc->model->header->filename, doc->model->name, doc->new_lod_info);

	// Setup LODs
	modelInitLODs(doc->model, 0);

	geo2UpdateBinsForLoadData(doc->model->header,doc->gld,&doc->gld_filelist);

	//modelHeaderReloaded(doc->model);

	modelsReloaded();

	lodedFillLODInfo(doc);

	doc->changed = 1;
	doc->doing_update = 0;
}

static void lodedLODChanged(void *unused, LODEditDoc *doc)
{
	lodedUpdateLODs(doc);
}

static void lodedSelectedLODChanged(UITabGroup *tabs, LODEditDoc *doc)
{
	doc->current_lod_display = ui_TabGroupGetActiveIndex(tabs);
	MAX1(doc->current_lod_display, 0);
	MIN1(doc->current_lod_display, doc->info.num_lods-1);
}

static void lodedToggleWaitToUpdate(UICheckButton *check, LODEditDoc *doc)
{
	doc->wait_to_update = !!check->state;
	if(doc->update_pending && !doc->wait_to_update) {
		lodedUpdateLODs(doc);
	} else {
		doc->changed = 1;
	}
}

static void lodedToggleAutomaticLODs(UICheckButton *check, LODEditDoc *doc)
{
	doc->info.automatic = !!check->state;
	lodedUpdateLODs(doc);
}

static void lodedToggleHighDetailHighLod(UICheckButton *check, LODEditDoc *doc)
{
	doc->info.high_detail_high_lod = !!check->state;
	lodedUpdateLODs(doc);
}

static void lodedTogglePreventClustering(UICheckButton *check, LODEditDoc *doc)
{
	doc->info.prevent_clustering = !!check->state;
	lodedUpdateLODs(doc);
}

static void lodedSwitchLodTemplate(UIComboBox *combo, LODEditDoc *doc)
{
	char *template_name = ui_ComboBoxGetSelectedObject(combo);
	strcpy(doc->info.template_name, template_name?template_name:"");
	lodedUpdateLODs(doc);
}

static void lodedNumLODsChanged(UISlider *slider, bool bFinished, LODEditDoc *doc)
{
	doc->info.num_lods = ui_IntSliderGetValue(slider);
	lodedUpdateLODs(doc);
}

static void lodedDistancesChanged(UISlider *slider, bool bFinished, LODEditDoc *doc)
{
	int i;
	F32 total_dist = 0;
	for (i = 0; i < doc->info.num_lods; ++i)
	{
		F32 dist = ui_FloatSliderGetValueEx(doc->ui_controls.lod_distances, i);
		doc->info.lods[i].distance = dist - total_dist;
		total_dist += doc->info.lods[i].distance;
	}
	lodedUpdateLODs(doc);
}

static void lodedTriPercentChanged(UISlider *slider, bool bFinished, LODParams *lod)
{
	lod->tri_percent = 100.f * ui_IntSliderGetValue(slider) / lod->doc->model->header->tri_count;
	lodedUpdateLODs(lod->doc);
}

static void lodedUpScaleChanged(UISlider *slider, bool bFinished, LODParams *lod)
{
	lod->upscale = ui_FloatSliderGetValue(slider);
	lod->upscale = CLAMP(lod->upscale, -1, 1);
	lodedUpdateLODs(lod->doc);
}

static void lodedDecimationToggled(UIRadioButton *button, LODParams *lod)
{
	lod->use_modelname = !button->state;
	lod->null_model = false;
	lod->do_remesh = false;
	lodedUpdateLODs(lod->doc);
}

static void lodedRemeshToggled(UIRadioButton *button, LODParams *lod)
{
	lod->use_modelname = false;
	lod->null_model = false;
	// TODO REMESH LOD
	//  remesh button disabled for now
	lod->do_remesh = false; // button->state;
	lodedUpdateLODs(lod->doc);
}

static void lodedHandBuiltToggled(UIRadioButton *button, LODParams *lod)
{
	lod->use_modelname = button->state;
	lod->null_model = false;
	lod->do_remesh = false;
	lodedUpdateLODs(lod->doc);
}

static void lodedEmptyToggled(UIRadioButton *button, LODParams *lod)
{
	lod->use_modelname = !button->state;
	lod->null_model = button->state;
	lod->do_remesh = false;
	lodedUpdateLODs(lod->doc);
}

static void lodedFloatSliderTextChanged(UITextEntry *textentry, UISlider *slider)
{
	ui_FloatSliderSetValueAndCallback(slider, atof(ui_TextEntryGetText(textentry)));
}

static void lodedIntSliderTextChanged(UITextEntry *textentry, UISlider *slider)
{
	ui_IntSliderSetValueAndCallback(slider, atoi(ui_TextEntryGetText(textentry)));
}

static bool lodedApplySwaps(const char * const *texSwaps, const char * const *matSwaps, LODParams *lod)
{
	int i, j;

	for (i = 0; i < eaSize(&texSwaps); i+=2)
	{
		for (j = 0; j < eaSize(&lod->texture_swaps); j++)
		{
			TextureSwap *currSwap = lod->texture_swaps[j];
			if (stricmp(texSwaps[i], currSwap->orig_name) == 0)
				freeTextureSwap(eaRemove(&lod->texture_swaps, j));
		}
		if (texSwaps[i+1] && strlen(texSwaps[i+1]) > 0)
			eaPush(&lod->texture_swaps, createTextureSwap("LODEditor", texSwaps[i], texSwaps[i+1]));
	}

	for (i = 0; i < eaSize(&matSwaps); i+=2)
	{
		for (j = 0; j < eaSize(&lod->material_swaps); j++)
		{
			MaterialSwap *currSwap = lod->material_swaps[j];
			if (stricmp(matSwaps[i], currSwap->orig_name) == 0)
				freeMaterialSwap(eaRemove(&lod->material_swaps, j));
		}
		if (matSwaps[i+1] && strlen(matSwaps[i+1]) > 0)
			eaPush(&lod->material_swaps, createMaterialSwap(matSwaps[i], matSwaps[i+1]));
	}

	lodedUpdateLODs(lod->doc);

	return true;
}

static void lodedDrawGhosts(EMEditorDoc *doc_in)
{
	LODEditDoc *doc = (LODEditDoc *)doc_in;
	SingleModelParams params = {0};
	char buf[256];
	F32 cam_lod_dist = 0;

	motUpdateAndDraw(doc->toolbar);
	cam_lod_dist = motGetCamDist(doc->toolbar) - doc->model->radius;
	sprintf(buf,"%.2f",cam_lod_dist);
	ui_LabelSetText(doc->ui_controls.cam_lod_dist, buf);

	copyMat3(unitmat, params.world_mat);
	setVec3(params.world_mat[3], 0, -doc->model->min[1], 0);

	params.model = doc->model;
	params.model_tracker = &doc->model_tracker;
	params.force_lod = doc->show_selected_lod;
	params.lod_override = doc->current_lod_display;
	params.dist = -1;
	params.wireframe = motGetWireframeSetting(doc->toolbar);
	params.unlit = motGetUnlitSetting(doc->toolbar);
	motGetTintColor0(doc->toolbar, params.color);
	params.eaNamedConstants = motGetNamedParams(doc->toolbar);
	params.alpha = 255;
	gfxQueueSingleModelTinted(&params, -1);
}

static void lodedLostFocus(EMEditorDoc *doc_in)
{
	LODEditDoc *doc = (LODEditDoc *)doc_in;
	
	if(doc->update_pending)
		lodedUpdateLODs(doc);

	motLostFocus(doc->toolbar);
	eaFindAndRemove(&lod_editor.toolbars, motGetToolbar(doc->toolbar));
}

static void lodedGotFocus(EMEditorDoc *doc_in)
{
	LODEditDoc *doc = (LODEditDoc *)doc_in;
	EMToolbar *em_toolar = motGetToolbar(doc->toolbar);
	if (objectLibraryLastUpdated() > last_obj_lib_time)
	{
		wleUIObjectTreeRefresh();
		last_obj_lib_time = objectLibraryLastUpdated();
	}
	motGotFocus(doc->toolbar);
	eaPush(&lod_editor.toolbars, em_toolar);
}

static void lodedDrawDoc(EMEditorDoc *doc_in)
{
	LODEditDoc *doc = (LODEditDoc *)doc_in;

	//check the template for any updates (such as a modification to the text file)
	if (doc->model && doc->model->lod_info) {
		ModelLODInfo *lod_info = doc->model->lod_info;
		ModelLODTemplate *lod_template = GET_REF(lod_info->lod_template);

		if ((lod_info->is_automatic || lod_info->force_auto) &&
			(lod_template && lod_template->prevent_clustering != doc->info.prevent_clustering))
		{
			doc->info.prevent_clustering = lod_template->prevent_clustering;
			doc->changed = 1;
		}
	}

	if (doc->needs_model_info_update)
		lodedSetModelInfoText(doc);
	if (doc->changed)
		lodedUpdateUI(doc);
}

static void rebuildLODSliders(LODEditDoc *doc, int i)
{
	UIAutoWidgetParams params={0};

	params.alignTo = 75;
	ui_RebuildableTreeInit(doc->ui_controls.all_lods[i].ui_auto_widget, &doc->ui_controls.all_lods[i].tab->eaChildren, 10, 10, UIRTOptions_YScroll);

	params.type = AWT_Spinner;
	params.min[0] = DIST_MIN;
	params.max[0] = DIST_MAX;
	params.step[0] = 5;
	params.disabled = doc->info.automatic || doc->info.template_name[0];
	ui_AutoWidgetAdd(doc->ui_controls.all_lods[i].ui_auto_widget->root, parse_LODParams, "Distance", &doc->info.lods[i], true, lodedLODChanged, doc, &params, NULL);

	params.type = AWT_Spinner;
	params.min[0] = MORPH_MIN;
	params.max[0] = MORPH_MAX;
	params.step[0] = 5;
	params.disabled = doc->info.automatic;
	ui_AutoWidgetAdd(doc->ui_controls.all_lods[i].ui_auto_widget->root, parse_LODParams, "MorphDist", &doc->info.lods[i], true, lodedLODChanged, doc, &params, NULL);

	ui_RebuildableTreeDoneBuilding(doc->ui_controls.all_lods[i].ui_auto_widget);
}

EMFile *lodedGetDocFile(EMEditorDoc *doc_in)
{
	EMFile **file_list = NULL;
	EMFile *file = NULL;
	emDocGetFiles(doc_in, &file_list, false);
	assert(eaSize(&file_list)==1);
	file = file_list[0];
	eaDestroy(&file_list);
	return file;
}

#define MIN_LOD_DIST 10.0f
#define MAX_LOD_DIST 8000.0f
#define GET_LOD_GRAPH_POS(x, w) ((((x)-MIN_LOD_DIST)/MAX_LOD_DIST)*(w))
void lodedGraphDraw(LODGraph *pEntry, UI_PARENT_ARGS)
{
	int i;
	Color color;
	F32 last_distance = 0.0f;
	UI_GET_COORDINATES(pEntry);
	UI_DRAW_EARLY(pEntry);

	for (i = 0; i < pEntry->doc->info.num_lods; ++i)
	{
		F32 distance = GET_LOD_GRAPH_POS(pEntry->doc->info.lods[i].distance, w);
		F32 morph_distance = GET_LOD_GRAPH_POS(pEntry->doc->info.lods[i].morph_distance, w);

		switch(i)
		{
		case 1:
			setVec4(color.rgba, 0, 255, 0, 255); 
			break;
		case 2:
			setVec4(color.rgba, 255, 0, 255, 255); 
			break;
		case 3:
			setVec4(color.rgba, 255, 255, 0, 255); 
			break;
		default:
			setVec4(color.rgba, 0, 0, 255, 255); 
		}

		if(morph_distance > 0.0f)
			gfxDrawLine(x+last_distance+distance, y+2, z, x+last_distance+distance+morph_distance, y+h, color);
		if(distance > 0.0f)
			gfxDrawLine(x+last_distance, y+2, z, x+last_distance+distance, y+2, color);

		last_distance += distance;
	}

	UI_DRAW_LATE(pEntry);
}

void lodedGraphFreeInternal(LODGraph *pEntry)
{
	ui_WidgetFreeInternal(UI_WIDGET(pEntry));
}

static bool lodedObjectSelected(EMEditorDoc *doc_in, const char *name, const char *type)
{
	LODEditDoc *doc = (LODEditDoc *)doc_in;
	LODParams *lod;
	char model_name[1024];
	char *geo_file;
	GroupDef *group;

	if (!type || !name || stricmp(type, "model")!=0)
		return false;

	lod = &doc->info.lods[doc->current_lod_display];

	// only continue if hand built LOD specified
	if (!lod->use_modelname)
		return false;

	// split name into model name and geo name
	strcpy(model_name, name);
	geo_file = strchr(model_name, ',');
	if (!geo_file)
		return false;
	*geo_file = 0;
	geo_file += 1;

	group = objectLibraryGetGroupDefByName(model_name, false);
	if (!group)
		return false;

	strcpy(lod->modelname, group->name_str);
	strcpy(lod->geoname, geo_file);

	lodedUpdateLODs(doc);
	return true;
}

static bool lodedObjectPickerCB(EMPicker *picker, EMPickerSelection **selections, EMEditorDoc *doc_in)
{
	if(!doc_in || eaSize(&selections) != 1)
		return false;
	return lodedObjectSelected(doc_in, selections[0]->doc_name, selections[0]->doc_type);
}

static void lodedModelSelectCB(UIButton *button, EMEditorDoc *doc_in)
{
	LODEditDoc *doc = (LODEditDoc *)doc_in;
	bool is_char_lib = !!strstriConst(doc->model->header->filename, "character_library/");
	EMPicker *picker;

	picker = emPickerGetByName(is_char_lib?"Model Library":"Object Picker");
	if(!picker)
		return;

	emPickerShow(picker, "Select", false, lodedObjectPickerCB, doc_in);
}

static void lodedInit(EMEditor *editor)
{
	EMToolbar *Toolbar;

	Toolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE|EM_FILE_TOOLBAR_SAVE_ALL);
	eaPush(&editor->toolbars, Toolbar);
	eaPush(&editor->toolbars, emToolbarCreateWindowToolbar());
	
	editor->camera->lock_pivot = 1;
}

static EMEditorDoc *lodedNewDoc(const char *name, const char *type)
{
	LODEditDoc *doc;
	Model *model;
	ModelLOD *model_lod;
	char buf[1024];
	char model_name[1024];
	char *geo_file;
	EMPanel *panel;
	LODGraph *lod_graph;
	UIButton *button;
	UICheckButton *check;
	UISlider *slider;
	UILabel *label;
	UITabGroup *tabgroup;
	UITab *tab;
	UIRadioButtonGroup *radiogroup;
	UIRadioButton *radio;
	UITextEntry *textentry;
	UIComboBox *combo;
	int i;
	F32 y, last_percent, maxY = 0;
	F64 separation;
	EMFile *doc_file;

	// check type
	if (!type || stricmp(type, "model")!=0)
		return NULL;

	// split name into model name and geo name
	strcpy(model_name, name);
	geo_file = strchr(model_name, ',');
	if (!geo_file)
		return NULL;
	*geo_file = 0;
	geo_file += 1;

	// find model
	model = modelFindEx(geo_file, model_name, false, WL_FOR_UTIL); // for util so it doesn't get unloaded when the map is unloaded
	if (!model)
		return NULL;
	model_lod = modelLODWaitForLoad(model, 0);
	if (model_lod) {
		assert(model_lod->data);
	}

	// allocate document structure
	doc = calloc(1, sizeof(*doc));
	strcpy(doc->base_doc.doc_display_name, model_name);
	emDocAssocFile(&doc->base_doc, getLODFileName(model->header->filename, true));
	doc_file = lodedGetDocFile(&doc->base_doc);
	if (!fileExists(doc_file->filename))
		emSetDocUnsaved(&doc->base_doc, false);
	doc->model = model;
	doc->model_tracker.fade_in_lod = doc->model_tracker.fade_out_lod = -1;

	for (i = 0; i < MAX_EDIT_LODS; ++i)
		doc->info.lods[i].doc = doc;

	lodedFillLODInfo(doc);

	doc->toolbar = motCreateToolbar(MET_ALL & ~(MET_ALWAYS_ON_TOP), lod_editor.camera, model->min, model->max, model->radius, "Lod Editor");

	eaPush(&doc->lod_template_names, strdup(""));
	lodGetTemplateNames(&doc->lod_template_names);
	eaQSortG(doc->lod_template_names, strCmp);

	//////////////////////////////////////////////////////////////////////////
	// lod edit window
	panel = emPanelCreate("LOD Editor", "LODs", 0);
	emPanelSetHeight(panel, 540);
	eaPush(&doc->base_doc.em_panels, panel);

	y = 5;

	check = ui_CheckButtonCreate(5, y, "Wait to Update", doc->wait_to_update);
	ui_CheckButtonSetToggledCallback(check, lodedToggleWaitToUpdate, doc);
	check->widget.width = 125;
	check->widget.height = 15;
	emPanelAddChild(panel, check, false);
	doc->ui_controls.wait_to_update = check;

	y += 30;

	label = ui_LabelCreate("Camera LOD Distance:", 5, y);
	emPanelAddChild(panel, label, false);
	label = ui_LabelCreate("", (label->widget.x + label->widget.width) + 5, y);
	ui_WidgetSetFont(UI_WIDGET(label), "Default_Bold");
	doc->ui_controls.cam_lod_dist = label;
	emPanelAddChild(panel, label, false);

	y += 30;

	check = ui_CheckButtonCreate(5, y, "View Selected LOD", false);
	check->statePtr = &doc->show_selected_lod;
	check->widget.width = 145;
	check->widget.height = 15;
	emPanelAddChild(panel, check, false);

	y += 20;

	check = ui_CheckButtonCreate(5, y, "Automatic LODs", doc->info.automatic);
	ui_CheckButtonSetToggledCallback(check, lodedToggleAutomaticLODs, doc);
	check->widget.width = 125;
	check->widget.height = 15;
	emPanelAddChild(panel, check, false);
	doc->ui_controls.automatic = check;

	y += 20;

	check = ui_CheckButtonCreate(5, y, "High Detail High LOD", doc->info.high_detail_high_lod);
	ui_CheckButtonSetToggledCallback(check, lodedToggleHighDetailHighLod, doc);
	check->widget.width = 175;
	check->widget.height = 15;
	emPanelAddChild(panel, check, false);
	doc->ui_controls.high_detail_high_lod = check;

	y += 20;

	check = ui_CheckButtonCreate(5, y, "Prevent Clustering", doc->info.prevent_clustering);
	ui_CheckButtonSetToggledCallback(check, lodedTogglePreventClustering, doc);
	check->widget.width = 175;
	check->widget.height = 15;
	emPanelAddChild(panel, check, false);
	doc->ui_controls.prevent_clustering = check;

	y += 25;

	label = ui_LabelCreate("LOD Template:", 5, y);
	emPanelAddChild(panel, label, false);

	combo = ui_ComboBoxCreate((label->widget.width + label->widget.x + 5), y, 130, NULL, &doc->lod_template_names, NULL);
	ui_ComboBoxSetSelectedCallback(combo, lodedSwitchLodTemplate, doc);
	combo->widget.height = 20;
	doc->ui_controls.template_name = combo;
	ui_ComboBoxSetSelected(combo, 0);
	emPanelAddChild(panel, combo, false);

	y += 25;

	label = ui_LabelCreate("Num LODs:", 5, y);
	emPanelAddChild(panel, label, false);
	doc->ui_controls.num_lods_label = label;

	slider = ui_IntSliderCreate(MAX(SLIDER_START, label->widget.width + label->widget.x + 5), y, 70, 1, MAX_EDIT_LODS, doc->info.num_lods);
	ui_SliderSetChangedCallback(slider, lodedNumLODsChanged, doc);
	emPanelAddChild(panel, slider, false);
	doc->ui_controls.num_lods = slider;

	sprintf(buf, "%d", doc->info.num_lods);
	label = ui_LabelCreate(buf, slider->widget.x + slider->widget.width + 5, y);
	emPanelAddChild(panel, label, false);
	doc->ui_controls.num_lods_label2 = label;

	y += 25;

	lod_graph = calloc(1, sizeof(LODGraph));
	ui_WidgetInitialize(UI_WIDGET(lod_graph), NULL/*tick*/, lodedGraphDraw, lodedGraphFreeInternal, NULL, NULL);
	ui_WidgetSetPosition(UI_WIDGET(lod_graph), 10, y);
	ui_WidgetSetHeight(UI_WIDGET(lod_graph), 20);
	ui_WidgetSetWidthEx(UI_WIDGET(lod_graph), 0.9, UIUnitPercentage);
	lod_graph->doc = doc;
	emPanelAddChild(panel, lod_graph, false);

	y += 25;

	slider = ui_FloatSliderCreate(10, y, 275, 10, 8000, doc->info.lods[0].distance);
	separation = 10;
	ui_SliderSetCount(slider, doc->info.num_lods, separation);
	ui_SliderSetChangedCallback(slider, lodedDistancesChanged, doc);
	ui_WidgetSetWidthEx(UI_WIDGET(slider), 0.9, UIUnitPercentage);
	emPanelAddChild(panel, slider, false);
	doc->ui_controls.lod_distances = slider;

	y += 20;

	for (i = 0; i < MAX_EDIT_LODS; ++i)
	{
		label = ui_LabelCreate("", 5, y);
		emPanelAddChild(panel, label, false);
		doc->ui_controls.all_lods[i].info_label = label;

		y += 20;
	}

	y += 20;

	tabgroup = ui_TabGroupCreate(5, 0, 1.0, 1.0);
	ui_TabGroupSetChangedCallback(tabgroup, lodedSelectedLODChanged, doc);
	tabgroup->widget.topPad = y;
	tabgroup->widget.bottomPad = 5;
	tabgroup->widget.heightUnit = UIUnitPercentage;
	tabgroup->widget.widthUnit = UIUnitPercentage;
	emPanelAddChild(panel, tabgroup, false);
	doc->ui_controls.current_lods = tabgroup;

	for (i = 0; i < MAX_EDIT_LODS; ++i)
	{
		F32 y2;

		sprintf(buf, "LOD %d", i);
		tab = ui_TabCreate(buf);
		if (i < doc->info.num_lods)
			ui_TabGroupAddTab(tabgroup, tab);
		doc->ui_controls.all_lods[i].tab = tab;

		y2 = 10;
		doc->ui_controls.all_lods[i].ui_auto_widget = ui_RebuildableTreeCreate();
		rebuildLODSliders(doc, i);
		y2 += 105;

		// LOD method radio buttons
		radiogroup = ui_RadioButtonGroupCreate();

		radio = ui_RadioButtonCreate(10, y2, "Decim", radiogroup);
		ui_RadioButtonSetToggledCallback(radio, lodedDecimationToggled, &doc->info.lods[i]);
		ui_TabAddChild(tab, radio);
		doc->ui_controls.all_lods[i].decimation_method = radio;

		radio = ui_RadioButtonCreate(65, y2, "Remesh", radiogroup);
		ui_RadioButtonSetToggledCallback(radio, lodedRemeshToggled, &doc->info.lods[i]);
		ui_TabAddChild(tab, radio);
		doc->ui_controls.all_lods[i].remesh_method = radio;

		radio = ui_RadioButtonCreate(130, y2, "Hand Built", radiogroup);
		ui_RadioButtonSetToggledCallback(radio, lodedHandBuiltToggled, &doc->info.lods[i]);
		ui_TabAddChild(tab, radio);
		doc->ui_controls.all_lods[i].specified_method = radio;

		radio = ui_RadioButtonCreate(210, y2, "Empty", radiogroup);
		ui_RadioButtonSetToggledCallback(radio, lodedEmptyToggled, &doc->info.lods[i]);
		ui_TabAddChild(tab, radio);
		doc->ui_controls.all_lods[i].empty_method = radio;

		y2 += 15;

		// triangles slider
		label = ui_LabelCreate("Triangles:", 10, y2);
		if (!doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, label);
		doc->ui_controls.all_lods[i].tri_percent_label = label;

		slider = ui_IntSliderCreate(MAX(SLIDER_START, label->widget.width + label->widget.x + 10), y2, 120, 1, doc->model->header->tri_count, round(doc->info.lods[i].tri_percent * 0.01f * doc->model->header->tri_count));
		ui_SliderSetChangedCallback(slider, lodedTriPercentChanged, &doc->info.lods[i]);
		if (!doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, slider);
		doc->ui_controls.all_lods[i].tri_percent = slider;

		textentry = ui_TextEntryCreate("", slider->widget.x + slider->widget.width + 15, y2);
		ui_TextEntrySetFinishedCallback(textentry, lodedIntSliderTextChanged, slider);
		textentry->widget.height = 15;
		textentry->widget.width = TEXTENTRY_WIDTH;
		if (!doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, textentry);
		doc->ui_controls.all_lods[i].tri_percent_text = textentry;

		// modelname label
		button = ui_ButtonCreateImageOnly("eui_button_plus", 10, y2+3, lodedModelSelectCB, &doc->base_doc);
		ui_WidgetSetDimensions(UI_WIDGET(button), 15, 15);
		ui_ButtonSetImageStretch(button, true);
		if (doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, button);
		doc->ui_controls.all_lods[i].add_model_button = button;

		sprintf(buf, "Model: %s", doc->info.lods[i].modelname);
		label = ui_LabelCreate(buf, 35, y2);
		if (doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, label);
		doc->ui_controls.all_lods[i].model_label = label;

		y2 += 20;

		// upscale slider
		label = ui_LabelCreate("Scale Up:", 10, y2);
		if (!doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, label);
		doc->ui_controls.all_lods[i].upscale_label = label;

		slider = ui_FloatSliderCreate(MAX(SLIDER_START, label->widget.width + label->widget.x + 10), y2, 120, -1, 1, doc->info.lods[i].upscale);
		ui_SliderSetChangedCallback(slider, lodedUpScaleChanged, &doc->info.lods[i]);
		if (!doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, slider);
		doc->ui_controls.all_lods[i].upscale = slider;

		textentry = ui_TextEntryCreate("", slider->widget.x + slider->widget.width + 15, y2);
		ui_TextEntrySetFinishedCallback(textentry, lodedFloatSliderTextChanged, slider);
		textentry->widget.height = 15;
		textentry->widget.width = TEXTENTRY_WIDTH;
		if (!doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			ui_TabAddChild(tab, textentry);
		doc->ui_controls.all_lods[i].upscale_text = textentry;

		y2 += 35;

		y2 = wleAESwapsUICreate(&doc->ui_controls.all_lods[i].swaps, NULL, tab, NULL, lodedApplySwaps, &doc->info.lods[i], MATERIAL_SWAP | TEXTURE_SWAP, false, 10, y2);

		y2 += 20;

		check = ui_CheckButtonCreate(10, y2, "Use Fallback Materials", false);
		check->statePtr = &doc->info.lods[i].use_fallback_materials;
		ui_CheckButtonSetToggledCallback(check, lodedLODChanged, doc);
		check->widget.width = 175;
		check->widget.height = 15;
		ui_TabAddChild(tab, check);
		doc->ui_controls.all_lods[i].use_fallback_materials = check;

		y2 += 45;

		MAX1(maxY, y + y2 + 20);
	}

	emPanelSetHeight(panel, maxY);

	ui_TabGroupSetActiveIndex(tabgroup, 0);

	lodedFillLODInfo(doc);

	last_percent = 100;
	for (i = 0; i < doc->info.num_lods; ++i)
	{
		if (!doc->info.lods[i].use_modelname && !doc->info.lods[i].null_model)
			last_percent = doc->info.lods[i].tri_percent;
	}
	for (i = doc->info.num_lods; i < MAX_EDIT_LODS; ++i)
	{
		doc->info.lods[i].distance = doc->info.lods[i-1].distance;
		doc->info.lods[i].morph_distance = doc->info.lods[i-1].morph_distance;
		doc->info.lods[i].use_modelname = 0;
		doc->info.lods[i].null_model = 0;
		doc->info.lods[i].do_remesh = 0;
		if (last_percent <= 25)
			doc->info.lods[i].tri_percent = last_percent;
		else
			last_percent = doc->info.lods[i].tri_percent = last_percent - 25;
		doc->info.lods[i].modelname[0] = 0;
		doc->info.lods[i].geoname[0] = 0;
	}

	//////////////////////////////////////////////////////////////////////////
	// model info
	panel = emPanelCreate("LOD Editor", "Model Info", 0);
	eaPush(&doc->base_doc.em_panels, panel);

	y = 5;

	label = ui_LabelCreate("Bounds:", 5, y);
	emPanelAddChild(panel, label, false);

	y += 15;

	sprintf(buf, "  Radius: %.2f", distance3(doc->model->min, doc->model->max) * 0.5f);
	label = ui_LabelCreate(buf, 5, y);
	emPanelAddChild(panel, label, false);

	y += 15;

	sprintf(buf, "  Min: %.2f, %.2f, %.2f", doc->model->min[0], doc->model->min[1], doc->model->min[2]);
	label = ui_LabelCreate(buf, 5, y);
	emPanelAddChild(panel, label, false);

	y += 15;

	sprintf(buf, "  Max: %.2f, %.2f, %.2f", doc->model->max[0], doc->model->max[1], doc->model->max[2]);
	label = ui_LabelCreate(buf, 5, y);
	emPanelAddChild(panel, label, false);

	y += 25;

	label = ui_LabelCreate("Materials:", 5, y);
	emPanelAddChild(panel, label, false);

	for (i = 0; i < SAFE_MEMBER(model_lod, data->tex_count); ++i)
	{
		const char *mat_name = SAFE_MEMBER(model_lod->materials[i], material_name);

		y += 15;

		sprintf(buf, "  %s  (%d tris)", mat_name?mat_name:"UNKNOWN", model_lod->data->tex_idx[i].count);
		label = ui_LabelCreate(buf, 5, y);
		emPanelAddChild(panel, label, false);
	}

	y += 25;

	emPanelSetHeight(panel, y);

	lodedUpdateUI(doc);

	globCmdParse("show_model_binning_message 1");

	doc->gld = geo2LoadModelFromSource(model->header,&doc->gld_filelist);

	return &doc->base_doc;
}

static void lodedCloseDoc(EMEditorDoc *doc_in)
{
	LODEditDoc *doc = (LODEditDoc *)doc_in;
	int i;

	geo2Destroy(doc->gld);
	doc->gld = NULL;
	FileListDestroy(&doc->gld_filelist);

	eaFindAndRemove(&lod_editor.toolbars, motGetToolbar(doc->toolbar));
	motFreeToolbar(doc->toolbar);

	for (i = 0; i < ARRAY_SIZE(doc->ui_controls.all_lods); ++i)
		wleAESwapsFreeData(&doc->ui_controls.all_lods[i].swaps);

	eaDestroyEx(&doc->base_doc.ui_windows, ui_WindowFreeInternal);

	if (doc->new_lod_info)
		freeModelLODInfoData(doc->new_lod_info);
	modelBinningLODInfoOverride(doc->model->header->filename, doc->model->name, NULL);
	doc->model->lod_info = lodinfoFromModel(doc->model);
	// Cause it to be reloaded to revert changes
	modelHeaderReloaded(doc->model);

	eaDestroyEx(&doc->lod_template_names, NULL);

	SAFE_FREE(doc);
}

static EMTaskStatus lodedSaveDoc(EMEditorDoc *doc_in)
{
	EMFile *file = lodedGetDocFile(doc_in);
	int i;

	if (!file)
		return EM_TASK_FAILED;

	assert(eaSize(&file->docs));

	if (!emuCheckoutFile(file))
	{
		Errorf("Unable to checkout file \"%s\"", file->filename);
		return EM_TASK_FAILED;
	}

	for (i = 0; i < eaSize(&file->docs); ++i)
	{
		LODEditDoc *doc = (LODEditDoc *)(file->docs[i]->doc);
		if (doc->new_lod_info)
			writeLODInfo(doc->new_lod_info, doc->model->header->filename);
	}

	doc_in->saved = 1;
	return EM_TASK_SUCCEEDED;
}

static bool modelPreview(const char *name, const char *type)
{
	char model_name[1024];
	char *geo_file;
	SingleModelParams params = {0};

	// check type
	if (!type || stricmp(type, "model")!=0)
		return false;

	// split name into model name and geo name
	strcpy(model_name, name);
	geo_file = strchr(model_name, ',');
	if (!geo_file)
		return false;
	*geo_file = 0;
	geo_file += 1;

	// find model
	params.model = modelFindEx(geo_file, model_name, false, WL_FOR_WORLD);
	if (!params.model)
		return false;

	params.alpha = 255;
	copyMat3(unitmat, params.world_mat);
	setVec3(params.world_mat[3], 0, -params.model->min[1], 0);
	gfxQueueSingleModelTinted(&params, -1);

	return true;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// model library browser

//////////////////////////////////////////////////////////////////////////

static bool mlbLibToName(EMPicker *picker, EMPickerSelection *selection)
{
	ModelLibraryEntry *mle = selection->data;
	assert(mle);
	assert(selection->table == parse_ModelLibraryEntry);

	sprintf(selection->doc_name, "%s,%s", mle->name, mle->file->filename);
	strcpy(selection->doc_type, "model");

	return true;
}

static void mlbPreviewFree(EMPicker *picker)
{
	// Destroy preview textures
	FOR_EACH_IN_STASHTABLE( modellib_textures, BasicTexture, it ) {
		gfxHeadshotRelease( it );
	} FOR_EACH_END;
	stashTableClear( modellib_textures );
}

static void mlbPreview(EMPicker *picker, void* raw_entry, ParseTable pti[], BasicTexture** out_tex, Color* out_mod_color)
{
	char modelNameWithFilename[ MAX_PATH + MAX_PATH ];
	ModelLibraryEntry* entry = (ModelLibraryEntry*)raw_entry;
	Model* model;
	BasicTexture* texture;
	
	if (pti != parse_ModelLibraryEntry)
	{
		*out_tex = NULL;
		*out_mod_color = ColorTransparent;
		return;
	}

	sprintf( modelNameWithFilename, "%s,%s", entry->name, entry->file->filename );
	if (stashFindPointer( modellib_textures, modelNameWithFilename, &texture )) {
		assert( texture );

		*out_tex = texture;
		if( gfxHeadshotRaisePriority( texture )) {
			*out_mod_color = ColorWhite;
		} else {
			*out_mod_color = ColorTransparent;
		}
		return;
	}

	model = modelFindEx(entry->file->filename, entry->name, false, WL_FOR_UI);
	if (!model)
	{
		*out_tex = NULL;
		*out_mod_color = ColorTransparent;
		return;
	}

	texture = gfxHeadshotCaptureModel( "mlbModelPreview", 512, 512, model, NULL, ColorTransparent );
	stashAddPointer( modellib_textures, modelNameWithFilename, texture, true );

	*out_tex = texture;
	*out_mod_color = ColorTransparent;
	return;
}

static int cmpFolders(const ModelLibraryFolder **folder1, const ModelLibraryFolder **folder2)
{
	return stricmp((*folder1)->name, (*folder2)->name);
}

static int cmpGeoFiles(const ModelLibraryGeoFile **file1, const ModelLibraryGeoFile **file2)
{
	return stricmp((*file1)->name, (*file2)->name);
}

static int cmpEntries(const ModelLibraryEntry **entry1, const ModelLibraryEntry **entry2)
{
	const char *name1 = (*entry1)->name;
	const char *name2 = (*entry2)->name;
	int ret, under1 = 0, under2 = 0;

	if (name1[0] == '_')
	{
		name1++;
		under1 = 1;
	}

	if (name2[0] == '_')
	{
		name2++;
		under2 = 1;
	}

	ret = stricmp(name1, name2);
	if (!ret)
		return under1 - under2;
	return ret;
}

static void sortLibraryEntries(ModelLibraryFolder *folder)
{
	int i;

	eaQSortG(folder->folders, cmpFolders);
	eaQSortG(folder->files, cmpGeoFiles);

	for (i = 0; i < eaSize(&folder->folders); ++i)
	{
		sortLibraryEntries(folder->folders[i]);
	}

	for (i = 0; i < eaSize(&folder->files); ++i)
	{
		eaQSortG(folder->files[i]->entries, cmpEntries);
	}
}

static void addModelLibraryEntry(ModelLibraryFolder *folder, const char *name, const char *filename, const char *modelname, int flag)
{
	int i;
	char *slash;

	if (slash = strchr(name, '/'))
	{
		int name_len = (int)(slash - name);
		ModelLibraryFolder *child_folder = NULL;
		for (i = 0; i < eaSize(&folder->folders); ++i)
		{
			if (strnicmp(name, folder->folders[i]->name, name_len)==0)
			{
				child_folder = folder->folders[i];
				break;
			}
		}

		if (!child_folder)
		{
			char name_buf[MAX_PATH];
			child_folder = StructCreate(parse_ModelLibraryFolder);
			strncpy(name_buf, name, name_len);
			child_folder->name = allocAddFilename(name_buf);
			eaPush(&folder->folders, child_folder);
		}

		addModelLibraryEntry(child_folder, slash+1, filename, modelname, flag);
	}
	else
	{
		ModelLibraryGeoFile *file = NULL;
		ModelLibraryEntry *entry = NULL;

		for (i = 0; i < eaSize(&folder->files); ++i)
		{
			if (stricmp(name, folder->files[i]->name)==0)
			{
				file = folder->files[i];
				break;
			}
		}

		if (file)
		{
			// See if it already exists
			for (i = 0; i < eaSize(&file->entries); ++i)
			{
				if (stricmp(file->entries[i]->name, modelname)==0)
					entry = file->entries[i];
			}
		}
		else
		{
			file = StructCreate(parse_ModelLibraryGeoFile);
			eaPush(&folder->files, file);
			file->name = allocAddFilename(name);
		}

		file->filename = allocAddFilename(filename);

		if (!entry) {
			entry = StructCreate(parse_ModelLibraryEntry);
			entry->name = allocAddString(modelname);
			entry->file = file;
			eaPush(&file->entries, entry);
		}
		entry->flag = flag;
	}
}

static void pruneModelLibraryEntries(ModelLibraryFolder *folder, int flag)
{
	FOR_EACH_IN_EARRAY(folder->files, ModelLibraryGeoFile, file)
	{
		bool bEmpty=true;
		FOR_EACH_IN_EARRAY(file->entries, ModelLibraryEntry, entry)
		{
			if (entry->flag != flag)
			{
				// Remove it!
				StructDestroy(parse_ModelLibraryEntry, entry);
				eaRemove(&file->entries, ientryIndex);
			} else {
				bEmpty = false;
			}
		}
		FOR_EACH_END;
		if (bEmpty)
		{
			assert(eaSize(&file->entries) == 0);
			eaDestroy(&file->entries);
			StructDestroy(parse_ModelLibraryGeoFile, file);
			eaRemove(&folder->files, ifileIndex);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(folder->folders, ModelLibraryFolder, subfolder)
		pruneModelLibraryEntries(subfolder, flag);
	FOR_EACH_END;
}

static char **objlib_reloads;
static char **charlib_reloads;
static void checkForReloads(TimedCallback *callback, F32 unusedf, void *unused)
{
	static int reload_flag = 1;
	if (eaSize(&objlib_reloads) || eaSize(&charlib_reloads))
	{
		PERFINFO_AUTO_START_FUNC();

		// Ignoring which files changed, just rebuilding the tree from the ModelHeader dictionary
		eaDestroyEx(&objlib_reloads, NULL);
		eaDestroyEx(&charlib_reloads, NULL);

		reload_flag++;
		FOR_EACH_IN_REFDICT(hModelHeaderDict, ModelHeader, model_header)
		{
			addModelLibraryEntry(&modellib_root, model_header->filename, model_header->filename, model_header->modelname, reload_flag);
		}
		FOR_EACH_END;

		// Destroy preview textures
		FOR_EACH_IN_STASHTABLE( modellib_textures, BasicTexture, it ) {
			gfxHeadshotRelease( it );
		} FOR_EACH_END;
		stashTableClear( modellib_textures );
		
		// Prune those not flagged
		pruneModelLibraryEntries(&modellib_root, reload_flag);
		
		PERFINFO_AUTO_STOP();
	}
}

static void refreshObjectLibraryModel(const char *relpath, int when)
{
	if (!relpath || !relpath[0])
		return;
	eaPush(&objlib_reloads, strdup(relpath));
}

static void refreshCharacterLibraryModel(const char *relpath, int when)
{
	if (!relpath || !relpath[0])
		return;
	eaPush(&charlib_reloads, strdup(relpath));
}

static void mlbInit(EMPicker *picker)
{
	EMPickerDisplayType *display_type;

	assert(!modellib_root.name);
	modellib_root.name = StructAllocString("models");

	FOR_EACH_IN_REFDICT(hModelHeaderDict, ModelHeader, model_header)
	{
		addModelLibraryEntry(&modellib_root, model_header->filename, model_header->filename, model_header->modelname, 0);
	}
	FOR_EACH_END;
	sortLibraryEntries(&modellib_root);

	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "object_library/*.ModelHeader", refreshObjectLibraryModel);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "character_library/*.ModelHeader", refreshCharacterLibraryModel);

	TimedCallback_Add(checkForReloads, NULL, 0.5f);

	picker->display_data_root = &modellib_root;
	picker->display_parse_info_root = parse_ModelLibraryFolder;

	display_type = calloc(sizeof(*display_type),1);
	display_type->parse_info = parse_ModelLibraryFolder;
	display_type->display_name_parse_field = "name";
	display_type->selected_func = NULL;
	display_type->is_leaf = 0;
	display_type->color = CreateColorRGB(0, 0, 0);
	display_type->selected_color = CreateColorRGB(255, 255, 255);
	eaPush(&picker->display_types, display_type);

	display_type = calloc(sizeof(*display_type),1);
	display_type->parse_info = parse_ModelLibraryGeoFile;
	display_type->display_name_parse_field = "name";
	display_type->selected_func = NULL;
	display_type->is_leaf = 0;
	display_type->color = CreateColorRGB(0, 0, 80);
	display_type->selected_color = CreateColorRGB(255, 255, 255);
	eaPush(&picker->display_types, display_type);

	display_type = calloc(sizeof(*display_type),1);
	display_type->parse_info = parse_ModelLibraryEntry;
	display_type->display_name_parse_field = "name";
	display_type->selected_func = mlbLibToName;
	display_type->tex_func = mlbPreview;
	display_type->tex_free_func = mlbPreviewFree;
	display_type->is_leaf = 1;
	display_type->color = CreateColorRGB(0, 0, 0);
	display_type->selected_color = CreateColorRGB(255, 255, 255);
	eaPush(&picker->display_types, display_type);

	modellib_textures = stashTableCreateWithStringKeys( 32, StashDeepCopyKeys );
}

//////////////////////////////////////////////////////////////////////////
// registration with asset manager

#endif
AUTO_RUN_LATE;
int lodedRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;
	strcpy(lod_editor.editor_name, "LOD Editor");
	lod_editor.allow_multiple_docs = 1;
	lod_editor.allow_save = 1;
	lod_editor.hide_world = 1;
	lod_editor.allow_outsource = 1;

	lod_editor.init_func = lodedInit;
	lod_editor.load_func = lodedNewDoc;
	lod_editor.close_func = lodedCloseDoc;
	lod_editor.save_func = lodedSaveDoc;
	lod_editor.draw_func = lodedDrawDoc;
	lod_editor.ghost_draw_func = lodedDrawGhosts;
	lod_editor.lost_focus_func = lodedLostFocus;
	lod_editor.got_focus_func = lodedGotFocus;
// 	lod_editor.object_dropped_func = lodedObjectSelected;

	lod_editor.use_em_cam_keybinds = true;

	emRegisterEditor(&lod_editor);

	// Register the picker
	modellib_picker.allow_outsource = 1;
	strcpy(modellib_picker.picker_name, "Model Library");
	modellib_picker.init_func = mlbInit;
	emPickerRegister(&modellib_picker);
	eaPush(&lod_editor.pickers, &modellib_picker);

	emRegisterFileTypeEx("model", "LOD Editor", "LOD Editor", modelPreview);

	return 1;
#else
	return 0;
#endif
}
#ifndef NO_EDITORS

void lodedOncePerFrame(void)
{
	if (objectLibraryLastUpdated() > last_obj_lib_time)
	{
		wleUIObjectTreeRefresh();
		last_obj_lib_time = objectLibraryLastUpdated();
	}

	CHECK_EM_FUNC(lodedNewDoc);
	CHECK_EM_FUNC(lodedCloseDoc);
	CHECK_EM_FUNC(lodedDrawDoc);
	CHECK_EM_FUNC(lodedSaveDoc);
}

#endif

#include "LodEditor_c_ast.c"
