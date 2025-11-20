/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef NO_EDITORS


#include "wlEditorIncludes.h"

#include "GraphicsLib.h"

#include "EditorManager.h"

#include "EditLibGizmos.h"
#include "GenericMesh.h"
#include "serialize.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););


#define SLIDER_START 70
#define TEXTENTRY_WIDTH 60

extern bool g_TerrainUseOptimalVertPlacement;
extern F32 g_TerrainScaleByArea;

typedef struct MeshDebuggerDoc
{
	EMEditorDoc base_doc;

	GMesh *mesh;
	GMeshReductions *mesh_reductions;
	Vec3 bounds_min, bounds_max;

	Model *model;
	ModelLoadTracker model_tracker;

	int cur_reductions;
	int desired_reductions;
	F32 scale_by_area;

	UISlider *num_reductions_slider;
	UITextEntry *num_reductions_text;
	UISlider *scale_by_area_slider;
	UITextEntry *scale_by_area_text;
	UILabel *error_label;

	ModelOptionsToolbar *toolbar;

} MeshDebuggerDoc;



static EMEditor mesh_debugger;

static void mdFloatSliderTextChanged(UITextEntry *textentry, UISlider *slider)
{
	ui_FloatSliderSetValueAndCallback(slider, atof(ui_TextEntryGetText(textentry)));
}

static void mdNumReductionsChanged(UISlider *slider, bool bFinished, MeshDebuggerDoc *doc)
{
	doc->desired_reductions = ui_IntSliderGetValue(slider);
}

static void mdIntSliderTextChanged(UITextEntry *textentry, UISlider *slider)
{
	ui_IntSliderSetValueAndCallback(slider, atoi(ui_TextEntryGetText(textentry)));
}

static void mdScaleByAreaChanged(UISlider *slider, bool bFinished, MeshDebuggerDoc *doc)
{
	char buf[128];
	doc->scale_by_area = ui_FloatSliderGetValue(slider);
	sprintf(buf, "%.2f", doc->scale_by_area);
	ui_TextEntrySetText(doc->scale_by_area_text, buf);
}

static void mdRebuildReductions(UIButton *button, MeshDebuggerDoc *doc)
{
	Vec3 distances;
	tempModelFree(&doc->model);
	freeGMeshReductions(doc->mesh_reductions);
	doc->mesh_reductions = gmeshCalculateReductions(doc->mesh, distances, doc->scale_by_area, g_TerrainUseOptimalVertPlacement, false, true);
	if (doc->num_reductions_slider)
		ui_IntSliderSetRange(doc->num_reductions_slider, 0, doc->mesh_reductions->num_reductions);
}

static void mdDrawGhosts(EMEditorDoc *doc_in)
{
	MeshDebuggerDoc *doc = (MeshDebuggerDoc *)doc_in;
	SingleModelParams params = {0};

	motUpdateAndDraw(doc->toolbar);

	if (doc->desired_reductions != doc->cur_reductions)
	{
		tempModelFree(&doc->model);
	}
	if (!doc->model)
	{
		GMesh mesh = {0};
		char buf[128];
		F32 error;

		error = gmeshReducePrecalced(&mesh, doc->mesh, doc->mesh_reductions, doc->desired_reductions, COLLAPSE_COUNT_RMETHOD);
		doc->model = tempModelAlloc("Debug Model", NULL, 10, WL_FOR_WORLD);
		modelFromGmesh(doc->model, &mesh);
		copyVec3(doc->bounds_min, doc->model->min);
		copyVec3(doc->bounds_max, doc->model->max);
		gmeshFreeData(&mesh);
		doc->model_tracker.fade_in_lod = doc->model_tracker.fade_out_lod = -1;
		doc->cur_reductions = doc->desired_reductions;

		sprintf(buf, "%d", doc->cur_reductions);
		ui_TextEntrySetText(doc->num_reductions_text, buf);
		ui_IntSliderSetValue(doc->num_reductions_slider, doc->cur_reductions);

		sprintf(buf, "Error: %f", error);
		ui_LabelSetText(doc->error_label, buf);
	}

	if (!doc->model)
		return;

	copyMat3(unitmat, params.world_mat);
	setVec3(params.world_mat[3], 0, -doc->model->min[1], 0);

	params.model = doc->model;
	params.model_tracker = &doc->model_tracker;
	params.dist = -1;
	params.wireframe = motGetWireframeSetting(doc->toolbar);
	params.unlit = motGetUnlitSetting(doc->toolbar);
	motGetTintColor0(doc->toolbar, params.color);
	params.eaNamedConstants = motGetNamedParams(doc->toolbar);
	params.alpha = 255;
	gfxQueueSingleModelTinted(&params, -1);
}

static void mdLostFocus(EMEditorDoc *doc_in)
{
	MeshDebuggerDoc *doc = (MeshDebuggerDoc *)doc_in;
	motLostFocus(doc->toolbar);
	eaFindAndRemove(&mesh_debugger.toolbars, motGetToolbar(doc->toolbar));
}

static void mdGotFocus(EMEditorDoc *doc_in)
{
	MeshDebuggerDoc *doc = (MeshDebuggerDoc *)doc_in;
	EMToolbar *em_toolar = motGetToolbar(doc->toolbar);
	motGotFocus(doc->toolbar);
	eaPush(&mesh_debugger.toolbars, em_toolar);
}

