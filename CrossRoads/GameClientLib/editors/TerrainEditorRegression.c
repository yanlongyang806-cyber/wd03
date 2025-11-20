#ifndef NO_EDITORS

#include "TerrainEditorPrivate.h"

#include "WorldEditorOperations.h"
#include "WorldEditorUI.h"
#include "WorldGrid.h"
#include "ControllerScriptingSupport.h"
#include "rgb_hsv.h"
#include "StringCache.h"
#include "MapDescription.h"
#include "wlTerrainQueue.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/////////////////////////////////////////////////////////////////////////
// Map State Management
/////////////////////////////////////////////////////////////////////////

bool _test(char *filename)
{
	FILE *fTest = fileOpen(filename, "r");
	if (fTest)
	{
		fileClose(fTest);
		return true;
	}
	return false;
}

AUTO_COMMAND;
void TerrainEditorRegression_NewMap()
{
	int i;
	Message *message;
	char **file_array = NULL;
	eaPush(&file_array, strdup("maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/AUTO_REGRESSION_TEST_1.zone"));
	eaPush(&file_array, strdup("maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/AUTO_REGRESSION_TEST_1.zone.ms"));
	eaPush(&file_array, strdup("maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/geometry.layer"));
	eaPush(&file_array, strdup("maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/terrain.layer"));
	for (i = 0; i <= 36; i++)
	{
		char temp[256];
		sprintf(temp, "maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/terrain_B%d.alm", i);
		eaPush(&file_array, strdup(temp));
		sprintf(temp, "maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/terrain_B%d.hmp", i);
		eaPush(&file_array, strdup(temp));
		sprintf(temp, "maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/terrain_B%d.tiff", i);
		eaPush(&file_array, strdup(temp));
		sprintf(temp, "maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/terrain_B%d.mat", i);
		eaPush(&file_array, strdup(temp));
		sprintf(temp, "maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/terrain_B%d.soil", i);
		eaPush(&file_array, strdup(temp));
		sprintf(temp, "maps/_Test_Maps/_Terrain_Regression_Test/AUTO_REGRESSION_TEST_1/terrain_B%d.tom", i);
		eaPush(&file_array, strdup(temp));
	}
	for (i = 0; i < eaSize(&file_array); i++)
		fileForceRemove(file_array[i]);

	message = langCreateMessage("Maps__Test_Maps__Terrain_Regression_Test_AUTO_REGRESSION_TEST_1.Displayname", "", "DisplayName", "Terrain Regression Test Map");

	wleOpNewZoneMap("maps/_Test_Maps/_Terrain_Regression_Test", "AUTO_REGRESSION_TEST_1.zone", 32, 32, WLEUI_NEW_OUTDOOR_MAP, ZMTYPE_STATIC, message, NULL, false);

	for (i = 0; i < eaSize(&file_array); i++)
	{
		char error[256];
		if (!_test(file_array[i]))
		{
			sprintf(error, "Failed to create file: %s", file_array[i]);
			ControllerScript_Failed(error);
			eaDestroyEx(&file_array, NULL);
			return;
		}
	}
	worldControllerScriptWaitForMapLoad();
	eaDestroyEx(&file_array, NULL);
}

AUTO_COMMAND;
void TerrainEditorRegression_SetEditable(const char *layer_name)
{
	TerrainDoc *doc = terEdGetDoc();
	if (doc)
	{
		int i;
		for (i = 0; i < eaSize(&doc->state.source->layers); i++)
		{
			if (!stricmp(layerGetName(doc->state.source->layers[i]->layer), layer_name))
			{
				doc->state.source->layers[i]->layer->controller_script_wait_for_edit = true;
				wleUISetLayerMode(doc->state.source->layers[i]->layer, LAYER_MODE_EDITABLE, false);
			}
		}
	}
}

/////////////////////////////////////////////////////////////////////////
// Brush Testing
/////////////////////////////////////////////////////////////////////////

static const char *add_name = "Add";
static const char *flatten_name = "Flatten";
static const char *roughen_name = "Roughen";
static const char *smooth_name = "Smooth";
static const char *color_name = "Color";
static const char *color_blend_name = "Blend";
static const char *color_image_name = "Color Image";
static const char *material_name = "Material";
static const char *soil_set_name = "Soil Set";
static const char *soil_add_name = "Soil Add";
static const char *soil_smooth_name = "Soil Smooth";
static const char *soil_roughen_name = "Soil Roughen";

