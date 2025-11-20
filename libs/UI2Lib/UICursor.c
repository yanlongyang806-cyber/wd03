/***************************************************************************



***************************************************************************/

#include "Color.h"
#include "ResourceManager.h"

#include "RdrDevice.h"
#include "GraphicsLib.h"
#include "GfxCursor.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "inputMouse.h"
#include "inputLib.h"

#include "UIInternal.h"

#include "UICursor_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct UICursor 
{
	const char *pchName; AST(POOL_STRING STRUCTPARAM KEY)
	const char *pchTexture; AST(POOL_STRING RESOURCEDICT(Texture))
	const char *pchOverlay; AST(POOL_STRING RESOURCEDICT(Texture))
	U32 uiColor; AST(SUBTABLE(ColorEnum) NAME(Color) DEFAULT(0xFFFFFFFF))
	U32 uiOverlayColor; AST(SUBTABLE(ColorEnum) NAME(OverlayColor) DEFAULT(0xFFFFFFFF))
	S16 iHotspotX;
	S16 iHotspotY;
	const char *pchFilename; AST(CURRENTFILE)

	AtlasTex *pTexture; NO_AST
	AtlasTex *pOverlay; NO_AST
} UICursor;

// Turned on and off only by debugging and manual user override.
// gfxGetSoftwareCursorForce()

// Turned on and off regularly by various code things.
static bool s_bSoftwareCursorThisFrame = false;

UICursor* g_pOverrideCursor = NULL;

static bool s_bCursorDirty;

DictionaryHandle s_CursorDict;

static REF_TO(UICursor) s_hDefaultCursor;

void ui_SoftwareCursorThisFrame(void)
{
	s_bSoftwareCursorThisFrame = true;
}

AUTO_RUN;
void ui_CursorRegister(void)
{
	s_CursorDict = RefSystem_RegisterSelfDefiningDictionary("UICursor", false, parse_UICursor, true, true, NULL);
	SET_HANDLE_FROM_STRING(s_CursorDict, "Default", s_hDefaultCursor);
}

AUTO_FIXUPFUNC;
TextParserResult ui_CursorFixup(UICursor *pCursor, enumTextParserFixupType eType, void *pExtraData)
{
	if (eType == FIXUPTYPE_POST_BIN_READ || eType == FIXUPTYPE_POST_RELOAD || eType == FIXUPTYPE_POST_TEXT_READ)
	{
		UI_LOAD_TEXTURE_FOR(pCursor, Texture);
		UI_LOAD_TEXTURE_FOR(pCursor, Overlay);
	}
	return PARSERESULT_SUCCESS;
}

void ui_CursorLoad(void)
{
	resLoadResourcesFromDisk(s_CursorDict, "ui/cursors", ".cursor", NULL, RESOURCELOAD_USEOVERLAYS);
	if (!GET_REF(s_hDefaultCursor))
	{
		Errorf("Unable to find default cursor, is your core data missing?");
	}
}

void ui_SetCurrentDefaultCursorByPointer(SA_PARAM_OP_VALID UICursor *pCursor)
{
	if (pCursor)
	{
		SET_HANDLE_FROM_STRING(s_CursorDict, pCursor->pchName, s_hDefaultCursor);
	}
}

void ui_SetCurrentDefaultCursor(const char *pchName)
{
	UICursor *pCursor = RefSystem_ReferentFromString(s_CursorDict, pchName);

	ui_SetCurrentDefaultCursorByPointer(pCursor);
}

void ui_SetCursorByPointer(UICursor *pCursor)
{
	//to prevent fighting, if an override is set don't let it change again in this function
	if (pCursor && !g_pOverrideCursor)
	{
		ui_SetCursor(pCursor->pchTexture, pCursor->pchOverlay, pCursor->iHotspotX, pCursor->iHotspotY, pCursor->uiColor, pCursor->uiOverlayColor);
	}
}

void ui_SetCursorByName(const char *pchName)
{
	UICursor *pCursor = RefSystem_ReferentFromString(s_CursorDict, pchName);
	ui_SetCursorByPointer(pCursor);
}

const char *ui_GetCursorName(UICursor *pCursor)
{
	return pCursor ? pCursor->pchName : NULL;
}

const char *ui_GetCursorTexture(UICursor *pCursor)
{
	return pCursor ? pCursor->pchTexture : NULL;
}

