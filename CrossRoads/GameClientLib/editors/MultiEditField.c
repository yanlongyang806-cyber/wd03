//
// MultiEditField.c
//

#ifndef NO_EDITORS

#include "error.h"
#include "EditorManager.h"
#include "EditorPrefs.h"
#include "EString.h"
#include "eventeditor.h"
#include "Expression.h"
#include "GameActionEditor.h"
#include "GameEvent.h"
#include "GfxClipper.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GfxTexAtlas.h"
#include "MultiEditField.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TextParser.h"
#include "TextParserInheritance.h"
#include "tokenstore.h"
#include "WorldGrid.h"
#include "EditorPreviewWindow.h"
#include "EditorSearchWindow.h"
#include "EditorManagerUtils.h"
#include "rgb_hsv.h"
#include "UIColorSlider.h"

#include "AutoGen/MultiEditField_h_ast.h"

// These are used for auto-field determination and/or editor opening
#include "CostumeCommon.h"
#include "powers.h"
#include "mission_common.h"
#include "entCritter.h"
#include "AutoGen/statemachine_h_ast.h"

#define Z_DIST_BEFORE_SELECTION   (-0.15)

#define EDITMENU_EDIT_INDEX			0
#define EDITMENU_REVERT_INDEX		1
#define EDITMENU_INHERIT_INDEX		2
#define EDITMENU_NO_INHERIT_INDEX	3
#define EDITMENU_OPEN_IN_EDITOR		4
#define EDITMENU_OPEN_FILE			5
#define EDITMENU_OPEN_FOLDER		6
#define EDITMENU_PREVIEW			7
#define EDITMENU_SEARCH_FOR_VALUE	8

static void mef_updateFieldParented(MEField *pField, UIWidget *pWidget, bool bParented);
static void mef_revertField(MEField *pField, UIWidget *pWidget);


// This is extension type for expressions
MEFieldType kMEFieldTypeEx_Expression;
MEFieldType kMEFieldTypeEx_GameActionBlock;
MEFieldType kMEFieldTypeEx_GameEvent;

// This is used to track extension types
#define MIN_EX_TYPE    100
typedef struct MEFieldExternTypeInfo {
	int id;
	MEFieldCreateFunc cbCreate;
	MEFieldDrawFunc cbDraw;
	MEFieldCompareFunc cbCompare;
	MEFieldUpdateFunc cbUpdate;
	MEFieldGetStringFunc cbGetString;
} MEFieldExternTypeInfo;
static int gNextExType = MIN_EX_TYPE;
static MEFieldExternTypeInfo **eaExternTypes = NULL;

typedef struct MEMenuInfo {
	MEField *pField;
	UIWidget *pWidget;
} MEMenuInfo;

// The static editing window
static UIWindow *pGlobalWindow = NULL;
static MEField *pGlobalWindowField = NULL;
static UIMenu *pEditMenu = NULL;
static MEMenuInfo **eaEditMenuInfos = NULL;
static bool bDisableContextMenu = false;

// This magic line is required if you run in Full Debug to avoid assert
AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static MEField *mef_alloc( 	  void *pOld, void *pNew, ParseTable *pTable, const char *pchField, void *pParent,
							  void *pRootOld, void *pRootNew, ParseTable *pRootTable, char *pcRootPath,
							  MEFieldType eType, StaticDefineInt *pEnum, void *pExtensionData,
							  const char *pchDictName, ParseTable *pDictTable, const char *pchDictField,
							  const char *pchDictDisplayField, const char *pchGlobalDictName, const char *pchAlias,  
							  bool bLabel, int arrayIndex, int *pX, int *pY, int *pWidth )
{
	MEField *pField;
	int column = -1;

	if (!pNew) {
		Errorf("Cannot create a MEField with a NULL structure pointer");
		return NULL;
	}

	if(!ParserFindColumn(pTable, pchField, &column))
	{
		Errorf("Field %s Not Found", pchField);
		return NULL;
	}

	pField = calloc(sizeof(MEField),1);
	pField->pOld = pOld;
	pField->pNew = pNew;
	pField->pParent = pParent;
	if( pParent )
	{
		int iType = StructInherit_GetOverrideType(pRootTable, pRootNew, pcRootPath);
		pField->bParented = (iType == OVERRIDE_NONE);
	}
	pField->pchFieldName = pchField;
	pField->pTable = pTable;

	pField->pRootOld = pRootOld;
	pField->pRootNew = pRootNew;
	pField->pRootTable = pRootTable;
	if (pcRootPath) {
		eaPush(&pField->eaRootPaths, strdup(pcRootPath));
	}

	pField->pEnum = pEnum;
	pField->pExtensionData = pExtensionData;
	pField->pchDictName = pchDictName;
	pField->pchGlobalDictName = pchGlobalDictName;
	pField->pDictTable = pDictTable;
	pField->pchDictField = pchDictField;
	pField->pchDictDisplayField = pchDictDisplayField;
	if (pField->pDictTable && pField->pchDictField) {
		if (!ParserFindColumn(pField->pDictTable, pField->pchDictField, &pField->iDictCol)) {
			Errorf("Field %s Not Found", pchDictField);
			return NULL;
		}
		if (!pField->pchDictDisplayField) {
			pField->pchDictDisplayField = pField->pchDictField;
		}
	}
	pField->column = column;
	pField->type = pTable[column].type;
	
	//special case... treat ints with TOK_FORMAT_FLAGS specially, since TOK_FLAGS_X no longer exists
	if (TypeIsInt(TOK_GET_TYPE(pField->type)) && TOK_GET_FORMAT_OPTIONS(pTable[column].format) == TOK_FORMAT_FLAGS)
	{
		pField->bIsIntFlags = true;
	}

	pField->eType = eType;
	pField->arrayIndex = arrayIndex;

	pField->bEditable = true;
	pField->pchLabel = pchAlias;
	pField->fScale = 1.0f;

	return pField;
}


//----------------------------------------------------------------------------------
// Draw Functions
//----------------------------------------------------------------------------------

bool mef_getBooleanValue(ParseTable *pTable, int iCol, void *pData)
{
	if (TOK_GET_TYPE(pTable[iCol].type) == TOK_BIT) {
		return TokenStoreGetBit(pTable, iCol, pData, 0, 0);
	} else {
		return TokenStoreGetU8(pTable, iCol, pData, 0, 0);
	}
}

void mef_setBooleanValue(ParseTable *pTable, int iCol, void *pData, bool bValue)
{
	if (TOK_GET_TYPE(pTable[iCol].type) == TOK_BIT) {
		TokenStoreSetBit(pTable, iCol, pData, 0, bValue, 0, 0);
	} else {
		TokenStoreSetU8(pTable, iCol, pData, 0, bValue, 0, 0);
	}
}

void mef_getColorValue(ParseTable *pTable, int iCol, void *pData, Vec4 vColor)
{
	int iArraySize = TokenStoreGetFixedArraySize(pTable, iCol);
	if (iArraySize == 3) {
		vColor[0] = TokenStoreGetF32(pTable, iCol, pData, 0, NULL);
		vColor[1] = TokenStoreGetF32(pTable, iCol, pData, 1, NULL);
		vColor[2] = TokenStoreGetF32(pTable, iCol, pData, 2, NULL);
		vColor[3] = 0;
	} else if (iArraySize == 4) {
		vColor[0] = TokenStoreGetF32(pTable, iCol, pData, 0, NULL);
		vColor[1] = TokenStoreGetF32(pTable, iCol, pData, 1, NULL);
		vColor[2] = TokenStoreGetF32(pTable, iCol, pData, 2, NULL);
		vColor[3] = TokenStoreGetF32(pTable, iCol, pData, 3, NULL);
	} else {
		assert(false);
	}
}

void mef_setColorValue(ParseTable *pTable, int iCol, void *pData, Vec4 vColor)
{
	int iArraySize = TokenStoreGetFixedArraySize(pTable, iCol);
	if (iArraySize == 3) {
		TokenStoreSetF32(pTable, iCol, pData, 0, vColor[0], NULL, NULL);
		TokenStoreSetF32(pTable, iCol, pData, 1, vColor[1], NULL, NULL);
		TokenStoreSetF32(pTable, iCol, pData, 2, vColor[2], NULL, NULL);
	} else if (iArraySize == 4) {
		TokenStoreSetF32(pTable, iCol, pData, 0, vColor[0], NULL, NULL);
		TokenStoreSetF32(pTable, iCol, pData, 1, vColor[1], NULL, NULL);
		TokenStoreSetF32(pTable, iCol, pData, 2, vColor[2], NULL, NULL);
		TokenStoreSetF32(pTable, iCol, pData, 3, vColor[3], NULL, NULL);
	} else {
		assert(false);
	}
}

int mef_structGetFieldAtOffset(ParseTable tpi[], size_t offset)
{
	int i;
	FORALL_PARSETABLE(tpi, i)
	{
		if (TOK_GET_TYPE(tpi[i].type) != TOK_IGNORE
			&& TOK_GET_TYPE(tpi[i].type) != TOK_START 
			&& TOK_GET_TYPE(tpi[i].type) != TOK_END
			&& tpi[i].storeoffset == offset)
		{
			return i;
		}
	}
	return -1;
}

void MEDrawField(MEField *pField, UI_MY_ARGS, F32 z, Color *pBackgroundColor, UISkin *pSkin, bool bTinted)
{
	CBox box;

	// Draw appropriate background color
	if (pSkin) {
		if (pField->bDirty) {
			if (pField->pParent && pField->bParented) {
				AtlasTex *pTex = (g_ui_Tex.white);
				F32 angle = fatan2(h,w);
				Color c;
				BuildCBox(&box, x, y, w, h);
				clipperPushRestrict(&box);
				c = ColorLerp(pBackgroundColor ? *pBackgroundColor : pSkin->entry[1], pSkin->entry[3], 0.5);
				display_sprite_rotated_ex(pTex, NULL, x, y, z + Z_DIST_BEFORE_SELECTION, 2*fsqrt(w*w + h*h)/pTex->width, h/pTex->height, RGBAFromColor(c), angle, 0);
				c = ColorLerp(pBackgroundColor ? *pBackgroundColor : pSkin->entry[1], pSkin->entry[4], 0.5);
				display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION-0.01, RGBAFromColor(c));
				clipperPop();
			} else {
				Color c = ColorLerp(pBackgroundColor ? *pBackgroundColor : pSkin->entry[1], pSkin->entry[3], 0.5);
				BuildCBox(&box, x, y, w, h);
				display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
			}
		} else if (pField->pParent && pField->bParented) {
			Color c = ColorLerp(pBackgroundColor ? *pBackgroundColor : pSkin->entry[1], pSkin->entry[4], 0.5);
			BuildCBox(&box, x, y, w, h);
			display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
		}
		else if (bTinted) {
			BuildCBox(&box, x, y, w, h);
			display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(*pBackgroundColor));
		}
	}

	// Build a clipping box that keeps text away from the edges of the space provided
	w -= 3;
	BuildCBox(&box, x, y, w, h);
	clipperPushRestrict(&box);
	x += 3; w -=3;

	if (pField->eType >= MIN_EX_TYPE) {
		eaExternTypes[pField->eType - MIN_EX_TYPE]->cbDraw(pField, x, y, w, h, scale, z, pBackgroundColor, pSkin);
	} else {
		switch( pField->eType )
		{
			xcase kMEFieldType_Check:
			case kMEFieldType_BooleanCombo:
			{
				// CheckBox requires special logic
				bool bValue = mef_getBooleanValue(pField->pTable, pField->column, pField->pNew);
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", bValue ? "TRUE" : "FALSE");
			}
			xcase kMEFieldType_Texture:
			{
				// Texture requires special logic
				const char *pcTexName = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
				AtlasTex *pTex;
				if (pcTexName) {
					pTex = atlasLoadTexture(pcTexName);
					if (pTex) {
						CBox texbox = { x-3, y, x+h-3, y+h };
						Color borderColor;
						if (pSkin) {
							borderColor = pSkin->thinBorder[0];
						} else {
							borderColor = ColorBlack;
						}
						ui_DrawOutline(&texbox, z, borderColor, scale);
						CBoxAlter(&texbox, CBAT_SHRINK, CBAD_ALL, 1);
						display_sprite_box(pTex, &texbox, z, 0xffffffff);
					}
					gfxfont_Printf(x + h, y + h/2, z, scale, scale, CENTER_Y, "%s", pcTexName);
				}
			}
			xcase kMEFieldType_Color:
			{
				Vec4 vColor;
				mef_getColorValue(pField->pTable, pField->column, pField->pNew, vColor);
				{
					U32 color = ((int)(vColor[0]*255 + 0.5) << 24) + 
						((int)(vColor[1]*255 + 0.5) << 16) + 
						((int)(vColor[2]*255 + 0.5) << 8) + 
						(255 << 0);
					CBox texbox = { x-3, y, x+h-3, y+h };
					Color borderColor;
					if (pSkin) {
						borderColor = pSkin->thinBorder[0];
					} else {
						borderColor = ColorBlack;
				}
				ui_DrawOutline(&texbox, z, borderColor, scale);
				CBoxAlter(&texbox, CBAT_SHRINK, CBAD_ALL, 1);
				display_sprite_box((g_ui_Tex.white), &texbox, z, color);
				}
				gfxfont_Printf(x + h, y + h/2, z, scale, scale, CENTER_Y, "Color");
			}
			xcase kMEFieldType_DisplayMessage:
			{
				const DisplayMessage *pMsg = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, 0);
				if (pMsg && pMsg->pEditorCopy && pMsg->pEditorCopy->pcDefaultString) {
					gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pMsg->pEditorCopy->pcDefaultString);
				}
			}
			xcase kMEFieldType_Message:
			{
				const Message *pMsg = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, 0);
				if (pMsg && pMsg->pcDefaultString) {
					gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pMsg->pcDefaultString);
				}
			}
			xdefault:
			{
				if ((pField->eType == kMEFieldType_FlagCombo) && (TOK_GET_TYPE(pField->type) == TOK_STRUCT_X)) {
					void *pStruct;

					if((pField->type & TOK_EARRAY))
					{
						ParseTable *pStructTable = NULL;
						int column;
						StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
						column = mef_structGetFieldAtOffset(pStructTable, offsetof(MERefArrayStruct, hRef));
						if (pStructTable 
							&& (column >= 0)
							&& (TOK_GET_TYPE(pStructTable[column].type) == TOK_REFERENCE_X))
						{
							// An array on substruct containing a reference requires a bit of trickery to get to 
							// display because text parser doesn't do it nice
							MERefArrayStruct ***peaRefs = (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
							char *achValue = NULL;
							int i;

							if (peaRefs && eaSize(peaRefs)) {
								estrCopy2(&achValue, RefSystem_StringFromHandle(&(*peaRefs)[0]->hRef.__handle_INTERNAL));
								for(i=1; i<eaSize(peaRefs); i++) {
									if((*peaRefs)[i]) { //Hack to fix an error
										estrAppend2(&achValue, ", ");
										estrAppend2(&achValue, RefSystem_StringFromHandle(&(*peaRefs)[i]->hRef.__handle_INTERNAL));
									}
								}
								gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", achValue);
							}

							estrDestroy(&achValue);
						}
					}

					else
					{
						// An int array inside a substruct requires a bit of trickery to get to display because
						// the substruct may not exist
						pStruct = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
						if (pStruct) {
							ParseTable *pStructTable;
							char *estr = NULL;
							StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
							if(	FieldWriteText(pStructTable, 2, pStruct, 0, &estr, 0) ) {
								if (estr) {
									gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", estr);
								}
							}
							estrDestroy(&estr);
						}
					}
					
				} else if (((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry)) && 
						   (TOK_GET_TYPE(pField->type) == TOK_STRUCT_X) && (pField->type & TOK_EARRAY)) {
					ParseTable *pStructTable = NULL;
					StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
					if (pStructTable && (TOK_GET_TYPE(pStructTable[1].type) == TOK_REFERENCE_X)) {
						// An array on substruct containing a reference requires a bit of trickery to get to 
						// display because text parser doesn't do it nice
						MERefArrayStruct ***peaRefs = (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
						char *achValue = NULL;
						int i;

						if (peaRefs && eaSize(peaRefs)) {
							estrCopy2(&achValue, RefSystem_StringFromHandle(&(*peaRefs)[0]->hRef.__handle_INTERNAL));
							for(i=1; i<eaSize(peaRefs); i++) {
								if((*peaRefs)[i]) { //Hack to fix an error
									estrAppend2(&achValue, " ");
									estrAppend2(&achValue, RefSystem_StringFromHandle(&(*peaRefs)[i]->hRef.__handle_INTERNAL));
								}
							}
							gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", achValue);
						}

						estrDestroy(&achValue);
					} else if (pStructTable && (TOK_GET_TYPE(pStructTable[1].type) == TOK_STRING_X)) {
						// An array on substruct containing a string requires a bit of trickery to get to 
						// display because text parser doesn't do it nice
						MEArrayWithStringStruct ***peaStrings = (MEArrayWithStringStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
						char *achValue = NULL;
						int i;

						if (peaStrings && eaSize(peaStrings)) {
							estrCopy2(&achValue, (*peaStrings)[0]->pcString);
							for(i=1; i<eaSize(peaStrings); i++) {
								if((*peaStrings)[i]) { //Hack to fix an error
									estrAppend2(&achValue, " ");
									estrAppend2(&achValue, (*peaStrings)[0]->pcString);
								}
							}
							gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", achValue);
						}

						estrDestroy(&achValue);
					}
				} else if (pField->type & TOK_EARRAY) {
					// An earray has to be iterated to get the right string since textparser doesn't do it right
					int n = TokenStoreGetNumElems(pField->pTable, pField->column, pField->pNew, NULL);
					int i;
					char *estr = NULL;
					for(i=0; i<n; ++i) {
						if (FieldWriteText(pField->pTable, pField->column, pField->pNew, i, &estr, 0)) {
							if (i < n-1) {
								estrConcat(&estr, ", ", 2);
							}
						}
					}
					if (estr) {
						gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", estr);
					}
					estrDestroy(&estr);
				} else if (pField->fScale != 1.0f && TOK_GET_TYPE(pField->type) == TOK_F32_X){
					// A float with a scale value needs to take the stored value, and apply the scaled value
					F32 fValue = TokenStoreGetF32(pField->pTable, pField->column, pField->pNew, 1, 0);

					fValue *= pField->fScale;

					gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%f", fValue);
				} else {
					char *estr = NULL;
					// All primitive fields can use generic logic
					if(	FieldWriteText(pField->pTable, pField->column, pField->pNew, 0, &estr, 0) ) {
						if (estr) {
							gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", estr);
						}
					}
					estrDestroy(&estr);
				}
			}
		}
	}

	// Cache the scale
	pField->fLastScale = scale;

	// Pop the clipping box
	clipperPop();
}


//----------------------------------------------------------------------------------
// Right Click Menu Functions
//----------------------------------------------------------------------------------


static void mef_UIEditFields(UIWidget *pWidget, void *pUserData)
{
	int i;

	if (!eaEditMenuInfos || !eaSize(&eaEditMenuInfos)) {
		return;
	}

	// Let first field take the lead, and make all other fields into its siblings
	eaEditMenuInfos[0]->pField->eaSiblings = NULL;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=1; --i) { // Note that this goes to 1 instead of 0 on purpose
		eaPush(&eaEditMenuInfos[0]->pField->eaSiblings, eaEditMenuInfos[i]->pField);
	}

	MEFieldOpenEditor(eaEditMenuInfos[0]->pField, NULL);
}

static void mef_UIRevertFields(UIWidget *pWidget, void *pUserData)
{
	int i;
	if (UIYes != ui_ModalDialog("Revert?", "Are you sure you want to revert these fields?", ColorBlack, UIYes | UINo)) {
		return;
	}
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		mef_revertField(eaEditMenuInfos[i]->pField, eaEditMenuInfos[i]->pWidget);
	}
}

static void mef_UIInheritFields(UIWidget *pWidget, void *pUserData)
{
	int i;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		mef_updateFieldParented(eaEditMenuInfos[i]->pField, eaEditMenuInfos[i]->pWidget, true);
	}
}

static void mef_UINoInheritFields(UIWidget *pWidget, void *pUserData)
{
	int i;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		mef_updateFieldParented(eaEditMenuInfos[i]->pField, eaEditMenuInfos[i]->pWidget, false);
	}
}

static void mef_UIOpenEMEditor(UIWidget *pWidget, void *pUserData)
{
	int i;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		MEFieldOpenEMEditor(eaEditMenuInfos[i]->pField);
	}
}

static void mef_UIOpenFile(UIWidget *pWidget, void *pUserData)
{
	int i;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		MEFieldOpenFile(eaEditMenuInfos[i]->pField);
	}
}

static void mef_UIOpenFolder(UIWidget *pWidget, void *pUserData)
{
	int i;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		MEFieldOpenFolder(eaEditMenuInfos[i]->pField);
	}
}

static void mef_UIPreviewItem(UIWidget *pWidget, void *pUserData)
{
	int i;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		MEFieldPreview(eaEditMenuInfos[i]->pField);
	}
}

static void mef_UIValueSearchItem(UIWidget *pWidget, void *pUserData)
{
	int i;
	for(i=eaSize(&eaEditMenuInfos)-1; i>=0; --i) {
		MEFieldValueSearch(eaEditMenuInfos[i]->pField);
	}
}

static void mef_createEditMenu(void)
{
	if (pEditMenu) {
		return;
	}
	pEditMenu = ui_MenuCreate(NULL);
	ui_MenuAppendItems(pEditMenu,
		ui_MenuItemCreate("Edit",UIMenuCallback, mef_UIEditFields, NULL, NULL),
		ui_MenuItemCreate("Revert",UIMenuCallback, mef_UIRevertFields, NULL, NULL),
		ui_MenuItemCreate("Inherit from Parent",UIMenuCallback, mef_UIInheritFields, NULL, NULL),
		ui_MenuItemCreate("Don't Inherit from Parent",UIMenuCallback, mef_UINoInheritFields, NULL, NULL),
		ui_MenuItemCreate("Open in Editor",UIMenuCallback, mef_UIOpenEMEditor, NULL, NULL),
		ui_MenuItemCreate("Open File",UIMenuCallback, mef_UIOpenFile, NULL, NULL),
		ui_MenuItemCreate("Open Folder",UIMenuCallback, mef_UIOpenFolder, NULL, NULL),
		ui_MenuItemCreate("Preview",UIMenuCallback, mef_UIPreviewItem, NULL, NULL),
		ui_MenuItemCreate("Search for Value",UIMenuCallback, mef_UIValueSearchItem, NULL, NULL),
		NULL);
}

void mef_displayMenu(UIAnyWidget *pWidget, MEField *pField)
{
	int i;
	MEMenuInfo *pInfo;

	if (bDisableContextMenu) {
		return;
	}

	mef_createEditMenu();

	// Clear previous menu data
	for(i=eaSize(&eaEditMenuInfos)-1; i >= 0; --i) {
		free(eaEditMenuInfos[i]);
	}
	eaClear(&eaEditMenuInfos);

	// Put this field in the menu
	pInfo = calloc(1, sizeof(MEMenuInfo));
	pInfo->pField = pField;
	pInfo->pWidget = pWidget;
	eaPush(&eaEditMenuInfos, pInfo);

	pEditMenu->items[EDITMENU_EDIT_INDEX]->active = (pField->bEditable && (pField->pUIWidget == pWidget));
	pEditMenu->items[EDITMENU_REVERT_INDEX]->active = pField->bDirty && !pField->bNotRevertable;
	pEditMenu->items[EDITMENU_INHERIT_INDEX]->active = pField->pParent && !pField->bParented && !pField->bNotParentable;
	pEditMenu->items[EDITMENU_NO_INHERIT_INDEX]->active = pField->pParent && pField->bParented;
	pEditMenu->items[EDITMENU_OPEN_IN_EDITOR]->active = MEFieldCanOpenEMEditor(pField);
	pEditMenu->items[EDITMENU_OPEN_FILE]->active = MEFieldCanPreview(pField);
	pEditMenu->items[EDITMENU_OPEN_FOLDER]->active = MEFieldCanPreview(pField);
	pEditMenu->items[EDITMENU_PREVIEW]->active = MEFieldCanPreview(pField);
	pEditMenu->items[EDITMENU_SEARCH_FOR_VALUE]->active = MEFieldCanValueSearch(pField);

	pEditMenu->widget.scale = (pField->fLastScale > 0) ? pField->fLastScale : 1.0;
	ui_MenuPopupAtCursor(pEditMenu);
}

static void mef_displayMultiMenu(MEField **eaFields) 
{
	bool bHasParented = 0, bHasNoParented = 0, bHasDirty = 0, bHasEditable = 0, bHasTypeMismatch = 0;
	int i;
	MEField *pEditField = NULL;
	MEMenuInfo *pInfo;

	if (bDisableContextMenu || !eaSize(&eaFields)) {
		return;
	}

	mef_createEditMenu();

	// Clear previous menu info
	for(i=eaSize(&eaEditMenuInfos)-1; i >= 0; --i) {
		free(eaEditMenuInfos[i]);
	}
	eaClear(&eaEditMenuInfos);

	// Scan the selection
	for(i=eaSize(&eaFields)-1; i>=0; --i) {
		if (eaFields[i]) {
			pInfo = calloc(1, sizeof(MEMenuInfo));
			pInfo->pField = eaFields[i];
			pInfo->pWidget = NULL;
			eaPush(&eaEditMenuInfos, pInfo);

			if (eaFields[i]->bParented) {
				bHasParented = true;
			} else if (eaFields[i]->pParent && !eaFields[i]->bNotParentable) {
				bHasNoParented = true;
			}
			if (eaFields[i]->bDirty) {
				bHasDirty = true;
			}
			if (eaFields[i]->bEditable) {
				bHasEditable = true;
			}
			if (!pEditField) {
				pEditField = eaFields[i];
			} else if (!MEFieldHasCompatibleTypes(eaFields[i],pEditField)) {
				bHasTypeMismatch = true;
			}
			if (eaFields[i]->bNotGroupEditable) {
				bHasTypeMismatch = true;
			}
		}
	}

	// Setup edit menu
	pEditMenu->items[EDITMENU_EDIT_INDEX]->active = bHasEditable && !bHasTypeMismatch;
	pEditMenu->items[EDITMENU_REVERT_INDEX]->active = bHasDirty;
	pEditMenu->items[EDITMENU_INHERIT_INDEX]->active = bHasNoParented;
	pEditMenu->items[EDITMENU_NO_INHERIT_INDEX]->active = bHasParented;

	pEditMenu->widget.scale = (pEditField && (pEditField->fLastScale > 0)) ? pEditField->fLastScale : 1.0;
	ui_MenuPopupAtCursor(pEditMenu);
}


void MEFieldSetDisableContextMenu(bool bDisable)
{
	bDisableContextMenu = bDisable;
}


//----------------------------------------------------------------------------------
// Update Functions
//----------------------------------------------------------------------------------

