/***************************************************************************



***************************************************************************/

#ifndef UI_SKIN_H
#define UI_SKIN_H

#include "UICore.h"

// Create a new UI skin, using the values from base if not NULL.
SA_RET_NN_VALID UISkin *ui_SkinCreate(SA_PARAM_OP_VALID UISkin *base);
void ui_SkinFree(SA_PRE_NN_VALID SA_POST_P_FREE UISkin *skin);

// Copy the values in source to target.
void ui_SkinCopy(SA_PARAM_OP_VALID UISkin *target, SA_PARAM_OP_VALID UISkin *source);

//////////////////////////////////////////////////////////////////////////
// A skin is a set of global settings for UI colors and fonts. Each widget
// gets the default skin, g_ui_State.skin, when it's created. Widgets can
// take new skins later using ui_WidgetSkin.

void ui_SkinSetBackground(SA_PARAM_NN_VALID UISkin *skin, Color c);
void ui_SkinSetBackgroundEx(SA_PARAM_NN_VALID UISkin *skin, Color normal, Color highlight);

void ui_SkinSetButton(SA_PARAM_NN_VALID UISkin *skin, Color c);
void ui_SkinSetButtonEx(SA_PARAM_NN_VALID UISkin *skin, Color normal, Color highlight, Color pressed, Color disabled, Color changed, Color parented);

void ui_SkinSetBorder(SA_PARAM_NN_VALID UISkin *skin, Color c);
void ui_SkinSetBorderEx(SA_PARAM_NN_VALID UISkin *skin, Color active, Color inactive);

void ui_SkinSetThinBorder(SA_PARAM_NN_VALID UISkin *skin, Color c);

void ui_SkinSetTitleBar(SA_PARAM_NN_VALID UISkin *skin, Color c);
void ui_SkinSetTitleBarEx(SA_PARAM_NN_VALID UISkin *skin, Color active, Color inactive);

void ui_SkinSetTrough(SA_PARAM_NN_VALID UISkin *skin, Color c);
void ui_SkinSetTroughEx(SA_PARAM_NN_VALID UISkin *skin, Color normal, Color pressed);

void ui_SkinSetEntry(SA_PARAM_NN_VALID UISkin *skin, Color c);
void ui_SkinSetEntryEx(SA_PARAM_NN_VALID UISkin *skin, Color normal, Color active, Color disabled, Color changed, Color parented);

// Set a skin on a widget and all of its children.
void ui_WidgetSkin(SA_PARAM_NN_VALID UIWidget *widget, SA_PARAM_OP_VALID UISkin *skin);
// Unset a skin on a widget and all of its current children.
void ui_WidgetUnskin(SA_PARAM_NN_VALID UIWidget *widget, Color c1, Color c2, Color c3, Color c4);

void ui_SkinLoad( void );

extern DictionaryHandle g_hUISkinDict;

#endif