S16 ui_GetCursorHotSpotX(UICursor *pCursor)
{
	return pCursor ? pCursor->iHotspotX : 0;
}

S16 ui_GetCursorHotSpotY(UICursor *pCursor)
{
	return pCursor ? pCursor->iHotspotY : 0;
}

void ui_SetCursor(const char *pchBase, const char *pchOverlay, int iHotX, int iHotY, U32 uiColor, U32 uiOverlayColor)
{
	if (g_ui_State.device)
	{
		UIDeviceState *device = ui_StateForDevice(g_ui_State.device);
		ui_SetCursorForDeviceState(device, pchBase, pchOverlay, iHotX, iHotY, uiColor, uiOverlayColor);
	}
}

void ui_SetCursorForDeviceState(UIDeviceState *device, const char *base, const char *overlay, int hotX, int hotY, U32 uiColor, U32 uiOverlayColor)
{
	if (g_ui_State.cursorLock)
		return;

	if (device->cursor.draggedIcon) {
		UICursor* cursor = NULL;

		if(!cursor && device->cursor.dragCursorName) {
			cursor = RefSystem_ReferentFromString(s_CursorDict, device->cursor.dragCursorName);
		}
		if(!cursor) {
			cursor = GET_REF(s_hDefaultCursor);
		}

		if( cursor ) {
			base = cursor->pchTexture;
			hotX = cursor->iHotspotX;
			hotY = cursor->iHotspotY;
		} else {
			base = "eui_pointer_arrow";
			hotX = 0;
			hotY = 0;
		}
	}
	
	if (base && *base) {
		device->cursor.base = atlasLoadTexture(base);
	} else {
		device->cursor.base = atlasLoadTexture("eui_pointer_arrow");
		hotX = 0;
		hotY = 0;
	}

	if (overlay && *overlay)
		device->cursor.overlay = atlasLoadTexture(overlay);
	else
		device->cursor.overlay = NULL;

	device->cursor.hotX = hotX;
	device->cursor.hotY = hotY;
	device->cursor.color = uiColor;
	device->cursor.overlayColor = uiOverlayColor;
}

void ui_SetCursorForDirection(UIDirection eDirection)
{
	int i;
	//If you change this struct update UISKin pchDragDirCursors to match
	static struct {
		UIDirection eDirection;
		const char *pchCursor;
	}  aCursors[] = {
		{UIBottomLeft, "Resize_TopRightBottomLeft"},
		{UIBottomRight, "Resize_TopLeftBottomRight"},
		{UITopLeft, "Resize_TopLeftBottomRight"},
		{UITopRight, "Resize_TopRightBottomLeft"},
		{UIBottom, "Resize_Vertical"},
		{UILeft, "Resize_Horizontal"},
		{UIRight, "Resize_Horizontal"},
		{UITop, "Resize_Vertical"},
		{UIAnyDirection, "Resize_All"},
	};
	for (i = 0; i<ARRAY_SIZE(aCursors); i++)
	{
		if (aCursors[i].eDirection == eDirection)
		{
			UISkin* skin = ui_GetActiveSkin();
			if(	i < UI_DRAG_DIR_CURSOR_SKIN_CNT && 
				skin->pchDragDirCursors[i] &&
				skin->pchDragDirCursors[i][0]) {
				ui_SetCursorByName(skin->pchDragDirCursors[i]);
			} else {
				ui_SetCursorByName(aCursors[i].pchCursor);
			}
			return;
		}
	}
	assertmsgf(0, "Invalid direction passed to %s", __FUNCTION__);
}

