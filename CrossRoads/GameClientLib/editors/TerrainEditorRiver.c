#ifndef NO_EDITORS

#include "TerrainEditorPrivate.h"




AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

extern ParseTable parse_River[];
#define TYPE_parse_River River

void deinit_rivers(TerrainDoc *doc)
{
	/*int i;
	for (i = 0; i < eaSize(&doc->state.river_curves); i++)
	{
		eafDestroy(&doc->state.river_curves[i]->points);
        eafDestroy(&doc->state.river_curves[i]->l_points);
        eafDestroy(&doc->state.river_curves[i]->r_points);
        eafDestroy(&doc->state.river_curves[i]->widths);
        eaiDestroy(&doc->state.river_curves[i]->point_indices);
		SAFE_FREE(doc->state.river_curves[i]);
	}*/
	eaDestroy(&doc->state.river_curves);
}

void terrainRiversRefreshDoc(TerrainDoc *doc)
{
	/*int i, j, l;
	deinit_rivers(doc);
	doc->state.river_max_width = 0;
	for (l = 0; l < eaSize(&doc->state.source->layers); l++)
	{
		TerrainEditorSourceLayer *layer = doc->state.source->layers[l];
		if (layer->mode > LAYER_MODE_GAME)
			for (i = 0; i < eaSize(&layer->rivers); i++)
			{
				if (layer->rivers[i]->curve)
				{
					RiverCurve *curve = (RiverCurve *)calloc(1, sizeof(RiverCurve));
					curve->layer = layer;
                    curve->river = layer->rivers[i];
					curve->curve = layer->rivers[i]->curve;
					curve->points = NULL;
					curve->r_points = NULL;
					curve->l_points = NULL;

					eaPush(&doc->state.river_curves, curve);

					terrainUpdateRiverPoints(curve);
					for (j = 0; j < eafSize(&curve->widths); j++)
						if (curve->widths[j] > doc->state.river_max_width)
							doc->state.river_max_width = curve->widths[j];
				}
			}
	}*/
}

void terrainUpdateRiverPoints(RiverCurve *curve)
{
	/*TerrainDoc *doc = terEdGetDoc();
	TerrainEditorSource *source = doc->state.source;
	int index = 0, i;
	F32 t = 0, length, remainder;
	Vec3 point, up, tangent, left, pt;
	Vec3 offset = { 0, 0, 0 };
    F32 *left_dir = NULL;
    int num_points = eaSize(&curve->river->param_points);
    int *point_indices = (int*)calloc(1, num_points*sizeof(int));
    int current_index = 0;

    eaiDestroy(&curve->point_indices);
    eaiSetSize(&curve->point_indices, num_points);
    for (i = 0; i < num_points; i++)
        curve->point_indices[i] = -1;

	eafDestroy(&curve->points);
	eafDestroy(&curve->l_points);
	eafDestroy(&curve->r_points);
	eafDestroy(&curve->widths);

	splineGetNextPoint(&curve->curve->spline, false, offset, 
		index, eafSize(&curve->curve->spline.spline_points), t, 0.f, 0.f, //int begin_pt, int end_pt, F32 begin_t, F32 end_t, F32 length, 
		&index, &t, &length, &remainder,
		point, up, tangent);
    for (i = 0; i < num_points; i++)
        if (curve->point_indices[i] < 0 &&
            curve->river->param_points[i]->index <= index ||
            (curve->river->param_points[i]->index == index && curve->river->param_points[i]->t <= t))
            curve->point_indices[i] = current_index;
	crossVec3(up, tangent, left);
	if (!terrainSourceGetInterpolatedHeight(source, point[0], point[2], &point[1], NULL))
        point[1] = 0.f;
    assert(FINITE(point[1]));
	eafPush(&curve->points, point[0]);
	eafPush(&curve->points, point[1]+0.5f);
	eafPush(&curve->points, point[2]);
    eafPush(&left_dir, left[0]);
    eafPush(&left_dir, left[2]);
    
	do
	{
        current_index++;
		splineGetNextPoint(&curve->curve->spline, false, offset, 
			index, eafSize(&curve->curve->spline.spline_points), t, 0.f, 4.f, //int begin_pt, int end_pt, F32 begin_t, F32 end_t, F32 length, 
			&index, &t, &length, &remainder, 
			point, up, tangent);
        for (i = 0; i < num_points; i++)
            if (curve->point_indices[i] < 0 &&
                (curve->river->param_points[i]->index <= index ||
                 (curve->river->param_points[i]->index == index && curve->river->param_points[i]->t <= t)))
                curve->point_indices[i] = current_index;
		crossVec3(up, tangent, left);
		if (!terrainSourceGetInterpolatedHeight(source, point[0], point[2], &point[1], NULL))
            point[1] = 0.f;
        assert(FINITE(point[1]));
		if (length > 0)
		{
			eafPush(&curve->points, point[0]);
			eafPush(&curve->points, point[1]+0.5f);
			eafPush(&curve->points, point[2]);
            eafPush(&left_dir, left[0]);
            eafPush(&left_dir, left[2]);
		}
	} while (length > 0 && index < eafSize(&curve->curve->spline.spline_points));

    for (i = 0; i < eafSize(&left_dir); i += 2)
    {
        int j;
        F32 width = 0.f;
        int prev_idx = -1, next_idx = -1;
        for (j = 0; j < num_points; j++)
            if (curve->point_indices[j] <= i/2 &&
                (prev_idx < 0 || curve->point_indices[prev_idx] < curve->point_indices[j]))
                prev_idx = j;
            else if (curve->point_indices[j] >= i/2 &&
                (next_idx < 0 || curve->point_indices[next_idx] > curve->point_indices[j]))
                next_idx = j;
        if (prev_idx > -1)
        {
            if (next_idx > -1 && next_idx > prev_idx)
            {
                F32 interp = ((F32)(i/2)-curve->point_indices[prev_idx]) / ((F32)curve->point_indices[next_idx]-curve->point_indices[prev_idx]);
                width = curve->river->param_points[prev_idx]->width * (1-interp) +
                    curve->river->param_points[next_idx]->width * interp;
            }
            else
            {
                width = curve->river->param_points[prev_idx]->width;
            }
        }
        setVec3(pt, curve->points[i*3/2] + left_dir[i]*width, 0.f, curve->points[i*3/2 + 2] + left_dir[i+1]*width);
        if (!terrainSourceGetInterpolatedHeight(source, pt[0], pt[2], &pt[1], NULL))
            pt[1] = 0.f;
        eafPush(&curve->l_points, pt[0]);
        eafPush(&curve->l_points, pt[1]+0.5f);
        eafPush(&curve->l_points, pt[2]);
        setVec3(pt, curve->points[i*3/2] - left_dir[i]*width, 0.f, curve->points[i*3/2 + 2] - left_dir[i+1]*width);
        if (!terrainSourceGetInterpolatedHeight(source, pt[0], pt[2], &pt[1], NULL))
            pt[1] = 0.f;
        eafPush(&curve->r_points, pt[0]);
        eafPush(&curve->r_points, pt[1]+0.5f);
        eafPush(&curve->r_points, pt[2]);
		eafPush(&curve->widths, width);
    }
	eafDestroy(&left_dir);*/
}