static const char* mef_GetPickerText(UIButton *pButton)
{
	const char *pchText;
	MEField *pField = pButton->clickedData;
	pchText = ui_ButtonGetText(pButton);
	if(stricmp_safe(pchText, "None") == 0) {
		return "";
	}
	if(stricmp_safe(pField->pchEMPickerName, "Object Picker") == 0) {
		GroupDef *pDef = objectLibraryGetGroupDefByName(pchText, false);
		char pchBuf[256];
		if(!pDef)
			return "";
		sprintf(pchBuf, "%d", pDef->name_uid);
		return allocAddString(pchBuf);
	}
	return pchText;
}

static void mef_SetPickerText(UIButton *pButton, const char *pchText)
{
	MEField *pField = pButton->clickedData;
	if(pchText && pchText[0]) {
		if(stricmp_safe(pField->pchEMPickerName, "Object Picker") == 0) {
			GroupDef *pDef = objectLibraryGetGroupDefByName(pchText, false);
			if(pDef) {
				ui_ButtonSetText(pButton, pDef->name_str);
				return;
			}
		} else {
			ui_ButtonSetText(pButton, pchText);
			return;
		}
	}
	ui_ButtonSetText(pButton, "None");
}

static void mef_updateDirty(MEField *pField, UIWidget *pWidget, bool bDirty)
{
	// Update color and dirty tracking
	if (bDirty!=pField->bDirty) {
		pField->bDirty = bDirty;
		if (pField->pUIWidget) {
			ui_SetChanged(pField->pUIWidget, bDirty);
		}
		if (pWidget && (pWidget != pField->pUIWidget)) {
			ui_SetChanged(pWidget, bDirty);
		}
	}
}


static void mef_updateParented(MEField *pField, UIWidget *pWidget, bool bParented, bool bDoUpdate)
{
	// Update color
	if (bParented!=pField->bParented) {
		if (pField->preChangedCallback && !(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			return; // Cancel action if pre-change rejected
		}
		pField->bParented = bParented;
		if (pField->pUIWidget) {
			ui_SetInherited(pField->pUIWidget, bParented);
		}
		if (pWidget && (pWidget != pField->pUIWidget)) {
			ui_SetInherited(pWidget, bParented);
		}
	}

	if (bDoUpdate) {
		// Mark for changes on next update
		pField->bParentUpdate = bParented;
		pField->bUpdate = !bParented;
	}
}


static int mef_compareFields(MEField *pField, void *pLeft, void *pRight)
{
	if(!pLeft || !pRight)
	{
		return -1;
	}

	if (pField->eType >= MIN_EX_TYPE) {
		return eaExternTypes[pField->eType - MIN_EX_TYPE]->cbCompare(pField, pLeft, pRight);
	} else {
		// Most types just do a token compare
		// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
		return TokenCompare(pField->pTable, pField->column, pLeft, pRight, 0, TOK_USEROPTIONBIT_1);
	}
}


void mef_checkIfDirty(MEField *pField, UIAnyWidget *pWidget)
{
	bool bDirty = false;

	// Mark as dirty if no old
	if (!pField->pOld) {
		bDirty = true;
	}

	// Mark as dirty if value different
	if (pField->pParent && pField->bParented) {
		if (mef_compareFields(pField, pField->pNew, pField->pParent) != 0) {
			// Differs from parent, so need to clear parenting and check against old for dirty
			mef_updateParented(pField, pWidget, 0, 0);
			if (!bDirty && (mef_compareFields(pField, pField->pNew, pField->pOld) != 0)) {
				bDirty = true;
			}
		}
	} else {
		if (!bDirty && (mef_compareFields(pField, pField->pNew, pField->pOld) != 0)) {
			bDirty = true;
		}
	}

	// Mark as dirty if parenting changed
	if (!bDirty && pField->pParent && pField->pRootOld) {
		if (StructInherit_GetParentName(pField->pRootTable, pField->pRootOld)) {
			int i;
			bool bOrigParented = true;

			// Old has a parent, so compare to its parenting
			for(i=eaSize(&pField->eaRootPaths)-1; i>=0; --i) {
				if (StructInherit_GetOverrideType(pField->pRootTable, pField->pRootOld, pField->eaRootPaths[i]) != OVERRIDE_NONE) {
					bOrigParented = false;
					break;
				}
			}
			if (pField->bParented != bOrigParented) {
				bDirty = true;
			}
		} else {
			// Old has no parent
			if (pField->bParented) {
				bDirty = true;
			}
		}
	}
	
	mef_updateDirty(pField, pWidget, bDirty);
}


static bool mef_validatePooledString(UIAnyWidget *widget, unsigned char **oldString, unsigned char **newString, void *unused)
{
	if (newString && *newString)
	{
		const char *st = allocAddString(*newString);

		// Note that this is case sensitive on purpose
		if (strcmp(st, *newString) != 0) {
			estrClear(newString);
			estrConcatString(newString, st, (int)strlen(st));
		}
	}
	return true;
}


static void mef_sliderFieldChanged(UISlider *pSlider, bool bFinished, MEField *pField)
{
	// Get the current state
	F32 fScale = pField->fScale;
	F32 fTarget = TokenStoreGetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL) * fScale;
	F32 fOrig = pField->pOld ? TokenStoreGetF32(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), NULL) * fScale : fTarget;
	F32 fNewTarget;
	F32 f = pSlider ? ui_SliderGetValue(pSlider) : 0;
	bool bUpdateControl = false;

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if ((fTarget != f) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		f = pField->pOld ? fOrig : fTarget;
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			F32 fParent = TokenStoreGetF32(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL) * fScale;
			f = pField->pParent ? fParent : fTarget;
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		f = pField->pNew ? fTarget : 0;
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the control and value
	if (bUpdateControl && pSlider) {
		ui_SliderSetValue(pSlider, f);

		if (pSlider != pField->pUISlider) {
			ui_SliderSetValue(pField->pUISlider, f);
		}
	}

	TokenStoreSetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), f / fScale, NULL, NULL);

	// Check for Changes
	fNewTarget = TokenStoreGetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL) * fScale;

	// Check for dirty
	mef_checkIfDirty(pField, pSlider);
	
	// Call changed callback if present
	if (pField->changedCallback && (bFinished || (fTarget != fNewTarget))) {
		pField->changedCallback(pField, bFinished, pField->pChangeUserData);
	}
}


static int mef_textFieldUpdateStringArray(MEField *pField, UIWidget *pWidget, const char *pchText, bool bFinished)
{
	// Get the current state
	char ***ppchTarget = (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
	char ***ppchOrig = pField->pOld ? (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pOld, NULL) : NULL;

	char *pchEntry, *pchFind;
	char *achCompare = estrCreateFromStr("");
	int i;
	bool bUpdateControl = false;

	const char* pchIndexSeparator = (pField->pchIndexSeparator ? pField->pchIndexSeparator : " ");

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Calc original compare string
		if(ppchOrig && (eaSize(ppchOrig) > 0)) {
			for(i=0; i<eaSize(ppchOrig); i++) {
				if ((*ppchOrig)[i]) { // Skip until first non-NULL
					estrCopy2(&achCompare, (*ppchOrig)[i]);
					break;
				}
			}
			for(++i; i<eaSize(ppchOrig); i++) {
				if((*ppchOrig)[i]) { // Deal with NULL entries
					estrAppend2(&achCompare, pchIndexSeparator);
					estrAppend2(&achCompare, (*ppchOrig)[i]);
				}
			}
		}
		// Only call pre-change callback if value changed
		if (pchText && (strcmp(pchText, achCompare) != 0) &&
			!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Put incoming value in the buffer to be used if not overwritten
	estrCopy2(&achCompare, pchText);

	// Determine the proper value
	if (pField->bRevert) {
		estrClear(&achCompare);
		if(ppchOrig && (eaSize(ppchOrig) > 0)) {
			for(i=0; i<eaSize(ppchOrig); i++) {
				if ((*ppchOrig)[i]) { // Skip until first non-NULL
					estrCopy2(&achCompare, (*ppchOrig)[i]);
					break;
				}
			}
			for(++i; i<eaSize(ppchOrig); i++) {
				if((*ppchOrig)[i]) { // Deal with NULL entries
					estrAppend2(&achCompare, pchIndexSeparator);
					estrAppend2(&achCompare, (*ppchOrig)[i]);
				}
			}
		}
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		estrClear(&achCompare);
		if(ppchTarget && (eaSize(ppchTarget) > 0)) {
			for(i=0; i<eaSize(ppchTarget); i++) {
				if ((*ppchTarget)[i]) { // Skip until first non-NULL
					estrCopy2(&achCompare, (*ppchTarget)[i]);
					break;
				}
			}
			for(++i; i<eaSize(ppchTarget); i++) {
				if((*ppchTarget)[i]) { // Deal with NULL entries
					estrAppend2(&achCompare, pchIndexSeparator);
					estrAppend2(&achCompare, (*ppchTarget)[i]);
				}
			}
		}
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			char ***ppchParent = (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pParent, NULL);

			estrClear(&achCompare);
			if (ppchParent && (eaSize(ppchParent) > 0)) {
				for(i=0; i<eaSize(ppchParent); i++) {
					if ((*ppchParent)[i]) { // Skip to first non-NULL
						estrCopy2(&achCompare, (*ppchParent)[i]);
						break;
					}
				}
				for(++i; i<eaSize(ppchParent); i++) {
					if((*ppchParent)[i]) { // Skip NULL entries
						estrAppend2(&achCompare, pchIndexSeparator);
						estrAppend2(&achCompare, (*ppchParent)[i]);
					}
				}
			}
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	if (bUpdateControl && pWidget) {
		if (pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry)) {
				if (strcmp(ui_TextEntryGetText(pField->pUIText), achCompare) != 0) {
					ui_TextEntrySetText(pField->pUIText, achCompare);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText(pField->pUITextArea), achCompare) != 0) {
					ui_TextAreaSetText(pField->pUITextArea, achCompare);
				}
			}
		}
		if (pWidget != pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry)) {
				if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), achCompare) != 0) {
					ui_TextEntrySetText((UITextEntry*)pWidget, achCompare);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText((UITextArea*)pWidget), achCompare) != 0) {
					ui_TextAreaSetText((UITextArea*)pWidget, achCompare);
				}
			}
		}
	}

	// Update the value and control
	if (ppchTarget) {
		TokenStoreDestroyEArray(pField->pTable, &pField->pTable[pField->column], pField->column, pField->pNew, NULL);
	}
	TokenStoreMakeLocalEArray(pField->pTable, pField->column, pField->pNew, NULL);
	ppchTarget = (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);

	pchEntry = achCompare;
	pchFind = strstr(achCompare, pchIndexSeparator);
	while (pchFind) {
		pchFind[0] = '\0';
		eaPush(ppchTarget, StructAllocString(pchEntry));
		pchFind+=strlen(pchIndexSeparator);
		pchEntry = pchFind;
		pchFind = strstr(pchEntry,pchIndexSeparator);
	}
	if (pchEntry && pchEntry[0] != '\0') {
		eaPush(ppchTarget, StructAllocString(pchEntry));
	}

	estrDestroy(&achCompare);

	// Check for changes
	// TODO: Actually check this
	return 1;
}


static int mef_textFieldUpdateString(MEField *pField, UIWidget *pWidget, const char *pchText, bool bFinished)
{
	// Get the current state
	const char *pchTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), 0);
	const char *pchOrig	= pField->pOld ? TokenStoreGetString(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), 0) : NULL;
	const char *pchNewTarget;
	bool bUpdateControl = false;
	bool bNeedsFree = false;
	int retVal;

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pchTarget && (pchText[0] != '\0')) || (pchTarget && (strcmp(pchTarget,pchText) != 0))) && 
			!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pchText = pField->pOld ? (pchOrig ? pchOrig : "") : (pchTarget ? pchTarget : "");
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		pchText = pField->pNew ? (pchTarget ? pchTarget : "") : "";
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			const char *pchParent = TokenStoreGetString(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL);
			pchText = pField->pParent && pchParent ? pchParent : "";
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	// Update the control and value
	if (bUpdateControl && pWidget) {
		if (pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (strcmp(ui_TextEntryGetText(pField->pUIText), pchText) != 0) {
					ui_TextEntrySetText(pField->pUIText, pchText);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText(pField->pUITextArea), pchText) != 0) {
					ui_TextAreaSetText(pField->pUITextArea, pchText);
				}
			}
		}
		if (pWidget != pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), pchText) != 0) {
					ui_TextEntrySetText((UITextEntry*)pWidget, pchText);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText((UITextArea*)pWidget), pchText) != 0) {
					ui_TextAreaSetText((UITextArea*)pWidget, pchText);
				}
			}
		}
	}

	if (pchTarget != pchText)
	{
		if (pchTarget)
		{
			pchTarget = strdup(pchTarget);
			bNeedsFree = true;
		}
		TokenStoreSetString(pField->pTable, pField->column, pField->pNew,MAX(0, pField->arrayIndex),pchText,0,0,0,0);
	}

	// Check for changes
	pchNewTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), 0);
	retVal = 0; // init to 'no change'
	if (pchTarget && pchNewTarget)
	{
		ANALYSIS_ASSUME(pchTarget != NULL && pchNewTarget != NULL);
		if (strcmp(pchTarget,pchNewTarget) != 0)
		{
			retVal = 1; // strings different, changed
		}
	}

	if (!retVal)
	{
		if ((pchTarget && !pchNewTarget) || (!pchTarget && pchNewTarget))
		{
			retVal = 1; // one or the other string isn't available, changed
		}
	}

	if (bNeedsFree)
	{
		free((char*)pchTarget);
	}

	return retVal;
}


static int mef_textFieldUpdateF32(MEField *pField, UIWidget *pWidget, F32 f, bool bFinished)
{
	// Get the current state
	F32 fScale = pField->fScale;
	F32 fTarget = TokenStoreGetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL) * fScale;
	F32 fOrig = pField->pOld ? TokenStoreGetF32(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), NULL) * fScale : fTarget;
	F32 fNewTarget;
	bool bUpdateControl = false;

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if ((fTarget != f) && 
			!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		f = pField->pOld ? fOrig : fTarget;
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			F32 fParent = TokenStoreGetF32(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL) * fScale;
			f = pField->pParent ? fParent : fTarget;
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		f = pField->pNew ? fTarget : 0;
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the control and value
	if (bUpdateControl && pWidget) {
		char *pchF = NULL;

		estrStackCreate(&pchF);
		estrPrintf(&pchF,"%g",f);

		if (pField->eType == kMEFieldType_SliderText) {
			if (pField->pUISliderText) {
				if (strcmp(ui_SliderTextEntryGetText(pField->pUISliderText), pchF) != 0) {
					ui_SliderTextEntrySetTextAndCallback(pField->pUISliderText, pchF);
				}
			}
			if (pWidget != pField->pUIWidget) {
				if (strcmp(ui_SliderTextEntryGetText((UISliderTextEntry*)pWidget), pchF) != 0) {
					ui_SliderTextEntrySetTextAndCallback((UISliderTextEntry*)pWidget, pchF);
				}
			}
		} else if (pField->eType == kMEFieldType_Spinner) {
			if (pField->pUISpinner) {
				if (ui_SpinnerEntryGetValue(pField->pUISpinner) != f)
					ui_SpinnerEntrySetValue(pField->pUISpinner, f);
			}
			if (pWidget != pField->pUIWidget) {
				if (ui_SpinnerEntryGetValue((UISpinnerEntry*)pWidget) != f)
					ui_SpinnerEntrySetValue((UISpinnerEntry*)pWidget, f);
			}
		} else if (pField->eType == kMEFieldType_MultiSpinner) {
			if (pField->pUIMultiSpinner) {
				if (ui_MultiSpinnerEntryGetIdxValue(pField->pUIMultiSpinner, pField->arrayIndex) != f)
					ui_MultiSpinnerEntrySetIdxValue(pField->pUIMultiSpinner, f, pField->arrayIndex);
			}
			if (pWidget != pField->pUIWidget) {
				if (ui_MultiSpinnerEntryGetIdxValue((UIMultiSpinnerEntry*)pWidget, pField->arrayIndex) != f)
					ui_MultiSpinnerEntrySetIdxValue((UIMultiSpinnerEntry*)pWidget, f, pField->arrayIndex);
			}
		} else {
			if (pField->pUIText) {
				if (strcmp(ui_TextEntryGetText(pField->pUIText), pchF) != 0) {
					ui_TextEntrySetText(pField->pUIText, pchF);
				}
			}
			if (pWidget != pField->pUIWidget) {
				if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), pchF) != 0) {
					ui_TextEntrySetText((UITextEntry*)pWidget, pchF);
				}
			}
		}
		estrDestroy(&pchF);
	}

	TokenStoreSetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), f / fScale, NULL, NULL);

	// Check for Changes
	fNewTarget = TokenStoreGetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL) * fScale;
	if (fTarget != fNewTarget) {
		return 1;
	} else {
		return 0;
	}
}

static bool earray32Compare(int **a, int **b)
{
	int i;

	if(ea32Size(a) != ea32Size(b))
		return false;

	for(i=0;i<ea32Size(a);i++)
	{
		if((*a)[i] != (*b)[i])
			return false;
	}

	return true;
}

static void mef_ConvertIntArrayToString(MEField *pField, int **iArray, char **ppEStrOut)
{
	int i;

	for(i=0;i<ea32Size(iArray);i++)
	{
		if(pField->pEnum)
			estrAppend2(ppEStrOut,(char*)StaticDefineIntRevLookup(pField->pEnum,(*iArray)[i]));
		else
			estrAppendEscapedf(ppEStrOut,"%d",(*iArray)[i]);

		if(i<ea32Size(iArray)-1)
			estrAppend2(ppEStrOut,", ");
	}
}

static void mef_ConvertStringToIntArray(MEField *pField, char *pchValue, int **iArrayOut)
{
	char *pchString;
	char *pchFind;
	char *pchOrigString;

	ea32Clear(iArrayOut);

	pchString = strdup(pchValue);
	pchOrigString = pchString;

	pchFind = strchr(pchString,',');

	do 
	{
		int iValue;

		if(pchFind)
		{
			pchFind[0] = 0;
			pchFind++;
			while(pchFind[0] == ' ')
				pchFind++;
		}

		if(pField->pEnum)
			iValue = StaticDefineIntGetInt(pField->pEnum,pchString);
		else
			iValue = atoi(pchString);

		if(iValue!=-1)
			ea32Push(iArrayOut,iValue);
		
		pchString = pchFind;

		if(pchString)
			pchFind = strchr(pchString,',');
	} while (pchString);

	free(pchOrigString);
}

static int mef_textFieldUpdateIntArray(MEField *pField, UIWidget *pWidget, char *pchValue, bool bFinished)
{
	int *iValue = NULL;
	int **iNewValue = NULL;
	int *iTarget = NULL;
	int **iOrig = pField->pOld ? TokenStoreGetEArrayInt(pField->pTable, pField->column, pField->pOld,NULL) : NULL;
	bool bUpdateControl = false;
	int i;

	ea32Copy(&iTarget,TokenStoreGetEArrayInt(pField->pTable, pField->column, pField->pNew, NULL));

	mef_ConvertStringToIntArray(pField,pchValue,&iValue);

	// Handle pre-change refusal
	if(pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if(earray32Compare(&iTarget,&iValue) == false &&
			!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
				pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		ea32Copy(&iValue,pField->pOld ? iOrig : &iTarget);
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if(pField->bParentUpdate) {
		if(pField->pParent) {
			int **iParent = TokenStoreGetEArrayInt(pField->pTable, pField->column, pField->pParent, NULL);
			ea32Copy(&iValue,iParent);
			bUpdateControl = true;
		}
	}
	if(pField->bUpdate) {
		if(pField->pNew)
			ea32Copy(&iValue,&iTarget);
		else
			ea32Clear(&iValue);
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the control and the value
	if(bUpdateControl && pWidget)
	{
		char *pchFinalValue = NULL;
		estrStackCreate(&pchFinalValue);

		mef_ConvertIntArrayToString(pField,&iValue,&pchFinalValue);

		if (pField->pUIText) {
			if (strcmp(ui_TextEntryGetText(pField->pUIText), pchFinalValue) != 0) {
				ui_TextEntrySetText(pField->pUIText, pchFinalValue);
			}
		}
		if (pWidget != pField->pUIWidget) {
			if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), pchFinalValue) != 0) {
				ui_TextEntrySetText((UITextEntry*)pWidget, pchFinalValue);
			}
		}

		estrDestroy(&pchFinalValue);
	}

	TokenStoreSetEArraySize(pField->pTable, pField->column, pField->pNew, ea32Size(&iValue),NULL);
	for(i=0;i<ea32Size(&iValue);i++)
	{
		TokenStoreSetIntAuto(pField->pTable, pField->column, pField->pNew, i, iValue[i], NULL, NULL);
	}

	// Check for changes
	iNewValue = TokenStoreGetEArrayInt(pField->pTable, pField->column, pField->pNew, NULL);
	if (!earray32Compare(&iTarget,iNewValue)) {
		return 1;
	} else {
		return 0;
	}

}

static int mef_textFieldUpdateInt(MEField *pField, UIWidget *pWidget, int i, bool bFinished)
{
	// Get the current state
	int iTarget = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL);
	int iOrig = pField->pOld ? TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), NULL) : -1;
	int iNewTarget;
	bool bUpdateControl = false;

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if ((iTarget != i) && 
			!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		i = pField->pOld ? iOrig : iTarget;
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			int iParent = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL);
			i = pField->pParent ? iParent : iTarget;
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		i = pField->pNew ? iTarget : 0;
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the control and value
	if (bUpdateControl && pWidget) {
		char *pchI = NULL;
		const char *pchEnumStr;
		estrStackCreate(&pchI);

		if(pField->pEnum && (pchEnumStr = StaticDefineIntRevLookup(pField->pEnum, i))) {
			estrPrintf(&pchI,"%s",pchEnumStr);
		} else {
			estrPrintf(&pchI,"%d",i);
		}

		if (pField->eType == kMEFieldType_SliderText) {
			if (pField->pUISliderText) {
				if (strcmp(ui_SliderTextEntryGetText(pField->pUISliderText), pchI) != 0) {
					ui_SliderTextEntrySetTextAndCallback(pField->pUISliderText, pchI);
				}
			}
			if (pWidget != pField->pUIWidget) {
				if (strcmp(ui_SliderTextEntryGetText((UISliderTextEntry*)pWidget), pchI) != 0) {
					ui_SliderTextEntrySetTextAndCallback((UISliderTextEntry*)pWidget, pchI);
				}
			}
		} else if (pField->eType == kMEFieldType_Spinner) {
			if (pField->pUISpinner) {
				if (ui_SpinnerEntryGetValue(pField->pUISpinner) != i)
					ui_SpinnerEntrySetValue(pField->pUISpinner, i);
			}
			if (pWidget != pField->pUIWidget) {
				if (ui_SpinnerEntryGetValue((UISpinnerEntry*)pWidget) != i)
					ui_SpinnerEntrySetValue((UISpinnerEntry*)pWidget, i);
			}
		} else {
			if (pField->pUIText) {
				if (strcmp(ui_TextEntryGetText(pField->pUIText), pchI) != 0) {
					ui_TextEntrySetText(pField->pUIText, pchI);
				}
			}
			if (pWidget != pField->pUIWidget) {
				if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), pchI) != 0) {
					ui_TextEntrySetText((UITextEntry*)pWidget, pchI);
				}
			}
		}
		estrDestroy(&pchI);
	}

	TokenStoreSetIntAuto(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), i, NULL, NULL);

	// Check for changes
	iNewTarget = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL);
	if (iTarget != iNewTarget) {
		return 1;
	} else {
		return 0;
	}
}


