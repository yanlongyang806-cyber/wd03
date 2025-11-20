/***************************************************************************



***************************************************************************/


#include "StringCache.h"
#include "file.h"
#include "EventTimingLog.h"
#include "cmdparse.h"
#include "Expression.h"
#include "Message.h"

#include "GraphicsLib.h"
#include "GfxCamera.h"
#include "GfxConsole.h"
#include "GfxDrawFrame.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GfxDebug.h"
#include "GfxPrimitive.h"

#include "inputMouse.h"
#include "inputText.h"
#include "inputKeyBind.h"
#include "inputlib.h"

#include "dynFxInfo.h"

#include "UICore.h"
#include "UISizer.h"
#include "UIDnD.h"
#include "UIFocus.h"
#include "UIInternal.h"
#include "UIScrollbar.h"
#include "UISerialize.h"
#include "UISkin.h"
#include "UITooltips.h"
#include "UIWindow.h"
#include "UITexSplit.h"
#include "UITextureAssembly.h"
#include "UIGen.h"
#include "UIGenJail.h"
#include "gen/UIGenPrivate.h"
#include "gen/UIGenDnD.h"
#include "gen/UIGenWindowManager.h"

#include "GfxSpriteText.h"
#include "gclGraphicsOptions.h"

#include "UICore_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

int g_traceFocus = 0;
UIGlobalState g_ui_State;
struct UITextureCache g_ui_Tex;

KeyBindProfile s_EscapeUIRestoreBinds = { "UI EscapeUIRestore Profile", NULL, KeyboardLocale_EnglishUS, NULL, true, true, NULL, NULL, InputBindPriorityUI };

static bool bDrawResolutionOutlines = false;
AUTO_CMD_INT(bDrawResolutionOutlines, drawresolutionoutlines);

// Draw outlines representing common resolutions and their safe areas on
// the screen.
AUTO_CMD_INT(bDrawResolutionOutlines, showsafearea) ACMD_CATEGORY(Graphics);

static CRITICAL_SECTION s_csFreeQueue;

static S32 s_bShowGameUI = true;
AUTO_CMD_INT(s_bShowGameUI, ShowGameUI) ACMD_CALLBACK(ui_SetGameUI)  ACMD_CATEGORY(Debug) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0);
// This command does not add any keybinds for showing the UI when the user presses escape
AUTO_CMD_INT(s_bShowGameUI, ShowGameUINoExtraKeyBinds) ACMD_CALLBACK(ui_SetGameUINoExtraKeyBinds)  ACMD_CATEGORY(Debug) ACMD_CMDLINEORPUBLIC ACMD_ACCESSLEVEL(0);

static F32 s_fTimeScale = 1.0;

// Set the UI time scale.
AUTO_CMD_FLOAT(s_fTimeScale, ui_TimeScale) ACMD_ACCESSLEVEL(1);

F32 ui_WidgetXPosition(UIWidget *widget, F32 pX, F32 pW, F32 pScale)
{
	F32 pOffset = widget->xPOffset * pW;
	switch (widget->offsetFrom)
	{
	case UILeft:
	case UITopLeft:
	case UIBottomLeft:
		return pX + widget->x * pScale + pOffset + widget->leftPad * pScale;
	case UIRight:
	case UITopRight:
	case UIBottomRight:
		return (pX + pW) - (ui_WidgetWidth(widget, pW, pScale) +
			(widget->rightPad + widget->leftPad + widget->x) * pScale + pOffset);
	case UITop:
	case UIBottom:
	case UINoDirection:
	default:
		return (pX + pW/2) - (ui_WidgetWidth(widget, pW, pScale) +
			(widget->x) * pScale + pOffset)/2;
	}
}

F32 ui_WidgetYPosition(UIWidget *widget, F32 pY, F32 pH, F32 pScale)
{
	F32 pOffset = widget->yPOffset * pH;
	switch (widget->offsetFrom)
	{
	case UITop:
	case UITopLeft:
	case UITopRight:
		return pY + (widget->y + widget->topPad) * pScale + pOffset;
	case UIBottom:
	case UIBottomLeft:
	case UIBottomRight:
		return (pY + pH) - (ui_WidgetHeight(widget, pH, pScale) +
			(widget->bottomPad + widget->topPad + widget->y) * pScale + pOffset);
	case UILeft:
	case UIRight:
	case UINoDirection:
	default:
		return (pY + pH/2) - (ui_WidgetHeight(widget, pH, pScale) +
			(widget->y) * pScale + pOffset)/2;
	}
}

F32 ui_WidgetWidth(UIWidget *widget, F32 parentWidth, F32 pScale)
{
	F32 width = widget->width;
	F32 scale = pScale * widget->scale;
	if (widget->widthUnit == UIUnitPercentage)
		return width * parentWidth - (widget->leftPad + widget->rightPad + widget->x) * pScale;
	else
		return width * scale - (widget->leftPad + widget->rightPad) * pScale;
}

F32 ui_WidgetHeight(UIWidget *widget, F32 parentHeight, F32 pScale)
{
	F32 height = widget->height;
	F32 scale = pScale * widget->scale;
	if (widget->heightUnit == UIUnitPercentage)
		return height * parentHeight - (widget->topPad + widget->bottomPad + widget->y) * pScale;
	else
		return height * scale - (widget->topPad + widget->bottomPad) * pScale;
}

F32 ui_WidgetCalcHeight(UIWidget *widget)
{
	F32 maxHeight = 0.0f;
	int i;

	assert(widget->heightUnit != UIUnitPercentage);
	for ( i = 0; i < eaSize(&widget->children); ++i )
	{
		assert(widget->children[i]->heightUnit != UIUnitPercentage);
		if ( widget->children[i]->y + widget->children[i]->height > maxHeight )
			maxHeight = widget->children[i]->y + widget->children[i]->height;
	}

	return maxHeight;
}

F32 ui_WidgetCalcWidth(SA_PARAM_NN_VALID UIWidget *widget)
{
	F32 maxWidth = 0.0f;
	int i;

	assert(widget->heightUnit != UIUnitPercentage);
	for ( i = 0; i < eaSize(&widget->children); ++i )
	{
		assert(widget->children[i]->widthUnit != UIUnitPercentage);
		if ( widget->children[i]->x + widget->children[i]->width > maxWidth )
			maxWidth = widget->children[i]->x + widget->children[i]->width;
	}

	return maxWidth;
}

static void ui_InitDefaultSkin(void)
{
	UISkin *pSkin = &g_ui_State.default_skin;
	Color background = CreateColorRGB(191, 188, 180),
		button = CreateColorRGB(225, 225, 225),
		border = CreateColorRGB(255, 255, 255),
		titlebar = CreateColorRGB(12, 38, 107),
		trough = CreateColorRGB(110, 108, 100),
		background2 = CreateColorRGB(40, 40, 120),
		entry = CreateColorRGB(245, 245, 245);

	if( g_ui_State.skinCmdlineFilename )
	{
		ParserReadTextFile(g_ui_State.skinCmdlineFilename, parse_UISkin, &g_ui_State.default_skin, 0);
		free( g_ui_State.skinCmdlineFilename );
	} else {
		ui_SkinSetBackgroundEx(pSkin, background, background2);
		ui_SkinSetButton(pSkin, button);
		ui_SkinSetBorder(pSkin, border);
		ui_SkinSetThinBorder(pSkin, ColorWhite);
		ui_SkinSetTitleBar(pSkin, titlebar);
		ui_SkinSetEntry(pSkin, entry);
		ui_SkinSetTrough(pSkin, trough);

		pSkin->pchCheckBoxChecked = "eui_tickybox_checked_8x8";
		pSkin->pchCheckBoxUnchecked = "eui_tickybox_unchecked_8x8";
		pSkin->pchRadioBoxChecked = "eui_tickybox_checked_8x8";
		pSkin->pchRadioBoxUnchecked = "eui_tickybox_unchecked_8x8";
		pSkin->pchMinus = "eui_button_minus";
		pSkin->pchPlus = "eui_button_plus";
		pSkin->eWindowTitleTextAlignment = UILeft;
		pSkin->iWindowModalBackgroundColor = 0x00000080;
		
		UI_SET_STYLE_FONT_NAME(pSkin->hWindowTitleFont, "Default_WindowTitle");
		UI_SET_STYLE_BAR_NAME(pSkin->hTitlebarBar, "Default_WindowTitle");

		UI_SET_STYLE_FONT_NAME(pSkin->hNormal, "Default");
	}

	REMOVE_HANDLE( g_ui_State.hActiveSkin );
	g_ui_State.pActiveSkin = NULL;
	ui_SetGlobalValuesFromActiveSkin();
}

AUTO_COMMAND ACMD_CMDLINE;
void ui_LoadSkin(char* filename)
{
	ParserReadTextFile(filename, parse_UISkin, &g_ui_State.default_skin, 0);
	
	if( g_ui_State.skinCmdlineFilename )
		free( g_ui_State.skinCmdlineFilename );
	g_ui_State.skinCmdlineFilename = strdup( filename );

	REMOVE_HANDLE( g_ui_State.hActiveSkin );
	g_ui_State.pActiveSkin = NULL;
	ui_SetGlobalValuesFromActiveSkin();
}

AUTO_COMMAND;
void ui_SaveSkin(char* filename)
{
	ParserWriteTextFile(filename, parse_UISkin, ui_GetActiveSkin(), 0, 0);
}

AUTO_COMMAND ACMD_NAME("TraceFocus");
void ui_TraceFocus(int enabled)
{
	g_traceFocus = !!enabled;
}

AUTO_STARTUP(UILib) ASTRT_DEPS(GraphicsLib Colors UITextureAssembly UISize UIGenScrollbar);
void ui_Startup(void)
{
	if(gbNoGraphics)
	{
		return;
	}

	loadstart_printf("UI2Lib startup...");
	InitializeCriticalSection(&s_csFreeQueue);
	g_ui_State.mouseDynNode = dynNodeAlloc();
	g_ui_State.scale = 1.f;
	g_ui_State.family = UI_FAMILY_ALL_BUT(UI_FAMILY_EDITOR | UI_FAMILY_CUTSCENE);
	g_ui_State.uGameUIHiderFlags = 0x00000000;
	g_ui_State.cbKeyInput = ui_ProcessKeyboardInput;
	g_ui_State.cursorLock = false;
	g_ui_State.states = stashTableCreateAddress(8);
	g_ui_State.bSafeToFree = true;
	ui_LoadStyles();
	SET_HANDLE_FROM_STRING(g_ui_FontDict, "Default", g_ui_State.font);
	ui_FillTextureCache();
	ui_InitDefaultSkin();
	ui_InitKeyBinds();
	ui_AutoLoadLayouts();
	ui_TexSplitLoad();
	ui_CursorLoad();
	ui_SkinLoad();
	
	if (!s_EscapeUIRestoreBinds.eaBinds)
		keybind_BindKeyInProfile(&s_EscapeUIRestoreBinds, "Escape", "RestoreGameUI");
	
	loadend_printf("UI2Lib startup done.");
}

static bool ui_GameUIIsVisible(void)
{
	return(g_ui_State.uGameUIHiderFlags==0);
}

static UIWidget *ui_FindWidgetParentWindowInGroup(UIWidgetGroup *group, UIWidget *widget, UIWidget *window)
{
	int i;
	for (i = 0; i < eaSize(group); i++)
	{
		if (widget == (*group)[i])
		{
			return window;
		}
		else if ((*group)[i] && (*group)[i]->children)
		{
			UIWidget *foundWindow;
			UIWidget *searchWindow = window;
			if ((*group)[i]->tickF == ui_WindowTick) {
				searchWindow = (*group)[i];
			}
			foundWindow = ui_FindWidgetParentWindowInGroup(&(*group)[i]->children, widget, searchWindow);
			if (foundWindow)
				return foundWindow;
		}
	}
	return NULL;
}

UIWidget *ui_FindParentWindow(RdrDevice *pDevice, UIWidget *widget)
{
	UIWidget *foundWindow;

	// Search Window Group (the likely one)
	foundWindow = ui_FindWidgetParentWindowInGroup((UIWidgetGroup*)ui_WindowGroupForDevice(pDevice), widget, NULL);

	if (!foundWindow) {
		// Search Main Group
		foundWindow = ui_FindWidgetParentWindowInGroup(ui_WidgetGroupForDevice(pDevice), widget, NULL);
	}

	if (!foundWindow) {
		// Search Top Group
		foundWindow = ui_FindWidgetParentWindowInGroup(ui_TopWidgetGroupForDevice(pDevice), widget, NULL);
	}

	if (!foundWindow) {
		// Search Pane Group
		foundWindow = ui_FindWidgetParentWindowInGroup(ui_PaneWidgetGroupForDevice(pDevice), widget, NULL);
	}

	return foundWindow;
}

