#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_DND_H
#define UI_DND_H

#include "UICore.h"

//////////////////////////////////////////////////////////////////////////
// Drag-and-Drop support for the UI. Drags have several states:
//
// 1) A widget initiates a drag by calling ui_DragStart with its
//    payload data. Presumably this happens because the widget checked
//    mouseDrag in its tick function, but it can start it for any reason.
// 2) Any widgets that want to receive a drag should check for mouseUp
//    events along with ui_DragIsActive.
// 3) If a widget decides to accept, it should call DragAccept. The widget
//    then internally calls its 'dropF' function.
// 4) Then, the source widget's 'acceptF' function is called. If the drag
//    was canceled, the dest widget will be NULL.
//
//
// dropF and acceptF are generally not filled in by widgets themselves,
// but by application code.

// You should put "public" payload types here, rather than in your module
// or using them inline.
#define UI_DND_TEXT "text/plain" // Use for plain text
#define UI_DND_ATLASTEX "application/vnd.cryptic-atlas-tex" // AtlasTex pointer
#define UI_DND_TEXTURENAME "application/vnd.cryptic-atlas-tex-name" // AtlasTex name
#define UI_DND_OPAQUE "application/octet-stream" // Opaque binary data
#define UI_DND_ASSET "application/cryptic." // Prepended to Asset Manager doc type

typedef struct UIDnDPayload
{
	UIWidget *source;
	const char *type;
	UserData payload;
} UIDnDPayload;

void ui_DragStartEx(SA_PARAM_NN_VALID UIWidget *source, SA_PARAM_NN_STR const char *type, UserData payload, SA_PARAM_OP_VALID AtlasTex *icon, U32 iconColor, U32 centerIcon, SA_PARAM_OP_STR const char* dragCursorOverride);
#define ui_DragStart(source, type, payload, icon) ui_DragStartEx(source, type, payload, icon, 0xFF000000, 0, NULL)
void ui_DragAccept(SA_PARAM_NN_VALID UIWidget *dest);
void ui_DragCancel(void);
UIDnDPayload *ui_DragIsActive(void);

// Return true if a drag operation was dropped in this box, this frame.
bool ui_DragDropped(SA_PARAM_NN_VALID CBox *box);

#endif