static int mef_textFieldUpdateMultiVal(MEField *pField, UIWidget *pWidget, const char *pchText, bool bFinished)
{
	// Get the current state
	MultiVal *pTarget = TokenStoreGetMultiVal(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), 0);
	MultiVal *pOrig	= pField->pOld ? TokenStoreGetMultiVal(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), 0) : NULL;
	MultiVal *pNewTarget = NULL;
	bool bUpdateControl = false;
	bool bAllocated = false;
	bool bChanged = false;

	assert(pTarget);

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (MultiValIsString(pTarget)) {
			const char *pcTargetText = MultiValGetString(pTarget, NULL);
			if (((!pcTargetText && (pchText[0] != '\0')) || (pcTargetText && (strcmp(pcTargetText,pchText) != 0))) && 
				!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
				pField->bRevert = true;
			}
		} else if (pTarget->type == MULTI_INT) {
			int iTarget = MultiValGetInt(pTarget, NULL);
			int iNew = atoi(pchText);
			if ((iTarget != iNew) && 
				!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
				pField->bRevert = true;
			}
		} else if (pTarget->type == MULTI_FLOAT) {
			int fTarget = MultiValGetFloat(pTarget, NULL);
			int fNew = atoi(pchText);
			if ((fTarget != fNew) && 
				!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
				pField->bRevert = true;
			}
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		// Only revert if original was of the same type
		if (pOrig && (pOrig->type == pTarget->type)) {
			pNewTarget = pOrig;
		}
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		pNewTarget = pTarget;
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			MultiVal *pParent = TokenStoreGetMultiVal(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL);
			pNewTarget = pParent;
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	if (!pNewTarget) {
		pNewTarget = MultiValCreate();
		pNewTarget->type = pTarget->type;
		if (MultiValIsString(pTarget)) {
			MultiValSetString(pNewTarget, pchText);
		} else if (pTarget->type == MULTI_INT) {
			int iValue = atoi(pchText);
			MultiValSetInt(pNewTarget, iValue);
		} else if (pTarget->type == MULTI_FLOAT) {
			int fValue = atof(pchText);
			MultiValSetFloat(pNewTarget, fValue);
		} else {
			Alertf("Unsupported multi-val type");
		}
		bAllocated = true;
	}

	// Update the control and value
	if (bUpdateControl && pWidget) {
		if (pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (MultiValIsString(pTarget)) {
					if (strcmp(ui_TextEntryGetText(pField->pUIText), MultiValGetString(pNewTarget, NULL)) != 0) {
						ui_TextEntrySetText(pField->pUIText, MultiValGetString(pNewTarget, NULL));
					}
				} else if (pTarget->type == MULTI_INT) {
					int iValue = atoi(ui_TextEntryGetText(pField->pUIText));
					if (iValue != MultiValGetInt(pNewTarget, NULL)) {
						char *pchI = NULL;
						estrStackCreate(&pchI);
						estrPrintf(&pchI,"%"FORM_LL"d",MultiValGetInt(pNewTarget,NULL));
						ui_TextEntrySetText(pField->pUIText, pchI);
						estrDestroy(&pchI);
					}
				} else if (pTarget->type == MULTI_FLOAT) {
					int fValue = atof(ui_TextEntryGetText(pField->pUIText));
					if (fValue != MultiValGetFloat(pNewTarget, NULL)) {
						char *pchF = NULL;
						estrStackCreate(&pchF);
						estrPrintf(&pchF,"%g",MultiValGetFloat(pNewTarget,NULL));
						ui_TextEntrySetText(pField->pUIText, pchF);
						estrDestroy(&pchF);
					}
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (MultiValIsString(pTarget)) {
					if (strcmp(ui_TextAreaGetText(pField->pUITextArea), MultiValGetString(pNewTarget, NULL)) != 0) {
						ui_TextAreaSetText(pField->pUITextArea, MultiValGetString(pNewTarget, NULL));
					}
				} else if (pTarget->type == MULTI_INT) {
					int iValue = atoi(ui_TextAreaGetText(pField->pUITextArea));
					if (iValue != MultiValGetInt(pNewTarget, NULL)) {
						char *pchI = NULL;
						estrStackCreate(&pchI);
						estrPrintf(&pchI,"%"FORM_LL"d",MultiValGetInt(pNewTarget,NULL));
						ui_TextAreaSetText(pField->pUITextArea, pchI);
						estrDestroy(&pchI);
					}
				} else if (pTarget->type == MULTI_FLOAT) {
					int fValue = atof(ui_TextAreaGetText(pField->pUITextArea));
					if (fValue != MultiValGetFloat(pNewTarget, NULL)) {
						char *pchF = NULL;
						estrStackCreate(&pchF);
						estrPrintf(&pchF,"%g",MultiValGetFloat(pNewTarget,NULL));
						ui_TextAreaSetText(pField->pUITextArea, pchF);
						estrDestroy(&pchF);
					}
				}
			}
		}
		if (pWidget != pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (MultiValIsString(pTarget)) {
					if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), MultiValGetString(pNewTarget, NULL)) != 0) {
						ui_TextEntrySetText((UITextEntry*)pWidget, MultiValGetString(pNewTarget, NULL));
					}
				} else if (pTarget->type == MULTI_INT) {
					int iValue = atoi(ui_TextEntryGetText((UITextEntry*)pWidget));
					if (iValue != MultiValGetInt(pNewTarget, NULL)) {
						char *pchI = NULL;
						estrStackCreate(&pchI);
						estrPrintf(&pchI,"%"FORM_LL"d",MultiValGetInt(pNewTarget,NULL));
						ui_TextEntrySetText((UITextEntry*)pWidget, pchI);
						estrDestroy(&pchI);
					}
				} else if (pTarget->type == MULTI_FLOAT) {
					int fValue = atof(ui_TextEntryGetText((UITextEntry*)pWidget));
					if (fValue != MultiValGetFloat(pNewTarget, NULL)) {
						char *pchF = NULL;
						estrStackCreate(&pchF);
						estrPrintf(&pchF,"%g",MultiValGetFloat(pNewTarget,NULL));
						ui_TextEntrySetText((UITextEntry*)pWidget, pchF);
						estrDestroy(&pchF);
					}
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (MultiValIsString(pTarget)) {
					if (strcmp(ui_TextAreaGetText((UITextArea*)pWidget), MultiValGetString(pNewTarget, NULL)) != 0) {
						ui_TextAreaSetText((UITextArea*)pWidget, MultiValGetString(pNewTarget, NULL));
					}
				} else if (pTarget->type == MULTI_INT) {
					int iValue = atoi(ui_TextAreaGetText((UITextArea*)pWidget));
					if (iValue != MultiValGetInt(pNewTarget, NULL)) {
						char *pchI = NULL;
						estrStackCreate(&pchI);
						estrPrintf(&pchI,"%"FORM_LL"d",MultiValGetInt(pNewTarget,NULL));
						ui_TextAreaSetText((UITextArea*)pWidget, pchI);
						estrDestroy(&pchI);
					}
				} else if (pTarget->type == MULTI_FLOAT) {
					int fValue = atof(ui_TextAreaGetText((UITextArea*)pWidget));
					if (fValue != MultiValGetFloat(pNewTarget, NULL)) {
						char *pchF = NULL;
						estrStackCreate(&pchF);
						estrPrintf(&pchF,"%g",MultiValGetFloat(pNewTarget,NULL));
						ui_TextAreaSetText((UITextArea*)pWidget, pchF);
						estrDestroy(&pchF);
					}
				}
			}
		}
	}

	if (pTarget != pNewTarget) {
		// Check for change before setting value
		if (MultiValIsString(pTarget)) {
			if (strcmp(MultiValGetString(pTarget, NULL), MultiValGetString(pNewTarget, NULL)) != 0) {
				bChanged = true;
			}
		} else if (pTarget->type == MULTI_INT) {
			if (MultiValGetInt(pTarget, NULL) != MultiValGetInt(pNewTarget, NULL)) {
				bChanged = true;
			}
		} else if (pTarget->type == MULTI_FLOAT) {
			if (MultiValGetFloat(pTarget, NULL) != MultiValGetFloat(pTarget, NULL)) {
				bChanged = true;
			}
		}

		// Set the value
		if (bChanged) {
			TokenStoreSetMultiVal(pField->pTable, pField->column, pField->pNew,MAX(0, pField->arrayIndex),pNewTarget,0,0,0,0);
		}
		if (bAllocated) {
			MultiValDestroy(pNewTarget);
		}

		return bChanged;
	} else {
		// No change
		return 0;
	}
}


static int mef_textFieldUpdateReference(MEField *pField, UIAnyWidget *pWidget, const char *pchText, bool bFinished)
{
	// Get the current state
	const char *pchTarget = NULL;
	const char *pchOrig = NULL;
	const char *pchNewTarget = NULL;
	char *pchTemp = NULL;
	bool bUpdateControl = false;
	ReferenceHandle *pTarget = TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	ReferenceHandle *pOrig = pField->pOld ? TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	ReferenceHandle *pNewTarget;

	if ((pField->pNew != NULL) && RefSystem_IsHandleActive(pTarget)) {
		pchTarget = RefSystem_StringFromHandle(pTarget);
	}
	if ((pField->pOld != NULL) && RefSystem_IsHandleActive(pOrig)) {
		pchOrig = RefSystem_StringFromHandle(pOrig);
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pchTarget && (pchText[0] != '\0')) || (pchTarget && (strcmp(pchTarget,pchText) != 0))) && 
			!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pchText = pchOrig ? pchOrig : "";
		bUpdateControl = true;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			ReferenceHandle *pParent = TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pParent, 0, NULL);
			const char *pchParent = NULL;
			if (pField->pParent != NULL && RefSystem_IsHandleActive(pParent)) {
				pchParent = RefSystem_StringFromHandle(pParent);
			}
			pchText = pField->pParent ? (pchParent ? pchParent : "") : pchTarget ? pchTarget : "";
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pchText = pchTarget ? pchTarget : "";
		bUpdateControl = true;
		pField->bUpdate = false;
	}
	bUpdateControl = true;

	if (bUpdateControl && pWidget) {
		if (pField->pUIWidget) {
			if(pField->eType == kMEFieldType_EMPicker) {
				if (strcmp(mef_GetPickerText(pField->pUIButton), pchText) != 0) {
					mef_SetPickerText(pField->pUIButton, pchText);
				}
			} else {
				if (strcmp(ui_TextEntryGetText(pField->pUIText), pchText) != 0) {
					ui_TextEntrySetText(pField->pUIText, pchText);
				}
			}
		}
		if (pWidget != pField->pUIWidget) {
			if(pField->eType == kMEFieldType_EMPicker) {
				if (strcmp(mef_GetPickerText(pWidget), pchText) != 0) {
					mef_SetPickerText(pWidget, pchText);
				}
			} else {
				if (strcmp(ui_TextEntryGetText(pWidget), pchText) != 0) {
					ui_TextEntrySetText(pWidget, pchText);
				}
			}
		}
	}

	// Update the state and control
	if (strlen(pchText)==0) {
		if (pchTarget) {
			RefSystem_RemoveHandle(pTarget);
		}
	} else {
		if (pchTarget) {
			RefSystem_RemoveHandle(pTarget);
		}

		pchTemp = strchr(pchText,'\\');
		while (pchTemp) {
			pchTemp[0] = '/';
			pchTemp = strchr(pchTemp,'\\');
		}
		
		TokenStoreSetRef(pField->pTable, pField->column, pField->pNew, 0, pchText, NULL, NULL);
	}

	// Check to see if the value changed
	pNewTarget = TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	if (RefSystem_IsHandleActive(pNewTarget)) {
		pchNewTarget = RefSystem_StringFromHandle(pNewTarget);
	}
	if ((pchTarget && pchNewTarget && (strcmp(pchTarget,pchNewTarget) != 0)) ||
	    (pchTarget && !pchNewTarget) || (!pchTarget && pchNewTarget)) {
		return 1;
	} else {
		return 0;
	}
}


static int mef_textFieldUpdateRefStructArray(MEField *pField, UIWidget *pWidget, const char *pchText, bool bFinished)
{
	// Get the current state
	MERefArrayStruct ***peaTarget = (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
	MERefArrayStruct ***peaOrig = pField->pOld ? (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pOld, NULL) : NULL;

	char *pchEntry, *pchFind;
	char *achCompare = estrCreateFromStr("");
	int i;
	bool bUpdateControl = false;
	MERefArrayStruct *pRefStruct;
	ParseTable *pStructTable;

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate &&
		!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
		pField->bRevert = true;
	}

	// Put incoming value in the buffer to be used if not overwritten
	estrCopy2(&achCompare, pchText);

	// Determine the proper value
	if (pField->bRevert) {
		estrClear(&achCompare);
		if(peaOrig && (eaSize(peaOrig) > 0)) {
			estrCopy2(&achCompare, RefSystem_StringFromHandle(&(*peaOrig)[0]->hRef.__handle_INTERNAL));
			for(i=1; i<eaSize(peaOrig); i++) {
				if((*peaOrig)[i]) { //Hack to fix an error
					estrAppend2(&achCompare, " ");
					estrAppend2(&achCompare, RefSystem_StringFromHandle(&(*peaOrig)[i]->hRef.__handle_INTERNAL));
				}
			}
		}
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		estrClear(&achCompare);
		if(peaTarget && (eaSize(peaTarget) > 0)) {
			estrCopy2(&achCompare, RefSystem_StringFromHandle(&(*peaTarget)[0]->hRef.__handle_INTERNAL));
			for(i=1; i<eaSize(peaTarget); i++) {
				if((*peaTarget)[i]) {
					estrAppend2(&achCompare, " ");
					estrAppend2(&achCompare, RefSystem_StringFromHandle(&(*peaTarget)[i]->hRef.__handle_INTERNAL));
				}
			}
		}
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			MERefArrayStruct ***peaParent = pField->pParent ? (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pParent, NULL) : NULL;

			estrClear(&achCompare);
			if (peaParent && (eaSize(peaParent) > 0)) {
				estrCopy2(&achCompare, RefSystem_StringFromHandle(&(*peaParent)[0]->hRef.__handle_INTERNAL));
				for(i=1; i<eaSize(peaParent); i++) {
					if((*peaParent)[i]) {
						estrAppend2(&achCompare, " ");
						estrAppend2(&achCompare, RefSystem_StringFromHandle(&(*peaParent)[i]->hRef.__handle_INTERNAL));
					}
				}
			}
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	if (bUpdateControl && pWidget) {
		if (pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (strcmp(ui_TextEntryGetText(pField->pUIText), achCompare) != 0) {
					ui_TextEntrySetText(pField->pUIText, achCompare);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText(pField->pUITextArea), achCompare) != 0) {
					ui_TextAreaSetText(pField->pUITextArea, achCompare);
				}
			} else if (pField->eType == kMEFieldType_EMPicker) {
				if (strcmp(mef_GetPickerText(pField->pUIButton), achCompare) != 0) {
					mef_SetPickerText(pField->pUIButton, achCompare);
				}
			}
		}
		if (pWidget != pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), achCompare) != 0) {
					ui_TextEntrySetText((UITextEntry*)pWidget, achCompare);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText((UITextArea*)pWidget), achCompare) != 0) {
					ui_TextAreaSetText((UITextArea*)pWidget, achCompare);
				}
			} else if (pField->eType == kMEFieldType_EMPicker) {
				if (strcmp(mef_GetPickerText((UIButton*)pWidget), achCompare) != 0) {
					mef_SetPickerText((UIButton*)pWidget, achCompare);
				}
			}
		}
	}

	// Update the value and control
	if (peaTarget) {
		TokenStoreDestroyEArray(pField->pTable, &pField->pTable[pField->column], pField->column, pField->pNew, NULL);
	}
	TokenStoreMakeLocalEArray(pField->pTable, pField->column, pField->pNew, NULL);
	peaTarget = (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);

	pchEntry = achCompare;
	pchFind = strchr(achCompare, ' ');
	while (pchFind) {
		pchFind[0] = '\0';

		// Create the struct
		StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
		pRefStruct = StructCreateVoid(pStructTable);
		if (pField->pchDictName)
			SET_HANDLE_FROM_STRING(pField->pchDictName, pchEntry, pRefStruct->hRef);
		else if (pField->pchGlobalDictName)
			SET_HANDLE_FROM_STRING(pField->pchGlobalDictName, pchEntry, pRefStruct->hRef);
		eaPush(peaTarget, pRefStruct);

		pchFind++;
		pchEntry = pchFind;
		pchFind = strchr(pchEntry,' ');
	}
	if (pchEntry && pchEntry[0] != '\0') {
		// Create the struct
		StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
		pRefStruct = StructCreateVoid(pStructTable);
		if (pField->pchDictName)
			SET_HANDLE_FROM_STRING(pField->pchDictName, pchEntry, pRefStruct->hRef);
		else if (pField->pchGlobalDictName)
			SET_HANDLE_FROM_STRING(pField->pchGlobalDictName, pchEntry, pRefStruct->hRef);
		eaPush(peaTarget, pRefStruct);
	}

	estrDestroy(&achCompare);

	// Check for changes
	// TODO: Actually check this
	return 1;
}


static int mef_textFieldUpdateStringStructArray(MEField *pField, UIWidget *pWidget, const char *pchText, bool bFinished)
{
	// Get the current state
	MEArrayWithStringStruct ***peaTarget = (MEArrayWithStringStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
	MEArrayWithStringStruct ***peaOrig = pField->pOld ? (MEArrayWithStringStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pOld, NULL) : NULL;

	char *pchEntry, *pchFind;
	char *achCompare = estrCreateFromStr("");
	int i;
	bool bUpdateControl = false;
	MEArrayWithStringStruct *pStringStruct;
	ParseTable *pStructTable;

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate &&
		!(*pField->preChangedCallback)(pField, bFinished, pField->pPreChangeUserData)) {
		pField->bRevert = true;
	}

	// Put incoming value in the buffer to be used if not overwritten
	estrCopy2(&achCompare, pchText);

	// Determine the proper value
	if (pField->bRevert) {
		estrClear(&achCompare);
		if(peaOrig && (eaSize(peaOrig) > 0)) {
			estrCopy2(&achCompare, (*peaOrig)[0]->pcString);
			for(i=1; i<eaSize(peaOrig); i++) {
				if((*peaOrig)[i]) { //Hack to fix an error
					estrAppend2(&achCompare, " ");
					estrAppend2(&achCompare, (*peaOrig)[i]->pcString);
				}
			}
		}
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		estrClear(&achCompare);
		if(peaTarget && (eaSize(peaTarget) > 0)) {
			estrCopy2(&achCompare, (*peaTarget)[0]->pcString);
			for(i=1; i<eaSize(peaTarget); i++) {
				if((*peaTarget)[i]) {
					estrAppend2(&achCompare, " ");
					estrAppend2(&achCompare, (*peaTarget)[i]->pcString);
				}
			}
		}
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			MEArrayWithStringStruct ***peaParent = pField->pParent ? (MEArrayWithStringStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pParent, NULL) : NULL;

			estrClear(&achCompare);
			if (peaParent && (eaSize(peaParent) > 0)) {
				estrCopy2(&achCompare, (*peaParent)[0]->pcString);
				for(i=1; i<eaSize(peaParent); i++) {
					if((*peaParent)[i]) {
						estrAppend2(&achCompare, " ");
						estrAppend2(&achCompare, (*peaParent)[i]->pcString);
					}
				}
			}
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	if (bUpdateControl && pWidget) {
		if (pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (strcmp(ui_TextEntryGetText(pField->pUIText), achCompare) != 0) {
					ui_TextEntrySetText(pField->pUIText, achCompare);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText(pField->pUITextArea), achCompare) != 0) {
					ui_TextAreaSetText(pField->pUITextArea, achCompare);
				}
			}
		}
		if (pWidget != pField->pUIWidget) {
			if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
				if (strcmp(ui_TextEntryGetText((UITextEntry*)pWidget), achCompare) != 0) {
					ui_TextEntrySetText((UITextEntry*)pWidget, achCompare);
				}
			} else if (pField->eType == kMEFieldType_TextArea) {
				if (strcmp(ui_TextAreaGetText((UITextArea*)pWidget), achCompare) != 0) {
					ui_TextAreaSetText((UITextArea*)pWidget, achCompare);
				}
			}
		}
	}

	// Update the value and control
	if (peaTarget) {
		TokenStoreDestroyEArray(pField->pTable, &pField->pTable[pField->column], pField->column, pField->pNew, NULL);
	}
	TokenStoreMakeLocalEArray(pField->pTable, pField->column, pField->pNew, NULL);
	peaTarget = (MEArrayWithStringStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);

	pchEntry = achCompare;
	pchFind = strchr(achCompare, ' ');
	while (pchFind) {
		pchFind[0] = '\0';

		// Create the struct
		StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
		pStringStruct = StructCreateVoid(pStructTable);
		pStringStruct->pcString = StructAllocString(pchEntry);
		eaPush(peaTarget, pStringStruct);

		pchFind++;
		pchEntry = pchFind;
		pchFind = strchr(pchEntry,' ');
	}
	if (pchEntry && pchEntry[0] != '\0') {
		// Create the struct
		StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
		pStringStruct = StructCreateVoid(pStructTable);
		pStringStruct->pcString = StructAllocString(pchEntry);
		eaPush(peaTarget, pStringStruct);
	}

	estrDestroy(&achCompare);

	// Check for changes
	// TODO: Actually check this
	return 1;
}


static void mef_textFieldChangedInternal(UIWidget *pWidget, MEField *pField, int bFinished)
{
	char *pText;
	int fieldType;
	bool bChanged = 0;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_textFieldChangedInternal(pWidget, pField->eaSiblings[i], bFinished);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	if (!pWidget) {
		pText = "";
	} else if ((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry) || (pField->eType == kMEFieldType_MultiText) || (pField->eType == kMEFieldType_SMFTextEntry)) {
		pText = (char*)ui_TextEntryGetText((UITextEntry*)pWidget);
	} else if (pField->eType == kMEFieldType_TextArea) {
		pText = (char*)ui_TextAreaGetText((UITextArea*)pWidget);
	} else if (pField->eType == kMEFieldType_EMPicker) {
		pText = (char*)mef_GetPickerText((UIButton*)pWidget);
	} else {
		// Unexpected type
		return;
	}

	// Update the fields based on the type
	fieldType = TOK_GET_TYPE(pField->type);
	if ((fieldType == TOK_STRING_X) && (pField->type & TOK_EARRAY) && pField->arrayIndex == -1) {
		bChanged = mef_textFieldUpdateStringArray(pField, pWidget, pText, bFinished);
	} else if (fieldType == TOK_STRING_X) {
		bChanged = mef_textFieldUpdateString(pField, pWidget, pText, bFinished);
	} else if (fieldType == TOK_F32_X) {
		bChanged = mef_textFieldUpdateF32(pField, pWidget, atof(pText), bFinished);
	} else if ((fieldType == TOK_INT_X) && (pField->type & TOK_EARRAY)) {
		bChanged = mef_textFieldUpdateIntArray(pField, pWidget, pText, bFinished);
	} else if ((fieldType == TOK_INT_X) || (fieldType == TOK_U8_X) || (fieldType == TOK_BIT)) {
		if ((pField->eType == kMEFieldType_TextEntry || pField->eType == kMEFieldType_ValidatedTextEntry)
			&& pField->pEnum
			&& pWidget
			&& ((UITextEntry*)pWidget)->cb)
		{
			int iSelected = ui_ComboBoxGetSelectedEnum(((UITextEntry*)pWidget)->cb);
			if (iSelected >= 0) {
				bChanged = mef_textFieldUpdateInt(pField, pWidget, iSelected, bFinished);
			} else {
				bChanged = mef_textFieldUpdateInt(pField, pWidget, atoi(pText), bFinished);
			}
		} else {
			bChanged = mef_textFieldUpdateInt(pField, pWidget, atoi(pText), bFinished);
		}
	} else if (fieldType == TOK_REFERENCE_X) {
		bChanged = mef_textFieldUpdateReference(pField, (UITextEntry*)pWidget, pText, bFinished);
	} else if (fieldType == TOK_CURRENTFILE_X) {
		bChanged = mef_textFieldUpdateString(pField, pWidget, pText, bFinished);
//totally identical to INT case
//	} else if (fieldType == TOK_FLAGS_X) {
//		bChanged = mef_textFieldUpdateInt(pField, pWidget, atoi(pText), bFinished);
	} else if (fieldType == TOK_MULTIVAL_X) {
		bChanged = mef_textFieldUpdateMultiVal(pField, pWidget, pText, bFinished);
	} else if ((fieldType == TOK_STRUCT_X) && (pField->type & TOK_EARRAY)) {
		ParseTable *pStructTable = NULL;
		StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
		if (pStructTable && (TOK_GET_TYPE(pStructTable[1].type) == TOK_REFERENCE_X)) {
			bChanged = mef_textFieldUpdateRefStructArray(pField, pWidget, pText, bFinished);
		} else if (pStructTable && (TOK_GET_TYPE(pStructTable[1].type) == TOK_STRING_X)) {
			bChanged = mef_textFieldUpdateStringStructArray(pField, pWidget, pText, bFinished);
		} else {
			Errorf("Error in %s\n\nTOK_GET_TYPE %d not found !\n\n(%s)", __FILE__, TOK_GET_TYPE(pField->type), pText);
		}
	} else { //Type not found! Error Message!
		Errorf("Error in %s\n\nTOK_GET_TYPE %d not found!\n\n(%s)", __FILE__, TOK_GET_TYPE(pField->type), pText);	
	}

	// Check for dirty
	mef_checkIfDirty(pField, pWidget);
	
	// Call changed callback if present
	if (pField->changedCallback && (bChanged || bFinished)) {
		pField->changedCallback(pField, bFinished, pField->pChangeUserData);
	}

	// This is pretty weak
	//ui_WidgetSetTooltip(pWidget, ui_TextEntryGetText(pWidget));
}


static void mef_textFieldChanged(UIWidget *pWidget, MEField *pField)
{
	mef_textFieldChangedInternal(pWidget, pField, 0);
}

static void mef_textFieldFinished(UIWidget *pWidget, MEField *pField)
{
	mef_textFieldChangedInternal(pWidget, pField, 1);
}

static bool mef_PickerSelectedCallback(EMPicker *pPicker, EMPickerSelection **ppSelections, MEField *pField)
{
	ResourceInfo *pEntry;
	GroupDef *pDef = NULL;

	if(eaSize(&ppSelections) != 1)
		return false;

	assert(ppSelections[0]->table == parse_ResourceInfo);
	pEntry = ppSelections[0]->data;
	mef_SetPickerText(pField->pUIButton, pEntry->resourceName);
	mef_textFieldChangedInternal(UI_WIDGET(pField->pUIButton), pField, 1);
	return true;
}

static void mef_PickerButtonClicked(UIButton *pButton, MEField *pField)
{
	EMPicker *pPicker = emPickerGetByName(pField->pchEMPickerName);
	emPickerShow(pPicker, NULL, false, mef_PickerSelectedCallback, pField);
}

static void mef_sliderTextFieldChanged(UISliderTextEntry *pEntry, bool bFinished, MEField *pField)
{
	char *pText;
	int fieldType;
	bool bChanged = 0;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_sliderTextFieldChanged(pEntry, bFinished, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	if (!pEntry) {
		pText = "";
	} else {
		pText = (char*)ui_SliderTextEntryGetText(pEntry);
	}

	// Prechange is handled within nested calls in if/else statement

	// Update the fields based on the type
	fieldType = TOK_GET_TYPE(pField->type);
	if(fieldType == TOK_F32_X) {
		bChanged = mef_textFieldUpdateF32(pField, (UIWidget*)pEntry, atof(pText), true);
	} else if ((fieldType == TOK_INT_X) || (fieldType == TOK_U8_X) || (fieldType == TOK_BIT)) {
		bChanged = mef_textFieldUpdateInt(pField, (UIWidget*)pEntry, atoi(pText), true);
	} else { //Type not found! Error Message!
		Errorf("Error in %s\n\nTOK_GET_TYPE %d not found!\n\n(%s)", __FILE__, TOK_GET_TYPE(pField->type), pText);	
	}

	// Check for dirty
	mef_checkIfDirty(pField, pEntry);

	// Call changed callback if present
	if (pField->changedCallback && (bChanged || bFinished)) {
		pField->changedCallback(pField, bFinished, pField->pChangeUserData);
	}
}


static void mef_spinnerFieldChanged(UISpinnerEntry *pEntry, MEField *pField)
{
	F32 fValue;
	int fieldType;
	bool bChanged = 0;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_spinnerFieldChanged(pEntry, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	if (!pEntry) {
		fValue = 0;
	} else {
		fValue = ui_SpinnerEntryGetValue(pEntry);
	}

	// Prechange callback is handled in if/else statement

	// Update the fields based on the type
	fieldType = TOK_GET_TYPE(pField->type);
	if(fieldType == TOK_F32_X) {
		bChanged = mef_textFieldUpdateF32(pField, (UIWidget*)pEntry, fValue, true);
	} else if ((fieldType == TOK_INT_X) || (fieldType == TOK_U8_X) || (fieldType == TOK_BIT)) {
		bChanged = mef_textFieldUpdateInt(pField, (UIWidget*)pEntry, fValue, true);
	} else { //Type not found! Error Message!
		Errorf("Error in %s\n\nTOK_GET_TYPE %d not found!\n\n(%f)", __FILE__, TOK_GET_TYPE(pField->type), fValue);	
	}

	// Check for dirty
	mef_checkIfDirty(pField, pEntry);

	// Call changed callback if present
	if (pField->changedCallback && bChanged) {
		pField->changedCallback(pField, true, pField->pChangeUserData);
	}
}

static void mef_multiSpinnerFieldChanged(UIMultiSpinnerEntry *pEntry, MEField *pField)
{
	Vec4 vValues;
	int fieldType;
	bool bChanged = 0;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_multiSpinnerFieldChanged(pEntry, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}


	setVec4same(vValues, 0);
	if (pEntry) {
		assert(pField->arraySize > 1 && pField->arraySize <= 4);
		ui_MultiSpinnerEntryGetValue(pEntry, vValues, pField->arraySize);
	}

	// Prechange callback is handled in if/else statement

	// Update the fields based on the type
	fieldType = TOK_GET_TYPE(pField->type);
	if(fieldType == TOK_F32_X && (pField->type & TOK_FIXED_ARRAY)) {
		if(pEntry) {
			int i;
			bool bRevert = pField->bRevert;
			bool bParentUpdate = pField->bParentUpdate;
			bool bUpdate = pField->bUpdate;
			for ( i=0; i < pField->arraySize; i++ ) {
				pField->arrayIndex = i;
				pField->bRevert = bRevert;
				pField->bParentUpdate = bParentUpdate;
				pField->bUpdate = bUpdate;
				bChanged |= mef_textFieldUpdateF32(pField, (UIWidget*)pEntry, vValues[i], true);
			}
			pField->arrayIndex = -1;
		}
	} else { //Type not found! Error Message!
		Errorf("Error in %s\n\nTOK_GET_TYPE %d not found!\n\n(%f)(%f)(%f)(%f)", __FILE__, TOK_GET_TYPE(pField->type), vValues[0], vValues[1], vValues[2], vValues[3]);	
	}

	// Check for dirty
	mef_checkIfDirty(pField, pEntry);

	// Call changed callback if present
	if (pField->changedCallback && bChanged) {
		pField->changedCallback(pField, true, pField->pChangeUserData);
	}
}


static void mef_comboIntFieldChanged(UIComboBox *pCombo, int value, MEField *pField)
{	
	// Get the current state
	int iTarget = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pNew, pField->arrayIndex, NULL);
	int iOrig = pField->pOld ? TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pOld, pField->arrayIndex, NULL) : -1;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboIntFieldChanged(pCombo, value, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if ((iTarget != value) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		value = pField->pOld ? iOrig : iTarget;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			int iParent = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pParent, 0, NULL);
			value = pField->pParent ? iParent : iTarget;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		value = pField->pNew ? iTarget : 0;
		pField->bUpdate = false;
	}

	// Set the state and control
	if (pCombo) {
		if (pField->pUICombo) {
			ui_ComboBoxSetSelectedEnum(pField->pUICombo, value);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelectedEnum(pCombo, value);
		}
		TokenStoreSetIntAuto(pField->pTable, pField->column, pField->pNew, pField->arrayIndex, value, NULL, NULL);
	}

	// Check for dirty
	mef_checkIfDirty(pField, pCombo);
	
	// Call changed callback if present
	if (pField->changedCallback) {
		int iNewTarget = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pNew, 0, NULL);
		if (iTarget != iNewTarget) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
}