static bool ui_FindWidgetInGroup(UIWidgetGroup group, UIWidget *widget)
{
	int i;
	for (i = 0; i < eaSize(&group); i++)
	{
		if (widget == group[i])
			return true;
		else if (group[i]->children && ui_FindWidgetInGroup(group[i]->children, widget))
			return true;
	}
	return false;
}

void ui_SetDeviceKeyboardTextInputEnable(bool bEnable)
{
	if (bEnable)
		rdrStartTextInput(g_ui_State.device);
	else
		rdrStopTextInput(g_ui_State.device);
}

void ui_StartDeviceKeyboardTextInput()
{
	rdrStartTextInput(g_ui_State.device);
}

void ui_StopDeviceKeyboardTextInput()
{
	rdrStopTextInput(g_ui_State.device);
}

void ui_ProcessKeyboardInput(F32 fElapsed, RdrDevice *pDevice)
{
	UIWidgetGroup *pMainGroup = ui_WidgetGroupForDevice(pDevice);
	UIWindowGroup *pWindowGroup = ui_WindowGroupForDevice(pDevice);
	UIWidgetGroup *pTopGroup = ui_TopWidgetGroupForDevice(pDevice);
	UIWidgetGroup *pPaneGroup = ui_PaneWidgetGroupForDevice(pDevice);
	KeyInput *pKey;
	if (g_ui_State.focused && !gfxConsoleVisible())
	{
		for (pKey = inpGetKeyBuf(); pKey; inpGetNextKey(&pKey))
		{
			bool bHandled = false;
			if (!isProductionMode() && (pKey->scancode == INP_TILDE || pKey->character == '`' || pKey->character == '~'))
				continue;
			else if (pKey->scancode == INP_ESCAPE && g_ui_State.focused->bConsumesEscape)
			{
				ui_SetFocus(NULL);
				bHandled = true;
			}
			else if (!g_ui_State.focused->inputF || !(bHandled = g_ui_State.focused->inputF(g_ui_State.focused, pKey)))
			{
				// The focused widget punted the decision to its top-level window.
				UIWidget *pTopWidget = eaGet(pTopGroup, 0);
				if (!pTopWidget)
				{
					UIWindow* pTopWindow = eaGet(pWindowGroup, 0);
					if (pTopWindow)
						pTopWidget = UI_WIDGET(pTopWindow);
				}
				if (!pTopWidget)
				{
					int i;
					for (i = 0; i < eaSize(pPaneGroup); i++)
					{
						if ((*pPaneGroup)[i]->children && ui_FindWidgetInGroup((*pPaneGroup)[i]->children, g_ui_State.focused))
							pTopWidget = (*pPaneGroup)[i];
					}
				}
				if (!pTopWidget)
					pTopWidget = eaGet(pMainGroup, 0);

				if (!bHandled)
				{
					UIWidget *pNewFocus = NULL;
					// MJF TODO: This logic is lame and rarely works.
					// If specific people want this feature, I'll
					// bring it back, but otherwise I'm gonna kill it.
					if( false ) {
						if (pKey->scancode == INP_UP)
							pNewFocus = ui_FocusUp(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
						else if (pKey->scancode == INP_DOWN)
							pNewFocus = ui_FocusDown(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
						else if (pKey->scancode == INP_LEFT)
							pNewFocus = ui_FocusLeft(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
						else if (pKey->scancode == INP_RIGHT)
							pNewFocus = ui_FocusRight(g_ui_State.focused, pMainGroup, g_ui_State.screenWidth, g_ui_State.screenHeight);
					}

					if (pKey->scancode == INP_TAB && pTopWidget && !(pKey->attrib & ~KIA_SHIFT))
					{
						UIWidget *pParentWindow = ui_FindParentWindow(pDevice, g_ui_State.focused);
						if (pParentWindow) {
							pTopWidget = pParentWindow;
						}
						if (pKey->attrib & KIA_SHIFT)
							pNewFocus = ui_FocusPrevInGroup(pTopWidget, g_ui_State.focused);
						else
							pNewFocus = ui_FocusNextInGroup(pTopWidget, g_ui_State.focused);
					}
					if (pNewFocus && pNewFocus != g_ui_State.focused)
						ui_SetFocus(pNewFocus);
					if (pNewFocus)
						inpCapture(pKey->scancode);
				}
			}
			if (bHandled)
				inpCapture(pKey->scancode);
			if (!g_ui_State.focused)
				break;
		}
	}
}

void ui_OncePerFramePerDevice(F32 frameTime, RdrDevice *currentDevice)
{
	UIWidgetGroup *maingroup;
	UIWidgetGroup *topgroup;
	UIWindowGroup *windowgroup;
	UIWidgetGroup *panegroup;
	UIDeviceState *deviceState;
	
	if(gbNoGraphics){
		return;
	}

	ui_GenStartTimings();

	PERFINFO_AUTO_START_FUNC_PIX();
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);

	frameTime *= s_fTimeScale;
	g_ui_State.timestep = frameTime;
	g_ui_State.totalTimeInMs += (frameTime * 1000);
	g_ui_State.device = currentDevice;
	g_ui_State.uiFrameCount++;

	// Use consistent family flags throughout the tick
	g_ui_State.prev_family = g_ui_State.family;
	g_ui_State.prev_familyChanged = g_ui_State.familyChanged;
	g_ui_State.familyChanged = 0;

	if (g_ui_State.graphicsUINeedsUpdate)
	{
		gfxSettingsChangedCallback(NULL);
		g_ui_State.graphicsUINeedsUpdate = 0;
	}

	maingroup = ui_WidgetGroupForDevice(currentDevice);
	topgroup = ui_TopWidgetGroupForDevice(currentDevice);
	windowgroup = ui_WindowGroupForDevice(currentDevice);
	panegroup = ui_PaneWidgetGroupForDevice(currentDevice);
	deviceState = ui_StateForDevice(currentDevice);

	gfxGetActiveDeviceSize(&g_ui_State.screenWidth, &g_ui_State.screenHeight);

	if( g_ui_State.minScreenWidth && g_ui_State.minScreenHeight ) {
		float wScale = (float)g_ui_State.screenWidth / g_ui_State.minScreenWidth;
		float hScale = (float)g_ui_State.screenHeight / g_ui_State.minScreenHeight;
		g_ui_State.scale = CLAMP( MIN( wScale, hScale ), 0.01, 1 );
	}
	
	mousePos(&g_ui_State.mouseX, &g_ui_State.mouseY);
    {
        Vec3 pos;
        pos[ 0 ] = (float)g_ui_State.mouseX / g_ui_State.screenWidth;
        pos[ 1 ] = 1 - (float)g_ui_State.mouseY / g_ui_State.screenHeight;
        pos[ 2 ] = 0;

        dynNodeSetPos( g_ui_State.mouseDynNode, pos );
    }

	if (inpDidAnything())
		g_ui_State.chInputDidAnything = 5; // FIXME(jfw): 5 should be max(SpriteCacheSkip) + 1.
	else if (g_ui_State.chInputDidAnything > 0)
		g_ui_State.chInputDidAnything--;

	ui_DrawWindowButtons();
	ui_CursorOncePerFrameBeforeTick();

	PERFINFO_AUTO_START_PIX("ui_WidgetGroupFreeInternal", 1);
	EnterCriticalSection(&s_csFreeQueue);
	g_ui_State.bSafeToFree = false;
	ui_WidgetGroupFreeInternal(&g_ui_State.freeQueue);
	LeaveCriticalSection(&s_csFreeQueue);
	PERFINFO_AUTO_STOP_PIX();

	g_ui_State.mode = (g_ui_State.screenWidth < 1024 || g_ui_State.screenHeight < 720) ? UISD : UIHD;
	if (g_ui_State.focused && !((ui_IsActive(g_ui_State.focused) || g_ui_State.focused->uFocusWhenDisabled) && ui_IsVisible(g_ui_State.focused)))
		ui_SetFocus(NULL);

	ui_GenOncePerFrameEarly();

	GEN_PERFINFO_START_DEFAULT("UI Ticking", 1);
	g_ui_State.drawZ = UI_TOP_Z;

	GEN_PERFINFO_START_DEFAULT("ui_WidgetGroupTick top", 1);
	ui_WidgetGroupTick(topgroup, 0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight, g_ui_State.scale);
	GEN_PERFINFO_STOP_DEFAULT();

	GEN_PERFINFO_START_DEFAULT("ui_WidgetGroupTick window", 1);
	ui_WidgetGroupTick((UIWidgetGroup*)windowgroup, 0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight, g_ui_State.scale);
	GEN_PERFINFO_STOP_DEFAULT();

	g_ui_State.drawZ = UI_PANE_Z;
	setVec2(g_ui_State.viewportMin, 0, 0);
	setVec2(g_ui_State.viewportMax, g_ui_State.screenWidth, g_ui_State.screenHeight);
	//ui_WidgetGroupTick(panegroup, g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0], g_ui_State.viewportMax[1], g_ui_State.scale);
	{
		// copy of ui_WidgetGroupTick modifed to use the viewport values directly since they will be modified within the tick functions
		S32 i, count = eaSize(panegroup);
		GEN_PERFINFO_START_DEFAULT("ui_WidgetGroupTick pane", 1);
		if (count)
		{
			size_t size = sizeof(UIWidget *) * count;
			UIWidgetGroup copied = NULL;
			devassertmsg(size < 20480, "Way too many widgets in this group.");
			copied = _alloca(size);
			memcpy(copied, *panegroup, size);
			// Pane group must tick and draw in forwards order
			for (i = 0; i < count; i++)
			{
				if (copied[i])
				{
					if ((copied[i]->family & g_ui_State.prev_familyChanged) && (copied[i]->family & g_ui_State.prev_family) && copied[i]->familyAddedF)
						copied[i]->familyAddedF(copied[i], g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);
					if ((copied[i]->family & g_ui_State.prev_family) && copied[i]->tickF)
						copied[i]->tickF(copied[i], g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);
					else if ((copied[i]->family & g_ui_State.prev_familyChanged) && !(copied[i]->family & g_ui_State.prev_family) && copied[i]->familyRemovedF)
						copied[i]->familyRemovedF(copied[i], g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);
				}
			}
		}
		GEN_PERFINFO_STOP_DEFAULT();
	}
	g_ui_State.drawZ = UI_MAIN_Z;

	GEN_PERFINFO_START_DEFAULT("ui_WidgetGroupTick main", 1);
	{
		S32 i, count = eaSize(maingroup);
		static UIWidgetGroup maingroup_ui2 = NULL;
		static UIWidgetGroup maingroup_uigen = NULL;

		eaClearFast(&maingroup_ui2);
		eaCopy(&maingroup_uigen,maingroup);

		// Move anything that isn't a UIGen widget into the UI2 list
		for(i=0; i<count; i++)
		{
			if(maingroup_uigen[i] && !maingroup_uigen[i]->uUIGenWidget)
			{
				UIWidget *pWidget = eaRemove(&maingroup_uigen,i);
				eaPush(&maingroup_ui2,pWidget);
				i--;
				count--;
			}
		}

		// Handle Window Manager state changes before the UIGen update frame
		// begins, so that any state changes that need to hide gens, hide
		// gens before they have a chance to start executing expressions.
		GEN_PERFINFO_START_DEFAULT("ui_GenWindowManagerOncePerFrameInput", 1);
		ui_GenWindowManagerOncePerFrameInput();
		GEN_PERFINFO_STOP_DEFAULT();

		// Handle Jail Keeper state changes before the UIGen update frame
		// begins, so that any state changes that need to hide gens, hide
		// gens before they have a chance to start executing expressions.
		GEN_PERFINFO_START_DEFAULT("ui_GenJailOncePerFrameInput", 1);
		ui_GenJailOncePerFrameInput();
		GEN_PERFINFO_STOP_DEFAULT();

		GEN_PERFINFO_START_DEFAULT("ui_GenOncePerFramePointerUpdate", 1);
		ui_GenOncePerFramePointerUpdate();
		GEN_PERFINFO_STOP_DEFAULT();

		// Next process KB input
		GEN_PERFINFO_START_DEFAULT("UI Input", 1);
		g_ui_State.cbKeyInput(frameTime, currentDevice);
		ui_GenProcessInput();
		GEN_PERFINFO_STOP_DEFAULT();

		// Tick all UI2 stuff in the maingroup, so that any UI2 stuff can eat input before UIGens do
		GEN_PERFINFO_START_DEFAULT("ui_WidgetGroupTick main UI2", 1);
		ui_WidgetGroupTick(&maingroup_ui2, g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);
		GEN_PERFINFO_STOP_DEFAULT();

		// Have the UIGens processes input (by calling the rather tragically named TickCB)
		// This is done before calling the UIGen-based widget groups' tick functions, which is what causes state update
		//  and layout.  This way the gens process input based on the state of the last frame (which is what the input
		//  would be based on) and any side effects of the input happen this frame.
		//  on the old state.
		GEN_PERFINFO_START_DEFAULT("ui_GenOncePerFrameInput", 1);
		ui_GenOncePerFrameInput(ui_GameUIIsVisible());
		GEN_PERFINFO_STOP_DEFAULT();

		// UI has now captured all the input it's going to get, so we can run inpUpdateLate() to process keybinds
		inpUpdateLate(gfxGetActiveInputDevice());

		// All input has been consumed, so it's now safe to run the camera
		gfxRunActiveCameraController(-1, NULL);
		if(gfxWillWaitForZOcclusion())
			gfxFlipCameraFrustum(gfxGetActiveCameraView());

		// Since we've run the camera, we can now start the early ZO test, which runs in another thread
		//  while the main thread does the rest of the UI update/layout.
		if(g_ui_State.bEarlyZOcclusion)
		{
			g_ui_State.bEarlyZOcclusion = false;
			gfxStartEarlyZOcclusionTest();
		}

		if (g_ui_State.cbPostCameraUpdateFunc)
		{
			g_ui_State.cbPostCameraUpdateFunc(frameTime);
		}

		// Tick all UIGen stuff in the maingroup, which  actually results in calls to ui_GenUpdate() and ui_GenLayout()
		GEN_PERFINFO_START_DEFAULT("ui_WidgetGroupTick main UIGen", 1);
		ui_WidgetGroupTick(&maingroup_uigen, g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);
		GEN_PERFINFO_STOP_DEFAULT();
	}
	GEN_PERFINFO_STOP_DEFAULT();

	GEN_PERFINFO_START_DEFAULT("UI Debug Update", 1);
	ui_GenDebugUpdate();
	GEN_PERFINFO_STOP_DEFAULT();

	GEN_PERFINFO_STOP_DEFAULT();

	// This must be done after UILib updates, so that widgets and/or
	// the camera controller can lock the mouse.
	ui_CursorOncePerFrameAfterTick();

	GEN_PERFINFO_START_DEFAULT("UI Drawing Top", 1);
	g_ui_State.drawZ = UI_TOP_Z;
	ui_WidgetGroupDraw((UIWidgetGroup*)windowgroup, 0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight, g_ui_State.scale);
	ui_WidgetGroupDraw(topgroup, 0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight, g_ui_State.scale);

	g_ui_State.drawZ = UI_PANE_Z;
	setVec2(g_ui_State.viewportMin, 0, 0);
	setVec2(g_ui_State.viewportMax, g_ui_State.screenWidth, g_ui_State.screenHeight);
	//ui_WidgetGroupDraw(panegroup, 0, 0, g_ui_State.screenWidth, g_ui_State.screenHeight, g_ui_State.scale);
	{
		// copy of ui_WidgetGroupDraw modifed to use the viewport values directly since they will be modified within the draw functions
		int i;
		// Pane group must tick and draw in forwards order
		for (i = 0; i < eaSize(panegroup); ++i)
		{
			UIWidget *widget = (*panegroup)[i];
			if (widget && (widget->family & g_ui_State.family) && widget->drawF)
				widget->drawF(widget, g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);
		}
	}
	GEN_PERFINFO_STOP_DEFAULT();

	GEN_PERFINFO_START_DEFAULT("UI Middle", 1);
	g_ui_State.drawZ = UI_MAIN_Z;
	ui_GenOncePerFrameMiddle(ui_GameUIIsVisible());
	GEN_PERFINFO_STOP_DEFAULT();

	GEN_PERFINFO_START_DEFAULT("UI Drawing Main", 1);
	g_ui_State.drawZ = UI_UI2LIB_Z;
	ui_WidgetGroupDraw(maingroup, g_ui_State.viewportMin[0], g_ui_State.viewportMin[1], g_ui_State.viewportMax[0] - g_ui_State.viewportMin[0], g_ui_State.viewportMax[1] - g_ui_State.viewportMin[1], g_ui_State.scale);

	if(deviceState->dnd && !deviceState->cursor.draggedIcon && deviceState->dnd->source)
	{
		UIWidget *src = deviceState->dnd->source;
		S32 tempX, tempY;

		tempX = src->x; tempY = src->y;
		src->x = 0; src->y = 0;
		if(src->drawF)
			src->drawF(src, g_ui_State.mouseX - src->width/2, g_ui_State.mouseY - src->height/2, src->width, src->height, g_ui_State.scale);
		src->x = tempX; src->y = tempY;
	}
		
	GEN_PERFINFO_STOP_DEFAULT();

	if (ui_DragIsActive() && (mouseUnfilteredUp(MS_LEFT) || !mouseIsDown(MS_LEFT)))
		ui_DragCancel();

	if ((mouseDownAny(MS_LEFT) || mouseDownAny(MS_RIGHT)))
		ui_SetFocus(NULL);
	
	ui_GenOncePerFrameLate(ui_GameUIIsVisible());

	g_ui_State.drawZ = UI_INFINITE_Z;
	ui_TooltipsTick();

	EnterCriticalSection(&s_csFreeQueue);
	ui_WidgetGroupFreeInternal(&g_ui_State.freeQueue);
	g_ui_State.bSafeToFree = true;
	LeaveCriticalSection(&s_csFreeQueue);

	g_ui_State.cursorLock = false;

	if (bDrawResolutionOutlines)
		ui_DrawResolutionOutlines();

	gfxDebugViewport(g_ui_State.viewportMin, g_ui_State.viewportMax);

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);
	PERFINFO_AUTO_STOP_FUNC_PIX();

	// Warn the developer if they have created a UI which is over-budget (due to expressions, generally)
	ui_GenProcessTimings();
}


void ui_WidgetGroupTick(UIWidgetGroup *widgets, UI_PARENT_ARGS)
{
	ui_WidgetGroupTickEx( widgets, UI_PARENT_VALUES, pX, pY, pW, pH, pScale );
}

void ui_WidgetGroupTickEx(UIWidgetGroup *widgets, UI_PARENT_ARGS, F32 pXNoScroll, F32 pYNoScroll, F32 pWNoScroll, F32 pHNoScroll, F32 pScaleNoScroll )
{
	S32 i, count = eaSize(widgets);
	size_t size = sizeof(UIWidget *) * count;
	UIWidgetGroup copied = NULL;

	if (!count)
		return;
	// Iterate over a copy of the list, so that the list can be modified during
	// the iteration (e.g. changing window order).
	devassertmsg(size < 20480, "Way too many widgets in this group.");
	copied = _alloca(size);
	memcpy(copied, *widgets, size);
	for (i = 0; i < count; i++)
	{
		if (copied[i])
		{
			if ((copied[i]->family & g_ui_State.prev_familyChanged) && (copied[i]->family & g_ui_State.prev_family) && copied[i]->familyAddedF != NULL)
				copied[i]->familyAddedF(copied[i], UI_PARENT_VALUES);
			if ((copied[i]->family & g_ui_State.prev_family) && copied[i]->tickF != NULL)
				copied[i]->tickF(copied[i],
								 (copied[i]->bNoScrollX ? pXNoScroll : pX),
								 (copied[i]->bNoScrollY ? pYNoScroll : pY),
								 (copied[i]->bNoScrollX ? pWNoScroll : pW),
								 (copied[i]->bNoScrollY ? pHNoScroll : pH),
								 (copied[i]->bNoScrollScale ? pScaleNoScroll : pScale));
			else if ((copied[i]->family & g_ui_State.prev_familyChanged) && !(copied[i]->family & g_ui_State.prev_family) && copied[i]->familyRemovedF != NULL)
				copied[i]->familyRemovedF(copied[i], UI_PARENT_VALUES);
		}
	}
}

void ui_WidgetGroupDraw(UIWidgetGroup *widgets, UI_PARENT_ARGS)
{
	ui_WidgetGroupDrawEx( widgets, UI_PARENT_VALUES, pX, pY, pW, pH, pScale );
}

void ui_WidgetGroupDrawEx(UIWidgetGroup *widgets, UI_PARENT_ARGS, F32 pXNoScroll, F32 pYNoScroll, F32 pWNoScroll, F32 pHNoScroll, F32 pScaleNoScroll )
{
	int i;
	
	if (gbNoGraphics)
	{
		return;
	}

	for (i = eaSize(widgets) - 1; i >= 0; i--)
	{
		UIWidget *widget = (*widgets)[i];

		if (widget && (widget->family & g_ui_State.prev_family) && widget->drawF)
		{
			widget->drawF(widget,
						  (widget->bNoScrollX ? pXNoScroll : pX),
						  (widget->bNoScrollY ? pYNoScroll : pY),
						  (widget->bNoScrollX ? pWNoScroll : pW),
						  (widget->bNoScrollY ? pHNoScroll : pH),
						  (widget->bNoScrollScale ? pScaleNoScroll : pScale));
		}
	}
}

void ui_WidgetGroupFreeInternal(UIWidgetGroup *widgets)
{
	U32 i, count = eaSize(widgets);
	size_t size = sizeof(UIWidget *) * count;
	UIWidgetGroup copied = NULL;

	if (!count)
		return;
	// Larger than Tick/Draw because this can contain the union of several
	// groups freed during this frame.
	devassertmsg(size < 204800, "Way too many widgets in this group.");
	copied = _alloca(size);
	memcpy(copied, *widgets, size);
	for (i = 0; i < count; i++)
		if (copied[i])
			copied[i]->freeF(copied[i]);
	eaDestroy(widgets);
}

void ui_WidgetSetTextString(UIWidget *widget, const char *text)
{
	REMOVE_HANDLE( widget->message_USEACCESSOR );
	if( !text ) {
		estrDestroy( &widget->text_USEACCESSOR );
	} else if( !widget->text_USEACCESSOR || (widget->text_USEACCESSOR != text && strcmp( widget->text_USEACCESSOR, text ))) {
		estrCopy2( &widget->text_USEACCESSOR, text );
	}
}

void ui_WidgetSetTextMessage(UIWidget *widget, const char* messageKey )
{
	estrDestroy( &widget->text_USEACCESSOR );
	if( !messageKey ) {
		REMOVE_HANDLE( widget->message_USEACCESSOR );
	} else {
		SET_HANDLE_FROM_STRING( "Message", messageKey, widget->message_USEACCESSOR );
	}
}

void ui_WidgetSetName(UIWidget *widget, const char *name)
{
	if (widget->name != name)
	{
		widget->name = allocAddString(name);
		if (name)
			ui_LoadLayout(widget);
	}
}

void ui_WidgetSetTooltipString(UIWidget *widget, const char *tooltip)
{
	REMOVE_HANDLE(widget->tooltipMessage_USEACCESSOR);
		
	if (tooltip && widget->tooltip_USEACECSSOR && tooltip && strcmp(tooltip, widget->tooltip_USEACECSSOR)==0)
		return;
	if (widget->tooltip_USEACECSSOR)
		StructFreeString(widget->tooltip_USEACECSSOR);
	widget->tooltip_USEACECSSOR = StructAllocString(tooltip);
}

void ui_WidgetSetTooltipMessage(UIWidget *widget, const char *tooltipKey)
{
	StructFreeStringSafe(&widget->tooltip_USEACECSSOR);
	SET_HANDLE_FROM_STRING( "Message", tooltipKey, widget->tooltipMessage_USEACCESSOR );
}

const char* ui_WidgetGetText(SA_PARAM_NN_VALID UIWidget *widget)
{
	if( widget ) {
		if( IS_HANDLE_ACTIVE( widget->message_USEACCESSOR )) {
			return TranslateMessageRef( widget->message_USEACCESSOR );
		} else {
			return widget->text_USEACCESSOR;
		}
	} else {
		return NULL;
	}
}

const char *ui_WidgetGetTooltip(UIWidget *widget)
{
	if( widget ) {
		if( IS_HANDLE_ACTIVE( widget->tooltipMessage_USEACCESSOR )) {
			return TranslateMessageRef( widget->tooltipMessage_USEACCESSOR );
		} else {
			return widget->tooltip_USEACECSSOR;
		}
	} else {
		return NULL;
	}
}

void ui_SetActive(SA_PARAM_OP_VALID UIWidget *widget, bool active)
{
	if (!widget)
		return;
	if (active)
		widget->state &= ~kWidgetModifier_Inactive;
	else
		widget->state |= kWidgetModifier_Inactive;
	ui_WidgetGroupSetActive(&widget->children, active);
}

void ui_WidgetGroupSetActive(const UIWidgetGroup *group, bool active)
{
	int i;
	for (i = 0; i < eaSize(group); i++)
		ui_SetActive((*group)[i], active);
}

void ui_WidgetFreeInternal(UIWidget *widget)
{
	if (widget->onFreeF)
		widget->onFreeF(widget);
	ui_WidgetGroupFreeInternal(&widget->children);
	if (ui_IsFocused(widget))
	{
		widget->onUnfocusF = NULL;
		ui_SetFocus(NULL);
	}
	if (widget->group)
		ui_WidgetGroupRemove(widget->group, widget);
	if (widget->sb)
		ui_ScrollbarFree(widget->sb);
	estrDestroy(&widget->text_USEACCESSOR);
	REMOVE_HANDLE(widget->message_USEACCESSOR);
	if (widget->dynNode)
		dynNodeFree(widget->dynNode);
	REMOVE_HANDLE(widget->hOverrideSkin);
	REMOVE_HANDLE(widget->hOverrideFont);
	StructFreeStringSafe(&widget->tooltip_USEACECSSOR);
	REMOVE_HANDLE(widget->tooltipMessage_USEACCESSOR);

	if(widget->pSizer)
		ui_SizerFreeInternal(widget->pSizer);

	free(widget);
}

void ui_WidgetForceQueueFree(UIWidget *widget)
{
	if (widget)
	{
		ui_WidgetRemoveFromGroup(widget);
		eaPushUnique(&g_ui_State.freeQueue, widget);
	}
}

void ui_WidgetQueueFree(UIWidget *widget)
{
	if (!widget)
		return;
	EnterCriticalSection(&s_csFreeQueue);
	if (g_ui_State.bSafeToFree)
		widget->freeF(widget);
	else
		ui_WidgetForceQueueFree(widget);
	LeaveCriticalSection(&s_csFreeQueue);
}

void ui_WidgetInitializeEx(UIWidget *widget, UILoopFunction tickF, UILoopFunction drawF,
						 UIFreeFunction freeF, UIInputFunction inputF, UIFocusFunction focusF
						 MEM_DBG_PARMS)
{
	widget->tickF = tickF;
	widget->drawF = drawF;
	widget->freeF = freeF;
	widget->inputF = inputF;
	widget->focusF = focusF;
	widget->group = NULL;
	widget->family = UI_FAMILY_ALWAYS_SHOW;
	ui_WidgetSetDimensions(widget, 0.f, 0.f);
	ui_WidgetSetPosition(widget, 0.f, 0.f);
	ui_WidgetSetPadding(widget, 0.f, 0.f);
	ui_WidgetSkin(widget, NULL);
	widget->color[0] = ColorGreen;
	widget->color[1] = ColorRed;
	widget->color[2] = ColorGreen;
	widget->color[3] = ColorRed;
	widget->scale = 1.f;
	//widget->keybinds = &g_ui_KeyBindActivate;
	widget->create_filename = caller_fname;
	widget->create_line = line;
}

void ui_WidgetSetPositionEx(UIWidget *widget, F32 x, F32 y, F32 xPOffset, F32 yPOffset, UIDirection offsetFrom)
{
	widget->x = x;
	widget->y = y;
	widget->xPOffset = xPOffset;
	widget->yPOffset = yPOffset;
	widget->offsetFrom = offsetFrom;
}

F32 ui_WidgetGetX(UIWidget *widget)
{
	if(widget->x > 0.0 && widget->x < 1.0)
	{
		assertmsg(0, "Percentile x retrieval not implemented.");
	}
	return widget->x;
}

F32 ui_WidgetGetY(UIWidget *widget)
{
	if(widget->y > 0.0 && widget->y < 1.0)
	{
		assertmsg(0, "Percentile y retrieval not implemented.");
	}
	return widget->y;
}

F32 ui_WidgetGetNextY(SA_PARAM_NN_VALID UIWidget *widget)
{
	return widget->y + widget->height * widget->scale;
}

F32 ui_WidgetGetNextX(SA_PARAM_NN_VALID UIWidget *widget)
{
	return widget->x + widget->width * widget->scale;
}

void ui_WidgetSetDimensionsEx(UIWidget *widget, F32 w, F32 h, UIUnitType wUnit, UIUnitType hUnit)
{
	ui_WidgetSetWidthEx(widget, w, wUnit);
	ui_WidgetSetHeightEx(widget, h, hUnit);
}

void ui_WidgetSetWidthEx(UIWidget *widget, F32 w, UIUnitType wUnit)
{
	widget->width = w;
	widget->widthUnit = wUnit;
}

void ui_WidgetSetHeightEx(UIWidget *widget, F32 h, UIUnitType hUnit)
{
	widget->height = h;
	widget->heightUnit = hUnit;
}

F32 ui_WidgetGetHeight(UIWidget *widget)
{
	if((widget->height > 0.0 && widget->height < 1.0) || widget->heightUnit==UIUnitPercentage)
	{
		assertmsg(0, "Percentile height retrieval not implemented.");
	}
	return widget->height;
}

F32 ui_WidgetGetWidth(UIWidget *widget)
{
	if((widget->height > 0.0 && widget->width < 1.0) || widget->widthUnit==UIUnitPercentage)
	{
		assertmsg(0, "Percentile width retrieval not implemented.");
	}
	return widget->width;
}

const char* ui_WidgetGetName(UIAnyWidget *any)
{
	UIWidget *widget = any;
	const char* nameptr = "Unnamed widget";
	static char name[1024];

	if(widget->name && widget->name[0])
		nameptr = widget->name;

	sprintf(name, "%s (%s:%d)", nameptr, widget->create_filename, widget->create_line);
	return name;
}

void ui_WidgetSetPaddingEx(UIWidget *widget, F32 left, F32 right, F32 top, F32 bottom)
{
	widget->leftPad = left;
	widget->rightPad = right;
	widget->topPad = top;
	widget->bottomPad = bottom;
}

F32 ui_WidgetGetPaddingTop(UIWidget *widget)
{
	return widget->topPad;
}

F32 ui_WidgetGetPaddingBottom(UIWidget *widget)
{
	return widget->bottomPad;
}

static void ui_WidgetSetSizerWidgetRecurse(SA_PARAM_NN_VALID UIWidget *pWidget, SA_PARAM_OP_VALID UISizer *pSizer)
{
	int i;
	int count = eaSize(&pSizer->children);
	pSizer->pWidget = pWidget;
	for(i = 0; i < count; i++)
	{
		if(pSizer->children[i]->type == UISizerChildType_Sizer)
			ui_WidgetSetSizerWidgetRecurse(pWidget, pSizer->children[i]->pSizer);
		else if(pSizer->children[i]->type == UISizerChildType_Widget)
		{
			if(-1 == eaFind(&pWidget->children, pSizer->children[i]->pWidget))
				ui_WidgetAddChild(pWidget, pSizer->children[i]->pWidget);
		}
	}		
}

void ui_WidgetSetSizer(UIWidget *pWidget, UISizer *pSizer)
{
	if(pWidget->pSizer != pSizer)
	{
		if(pWidget->pSizer)
			ui_SizerFreeInternal(pWidget->pSizer);

		pWidget->pSizer = pSizer;

		if(pWidget->pSizer)
			ui_WidgetSetSizerWidgetRecurse(pWidget, pWidget->pSizer);
	}
}

void ui_WidgetSizerLayout(UIWidget *pWidget, F32 width, F32 height)
{
	if(pWidget->pSizer)
		ui_SizerLayout(pWidget->pSizer, width, height);
}

void ui_WidgetSetPreDragCallback(UIWidget *widget, UIActivationFunc preDragF, UserData preDragData)
{
	widget->preDragF = preDragF;
	widget->preDragData = preDragData;
}

void ui_WidgetSetDragCallback(UIWidget *widget, UIActivationFunc dragF, UserData dragData)
{
	widget->dragF = dragF;
	widget->dragData = dragData;
}

void ui_WidgetSetDropCallback(UIWidget *widget, UIDragFunction dropF, UserData dropData)
{
	widget->dropF = dropF;
	widget->dropData = dropData;
}

void ui_WidgetSetAcceptCallback(UIWidget *widget, UIDragFunction acceptF, UserData acceptData)
{
	widget->acceptF = acceptF;
	widget->acceptData = acceptData;
}

void ui_WidgetSetFocusCallback(UIWidget *widget, UIActivationFunc onFocusF, UserData onFocusData)
{
	widget->onFocusF = onFocusF;
	widget->onFocusData = onFocusData;
}

void ui_WidgetSetUnfocusCallback(UIWidget *widget, UIActivationFunc onUnfocusF, UserData onUnfocusData)
{
	widget->onUnfocusF = onUnfocusF;
	widget->onUnfocusData = onUnfocusData;
}

void ui_WidgetSetFreeCallback(UIWidget *widget, UIFreeFunction onFreeF)
{
	widget->onFreeF = onFreeF;
}

void ui_WidgetSetMouseOverCallback(UIWidget *widget, UIActivationFunc mouseOverF, UserData mouseOverData)
{
	widget->mouseOverF = mouseOverF;
	widget->mouseOverData = mouseOverData;
}

void ui_WidgetSetMouseLeaveCallback(UIWidget *widget, UIActivationFunc mouseLeaveF, UserData mouseLeaveData)
{
	widget->mouseLeaveF = mouseLeaveF;
	widget->mouseLeaveData = mouseLeaveData;
}

void ui_WidgetSetFamily(SA_PARAM_NN_VALID UIWidget *widget, U8 family)
{
	devassertmsg(family, "Widget has no family, this is probably not what you want.");
	widget->family = family;
}

void ui_WidgetSetClickThrough(SA_PARAM_NN_VALID UIWidget *widget, bool c)
{
	widget->uClickThrough = !!c;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////
// Family Activation

// Set visible UI families. This is the only direct access to State.family that should be used.
static void ui_SetActiveFamilies(U8 family)
{
	U8 prev_family = g_ui_State.family;

	devassertmsg(family, "Setting UI FAMILY to zero! This is probably not what you want.");
	family |= UI_FAMILY_ALWAYS_SHOW;

	g_ui_State.family = family;
	g_ui_State.familyChanged = g_ui_State.familyChanged ^ family ^ prev_family; // if it was toggled twice the same frame, clear the changed bit

	// In case we need to debug this again
	//  printf("Active Family: %d -> %d\n", (int)prev_family, (int)family);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void ui_AddActiveFamilies(U8 family)
{
	devassertmsg((family & UI_FAMILY_GAME)==0, "Use ui_GameUIShow instead of AddActiveFamilies to show UI_FAMILY_GAME.");
	ui_SetActiveFamilies(g_ui_State.family | family);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void ui_RemoveActiveFamilies(U8 family)
{
	devassertmsg((family & UI_FAMILY_GAME)==0, "Use ui_GameUIHide instead of RemoveActiveFamilies to hide UI_FAMILY_GAME.");
	ui_SetActiveFamilies(g_ui_State.family & ~family);
}


extern bool GameDialogPopup(void);

void ui_UpdateGameUIVisibility()
{
	if (g_ui_State.uGameUIHiderFlags==0)
	{
		// We are visible
		ui_SetActiveFamilies(g_ui_State.family | UI_FAMILY_GAME);
		
		// Remove the escape in case it was there
		keybind_PopProfile(&s_EscapeUIRestoreBinds);	

		// Not sure about this. It was in the escapable version before. Need to make sure we aren't spamming this function
		GameDialogPopup();
		ui_GenWindowManagerForceTick();
	}
	else
	{
		// Need to be not visible
		ui_SetActiveFamilies(g_ui_State.family & ~UI_FAMILY_GAME); 
		
		// Add in the escape keybind if we have no non-escapable hiders
		if ((g_ui_State.uGameUIHiderFlags & (UI_GAME_HIDER_VIDEO | UI_GAME_HIDER_ENTDEBUG | UI_GAME_HIDER_CMD_NOESC | UI_GAME_HIDER_EDITOR)) != 0)
		{
			// Not escapable
			keybind_PopProfile(&s_EscapeUIRestoreBinds);	// Remove the escape in case it was there
		}
		else
		{
			keybind_PushProfile(&s_EscapeUIRestoreBinds);
		}
	}
}

void ui_GameUIShow(U32 uHider)
{
	g_ui_State.uGameUIHiderFlags &= ~uHider;
	ui_UpdateGameUIVisibility();
}

void ui_GameUIHide(U32 uHider)
{
	if (uHider!=0)
	{
		g_ui_State.uGameUIHiderFlags |= uHider;
		ui_UpdateGameUIVisibility();
	}
}

// This is the cmd-line available version
void ui_SetGameUI(void)
{
	if (s_bShowGameUI)
	{
		ui_GameUIShow(UI_GAME_HIDER_CMD);
	}
	else
	{
		ui_GameUIHide(UI_GAME_HIDER_CMD);
	}
}

void ui_SetGameUINoExtraKeyBinds(void)
{
	if (s_bShowGameUI)
	{
		ui_GameUIShow(UI_GAME_HIDER_CMD_NOESC);
	}
	else
	{
		ui_GameUIHide(UI_GAME_HIDER_CMD_NOESC);
	}
}

// Restore the UI when escape is pressed
AUTO_COMMAND ACMD_NAME(RestoreGameUI) ACMD_ACCESSLEVEL(0);
void ui_RestoreGameUI(void)
{
	// Clear all escapable Hider flags and update the visibility accordingly
	ui_GameUIShow(UI_GAME_HIDER_CUTSCENE | UI_GAME_HIDER_EDITOR | UI_GAME_HIDER_UGC | UI_GAME_HIDER_CMD);
}


void ui_PlaceBoxNextToBox( CBox* toPlace, const CBox* box )
{
	float toPlaceWidth = CBoxWidth( toPlace );
	float toPlaceHeight = CBoxHeight( toPlace );
	float x = box->lx+1; // plus 1 makes popup menus not have their first element already active so they do not get clicked by mistake
	float y = box->hy+1; // plus 1 makes popup menus not have their first element already active so they do not get clicked by mistake

	if( x + toPlaceWidth > g_ui_State.viewportMax[ 0 ]) {
		x = MAX( box->hx - toPlaceWidth, 0 );
	}	
	if( y + toPlaceHeight > g_ui_State.viewportMax[ 1 ]) {
		y = MAX( box->ly - toPlaceHeight, 0 );
	}

	toPlace->lx = x;
	toPlace->ly = y;
	toPlace->hx = x + toPlaceWidth;
	toPlace->hy = y + toPlaceHeight;
}

void ui_DrawTextInBoxSingleLine( UIStyleFont *pFont, const char* text, bool truncate, const CBox* box, float z, float scale, UIDirection dir )
{
	// Calcuate the lower left position of the text (that's what gfxfont_Print takes)
	float x;
	float y;
	
	if( dir & UILeft ) {
		x = box->lx;
	} else if( dir & UIRight ) {
		x = box->hx - ui_StyleFontWidth( pFont, scale, text );
	} else {
		x = (box->lx + box->hx - ui_StyleFontWidth( pFont, scale, text )) / 2;
	}
	if( dir & UITop ) {
		y = box->ly + ui_StyleFontLineHeight( pFont, scale );
	} else if( dir & UIBottom ) {
		y = box->hy;
	} else {
		y = (box->ly + box->hy + ui_StyleFontLineHeight( pFont, scale )) / 2;
	}

	if( truncate ) {
		x = MAX( x, box->lx );
		y = MAX( y, box->ly + ui_StyleFontLineHeight( pFont, scale ));
		gfxfont_PrintMaxWidth( x, y, z, box->hx - box->lx, scale, scale, 0, text );
	} else {
		gfxfont_Print( x, y, z, scale, scale, 0, text );
	}
}

void ui_DrawTextInBoxSingleLineRotatedCCW( UIStyleFont *pFont, const char* text, bool truncate, const CBox* box, float z, float scale, UIDirection dir )
{
	// Calcuate the lower left position of the text (that's what gfxfont_Print takes)
	float lineWidth = ui_StyleFontWidth( pFont, scale, text );
	float lineHeight = ui_StyleFontLineHeight( pFont, scale );
	float x;
	float y;

	if( dir & UILeft ) {
		// +-----+
		// |t    |
		// |x    |
		// |e    |
		// |T    |
		// +-----+
		x = box->lx + lineHeight;
	} else if( dir & UIRight ) {
		// +-----+
		// |    t|
		// |    x|
		// |    e|
		// |    T|
		// +-----+
		x = box->hx;
	} else {
		// +-----+
		// |  t  |
		// |  x  |
		// |  e  |
		// |  T	 |
		// +-----+
		x = (box->lx + box->hx + lineHeight) / 2;
	}
	if( dir & UITop ) {
		// +-----+
		// |t    |
		// |x    |
		// |e    |
		// |T    |
		// |     |
		// |     |
		// +-----+
		y = box->ly + lineWidth;
	} else if( dir & UIBottom ) {
		// +-----+
		// |     |
		// |     |
		// |t    |
		// |x    |
		// |e    |
		// |T    |
		// +-----+
		y = box->hy;
	} else {
		// +-----+
		// |     |
		// |t    |
		// |x    |
		// |e    |
		// |T    |
		// |     |
		// +-----+
		y = (box->ly + box->hy + lineWidth) / 2;
	}

	x = CLAMP( x, box->lx, box->hx );
	y = CLAMP( y, box->ly, box->hy );

	// MJF Feb/1/2013
	//
	// Matrix math -- all rendering is done at (0, 0), so the matrix
	// can do the full transformation
	//
	// This math is strange because the coordinate system goes through
	// a bunch of transforms putting Y=0 alternately at the top or
	// bottom of the screen.
	gfxMatrixPush();
	{
		Mat3* m = gfxMatrixGet();
		zeroMat3( *m );
		(*m)[0][1] = 1;
		(*m)[1][0] = -1;
		(*m)[2][0] = g_ui_State.screenHeight + x;
		(*m)[2][1] = g_ui_State.screenHeight - y; 
	}

	// MJF Feb/1/2013 -- The clipping is done BEFORE the matrix above
	// is applied (see create_df_sprite_ex).  This is a limitation of
	// the sprite rendering code.  To support arbitrary clip rects,
	// reverse the transformation applied to the clip rect.
	{
		Clipper2D* clipper = clipperGetCurrent();
		// MJF Feb/12/2013 -- Disable the clipper.  Some other system
		// is using this to scale things.
		if( !clipper || true ) {
			clipperPush( NULL );
		} else {
			CBox clipBox = clipper->box;
			CBox revRotClipBox;

			revRotClipBox.lx = y - clipBox.hy;
			revRotClipBox.hx = y - clipBox.ly;
			revRotClipBox.ly = clipBox.lx - x;
			revRotClipBox.hy = clipBox.hx - x;
			clipperPush( &revRotClipBox );
		}
	}
	
	if( truncate ) {
		gfxfont_PrintMaxWidth( 0, 0, z, box->hy - box->ly, scale, scale, 0, text );
	} else {
		gfxfont_Print( 0, 0, z, scale, scale, 0, text );
	}
	clipperPop();
	gfxMatrixPop();
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void ui_dummy(void) { }

void ui_InitKeyBinds()
{
	g_ui_KeyBindModal.pchName = "UI Key Binds (Modal)";
	g_ui_KeyBindModal.bTrickleCommands = true;
	g_ui_KeyBindModal.bTrickleKeys = false;
}

void ui_SetFocusEx(UIAnyWidget *anyWidget, const char* file, int line, const char* reason)
{
	UIWidget *widget = (UIWidget *)anyWidget;
	UIAnyWidget *prevFocus = g_ui_State.focused;
	bool pre_safe_to_free = g_ui_State.bSafeToFree;

	if (widget && widget->group)
		ui_WidgetGroupSteal(widget->group, widget);

	// If widget is NULL, it might just be an attempt to get the keybinds
	// into a sane state -- don't return immediately.
	if (widget == g_ui_State.focused)
		return;

	g_ui_State.bSafeToFree = false;

	if (g_ui_State.focused)
	{
		UIWidget *focused = g_ui_State.focused;
		keybind_PopProfileEx(focused->keybinds, InputBindPriorityBlock);
		g_ui_State.focused = NULL;
		if (focused->onUnfocusF)
			focused->onUnfocusF(focused, focused->onUnfocusData);
		if (focused->unfocusF)
			focused->unfocusF(focused,widget);
	}

	if(g_traceFocus && g_ui_State.focused && !widget)
		printf("Focus Loss (%s:%d): %s - Reason: %s\n", file, line, ui_WidgetGetName(g_ui_State.focused), reason);

	g_ui_State.focused = widget;
	if (widget)
	{
		if (g_ui_State.focused)
			ui_GenSetFocus(NULL);

		if (widget->keybinds)
			keybind_PushProfileEx(widget->keybinds, InputBindPriorityBlock);
		if (widget->focusF)
			widget->focusF(widget,prevFocus);
		if (widget->onFocusF)
			widget->onFocusF(widget,widget->onFocusData);

		if(g_traceFocus)
			printf("Focus Gain (%s:%d): %s - Reason: %s\n", file, line, ui_WidgetGetName(g_ui_State.focused), reason);
	}

	g_ui_State.bSafeToFree = pre_safe_to_free;
}

void ui_WidgetGroupAdd(UIWidgetGroup *group, UIWidget *widget)
{
	devassertmsg(!widget || !widget->group || widget->group == group, "Widget is already in a group, call ui_WidgetGroupMove to change group");
	if (eaFind(group, widget) < 0)
	{
		int pos = 0;
		while((pos < eaSize(group)) && (*group)[pos] && (*group)[pos]->priority > widget->priority)
			++pos;
		eaInsert(group, widget, pos);
		if (widget)
			widget->group = group;
	}
}

void ui_WidgetGroupMove(UIWidgetGroup* group, UIWidget* widget)
{
	devassertmsg( group, "You must have a group to move a widget into.  Call ui_WidgetRemoveFromGroup to just remove it." );
	if( widget->group != group ) {
		if( widget->group ) {
			ui_WidgetGroupRemove(widget->group, widget);
		}
		if( group ) {
			ui_WidgetGroupAdd(group, widget);
		}
	}
}

bool ui_WidgetGroupRemove(UIWidgetGroup *group, UIWidget *widget)
{
	bool found;

	found = eaFindAndRemove(group, widget)!=-1;
	if (widget && widget->group == group)
		widget->group = NULL;

	return found;
}

void ui_WidgetGroupSort(UIWidgetGroup *group)
{
	UIWidgetGroup copy = NULL;
	eaCopy(&copy, group);
	eaClear(group);
	FOR_EACH_IN_EARRAY(copy, UIWidget, widget)
	{
		widget->group = NULL;
		ui_WidgetGroupAdd(group, widget);
	}
	FOR_EACH_END;
}

// Tells whether or not a modal window is currently displayed
bool ui_IsModalShowing(void)
{
	UIWidgetGroup *group = ui_TopWidgetGroupForDevice(NULL);
	return g_ui_State.modal || (eaSize(group) && ((*group)[0]->priority > 0));
}

void ui_WidgetGroupSteal(UIWidgetGroup *group, UIWidget *widget)
{
	// Don't steal if no widgets or we are the first widget already
	if (!eaSize(group) || !widget || ((*group)[0] == widget))
		return;
	if (eaFindAndRemove(group, widget) >= 0)
	{
		// Move to be head of its priority level, but not in front of higher priority
		int pos = 0;
		while((pos < eaSize(group)) && (*group)[pos] && ((*group)[pos]->priority > widget->priority))
			++pos;
		eaInsert(group, widget, pos);
	}
}

void ui_WidgetGroupQueueFree(UIWidgetGroup *widgets)
{
	S32 i = 0;
	for (i = eaSize(widgets)-1; i >= 0; --i)
		ui_WidgetQueueFree((*widgets)[i]);
}

void ui_WidgetGroupQueueFreeAndRemove(UIWidgetGroup *widgets)
{
	S32 i = 0;
	for (i = eaSize(widgets)-1; i >= 0; --i)
	{
		UIWidget *widget = (*widgets)[i];
		if (widget && widget->group == widgets)
			widget->group = NULL;
		ui_WidgetQueueFree((*widgets)[i]);
	}
	eaClear(widgets);
}

void ui_WidgetAddChild(UIWidget *parent, UIWidget *child)
{
	ui_WidgetGroupAdd(&parent->children, child);
}

bool ui_WidgetRemoveChild(UIWidget *parent, UIWidget *child)
{
	return ui_WidgetGroupRemove(&parent->children, child);
}

void ui_WidgetGroupGetDimensions(UIWidgetGroup *group, F32 *width, F32 *height, UI_PARENT_ARGS)
{
	int i;
	F32 mw=0, mh=0;
	for (i=eaSize(group)-1; i>=0; i--) {
		UIWidget *widget = (*group)[i];
		UI_GET_COORDINATES((UIWidgetWidget*)widget);
		if (x + w - pX > mw)
			mw = x + w - pX;
		if (y + h - pY > mh)
			mh = y + h - pY;
	}
	if (width)
		*width = mw;
	if (height)
		*height = mh;
}

UIDeviceState *ui_StateForDevice(RdrDevice *device)
{
	UIDeviceState *state = NULL;
	if (!device)
		device = gfxGetActiveOrPrimaryDevice();
	if (gbNoGraphics && !device)
		device = (void *)1;
	devassertmsg(device, "No device found (and graphics are on)");

	if (!stashAddressFindPointer(g_ui_State.states, device, &state))
	{
		state = calloc(1, sizeof(*state));
		ui_SetCursorForDeviceState(state, NULL, NULL, 2, 2, 0xFFFFFFFF, 0xFFFFFFFF);
		stashAddressAddPointer(g_ui_State.states, device, state, true);
	}
	
	return state;
}

UIWidgetGroup *ui_WidgetGroupForDevice(RdrDevice *device)
{
	UIDeviceState *state = ui_StateForDevice(device);
	return &state->maingroup;
}

UIWidgetGroup *ui_TopWidgetGroupForDevice(RdrDevice *device)
{
	UIDeviceState *state = ui_StateForDevice(device);
	return &state->topgroup;
}

UIWindowGroup *ui_WindowGroupForDevice(RdrDevice *device)
{
	UIDeviceState *state = ui_StateForDevice(device);
	return &state->windowgroup;
}

UIWidgetGroup *ui_PaneWidgetGroupForDevice(RdrDevice *device)
{
	UIDeviceState *state = ui_StateForDevice(device);
	return &state->panegroup;
}

void ui_StateFreeForDevice(RdrDevice *device)
{
	UIDeviceState *state = NULL;
	if (!device)
		device = gfxGetActiveOrPrimaryDevice();
	if (gbNoGraphics && !device)
		device = (void *)1;
	devassertmsg(device, "No device found (and graphics are on)");

	if (stashAddressRemovePointer(g_ui_State.states, device, (void **)&state))
	{
		ui_WidgetGroupFreeInternal(&state->topgroup);
		ui_WidgetGroupFreeInternal(&state->panegroup);
		ui_WidgetGroupFreeInternal(&state->maingroup);
		free(state);
	}
}

void ui_WidgetAddToDevice(UIWidget *widget, struct RdrDevice *device)
{
	ui_WidgetGroupAdd(ui_WidgetGroupForDevice(device), widget);
}

void ui_WidgetRemoveFromGroup(UIWidget *widget)
{
	if (widget && widget->group)
	{
		ui_WidgetGroupRemove(widget->group, widget);
		ui_WidgetUnfocusAll(widget);
	}
}

void ui_TopWidgetAddToDevice(UIWidget *widget, struct RdrDevice *device)
{
	ui_WidgetGroupAdd(ui_TopWidgetGroupForDevice(device), widget);
}

void ui_WindowAddToDevice(UIWindow *window, struct RdrDevice *device)
{
	ui_WidgetGroupAdd((UIWidgetGroup*)ui_WindowGroupForDevice(device), UI_WIDGET(window));
}

void ui_PaneWidgetAddToDevice(UIWidget *widget, struct RdrDevice *device)
{
	ui_WidgetGroupAdd(ui_PaneWidgetGroupForDevice(device), widget);
}

void ui_DrawCapsule(const CBox *box, F32 z, Color c, F32 scale)
{
	U32 uiColor = RGBAFromColor(c);
	Color4 Tint = {uiColor ? uiColor : 0xFF00, uiColor ? uiColor : 0xFF00, uiColor ? uiColor : 0xFF00, uiColor ? uiColor : 0xFF00};
	ui_TextureAssemblyDraw(GET_REF(g_ui_Tex.hCapsuleAssembly), box, NULL, scale, z, z + 0.001, 255, &Tint);
}

void ui_DrawOutline(const CBox *box, F32 z, Color c, F32 scale)
{
	U32 uiColor = RGBAFromColor(c);
	Color4 Tint = {uiColor ? uiColor : 0xFF00, uiColor ? uiColor : 0xFF00, uiColor ? uiColor : 0xFF00, uiColor ? uiColor : 0xFF00};
	ui_TextureAssemblyDraw(GET_REF(g_ui_Tex.hMiniFrameAssembly), box, NULL, scale, z, z + 0.001, 255, &Tint);
}

void ui_LayOutVertically(UIWidgetGroup *peaGroup)
{
	F32 y = UI_HSTEP;
	S32 i;
	for (i = eaSize(peaGroup) - 1; i >= 0; i--)
	{
		if ((*peaGroup)[i])
		{
			ui_WidgetSetPosition((*peaGroup)[i], (*peaGroup)[i]->x, y);
			y += (*peaGroup)[i]->height + UI_STEP;
		}
	}
}

UIWidget *ui_WidgetFindChild(UIWidget *widget, const char *childName)
{
	int i;
	for (i = 0; i < eaSize(&widget->children); i++)
		if (widget->children[i] && widget->children[i]->name && !stricmp(widget->children[i]->name, childName))
			return widget->children[i];
	// devassertmsg(false, "Widget name not found");
	return NULL;
}

bool ui_WidgetSearchTree(UIWidget* treeRoot, UIWidget* toFind)
{
	int i;

	if( treeRoot == NULL ) {
		return false;
	}

	if (treeRoot == toFind) {
		return true;
	}
	
	for (i = 0; i < eaSize(&treeRoot->children); i++)
		if (treeRoot->children[i] && ui_WidgetSearchTree(treeRoot->children[i], toFind)) {
			return true;
		}

	return false;
}

bool ui_IsVisible(UIWidget *widget)
{
	return (widget->group ? true : false) && (widget->family & g_ui_State.family);
}

bool ui_IsFocused( UIAnyWidget *anyWidget)
{
	UIWidget *widget = (UIWidget*)anyWidget;
	return g_ui_State.focused == widget;
}

bool ui_IsFocusedOrChildren(UIAnyWidget *anyWidget)
{
	UIWidget *widget = (UIWidget*)anyWidget;

	if (!widget) {
		return g_ui_State.focused == NULL;
	} else if (ui_IsFocused(widget)) {
		return true;
	} else {
		return ui_IsFocusedWidgetGroup(&widget->children);
	}
}

bool ui_IsFocusedWidgetGroup(SA_PARAM_NN_VALID UIWidgetGroup *group)
{
	
	S32 i;
	for (i = 0; i < eaSize(group); i++)
	{
		UIWidget *pWidget = (*group)[i];
		if (ui_IsFocusedOrChildren(pWidget))
			return true;
	}

	return false;
}

bool ui_IsActive(UIWidget *widget)
{
	return !(widget->state & kWidgetModifier_Inactive);
}

bool ui_IsHovering(UIWidget *widget)
{
	return widget->state & kWidgetModifier_Hovering;
}

bool ui_IsChanged(UIWidget *widget)
{
	return !!(widget->state & kWidgetModifier_Changed);
}

bool ui_IsInherited(UIWidget *widget)
{
	return !!(widget->state & kWidgetModifier_Inherited);
}

void ui_SetChanged(UIWidget *pWidget, bool bChanged)
{
	if (bChanged)
		pWidget->state |= kWidgetModifier_Changed;
	else
		pWidget->state &= ~kWidgetModifier_Changed;
}

void ui_SetInherited(UIWidget *pWidget, bool bInherited)
{
	if (bInherited)
		pWidget->state |= kWidgetModifier_Inherited;
	else
		pWidget->state &= ~kWidgetModifier_Inherited;
}

void ui_SetHovering(UIWidget *pWidget, bool bHovering)
{
	if (bHovering)
		pWidget->state |= kWidgetModifier_Hovering;
	else
		pWidget->state &= ~kWidgetModifier_Hovering;
}

void ui_WidgetSetContextCallback(UIWidget *widget, UIActivationFunc contextF, UserData contextData)
{
	widget->contextF = contextF;
	widget->contextData = contextData;
}

UIStyleFont *ui_WidgetGetFont(UIWidget *pWidget)
{
	if (GET_REF(pWidget->hOverrideFont))
		return GET_REF(pWidget->hOverrideFont);
	else if (GET_REF(ui_WidgetGetSkin(pWidget)->hNormal))
		return GET_REF(ui_WidgetGetSkin(pWidget)->hNormal);
	else
		return GET_REF(g_ui_State.font);
}

void ui_WidgetSetFont(UIWidget *pWidget, const char *pchFont)
{
	UI_SET_STYLE_FONT_NAME(pWidget->hOverrideFont, pchFont);
}

static bool g_TextureCacheInited = false;

// This function sucks so I'm putting it at the end.
void ui_FillTextureCache(void)
{
	g_TextureCacheInited = true;
	g_ui_Tex.windowTitleLeft = atlasLoadTexture("eui_window_title_left");

	g_ui_Tex.windowTitleMiddle = atlasLoadTexture("eui_window_title_middle");
	g_ui_Tex.windowTitleRight = atlasLoadTexture("eui_window_title_right");

	g_ui_Tex.arrowSmallUp = atlasLoadTexture("eui_arrow_small_up");
	g_ui_Tex.arrowSmallDown = atlasLoadTexture("eui_arrow_small_down");
	g_ui_Tex.arrowSmallLeft = atlasLoadTexture("eui_arrow_small_left");
	g_ui_Tex.arrowSmallRight = atlasLoadTexture("eui_arrow_small_right");

	g_ui_Tex.arrowDropDown = atlasLoadTexture("eui_arrow_dropdown_down.tga");

	SET_HANDLE_FROM_STRING(g_ui_BorderDict, "Default_Capsule_Filled", g_ui_Tex.hCapsule);
	SET_HANDLE_FROM_STRING(g_ui_BorderDict, "Default_MiniFrame_Filled", g_ui_Tex.hMiniFrame);

	SET_HANDLE_FROM_STRING("UITextureAssembly", "Default_Capsule_Filled", g_ui_Tex.hCapsuleAssembly);
	SET_HANDLE_FROM_STRING("UITextureAssembly", "Default_Capsule_Filled_Disabled", g_ui_Tex.hCapsuleDisabledAssembly);
	SET_HANDLE_FROM_STRING("UITextureAssembly", "Default_MiniFrame_Empty", g_ui_Tex.hMiniFrameAssembly);
	SET_HANDLE_FROM_STRING("UITextureAssembly", "Default_WindowFrame", g_ui_Tex.hWindowFrameAssembly);

	g_ui_Tex.minus = atlasLoadTexture("eui_button_minus");
	g_ui_Tex.plus = atlasLoadTexture("eui_button_plus");

	g_ui_Tex.white = atlasLoadTexture("white");
}

void ui_SetGlobalValuesFromActiveSkin(void)
{
	UISkin* skin = ui_GetActiveSkin();
	AtlasTex *pTempTex=NULL;

	if(!g_TextureCacheInited)
		return;
	ui_FillTextureCache();
	
	if(skin->pchDefaultCursor && skin->pchDefaultCursor[0])
		ui_SetCurrentDefaultCursor(skin->pchDefaultCursor);
	else
		ui_SetCurrentDefaultCursor("Default");

	#define SET_IF_REF(x, r) ((pTempTex = UI_TEXTURE(r)) ? (x) = pTempTex : 0);
	SET_IF_REF(g_ui_Tex.arrowDropDown, skin->pchArrowDropDown);
	SET_IF_REF(g_ui_Tex.minus, skin->pchMinus);
	SET_IF_REF(g_ui_Tex.plus, skin->pchPlus);
}

void ui_DrawAndDecrementOverlay(UIWidget *widget, CBox *box, F32 z)
{
	if (widget->highlightPercent > 0)
	{
		Color c = ui_WidgetGetSkin(widget)->background[1];
		widget->highlightPercent -= g_ui_State.timestep / UI_HIGHLIGHT_TIME;
		MAX1(widget->highlightPercent, 0);
		c.a = (U8)MIN(c.a, c.a * widget->highlightPercent);
		display_sprite_box(g_ui_Tex.white, box, z + 0.5, RGBAFromColor(c));
	}
}

void ui_WidgetDummyFocusFunc(SA_PARAM_NN_VALID UIWidget *widget, UIAnyWidget *focusitem)
{
}

void ui_DrawResolutionOutlines(void)
{
	Vec2 aSizes[] = {{1920, 1080}, {1632, 918}, {1280, 720}, {1088, 612}, {854, 480}, {726, 408}, {640, 480}, {544, 408}};
	Color aColors[] = {{0, 0xFF, 0, 0xFF}, {0xFF, 0x80, 0x40, 0xFF}, {0, 0, 0xFF, 0xFF}, {0xFF, 0, 0, 0xFF}};
	S32 i;

	devassertmsg(ARRAY_SIZE(aSizes) == 2 * ARRAY_SIZE(aColors), "Array size mismatch");

	for (i = 0; i < ARRAY_SIZE(aSizes); i++)
	{
		CBox box = {0, 0, aSizes[i][0], aSizes[i][1]};
		CBoxSetCenter(&box, g_ui_State.screenWidth / 2, g_ui_State.screenHeight / 2);
		ui_DrawOutline(&box, 1.f, aColors[i / 2], 1.f);
	}
}

void ui_WidgetGroupUnfocus(UIWidgetGroup *group)
{
	S32 i;
	for (i = 0; i < eaSize(group) && g_ui_State.focused; i++)
	{
		UIWidget *pWidget = (*group)[i];
		if (ui_IsFocused(pWidget))
			ui_SetFocus(NULL);
	}
}

void ui_WidgetUnfocusAll(UIWidget *widget)
{
	if (ui_IsFocused(widget))
		ui_SetFocus(NULL);
	if (widget)
		ui_WidgetGroupUnfocus(&widget->children);
}

void ui_WidgetDestroy(UIAnyWidget **ppWidget)
{
	ui_WidgetQueueFree(*ppWidget);
	*ppWidget = NULL;
}

const char *ui_ControllerButtonToTexture(S32 iButton)
{
	switch (iButton)
	{
	case INP_LTRIGGER: return UI_XBOX_TINY_LT;
	case INP_RTRIGGER: return UI_XBOX_TINY_RT;
	case INP_LB: return UI_XBOX_TINY_LB;
	case INP_RB: return UI_XBOX_TINY_RB;
	case INP_AB: return UI_XBOX_TINY_AB;
	case INP_BB: return UI_XBOX_TINY_BB;
	case INP_XB: return UI_XBOX_TINY_XB;
	case INP_YB: return UI_XBOX_TINY_YB;
	case INP_START: return UI_XBOX_TINY_START;
	case INP_SELECT: return UI_XBOX_TINY_BACK;
	case INP_JOYPAD_DOWN: return UI_XBOX_TINY_DPAD_DOWN;
	case INP_JOYPAD_LEFT: return UI_XBOX_TINY_DPAD_LEFT;
	case INP_JOYPAD_RIGHT: return UI_XBOX_TINY_DPAD_RIGHT;
	case INP_JOYPAD_UP: return UI_XBOX_TINY_DPAD_UP;
	case INP_LSTICK_UP:
	case INP_LSTICK_DOWN:
	case INP_LSTICK_LEFT:
	case INP_LSTICK_RIGHT:
	case INP_LSTICK:
		return UI_XBOX_TINY_LSTICK;
	case INP_RSTICK_UP:
	case INP_RSTICK_DOWN:
	case INP_RSTICK_LEFT:
	case INP_RSTICK_RIGHT:
	case INP_RSTICK:
		return UI_XBOX_TINY_RSTICK;
	default: return "white";
	}
}

const char *ui_ControllerButtonToBigTexture(S32 iButton)
{
	switch (iButton)
	{
	case INP_LTRIGGER: return UI_XBOX_LT;
	case INP_RTRIGGER: return UI_XBOX_RT;
	case INP_LB: return UI_XBOX_LB;
	case INP_RB: return UI_XBOX_RB;
	case INP_AB: return UI_XBOX_AB;
	case INP_BB: return UI_XBOX_BB;
	case INP_XB: return UI_XBOX_XB;
	case INP_YB: return UI_XBOX_YB;
	case INP_START: return UI_XBOX_START;
	case INP_SELECT: return UI_XBOX_BACK;
	default: return "white";
	}
}

void ui_LayOutHBox(UIDirection eFrom, F32 fStartY, F32 fYPOffset, ...)
{
	va_list va;
	UIWidget *pWidget;
	F32 fX = 0;
	F32 fXP = 0;
	F32 fTmpX;
	va_start(va, fYPOffset);
	fTmpX = va_arg(va, double);
	pWidget = va_arg(va, UIWidget *);
	while (pWidget)
	{
		fX += fTmpX;
		ui_WidgetSetPositionEx(pWidget, fX, fStartY, fXP, fYPOffset, eFrom);
		if (pWidget->widthUnit == UIUnitPercentage)
			fXP += pWidget->width;
		else
			fX += pWidget->width;

		fTmpX = va_arg(va, double);
		pWidget = va_arg(va, UIWidget *);
	}
	va_end(va);
}

void ui_WidgetSetCBox(UIWidget *pWidget, CBox *pBox)
{
	ui_WidgetSetPosition(pWidget, pBox->lx, pBox->ly);
	ui_WidgetSetDimensions(pWidget, CBoxWidth(pBox), CBoxHeight(pBox));
}

void ui_WidgetGetCBox(UIWidget *pWidget, CBox *pBox)
{
	CBoxSet(pBox, pWidget->x, pWidget->y, pWidget->x+pWidget->width*pWidget->scale, pWidget->y+pWidget->height*pWidget->scale);
}

void ui_ScreenGetBounds(S32 *piLeft, S32 *piRight, S32 *piTop, S32 *piBottom)
{
#if PLATFORM_CONSOLE
	if (piLeft) *piLeft = UI_SAFE_AREA_OFFSET * g_ui_State.screenWidth;
	if (piRight) *piRight = (1 - UI_SAFE_AREA_OFFSET) * g_ui_State.screenWidth;
	if (piTop) *piTop = UI_SAFE_AREA_OFFSET * g_ui_State.screenHeight;
	if (piBottom) *piBottom = (1 - UI_SAFE_AREA_OFFSET) * g_ui_State.screenHeight;
#else
	if (piLeft) *piLeft = 0;
	if (piRight) *piRight = g_ui_State.screenWidth;
	if (piTop) *piTop = 0;
	if (piBottom) *piBottom = g_ui_State.screenHeight;
#endif
}

void ui_WidgetInternalUpdateDynNode(UIWidget* pWidget, UI_PARENT_ARGS)
{
	if (pWidget->dynNode)
	{
		Vec3 pos;
		int widgetX = ui_WidgetXPosition(pWidget, pX, pW, pScale);
		int widgetY = ui_WidgetYPosition(pWidget, pY, pH, pScale);
		int width, height;

		gfxGetActiveDeviceSize( &width, &height );

		pos[ 0 ] = (float)widgetX / width;
		pos[ 1 ] = 1 - (float)widgetY / height;
		pos[ 2 ] = 0;
		dynNodeSetPos( pWidget->dynNode, pos );
	}
}

/// Attach an FX by name to WIDGET, optionally with a target.
DynFx* ui_WidgetAttachFx( SA_PARAM_NN_VALID UIWidget* widget, SA_PARAM_NN_STR const char* fxName, SA_PARAM_OP_VALID DynNode* targetNode )
{
	DynAddFxParams params = {0};
	if (!widget->dynNode)
		widget->dynNode = dynNodeAlloc();
	params.pTargetRoot = targetNode;
	params.pSourceRoot = widget->dynNode;
	params.bOverridePriority = true;
	params.ePriorityOverride = edpOverride; // All UI FX are override
	params.eSource = eDynFxSource_UI;
    return dynAddFx( dynFxGetUiManager(false), fxName, &params);
}

/// Attach an FX by name to the mouse, optionally with a target.
DynFx* ui_MouseAttachFx( SA_PARAM_NN_STR const char* fxName, SA_PARAM_OP_VALID DynNode* targetNode )
{
	DynAddFxParams params = {0};
	params.pTargetRoot = targetNode;
	params.pSourceRoot = g_ui_State.mouseDynNode;
	params.eSource = eDynFxSource_UI;
	params.bOverridePriority = true;
	params.ePriorityOverride = edpOverride; // All UI FX are override
    return dynAddFx( dynFxGetUiManager(false), fxName, &params);
}

/// Attach an FX to the mouse.  Debugging version of ui_MouseAttachFx.
AUTO_COMMAND ACMD_CATEGORY(dynFx);
void dfxTestFxMouse2d(const char* fx_name ACMD_NAMELIST("DynFxInfo", RESOURCEINFO) )
{
    ui_MouseAttachFx( fx_name, NULL );
}

const char *ui_ButtonForCommand(const char *pchCommand)
{
	KeyBind *pBind = keybind_BindForCommand(pchCommand, true, true);
	if (pBind)
		return ui_ControllerButtonToTexture(pBind->iKey1);
	else
		return "";
}

void ui_SetPlayAudioFunc(UIPlayAudioByName cbPlayAudio, UIPlayAudioByName cbValidateAudio)
{
	g_ui_State.cbPlayAudio = cbPlayAudio;
	g_ui_State.cbValidateAudio = cbValidateAudio;
}

void ui_SetBeforeMainDraw(UIExternLoopFunc cbOncePerFrameBeforeMainDraw)
{
	g_ui_State.cbOncePerFrameBeforeMainDraw = cbOncePerFrameBeforeMainDraw;
}

void ui_SetPostCameraUpdateFunc(UIPostCameraUpdateFunc func)
{
	g_ui_State.cbPostCameraUpdateFunc = func;
}

bool ui_LoadTexture(const char *pchName, AtlasTex **ppTex, const char *pchFilename, const char *pchResourceName)
{
	if (pchName)
	{
		AtlasTex *pTex = *ppTex = atlasLoadTexture(pchName);
		// atlasLoadTexture return white when it can't find the texture.
		if (!stricmp(pTex->name, "white") && stricmp(pchName, "white") && !gbNoGraphics)
		{
			ErrorFilenamef(pchFilename, "Resource %s tried to load nonexistent texture %s", pchResourceName, pchName);
			return false;
		}
	}
	return true;
}

void ui_DrawWindowButtons(void)
{
	static AtlasTex *s_pMinimize;
	static AtlasTex *s_pRestore;
	static AtlasTex *s_pMinimizePressed;
	static AtlasTex *s_pRestorePressed;
	static AtlasTex *s_pMinimizeMouseOver;
	static AtlasTex *s_pRestoreMouseOver;
	static unsigned char s_chAlpha = 0;
	static bool s_bRestorePressed;
	static bool s_bMinimizePressed;

	PERFINFO_AUTO_START_FUNC_PIX();

	if (!s_pMinimize)
	{
		s_pMinimize = atlasLoadTexture("Button_Window_Minimize_Idle");
		s_pMinimizeMouseOver = atlasLoadTexture("Button_Window_Minimize_MouseOver");
		s_pMinimizePressed = atlasLoadTexture("Button_Window_Minimize_Pushed");
		s_pRestore = atlasLoadTexture("Button_Window_Restore_Idle");
		s_pRestoreMouseOver = atlasLoadTexture("Button_Window_Restore_MouseOver");
		s_pRestorePressed = atlasLoadTexture("Button_Window_Restore_Pushed");
	}
	if (gfxShouldShowRestoreButtons() && !g_ui_State.forceHideWindowButtons )
	{
		CBox RestoreBox = {
			g_ui_State.screenWidth - s_pRestore->width * g_ui_State.scale, 0,
			g_ui_State.screenWidth, s_pRestore->height * g_ui_State.scale
		};
		CBox MinimizeBox = {
			RestoreBox.lx - s_pMinimize->width * g_ui_State.scale, 0,
			RestoreBox.lx, s_pMinimize->height * g_ui_State.scale
		};
		bool bRestoreMouseOver = point_cbox_clsn(g_ui_State.mouseX, g_ui_State.mouseY, &RestoreBox);
		bool bMinimizeMouseOver = point_cbox_clsn(g_ui_State.mouseX, g_ui_State.mouseY, &MinimizeBox);
		if (bRestoreMouseOver || bMinimizeMouseOver || gfxTimeSinceWindowChanged() < 10)
			s_chAlpha = min(255, s_chAlpha + g_ui_State.timestep * 255 * 2);
		else
			s_chAlpha = max(0, s_chAlpha - g_ui_State.timestep * 255 * 2);

		if (mouseDownHit(MS_LEFT, &RestoreBox))
			s_bRestorePressed = true;
		else if (mouseDownHit(MS_LEFT, &MinimizeBox))
			s_bMinimizePressed = true;

		if (s_bRestorePressed && mouseUpHit(MS_LEFT, &RestoreBox))
			globCmdParse("window_restore");
		else if (s_bMinimizePressed && mouseUpHit(MS_LEFT, &MinimizeBox))
			globCmdParse("window_minimize");
		else if (!mouseIsDown(MS_LEFT))
		{
			s_bRestorePressed = false;
			s_bMinimizePressed = false;
		}

		if (s_chAlpha)
		{
			AtlasTex *pRestore = s_pRestore;
			AtlasTex *pMinimize = s_pMinimize;
			if (bRestoreMouseOver)
				pRestore = s_bRestorePressed ? s_pRestorePressed : s_pRestoreMouseOver;
			if (bMinimizeMouseOver)
				pMinimize = s_bMinimizePressed ? s_pMinimizePressed : s_pMinimizeMouseOver;
			display_sprite_box(pRestore, &RestoreBox, UI_INFINITE_Z, 0xFFFFFF00 | s_chAlpha);
			display_sprite_box(pMinimize, &MinimizeBox, UI_INFINITE_Z, 0xFFFFFF00 | s_chAlpha);
		}

		if (bRestoreMouseOver || bMinimizeMouseOver)
			inpHandled();
		
	}
	else
		s_chAlpha = 0;

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

void ui_DrawingDescriptionDraw( const UIDrawingDescription* drawDesc, const CBox* box, float scale, float z, char alpha, Color legacyColor, Color legacyColor2 )
{
	if( drawDesc->textureAssemblyName ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyName );
		if( texas ) {
			ui_TextureAssemblyDraw( texas, box, NULL, scale, z, z + 0.1, alpha, NULL );
		}
	} else if( drawDesc->textureAssemblyNameUsingLegacyColor ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyNameUsingLegacyColor );
		if( texas ) {
			U32 rgba = RGBAFromColor( legacyColor );
			Color4 c4 = { rgba, rgba, rgba, rgba };
			ui_TextureAssemblyDraw( texas, box, NULL, scale, z, z + 0.1, legacyColor.a, &c4 );
		}
	} else if( drawDesc->styleBorderName ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderName );
		if( styleBorder ) {
			ui_StyleBorderDrawMagic( styleBorder, box, z, scale, alpha );
		}
	} else if( drawDesc->styleBorderNameUsingLegacyColor ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderNameUsingLegacyColor );
		if( styleBorder ) {
			U32 rgba = RGBAFromColor( legacyColor );
			ui_StyleBorderDraw( styleBorder, box, rgba, rgba, z, scale, legacyColor.a );
		}
	} else if( drawDesc->styleBarName ) {
		UIStyleBar* styleBar = RefSystem_ReferentFromString( "UIStyleBar", drawDesc->styleBarName );
		if( styleBar ) {
			ui_StyleBarDraw( styleBar, box, 1, -1, -1, 0, NULL, z, alpha, true, scale, NULL );
		}
	} else if( drawDesc->textureNameUsingLegacyColor ) {
		AtlasTex* tex = atlasLoadTexture( drawDesc->textureNameUsingLegacyColor );
		if( tex ) {
			display_sprite_box( tex, box, z, RGBAFromColor( legacyColor ));
		}
	} else if( drawDesc->horzLineUsingLegacyColor ) {
		gfxDrawLineWidth( box->lx, (box->ly + box->hy) / 2, z, box->hx, (box->ly + box->hy) / 2, legacyColor, 2 );
	} else if( drawDesc->vertLineUsingLegacyColor ) {
		gfxDrawLineWidth( (box->lx + box->hx) / 2, box->ly, z, (box->lx + box->hx) / 2, box->hy, legacyColor, 2 );
	}

	if( drawDesc->overlayOutlineUsingLegacyColor2 ) {
		ui_DrawOutline( box, z + 0.001, legacyColor2, scale );
	}
}

void ui_DrawingDescriptionDrawRotated( const UIDrawingDescription* drawDesc, const CBox* box, float rot, float scale, float z, char alpha, Color legacyColor, Color legacyColor2 )
{
	if( drawDesc->textureAssemblyName ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyName );
		if( texas ) {
			float centerX;
			float centerY;
			CBoxGetCenter( box, &centerX, &centerY );
			ui_TextureAssemblyDrawRot( texas, box, centerX, centerY, rot, NULL, scale, z, z + 0.1, alpha, NULL );
		}
	} else if( drawDesc->textureAssemblyNameUsingLegacyColor ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyNameUsingLegacyColor );
		if( texas ) {
			U32 rgba = RGBAFromColor( legacyColor );
			Color4 c4 = { rgba, rgba, rgba, rgba };
			float centerX;
			float centerY;
			CBoxGetCenter( box, &centerX, &centerY );
			ui_TextureAssemblyDrawRot( texas, box, centerX, centerY, rot, NULL, scale, z, z + 0.1, legacyColor.a, &c4 );
		}
	} else if( drawDesc->styleBorderName ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderName );
		if( styleBorder ) {
			float centerX;
			float centerY;
			CBoxGetCenter( box, &centerX, &centerY );
			ui_StyleBorderDrawMagicRot( styleBorder, box, centerX, centerY, rot, z, scale, alpha );
		}
	} else {
		// Not yet implemented!
		assert( false );
	}

	if( drawDesc->overlayOutlineUsingLegacyColor2 ) {
		// Also not yet implemented!
		assert( false );
	}
}

