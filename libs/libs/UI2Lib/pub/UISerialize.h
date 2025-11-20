#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_SERIALIZE_H
#define UI_SERIALIZE_H

#include "UICore.h"

AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("");
typedef struct UILayouts
{
	UIWidget **layouts; AST(NAME("Widget"))
} UILayouts;

// Apply this layout to this widget, regardless of the names.
void ui_ApplyLayout(UIWidget *layout, UIWidget *widget);

// Search all available layouts for one matching this widget name,
// and apply it.
void ui_LoadLayout(SA_PARAM_NN_VALID UIWidget *widget);

void ui_AutoLoadLayouts(void);


#endif