static void mef_comboStringFieldChanged(UIComboBox *pCombo, MEField *pField)
{	
	// Get the current state
	const char *pcTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL);
	const char *pcOrig = pField->pOld ? TokenStoreGetString(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), NULL) : NULL;
	const char *pcValue = NULL;
	int iSelected = -1;
	bool bNeedsFree = false;

	if (pCombo) {
		iSelected = ui_ComboBoxGetSelected(pCombo);
		if (iSelected >= 0) {
			if (pField->pchDictName)
			{			
				DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(pField->pchDictName);
				pcValue = TokenStoreGetString(pField->pDictTable, pField->iDictCol, pStruct->ppReferents[iSelected], MAX(0, pField->arrayIndex), NULL);
			}
			else if (pField->pchGlobalDictName)
			{
				ResourceDictionaryInfo *pStruct = resDictGetInfo(pField->pchGlobalDictName);
				pcValue = pStruct->ppInfos[iSelected]->resourceName;
			}
		}
	}

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboStringFieldChanged(pCombo, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pcTarget && pcValue && (pcValue[0] != '\0')) || (pcTarget && !pcValue && (pcTarget[0] != '\0')) || (pcTarget && pcValue && (strcmp(pcTarget,pcValue) != 0))) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pcValue = pField->pOld ? pcOrig : pcTarget;
		iSelected = -1;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			const char *pcParent = TokenStoreGetString(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL);
			pcValue = pField->pParent ? pcParent : pcTarget;
			iSelected = -1;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pcValue = pField->pNew ? pcTarget : 0;
		iSelected = -1;
		pField->bUpdate = false;
	}

	// Set the state and control
	if (pCombo) {
		if ((iSelected < 0) && pcValue) {
			ANALYSIS_ASSUME(pcValue);
			if (pField->pchDictName)
			{
				DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct(pField->pchDictName);
				int i;
				for(i=eaSize(&pStruct->ppReferents)-1; i>=0; --i) {
					const char *pcText = TokenStoreGetString(pField->pDictTable, pField->iDictCol, pStruct->ppReferents[i], MAX(0, pField->arrayIndex), NULL);
					if (strcmp(pcText, pcValue) == 0) {
						iSelected = i;
						break;
					}
				}
			}
			else if (pField->pchGlobalDictName)
			{
				ResourceDictionaryInfo *pStruct = resDictGetInfo(pField->pchGlobalDictName);
				int i;
				for(i=eaSize(&pStruct->ppInfos)-1; i>=0; --i) {
					const char *pcText = pStruct->ppInfos[i]->resourceName;
					if (strcmp(pcText, pcValue) == 0) {
						iSelected = i;
						break;
					}
				}
			}
		}
		if (pField->pUICombo) {
			ui_ComboBoxSetSelected(pField->pUICombo, iSelected);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelected(pCombo, iSelected);
		}
	}
	if (pcTarget) {
		pcTarget = strdup(pcTarget);
		bNeedsFree = true;
	}
	TokenStoreSetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), pcValue, NULL, NULL, NULL, NULL);

	// Check for dirty
	mef_checkIfDirty(pField, UI_WIDGET(pCombo));
	
	// Call changed callback if present
	if (pField->changedCallback) {
		const char *pcNewTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL);
		if ((pcNewTarget != pcTarget) &&
			(!pcNewTarget || !pcTarget || strcmp(pcNewTarget, pcTarget) != 0)) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
	if (bNeedsFree) {
		free((char*)pcTarget);
	}
}


static void mef_comboModelListStringFieldChanged(UIComboBox *pCombo, MEField *pField)
{	
	// Get the current state
	const char *pcTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, NULL);
	const char *pcOrig = pField->pOld ? TokenStoreGetString(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	const char *pcValue = NULL;
	int iSelected = -1;
	bool bNeedsFree = false;

	if (pCombo) {
		iSelected = ui_ComboBoxGetSelected(pCombo);
		if ((iSelected >= 0) && (iSelected < eaSize(pField->peaComboModel))) {
			if (pField->pDictTable) {
				pcValue = TokenStoreGetString(pField->pDictTable, pField->iDictCol, (*pField->peaComboModel)[iSelected], 0, NULL);
			} else {
				pcValue = (*pField->peaComboModel)[iSelected];
			}
		} else {
			iSelected = -1;
		}
	}

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboModelListStringFieldChanged(pCombo, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pcTarget && pcValue && (pcValue[0] != '\0')) || (pcTarget && !pcValue && (pcTarget[0] != '\0')) || (pcTarget && pcValue && (strcmp(pcTarget,pcValue) != 0))) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pcValue = pField->pOld ? pcOrig : pcTarget;
		iSelected = -1;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			const char *pcParent = TokenStoreGetString(pField->pTable, pField->column, pField->pParent, 0, NULL);
			pcValue = pField->pParent ? pcParent : pcTarget;
			iSelected = -1;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pcValue = pField->pNew ? pcTarget : 0;
		iSelected = -1;
		pField->bUpdate = false;
	}

	// Set the state and control
	if (pCombo) {
		if ((iSelected < 0) && pcValue) {
			int i;
			ANALYSIS_ASSUME(pcValue);
			for(i=eaSize(pField->peaComboModel)-1; i>=0; --i) {
				const char *pcText;
				if (pField->pDictTable) {
					pcText = TokenStoreGetString(pField->pDictTable, pField->iDictCol, (*pField->peaComboModel)[i], 0, NULL);
				} else {
					pcText = (*pField->peaComboModel)[i];
				}
				if (pcText) {
					ANALYSIS_ASSUME(pcText);
					if (strcmp(pcText, pcValue) == 0) {
						iSelected = i;
						break;
					}
				}
			}
		}
		if (pField->pUICombo) {
			ui_ComboBoxSetSelected(pField->pUICombo, iSelected);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelected(pCombo, iSelected);
		}
	}
	if (pcTarget)
	{
		pcTarget = strdup(pcTarget);
		bNeedsFree = true;
	}
	TokenStoreSetString(pField->pTable, pField->column, pField->pNew, 0, pcValue, NULL, NULL, NULL, NULL);

	// Check for dirty
	mef_checkIfDirty(pField, UI_WIDGET(pCombo));
	
	// Call changed callback if present
	if (pField->changedCallback) {
		const char *pcNewTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, NULL);
		if ((pcNewTarget != pcTarget) &&
			(!pcNewTarget || !pcTarget || strcmp(pcNewTarget, pcTarget) != 0)) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
	if (bNeedsFree) {
		free((char*)pcTarget);
	}
}


static void mef_comboModelListReferenceFieldChanged(UIComboBox *pCombo, MEField *pField)
{	
	// Get the current state
	const char *pchTarget = NULL;
	const char *pchOrig = NULL;
	const char *pchNewTarget = NULL;
	char *pchTemp = NULL;
	const char *pchText = NULL;
	ReferenceHandle *pTarget = TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	ReferenceHandle *pOrig = pField->pOld ? TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	ReferenceHandle *pNewTarget;
	int iSelected = -1;

	if ((pField->pNew != NULL) && RefSystem_IsHandleActive(pTarget)) {
		pchTarget = RefSystem_StringFromHandle(pTarget);
	}
	if ((pField->pOld != NULL) && RefSystem_IsHandleActive(pOrig)) {
		pchOrig = RefSystem_StringFromHandle(pOrig);
	}

	if (pCombo) {
		iSelected = ui_ComboBoxGetSelected(pCombo);
		if ((iSelected >= 0) && (iSelected < eaSize(pField->peaComboModel))) {
			if (pField->pDictTable) {
				pchText = TokenStoreGetString(pField->pDictTable, pField->iDictCol, (*pField->peaComboModel)[iSelected], 0, NULL);
			} else {
				pchText = (*pField->peaComboModel)[iSelected];
			}
		} else {
			iSelected = -1;
		}
	}

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboModelListReferenceFieldChanged(pCombo, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pchTarget && pchText && (pchText[0] != '\0')) || (pchTarget && !pchText && (pchTarget[0] != '\0')) || (pchTarget && pchText && (strcmp(pchTarget,pchText) != 0))) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pchText = pchOrig ? pchOrig : "";
		iSelected = -1;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			ReferenceHandle *pParent = TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pParent, 0, NULL);
			const char *pchParent = NULL;
			if (pField->pParent != NULL && RefSystem_IsHandleActive(pParent)) {
				pchParent = RefSystem_StringFromHandle(pParent);
			}
			pchText = pField->pParent ? (pchParent ? pchParent : "") : pchTarget ? pchTarget : "";
			iSelected = -1;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pchText = pchTarget ? pchTarget : "";
		iSelected = -1;
		pField->bUpdate = false;
	}

	// Set the state and control
	if (pCombo) {
		if ((iSelected < 0) && pchText) {
			int i;
			for(i=eaSize(pField->peaComboModel)-1; i>=0; --i) {
				const char *pcOption;
				if (pField->pDictTable) {
					pcOption = TokenStoreGetString(pField->pDictTable, pField->iDictCol, (*pField->peaComboModel)[i], 0, NULL);
				} else {
					pcOption = (*pField->peaComboModel)[i];
				}
				if (pcOption) {
					ANALYSIS_ASSUME(pcOption);
					if (strcmp(pcOption, pchText) == 0) {
						iSelected = i;
						break;
					}
				}
			}
		}
		if (pField->pUICombo) {
			ui_ComboBoxSetSelected(pField->pUICombo, iSelected);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelected(pCombo, iSelected);
		}
	}

	// Update the state and control
	if (strlen(pchText)==0) {
		if (pchTarget) {
			RefSystem_RemoveHandle(pTarget);
		}
	} else {
		if (pchTarget) {
			RefSystem_RemoveHandle(pTarget);
		}

		pchTemp = strchr(pchText,'\\');
		while (pchTemp) {
			pchTemp[0] = '/';
			pchTemp = strchr(pchTemp,'\\');
		}
		
		TokenStoreSetRef(pField->pTable, pField->column, pField->pNew, 0, pchText, NULL, NULL);
	}

	// Check for dirty
	mef_checkIfDirty(pField, UI_WIDGET(pCombo));
	
	// Check to see if the value changed
	pNewTarget = TokenStoreGetRefHandlePointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	if (pNewTarget && RefSystem_IsHandleActive(pNewTarget)) {
		pchNewTarget = RefSystem_StringFromHandle(pNewTarget);
	}
	if ((pchTarget && pchNewTarget && (strcmp(pchTarget,pchNewTarget) != 0)) ||
	    (pchTarget && !pchNewTarget) || (!pchTarget && pchNewTarget)) {
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}
}


static void mef_comboStringListFieldChanged(UIComboBox *pCombo, MEField *pField)
{	
	// Get the current state
	const char *pcTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL);
	const char *pcOrig = pField->pOld ? TokenStoreGetString(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), NULL) : NULL;
	const char *pcValue = NULL;
	int iSelected = -1;
	bool bNeedsFree = false;

	if (pCombo) {
		iSelected = ui_ComboBoxGetSelected(pCombo);
		if ((iSelected >= 0) && (iSelected < eaSize(pField->peaComboStrings))) {
			pcValue = (*(char***)pField->peaComboStrings)[iSelected];
		} else {
			iSelected = -1;
		}
	}

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboStringListFieldChanged(pCombo, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pcTarget && pcValue && (pcValue[0] != '\0')) || (pcTarget && !pcValue && (pcTarget[0] != '\0')) || (pcTarget && pcValue && (strcmp(pcTarget,pcValue) != 0))) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pcValue = pField->pOld ? pcOrig : pcTarget;
		iSelected = -1;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			const char *pcParent = TokenStoreGetString(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL);
			pcValue = pField->pParent ? pcParent : pcTarget;
			iSelected = -1;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pcValue = pField->pNew ? pcTarget : 0;
		iSelected = -1;
		pField->bUpdate = false;
	}

	// Set the state and control
	if (pCombo) {
		if ((iSelected < 0) && pcValue) {
			int i;
			ANALYSIS_ASSUME(pcValue);
			for(i=eaSize(pField->peaComboStrings)-1; i>=0; --i) {
				const char *pcText = (*(char***)pField->peaComboStrings)[i];
				if (pcText) {
					ANALYSIS_ASSUME(pcText);
					if (strcmp(pcText, pcValue) == 0) {
						iSelected = i;
						break;
					}
				}
			}
		}
		if (pField->pUICombo) {
			ui_ComboBoxSetSelected(pField->pUICombo, iSelected);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelected(pCombo, iSelected);
		}
	}
	if (pcTarget)
	{
		pcTarget = strdup(pcTarget);
		bNeedsFree = true;
	}
	TokenStoreSetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), pcValue, NULL, NULL, NULL, NULL);

	// Check for dirty
	mef_checkIfDirty(pField, UI_WIDGET(pCombo));
	
	// Call changed callback if present
	if (pField->changedCallback) {
		const char *pcNewTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL);
		if ((pcNewTarget != pcTarget) &&
			(!pcNewTarget || !pcTarget || strcmp(pcNewTarget, pcTarget) != 0)) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
	if (bNeedsFree) {
		free((char*)pcTarget);
	}
}


static void mef_flagFieldChanged(UIComboBox *pCombo, MEField *pField)
{
	// Get the current state
	S32 iValue = pCombo ? pCombo->iSelected : 0;
	S32 iTarget = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pNew, 0, NULL);
	S32 iOrig = pField->pOld ? TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pOld, 0, NULL) : -1;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_flagFieldChanged(pCombo, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if ((iValue != iTarget) &&
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		iValue = pField->pOld ? iOrig : iTarget;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			S32 iParent = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pParent, 0, NULL);
			iValue = pField->pParent ? iParent : iTarget;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		iValue = iTarget;
		pField->bUpdate = false;
	}

	// Update the state and control
	if (pCombo) {
		if (pField->pUICombo) {
			ui_ComboBoxSetSelected(pField->pUICombo, iValue);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelected(pCombo, iValue);
		}
	}
	TokenStoreSetIntAuto(pField->pTable, pField->column, pField->pNew, 0, iValue, NULL, NULL);

	// Check for dirty
	mef_checkIfDirty(pField, pCombo);
	
	// Call changed callback if present
	if (pField->changedCallback) {
		S32 iNewTarget = TokenStoreGetIntAuto(pField->pTable, pField->column, pField->pNew, 0, NULL);
		if (iTarget != iNewTarget) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
}