void ui_DrawingDescriptionInnerBox( const UIDrawingDescription* drawDesc, CBox* box, float scale )
{
	box->lx += ui_DrawingDescriptionLeftSize( drawDesc ) * scale;
	box->ly += ui_DrawingDescriptionTopSize( drawDesc ) * scale;
	box->hx -= ui_DrawingDescriptionRightSize( drawDesc ) * scale;
	box->hy -= ui_DrawingDescriptionBottomSize( drawDesc ) * scale;

	if( box->hx < box->lx ) {
		box->hx = box->lx;
	}
	if( box->hy < box->ly ) {
		box->hy = box->ly;
	}
}

void ui_DrawingDescriptionInnerBoxCoords( const UIDrawingDescription* drawDesc, float* x, float* y, float* w, float* h, float scale )
{
	CBox box;

	BuildCBox( &box, *x, *y, *w, *h );
	ui_DrawingDescriptionInnerBox( drawDesc, &box, scale );
	*x = box.lx;
	*y = box.ly;
	*w = box.hx - box.lx;
	*h = box.hy - box.ly;
}

void ui_DrawingDescriptionOuterBox( const UIDrawingDescription* drawDesc, CBox* box, float scale )
{
	box->lx -= ui_DrawingDescriptionLeftSize( drawDesc ) * scale;
	box->ly -= ui_DrawingDescriptionTopSize( drawDesc ) * scale;
	box->hx += ui_DrawingDescriptionRightSize( drawDesc ) * scale;
	box->hy += ui_DrawingDescriptionBottomSize( drawDesc ) * scale;
}

