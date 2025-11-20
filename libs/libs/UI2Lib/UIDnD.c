/***************************************************************************



***************************************************************************/



#include "inputMouse.h"
#include "UIInternal.h"

#include "UIDnD.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_DragStartEx(UIWidget *source, const char *type, UserData payload, AtlasTex *icon, U32 iconColor, U32 iconCenter, const char* dragCursorOverride)
{
	UIDeviceState *device = ui_StateForDevice(g_ui_State.device);
	if (device->dnd)
		return;
	device->dnd = calloc(1, sizeof(UIDnDPayload));
	device->dnd->source = source;
	device->dnd->type = type;
	device->dnd->payload = payload;
	device->cursor.draggedIcon = icon;
	device->cursor.draggedIconCenter = !!iconCenter;
	device->cursor.draggedIconColor = iconColor;
	device->cursor.dragCursorName = dragCursorOverride;
}

void ui_DragAccept(UIWidget *dest)
{
	UIDeviceState *device = ui_StateForDevice(g_ui_State.device);
	UIDnDPayload *payload = device->dnd;

	devassertmsg(payload, "Accepting a non-existent drag");
	device->dnd = NULL;
	device->cursor.draggedIcon = NULL;
	device->cursor.draggedIconCenter = 0;
	device->cursor.draggedIconColor = 0;
	device->cursor.dragCursorName = NULL;
	if (dest->dropF)
		dest->dropF(payload->source, dest, payload, dest->dropData);
	if (payload->source && payload->source->acceptF)
		payload->source->acceptF(payload->source, dest, payload, payload->source->acceptData);
	free(payload);
}

void ui_DragCancel(void)
{
	UIDeviceState *device = ui_StateForDevice(g_ui_State.device);
	UIDnDPayload *payload = device->dnd;
	device->dnd = NULL;
	device->cursor.draggedIcon = NULL;
	device->cursor.draggedIconCenter = 0;
	device->cursor.draggedIconColor = 0;
	device->cursor.dragCursorName = NULL;
	if (!payload)
		return;
	if (payload->source && payload->source->acceptF)
		payload->source->acceptF(payload->source, NULL, payload, payload->source->acceptData);
	free(payload);
}

UIDnDPayload *ui_DragIsActive(void)
{
	return ui_StateForDevice(g_ui_State.device)->dnd;
}

bool ui_DragDropped(CBox *box)
{
	if((ui_DragIsActive() && (mouseUnfilteredUp(MS_LEFT) || !mouseIsDown(MS_LEFT)) && mouseCollision(box)))
		return true;
	return false;
}

void ui_WidgetCheckStartDrag(UIWidget *widget, CBox *box)
{
	if (mouseCollision(box) && widget->preDragF)
	{
		widget->preDragF(widget, widget->preDragData);
	}
	
	if (mouseDragHit(MS_LEFT, box) && widget->dragF)
	{
		widget->dragF(widget, widget->dragData);
		inpHandled();
	}
}

void ui_WidgetCheckDrop(UIWidget *widget, CBox *box)
{
	if (widget->dropF && ui_DragDropped(box))
	{
		ui_DragAccept(widget);
		inpHandled();
	}
}