static EMEditorDoc *mdNewDoc(const char *name, const char *type)
{
	MeshDebuggerDoc *doc;
	EMPanel *panel;
	UISlider *slider;
	UILabel *label;
	UITextEntry *textentry;
	UIButton *button;
	int i;
	F32 y;
	SimpleBufHandle buf;
	char path[MAX_PATH];
	GMesh *mesh;
	char buffer[128];

	// find mesh
	sprintf(path, "%s/testmesh.msh", fileTempDir());
	buf = SimpleBufOpenRead(path, NULL);
	if (!buf)
		return NULL;
	mesh = gmeshFromBinData(buf);
	SimpleBufClose(buf);

	if (!mesh)
		return NULL;

	// allocate document structure
	doc = calloc(1, sizeof(*doc));
	strcpy(doc->base_doc.doc_display_name, "Mesh Debugger");
	emSetDocUnsaved(&doc->base_doc, false);

	setVec3same(doc->bounds_min, 8e16);
	setVec3same(doc->bounds_max, -8e16);
	for (i = 0; i < mesh->vert_count; ++i)
		vec3RunningMinMax(mesh->positions[i], doc->bounds_min, doc->bounds_max);

	doc->scale_by_area = g_TerrainScaleByArea;

	doc->mesh = mesh;
	doc->toolbar = motCreateToolbar(MET_ALL & ~(MET_ALWAYS_ON_TOP), mesh_debugger.camera, doc->bounds_min, doc->bounds_max, 0.5f * distance3(doc->bounds_min, doc->bounds_max), "Mesh Debugger");

	mdRebuildReductions(NULL, doc);

	//////////////////////////////////////////////////////////////////////////

	panel = emPanelCreate("Mesh Debugger", "Reductions", 0);
	emPanelSetHeight(panel, 135);
	eaPush(&doc->base_doc.em_panels, panel);

	y = 5;

	label = ui_LabelCreate("Reductions:", 10, y);
	emPanelAddChild(panel, label, false);

	slider = ui_IntSliderCreate(MAX(SLIDER_START, label->widget.width + label->widget.x + 10), y, 120, 0, doc->mesh_reductions->num_reductions, 0);
	ui_SliderSetChangedCallback(slider, mdNumReductionsChanged, doc);
	emPanelAddChild(panel, slider, false);
	doc->num_reductions_slider = slider;

	textentry = ui_TextEntryCreate("", slider->widget.x + slider->widget.width + 15, y);
	ui_TextEntrySetFinishedCallback(textentry, mdIntSliderTextChanged, slider);
	textentry->widget.height = 15;
	textentry->widget.width = TEXTENTRY_WIDTH;
	emPanelAddChild(panel, textentry, false);
	doc->num_reductions_text = textentry;

	y += 20;

	label = ui_LabelCreate("Error: 0", 10, y);
	emPanelAddChild(panel, label, false);
	doc->error_label = label;

	y += 25;

	label = ui_LabelCreate("ScaleByArea:", 10, y);
	emPanelAddChild(panel, label, false);

	slider = ui_FloatSliderCreate(MAX(SLIDER_START, label->widget.width + label->widget.x + 10), y, 120, 0, 1, doc->scale_by_area);
	ui_SliderSetChangedCallback(slider, mdScaleByAreaChanged, doc);
	emPanelAddChild(panel, slider, false);
	doc->scale_by_area_slider = slider;

	textentry = ui_TextEntryCreate("", slider->widget.x + slider->widget.width + 15, y);
	ui_TextEntrySetFinishedCallback(textentry, mdFloatSliderTextChanged, slider);
	textentry->widget.height = 15;
	textentry->widget.width = TEXTENTRY_WIDTH;
	emPanelAddChild(panel, textentry, false);
	sprintf(buffer, "%.2f", doc->scale_by_area);
	ui_TextEntrySetText(textentry, buffer);
	doc->scale_by_area_text = textentry;

	y += 30;

	button = ui_ButtonCreate("Recalc Reductions", 10, y, mdRebuildReductions, doc);
	emPanelAddChild(panel, button, false);


	return &doc->base_doc;
}

static void mdCloseDoc(EMEditorDoc *doc_in)
{
	MeshDebuggerDoc *doc = (MeshDebuggerDoc *)doc_in;

	eaFindAndRemove(&mesh_debugger.toolbars, motGetToolbar(doc->toolbar));
	motFreeToolbar(doc->toolbar);

	eaDestroyEx(&doc->base_doc.ui_windows, ui_WindowFreeInternal);

	tempModelFree(&doc->model);
	gmeshFreeData(doc->mesh);
	free(doc->mesh);
	freeGMeshReductions(doc->mesh_reductions);

	SAFE_FREE(doc);
}


//////////////////////////////////////////////////////////////////////////
// registration with asset manager

#endif

AUTO_RUN_LATE;
int mdRegister(void)
{
#ifndef NO_EDITORS
	if (!areEditorsAllowed())
		return 0;
	//if (!UserIsInGroup("Software")) // can't call UserIsInGroup() in an AUTO_RUN
	//	return 0;
	if (1) // Not generally useful
		return 0;
	strcpy(mesh_debugger.editor_name, "Mesh Debugger");
	mesh_debugger.hide_world = 1;
	mesh_debugger.allow_outsource = 0;

	mesh_debugger.new_func = mdNewDoc;
	mesh_debugger.close_func = mdCloseDoc;
	mesh_debugger.ghost_draw_func = mdDrawGhosts;
	mesh_debugger.lost_focus_func = mdLostFocus;
	mesh_debugger.got_focus_func = mdGotFocus;

	mesh_debugger.default_type = "msh";
	mesh_debugger.use_em_cam_keybinds = true;

	emRegisterEditor(&mesh_debugger);
	emRegisterFileType("msh", "Mesh", "Mesh Debugger");

	return 1;
#else
	return 0;
#endif
}