void ui_DrawingDescriptionOuterBoxCoords( const UIDrawingDescription* drawDesc, float* x, float* y, float* w, float* h, float scale )
{
	CBox box;

	BuildCBox( &box, *x, *y, *w, *h );

	ui_DrawingDescriptionOuterBox( drawDesc, &box, scale );
	*x = box.lx;
	*y = box.ly;
	*w = box.hx - box.lx;
	*h = box.hy - box.ly;
}

F32 ui_DrawingDescriptionWidth( const UIDrawingDescription* drawDesc )
{
	return ui_DrawingDescriptionLeftSize( drawDesc ) + ui_DrawingDescriptionRightSize( drawDesc );
}

F32 ui_DrawingDescriptionHeight( const UIDrawingDescription* drawDesc )
{
	return ui_DrawingDescriptionTopSize( drawDesc ) + ui_DrawingDescriptionBottomSize( drawDesc );
}

F32 ui_DrawingDescriptionLeftSize( const UIDrawingDescription* drawDesc )
{
	if( drawDesc->textureAssemblyName ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyName );
		if( texas ) {
			return ui_TextureAssemblyLeftSize( texas );
		}
	} else if( drawDesc->textureAssemblyNameUsingLegacyColor ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyNameUsingLegacyColor );
		if( texas ) {
			return ui_TextureAssemblyLeftSize( texas );
		}
	} else if( drawDesc->styleBorderName ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderName );
		if( styleBorder ) {
			return ui_StyleBorderLeftSize( styleBorder );
		}
	} else if( drawDesc->styleBorderNameUsingLegacyColor ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderNameUsingLegacyColor );
		if( styleBorder ) {
			return ui_StyleBorderLeftSize( styleBorder );
		}
	} else if( drawDesc->styleBarName ) {
		UIStyleBar* styleBar = RefSystem_ReferentFromString( "UIStyleBar", drawDesc->styleBarName );
		if( styleBar ) {
			return ui_TextureAssemblyLeftSize( GET_REF( styleBar->hEmpty ));
		}
	} else if( drawDesc->textureNameUsingLegacyColor ) {
		return 0;
	} else if( drawDesc->horzLineUsingLegacyColor ) {
		return 0;
	} else if( drawDesc->vertLineUsingLegacyColor ) {
		return 1;
	}

	return 0;
}