static void CursorLayoutPlace(UICursorState* pState, IVec2 out_cursorPos, IVec2 out_dragPos, F32* out_dragScale, IVec2 out_hotspotPos )
{
	setVec2( out_cursorPos, 0, 0 );
	setVec2( out_dragPos, 0, 0 );
	*out_dragScale = 1;
	setVec2( out_hotspotPos, pState->hotX, pState->hotY );

	if (pState->draggedIcon)
	{
 		if(pState->draggedIconCenter)
		{
			F32 dragScale = MIN( 1, MIN( (float)RDR_CURSOR_SIZE / pState->draggedIcon->width+pState->draggedIconX,
										 (float)RDR_CURSOR_SIZE / pState->draggedIcon->height+pState->draggedIconY ));
			setVec2( out_dragPos,
					 (RDR_CURSOR_SIZE - pState->draggedIcon->width * dragScale) / 2,
					 (RDR_CURSOR_SIZE - pState->draggedIcon->height * dragScale) / 2 );
			*out_dragScale = dragScale;
			
			setVec2( out_cursorPos, RDR_CURSOR_SIZE / 2 - pState->hotX, RDR_CURSOR_SIZE / 2 - pState->hotY );
			setVec2( out_hotspotPos, RDR_CURSOR_SIZE / 2, RDR_CURSOR_SIZE / 2 );
		}
		else
		{
			copyVec2( out_hotspotPos, out_dragPos );
			out_dragPos[0] += pState->draggedIconX;
			out_dragPos[1] += pState->draggedIconY;
			*out_dragScale = MIN( 1, MIN( (float)(RDR_CURSOR_SIZE - (out_hotspotPos[0]+pState->draggedIconX)) / pState->draggedIcon->width,
										  (float)(RDR_CURSOR_SIZE - (out_hotspotPos[1]+pState->draggedIconY)) / pState->draggedIcon->height ));
		}
	}
}

static void CursorDrawSoftware(UIDeviceState *device)
{
	IVec2 cursorPos;
	IVec2 dragPos;
	F32 dragScale;
	IVec2 hotspotPos;

	CursorLayoutPlace( &device->cursor, cursorPos, dragPos, &dragScale, hotspotPos );

	// Offset everything by MousePos.
	hotspotPos[0] -= g_ui_State.mouseX;
	hotspotPos[1] -= g_ui_State.mouseY;
	
	if (device->cursor.draggedIcon)
	{
		U32 iconColor = 0xFFFFFFFF;
		if(device->cursor.draggedIconColor)
			iconColor = device->cursor.draggedIconColor;
		display_sprite(device->cursor.draggedIcon, dragPos[0]-hotspotPos[0], dragPos[1]-hotspotPos[1], GRAPHICSLIB_Z + 1000, dragScale, dragScale, iconColor | 0xFF);
	}
	display_sprite(device->cursor.base, cursorPos[0]-hotspotPos[0], cursorPos[0]-hotspotPos[1], GRAPHICSLIB_Z + 1001, 1, 1, device->cursor.color | 0xFF);
	if (device->cursor.overlay)
		display_sprite(device->cursor.overlay, cursorPos[0]-hotspotPos[0], cursorPos[1]-hotspotPos[1], GRAPHICSLIB_Z + 1002, 1.f, 1.f, device->cursor.overlayColor | 0xFF);
}

static void UpdateHardwareCursor(RdrDevice *pDevice, UICursorState *pState, bool bUseCache)
{
	char achCursor[1000] = {0};
	sprintf(achCursor, "%s(%d,%d)%x", pState->base->name, pState->hotX, pState->hotY, pState->color);

	if( pState->overlay )
		strcatf(achCursor, ",%s,%x", pState->overlay->name, pState->overlayColor);
	if (pState->draggedIcon)
		strcatf(achCursor, ",%s", pState->draggedIcon->name);
	if (pState->draggedIcon2)
		strcatf(achCursor, ",%s", pState->draggedIcon2->name);
	if (pState->draggedIconCenter)
		strcatf(achCursor, "{C}");
	if (pState->draggedIconX != 0)
		strcatf(achCursor, "x%i", pState->draggedIconX);
	if (pState->draggedIconY != 0)
		strcatf(achCursor, "y%i", pState->draggedIconY);
	if (pState->draggedIconColor)
		strcatf(achCursor, "{0x%x}",pState->draggedIconColor);

	if (!(bUseCache && rdrSetCursorFromCache(g_ui_State.device, achCursor)))
	{
		IVec2 cursorPos;
		IVec2 dragPos;
		F32 dragScale;
		IVec2 hotspotPos;
		
		PERFINFO_AUTO_START("cursor update", 1);
		gfxLockActiveDeviceEx(true);
		PERFINFO_AUTO_START("clear", 1);
		gfxCursorClear();
		PERFINFO_AUTO_STOP_START("blits", 1);
		
		CursorLayoutPlace( pState, cursorPos, dragPos, &dragScale, hotspotPos );

		if (pState->draggedIcon)
		{
			U32 iconColor = 0xFFFFFFFF;
			if(pState->draggedIconColor)
				iconColor = pState->draggedIconColor;

			gfxCursorBlit(pState->draggedIcon, dragPos[0], dragPos[1], dragScale, colorFromRGBA(iconColor));
		}
		if (pState->draggedIcon2)
		{
			U32 iconColor = 0xFFFFFFFF;
			if(pState->draggedIconColor)
				iconColor = pState->draggedIconColor;

			gfxCursorBlit(pState->draggedIcon2, dragPos[0], dragPos[1], dragScale, colorFromRGBA(iconColor));
		}
		gfxCursorBlit(pState->base, cursorPos[0], cursorPos[1], 1.0f, colorFromRGBA(pState->color));
		if (pState->overlay)
			gfxCursorBlit(pState->overlay, cursorPos[0], cursorPos[1], 1.f, colorFromRGBA(pState->overlayColor));
		
		PERFINFO_AUTO_STOP_START("finalize", 1);
		gfxCursorSet(pDevice, achCursor, hotspotPos[0], hotspotPos[1]);
		PERFINFO_AUTO_STOP();
		gfxUnlockActiveDeviceEx(false, true, false);
		PERFINFO_AUTO_STOP();
	}
}