static void mef_comboFieldMultiChanged(UIComboBox *pCombo, int value, MEField *pField)
{
	// Get the current state
	int **ppTarget = TokenStoreGetEArrayInt(pField->pTable, pField->column, pField->pNew, 0);
	int **ppOrig = pField->pOld ? TokenStoreGetEArrayInt(pField->pTable, pField->column, pField->pOld, 0) : ppTarget;
	int *piCurrent = NULL;
	bool bUpdateControl = false;

	ea32Create(&piCurrent);
	if (pCombo) {
		ui_ComboBoxGetSelectedEnums(pCombo, &piCurrent);
	}

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboFieldMultiChanged(pCombo, value, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate &&
		!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
		pField->bRevert = true;
	}

	// Determine the proper value
	if (pField->bRevert) {
		*ppOrig ? ea32Copy(&piCurrent,ppOrig) : *ppTarget ? ea32Copy(&piCurrent,ppTarget): ea32Clear(&piCurrent);
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			int **ppParent = TokenStoreGetEArrayInt(pField->pTable, pField->column, pField->pParent, 0);
			pField->pParent && *ppParent ? ea32Copy(&piCurrent,ppParent) : *ppTarget ? ea32Copy(&piCurrent,ppTarget): ea32Clear(&piCurrent);
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		*ppTarget ? ea32Copy(&piCurrent,ppTarget) : ea32Clear(&piCurrent);
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the state and control
	if (pCombo) {
		if (pField->pUICombo) {
			ui_ComboBoxSetSelectedEnums(pField->pUICombo, &piCurrent);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelectedEnums(pCombo, &piCurrent);
		}
	}

	// Set value back into data
	if (ea32Size(&piCurrent) == 0) {
		if (*ppTarget) {
			ea32Destroy(ppTarget);
			*ppTarget = NULL;
		}
	} else {
		ea32Copy(ppTarget, &piCurrent);
	}

	// Check for dirty
	mef_checkIfDirty(pField, UI_WIDGET(pCombo));
	
	// Call changed callback if present
	if (pField->changedCallback) {
		// TODO: Should only fire this if the value changes
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}
}

static void MERefArrayStructToString(MERefArrayStruct ***pppArray, char **pestrOut)
{
	int i;
	estrClear(pestrOut);
	for(i=0;i<eaSize(pppArray);i++)
	{
		if(i!=0)
			estrConcat(pestrOut," ",1);
		estrConcatf(pestrOut,"%s",REF_STRING_FROM_HANDLE((*pppArray)[i]->hRef));
	}
}

static void mef_comboFieldRefArrayStructMultiChanged(UIComboBox *pCombo, MEField *pField)
{
	MERefArrayStruct ***pppStructTarget = (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
	MERefArrayStruct ***pppStructOrig = pField->pOld ? (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL) : NULL;
	void **ppCurrent = NULL;
	char *pchCurrent = NULL;
	bool bUpdateControl = false;

	estrCreate(&pchCurrent);

	if(pCombo) {
		ui_ComboBoxGetSelectedsAsString(pCombo,&pchCurrent);
	}

	// Support for multi-field editing
	if(!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboFieldRefArrayStructMultiChanged(pCombo, pField->eaSiblings[i]);
			if(!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changes only some of the fields. This is because the change had side-effects in the editor. Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if(pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate &&
		!(*pField->preChangedCallback)(pField,true,pField->pPreChangeUserData)) {
			pField->bRevert = true;
	}

	// Determine the proper value
	if(pField->bRevert) {
		eaClearStructVoid(pppStructTarget,pField->pTable[pField->column].subtable);
		if(pppStructOrig && eaSize(pppStructOrig) > 0)
		{
			MERefArrayStructToString(pppStructOrig,&pchCurrent);
		}

		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			MERefArrayStruct ***pppStructParent = (MERefArrayStruct***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pParent, NULL);
			if(pppStructParent)
			{
				MERefArrayStructToString(pppStructParent,&pchCurrent);
			}
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	if (pField->bUpdate) {
		if(pppStructTarget && eaSize(pppStructTarget) > 0)
		{
			MERefArrayStructToString(pppStructTarget,&pchCurrent);
		}
		else
		{
			estrClear(&pchCurrent);
		}
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the state and control
	if (pCombo) {
		if (pField->pUICombo) {
			ui_ComboBoxSetSelectedsAsString(pField->pUICombo,pchCurrent);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelectedsAsString(pCombo,pchCurrent);
		}
	}

	if (estrLength(&pchCurrent)<=0) {
		if (*pppStructTarget) {
			ParseTable *pStructTable;

			// Destroy the struct
			StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
			eaDestroyStructVoid(pppStructTarget,pStructTable);
			*pppStructTarget = NULL;
			TokenStoreSetEArraySize(pField->pTable,pField->column,pField->pNew, 0, NULL);
		}
	} else {
		ParseTable *pStructTable;
		char tempString[1064], *strings[64];
		int i,count;

		strcpy(tempString, pchCurrent);
		count = tokenize_line(tempString,strings, NULL);

		StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
		eaDestroyStructVoid(pppStructTarget,pStructTable);

		for(i=0;i<count;i++)
		{
			MERefArrayStruct *pNew = StructCreateVoid(pStructTable);

			SET_HANDLE_FROM_STRING(pField->pchGlobalDictName,strings[i],pNew->hRef);

			eaPush(pppStructTarget,pNew);
		}
	}

	estrDestroy(&pchCurrent);

	// Check for dirty
	mef_checkIfDirty(pField, UI_WIDGET(pCombo));

	// Call changed callback if present
	if (pField->changedCallback) {
		// TODO: Should only fire this if the value changes
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}

}

static void mef_comboFieldIntArrayStructMultiChanged(UIComboBox *pCombo, int iValue, MEField *pField)
{
	// Get the current state
	MEIntArrayStruct *pStructTarget = (MEIntArrayStruct*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	MEIntArrayStruct *pStructOrig = pField->pOld ? (MEIntArrayStruct*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	int *piCurrent = NULL;
	bool bUpdateControl = false;

	ea32Create(&piCurrent);
	if (pCombo) {
		ui_ComboBoxGetSelectedEnums(pCombo, &piCurrent);
	}

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_comboFieldIntArrayStructMultiChanged(pCombo, iValue, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate &&
		!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
		pField->bRevert = true;
	}

	// Determine the proper value
	if (pField->bRevert) {
		pStructOrig && pStructOrig->eaInt ? ea32Copy(&piCurrent,&pStructOrig->eaInt) : pStructTarget && pStructTarget->eaInt ? ea32Copy(&piCurrent,&pStructTarget->eaInt): ea32Clear(&piCurrent);
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			MEIntArrayStruct *pStructParent = TokenStoreGetPointer(pField->pTable, pField->column, pField->pParent, 0, NULL);
			pField->pParent && pStructParent && pStructParent->eaInt ? ea32Copy(&piCurrent,&pStructParent->eaInt) : ea32Clear(&piCurrent);
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pStructTarget && pStructTarget->eaInt ? ea32Copy(&piCurrent,&pStructTarget->eaInt) : ea32Clear(&piCurrent);
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the state and control
	if (pCombo) {
		if (pField->pUICombo) {
			ui_ComboBoxSetSelectedEnums(pField->pUICombo, &piCurrent);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelectedEnums(pCombo, &piCurrent);
		}
	}

	if (ea32Size(&piCurrent) == 0) {
		if (pStructTarget) {
			ParseTable *pStructTable;

			// Destroy the struct
			StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
			StructDestroyVoid(pStructTable, pStructTarget);
			pStructTarget = NULL;
			TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL, NULL);
		}
	} else {
		if (!pStructTarget) {
			ParseTable *pStructTable;

			// Create the struct
			StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
			pStructTarget = StructCreateVoid(pStructTable);
			TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, pStructTarget, NULL);
		}

		ea32Copy(&pStructTarget->eaInt, &piCurrent);
	}

	// Check for dirty
	mef_checkIfDirty(pField, UI_WIDGET(pCombo));
	
	// Call changed callback if present
	if (pField->changedCallback) {
		// TODO: Should only fire this if the value changes
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}
}


static void mef_booleanComboFieldChanged(UIComboBox *pCombo, int value, MEField *pField)
{	
	// Get the current state
	bool bTarget = mef_getBooleanValue(pField->pTable, pField->column, pField->pNew);
	bool bOrig = pField->pOld ? mef_getBooleanValue(pField->pTable, pField->column, pField->pOld) : -1;
	bool b = value ? true : false;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_booleanComboFieldChanged(pCombo, value, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((bTarget && !b) || (!bTarget && b)) &&
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if(pField->bRevert)	{
		b = pField->pOld ? bOrig : bTarget;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			bool bParent = mef_getBooleanValue(pField->pTable, pField->column, pField->pParent);
			b = pField->pParent ? bParent : bTarget;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate)	{
		b = bTarget;
		pField->bUpdate = false;
	}

	// Set the state and control
	if (pCombo) {
		if (pField->pUICombo) {
			ui_ComboBoxSetSelectedEnum(pField->pUICombo, b ? 1 : 0);
		}
		if (pCombo != pField->pUICombo) {
			ui_ComboBoxSetSelectedEnum(pCombo, b ? 1 : 0);
		}
	}
	mef_setBooleanValue(pField->pTable, pField->column, pField->pNew, b);
	b = mef_getBooleanValue(pField->pTable, pField->column, pField->pNew);

	// Check for dirty
	mef_checkIfDirty(pField, pCombo);
	
	// Call changed callback if present
	if (pField->changedCallback && (bTarget != b)) {
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}
}


static void mef_checkFieldChanged(UICheckButton *pCheck, MEField *pField)
{
	// Get the current state
	bool b = false;
	bool bTarget = mef_getBooleanValue(pField->pTable, pField->column, pField->pNew);
	bool bOrig = pField->pOld ? mef_getBooleanValue(pField->pTable, pField->column, pField->pOld) : 0;
	bool bNewTarget;
	
	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings))
	{
		int i;

		//callbackHolder is here to hold onto the callback so the siblings don't have it when they run through this function on their own.
			//The callback updates ALL the siblings, not just the one calling. This change reduces the number of calls from n^2 to n.
		MEFieldChangeCallback callbackHolder = NULL;

		for(i = eaSize(&pField->eaSiblings) - 1; i >= 0; --i)
		{
			if (pField->eaSiblings[i]->bSkipSiblingChangedCallbacks)
			{
				callbackHolder = pField->eaSiblings[i]->changedCallback;
				pField->eaSiblings[i]->changedCallback = NULL;
			}
			mef_checkFieldChanged(pCheck, pField->eaSiblings[i]);
			if (pField->eaSiblings[i]->bSkipSiblingChangedCallbacks)
			{
				pField->eaSiblings[i]->changedCallback = callbackHolder;
			}
			if (!pField->eaSiblings)
			{
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Have to determine b before we can check if it changed or not below...
	if (pCheck) {
		b = pCheck->state;
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((bTarget && !b) || (!bTarget && b)) &&
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if(pField->bRevert)	{
		b = pField->pOld ? bOrig : bTarget;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			bool bParent = mef_getBooleanValue(pField->pTable, pField->column, pField->pParent);
			b = pField->pParent ? bParent : bTarget;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		b = bTarget;
		pField->bUpdate = false;
	}

	// Update the data and control
	if (pCheck) {
		pCheck->state = b;
	}
	mef_setBooleanValue(pField->pTable, pField->column, pField->pNew, b);
	bNewTarget = mef_getBooleanValue(pField->pTable, pField->column, pField->pNew);

	// Check for dirty
	mef_checkIfDirty(pField, pCheck);
	
	// Call changed callback if present
	if (pField->changedCallback && (bTarget != bNewTarget))
	{
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}
}


static void mef_fileNameFieldChanged(UIFileNameEntry *pEntry, MEField *pField)
{
	// Get the current state
	const char *pchTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
	const char *pchOrig	= pField->pOld ? TokenStoreGetString(pField->pTable, pField->column, pField->pOld, 0, 0) : NULL;
	const char *pchNewTarget = pEntry ? ui_FileNameEntryGetFileName(pEntry) : "";
	bool bUpdateControl = false;
	bool bNeedsFree = false;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_fileNameFieldChanged(pEntry, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		bool bCallPreChangedCallback = false;
		// Only do pre-change if it actually changed
		if (!pchTarget && (pchNewTarget[0] != '\0')) {
			bCallPreChangedCallback = true;
		}
		else if (pchTarget) {
			ANALYSIS_ASSUME(pchTarget);
			if (strcmp(pchTarget, pchNewTarget) != 0) {
				bCallPreChangedCallback = true;
			}
		}

		if (bCallPreChangedCallback) {
			pField->bRevert = !(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData);
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pchNewTarget = pField->pOld ? (pchOrig ? pchOrig : "") : (pchTarget ? pchTarget : "");
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		pchNewTarget = pField->pNew ? (pchTarget ? pchTarget : "") : "";
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			const char *pchParent = TokenStoreGetString(pField->pTable, pField->column, pField->pParent, 0, NULL);
			pchNewTarget = pField->pParent && pchParent ? pchParent : "";
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	// Update the control and value
	if (bUpdateControl && pEntry) {
		if (pField->pUIFileName) {
			if (strcmp(ui_FileNameEntryGetFileName(pField->pUIFileName), pchNewTarget) != 0) {
				ui_FileNameEntrySetFileName(pField->pUIFileName, pchNewTarget);
			}
		}
		if (pEntry != pField->pUIFileName) {
			if (strcmp(ui_FileNameEntryGetFileName(pEntry), pchNewTarget) != 0) {
				ui_FileNameEntrySetFileName(pEntry, pchNewTarget);
			}
		}
	}

	if (pchTarget != pchNewTarget) {
		if (pchTarget) {
			pchTarget = strdup(pchTarget);
			bNeedsFree = true;
		}
		TokenStoreSetString(pField->pTable, pField->column, pField->pNew, 0, pchNewTarget, 0, 0, 0, 0);
	}

	// Check for dirty
	mef_checkIfDirty(pField, pEntry);
	
	// Check for changes
	if (pField->changedCallback) {
		bool bCallChangedCallback = false;
		pchNewTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
		if (pchTarget) {
			ANALYSIS_ASSUME(pchTarget);
			if (pchNewTarget) {
				ANALYSIS_ASSUME(pchNewTarget);
				if (strcmp(pchTarget,pchNewTarget) != 0) {
					bCallChangedCallback = true;
				}
			}
			else {
				bCallChangedCallback = true;
			}
		}
		else {
			if (!pchNewTarget) {
				bCallChangedCallback = true;
			}
		}

		if (bCallChangedCallback) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
	if (bNeedsFree) {
		free((char*)pchTarget);
	}
}


static bool mef_messageFieldPreChanged(UIMessageEntry *pEntry, MEField *pField)
{
	if (pField->preChangedCallback) {
		return (*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData);
	} else {
		return true;
	}
}


static void mef_messageFieldChanged(UIMessageEntry *pEntry, MEField *pField)
{
	// Get the current state
	bool bUpdateControl = false;
	bool bChanged = false;
	Message *pTarget;
	Message *pOrig;
	Message *pNewTarget;

	if(pField->eType == kMEFieldType_DisplayMessage) {
		DisplayMessage *pDisplayMessage;
		pDisplayMessage = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		pTarget = SAFE_MEMBER(pDisplayMessage, pEditorCopy);
		pDisplayMessage = pField->pOld ? TokenStoreGetPointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
		pOrig = SAFE_MEMBER(pDisplayMessage, pEditorCopy);
	} else {
		pTarget = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		pOrig = pField->pOld ? TokenStoreGetPointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	}
	pNewTarget = pEntry ? (Message*)ui_MessageEntryGetMessage(pEntry) : NULL;


	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_messageFieldChanged(pEntry, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pTarget && pNewTarget) || 
			 (pTarget && pNewTarget && (StructCompare(parse_Message,pTarget,pNewTarget,0,0, TOK_USEROPTIONBIT_1) != 0)) ||
			 (pTarget && pNewTarget && pTarget->pcDefaultString && pNewTarget->pcDefaultString && (strcmp(pTarget->pcDefaultString, pNewTarget->pcDefaultString) != 0))) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pNewTarget = pOrig ? pOrig : NULL;
		bUpdateControl = true;
		pField->bRevert = false;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			pNewTarget = pField->pParent ? TokenStoreGetPointer(pField->pTable, pField->column, pField->pParent, 0, NULL) : pTarget;
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pNewTarget = pTarget;
		bUpdateControl = true;
		pField->bUpdate = false;
	}

	if (bUpdateControl && pEntry) {
		Message *pTempMsg = pNewTarget;
		if (!pNewTarget) {
			pTempMsg = StructCreate(parse_Message);
		}
		if (pField->pUIMessage) {
			const Message *pCurrentMsg = ui_MessageEntryGetMessage(pField->pUIMessage);
			if (!pCurrentMsg || (StructCompare(parse_Message, pTempMsg, pCurrentMsg, 0, 0, TOK_USEROPTIONBIT_1) != 0) ||
				(pTempMsg && pTempMsg->pcDefaultString && pCurrentMsg && pCurrentMsg->pcDefaultString && (strcmp(pTempMsg->pcDefaultString, pCurrentMsg->pcDefaultString) != 0))) {
				ui_MessageEntrySetMessage(pField->pUIMessage, pTempMsg);
			}
		}
		if (pEntry != pField->pUIMessage) {
			const Message *pCurrentMsg = ui_MessageEntryGetMessage(pEntry);
			if (!pCurrentMsg || (StructCompare(parse_Message, pTempMsg, pCurrentMsg, 0, 0, TOK_USEROPTIONBIT_1) != 0) ||
				(pTempMsg && pTempMsg->pcDefaultString && pCurrentMsg && pCurrentMsg->pcDefaultString && (strcmp(pTempMsg->pcDefaultString, pCurrentMsg->pcDefaultString) != 0))) {
				ui_MessageEntrySetMessage(pEntry, pTempMsg);
			}
		}
		if (!pNewTarget) {
			StructDestroy(parse_Message, pTempMsg);
		}
	}

	// Update the state and control
	if (!pNewTarget) {
		if(pField->eType == kMEFieldType_DisplayMessage) {
			DisplayMessage *pDisplayMessage;
			pDisplayMessage = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
			pDisplayMessage->pEditorCopy = pNewTarget;
		} else {
			TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, pNewTarget, NULL);
		}
		bChanged = true;
	} else if (!pTarget || (StructCompare(parse_Message, pNewTarget, pTarget, 0, 0, TOK_USEROPTIONBIT_1) != 0) ||
			   (pTarget && pNewTarget && pTarget->pcDefaultString && pNewTarget->pcDefaultString && (strcmp(pTarget->pcDefaultString,pNewTarget->pcDefaultString) != 0))) {
		// It changed, so store it
		Message *pMsgToStore = StructClone(parse_Message, pNewTarget);
		assert(pMsgToStore);
		if(pField->eType == kMEFieldType_DisplayMessage) {
			DisplayMessage *pDisplayMessage;
			pDisplayMessage = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
			pDisplayMessage->pEditorCopy = pMsgToStore;
		} else {
			TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, pMsgToStore, NULL);
		}
		bChanged = true;
	}

	// Check for dirty
	mef_checkIfDirty(pField, pEntry);

	if (bChanged) {
		// Then make callback if necessary
		if (pField->changedCallback) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
}

static void mef_multiTextureFieldChanged(UITextureEntry *pEntry, MEField *pField)
{
	// Get the current state
	char ***ppchTarget = (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);
	char ***ppchOrig = pField->pOld ? (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pOld, NULL) : NULL;
	const char* pchText = pEntry ? ui_TextureEntryGetTextureName(pEntry) : "";
	char *pchEntry, *pchFind;
	char *achCompare = estrCreateFromStr("");
	int i;
	bool bUpdateControl = false;

	const char* pchIndexSeparator = (pField->pchIndexSeparator ? pField->pchIndexSeparator : " ");

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Calc original compare string
		estrClear(&achCompare);
		if(ppchOrig && (eaSize(ppchOrig) > 0)) {
			for(i=0; i<eaSize(ppchOrig); i++) {
				if ((*ppchOrig)[i]) { // Skip until first non-NULL
					estrCopy2(&achCompare, (*ppchOrig)[i]);
					break;
				}
			}
			for(++i; i<eaSize(ppchOrig); i++) {
				if((*ppchOrig)[i]) { // Deal with NULL entries
					estrAppend2(&achCompare, pchIndexSeparator);
					estrAppend2(&achCompare, (*ppchOrig)[i]);
				}
			}
		}
		// Only call pre-change callback if value changed
		if (pchText) {
			ANALYSIS_ASSUME(pchText);
			if (strcmp(pchText, achCompare) != 0) {
				pField->bRevert = !(*pField->preChangedCallback)(pField, 1, pField->pPreChangeUserData);
			}
		}
	}

	// Put incoming value in the buffer to be used if not overwritten
	estrCopy2(&achCompare, pchText);

	// Determine the proper value
	if (pField->bRevert) {
		estrClear(&achCompare);
		if(ppchOrig && (eaSize(ppchOrig) > 0)) {
			for(i=0; i<eaSize(ppchOrig); i++) {
				if ((*ppchOrig)[i]) { // Skip until first non-NULL
					estrCopy2(&achCompare, (*ppchOrig)[i]);
					break;
				}
			}
			for(++i; i<eaSize(ppchOrig); i++) {
				if((*ppchOrig)[i]) { // Deal with NULL entries
					estrAppend2(&achCompare, pchIndexSeparator);
					estrAppend2(&achCompare, (*ppchOrig)[i]);
				}
			}
		}
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		estrClear(&achCompare);
		if(ppchTarget && (eaSize(ppchTarget) > 0)) {
			for(i=0; i<eaSize(ppchTarget); i++) {
				if ((*ppchTarget)[i]) { // Skip until first non-NULL
					estrCopy2(&achCompare, (*ppchTarget)[i]);
					break;
				}
			}
			for(++i; i<eaSize(ppchTarget); i++) {
				if((*ppchTarget)[i]) { // Deal with NULL entries
					estrAppend2(&achCompare, pchIndexSeparator);
					estrAppend2(&achCompare, (*ppchTarget)[i]);
				}
			}
		}
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			char ***ppchParent = (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pParent, NULL);

			estrClear(&achCompare);
			if (ppchParent && (eaSize(ppchParent) > 0)) {
				for(i=0; i<eaSize(ppchParent); i++) {
					if ((*ppchParent)[i]) { // Skip to first non-NULL
						estrCopy2(&achCompare, (*ppchParent)[i]);
						break;
					}
				}
				for(++i; i<eaSize(ppchParent); i++) {
					if((*ppchParent)[i]) { // Skip NULL entries
						estrAppend2(&achCompare, pchIndexSeparator);
						estrAppend2(&achCompare, (*ppchParent)[i]);
					}
				}
			}
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}


	// Update the control and value
	if (bUpdateControl && pEntry) {
		if (pField->pUITexture) {
			ui_TextureEntrySetTextureName(pField->pUITexture, achCompare);
		}
		if (pEntry != pField->pUITexture) {
			ui_TextureEntrySetTextureName(pEntry, achCompare);
		}
	}

	// Update the value and control
	if (ppchTarget) {
		TokenStoreDestroyEArray(pField->pTable, &pField->pTable[pField->column], pField->column, pField->pNew, NULL);
	}
	TokenStoreMakeLocalEArray(pField->pTable, pField->column, pField->pNew, NULL);
	ppchTarget = (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL);


	pchEntry = achCompare;
	pchFind = strstr(achCompare,pchIndexSeparator);
	while (pchFind) {
		pchFind[0] = '\0';
		eaPush(ppchTarget, StructAllocString(pchEntry));
		pchFind+=strlen(pchIndexSeparator);
		pchEntry = pchFind;
		pchFind = strstr(pchEntry,pchIndexSeparator);
	}
	if (pchEntry && pchEntry[0] != '\0') {
		eaPush(ppchTarget, StructAllocString(pchEntry));
	}

	// Check for dirty
	mef_checkIfDirty(pField, pEntry);

	estrDestroy(&achCompare);
}

static void mef_textureFieldChanged(UITextureEntry *pEntry, MEField *pField)
{
	// Get the current state
	const char *pchTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
	const char *pchOrig	= pField->pOld ? TokenStoreGetString(pField->pTable, pField->column, pField->pOld, 0, 0) : NULL;
	const char *pchNewTarget = pEntry ? ui_TextureEntryGetTextureName(pEntry) : "";
	bool bUpdateControl = false;
	bool bNeedsFree = false;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_textureFieldChanged(pEntry, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		bool bCallPreChangedCallback = false;
		if (!pchTarget && (pchNewTarget[0] != '\0')) {
			bCallPreChangedCallback = true;
		}
		else if (pchTarget) {
			ANALYSIS_ASSUME(pchTarget);
			if (strcmp(pchTarget, pchNewTarget) != 0) {
				bCallPreChangedCallback = true;
			}
		}

		if (bCallPreChangedCallback) {
			pField->bRevert = !(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData);
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pchNewTarget = pField->pOld ? (pchOrig ? pchOrig : "") : (pchTarget ? pchTarget : "");
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		pchNewTarget = pField->pNew ? (pchTarget ? pchTarget : "") : "";
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			const char *pchParent = TokenStoreGetString(pField->pTable, pField->column, pField->pParent, 0, NULL);
			pchNewTarget = pField->pParent && pchParent ? pchParent : "";
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	// Update the control and value
	if (bUpdateControl && pEntry) {
		if (pField->pUITexture) {
			ui_TextureEntrySetTextureName(pField->pUITexture, pchNewTarget);
		}
		if (pEntry != pField->pUITexture) {
			ui_TextureEntrySetTextureName(pEntry, pchNewTarget);
		}
	}

	if (pchTarget != pchNewTarget) {
		if (pchTarget) {
			pchTarget = strdup(pchTarget);
			bNeedsFree = true;
		}
		TokenStoreSetString(pField->pTable, pField->column, pField->pNew, 0, pchNewTarget, 0, 0, 0, 0);
	}

	// Check for dirty
	mef_checkIfDirty(pField, pEntry);
	
	// Check for changes
	if (pField->changedCallback) {
		bool bCallChangedCallback = false;
		pchNewTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
		if (pchTarget) {
			ANALYSIS_ASSUME(pchTarget);
			if (pchNewTarget) {
				ANALYSIS_ASSUME(pchNewTarget);
				if (strcmp(pchTarget,pchNewTarget) != 0) {
					bCallChangedCallback = true;
				}
			}
			else {
				bCallChangedCallback = true;
			}
		}
		else {
			if (pchNewTarget) {
				bCallChangedCallback = true;
			}
		}

		if (bCallChangedCallback) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
	if (bNeedsFree) {
		free((char*)pchTarget);
	}
}

static void mef_colorFieldChanged(UIColorButton *pColor, bool bFinished, MEField *pField)
{
	Vec4 vTarget, vOrig, vNewTarget = {0,0,0};
	bool bUpdateControl = false;

	// Get the current state
	mef_getColorValue(pField->pTable, pField->column, pField->pNew, vTarget);
	if(pField->pOld)
		mef_getColorValue(pField->pTable, pField->column, pField->pOld, vOrig);
	if(pColor) {
		ui_ColorButtonGetColor(pColor, vNewTarget);
		if(pField->eColorType == kMEFieldColorType_HSV) {
			Vec3 vTemp;
			rgbToHsv(vNewTarget, vTemp);
			copyVec3(vTemp, vNewTarget);
		}
	}

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_colorFieldChanged(pColor, bFinished, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (!sameVec4(vTarget, vNewTarget) && !(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		if(pField->pOld)
			copyVec4(vOrig, vNewTarget);
		else
			copyVec4(vTarget, vNewTarget);
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		copyVec4(vTarget, vNewTarget);
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			Vec4 vParent;
			mef_getColorValue(pField->pTable, pField->column, pField->pParent, vParent);
			copyVec4(vParent, vNewTarget);
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	// Update the control and value
	if (bUpdateControl && pColor) {
		Vec4 vUpdateColor;
		copyVec4(vNewTarget, vUpdateColor);
		if(pField->eColorType == kMEFieldColorType_HSV) {
			Vec3 vTemp;
			hsvToRgb(vUpdateColor, vTemp);
			copyVec3(vTemp, vUpdateColor);
		}
		if (pField->pUIColor) {
			ui_ColorButtonSetColor(pField->pUIColor, vUpdateColor);
		}
		if (pColor != pField->pUIColor) {
			ui_ColorButtonSetColor(pColor, vUpdateColor);
		}
	}

	mef_setColorValue(pField->pTable, pField->column, pField->pNew, vNewTarget);
	mef_getColorValue(pField->pTable, pField->column, pField->pNew, vNewTarget);

	// Check for dirty
	mef_checkIfDirty(pField, pColor);

	// Call changed callback if present
	if (pField->changedCallback && (!sameVec4(vTarget, vNewTarget) || bFinished)) {
		pField->changedCallback(pField, bFinished, pField->pChangeUserData);
	}
}

static void mef_hueFieldChanged(UIColorSlider *pColorSlider, MEField *pField)
{
	// Get the current state
	F32 fScale = pField->fScale;
	F32 fTarget = TokenStoreGetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL) * fScale;
	F32 fOrig = pField->pOld ? TokenStoreGetF32(pField->pTable, pField->column, pField->pOld, MAX(0, pField->arrayIndex), NULL) * fScale : fTarget;
	F32 fNewTarget;
	Vec3 slider_value;
	F32 f = 0;
	bool bUpdateControl = false;

	if(pColorSlider)
	{
		ui_ColorSliderGetValue(pColorSlider, slider_value);
		f = slider_value[0];
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if ((fTarget != f) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
				pField->bRevert = true;
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		f = pField->pOld ? fOrig : fTarget;
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			F32 fParent = TokenStoreGetF32(pField->pTable, pField->column, pField->pParent, MAX(0, pField->arrayIndex), NULL) * fScale;
			f = pField->pParent ? fParent : fTarget;
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		f = pField->pNew ? fTarget : 0;
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the control and value
	if (bUpdateControl && pColorSlider) {
		slider_value[0] = f;
		ui_ColorSliderSetValue(pColorSlider, slider_value);

		if (pColorSlider != pField->pUIColorSlider) {
			ui_ColorSliderSetValue(pField->pUIColorSlider, slider_value);
		}
	}

	TokenStoreSetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), f / fScale, NULL, NULL);

	// Check for Changes
	fNewTarget = TokenStoreGetF32(pField->pTable, pField->column, pField->pNew, MAX(0, pField->arrayIndex), NULL) * fScale;

	// Check for dirty
	mef_checkIfDirty(pField, pColorSlider);

	// Call changed callback if present
	if (pField->changedCallback && fTarget != fNewTarget) {
		pField->changedCallback(pField, true, pField->pChangeUserData);
	}
}

static void mef_updateField(MEField *pField, UIWidget *pWidget, bool bRevert)
{
	if (bRevert) {
		pField->bRevert = true;
	}

	if (pField->eType >= MIN_EX_TYPE) {
		eaExternTypes[pField->eType-MIN_EX_TYPE]->cbUpdate(pWidget, pField);
	} else {
		switch( pField->eType )
		{
			case kMEFieldType_TextEntry:
			case kMEFieldType_EMPicker:
			case kMEFieldType_ValidatedTextEntry:
			case kMEFieldType_TextArea:
			case kMEFieldType_MultiText:
			case kMEFieldType_SMFTextEntry:
				mef_textFieldChangedInternal(pWidget, pField,1);
			xcase kMEFieldType_Combo:
				{
					if (pField->pEnum) {
						mef_comboIntFieldChanged((UIComboBox*)pWidget, pWidget ? ui_ComboBoxGetSelectedEnum((UIComboBox*)pWidget) : 0, pField);
					} else if (pField->peaComboStrings) {
						mef_comboStringListFieldChanged((UIComboBox*)pWidget, pField);
					} else if (pField->peaComboModel) {
						if (TOK_GET_TYPE(pField->type) == TOK_REFERENCE_X) {
							mef_comboModelListReferenceFieldChanged((UIComboBox*)pWidget, pField);
						} else {
							mef_comboModelListStringFieldChanged((UIComboBox*)pWidget, pField);
						}
					} else {
						mef_comboStringFieldChanged((UIComboBox*)pWidget, pField);
					}
				}
			xcase kMEFieldType_BooleanCombo:
				mef_booleanComboFieldChanged((UIComboBox*)pWidget, pWidget ? ui_ComboBoxGetSelectedEnum((UIComboBox*)pWidget) : 0, pField);
			xcase kMEFieldType_Check:
				mef_checkFieldChanged((UICheckButton*)pWidget, pField);
			xcase kMEFieldType_FileName:
				mef_fileNameFieldChanged((UIFileNameEntry*)pWidget, pField);
			xcase kMEFieldType_DisplayMessage:
			acase kMEFieldType_Message:
				mef_messageFieldChanged((UIMessageEntry*)pWidget, pField);
			xcase kMEFieldType_Slider:
				mef_sliderFieldChanged((UISlider*)pWidget, true, pField);
			xcase kMEFieldType_SliderText:
				mef_sliderTextFieldChanged((UISliderTextEntry*)pWidget, true, pField);
			xcase kMEFieldType_Spinner:
				mef_spinnerFieldChanged((UISpinnerEntry*)pWidget, pField);
			xcase kMEFieldType_MultiSpinner:
				mef_multiSpinnerFieldChanged((UIMultiSpinnerEntry*)pWidget, pField);
			xcase kMEFieldType_MultiTexture:
				mef_multiTextureFieldChanged((UITextureEntry*)pWidget, pField);
			xcase kMEFieldType_Texture:
				mef_textureFieldChanged((UITextureEntry*)pWidget, pField);
			xcase kMEFieldType_Color:
				mef_colorFieldChanged((UIColorButton*)pWidget, true, pField);
			xcase kMEFieldType_Hue:
				mef_hueFieldChanged((UIColorSlider*)pWidget, pField);
			xcase kMEFieldType_FlagCombo:
				{
//					if (TOK_GET_TYPE(pField->type) == TOK_FLAGS_X) {
					if (pField->bIsIntFlags) {
						mef_flagFieldChanged((UIComboBox*)pWidget, pField);
					} else if (TOK_GET_TYPE(pField->type) == TOK_STRUCT_X) {
						if(pField->pEnum)
							mef_comboFieldIntArrayStructMultiChanged((UIComboBox*)pWidget, 0, pField);
						else if(pField->pchGlobalDictName || pField->pchDictName)
							mef_comboFieldRefArrayStructMultiChanged((UIComboBox*)pWidget, pField);
					} else { // int array
						mef_comboFieldMultiChanged((UIComboBox*)pWidget, 0, pField);
					}
				}
		}
	}
}

static void mef_AssertOnWidgetFree(UIWidget *pWidget)
{
	devassertmsg(0, "Someone directly freed a widget owned by a MEField.  This is not allowed.  The widget should only be freed by properly destroying the MEField.  This is crashing now on purpose because continuing will result in a later crash because memory would be corrupted.  Please comment on this crash to help replicate the problem so the fix can be made.");
}

static UIWidget *mef_createWidget(MEField *pField, F32 x, F32 y, F32 width, F32 height, bool bSafe)
{
	UIWidget *pWidget = NULL;

	// Creates the text area, sets up its callback, and then uses the callback to init
	if (pField->eType >= MIN_EX_TYPE) {
		pWidget = eaExternTypes[pField->eType-MIN_EX_TYPE]->cbCreate(pField, x, y, width, height);
	} else {
		switch( pField->eType )
		{
			case kMEFieldType_Combo:	
			{
				UIComboBox *pCombo = NULL;
				if (pField->pEnum) {
					pCombo = ui_ComboBoxCreateWithEnum(x, y, width, pField->pEnum, mef_comboIntFieldChanged, pField);
					ui_ComboBoxRemoveEnumDuplicates(pCombo);
					if( !pField->bDontSortEnums )
						ui_ComboBoxSortEnum(pCombo);
				} else if (pField->pchDictName) {
					pCombo = ui_ComboBoxCreateWithDictionary(x, y, width, pField->pchDictName, pField->pDictTable, pField->pchDictField);
					ui_ComboBoxSetSelectedCallback(pCombo, mef_comboStringFieldChanged, pField);
				} else if (pField->pchGlobalDictName) {
					pCombo = ui_ComboBoxCreateWithGlobalDictionary(x, y, width, pField->pchGlobalDictName, pField->pchDictField);
					ui_ComboBoxSetSelectedCallback(pCombo, mef_comboStringFieldChanged, pField);
				} else if (pField->peaComboStrings) {
					pCombo = ui_ComboBoxCreate(x, y, width, NULL, pField->peaComboStrings, NULL);
					ui_ComboBoxSetSelectedCallback(pCombo, mef_comboStringListFieldChanged, pField);
				} else if (pField->peaComboModel) {
					pCombo = ui_ComboBoxCreate(x, y, width, pField->pDictTable, pField->peaComboModel, pField->pchDictDisplayField);
					if (TOK_GET_TYPE(pField->type) == TOK_REFERENCE_X) {
						ui_ComboBoxSetSelectedCallback(pCombo, mef_comboModelListReferenceFieldChanged, pField);
					} else {
						ui_ComboBoxSetSelectedCallback(pCombo, mef_comboModelListStringFieldChanged, pField);
					}
				} else {
					assertmsg(0, "Combo must have an enum, dictionary, or list of strings");
				}

				pWidget = UI_WIDGET(pCombo);
				pCombo->drawSelected = true;
			}
			xcase kMEFieldType_FlagCombo:
			{
//				if(TOK_GET_TYPE(pField->type) == TOK_FLAGS_X)
				if (pField->bIsIntFlags)
				{
					UIFlagComboBox *pFlagBox = ui_FlagComboBoxCreate(pField->pEnum);
					ui_FlagComboBoxRemoveDuplicates(pFlagBox);
					ui_FlagComboBoxSort(pFlagBox);
					pWidget = UI_WIDGET(pFlagBox);
					pFlagBox->widget.width = width;
					ui_ComboBoxSetSelectedCallback(pFlagBox, mef_flagFieldChanged, pField);
					ui_WidgetSetPosition(pWidget, x, y);
				}
				else if(TOK_GET_TYPE(pField->type) == TOK_STRUCT_X)
				{
					if(pField->pEnum)
					{
						UIComboBox *pCombo = ui_ComboBoxCreateWithEnum(x, y, width, pField->pEnum, mef_comboFieldIntArrayStructMultiChanged, pField);
						ui_ComboBoxRemoveEnumDuplicates(pCombo);
						if( !pField->bDontSortEnums )
							ui_ComboBoxSortEnum(pCombo);
						pWidget = UI_WIDGET(pCombo);
						ui_ComboBoxSetMultiSelect(pCombo, true);
					} else if(pField->pchGlobalDictName) {
						UIComboBox *pCombo = ui_ComboBoxCreateWithGlobalDictionary(x, y, width, pField->pchGlobalDictName, pField->pchDictDisplayField);
						ui_ComboBoxSetSelectedCallback(pCombo,mef_comboFieldRefArrayStructMultiChanged,pField);
						pWidget = UI_WIDGET(pCombo);
						ui_ComboBoxSetMultiSelect(pCombo, true);
					} else if(pField->pchDictName) {
						UIComboBox *pCombo = ui_ComboBoxCreateWithDictionary(x, y, width, pField->pchDictName, pField->pDictTable, pField->pchDictDisplayField);
						pWidget = UI_WIDGET(pCombo);
						ui_ComboBoxSetMultiSelect(pCombo, true);
					}
				}
				else
				{
					UIComboBox *pCombo = ui_ComboBoxCreateWithEnum(x, y, width, pField->pEnum, mef_comboFieldMultiChanged, pField);
					ui_ComboBoxRemoveEnumDuplicates(pCombo);
					if( !pField->bDontSortEnums )
						ui_ComboBoxSortEnum(pCombo);
					pWidget = UI_WIDGET(pCombo);
					ui_ComboBoxSetMultiSelect(pCombo, true);
				}

			}
			xcase kMEFieldType_BooleanCombo:
			{
				UIComboBox *pCombo = ui_ComboBoxCreateWithEnum(x, y, width, MEFieldBooleanEnum, mef_booleanComboFieldChanged, pField);
				pWidget = UI_WIDGET(pCombo);
			}
			xcase kMEFieldType_Check:
			{
				UICheckButton *pCheck = ui_CheckButtonCreate(x, y, " ", false);

				// Creates the check button, sets up its callback, and then uses the callback to init
				pWidget = UI_WIDGET(pCheck);
				pCheck->widget.width = width;
				ui_CheckButtonSetToggledCallback(pCheck, mef_checkFieldChanged, pField);
			}
			xcase kMEFieldType_TextArea:
			{
				UITextArea *pTextArea = ui_TextAreaCreate("");
				pWidget = UI_WIDGET(pTextArea);
				ui_WidgetSetDimensionsEx(pWidget, 1.f, 1.f, UIUnitPercentage, UIUnitPercentage);
				if(x || y)
					ui_WidgetSetPosition(pWidget, x, y);
				ui_TextAreaSetChangedCallback(pTextArea, mef_textFieldChanged, pField);
				ui_TextAreaSetFinishedCallback(pTextArea, mef_textFieldFinished, pField);
			}
			xcase kMEFieldType_EMPicker:
			{
				UIButton *pButton = ui_ButtonCreate("None", x, y, mef_PickerButtonClicked, pField);
				pWidget = UI_WIDGET(pButton);
			}
			xcase kMEFieldType_TextEntry:
			case kMEFieldType_ValidatedTextEntry:
			case kMEFieldType_MultiText:
			case kMEFieldType_SMFTextEntry:
			{
				UITextEntry *pText;
				bool bMultiSelect = ((TOK_GET_TYPE(pField->type) == TOK_STRING_X) && (pField->type & TOK_EARRAY)) ||
									((TOK_GET_TYPE(pField->type) == TOK_STRUCT_X) && (pField->type & TOK_EARRAY));
				if(pField->arrayIndex >= 0)
					bMultiSelect = false;
				if (pField->pEnum) {
					pText = ui_TextEntryCreateWithEnumCombo("", x, y, pField->pEnum, true, true, (pField->eType == kMEFieldType_ValidatedTextEntry), !bMultiSelect);
					if (bMultiSelect) {
						ui_ComboBoxSetMultiSelect(pText->cb, 1);
					}
				} else if (pField->peaComboStrings) {
					pText = ui_TextEntryCreateWithStringCombo("", x, y, pField->peaComboStrings, true, true, (pField->eType == kMEFieldType_ValidatedTextEntry), !bMultiSelect);
					if (bMultiSelect) {
						ui_ComboBoxSetMultiSelect(pText->cb, 1);
					}
				} else if (pField->peaComboModel) {
					pText = ui_TextEntryCreateWithObjectCombo("", x, y, pField->peaComboModel, pField->pDictTable, pField->pchDictDisplayField, true, true, (pField->eType == kMEFieldType_ValidatedTextEntry), !bMultiSelect);
					if (bMultiSelect) {
						ui_ComboBoxSetMultiSelect(pText->cb, 1);
					}
				} else if (pField->pchDictName) {
					pText = ui_TextEntryCreateWithDictionaryCombo("", x, y, pField->pchDictName, pField->pDictTable, pField->pchDictField, true, true, (pField->eType == kMEFieldType_ValidatedTextEntry), !bMultiSelect);
					if (bMultiSelect) {
						ui_ComboBoxSetMultiSelect(pText->cb, 1);
					}
				} else if (pField->pchGlobalDictName) {
					pText = ui_TextEntryCreateWithGlobalDictionaryCombo("", x, y, pField->pchGlobalDictName, pField->pchDictField, true, true, (pField->eType == kMEFieldType_ValidatedTextEntry), !bMultiSelect);
					if (bMultiSelect) {
						ui_ComboBoxSetMultiSelect(pText->cb, 1);
					}
				} else if (pField->eType == kMEFieldType_MultiText) {
					pText = ui_TextEntryCreateWithTextArea("", x, y);
				} else if (pField->eType == kMEFieldType_SMFTextEntry) {
					pText = ui_TextEntryCreateWithSMFView("", x, y);
				} else {
					pText = ui_TextEntryCreate("", x, y);
				}
				pWidget = UI_WIDGET(pText);

				if(!(pField->type & TOK_EARRAY) && !pField->pEnum)
				{
					if (TOK_GET_TYPE(pField->type) == TOK_F32_X) {
						ui_TextEntrySetFloatOnly(pText);
					} else if (TOK_GET_TYPE(pField->type) == TOK_INT_X) {
						ui_TextEntrySetIntegerOnly(pText);
					}
				}

				pText->widget.width = width;
				pText->pchIndexSeparator = pField->pchIndexSeparator;
				ui_TextEntrySetChangedCallback(pText, mef_textFieldChanged, pField);
				ui_TextEntrySetFinishedCallback(pText, mef_textFieldFinished, pField);
				if (pField->type & TOK_POOL_STRING) {
					ui_TextEntrySetValidateCallback(pText, mef_validatePooledString, NULL);
				}
			}
			xcase kMEFieldType_FileName:
			{
				UIFileNameEntry *pEntry = ui_FileNameEntryCreate("", NULL, "defs", (char *)pField->pchDictField, ".replaceme", UIBrowseExisting);
				pWidget = UI_WIDGET(pEntry);
				ui_FileNameEntrySetChangedCallback(pEntry, mef_fileNameFieldChanged, pField);
			}
			xcase kMEFieldType_DisplayMessage:
			acase kMEFieldType_Message:
			{
				UIMessageEntry *pEntry = ui_MessageEntryCreate(StructCreate(parse_Message),0,0,100);
				pWidget = UI_WIDGET(pEntry);
				ui_MessageEntrySetCanEditKey(pEntry, false);
				ui_MessageEntrySetChangedCallback(pEntry, mef_messageFieldChanged, pField);
				ui_MessageEntrySetPreChangedCallback(pEntry, mef_messageFieldPreChanged, pField);
			}
			xcase kMEFieldType_Slider:
			{
				UISlider *pSlider = ui_SliderCreate(0, 0, 1, 0, 1, 0);
				ui_SliderSetPolicy(pSlider, UISliderContinuous);
				pWidget = UI_WIDGET(pSlider);
				ui_SliderSetChangedCallback(pSlider, mef_sliderFieldChanged, pField);
			}
			xcase kMEFieldType_SliderText:
			{
				UISliderTextEntry *pEntry = ui_SliderTextEntryCreate("", 0, 1.0, x, y, width);
				ui_SliderSetPolicy(pEntry->pSlider, UISliderContinuous);
				pWidget = UI_WIDGET(pEntry);
				ui_SliderTextEntrySetChangedCallback(pEntry, mef_sliderTextFieldChanged, pField);
			}
			xcase kMEFieldType_Texture:
			{
				UITextureEntry *pEntry = ui_TextureEntryCreate("", NULL, false);
				pWidget = UI_WIDGET(pEntry);
				ui_TextureEntrySetChangedCallback(pEntry, mef_textureFieldChanged, pField);
				ui_TextureEntrySetFileStartDir(pEntry, pField->pchDictField);
			}
			xcase kMEFieldType_MultiTexture:
			{
				UITextureEntry *pEntry = ui_TextureEntryCreate("", (char***)TokenStoreGetEArray(pField->pTable, pField->column, pField->pNew, NULL), true);
				pWidget = UI_WIDGET(pEntry);
				ui_TextureEntrySetChangedCallback(pEntry, mef_multiTextureFieldChanged, pField);
				ui_TextureEntrySetFileStartDir(pEntry, pField->pchDictField);
			}
			xcase kMEFieldType_Color:
			{
				UIColorButton *pColor = ui_ColorButtonCreate(x, y, zerovec4);
				pColor->liveUpdate = true;
				pColor->noAlpha = true;
				pWidget = UI_WIDGET(pColor);
				ui_ColorButtonSetChangedCallback(pColor, mef_colorFieldChanged, pField);
			}
			xcase kMEFieldType_Hue:
			{
				Vec3 slider_vec_min = {0.0f, 1.0f, 1.0f}, slider_vec_max = {360.0f, 1.0f, 1.0f};
				UIColorSlider *pColorSlider = ui_ColorSliderCreate(0, 0, 1, slider_vec_min, slider_vec_max, true);
				pWidget = UI_WIDGET(pColorSlider);
				ui_ColorSliderSetChangedCallback(pColorSlider, mef_hueFieldChanged, pField);
			}
			xcase kMEFieldType_Spinner:
			{
				UISpinnerEntry *pEntry = NULL;
				if (!(pField->type & TOK_EARRAY) && !pField->pEnum)
				{
					if (TOK_GET_TYPE(pField->type) == TOK_F32_X)
					{
						pEntry = ui_SpinnerEntryCreate(-1e9, 1e9, 0, 0, true);
					}
					else if (TOK_GET_TYPE(pField->type) == TOK_INT_X)
					{
						pEntry = ui_SpinnerEntryCreate(-1e9, 1e9, 0, 0, false);
					}
					else
					{
						devassertmsg(1, "Attempting to create a spinner without correctly handling the type, a programmer needs to add the missing case");
					}
				}
				else
				{
					devassertmsg(1, "Spinner created with a type that is neither integer nor float.");
				}
				ANALYSIS_ASSUME(pEntry);
				pWidget = UI_WIDGET(pEntry);
				ui_SpinnerEntrySetCallback(pEntry, mef_spinnerFieldChanged, pField);
			}
			xcase kMEFieldType_MultiSpinner:
			{
				UIMultiSpinnerEntry *pEntry = NULL;
				if (!(pField->type & TOK_EARRAY) && !pField->pEnum)
				{
					if (TOK_GET_TYPE(pField->type) == TOK_F32_X ||
						TOK_GET_TYPE(pField->type) == TOK_QUATPYR_X)
					{
						pEntry = ui_MultiSpinnerEntryCreate(-1e9, 1e9, 0, 0, pField->arraySize, true);
					}
					else if (TOK_GET_TYPE(pField->type) == TOK_INT_X)
					{
						pEntry = ui_MultiSpinnerEntryCreate(-1e9, 1e9, 0, 0, pField->arraySize, false);
					}
					else
					{
						devassertmsg(1, "Attempting to create a multi-spinner without correctly handling the type, a programmer needs to add the missing case");
					}
				}
				else
				{
					devassertmsg(1, "Multi-spinner created with a type that is neither integer nor float.");
				}
				ANALYSIS_ASSUME(pEntry);
				pWidget = UI_WIDGET(pEntry);
				ui_MultiSpinnerEntrySetCallback(pEntry, mef_multiSpinnerFieldChanged, pField);			
			}
		}
	}

	if (pWidget) {
		char buf[1024];
		sprintf(buf, "MultiField = %s / %s", pField->pTable ? pField->pTable->name : "NoTable", pField->pchFieldName ? pField->pchFieldName : "NoField");
		if( !bDisableContextMenu ) {
			// Set right click menu for the widget
			ui_WidgetSetContextCallback(pWidget, mef_displayMenu, pField);
		}

		ui_SetChanged(pWidget, pField->bDirty);
		ui_SetInherited(pWidget, pField->bParented);
		ui_WidgetSetName(pWidget, buf);
		if (bSafe) {
			ui_WidgetSetFreeCallback(pWidget, mef_AssertOnWidgetFree);
		}
	}

	return pWidget;
}


//----------------------------------------------------------------------------------
// Public Functions
//----------------------------------------------------------------------------------

// SCARY SCARY NOTE: DO NOT SET ARRAY INDEX TO 0 ASSUMING IT IS SAFE!
//
// If you are using a single field to represent a whole EArray, set
// arrayIndex to -1.  If you are using a single field to represent a
// single element in an EArray, set arrayIndex to >= 0.
//
// If you get this wrong, fields will change in odd ways involving
// spaces.
MEField *MEFieldCreate(MEFieldType eType,  
					   void *pOld, void *pNew, ParseTable *pTable, const char *pchField, void *pParent,
					   void *pRootOld, void *pRootNew, ParseTable *pRootTable, char *pcRootPath,
					   const char *pchDictName, ParseTable *pDictTable, const char *pchDictField,
					   const char *pchDictDisplayField, const char *pchGlobalDictName, const char *pchAlias, bool bLabel,
					   cUIModel peaComboStrings, cUIModel peaComboModel, StaticDefineInt *pEnum, void *pExtensionData,
					   int arrayIndex, int x, int y, int width, const char* pchIndexSeparator)
{
	MEField *pField = mef_alloc(pOld,pNew,pTable,pchField,pParent,pRootOld,pRootNew,pRootTable,pcRootPath,
								   eType,pEnum,pExtensionData,pchDictName,pDictTable,pchDictField,pchDictDisplayField,
								   pchGlobalDictName,pchAlias,bLabel,arrayIndex, &x,&y,&width);
	if(!pField)
		return NULL;

	pField->bUpdate = true;
	pField->peaComboStrings = peaComboStrings;
	pField->peaComboModel = peaComboModel;
	pField->pchIndexSeparator = pchIndexSeparator;
	mef_updateField(pField, pField->pUIWidget, 0);

	return pField;
}

MEField *MEFieldCreateSimple(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField)
{
	StaticDefineInt *pEnum = NULL;
	return MEFieldCreate(eType, pOld, pNew, pTable, pchField, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
						 NULL, NULL, NULL, NULL, false , NULL, NULL, NULL, NULL, -1, 0, 0, 0, NULL);
}

MEField *MEFieldCreateSimpleEnum(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, StaticDefineInt *pEnum)
{
	return MEFieldCreate(eType, pOld, pNew, pTable, pchField, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
						 NULL, NULL, NULL, NULL, false, NULL, NULL, pEnum, NULL, -1, 0, 0, 0, NULL);
}

MEField *MEFieldCreateSimpleExpression(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, ExprContext *pExprContext)
{
	return MEFieldCreate(eType, pOld, pNew, pTable, pchField, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
						 NULL, NULL, NULL, NULL, false, NULL, NULL, NULL, pExprContext, -1, 0, 0, 0, NULL);
}

MEField *MEFieldCreateSimpleDictionary(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, const char *pchDictName, ParseTable *pDictTable, const char *pchDictField)
{
	return MEFieldCreate(eType, pOld, pNew, pTable, pchField, NULL, NULL, NULL, NULL, NULL, pchDictName, pDictTable,
						 pchDictField, NULL, NULL, NULL, false, NULL, NULL, NULL, NULL, -1, 0, 0, 0, NULL);
}

MEField *MEFieldCreateSimpleGlobalDictionary(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, const char *pchDictName, const char *pchDictField)
{
	return MEFieldCreate(eType, pOld, pNew, pTable, pchField, NULL, NULL, NULL, NULL, NULL, NULL, parse_ResourceInfo,
						 pchDictField, NULL, pchDictName, NULL, false, NULL, NULL, NULL, NULL, -1, 0, 0, 0, NULL);
}

MEField *MEFieldCreateSimpleDataProvided(MEFieldType eType, void *pOld, void *pNew, ParseTable *pTable, const char *pchField, ParseTable *pComboParseTable, cUIModel peaComboModel, const char *pchComboField)
{
	return MEFieldCreate(eType, pOld, pNew, pTable, pchField, NULL, NULL, NULL, NULL, NULL, NULL, pComboParseTable,
						 pchComboField, NULL, NULL, NULL, false, NULL, peaComboModel, NULL, NULL, -1, 0, 0, 0, NULL);
}


void MEFieldAddAlternatePath(MEField *pField, char *pcAltPath)
{
	int i;

	eaPush(&pField->eaRootPaths, strdup(pcAltPath));

	if (pField->pParent) {
		pField->bParented = true;
		for(i=eaSize(&pField->eaRootPaths)-1; i>=0; --i) {
			if (StructInherit_GetOverrideType(pField->pRootTable, pField->pRootNew, pField->eaRootPaths[i]) != OVERRIDE_NONE) {
				pField->bParented = false;
				break;
			}
		}
		mef_checkIfDirty(pField, NULL);
	}
}


void MEFieldDestroy(MEField *pField)
{
	// Free all the widgets for the field
	if (pField->pUIWidget) {
		ui_WidgetSetFreeCallback(pField->pUIWidget, NULL);
		ui_WidgetQueueFree(pField->pUIWidget);
	}
	if (pField->pUILabel) {
		ui_WidgetSetFreeCallback((UIWidget*)pField->pUILabel, NULL);
		ui_WidgetQueueFree((UIWidget*)pField->pUILabel);
	}
	eaDestroyEx(&pField->eaRootPaths, NULL);

	// Free the memory for the field itself
	free(pField);
}


void MEFieldAddToParent(MEField *pField, UIWidget *pParentWidget, F32 x, F32 y)
{
	// Late create the widget here
	if (!pField->pUIWidget) {
		pField->pUIWidget = mef_createWidget(pField, x, y, x, y, true);
	}

	// Force refresh of the control's data when displaying it
	pField->bUpdate = true;
	mef_updateField(pField, pField->pUIWidget, 0);

	// Bind the widget as requested and give it focus
	ui_WidgetSetPosition(pField->pUIWidget, x, y);
	ui_WidgetAddChild(pParentWidget, pField->pUIWidget);
}

void MEFieldAddToParentPriority(MEField *pField, UIWidget *pParentWidget, F32 x, F32 y, U8 priority)
{
	// Late create the widget here
	if (!pField->pUIWidget) {
		pField->pUIWidget = mef_createWidget(pField, x, y, x, y, true);
	}

	// Force refresh of the control's data when displaying it
	pField->bUpdate = true;
	mef_updateField(pField, pField->pUIWidget, 0);

	// Bind the widget as requested and give it focus
	ui_WidgetSetPosition(pField->pUIWidget, x, y);
	pField->pUIWidget->priority = priority;
	ui_WidgetAddChild(pParentWidget, pField->pUIWidget);
}


void MEFieldDisplay(MEField *pField, UIWidget *pParentWidget, CBox *pBox)
{
	// Late create the widget here
	if (!pField->pUIWidget) {
		pField->pUIWidget = mef_createWidget(pField, pBox->left, pBox->top, pBox->right - pBox->left, pBox->bottom - pBox->top, true);
	}

	// Force refresh of the control's data when displaying it
	pField->bUpdate = true;
	mef_updateField(pField, pField->pUIWidget, 0);

	// Bind the widget as requested and give it focus
	ui_WidgetSetCBox(pField->pUIWidget, pBox);
	ui_WidgetAddChild(pParentWidget, pField->pUIWidget);
	ui_SetFocus(pField->pUIWidget);
}


void MEFieldDismiss(MEField *pField, UIWidget *pParentWidget)
{
	// Hide the widget
	ui_WidgetRemoveChild(pParentWidget,pField->pUIWidget);

	// Release the widget
	ui_WidgetSetFreeCallback(pField->pUIWidget, NULL);
	ui_WidgetQueueFree(pField->pUIWidget);
	pField->pUIWidget = NULL;
}


void MEFieldShowMenu(MEField *pField)
{
	if (pField)
		mef_displayMenu(pField->pUIWidget, pField);
}


void MEFieldShowMultiFieldMenu(MEField **eaFields)
{
	int count = eaSize(&eaFields);
	if (!count) {
		return;
	} else if (count == 1) {
		MEFieldShowMenu(eaFields[0]);
	} else {
		mef_displayMultiMenu(eaFields);
	}
}


static void mef_updateFieldParented(MEField *pField, UIWidget *pWidget, bool bParented)
{
	bool bOldParented = pField->bParented;

	mef_updateParented(pField, pWidget, bParented, 1);
	mef_updateField(pField, pWidget, 0);

	// Call changed callback if present
	if (pField->changedCallback && (bOldParented != pField->bParented)) {
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}
}

void MEUpdateFieldParented(MEField *pField, bool bParented)
{
	mef_updateFieldParented(pField, pField->pUIWidget, bParented);
}


void MEUpdateFieldDirty(MEField *pField, bool bDirty)
{
	bool bOldDirty = pField->bDirty;

	mef_updateDirty(pField, NULL, bDirty);
	mef_updateField(pField, pField->pUIWidget, 0);

	// Call changed callback if present
	if (pField->changedCallback && (bOldDirty != pField->bDirty)) {
		pField->changedCallback(pField, 1, pField->pChangeUserData);
	}
}


void MEFieldSetParent(MEField *pField, void *pNewParent)
{
	void *pOldParent;

	pOldParent = pField->pParent;
	pField->pParent = pNewParent;

	if (!pNewParent) {
		pField->bParented = 0;
	} else if (!pOldParent && pNewParent) {
		// Compare field and be parented automatically if field value is the same
		if (!pField->bNotParentable && (mef_compareFields(pField, pField->pNew, pField->pParent) == 0)) {
			pField->bParented = 1;
		} else {
			pField->bParented = 0;
		}
	}
	// else just leave parenting the same as before

	// Refresh from underlying data
	pField->bUpdate = true;
	mef_updateField(pField, pField->pUIWidget, 0);
}


void MEFieldGetString(MEField *pField, char **estr)
{
	if (pField->bNotApplicable) {
		// Treat not applicable fields as being blank
		return;
	}
	if (pField->eType >= MIN_EX_TYPE) {
		eaExternTypes[pField->eType - MIN_EX_TYPE]->cbGetString(pField, estr);
	} else {
		switch( pField->eType )
		{
			xcase kMEFieldType_Check:
			case kMEFieldType_BooleanCombo:
			{
				// CheckBox requires special logic
				bool bValue = mef_getBooleanValue(pField->pTable, pField->column, pField->pNew);
				estrPrintf(estr, "%s", bValue ? "TRUE" : "FALSE");
			}
			xcase kMEFieldType_MultiTexture:
			case kMEFieldType_Texture:
			{
				// Texture requires special logic
				const char *pcTexName = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
				if (pcTexName) {
					estrPrintf(estr, "%s",  pcTexName);
				}
			}
			xcase kMEFieldType_Color:
			{
				Vec4 vColor;
				mef_getColorValue(pField->pTable, pField->column, pField->pNew, vColor);
				estrPrintf(estr, "%f %f %f %f", vColor[0], vColor[1], vColor[2], vColor[3]);
			}
			xcase kMEFieldType_DisplayMessage:
			{
				// Message requires special logic
				const DisplayMessage *pMsg = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, 0);
				if (pMsg && pMsg->pEditorCopy && pMsg->pEditorCopy->pcDefaultString) {
					estrPrintf(estr, "%s", pMsg->pEditorCopy->pcDefaultString);
				}
			}
			xcase kMEFieldType_Message:
			{
				// Message requires special logic
				const Message *pMsg = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, 0);
				if (pMsg && pMsg->pcDefaultString) {
					estrPrintf(estr, "%s", pMsg->pcDefaultString);
				}
			}
			xdefault:
			{
				char token[4096];
				if ((pField->eType == kMEFieldType_FlagCombo) && (TOK_GET_TYPE(pField->type) == TOK_STRUCT_X)) {
					// An int array inside a substruct requires a bit of trickery to get to display because
					// the substruct may not exist
					void *pStruct;

					pStruct = TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
					if (pStruct) {
						ParseTable *pStructTable;
						StructGetSubtable(pField->pTable, pField->column, pField->pNew, 0, &pStructTable, NULL);
						if(	TokenToSimpleString(pStructTable, 2, pStruct, token, 4096, 0) ) {
							estrPrintf(estr, "%s", token);
						}
					}
				} else {
					// All primitive fields can use generic logic
					if(	TokenToSimpleString(pField->pTable, pField->column,pField->pNew, token, 4096, 0) ) {
						estrPrintf(estr, "%s", token);
					} 
				}
			}
		}
	}
}


static void mef_revertField(MEField *pField, UIWidget *pWidget)
{
	// Revert parentage if required
	if (pField->pParent && pField->pRootOld) {
		if (StructInherit_GetParentName(pField->pRootTable, pField->pRootOld)) {
			// Original had a parent, so use parenting from there
			int i;
			bool bOrigParented = true;
			for(i=eaSize(&pField->eaRootPaths)-1; i>=0; --i) {
				if (StructInherit_GetOverrideType(pField->pRootTable, pField->pRootOld, pField->eaRootPaths[i]) != OVERRIDE_NONE) {
					bOrigParented = false;
					break;
				}
			}
			mef_updateParented(pField, pWidget, bOrigParented, true);
		} else {
			// Original had no parent, but we have one now, so inherit if we are allowed to
			mef_updateParented(pField, pWidget, !pField->bNotParentable, true);
		}
	}

	pField->bUpdate = false; // Make sure not to also do an update

	// Then revert the field data
	mef_updateField(pField, pWidget, true);
}


void MEFieldRevert(MEField *pField)
{
	mef_revertField(pField, pField->pUIWidget);
}


void MEFieldRefreshFromData(MEField *pField)
{
	pField->bUpdate = true;
	mef_updateField(pField, pField->pUIWidget, 0);
}


void MEFieldSetAndRefreshFromData(MEField *pField, void *pOld, void *pNew)
{
	pField->pOld = pOld;
	pField->pNew = pNew;
	pField->bUpdate = true;
	mef_updateField(pField, pField->pUIWidget, 0);
}


void MEFieldRefreshFromWidget(MEField *pField)
{
	mef_updateField(pField, pField->pUIWidget, 0);
}

void MEFieldFixupNameString(MEField *pField, const char **ppchName)
{
	if(*ppchName && strchr(*ppchName, ' ')) {
		int i;
		char pcNewName[256];
		strcpy(pcNewName, *ppchName);
		for ( i=0; i < 256 && pcNewName[i]; i++ ) {
			if(pcNewName[i] == ' ')
				pcNewName[i] = '_';
		}
		*ppchName = allocAddString(pcNewName);
		MEFieldRefreshFromData(pField);
	}
}

void MEFieldValidateInheritance(MEField *pField)
{
	if (pField->pParent && pField->pRootNew) {
		int i;
		bool bParented = true;

		// Get current inheritance tracking
		for(i=eaSize(&pField->eaRootPaths)-1; i>=0; --i) {
			if (StructInherit_GetOverrideType(pField->pRootTable, pField->pRootNew, pField->eaRootPaths[i]) != OVERRIDE_NONE) {
				bParented = false;
				break;
			}
		}

		// If inherited, make sure it is not overridden
		if (pField->bParented && !bParented) {
			StructInherit_DestroyOverride(pField->pRootTable, pField->pRootNew, pField->eaRootPaths[i]);
		}

		// If not inherited, make sure it has tracking info
		if ((!pField->bParented && bParented) || pField->bNotParentable) {
			// There is a special function for e-array fields (as long as they aren't structparams)
			if (pField->type & TOK_EARRAY && !(pField->type & TOK_STRUCTPARAM)) {
				StructInherit_CreateArrayOverride(pField->pRootTable, pField->pRootNew, pField->eaRootPaths[0]);
			} else {
				StructInherit_CreateFieldOverride(pField->pRootTable, pField->pRootNew, pField->eaRootPaths[0]);
			}
		}
	}
}

void MEFieldSetEnum(MEField* pField, StaticDefineInt* pEnum)
{
	assert( pField->pEnum );

	pField->bUpdate = true;
	pField->pEnum = pEnum;

	// Refresh UI widget, since the enum may have changed
	if( pField->pUIWidget ) {
		switch( pField->eType ) {
			xcase kMEFieldType_Combo:
				ui_ComboBoxSetEnum(pField->pUICombo, pEnum, mef_comboIntFieldChanged, pField);
				ui_ComboBoxRemoveEnumDuplicates( pField->pUICombo );
				if( !pField->bDontSortEnums )
					ui_ComboBoxSortEnum( pField->pUICombo );
				
			xcase kMEFieldType_FlagCombo:
				if( pField->bIsIntFlags ) {
					assert( false ); //< not yet implemented, not used?
				} else if (TOK_GET_TYPE(pField->type) == TOK_STRUCT_X) {
					ui_ComboBoxSetEnum(pField->pUICombo, pEnum, mef_comboFieldIntArrayStructMultiChanged, pField);
					ui_ComboBoxRemoveEnumDuplicates( pField->pUICombo );
				} else {
					ui_ComboBoxSetEnum(pField->pUICombo, pEnum, mef_comboFieldIntArrayStructMultiChanged, pField);
					ui_ComboBoxRemoveEnumDuplicates( pField->pUICombo );
				}

			xdefault:
				assert( false ); //< should be impossible to get to
		}
	}
}

void MEFieldSetComboModel(MEField* pField, cUIModel peaComboModel)
{
	assert( pField->peaComboModel );
	if( pField->peaComboModel == peaComboModel ) {
		return;
	}

	pField->bUpdate = true;
	pField->peaComboModel = peaComboModel;

	// Refresh UI widget, since the enum may have changed
	if( pField->pUIWidget ) {
		switch( pField->eType ) {
			xcase kMEFieldType_Combo:
				ui_ComboBoxSetModelNoCallback(pField->pUICombo, pField->pDictTable, peaComboModel);

			xdefault:
				assert( false ); //< should be impossible to get to
		}
	}
}


static void mef_UIDismissEditor(UIWidget *pWidget, void *pUserData)
{
	if (!pGlobalWindow) {
		return;
	}
	
	// Save the position
	EditorPrefStoreWindowPosition("MEField", "Edit Window", "Position", pGlobalWindow);

	// Hide the window
	ui_WindowHide(pGlobalWindow);

	if (pGlobalWindowField) {
		// Clear the siblings setting on the field
		eaDestroy(&pGlobalWindowField->eaSiblings);

		// Make sure any widget on the field is updated for the changes
		if (pGlobalWindowField->pUIWidget) {
			pGlobalWindowField->bUpdate = true;
			mef_updateField(pGlobalWindowField, pGlobalWindowField->pUIWidget, false);
		}
	}

	// Destroy the window
	ui_WidgetQueueFree(UI_WIDGET(pGlobalWindow));
	pGlobalWindow = NULL;
}


static bool mef_UICloseEditor(UIWindow *pWindow, void *pUserData)
{
	mef_UIDismissEditor(UI_WIDGET(pWindow), pUserData);
	return 1;
}


// Creates an edit widget that makes use of the field but is not
// the primary widget
UIWidget *MEFieldCreateEditWidget(MEField *pField)
{
	UIWidget *pWidget;

	// Pass "false" on the "bSafe" param because this is always a secondary widget that is safe to free
	pWidget = mef_createWidget(pField, 0, 0, 100, 0, false);
	if (pField->eType == kMEFieldType_TextArea) {
		ui_WidgetSetHeight(pWidget, 80);
	}

	pField->bUpdate = true;
	mef_updateField(pField, pWidget, 0);

	return pWidget;
}


// Creates the primary widget. Necessary for compatibility with the layout manager
void MEFieldCreateThisWidget(MEField *pField)
{
	pField->pUIWidget = mef_createWidget(pField, 0, 0, 100, 0, true);
	if (pField->eType == kMEFieldType_TextArea)
	{
		ui_WidgetSetHeight(pField->pUIWidget, 80);
	}

	pField->bUpdate = true;
	mef_updateField(pField, pField->pUIWidget, 0);
}


UIWindow *MEFieldOpenEditor(MEField *pField, char *pcTitle)
{
	UILabel *pLabel;
	UIButton *pButton;
	UIWidget *pWidget;

	if (!pcTitle) {
		pcTitle = "Edit Field";
	}

	if (pGlobalWindow) {
		// Close any previous version of this window
		pGlobalWindow->closeF(pGlobalWindow, pGlobalWindow->closeData);
	}

	pGlobalWindow = ui_WindowCreate(pcTitle,300,300,400,60);
	pGlobalWindow->widget.scale = (pField->fLastScale > 0) ? pField->fLastScale : 1.0;
	EditorPrefGetWindowPosition("MEField", "Edit Window", "Position", pGlobalWindow);

	pLabel = ui_LabelCreate(pField->pchLabel ? pField->pchLabel : "Field Value",10,0);
	ui_WindowAddChild(pGlobalWindow, pLabel);

	pWidget = MEFieldCreateEditWidget(pField);
	ui_WidgetSetPosition(pWidget, 20 + pLabel->widget.width, 0);
	ui_WidgetSetWidthEx(pWidget, 1.f, UIUnitPercentage);
	ui_WindowAddChild(pGlobalWindow, pWidget);

	if (pField->peaComboModel && pField->pUICombo && (pField->eType == kMEFieldType_Combo)) {
		// This is a fragile hack to re-direct the internals of the combo box
		UIComboBox *pUICombo = (UIComboBox*)pWidget;
		pUICombo->drawData = (char*)pField->pchDictDisplayField;
		pUICombo->pTextData = (char*)pField->pchDictDisplayField;
		pUICombo->bUseMessage = pField->bIsMessage;
		if (pUICombo->pPopupList) {
			ui_ListColumnSetType(pUICombo->pPopupList->eaColumns[0], pField->bIsMessage ? UIListPTMessage : UIListPTName, (intptr_t)pField->pchDictDisplayField, NULL);
		}
	} else if (pField->peaComboModel && pField->pUIText && pField->pUIText->cb && 
				((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry))) {
		// This is a fragile hack to re-direct the internals of the combo box
		UITextEntry *pUIText = (UITextEntry*)pWidget;
		pUIText->cb->drawData = (char*)pField->pchDictDisplayField;
		pUIText->cb->pTextData = (char*)pField->pchDictDisplayField;
		if (pUIText->cb) {
			pUIText->cb->bUseMessage = pField->bIsMessage;
		}
		if (pUIText->cb->pPopupFilteredList) {
			ui_ListColumnSetType(pUIText->cb->pPopupFilteredList->pList->eaColumns[0], pField->bIsMessage ? UIListPTMessage : UIListPTName, (intptr_t)pField->pchDictDisplayField, NULL);
		}
	}

	pButton = ui_ButtonCreate("OK", 0, 0, mef_UIDismissEditor,pField);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, 10 + pWidget->height, 0.5, 0, UITopLeft);
	pButton->widget.width = 100;
	ui_WindowAddChild(pGlobalWindow,pButton);

	ui_WidgetSetHeight(UI_WIDGET(pGlobalWindow), 40 + pWidget->height);

	ui_WindowSetCloseCallback(pGlobalWindow, mef_UICloseEditor, pField);

	// Show the window
	pGlobalWindowField = pField;
	ui_WindowPresent(pGlobalWindow);
	return pGlobalWindow;
}


UIWindow *MEFieldOpenMultiEditor(MEField **eaFields, char *pcTitle)
{
	int i;

	if (!eaFields || !eaSize(&eaFields)) {
		return NULL;
	}

	// Let first field take the lead, and make all other fields into its siblings
	eaFields[0]->eaSiblings = NULL;
	for(i=eaSize(&eaFields)-1; i>=1; --i) { // Note that this goes to 1 instead of 0 on purpose
		eaPush(&eaFields[0]->eaSiblings, eaFields[i]);
	}

	return MEFieldOpenEditor(eaFields[0], pcTitle);
}


void MEFieldCloseEditor(void)
{
	// Just close the open editor
	mef_UIDismissEditor(NULL, NULL);
}


bool MEFieldHasCompatibleTypes(MEField *pField1, MEField *pField2)
{
	return (pField1->eType == pField2->eType) &&
		   (pField1->type == pField2->type) &&
		   (pField1->pEnum == pField2->pEnum) &&
		   ((pField1->pchDictName == pField1->pchDictName) || (pField1->pchDictName && pField2->pchDictName && stricmp(pField1->pchDictName, pField2->pchDictName) == 0)) &&
		   ((pField1->pchGlobalDictName == pField1->pchGlobalDictName) || (pField1->pchGlobalDictName && pField2->pchGlobalDictName && stricmp(pField1->pchGlobalDictName, pField2->pchGlobalDictName) == 0)) &&
		   ((pField1->pchDictField == pField1->pchDictField) || (pField1->pchDictField && pField2->pchDictField && stricmp(pField1->pchDictField, pField2->pchDictField) == 0)) &&
		   ((pField1->pchDictDisplayField == pField1->pchDictDisplayField) || (pField1->pchDictDisplayField && pField2->pchDictDisplayField && stricmp(pField1->pchDictDisplayField, pField2->pchDictDisplayField) == 0)) &&
		   (pField1->pDictTable == pField2->pDictTable) &&
		   (pField1->peaComboStrings == pField2->peaComboStrings) &&
		   (pField1->peaComboModel == pField2->peaComboModel);
}

bool MEFieldCanOpenEMEditor(MEField *pField)
{
	const char *pDictName = NULL;

	if (pField->pchGlobalDictName)
	{
		pDictName = pField->pchGlobalDictName;
	}
	else if (pField->pDictTable)
	{
		pDictName = ParserGetTableName(pField->pDictTable);
	}

	if (pDictName)
	{
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
		if (pDictInfo)
		{
			pDictName = pDictInfo->pDictName;
			if (!emGetEditorForType(pDictName))		
				pDictName = NULL;
		}
		else
		{
			pDictName = NULL;
		}
	}

	if (pDictName)
	{
		return true;
	}

	return false;
}


void MEFieldOpenEMEditor(MEField *pField)
{
	const char *pDictName = NULL;
	char *estrName = NULL;

	estrStackCreate(&estrName);
	MEFieldGetString(pField, &estrName);

	if (pField->pchGlobalDictName)
	{
		pDictName = pField->pchGlobalDictName;
	}
	else if (pField->pDictTable)
	{
		pDictName = ParserGetTableName(pField->pDictTable);
	}

	if (pDictName)
	{
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
		if (pDictInfo)
		{
			pDictName = pDictInfo->pDictName;
			if (!emGetEditorForType(pDictName))		
				pDictName = NULL;
		}
		else
		{
			pDictName = NULL;
		}
	}

	if (pDictName)
	{
		emOpenFileEx(estrName, pDictName);
	}

	estrDestroy(&estrName);
}

bool MEFieldCanPreview(MEField *pField)
{
	const char *pDictName = NULL;

	if (pField->pchGlobalDictName)
	{
		pDictName = pField->pchGlobalDictName;
	}
	else if (pField->pDictTable)
	{
		pDictName = ParserGetTableName(pField->pDictTable);
	}

	if (pDictName)
	{
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
		if (pDictInfo)
		{
			pDictName = pDictInfo->pDictName;
		}
		else
		{
			pDictName = NULL;
		}
	}

	if (pDictName)
	{
		return true;
	}

	return false;
}

static const char* MEFieldGetDictName(MEField *pField)
{
	const char *pDictName = NULL;

	if (pField->pchGlobalDictName)
	{
		pDictName = pField->pchGlobalDictName;
	}
	else if (pField->pDictTable)
	{
		pDictName = ParserGetTableName(pField->pDictTable);
	}

	if (pDictName)
	{
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
		if (pDictInfo)
		{
			pDictName = pDictInfo->pDictName;			
		}
		else
		{
			pDictName = NULL;
		}
	}

	return pDictName;
}

void MEFieldOpenFile(MEField *pField)
{
	ResourceInfo *pInfo;
	const char *pDictName = MEFieldGetDictName(pField);
	char *estrName = NULL;
	estrStackCreate(&estrName);
	MEFieldGetString(pField, &estrName);

	pInfo = resGetInfo(pDictName, estrName);
	if(pInfo && pInfo->resourceLocation) {
		emuOpenFile(pInfo->resourceLocation);
	}

	estrDestroy(&estrName);
}

void MEFieldOpenFolder(MEField *pField)
{
	ResourceInfo *pInfo;
	const char *pDictName = MEFieldGetDictName(pField);
	char *estrName = NULL;
	estrStackCreate(&estrName);
	MEFieldGetString(pField, &estrName);

	pInfo = resGetInfo(pDictName, estrName);
	if(pInfo && pInfo->resourceLocation) {
		emuOpenContainingDirectory(pInfo->resourceLocation);
	}

	estrDestroy(&estrName);
}

void MEFieldPreview(MEField *pField)
{
	const char *pDictName = NULL;
	char *estrName = NULL;

	estrStackCreate(&estrName);
	MEFieldGetString(pField, &estrName);

	if (pField->pchGlobalDictName)
	{
		pDictName = pField->pchGlobalDictName;
	}
	else if (pField->pDictTable)
	{
		pDictName = ParserGetTableName(pField->pDictTable);
	}

	if (pDictName)
	{
		ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
		if (pDictInfo)
		{
			pDictName = pDictInfo->pDictName;			
		}
		else
		{
			pDictName = NULL;
		}
	}

	if (pDictName)
	{
		PreviewResource(pDictName, estrName);
	}

	estrDestroy(&estrName);
}


bool MEFieldCanValueSearch(MEField *pField)
{
	const char *pStructName = NULL;
	const char *pFieldName = NULL;
	char *estrName = NULL;	

	estrStackCreate(&estrName);
	MEFieldGetString(pField, &estrName);

	if (pField->pchRootDictName && pField->pTable)
	{
		pStructName = ParserGetTableName(pField->pTable);
		pFieldName = pField->pTable[pField->column].name;		
	}

	if (pStructName && pFieldName && estrLength(&estrName))
	{		
		estrDestroy(&estrName);
		return true;
	}

	estrDestroy(&estrName);
	return false;
}

void MEFieldValueSearch(MEField *pField)
{
	const char *pStructName = NULL;
	const char *pFieldName = NULL;
	char *estrName = NULL;

	estrStackCreate(&estrName);
	MEFieldGetString(pField, &estrName);

	if(pField->pchRootDictName && pField->pTable == parse_DisplayMessage) {	
		RequestDisplayNameSearch(pField->pchRootDictName, estrName);
		estrDestroy(&estrName);
		return;
	}

	if (pField->pchRootDictName && pField->pTable)
	{
		pStructName = ParserGetTableName(pField->pTable);
		pFieldName = pField->pTable[pField->column].name;		
	}

	if (pStructName && pFieldName && estrLength(&estrName))
	{
		char searchText[1024];
		sprintf(searchText, "%s.%s", pStructName, pFieldName);

		RequestFieldSearch(pField->pchRootDictName, searchText, estrName);
	}

	estrDestroy(&estrName);
}

void MEFieldSetChangeCallbackEX(MEField *pField, MEFieldChangeCallback cbChange, void *pUserData, bool bSkipSiblingChangedCallbacks)
{
	pField->changedCallback = cbChange;
	pField->bSkipSiblingChangedCallbacks = bSkipSiblingChangedCallbacks;
	pField->pChangeUserData = pUserData;
}

void MEFieldSetPreChangeCallback(MEField *pField, MEFieldPreChangeCallback cbPreChange, void *pUserData)
{
	pField->preChangedCallback = cbPreChange;
	pField->pPreChangeUserData = pUserData;
}


// The goal of this function is to do whatever magic we can to figure out the input parameters
// to creating an MEField from just what is in the ParseTable.  Right now this is more than a
// little incomplete and needs to be added to.
bool MEFieldGetParseTableInfo(ParseTable *pParseTable, int col, MEFieldType *pFieldType, 
							  char **ppDictName, ParseTable **ppDictParseTable, char **ppDictFieldName, char **ppGlobalDictName,
							  StaticDefineInt **ppEnum)
{
	*pFieldType = -1;
	*ppDictName = NULL;
	*ppGlobalDictName = NULL;
	*ppDictParseTable = NULL;
	*ppDictFieldName = NULL;
	*ppEnum = NULL;

	if (TOK_GET_TYPE(pParseTable[col].type) == TOK_STRUCT_X) {
		if ((pParseTable[col].subtable == parse_Expression) || (pParseTable[col].subtable == parse_Expression_StructParam)) {
			*pFieldType = kMEFieldTypeEx_Expression;
			return true;
		} else if (pParseTable[col].subtable == parse_WorldGameActionBlock) {
			*pFieldType = kMEFieldTypeEx_GameActionBlock;
			return true;
		} else if (pParseTable[col].subtable == parse_GameEvent) {
			*pFieldType = kMEFieldTypeEx_GameEvent;
			return true;
		}
		return false;
	} else if (TOK_GET_TYPE(pParseTable[col].type) == TOK_F32_X) {
		*pFieldType = kMEFieldType_TextEntry;
		return true;
	} else if (TOK_GET_TYPE(pParseTable[col].type) == TOK_INT_X
			|| TOK_GET_TYPE(pParseTable[col].type) == TOK_INT16_X
			// U8s with subtables should become combo boxes.
			// U8s without subtables are booleans and should become boolean combo boxes.
			|| (TOK_GET_TYPE(pParseTable[col].type) == TOK_U8_X && pParseTable[col].subtable)
			) {
		if (TOK_GET_FORMAT_OPTIONS(pParseTable[col].format) == TOK_FORMAT_FLAGS && pParseTable[col].subtable)
		{
			*pFieldType = kMEFieldType_FlagCombo;
			*ppEnum = (StaticDefineInt*)pParseTable[col].subtable;
			return true;
		}
		else if (pParseTable[col].subtable) {
			if (pParseTable[col].type & TOK_EARRAY) {
				*pFieldType = kMEFieldType_FlagCombo;
			}
			else {
				*pFieldType = kMEFieldType_Combo;
			}
			*ppEnum = (StaticDefineInt*)pParseTable[col].subtable;
		} else {
			*pFieldType = kMEFieldType_TextEntry;
		}
		return true;
/*	} else if (TOK_GET_TYPE(pParseTable[col].type) == TOK_FLAGS_X) {
		if (pParseTable[col].subtable) {
			*pFieldType = kMEFieldType_FlagCombo;
			*ppEnum = (StaticDefineInt*)pParseTable[col].subtable;
			return true;
		}*/
	} else if ((TOK_GET_TYPE(pParseTable[col].type) == TOK_BOOL_X) ||
	           (TOK_GET_TYPE(pParseTable[col].type) == TOK_U8_X) ||
			   (TOK_GET_TYPE(pParseTable[col].type) == TOK_BIT)) { // Bools turn into U8 in parse table
		*pFieldType = kMEFieldType_BooleanCombo;
		return true;
	} else if ((TOK_GET_TYPE(pParseTable[col].type) == TOK_STRING_X) || (TOK_GET_TYPE(pParseTable[col].type) == TOK_REFERENCE_X)) {
		*pFieldType = kMEFieldType_TextEntry;
		if (pParseTable[col].subtable) {
			// Probably a dictionary, but we don't know the parse table or name field
			if (RefSystem_DoesDictionaryExist((char*)pParseTable[col].subtable)) {
				// Hardcode info on dictionaries we know about
				if (strcmp((char*)pParseTable[col].subtable, "PowerDef") == 0) {
					*pFieldType = kMEFieldType_ValidatedTextEntry;					
					*ppGlobalDictName = (char*)pParseTable[col].subtable;
					*ppDictParseTable = parse_ResourceInfo;
					*ppDictFieldName = "resourceName";
				} else if (strcmp((char*)pParseTable[col].subtable, "PlayerCostume") == 0) {
					*ppGlobalDictName = (char*)pParseTable[col].subtable;
					*ppDictParseTable = parse_ResourceInfo;
					*ppDictFieldName = "resourceName";
				} else if (strcmp((char*)pParseTable[col].subtable, "Message") == 0) {
					*pFieldType = kMEFieldType_Message;
				} else if (strcmp((char*)pParseTable[col].subtable, "FSM") == 0) {
					*pFieldType = kMEFieldType_ValidatedTextEntry;
					*ppDictName = (char*)pParseTable[col].subtable;
					*ppDictParseTable = parse_FSM;
					*ppDictFieldName = "Name:";
				} else {	
					ParseTable *pTable = RefSystem_GetDictionaryParseTable(pParseTable[col].subtable);

					if(pTable)
					{
						int x;
						FORALL_PARSETABLE(pTable,x)
						{
							if(pTable[x].type & TOK_KEY)
							{
								*ppGlobalDictName = (char*)pParseTable[col].subtable;
								*ppDictParseTable = parse_ResourceInfo;
								*ppDictFieldName = "resourceName";
							}
						}
					}

				}
				
			}
		}
		return true;
	}
	return false;
}

void MEFieldSetRootDictName(MEField *pField, const char *pchDictField)
{
	pField->pchRootDictName = allocAddString(pchDictField);
}

void MEFieldSetDictField(MEField *pField, const char *pchDictField, const char *pchDictDisplayField, bool bIsMessage)
{
	pField->pchDictField = pchDictField;
	if (!ParserFindColumn(pField->pDictTable, pField->pchDictField, &pField->iDictCol)) {
		Errorf("Field %s Not Found", pchDictField);
	}
	pField->pchDictDisplayField = pchDictDisplayField;
	if (!pchDictDisplayField) {
		pField->pchDictDisplayField = pField->pchDictField;
	}
	pField->bIsMessage = bIsMessage;

	if (pField->peaComboModel && pField->pUICombo && (pField->eType == kMEFieldType_Combo)) {
		// This is a fragile hack to re-direct the internals of the combo box
		pField->pUICombo->drawData = (char*)pchDictDisplayField;
		pField->pUICombo->pTextData = (char*)pchDictDisplayField;
		pField->pUICombo->bUseMessage = bIsMessage;
		if (pField->pUICombo->pPopupList) {
			ui_ListColumnSetType(pField->pUICombo->pPopupList->eaColumns[0], bIsMessage ? UIListPTMessage : UIListPTName, (intptr_t)pchDictDisplayField, NULL);
		}
	} else if (pField->peaComboModel && pField->pUIText && pField->pUIText->cb && 
				((pField->eType == kMEFieldType_TextEntry) || (pField->eType == kMEFieldType_ValidatedTextEntry))) {
		// This is a fragile hack to re-direct the internals of the combo box
		pField->pUIText->cb->drawData = (char*)pchDictDisplayField;
		pField->pUIText->cb->pTextData = (char*)pchDictDisplayField;
		if (pField->pUIText->cb) {
			pField->pUIText->cb->bUseMessage = bIsMessage;
		}
		if (pField->pUIText->cb->pPopupFilteredList) {
			ui_ListColumnSetType(pField->pUIText->cb->pPopupFilteredList->pList->eaColumns[0], bIsMessage ? UIListPTMessage : UIListPTName, (intptr_t)pchDictDisplayField, NULL);
		}
	}
}


// Returns the new MEFieldType value for this field type
int MEFieldRegisterFieldType(MEFieldCreateFunc cbCreate, MEFieldDrawFunc cbDraw, 
							 MEFieldCompareFunc cbCompare, MEFieldUpdateFunc cbUpdate,
							 MEFieldGetStringFunc cbGetString)
{
	// Create the extern type info
	MEFieldExternTypeInfo *pInfo = (MEFieldExternTypeInfo*)calloc(1,sizeof(MEFieldExternTypeInfo));
	pInfo->id = gNextExType++;
	pInfo->cbCreate = cbCreate;
	pInfo->cbDraw = cbDraw;
	pInfo->cbCompare = cbCompare;
	pInfo->cbUpdate = cbUpdate;
	pInfo->cbGetString = cbGetString;

	// Put it in the list
	eaPush(&eaExternTypes,pInfo);

	return pInfo->id;
}


void MEExpanderAddFieldToParent(MEField *pField, UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
								MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	MEFieldAddToParent(pField, pParent, x, y);
	ui_WidgetSetPositionEx(pField->pUIWidget, x, y, xPercent, 0, UITopLeft);
	ui_WidgetSetWidthEx(pField->pUIWidget, w, wUnit);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, padRight, 0, 0);
	MEFieldSetChangeCallback(pField, cbChange, pCBData);
	MEFieldSetPreChangeCallback(pField, cbPreChange, pCBData);
}


void MEExpanderRefreshSimpleField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, MEFieldType eType,
								     UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
									 MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimple(eType, pOrigData, pData, pParseTable, pcFieldName);
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		ui_WidgetSetPaddingEx((*ppField)->pUIWidget, 0, padRight, 0, 0);
		// NOTE: We really should be updating the width and some of the information here. Please add as required
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}

void MEExpanderRefreshTextSliderField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, MEFieldType eType,
									 UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, F32 min, F32 max, F32 step,
									 int arrayIdx, MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		(*ppField) = MEFieldCreate(eType, pOrigData, pData, pParseTable, pcFieldName, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
									NULL, NULL, NULL, NULL, false , NULL, NULL, NULL, NULL, arrayIdx, 0, 0, 0, NULL);
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
		assert(eType == kMEFieldType_SliderText);
		(*ppField)->pUISliderText->pSlider->min = min;
		(*ppField)->pUISliderText->pSlider->max = max;
		(*ppField)->pUISliderText->pSlider->step = step;
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}

void MEExpanderRefreshColorField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, MEFieldType eType,
								  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
								  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData, MEFieldColorType eColorType)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimple(eType, pOrigData, pData, pParseTable, pcFieldName);
		(*ppField)->eColorType = eColorType;
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}

void MEExpanderRefreshEnumField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
								UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
								MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigData, pData, pParseTable, pcFieldName, pEnum);
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}


void MEExpanderRefreshEnumFieldNoSort(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
									  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
									  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pOrigData, pData, pParseTable, pcFieldName, pEnum);
		(*ppField)->bDontSortEnums = true;
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}


void MEExpanderRefreshFlagEnumField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, StaticDefineInt pEnum[],
									UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, 
									MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimpleEnum(kMEFieldType_FlagCombo, pOrigData, pData, pParseTable, pcFieldName, pEnum);
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}


void MEExpanderRefreshGlobalDictionaryField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, char *pcDict,
											UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
											MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimpleGlobalDictionary(kMEFieldType_ValidatedTextEntry, pOrigData, pData, pParseTable, pcFieldName, pcDict, "ResourceName");
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}


void MEExpanderRefreshGlobalDictionaryFieldEx(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, MEFieldType eType, char *pcDict,
										  	  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
										  	  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimpleGlobalDictionary(eType, pOrigData, pData, pParseTable, pcFieldName, pcDict, "ResourceName");
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}


void MEExpanderRefreshDataField(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, const char *pcFieldName, void ***peaModel, bool bValidated,
								UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
								MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimpleDataProvided(bValidated ? kMEFieldType_ValidatedTextEntry : kMEFieldType_TextEntry, pOrigData, pData, pParseTable, pcFieldName, NULL, peaModel, NULL);
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}

void MEExpanderRefreshDataFieldEx(MEField **ppField, void *pOrigData, void *pData, ParseTable *pParseTable, char *pcFieldName, MEFieldType eType, void ***peaModel, ParseTable *pModelPTI, const char* pModelKey,
								  UIWidget *pParent, F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight,
								  MEFieldChangeCallback cbChange, MEFieldPreChangeCallback cbPreChange, void *pCBData)
{
	if (!(*ppField)) {
		*ppField = MEFieldCreateSimpleDataProvided(eType, pOrigData, pData, pParseTable, pcFieldName, pModelPTI, peaModel, pModelKey);
		MEExpanderAddFieldToParent(*ppField, pParent, x, y, xPercent, w, wUnit, padRight, cbChange, cbPreChange, pCBData);
	} else {
		ui_WidgetSetPositionEx((*ppField)->pUIWidget, x, y, xPercent, 0, UITopLeft);
		ui_WidgetSetPaddingEx((*ppField)->pUIWidget, 0, padRight, 0, 0);
		ui_WidgetSetWidthEx((*ppField)->pUIWidget, w, wUnit); 
		MEFieldSetAndRefreshFromData(*ppField, pOrigData, pData);
		if (!(*ppField)->pUIWidget->group ) {
			ui_WidgetAddChild( pParent, (*ppField)->pUIWidget );
		}
	}
}


void MEExpanderRefreshLabel(UILabel **ppLabel, const char *pcText, const char *pcTooltip, F32 x, F32 xPercent, F32 y, UIWidget *pParent)
{
	if (!(*ppLabel)) {
		*ppLabel = ui_LabelCreate(NULL, x, y);
		ui_WidgetSetClickThrough(UI_WIDGET(*ppLabel), true);
		ui_WidgetSetPositionEx(UI_WIDGET(*ppLabel), x, y, xPercent, 0, UITopLeft);
		ui_WidgetSetTooltipString(UI_WIDGET(*ppLabel), pcTooltip);
		ui_LabelEnableTooltips(*ppLabel);
		ui_WidgetAddChild(pParent, UI_WIDGET(*ppLabel));
	} else {
		ui_WidgetSetTooltipString(UI_WIDGET(*ppLabel), pcTooltip);
		ui_WidgetSetPositionEx(UI_WIDGET(*ppLabel), x, y, xPercent, 0, UITopLeft);
		if (!UI_WIDGET(*ppLabel)->group ) {
			ui_WidgetAddChild( pParent, UI_WIDGET(*ppLabel) );
		}
	}
	ui_LabelSetWidthNoAutosize(*ppLabel, 1, UIUnitPercentage);
	ui_LabelSetText(*ppLabel, pcText);
}


void MEExpanderRefreshButton(UIButton **ppButton, const char *pcText, UIActivationFunc fn, UserData cb,
							 F32 x, F32 y, F32 xPercent, F32 w, UIUnitType wUnit, F32 padRight, UIWidget* pParent )
{
	if( !*ppButton ) {
		*ppButton = ui_ButtonCreate( "", 0, 0, NULL, NULL );
		ui_WidgetAddChild( pParent, UI_WIDGET( *ppButton ));
	}
	ui_ButtonSetTextAndResize( *ppButton, pcText );
	ui_WidgetSetPositionEx( UI_WIDGET( *ppButton ), x, y, xPercent, 0, UITopLeft );
	if( w > 0 ) {
		ui_WidgetSetWidthEx( UI_WIDGET( *ppButton ), w, wUnit );
	}
	ui_WidgetSetPaddingEx( UI_WIDGET( *ppButton ), 0, padRight, 0, 0 );
	ui_ButtonSetCallback( *ppButton, fn, cb );
}

void MEExpanderRefreshButtonImageOnly(UIButton **ppButton, const char *pcTexture, F32 x, F32 y, UIWidget* pParent, UIActivationFunc fn, UserData cb)
{
	if( !*ppButton ) {
		*ppButton = ui_ButtonCreate( NULL, 0, 0, NULL, NULL );
		ui_WidgetAddChild( pParent, UI_WIDGET( *ppButton ));
	}
	ui_ButtonClearImage(*ppButton);
	ui_ButtonSetImage(*ppButton, pcTexture);
	ui_ButtonSetImageStretch( *ppButton, true );
	ui_WidgetSetPositionEx( UI_WIDGET( *ppButton ), x, y, 0, 0, UITopLeft );
	ui_ButtonSetCallback( *ppButton, fn, cb );
}

void MEExpanderRefreshSprite(UISprite **ppSprite, const char *pcTexture, F32 x, F32 y, F32 w, F32 h, UIWidget *pParent)
{
	if (!(*ppSprite)) {
		*ppSprite = ui_SpriteCreate(x, y, w, h, pcTexture);
		ui_WidgetSetPositionEx(UI_WIDGET(*ppSprite), x, y, 0, 0, UITopLeft);
		ui_WidgetAddChild(pParent, UI_WIDGET(*ppSprite));
	} else {
		ui_SpriteSetTexture(*ppSprite, pcTexture);
		ui_WidgetSetPositionEx(UI_WIDGET(*ppSprite), x, y, 0, 0, UITopLeft);
	}
}

// -----------------------------------------------------------------------------------
// Expression Extension Type
// -----------------------------------------------------------------------------------

static void mef_ex_ExpressionExprUpdate(UIWidget *pWidget, MEField *pField)
{
	// Get current state
	UIExpressionEntry *pEntry = (UIExpressionEntry*)pWidget;
	Expression *pExprTarget = (Expression*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	Expression *pExprOrig = pField->pOld ? (Expression*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	const char *pchTargetText = pField->pNew && pExprTarget ? exprGetCompleteString(pExprTarget) : "";
	const char *pchNewText = pEntry ? ui_ExpressionEntryGetText(pEntry) : "";
	bool bUpdateControl = false;
	size_t iLen;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_ex_ExpressionExprUpdate(pWidget, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		if (((!pchTargetText && (pchNewText[0] != '\0')) || (pchTargetText && (strcmp(pchTargetText,pchNewText) != 0))) && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine proper value
	if (pField->bRevert) {
		pchNewText = pField->pOld ? (pExprOrig ? exprGetCompleteString(pExprOrig) : "") : (pExprTarget ? exprGetCompleteString(pExprTarget) : "");
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			Expression *pExprParent = (Expression*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pParent, 0, NULL);
			pchNewText = pField->pParent && pExprParent ? exprGetCompleteString(pExprParent) : "";
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		pchNewText = pchTargetText;
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Update the state and control
	if (bUpdateControl && pEntry) {
		if (pField->pUIExpression) {
			ui_ExpressionEntrySetText(pField->pUIExpression, pchNewText);
		}
		if (pEntry != pField->pUIExpression) {
			ui_ExpressionEntrySetText(pEntry, pchNewText);
		}
	}
	iLen = strlen(pchNewText);
	if (iLen == 0) {
		if (pExprTarget) {
			exprDestroy(pExprTarget);
			pExprTarget = NULL;
		}

		// Set the expression to NULL
		TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, pExprTarget, NULL);
	} else {
		if (!pExprTarget) {
			pExprTarget = exprCreate();
		} 
		exprSetOrigStrNoFilename(pExprTarget, pchNewText);

		// Set the new expression structure in
		TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, pExprTarget, NULL);
	}

	// Check for dirty
	mef_checkIfDirty(pField, pWidget);
	
	// Check for changes
	if ((pchTargetText && pchNewText && (strcmp(pchTargetText,pchNewText) != 0)) ||
		(pchTargetText && !pchNewText) || (!pchTargetText && pchNewText)) {
		if (pField->changedCallback) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
}


static void mef_ex_ExpressionStringUpdate(UIWidget *pWidget, MEField *pField)
{
	// Get the current state
	UIExpressionEntry *pEntry = (UIExpressionEntry*)pWidget;
	const char *pchTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
	const char *pchOrig	= pField->pOld ? TokenStoreGetString(pField->pTable, pField->column, pField->pOld, 0, 0) : NULL;
	const char *pchNewTarget;
	const char *pchNewText = pEntry ? ui_ExpressionEntryGetText(pEntry) : "";
	bool bUpdateControl = false;
	bool bNeedsFree = false;

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		bool bCallPreChangedCallback = false;
		if (!pchTarget && (pchNewText[0] != '\0')) {
			bCallPreChangedCallback = true;
		}
		else if (pchTarget) {
			ANALYSIS_ASSUME(pchTarget);
			if (strcmp(pchTarget, pchNewText) != 0) {
				bCallPreChangedCallback = true;
			}
		}

		if (bCallPreChangedCallback) {
			pField->bRevert = !(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData);
		}
	}

	// Determine the proper value
	if (pField->bRevert) {
		pchNewText = pField->pOld ? (pchOrig ? pchOrig : "") : (pchTarget ? pchTarget : "");
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bUpdate) {
		pchNewText = pField->pNew ? (pchTarget ? pchTarget : "") : "";
		pField->bUpdate = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			const char *pchParent = TokenStoreGetString(pField->pTable, pField->column, pField->pParent, 0, NULL);
			pchNewText = pField->pParent && pchParent ? pchParent : "";
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}

	// Update the state and control
	if (bUpdateControl && pEntry) {
		if (pField->pUIExpression) {
			ui_ExpressionEntrySetText(pField->pUIExpression, pchNewText);
		}
		if (pEntry != pField->pUIExpression) {
			ui_ExpressionEntrySetText(pEntry, pchNewText);
		}
	}

	if (pchTarget != pchNewText) {
		if (pchTarget) {
			pchTarget = strdup(pchTarget);
			bNeedsFree = true;
		}
		TokenStoreSetString(pField->pTable, pField->column, pField->pNew,0,pchNewText,0,0,0,0);
	}

	// Check for dirty
	mef_checkIfDirty(pField, pWidget);
	
	// Check for changes
	if (pField->changedCallback) {
		bool bCallChangedCallback = false;
		pchNewTarget = TokenStoreGetString(pField->pTable, pField->column, pField->pNew, 0, 0);
		if (pchTarget) {
			ANALYSIS_ASSUME(pchTarget);
			if (pchNewTarget) {
				ANALYSIS_ASSUME(pchNewTarget);
				if (strcmp(pchTarget, pchNewTarget) != 0) {
					bCallChangedCallback = true;
				}
			}
			else {
				bCallChangedCallback = true;
			}
		}
		else {
			if (!pchNewTarget) {
				bCallChangedCallback = true;
			}
		}

		if (bCallChangedCallback) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}

	if (bNeedsFree) {
		free((char*)pchTarget);
	}
}


static void mef_ex_ExpressionUpdate(UIWidget *pWidget, MEField *pField)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		mef_ex_ExpressionExprUpdate(pWidget, pField);
	} else if( TOK_GET_TYPE(pField->type) == TOK_STRING_X ) {
		mef_ex_ExpressionStringUpdate(pWidget, pField);
	}
}


static UIWidget *mef_ex_ExpressionCreate(MEField *pField, F32 x, F32 y, F32 width, F32 height)
{
	UIExpressionEntry *pEntry = ui_ExpressionEntryCreate("", (ExprContext*)pField->pExtensionData);

	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		ui_ExpressionEntrySetChangedCallback(pEntry, mef_ex_ExpressionExprUpdate, pField);
	} else if( TOK_GET_TYPE(pField->type) == TOK_STRING_X ) {
		ui_ExpressionEntrySetChangedCallback(pEntry, mef_ex_ExpressionStringUpdate, pField);
	} else {
		assertmsg(0, "Tried to create an expression entry for a non-expression field" );
	}
		
	return UI_WIDGET(pEntry);
}


static void mef_ex_ExpressionDraw(MEField *pField, UI_MY_ARGS, F32 z, Color *pBackgroundColor, UISkin *pSkin)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		// Expression requires special logic
		Expression *pExprTarget = (Expression*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		const char *pText = pField->pNew ? (pExprTarget ? exprGetCompleteString(pExprTarget) : "") : "";
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pText);
	} else {
		char *estr = NULL;
		// All primitive fields can use generic logic
		if(	FieldWriteText(pField->pTable, pField->column, pField->pNew, 0, &estr, 0) ) {
			if (estr) {
				gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", estr);
			}
		}
		estrDestroy(&estr);
	}
}