F32 ui_DrawingDescriptionRightSize( const UIDrawingDescription* drawDesc )
{
	if( drawDesc->textureAssemblyName ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyName );
		if( texas ) {
			return ui_TextureAssemblyRightSize( texas );
		}
	} else if( drawDesc->textureAssemblyNameUsingLegacyColor ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyNameUsingLegacyColor );
		if( texas ) {
			return ui_TextureAssemblyRightSize( texas );
		}
	} else if( drawDesc->styleBorderName ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderName );
		if( styleBorder ) {
			return ui_StyleBorderRightSize( styleBorder );
		}
	} else if( drawDesc->styleBorderNameUsingLegacyColor ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderNameUsingLegacyColor );
		if( styleBorder ) {
			return ui_StyleBorderRightSize( styleBorder );
		}
	} else if( drawDesc->styleBarName ) {
		UIStyleBar* styleBar = RefSystem_ReferentFromString( "UIStyleBar", drawDesc->styleBarName );
		if( styleBar ) {
			return ui_TextureAssemblyRightSize( GET_REF( styleBar->hEmpty ));
		}
	} else if( drawDesc->textureNameUsingLegacyColor ) {
		return 0;
	}else if( drawDesc->horzLineUsingLegacyColor ) {
		return 0;
	} else if( drawDesc->vertLineUsingLegacyColor ) {
		return 1;
	}

	return 0;
}