void terrainUIDrawCurve(RiverCurve *curve, bool river_mode)
{
	/*int i;
	TerrainDoc *doc = terEdGetDoc();
	//if (river_mode)
	{
		for (i = 0; i < eafSize(&curve->curve->spline.spline_points); i += 3)
			splineUIDrawControlPoint(&curve->curve->spline, i, -1, -1, false, false);
		splineUIDrawCurve(&curve->curve->spline, curve->selected);
	}

	terrainUpdateRiverPoints(curve);
	for (i = 0; i < eafSize(&curve->points)-3; i += 3)
	{
		Color color = { 0, 0, 255, 255 };
		Color color2 = { 255, 0, 255, 255 };
		if (!river_mode) color.r = color.g = color.b = 128;
		gfxDrawLine3D(&curve->points[i], &curve->points[i+3], color);
	    if (curve->enabled)
        {
			gfxDrawLine3D(&curve->l_points[i], &curve->l_points[i+3], color2);
			gfxDrawLine3D(&curve->r_points[i], &curve->r_points[i+3], color2);
        }
	}
	if (!curve->enabled)
        return;
    
    for (i = 0; i < eaiSize(&curve->point_indices); i++)
    {
        S32 idx = curve->point_indices[i];
        Vec3 min = { -1, -1, -1 };
        Vec3 max = { 1, 1, 1 };
        Mat4 mat;
        Color color = { 255, 0, 255, 255 };
        if (river_mode && curve->selected && doc->state.river_point_selected == i)
            setVec3(color.rgb, 255, 255, 255);
        subVec3(&curve->l_points[idx*3], &curve->points[idx*3], mat[0]);
        setVec3(mat[1], 0, 1, 0);
        crossVec3(mat[0], mat[1], mat[2]);
        normalVec3(mat[2]);
        copyVec3(&curve->points[idx*3], mat[3]);
        gfxDrawBox3D(min, max, mat, color, 5);
    }*/
}

//// User Interface

static void terrainRiverNameDraw(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, RiverCurve ***curves)
{
/*    RiverCurve *curve = (*curves)[index];
    char *name = curve->river->curve->visible_name;
	int oldrgba, oldrgba2;
	gfxfont_GetColorRGBA(&oldrgba, &oldrgba2);
    if (curve->enabled)
        gfxfont_SetColorRGBA(0x000000FF, 0x000000FF);
    else
        gfxfont_SetColorRGBA(0x808080FF, 0x808080FF);
    gfxfont_Printf(x + 10, y + h/2, z, scale, scale, CENTER_Y, "%s", name );
	gfxfont_SetColorRGBA(oldrgba, oldrgba2);*/
}