void ui_CursorOncePerFrameBeforeTick(void)
{
	UIDeviceState *pDevice = ui_StateForDevice(g_ui_State.device);

	if (gbNoGraphics || !pDevice || rdrIsDeviceInactive(g_ui_State.device))
		return;

	if (g_ui_State.chInputDidAnything)
	{
		if (g_ui_State.bInEditor)
			ui_SetCursor("eui_pointer_arrow", NULL, 0, 0, 0xFFFFFFFF, 0xFFFFFFFF);
		else
			ui_SetCursorByPointer(GET_REF(s_hDefaultCursor));
	}
}

void ui_CursorOncePerFrameAfterTick(void)
{
	UIDeviceState *pDevice = ui_StateForDevice(g_ui_State.device);
	bool cursorIsVisible;
	bool useSoftwareCursor;

	if (gbNoGraphics || !pDevice || rdrIsDeviceInactive(g_ui_State.device))
		return;

	PERFINFO_AUTO_START("ui_CursorOncePerFrame", 1);
	cursorIsVisible = !mouseIsLocked();
	if( inpIsInactiveWindow( inpGetActive() )) {
		useSoftwareCursor = false;
	} else {
		useSoftwareCursor = gfxGetSoftwareCursorForce() || s_bSoftwareCursorThisFrame;
	}
	
	if( cursorIsVisible ) {
		if( useSoftwareCursor ) {
			(rdrSetCursorState)(g_ui_State.device, 0, 0);
			CursorDrawSoftware(pDevice);
		} else {
			(rdrSetCursorState)(g_ui_State.device, 1, 0);
			UpdateHardwareCursor(g_ui_State.device, &pDevice->cursor, true);
		}
	} else {
		(rdrSetCursorState)( g_ui_State.device, 0, 1 );
	}
	s_bSoftwareCursorThisFrame = false;
	PERFINFO_AUTO_STOP();
}

void ui_CursorLock(void)
{
	g_ui_State.cursorLock = true;
}

void ui_CursorForceUpdate(void)
{
	UIDeviceState *pDevice = ui_StateForDevice(g_ui_State.device);
	if (g_ui_State.device)
		UpdateHardwareCursor(g_ui_State.device, &pDevice->cursor, false);
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorMode_OverrideCursor");
void exprCursorMode_OverrideCursor(const char* pchCursorName)
{
	UICursor* pCursor = RefSystem_ReferentFromString(s_CursorDict, pchCursorName);
	g_pOverrideCursor = NULL;
	ui_SetCursorByPointer(pCursor);
	g_pOverrideCursor = pCursor;
}


AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorMode_ClearOverrideCursor");
void exprCursorMode_ClearOverrideCursor()
{
	g_pOverrideCursor = NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorMode_GetCursorOverrideName");
const char* exprCursorMode_GetCursorOverrideName()
{
	return g_pOverrideCursor ? g_pOverrideCursor->pchName : NULL;
}

#include "UICursor_c_ast.c"