F32 ui_DrawingDescriptionTopSize( const UIDrawingDescription* drawDesc )
{
	if( drawDesc->textureAssemblyName ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyName );
		if( texas ) {
			return ui_TextureAssemblyTopSize( texas );
		}
	} else if( drawDesc->textureAssemblyNameUsingLegacyColor ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyNameUsingLegacyColor );
		if( texas ) {
			return ui_TextureAssemblyTopSize( texas );
		}
	} else if( drawDesc->styleBorderName ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderName );
		if( styleBorder ) {
			return ui_StyleBorderTopSize( styleBorder );
		}
	} else if( drawDesc->styleBorderNameUsingLegacyColor ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderNameUsingLegacyColor );
		if( styleBorder ) {
			return ui_StyleBorderTopSize( styleBorder );
		}
	} else if( drawDesc->styleBarName ) {
		UIStyleBar* styleBar = RefSystem_ReferentFromString( "UIStyleBar", drawDesc->styleBarName );
		if( styleBar ) {
			return ui_TextureAssemblyTopSize( GET_REF( styleBar->hEmpty ));
		}
	} else if( drawDesc->textureNameUsingLegacyColor ) {
		return 0;
	} else if( drawDesc->horzLineUsingLegacyColor ) {
		return 1;
	} else if( drawDesc->vertLineUsingLegacyColor ) {
		return 0;
	}

	return 0;
}

