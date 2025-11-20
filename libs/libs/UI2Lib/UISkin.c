/***************************************************************************



***************************************************************************/


#include "file.h"
#include "Color.h"
#include "mathutil.h"
#include "UISkin.h"
#include "UIScrollbar.h"
#include "ResourceManager.h"

#include "UICore_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

DictionaryHandle g_hUISkinDict;

UISkin *ui_SkinCreate(UISkin *base)
{
	UISkin *skin = StructCreate(parse_UISkin);
	if (!base)
		base = ui_GetActiveSkin();
	ui_SkinCopy(skin, base);
	return skin;
}

void ui_SkinFree(UISkin *skin)
{
	StructDestroy(parse_UISkin, skin);
}

void ui_SkinCopy(UISkin *target, UISkin *source)
{
	if( !source ) {
		source = &g_ui_State.default_skin;
	}
	
	devassert(target);
	StructCopyAll(parse_UISkin, source, target);
}

void ui_SkinSetBackground(UISkin *skin, Color c)
{
	ui_SkinSetBackgroundEx(skin, c, ColorDarken(c, 64));
}

void ui_SkinSetBackgroundEx(UISkin *skin, Color normal, Color highlight)
{
	setVec2(skin->background, normal, highlight);
}

void ui_SkinSetButton(UISkin *skin, Color c)
{
	Color inactive = c;
	Color changed = ColorDarken(c, 32);
	Color parented = ColorDarken(c, 32);
	changed.r += 32;
	parented.b += 32;
	inactive.a = 0x77;
	ui_SkinSetButtonEx(skin, c, ColorLighten(c, 64), ColorDarken(c, 64), inactive, changed, parented);
}

void ui_SkinSetButtonEx(UISkin *skin, Color normal, Color highlight, Color pressed, Color disabled, Color changed, Color parented)
{
	skin->button[0] = normal;
	skin->button[1] = highlight;
	skin->button[2] = pressed;
	skin->button[3] = disabled;
	skin->button[4] = changed;
	skin->button[5] = parented;
}

void ui_SkinSetBorder(UISkin *skin, Color c)
{
	ui_SkinSetBorderEx(skin, c, ColorDarken(c, 64));
}

void ui_SkinSetBorderEx(UISkin *skin, Color active, Color inactive)
{
	setVec2(skin->border, active, inactive);
}

void ui_SkinSetThinBorder(UISkin *skin, Color c)
{
	skin->thinBorder[0] = c;
}

void ui_SkinSetTrough(UISkin *skin, Color c)
{
	ui_SkinSetTroughEx(skin, c, ColorLighten(c, 64));
}

void ui_SkinSetTitleBar(UISkin *skin, Color c)
{
	ui_SkinSetTitleBarEx(skin, c, ColorDarken(c, 64));
}

void ui_SkinSetTitleBarEx(UISkin *skin, Color active, Color inactive)
{
	setVec2(skin->titlebar, active, inactive);
}

void ui_SkinSetTroughEx(UISkin *skin, Color normal, Color pressed)
{
	setVec2(skin->trough, normal, pressed);
}

void ui_SkinSetEntry(UISkin *skin, Color c)
{
	Color changed = ColorDarken(c, 32);
	Color parented = ColorDarken(c, 32);
	changed.r += 32;
	parented.b += 32;
	ui_SkinSetEntryEx(skin, c, ColorLighten(c, 32), ColorDarken(c, 32), changed, parented);
}

void ui_SkinSetEntryEx(UISkin *skin, Color normal, Color active, Color disabled, Color changed, Color parented)
{
	setVec5(skin->entry, normal, active, disabled, changed, parented);
}

void ui_WidgetSkin(UIWidget *widget, UISkin *skin)
{
	int i;
	widget->pOverrideSkin = skin;
	if (widget->sb)
		widget->sb->pOverrideSkin = skin;
	for (i = 0; i < eaSize(&widget->children); i++)
		ui_WidgetSkin(widget->children[i], skin);
}

void ui_WidgetUnskin(UIWidget *widget, Color c1, Color c2, Color c3, Color c4)
{
	int i;
	widget->pOverrideSkin = NULL;
	setVec4(widget->color, c1, c2, c3, c4);
	if (widget->sb)
	{
		widget->sb->pOverrideSkin = NULL;
		setVec3(widget->sb->color, c1, c2, c3);
	}
	for (i = 0; i < eaSize(&widget->children); i++)
		ui_WidgetUnskin(widget->children[i], c1, c2, c3, c4);
}

AUTO_RUN;
void ui_SkinRegister( void )
{
	g_hUISkinDict = RefSystem_RegisterSelfDefiningDictionary( "UISkin", false, parse_UISkin, true, false, NULL );
}

void ui_SkinLoad( void )
{
	resLoadResourcesFromDisk( g_hUISkinDict, "ui/skins/", ".skin", NULL, PARSER_OPTIONALFLAG );
}