static void mef_ex_ExpressionGetString(MEField *pField, char **estr)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		// Expression requires special logic
		Expression *pExprTarget = (Expression*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		const char *pText = pField->pNew ? (pExprTarget ? exprGetCompleteString(pExprTarget) : "") : "";
		estrPrintf(estr, "%s", pText);
	} else {
		// All primitive fields can use generic logic
		char token[4096];
		if(	TokenToSimpleString(pField->pTable, pField->column,pField->pNew, token, 1024, 0) ) {
			estrPrintf(estr, "%s", token);
		} 
	}
}

// NOTE: this will not actually return whether the two expressions are actually identical,
// just whether their source strings are identical
static int mef_ex_ExpressionCompare(MEField *pField, void *pLeft, void *pRight)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		// Expression types include a filename component that can differ and needs to be ignored
		// So just compare the originating string for the expression
		Expression *pExprLeft = (Expression*)TokenStoreGetPointer(pField->pTable, pField->column, pLeft, 0, NULL);
		Expression *pExprRight = (Expression*)TokenStoreGetPointer(pField->pTable, pField->column, pRight, 0, NULL);

		return exprCompare(pExprLeft, pExprRight);
	} else {
		// Most types just do a token compare
		// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
		return TokenCompare(pField->pTable, pField->column, pLeft, pRight, 0, TOK_USEROPTIONBIT_1);
	}
}