void TerrainEditorRegression_TestBrushPattern(const char *name)
{
	typedef struct ColorInfo {
		Color color;
		Vec3 hsv;
		Vec3 rgb;
	} ColorInfo;
	const static char * const material_names[] = { "SandboxDefault", "Sand_Wet_N", "Moon_Dirt_01_Ter", "Moon_Rock_01_Ter",
		"Icey_Ice_01_TER", "Jungle_Leaves_Dense_A", "Jungle_Mountain_A", "Icey_Cliff_01_TER", "Crystal_Grass_D_01",
		"Crystal_Grass_L_01", "Crystal_Cliff_01_TER", "Core_Forest_Dirt_01_TER", "Space_Grid_TER", "STO_Coral_Cliff_01" };
	TerrainDoc *doc = terEdGetDoc();
	TerrainCommonBrushParams* common;
	if (doc)
	{
		F32 t;
		ColorInfo color_info = { { 0, 0, 0, 255 }, { 0, 1, 1 }, { 0, 0, 0 } };
		int mat_idx = 0;
		Vec3 world_pos = { 0, 0, 0 };
		int type = 0;
		F32 brush_size = 5;
		int offsetx = 0, offsetz = 0;

		if (name == add_name || name == soil_add_name)
			offsetx = 128;
		else if (name == material_name)
			offsetz = 128;
		else if (name == flatten_name || name == soil_set_name)
			offsetx = offsetz = 128;
		else if (name == roughen_name || name == smooth_name ||
			name == soil_roughen_name || name == soil_smooth_name)
		{
			offsetx = 128;
			offsetz = 256;
		}
		else if (name == color_name)
			offsetx = 128;

		terrainSelectBrush(name);

		if (name == color_image_name)
		{
			doc->state.selected_brush->default_values.string_1 = StructAllocString("cat.tga");
			terrainLoadBrushImage(doc->state.source, &doc->state.selected_brush->default_values);
		}
		if (name == roughen_name || name == soil_roughen_name)
			doc->state.selected_brush->default_values.float_1 = 10;

		common = terEdGetCommonBrushParams(doc);
		common->brush_diameter = brush_size;
		common->brush_hardness = 0.5f;
		common->brush_strength = 1.0f;
		common->invert_filters = false;

		// Torture test 1
		setVec3(world_pos, 0, 0, 0);
		for (world_pos[0] = offsetx; world_pos[0] <= 4096+offsetx; world_pos[0] += 256)
		{
			if (name == flatten_name)
				brush_size = 1;
			else
				brush_size = 5;

			if (name == add_name || name == flatten_name)
			{
				doc->state.selected_brush->default_values.float_1 = sqrtf(brush_size)*20;
			}
			else if (name == soil_set_name)
			{
				doc->state.selected_brush->default_values.float_1 = sqrtf(brush_size);
			}
			else if (name == color_name)
			{
				hsvToRgb(color_info.hsv, color_info.rgb);
				scaleVec3(color_info.rgb, 255.f, color_info.color.rgb);
				copyVec4(color_info.color.rgba, doc->state.selected_brush->default_values.color_1.rgba);
				color_info.hsv[0] += 8;
				if (color_info.hsv[0] > 360)
					color_info.hsv[0] -= 360;
			}
			else if (name == material_name)
			{
				mat_idx++;
				doc->state.selected_brush->default_values.string_1 = StructAllocString(material_names[(mat_idx/8)%14]);
				doc->state.selected_brush->default_values.float_7 = 5;
			}

			terEdCompileBrush(doc);

			for (world_pos[2] = offsetz; world_pos[2] <= 4096+offsetz; world_pos[2] += 256)
			{
				type++;
				common->brush_shape = ((type%2) == 0) ? TBS_Circle : TBS_Square;
				common->falloff_type = ((type/2)%4);

				common->brush_diameter = brush_size;

				terrainQueuePaint(doc->state.task_queue, world_pos, false, false,
					terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, NULL), doc->state.source->visible_lod,
					doc->state.keep_high_res, TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS);

				terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);

				brush_size = MIN(2000, brush_size*1.6f);
			}
		}

		//terEdCompileBrush(doc);

		// Torture test 2
		common->brush_diameter = 1000;
		common->brush_shape = TBS_Circle;
		common->falloff_type = 0;
		for (t = 0; t < 1.5708f; t += 1.5708f*0.01)
		{
			F32 rad = 7500 + cos(t*20)*t*600;
			world_pos[0] = rad*cos(t);
			world_pos[2] = rad*sin(t);

			terrainQueuePaint(doc->state.task_queue, world_pos, false, false,
				terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, common), doc->state.source->visible_lod,
				doc->state.keep_high_res, TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS);
		}
		terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);

		// Last one
		setVec3(world_pos, 2048, 0, 2048);
		terrainQueuePaint(doc->state.task_queue, world_pos, false, false,
			terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, common), doc->state.source->visible_lod,
			doc->state.keep_high_res, TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_CONTROLLERSCRIPT);

		terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);

		terEdRefreshDefaultBrushUI(doc->state.selected_brush);
	}
}

