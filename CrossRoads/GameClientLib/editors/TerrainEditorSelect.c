#ifndef NO_EDITORS

#include "TerrainEditorPrivate.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void blockOffsetEditCallback(UISpinner *spinner, F32 *data)
{
	char buf[20];
	TerrainDoc *doc = terEdGetDoc();
	assert(doc);
	*data = ui_SpinnerGetValue(spinner);
	sprintf(buf, "%d", (S32)((*data)+0.1f));
	if (data == &doc->state.select_block_dims[0])
		ui_TextEntrySetText(doc->terrain_ui->select_block_x_entry, buf);
	else if (data == &doc->state.select_block_dims[1])
		ui_TextEntrySetText(doc->terrain_ui->select_block_y_entry, buf);
	else if (data == &doc->state.select_block_dims[2])
		ui_TextEntrySetText(doc->terrain_ui->select_block_width_entry, buf);
	else if (data == &doc->state.select_block_dims[3])
		ui_TextEntrySetText(doc->terrain_ui->select_block_height_entry, buf);
}

static void blockOffsetEditTextCallback(UITextEntry *entry, F32 *data)
{
	char buf[20];
	S32 value;
	TerrainDoc *doc = terEdGetDoc();
	assert(doc);
	if (!sscanf(ui_TextEntryGetText(entry), "%d", &value))
	{
		sprintf(buf, "%d", (S32)((*data)+0.1f));
		ui_TextEntrySetText(entry, buf);
		return;
	}
	value = ((S32)(value/GRID_BLOCK_SIZE)*GRID_BLOCK_SIZE);
	if (data == &doc->state.select_block_dims[2] ||
		data == &doc->state.select_block_dims[3])
		value = MAX(GRID_BLOCK_SIZE, value);
	*data = (F32)value;
	sprintf(buf, "%d", value);
	ui_TextEntrySetText(entry, buf);

	if (data == &doc->state.select_block_dims[0])
		ui_SpinnerSetValue(doc->terrain_ui->select_block_x, value);
	else if (data == &doc->state.select_block_dims[1])
		ui_SpinnerSetValue(doc->terrain_ui->select_block_y, value);
	else if (data == &doc->state.select_block_dims[2])
		ui_SpinnerSetValue(doc->terrain_ui->select_block_width, value);
	else if (data == &doc->state.select_block_dims[3])
		ui_SpinnerSetValue(doc->terrain_ui->select_block_height, value);
}

static void terrainCancelBlock(UIButton *button, TerrainDoc *doc)
{
	doc->state.new_block_mode = false;
	doc->state.split_block_mode = false;
	doc->state.remove_block_mode = false;
	ui_WindowHide(doc->terrain_ui->select_block_window);
}

void terEdCreateBlockSelectWindow(TerrainDoc *doc)
{
	UITextEntry *entry;
	UISpinner *spinner;
	UILabel *label;
	UIButton *button;

	doc->terrain_ui->select_block_window = ui_WindowCreate("Select Terrain", 50, 150, 200, 200);

	label = ui_LabelCreate("X (ft):", 10, 5);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, label);

	entry = ui_TextEntryCreate("", 100, 5);
	ui_WidgetSetDimensions(&entry->widget, 80, 20);
	ui_TextEntrySetFinishedCallback(entry, blockOffsetEditTextCallback, &doc->state.select_block_dims[0]);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, entry);
	doc->terrain_ui->select_block_x_entry = entry;

	spinner = ui_SpinnerCreate(180, 5, -(4096*GRID_BLOCK_SIZE), (4096*GRID_BLOCK_SIZE), GRID_BLOCK_SIZE, 0.f, blockOffsetEditCallback, &doc->state.select_block_dims[0]);
	ui_WidgetSetDimensions(UI_WIDGET(spinner), 10, 20);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, spinner);
	doc->terrain_ui->select_block_x = spinner;

	label = ui_LabelCreate("Y (ft):", 10, 30);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, label);

	entry = ui_TextEntryCreate("", 100, 30);
	ui_WidgetSetDimensions(&entry->widget, 80, 20);
	ui_TextEntrySetFinishedCallback(entry, blockOffsetEditTextCallback, &doc->state.select_block_dims[1]);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, entry);
	doc->terrain_ui->select_block_y_entry = entry;

	spinner = ui_SpinnerCreate(180, 30, -(4096*GRID_BLOCK_SIZE), (4096*GRID_BLOCK_SIZE), GRID_BLOCK_SIZE, 0.f, blockOffsetEditCallback, &doc->state.select_block_dims[1]);
	ui_WidgetSetDimensions(UI_WIDGET(spinner), 10, 20);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, spinner);
	doc->terrain_ui->select_block_y = spinner;

	label = ui_LabelCreate("Width (ft):", 10, 55);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, label);

	entry = ui_TextEntryCreate("", 100, 55);
	ui_WidgetSetDimensions(&entry->widget, 80, 20);
	ui_TextEntrySetFinishedCallback(entry, blockOffsetEditTextCallback, &doc->state.select_block_dims[2]);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, entry);
	doc->terrain_ui->select_block_width_entry = entry;

	spinner = ui_SpinnerCreate(180, 55, GRID_BLOCK_SIZE, (4096*GRID_BLOCK_SIZE), GRID_BLOCK_SIZE, 0.f, blockOffsetEditCallback, &doc->state.select_block_dims[2]);
	ui_WidgetSetDimensions(UI_WIDGET(spinner), 10, 20);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, spinner);
	doc->terrain_ui->select_block_width = spinner;

	label = ui_LabelCreate("Height (ft):", 10, 80);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, label);

	entry = ui_TextEntryCreate("", 100, 80);
	ui_WidgetSetDimensions(&entry->widget, 80, 20);
	ui_TextEntrySetFinishedCallback(entry, blockOffsetEditTextCallback, &doc->state.select_block_dims[3]);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, entry);
	doc->terrain_ui->select_block_height_entry = entry;

	doc->terrain_ui->distributed_remesh_check = ui_CheckButtonCreate(10, 105, "Distributed Remesh", true);
	doc->terrain_ui->remesh_higher_precision_check = ui_CheckButtonCreate(10, 125, "High Precision Remesh", true);

	spinner = ui_SpinnerCreate(180, 80, GRID_BLOCK_SIZE, (4096*GRID_BLOCK_SIZE), GRID_BLOCK_SIZE, 0.f, blockOffsetEditCallback, &doc->state.select_block_dims[3]);
	ui_WidgetSetDimensions(UI_WIDGET(spinner), 10, 20);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, spinner);
	doc->terrain_ui->select_block_height = spinner;

	label = ui_LabelCreate("", 10, 145);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, label);
	doc->terrain_ui->select_block_label = label;

    button = ui_ButtonCreate("Create", 10, 165, NULL, NULL);
	ui_WidgetSetDimensions(&button->widget, 80, 25);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, button);
    doc->terrain_ui->select_block_button = button;

	button = ui_ButtonCreate("Cancel", 110, 165, NULL, NULL);
	ui_WidgetSetDimensions(&button->widget, 80, 25);
	ui_ButtonSetCallback(button, terrainCancelBlock, doc);
	ui_WindowAddChild(doc->terrain_ui->select_block_window, button);
}

bool terrainIsOverlappingBlock(IVec2 offset, IVec2 size)
{
	int i;
	for (i = 0; i < zmapGetLayerCount(NULL); i++)
	{
		ZoneMapLayer *layer = zmapGetLayer(NULL, i);
		if (layer &&
			layerIsOverlappingTerrainBlock(layer, offset, size))
		{
			return true;
		}
	}
	return false;
}

#endif