// -----------------------------------------------------------------------------------
// GameActionBlock Extension Type
// -----------------------------------------------------------------------------------

static void mef_ex_GameActionBlockUpdate(UIWidget *pWidget, MEField *pField)
{
	// Get current state
	UIGameActionEditButton *pButton = (UIGameActionEditButton*)pWidget;
	WorldGameActionBlock *pNewBlock = pButton?pButton->pActionBlock:NULL;
	WorldGameActionBlock *pTargetBlock = (WorldGameActionBlock*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	WorldGameActionBlock *pOrigBlock = pField->pOld ? (WorldGameActionBlock*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	bool bUpdateControl = false;
	bool bChanged = false;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_ex_GameActionBlockUpdate(pWidget, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
		if (StructCompare(parse_WorldGameActionBlock, pOrigBlock, pTargetBlock, 0, 0, TOK_USEROPTIONBIT_1) != 0 && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
			pField->bRevert = true;
		}
	}

	// Determine proper value
	if (pField->bRevert) {
		pNewBlock = pOrigBlock;
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			pNewBlock = (WorldGameActionBlock*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pParent, 0, NULL);
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		// Update widget from data
		pNewBlock = pTargetBlock;
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Check whether the field will change before actually making changes
	// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
	if (StructCompare(parse_WorldGameActionBlock, pNewBlock, pTargetBlock, 0, 0, TOK_USEROPTIONBIT_1) != 0){
		bChanged = true;
	}

	// Update the state and control
	if (bUpdateControl && pButton) {
		// Set the GameActionBlock to the button
		if (pField->pUIGameActionButton){
			ui_GameActionEditButtonSetData(pField->pUIGameActionButton, pNewBlock, pOrigBlock);
		}
		if (pButton != pField->pUIGameActionButton) {
			ui_GameActionEditButtonSetData(pButton, pNewBlock, pOrigBlock);
		}
	}

	// Update the data in the MEField
	if (pNewBlock != pField->pNew){
		TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, StructClone(parse_WorldGameActionBlock, pNewBlock), NULL);
	}

	// Check for dirty
	mef_checkIfDirty(pField, pWidget);
	
	// Check for changes
	if (bChanged) {
		if (pField->changedCallback) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
}

static void mef_ex_GameActionBlockChangeFunc(UIGameActionEditButton *pButton, MEField *pField)
{
	mef_ex_GameActionBlockUpdate(UI_WIDGET(pButton), pField);
}

static UIWidget *mef_ex_GameActionBlockCreate(MEField *pField, F32 x, F32 y, F32 width, F32 height)
{
	UIGameActionEditButton *pButton = NULL;
	
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		pButton = ui_GameActionEditButtonCreate(NULL, NULL, NULL, mef_ex_GameActionBlockChangeFunc, NULL, pField);
	} else {
		assertmsg(0, "Tried to create a GameActionBlock entry for a non-GameActionBlock field" );
	}
		
	return pButton?UI_WIDGET(pButton):NULL;
}


static void mef_ex_GameActionBlockDraw(MEField *pField, UI_MY_ARGS, F32 z, Color *pBackgroundColor, UISkin *pSkin)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		WorldGameActionBlock *pActions = (WorldGameActionBlock*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		if (pActions && (eaSize(&pActions->eaActions) == 1))
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "1 Action");
		else if (pActions && (eaSize(&pActions->eaActions) > 1))
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%d Actions", eaSize(&pActions->eaActions));
		else
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "No Actions");
	}
}