static void riverNameChangedCB(UITextEntry *entry, void *unused)
{
    /*TerrainDoc *doc = terEdGetDoc();
    S32 selected;
    if (!doc)
        return;
    selected = ui_ListGetSelectedRow(doc->terrain_ui->river_list);
    if (selected >= 0 && selected < eaSize(&doc->state.river_curves))
    {
        const char *name = ui_TextEntryGetText(entry);
        StructFreeString(doc->state.river_curves[selected]->river->curve->visible_name);
        doc->state.river_curves[selected]->river->curve->visible_name = StructAllocString(name);
		doc->state.river_curves[selected]->layer->subdoc.saved = 0;
    }*/
}

static void riverDeleteSelectedCB(UIButton *button, void *unused)
{
    /*TerrainDoc *doc = terEdGetDoc();
    S32 selected;
    if (!doc)
        return;
    selected = ui_ListGetSelectedRow(doc->terrain_ui->river_list);
    if (selected >= 0 && selected < eaSize(&doc->state.river_curves))
    {
        eafDestroy(&doc->state.river_curves[selected]->points);
        eafDestroy(&doc->state.river_curves[selected]->l_points);
        eafDestroy(&doc->state.river_curves[selected]->r_points);
        eafDestroy(&doc->state.river_curves[selected]->widths);
        eaiDestroy(&doc->state.river_curves[selected]->point_indices);
		SAFE_FREE(doc->state.river_curves[selected]);
        eaRemove(&doc->state.river_curves, selected);
        ui_ListSetSelectedRowAndCallback(doc->terrain_ui->river_list, -1);
    }*/
}

static void riverEnabledCB(UIWidget *widget, void *unused)
{
    /*TerrainDoc *doc = terEdGetDoc();
    S32 selected;
    if (!doc)
        return;
    selected = ui_ListGetSelectedRow(doc->terrain_ui->river_list);
    if (selected >= 0 && selected < eaSize(&doc->state.river_curves))
    {
        doc->state.river_curves[selected]->enabled = ui_CheckButtonGetState(doc->terrain_ui->river_enabled_check);
    }*/
}

static void riverSelectedCB(UIList *list, void *unused)
{
    /*TerrainDoc *doc = terEdGetDoc();
    S32 selected = ui_ListGetSelectedRow(list);
    if (!doc)
        return;
    if (selected >= 0 && selected < eaSize(&doc->state.river_curves))
    {
        ui_TextEntrySetText(doc->terrain_ui->river_name_entry, doc->state.river_curves[selected]->river->curve->visible_name);
        ui_CheckButtonSetState(doc->terrain_ui->river_enabled_check, doc->state.river_curves[selected]->enabled);
        ui_SetActive(UI_WIDGET(doc->terrain_ui->river_name_entry), true);
        ui_SetActive(UI_WIDGET(doc->terrain_ui->river_enabled_check), true);
    }
    else
    {
        ui_TextEntrySetText(doc->terrain_ui->river_name_entry, "");
        ui_CheckButtonSetState(doc->terrain_ui->river_enabled_check, false);
        ui_SetActive(UI_WIDGET(doc->terrain_ui->river_name_entry), false);
        ui_SetActive(UI_WIDGET(doc->terrain_ui->river_enabled_check), false);
    }*/
}

EMPanel *terEdCreateRiversPanel(TerrainDoc *doc)
{
	EMPanel *panel;
    UIButton *button;
    UILabel *label;
    UITextEntry *entry;
    UIList *list;
    UICheckButton *check;
    UIListColumn *column;
	int y = 0;
    
	panel = emPanelCreate("Rivers", "Rivers", 0);

	list = ui_ListCreate(parse_River, &doc->state.river_curves, 24);
    ui_WidgetSetPosition(UI_WIDGET(list), 10, 10);
    ui_WidgetSetDimensionsEx(UI_WIDGET(list), 0.95f, 350, UIUnitPercentage, UIUnitFixed);
    ui_ListSetSelectedCallback(list, riverSelectedCB, NULL);
    ui_ListSetActivatedCallback(list, riverEnabledCB, NULL);
    doc->terrain_ui->river_list = list;

    y += 360;

    label = ui_LabelCreate("Name:", 10, y);
    emPanelAddChild(panel, label, true);
    
    entry = ui_TextEntryCreate("", 100, y);
    ui_TextEntrySetFinishedCallback(entry, riverNameChangedCB, NULL);
    ui_SetActive(UI_WIDGET(entry), false);
	doc->terrain_ui->river_name_entry = entry;
    emPanelAddChild(panel, entry, true);

    y += 25;

    button = ui_ButtonCreate("Delete", 80, y, riverDeleteSelectedCB, NULL);
    emPanelAddChild(panel, button, true);

    y += 25;

    check = ui_CheckButtonCreate(10, y, "Enabled", false);
    ui_CheckButtonSetToggledCallback(check, riverEnabledCB, NULL);
    ui_SetActive(UI_WIDGET(check), false);
    emPanelAddChild(panel, check, true);
    doc->terrain_ui->river_enabled_check = check;
    
	// Add the menu column
	column = ui_ListColumnCreateCallback("Name", terrainRiverNameDraw, &doc->state.river_curves);
	ui_ListAppendColumn(list, column);
    
    emPanelAddChild(panel, list, true);

    doc->terrain_ui->panel_rivers = panel;
    
    return panel;
}

#endif