AUTO_COMMAND;
void TerrainEditorRegression_WaitForQueue()
{
	TerrainDoc *doc = terEdGetDoc();
	if (doc)
	{
		terEdWaitForQueuedEvents(doc);
	}
	ControllerScript_Succeeded();
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Add()
{
	TerrainEditorRegression_TestBrushPattern(add_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Flatten()
{
	TerrainEditorRegression_TestBrushPattern(flatten_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Roughen()
{
	TerrainEditorRegression_TestBrushPattern(roughen_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Smooth()
{
	TerrainEditorRegression_TestBrushPattern(smooth_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Color()
{
	TerrainEditorRegression_TestBrushPattern(color_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_ColorBlend()
{
	TerrainEditorRegression_TestBrushPattern(color_blend_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_ColorImage()
{
	TerrainEditorRegression_TestBrushPattern(color_image_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Material()
{
	TerrainEditorRegression_TestBrushPattern(material_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_SoilAdd()
{
	TerrainEditorRegression_TestBrushPattern(soil_add_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_SoilSet()
{
	TerrainEditorRegression_TestBrushPattern(soil_set_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_SoilSmooth()
{
	TerrainEditorRegression_TestBrushPattern(soil_smooth_name);
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_SoilRoughen()
{
	TerrainEditorRegression_TestBrushPattern(soil_roughen_name);
}

static void TerrainEditorRegression_TestBrushLine(const char *brush_name)
{
	TerrainDoc *doc = terEdGetDoc();
	TerrainCommonBrushParams* common;
	if (doc)
	{
		Vec3 world_pos = { 0, 0, 3070 };

		terrainSelectBrush(brush_name);
		terEdCompileBrush(doc);

		common = terEdGetCommonBrushParams(doc);
		common->brush_diameter = 1000;
		common->brush_hardness = 0.5f;
		common->brush_strength = 1.0f;
		common->invert_filters = false;
		common->brush_shape = TBS_Circle;
		common->falloff_type = 0;

		for (world_pos[0] = 10; world_pos[0] < 4096; world_pos[0] *= 1.05)
		{
			terrainQueuePaint(doc->state.task_queue, world_pos, false, false,
				terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, common), doc->state.source->visible_lod,
				doc->state.keep_high_res, TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_CONTROLLERSCRIPT);
		}
		terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);

		terEdRefreshDefaultBrushUI(doc->state.selected_brush);
	}
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Erosion()
{
	TerrainEditorRegression_TestBrushLine("Erode");
}

AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Weather()
{
	TerrainEditorRegression_TestBrushLine("Weather");
}


AUTO_COMMAND;
void TerrainEditorRegression_TestBrush_Slope()
{
	static F32 dx[] = { 1, 0, -1, 0 };
	static F32 dz[] = { 0, 1, 0, -1 };
	TerrainDoc *doc = terEdGetDoc();
	TerrainCommonBrushParams* common;
	if (doc)
	{
		TerrainSlopeBrushParams *params;
		Vec3 world_pos = { 2048, 800, 1024 };
		F32 distance = 100;
		int dir = 0;
		int i;

		terrainSelectBrush("Slope");
		terEdCompileBrush(doc);

		common = terEdGetCommonBrushParams(doc);
		common->brush_diameter = 100;
		common->brush_hardness = 0.5f;
		common->brush_strength = 1.0f;
		common->invert_filters = false;
		common->brush_shape = TBS_Circle;
		common->falloff_type = 0;


		terrainQueuePaint(doc->state.task_queue, world_pos, false, false,
			terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, common), doc->state.source->visible_lod,
			doc->state.keep_high_res, TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_CONTROLLERSCRIPT);

		for (i = 0; i < 200; i++)
		{
			params = calloc(1, sizeof(TerrainSlopeBrushParams));
			copyVec3(world_pos, params->brush_start_pos);
			params->brush_end_pos[0] = world_pos[0] + dx[dir]*distance;
			params->brush_end_pos[1] = params->brush_start_pos[1] * 0.95f;
			params->brush_end_pos[2] = world_pos[2] + dz[dir]*distance;
			dir = (dir+1)%4;
			distance += 20;
			copyVec3(params->brush_end_pos, world_pos);

			terrainQueueSlopeBrush( doc->state.task_queue, params,
				terEdGetBrushParams(doc, common),
				doc->state.source->visible_lod, doc->state.keep_high_res,
				TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_CONTROLLERSCRIPT);
		}

		terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);

		terEdRefreshDefaultBrushUI(doc->state.selected_brush);
	}
}

// Brushes:
// - Grab
// - Smudge
// - Object
// - Erase All Objects
// - Cut
// - Selection
// - Blur Selection

/////////////////////////////////////////////////////////////////////////
// Filter Testing
/////////////////////////////////////////////////////////////////////////

static const char *angle_filter = "Angle";
static const char *alt_filter = "Altitude";
static const char *soil_filter = "Soil Filter";
static const char *perlin_filter = "Perlin";
static const char *selection_filter = "Selection Filter";
static const char *shadow_filter = "Shadow";

void TerrainEditorRegression_TestFilter(const char *name, bool optimize, Color color)
{
	TerrainDoc *doc = terEdGetDoc();
	TerrainDefaultBrush *filter;
	if (doc)
	{
			//doc->state.selected_brush->default_values.float_1 = 10;

		terrainSelectBrush("Color");
		copyVec4(color.rgba, doc->state.selected_brush->default_values.color_1.rgba);

		filter = terEdSetFilterEnabled(name, true);
		if (!filter)
		{
			ControllerScript_Failed("Filter not found.");
			return;
		}

		filter->default_values.strength = 1.f;
		if (name == angle_filter)
		{
			filter->default_values.float_1 = 25.f; // Min angle
			filter->default_values.float_2 = 75.f; // Max angle
			filter->default_values.float_3 = 5.f; // Falloff
		}
		else if (name == alt_filter)
		{
			filter->default_values.float_1 = 50.f; // Min height
			filter->default_values.float_2 = 150.f; // Max height
			filter->default_values.float_3 = 25.f; // Falloff
		}
		else if (name == soil_filter)
		{
			filter->default_values.float_1 = 5.f; // Min depth
			filter->default_values.float_2 = 25.f; // Max depth
			filter->default_values.float_3 = 5.f; // Falloff
		}
		terEdCompileBrush(doc);

		terrainQueueFillWithMultiBrush(doc->state.task_queue, 
			terEdGetMultibrush(doc, NULL), terEdGetBrushParams(doc, NULL), 
			optimize, doc->state.source->visible_lod, doc->state.keep_high_res, 
			TERRAIN_TASK_SHOW_PROGRESS | TERRAIN_TASK_UNDOABLE | TERRAIN_TASK_CONTROLLERSCRIPT);

		terrainQueueFinishTask(doc->state.task_queue, NULL, NULL);

		terEdSetFilterEnabled(name, false);
		terEdCompileBrush(doc);

		terEdRefreshDefaultBrushUI(doc->state.selected_brush);
	}
}

AUTO_COMMAND;
void TerrainEditorRegression_TestFilter_Angle()
{
	Color c = { 0, 0, 255, 255 };
	TerrainEditorRegression_TestFilter(angle_filter, true, c );
}

AUTO_COMMAND;
void TerrainEditorRegression_TestFilter_Altitude()
{
	Color c = { 0, 255, 255, 255 };
	TerrainEditorRegression_TestFilter(alt_filter, true, c );
}

AUTO_COMMAND;
void TerrainEditorRegression_TestFilter_Soil()
{
	Color c = { 0, 255, 0, 255 };
	TerrainEditorRegression_TestFilter(soil_filter, true, c );
}

AUTO_COMMAND;
void TerrainEditorRegression_TestFilter_Perlin()
{
	Color c = { 255, 0, 0, 255 };
	TerrainEditorRegression_TestFilter(perlin_filter, true, c );
}

AUTO_COMMAND;
void TerrainEditorRegression_TestFilter_Selection()
{
	Color c = { 255, 0, 255, 255 };
	TerrainEditorRegression_TestFilter(selection_filter, true, c );
}

AUTO_COMMAND;
void TerrainEditorRegression_TestFilter_Shadow()
{
	Color c = { 0, 0, 0, 255 };
	TerrainEditorRegression_TestFilter(shadow_filter, true, c );
}

// Filters:
// - Object
// - Material
// - Image


#endif // NO_EDITORS