static void mef_ex_GameActionBlockGetString(MEField *pField, char **estr)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		WorldGameActionBlock *pActions = (WorldGameActionBlock*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		if (pActions && (eaSize(&pActions->eaActions) == 1))
			estrPrintf(estr, "1 Action");
		else if (pActions && (eaSize(&pActions->eaActions) > 1))
			estrPrintf(estr, "%d Actions", eaSize(&pActions->eaActions));
		else
			estrPrintf(estr, "No Actions");
	}
}

static int mef_ex_GameActionBlockCompare(MEField *pField, void *pLeft, void *pRight)
{
	// Most types just do a token compare
	// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
	return TokenCompare(pField->pTable, pField->column, pLeft, pRight, 0, TOK_USEROPTIONBIT_1);
}

// -----------------------------------------------------------------------------------
// GameEvent Extension Type
// -----------------------------------------------------------------------------------

static void mef_ex_GameEventUpdate(UIWidget *pWidget, MEField *pField)
{
	// Get current state
	UIGameEventEditButton *pButton = (UIGameEventEditButton*)pWidget;
	GameEvent *pNewEvent = pButton?pButton->pEvent:NULL;
	GameEvent *pTargetEvent = (GameEvent*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
	GameEvent *pOrigEvent = pField->pOld ? (GameEvent*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pOld, 0, NULL) : NULL;
	bool bUpdateControl = false;
	bool bChanged = false;

	// Support for multi-field editing
	if (!pField->bUpdate && eaSize(&pField->eaSiblings)) {
		int i;
		for(i=eaSize(&pField->eaSiblings)-1; i>=0; --i) {
			mef_ex_GameEventUpdate(pWidget, pField->eaSiblings[i]);
			if (!pField->eaSiblings) {
				Alertf("Attempt to change multiple fields changed only some of the fields.  This is because the change had side-effects in the editor.  Multi-editing this data may not be possible.");
				return;
			}
		}
	}

	// Handle pre-change refusal
	if (pField->preChangedCallback && !pField->bRevert && !pField->bUpdate && !pField->bParentUpdate) {
		// Only do pre-change if it actually changed
		// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
		if (StructCompare(parse_GameEvent, pOrigEvent, pTargetEvent, 0, 0, TOK_USEROPTIONBIT_1) != 0 && 
			!(*pField->preChangedCallback)(pField, true, pField->pPreChangeUserData)) {
				pField->bRevert = true;
		}
	}

	// Determine proper value
	if (pField->bRevert) {
		pNewEvent = pOrigEvent;
		pField->bRevert = false;
		bUpdateControl = true;
	}
	if (pField->bParentUpdate) {
		if (pField->pParent) {
			pNewEvent = (GameEvent*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pParent, 0, NULL);
			bUpdateControl = true;
		}
		pField->bParentUpdate = false;
	}
	if (pField->bUpdate) {
		// Update widget from data
		pNewEvent = pTargetEvent;
		pField->bUpdate = false;
		bUpdateControl = true;
	}

	// Check whether the field will change before actually making changes
	// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
	if (StructCompare(parse_GameEvent, pNewEvent, pTargetEvent, 0, 0, TOK_USEROPTIONBIT_1) != 0){
		bChanged = true;
	}

	// Update the state and control
	if (bUpdateControl && pButton) {
		// Set the GameEvent to the button
		if (pField->pUIGameEventButton){
			ui_GameEventEditButtonSetData(pField->pUIGameEventButton, pNewEvent, pOrigEvent);
		}
		if (pButton != pField->pUIGameEventButton) {
			ui_GameEventEditButtonSetData(pButton, pNewEvent, pOrigEvent);
		}
	}

	// Update the data in the MEField
	if (pNewEvent != pField->pNew){
		TokenStoreSetPointer(pField->pTable, pField->column, pField->pNew, 0, StructClone(parse_GameEvent, pNewEvent), NULL);
	}

	// Check for dirty
	mef_checkIfDirty(pField, pWidget);

	// Check for changes
	if (bChanged) {
		if (pField->changedCallback) {
			pField->changedCallback(pField, 1, pField->pChangeUserData);
		}
	}
}

static void mef_ex_GameEventChangeFunc(UIGameEventEditButton *pButton, MEField *pField)
{
	mef_ex_GameEventUpdate(UI_WIDGET(pButton), pField);
}

static UIWidget *mef_ex_GameEventCreate(MEField *pField, F32 x, F32 y, F32 width, F32 height)
{
	UIGameEventEditButton *pButton = NULL;

	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		pButton = ui_GameEventEditButtonCreate(NULL, NULL, mef_ex_GameEventChangeFunc, NULL, pField);
	} else {
		assertmsg(0, "Tried to create a GameEvent entry for a non-GameEvent field" );
	}

	return pButton?UI_WIDGET(pButton):NULL;
}


static void mef_ex_GameEventDraw(MEField *pField, UI_MY_ARGS, F32 z, Color *pBackgroundColor, UISkin *pSkin)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		GameEvent *pEvent = (GameEvent*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		if (pEvent){
			char *estrBuffer = NULL;
			estrStackCreate(&estrBuffer);
			gameevent_WriteEventSingleLine(pEvent, &estrBuffer);
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", estrBuffer);
			estrDestroy(&estrBuffer);
		}else{
			gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "No Event");
		}
	}
}

static void mef_ex_GameEventGetString(MEField *pField, char **estr)
{
	if( TOK_GET_TYPE(pField->type) == TOK_STRUCT_X ) {
		GameEvent *pEvent = (GameEvent*)TokenStoreGetPointer(pField->pTable, pField->column, pField->pNew, 0, NULL);
		if (pEvent){
			gameevent_WriteEventSingleLine(pEvent, estr);
		}else{
			estrPrintf(estr, "No Event");
		}
	}
}

static int mef_ex_GameEventCompare(MEField *pField, void *pLeft, void *pRight)
{
	// Most types just do a token compare
	// Ignore any field tagged with TOK_USEROPTIONBIT_1 (notably Message CurrentFile)
	return TokenCompare(pField->pTable, pField->column, pLeft, pRight, 0, TOK_USEROPTIONBIT_1);
}


#endif

AUTO_RUN;
void MEFieldInitExtentionTypes(void) 
{
#ifndef NO_EDITORS
	kMEFieldTypeEx_Expression = MEFieldRegisterFieldType(mef_ex_ExpressionCreate,mef_ex_ExpressionDraw,mef_ex_ExpressionCompare,mef_ex_ExpressionUpdate,mef_ex_ExpressionGetString);
	kMEFieldTypeEx_GameActionBlock = MEFieldRegisterFieldType(mef_ex_GameActionBlockCreate,mef_ex_GameActionBlockDraw,mef_ex_GameActionBlockCompare,mef_ex_GameActionBlockUpdate,mef_ex_GameActionBlockGetString);
	kMEFieldTypeEx_GameEvent = MEFieldRegisterFieldType(mef_ex_GameEventCreate,mef_ex_GameEventDraw,mef_ex_GameEventCompare,mef_ex_GameEventUpdate,mef_ex_GameEventGetString);
#endif
}

//
// Include the auto-generated code so it gets compiled
//
#include "MultiEditField.h"
#include "AutoGen/MultiEditField_h_ast.c"