F32 ui_DrawingDescriptionBottomSize( const UIDrawingDescription* drawDesc )
{
	if( drawDesc->textureAssemblyName ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyName );
		if( texas ) {
			return ui_TextureAssemblyBottomSize( texas );
		}
	} else if( drawDesc->textureAssemblyNameUsingLegacyColor ) {
		UITextureAssembly* texas = RefSystem_ReferentFromString( "UITextureAssembly", drawDesc->textureAssemblyNameUsingLegacyColor );
		if( texas ) {
			return ui_TextureAssemblyBottomSize( texas );
		}
	} else if( drawDesc->styleBorderName ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderName );
		if( styleBorder ) {
			return ui_StyleBorderBottomSize( styleBorder );
		}
	} else if( drawDesc->styleBorderNameUsingLegacyColor ) {
		UIStyleBorder* styleBorder = RefSystem_ReferentFromString( "UIStyleBorder", drawDesc->styleBorderNameUsingLegacyColor );
		if( styleBorder ) {
			return ui_StyleBorderBottomSize( styleBorder );
		}
	} else if( drawDesc->styleBarName ) {
		UIStyleBar* styleBar = RefSystem_ReferentFromString( "UIStyleBar", drawDesc->styleBarName );
		if( styleBar ) {
			return ui_TextureAssemblyBottomSize( GET_REF( styleBar->hEmpty ));
		}
	} else if( drawDesc->textureNameUsingLegacyColor ) {
		return 0;
	} else if( drawDesc->horzLineUsingLegacyColor ) {
		return 1;
	} else if( drawDesc->vertLineUsingLegacyColor ) {
		return 0;
	}

	return 0;
}
