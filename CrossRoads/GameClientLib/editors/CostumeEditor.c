//
// CostumeEditor.c
//

#ifndef NO_EDITORS

#include "CostumeCommonLoad.h"
#include "CostumeCommonRandom.h"
#include "CostumeEditor.h"
#include "CostumeDefEditor.h"
#include "cmdparse.h"
#include "dynAnimChart.h"
#include "dynFxInfo.h"
#include "EditLibGizmos.h"
#include "EditorPrefs.h"
#include "file.h"
#include "fileutil.h"
#include "FolderCache.h"
#include "GameClientLib.h"
#include "GfxClipper.h"
#include "GfxMaterials.h"
#include "GfxSprite.h"
#include "GfxSpriteText.h"
#include "GraphicsLib.h"
#include "Quat.h"
#include "species_common.h"
#include "StringCache.h"
#include "strings_opt.h"
#include "tokenstore.h"
#include "UIGimmeButton.h"
#include "UIDictionaryEntry.h"
#include "UnitSpec.h"
#include "wlGroupPropertyStructs.h"
#include "UGCCommon.h"
#include "wlUGC.h"
#include "itemCommon.h"

#include "AutoGen/CostumeEditor_h_ast.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/dynFxInfo_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#define Z_DIST_BEFORE_SELECTION   (-0.15)

// In radians per second
#define ROTATION_SPEED PI

// Forward declarations
static void costumeEdit_CopyColorScale255(Vec4 src, U8 dest[4]);
NOCONST(PlayerCostume) *costumeEdit_CreateCostume(const char *pcName, NOCONST(PlayerCostume) *pCostumeToClone);
static bool costumeEdit_DeleteCostumeContinue(EMEditor *pEditor, const char *pcName, void *pCostume, EMResourceState eState, void *pData, bool bSuccess);
static bool costumeEdit_IsCostumeDirty(CostumeEditDoc *pDoc);
static bool costumeEdit_SaveCostumeContinue(EMEditor *pEditor, const char *pcName, void *pCostume, EMResourceState eState, void *pData, bool bSuccess);
static void costumeEdit_SavePrefs(CostumeEditDoc *pDoc);
static void costumeEdit_UpdateLists(CostumeEditDoc *pDoc);

void costumeEdit_UIFieldChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc);
void costumeEdit_UIScaleFieldChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc);
void costumeEdit_UITexFieldChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc);
void costumeEdit_UIFieldFinishChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc);
static void costumeEdit_UIBitsAdd(UIButton *pButton, CostumeEditDoc *pDoc);
static void costumeEdit_UIBitsChange(MEField *pField, bool bFinished, CostumeBitsGroup *pGroup);//original animation system version
static void costumeEdit_UIBitsChangeNew(UIComboBox *pComboBox, void* pFakePtr);//new animation system version
static void costumeEdit_UIBitsRemove(UIButton *pButton, CostumeBitsGroup *pGroup);
static void costumeEdit_UIBodyScaleComboChanged(UIComboBox* pCombo, CostumeBodyScaleGroup* pGroup);
static void costumeEdit_UIBodyScaleComboInit(UIComboBox* pCombo, CostumeBodyScaleGroup* pGroup);
static void costumeEdit_UIColorGroupChanged(UIColorButton *pButton, bool bFinished, CostumeColorGroup *pGroup);
static void costumeEdit_UIColorGroupToggled(UICheckButton *pButton, CostumeColorGroup *pGroup);
static void costumeEdit_UIFXAdd(UIButton *pButton, CostumeEditDoc *pDoc);
static void costumeEdit_UIFXRemove(UIButton *pButton, CostumeFxGroup *pGroup);
static void costumeEdit_UIFXSwapAdd(UIButton *pButton, CostumeEditDoc *pDoc);
static void costumeEdit_UIFXSwapRemove(UIButton *pButton, CostumeFxSwapGroup *pGroup);
static void costumeEdit_UIScaleZero(UIButton *pButton, CostumeEditDoc *pDoc);
static void costumeEdit_UITexAdd(UIButton *pButton, CostumeEditDoc *pDoc);
static void costumeEdit_UITexRemove(UIButton *pButton, CostumeTexGroup *pGroup);
static void costumeEdit_UIValueGroupChanged(UISliderTextEntry *pSlider, bool bFinished, CostumeColorGroup *pGroup);

static void costumeEdit_CostumeChanged(CostumeEditDoc *pDoc);

//-----------------------------------------------------------------------------------
// Global Data
//-----------------------------------------------------------------------------------


//
// The command list is global and needs to be extern
//
extern CmdList CostumeEditorCmdList = {0};


//
// The costume animations cannot be determined dynamically, so have a static list here
// (or it might actually be read from data/editors/CostumeEditorAnimList.txt!)
//
static CostumeAnim g_CostumeAnims[] = 
{
	//name				bits					stance words		keyword			move
	{"Idle",			"Idle",					NULL,				NULL,			NULL		},
	{"Walk",			"MOVE FORWARD",			NULL,				NULL,			"Forward"	},
	{"Run",				"MOVE FORWARD RUN",		"Run",				NULL,			"Forward"	},
	{"Action Mode",		"ACTIONMODE",			"ActionMode",		NULL,			NULL		},
};
static CostumeAnim **eaCostumeAnims = NULL;
static CostumeAnimList g_CostumeAnimsFromDisk = {0};

typedef struct CostumeEditColorSet {
	U8 color0[4];
	U8 color1[4];
	U8 color2[4];
	U8 color3[4];
	U8 glowScale[4];
} CostumeEditColorSet;

// Clipboard and such
static NOCONST(PlayerCostume) *gCostumeToClone = NULL;
static NOCONST(PCPart) *gPartClipboard = NULL;
static char *gMatChoiceClipboard = NULL;
static bool bColorClipboardSet = false;
static U8 gColorClipboard[4];
static U8 gColorGlowClipboard;
static bool bColorSetClipboardSet = false;
static CostumeEditColorSet gColorSetClipboard;
static bool bReflectClipboardSet = false;
static F32 gReflectClipboard;
static F32 gSpecularClipboard;
static bool bReflectSetClipboardSet = false;
static Vec4 gReflectSetClipboard;
static Vec4 gSpecularSetClipboard;
static F32 gYawSpeed = 0;
static const char **g_eaGeoFileNames = NULL;
static const char **g_eaPoseFileNames = NULL;

static char **geaScopes = NULL;

// Global flags
bool gUseDispNames = true;
bool gCEColorLinkAll = true;
bool gCEMatLinkAll = false;
bool gCEUnlockAll = true;
bool gUseSymmetry = true;
bool bBoneGroupMatching = true;
static bool gInitializedEditor = false;
static bool bIgnoreColorButtonUpdates = false;

// Shared definition editor data
CostumeEditDefDoc *gDefDoc = NULL;

static NOCONST(PCPart) *gEmptyPart;

static UIWindow *pGlobalWindow = NULL;

static bool gAssetsChanged = false;
static bool gIndexChanged = false;

static ModelOptionsToolbar *gCostumeMOTToolbar;

static const CostumeEditDyePackStruct **s_eaDyePacks = NULL;


#define SCALE_MARGIN 25

//---------------------------------------------------------------------------------------------------
// Data Manipulation
//---------------------------------------------------------------------------------------------------

void costumeEdit_UIColorChanged(UIColorButton *pButton, bool bFinished, CostumeEditDoc *pDoc);

static void costumeEdit_UpdateColorButtons(CostumeEditDoc *pDoc)
{
	bool bLimit = (pDoc->pCostume->eCostumeType == kPCCostumeType_Player || 
					pDoc->pCostume->eCostumeType == kPCCostumeType_Item ||
					pDoc->pCostume->eCostumeType == kPCCostumeType_Overlay);

	if (bIgnoreColorButtonUpdates) {
		return;
	}
	bIgnoreColorButtonUpdates = true;

	//Give the color window a valid color set
	if (pDoc->pCButton0) {
		if (!pDoc->pCButton0->activeWindow) {
			ui_ColorButtonClick(pDoc->pCButton0, NULL); //Forces the color window to be made
			if (pDoc->pCButton0->activeWindow) {
				ui_WindowHide(UI_WINDOW(pDoc->pCButton0->activeWindow));
			}
		}
		if (pDoc->pCButton0->activeWindow)
		{
			ui_ColorButtonSetChangedCallback(pDoc->pCButton0, costumeEdit_UIColorChanged, pDoc);
			ui_ColorWindowSetColorSet(pDoc->pCButton0->activeWindow, costumeTailor_GetColorSetForPart(pDoc->pCostume,GET_REF(pDoc->pCostume->hSpecies),NULL, pDoc->pColorButtonCurrentPart,0));
			ui_ColorWindowSetLimitColors(pDoc->pCButton0->activeWindow, bLimit);
		}
	}
	if (pDoc->pCButton1) {
		if (!pDoc->pCButton1->activeWindow) {
			ui_ColorButtonClick(pDoc->pCButton1, NULL); //Forces the color window to be made
			if (pDoc->pCButton1->activeWindow) {
				ui_WindowHide(UI_WINDOW(pDoc->pCButton1->activeWindow));
			}
		}
		if (pDoc->pCButton1->activeWindow)
		{
			ui_ColorButtonSetChangedCallback(pDoc->pCButton1, costumeEdit_UIColorChanged, pDoc);
			ui_ColorWindowSetColorSet(pDoc->pCButton1->activeWindow, costumeTailor_GetColorSetForPart(pDoc->pCostume,GET_REF(pDoc->pCostume->hSpecies),NULL, pDoc->pColorButtonCurrentPart,1));
			ui_ColorWindowSetLimitColors(pDoc->pCButton1->activeWindow, bLimit);
		}
	}
	if (pDoc->pCButton2) {
		if (!pDoc->pCButton2->activeWindow) {
			ui_ColorButtonClick(pDoc->pCButton2, NULL); //Forces the color window to be made
			if (pDoc->pCButton2->activeWindow) {
				ui_WindowHide(UI_WINDOW(pDoc->pCButton2->activeWindow));
			}
		}
		if (pDoc->pCButton2->activeWindow)
		{
			ui_ColorButtonSetChangedCallback(pDoc->pCButton2, costumeEdit_UIColorChanged, pDoc);
			ui_ColorWindowSetColorSet(pDoc->pCButton2->activeWindow, costumeTailor_GetColorSetForPart(pDoc->pCostume,GET_REF(pDoc->pCostume->hSpecies),NULL, pDoc->pColorButtonCurrentPart,2));
			ui_ColorWindowSetLimitColors(pDoc->pCButton2->activeWindow, bLimit);
		}
	}
	if (pDoc->pCButton3) {
		if (!pDoc->pCButton3->activeWindow) {
			ui_ColorButtonClick(pDoc->pCButton3, NULL); //Forces the color window to be made
			if (pDoc->pCButton3->activeWindow) {
				ui_WindowHide(UI_WINDOW(pDoc->pCButton3->activeWindow));
			}
		}
		if (pDoc->pCButton3->activeWindow)
		{
			PCMaterialDef *mat = GET_REF(pDoc->pColorButtonCurrentPart->hMatDef);
			SpeciesDef *pSpecies = GET_REF(pDoc->pCostume->hSpecies);
			ui_ColorButtonSetChangedCallback(pDoc->pCButton3, costumeEdit_UIColorChanged, pDoc);
			if (mat && mat->bHasSkin)
			{
				if (pSpecies && GET_REF(pSpecies->hSkinColorSet))
				{
					ui_ColorWindowSetColorSet(pDoc->pCButton3->activeWindow, GET_REF(pSpecies->hSkinColorSet));
				}
				else
				{
					PCSkeletonDef *pSkel = GET_REF(pDoc->pCostume->hSkeleton);
					if (pSkel)
					{
						ui_ColorWindowSetColorSet(pDoc->pCButton3->activeWindow, GET_REF(pSkel->hSkinColorSet));
					}
				}
			}
			else
			{
				ui_ColorWindowSetColorSet(pDoc->pCButton3->activeWindow, costumeTailor_GetColorSetForPart(pDoc->pCostume,pSpecies,NULL, pDoc->pColorButtonCurrentPart,3));
			}
			ui_ColorWindowSetLimitColors(pDoc->pCButton3->activeWindow, bLimit);
		}
	}

	bIgnoreColorButtonUpdates = false;
}

static void costumeEdit_CopyColorScale1(U8 src[4], Vec4 dest)
{
	scaleVec4(src, U8TOF32_COLOR, dest);
}


static void costumeEdit_CopyColorScale255(Vec4 src, U8 dest[4])
{
	scaleVec4(src, 255.f, dest);
}


static int costumeEdit_CompareColorGroups(const CostumeColorGroup **left, const CostumeColorGroup **right)
{
	return stricmp((*left)->pcName, (*right)->pcName);
}


static int costumeEdit_CompareScaleGroups(const CostumeScaleGroup **left, const CostumeScaleGroup **right)
{
	return stricmp((*left)->pcDisplayName, (*right)->pcDisplayName);
}


static bool costumeEdit_StrDiff(const char *pcLeft, const char *pcRight) {
	return (pcLeft != pcRight) && (!pcLeft || !pcRight || stricmp(pcLeft,pcRight));
}


void costumeEdit_FixMessages(NOCONST(PlayerCostume) *pCostume)
{
}


static void costumeEdit_TickCheckChanges(CostumeEditDoc *pDoc)
{
	if ((pDoc) && (((pDoc->pCButton0->activeWindow) && (ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton0->activeWindow))) && (pDoc->pCButton0->activeWindow->bOrphaned))
		|| ((pDoc->pCButton1->activeWindow) && (ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton1->activeWindow))) && (pDoc->pCButton1->activeWindow->bOrphaned))
		|| ((pDoc->pCButton2->activeWindow) && (ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton2->activeWindow))) && (pDoc->pCButton2->activeWindow->bOrphaned))
		|| ((pDoc->pCButton3->activeWindow) && (ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton3->activeWindow))) && (pDoc->pCButton3->activeWindow->bOrphaned))))
	{
		costumeEdit_UpdateColorButtons(pDoc);
	}
	if (gAssetsChanged && gDefDoc->pGeoEditWin) {
		CostumeEditDoc **eaDocs = NULL;
		int i;

		gAssetsChanged = false;
		if (gDefDoc) {
			costumeDefEdit_DefRefresh(gDefDoc);
		}
		costumeEditorEMGetAllOpenDocs(&eaDocs);
		for(i=eaSize(&eaDocs)-1; i>=0; --i) {
			if (eaDocs[i]->pCurrentPart) {
				// After changes need to fix up pointers into dictionary
				costumeEdit_SelectBone(eaDocs[i], GET_REF(eaDocs[i]->pCurrentPart->hBoneDef));
			}
			costumeEdit_UpdateLists(eaDocs[i]);
			costumeView_RegenCostume(eaDocs[i]->pGraphics, (PlayerCostume*)eaDocs[i]->pCostume, NULL);
		}
	}

	if (gIndexChanged) {
		gIndexChanged = false;
		resGetUniqueScopes(g_hPlayerCostumeDict, &geaScopes);
	}
}


static void costumeDefEdit_UIMismatchFileWindow(UIButton *pButton, CostumeEditDoc *pDoc)
{
	if (pGlobalWindow) {
		EditorPrefStoreWindowPosition(COSTUME_EDITOR, "Window Position", "Mismatch", pGlobalWindow);

		// Free the window
		ui_WindowHide(pGlobalWindow);
		ui_WidgetQueueFreeAndNull(&pGlobalWindow);
	}
}

static void costumeDefEdit_MismatchFileError(CostumeEditDoc *pDoc, const char *message)
{
	UIButton *pButton;
	UILabel *pLabel;
	int y = 0;
	int width = 300;
	int x = 0;
	if (pDoc && pDoc->emDoc.doc_name)
		printf("mismatch error in file '%s'\n",pDoc->emDoc.doc_name);
	if (pGlobalWindow) {
		// Simply ignore this error if already have a dialog up.
		return;
	}

	pGlobalWindow = ui_WindowCreate(message, 200, 200, 300, 60);

	EditorPrefGetWindowPosition(COSTUME_EDITOR, "Window Position", "Mismatch", pGlobalWindow);

	pButton = ui_ButtonCreate("Revert", 0, 0, costumeDefEdit_UIMismatchFileWindow, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	if (pDoc && pDoc->emDoc.doc_name)
	{
		pLabel = ui_LabelCreate(pDoc->emDoc.doc_name, 0, y);
		ui_WidgetSetPositionEx(UI_WIDGET(pLabel),x, y, 0.125, 0, UITopLeft);
		ui_WindowAddChild(pGlobalWindow, pLabel);
	}

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}


static void costumeEdit_AssetDictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	// If something is modified, removed, or added, need to scan for updates to the UI
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {
		// Refresh lists on all docs on next tick
		if (!gAssetsChanged) {
			EMEditor *currEditor;
			gAssetsChanged = true;
			if ((eType == RESEVENT_RESOURCE_MODIFIED))
			{
				if (!stricmp(pDictName, "Costumegeometry"))
				{
					if (gDefDoc->pCurrentGeoDef && !stricmp(gDefDoc->pCurrentGeoDef->pcName,pRefData))
					{
						if (gDefDoc->bGeoDefSaved)
						{
							PCGeometryDef *pDefCopy = StructClone(parse_PCGeometryDef, pReferent);
							langMakeEditorCopy(parse_PCGeometryDef, pDefCopy, false);
							if (StructCompare(parse_PCGeometryDef, gDefDoc->pOrigGeoDef, pDefCopy, 0, 0, 0) != 0)
							{
								costumeDefEdit_MismatchFileError(NULL, "The geometry file has changed. Reverting to file.");
							}
							StructDestroy(parse_PCGeometryDef, pDefCopy);
						}
					}
				}
				else if (!stricmp(pDictName, "Costumematerial"))
				{
					if (gDefDoc->pCurrentMatDef && !stricmp(gDefDoc->pCurrentMatDef->pcName,pRefData))
					{
						if (gDefDoc->bMatDefSaved)
						{
							PCMaterialDef *pDefCopy = StructClone(parse_PCMaterialDef, pReferent);
							langMakeEditorCopy(parse_PCMaterialDef, pDefCopy, false);
							if (StructCompare(parse_PCMaterialDef, gDefDoc->pOrigMatDef, pReferent, 0, 0, 0) != 0)
							{
								costumeDefEdit_MismatchFileError(NULL, "The material file has changed. Reverting to file.");
							}
							StructDestroy(parse_PCMaterialDef, pDefCopy);
						}
					}
				}
				else if (!stricmp(pDictName, "Costumetexture"))
				{
					if (gDefDoc->pCurrentTexDef && !stricmp(gDefDoc->pCurrentTexDef->pcName,pRefData))
					{
						if (gDefDoc->bTexDefSaved)
						{
							PCTextureDef *pDefCopy = StructClone(parse_PCTextureDef, pReferent);
							langMakeEditorCopy(parse_PCTextureDef, pDefCopy, false);
							if (StructCompare(parse_PCTextureDef, gDefDoc->pOrigTexDef, pReferent, 0, 0, 0) != 0)
							{
								costumeDefEdit_MismatchFileError(NULL, "The texture file has changed. Reverting to file.");
							}
							StructDestroy(parse_PCTextureDef, pDefCopy);
						}
					}
				}
				else if (!stricmp(pDictName, "Message"))
				{
					Message *message = (Message *)pReferent;
					if (message && !stricmp(message->pcScope,"Costume/Geometry"))
					{
						if (gDefDoc->bGeoDefSaved && gDefDoc->pOrigGeoDef)
						{
							if ((stricmp(gDefDoc->pOrigGeoDef->displayNameMsg.pEditorCopy->pcMessageKey, message->pcMessageKey) == 0) && (stricmp(gDefDoc->pOrigGeoDef->displayNameMsg.pEditorCopy->pcDefaultString,message->pcDefaultString) != 0))
							{
								costumeDefEdit_MismatchFileError(NULL, "The geometry file has changed. Reverting to file.");
							}
						}
					}
					else if (message && !stricmp(message->pcScope,"Costume/Material"))
					{
						if (gDefDoc->bMatDefSaved && gDefDoc->pOrigMatDef)
						{
							if ((stricmp(gDefDoc->pOrigMatDef->displayNameMsg.pEditorCopy->pcMessageKey, message->pcMessageKey) == 0) && (stricmp(gDefDoc->pOrigMatDef->displayNameMsg.pEditorCopy->pcDefaultString,message->pcDefaultString) != 0))
							{
								costumeDefEdit_MismatchFileError(NULL, "The material file has changed. Reverting to file.");
							}
						}
					}
					else if (message && !stricmp(message->pcScope,"Costume/Texture"))
					{
						if (gDefDoc->bTexDefSaved && gDefDoc->pOrigTexDef)
						{
							if ((stricmp(gDefDoc->pOrigTexDef->displayNameMsg.pEditorCopy->pcMessageKey, message->pcMessageKey) == 0) && (stricmp(gDefDoc->pOrigTexDef->displayNameMsg.pEditorCopy->pcDefaultString,message->pcDefaultString) != 0))
							{
								costumeDefEdit_MismatchFileError(NULL, "The texture file has changed. Reverting to file.");
							}
						}
					}
				}
			}
			currEditor = emGetActiveEditor();
			if (currEditor && stricmp(currEditor->editor_name, "Costume Editor") == 0)
			{
				emQueueFunctionCall(costumeEdit_TickCheckChanges, (CostumeEditDoc*)emGetActiveEditorDoc());
			}
		}
	}
}


static void costumeEdit_BuildColorGroups(CostumeEditDoc *pDoc)
{
	const char **eaConstants = NULL;
	const char **eaCurrentConstants = NULL;
	PCMaterialDef *pMatDef;
	Material *pMaterial, *pCurrentMaterial = NULL;
	int i,j;
	bool bFound;

	// Find all the named constants
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		pMatDef = GET_REF(pDoc->pCostume->eaParts[i]->hMatDef);
		if (pMatDef) {
			pMaterial = materialFindNoDefault(pMatDef->pcMaterial, 0);
			if (pMaterial) {
				MaterialRenderInfo *render_info;
				
				if (!pMaterial->graphic_props.render_info) {
					gfxMaterialsInitMaterial(pMaterial, true);
				}
				render_info = pMaterial->graphic_props.render_info;
				assert(render_info);
				for(j=render_info->rdr_material.const_count*4-1; j>=0; --j) {
					if (render_info->constant_names[j]) {
						eaPushUnique(&eaConstants, render_info->constant_names[j]);
						if (pDoc->pCostume->eaParts[i] == pDoc->pCurrentPart) {
							eaPushUnique(&eaCurrentConstants, render_info->constant_names[j]);
							pCurrentMaterial = pMaterial;
						}
					}
				}
			}
		}
	}

	// Remove constants that have built-in UI
	for(i=eaSize(&eaConstants)-1; i>=0; --i) {
		if ((stricmp(eaConstants[i], "Color0") == 0) ||
			(stricmp(eaConstants[i], "Color1") == 0) ||
			(stricmp(eaConstants[i], "Color2") == 0) ||
			(stricmp(eaConstants[i], "Color3") == 0) ||
			(stricmp(eaConstants[i], "MuscleWeight") == 0) ||
			(stricmp(eaConstants[i], "ReflectionWeight") == 0) ||
			(stricmp(eaConstants[i], "SpecularWeight") == 0) ||
			// Artists request that these be hidden
			(stricmp(eaConstants[i], "FresnelTerm_Advanced1") == 0) ||
			(strnicmp(eaConstants[i], "LERP", 4) == 0) ||
			(stricmp(eaConstants[i], "MyOutput") == 0)
			) {
			eaRemove(&eaConstants, i);
		}
	}
	for(i=eaSize(&eaCurrentConstants)-1; i>=0; --i) {
		if ((stricmp(eaCurrentConstants[i], "Color0") == 0) ||
			(stricmp(eaCurrentConstants[i], "Color1") == 0) ||
			(stricmp(eaCurrentConstants[i], "Color2") == 0) ||
			(stricmp(eaCurrentConstants[i], "Color3") == 0) ||
			(stricmp(eaCurrentConstants[i], "MuscleWeight") == 0) ||
			(stricmp(eaCurrentConstants[i], "ReflectionWeight") == 0) ||
			(stricmp(eaCurrentConstants[i], "SpecularWeight") == 0) ||
			// Artists request that these be hidden
			(stricmp(eaCurrentConstants[i], "FresnelTerm_Advanced1") == 0) ||
			(strnicmp(eaCurrentConstants[i], "LERP", 4) == 0) ||
			(stricmp(eaCurrentConstants[i], "MyOutput") == 0)
			) {
			eaRemove(&eaCurrentConstants, i);
		}
	}

	// Remove color groups that no longer need to be here
	for(i=eaSize(&pDoc->eaColorGroups)-1; i>=0; --i) {
		CostumeColorGroup *pGroup = pDoc->eaColorGroups[i];
		bFound = false;
		for(j=eaSize(&eaConstants)-1; j>=0; --j) {
			if (stricmp(pDoc->eaColorGroups[i]->pcName, eaConstants[j]) == 0) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			if (pDoc->eaColorGroups[i]->pCheck) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pCheck));
			}
			if (pDoc->eaColorGroups[i]->pLabel) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pLabel));
			}
			if (pDoc->eaColorGroups[i]->pColorButton) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pColorButton));
			}
			if (pDoc->eaColorGroups[i]->pSlider1) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider1));
			}
			if (pDoc->eaColorGroups[i]->pSlider2) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider2));
			}
			if (pDoc->eaColorGroups[i]->pSlider3) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider3));
			}
			if (pDoc->eaColorGroups[i]->pSlider4) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider4));
			}
			if (pDoc->eaColorGroups[i]->pSubLabel1) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel1));
			}
			if (pDoc->eaColorGroups[i]->pSubLabel2) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel2));
			}
			if (pDoc->eaColorGroups[i]->pSubLabel3) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel3));
			}
			if (pDoc->eaColorGroups[i]->pSubLabel4) {
				ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel4));
			}
			free(pDoc->eaColorGroups[i]);
			eaRemove(&pDoc->eaColorGroups, i);
		}
	}

	// Add in missing color groups
	for(i=eaSize(&eaConstants)-1; i>=0; --i) {
		bFound = false;
		for(j=eaSize(&pDoc->eaColorGroups)-1; j>=0; --j) {
			if (stricmp(pDoc->eaColorGroups[j]->pcName, eaConstants[i]) == 0) {
				bFound = true;
				break;
			}
		}
		if (!bFound) {
			CostumeColorGroup *pGroup = calloc(1,sizeof(CostumeColorGroup));
			pGroup->pcName = eaConstants[i];
			pGroup->pDoc = pDoc;
			pGroup->bIsColor = (strstri(pGroup->pcName, "Color") != NULL);
			eaPush(&pDoc->eaColorGroups, pGroup);
		}
	}

	// Set current status on color groups
	for(i=eaSize(&pDoc->eaColorGroups)-1; i>=0; --i) {
		CostumeColorGroup *pGroup = pDoc->eaColorGroups[i];
		bFound = false;
		for(j=eaSize(&eaCurrentConstants)-1; j>=0; --j) {
			if (stricmp(eaCurrentConstants[j], pGroup->pcName) == 0) {
				bFound = true;
				break;
			}
		}
		if (bFound) {
			pGroup->bAvailable = true;

			if (pDoc->pColorButtonCurrentPart) {
				Vec4 value;

				// See if the value has a current value
				bFound = false;
				if (pGroup->bIsColor) {
					// search the part's material constants for a match
					for(j=eaSize(&pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors)-1; j>=0; --j) {
						if (stricmp(pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors[j]->pcName, pGroup->pcName) == 0) {
							bFound = true;
							break;
						}
					}
					if (bFound) {
						// use the value defined in the costume part
						COPY_COSTUME_COLOR(pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors[j]->color, pGroup->currentColor);
						pGroup->bIsSet = true;
					}
					else {
						// search the part material's material constants for a match
						pMatDef = GET_REF(pDoc->pColorButtonCurrentPart->hMatDef);
						if (pMatDef && pMatDef->pOptions) {
							for (j = eaSize(&pMatDef->pOptions->eaExtraColors)-1; j >= 0; --j) {
								if (stricmp(pMatDef->pOptions->eaExtraColors[j]->pcName, pGroup->pcName) == 0) {
									bFound = true;
									break;
								}
							}
						}
						
						if (bFound) {
							// use the base value defined in the costume part's material
							COPY_COSTUME_COLOR(pMatDef->pOptions->eaExtraColors[j]->color, pGroup->currentColor);
							pGroup->bIsSet = false;
						}
						else if (pCurrentMaterial && materialGetNamedConstantValue(pCurrentMaterial, pGroup->pcName, value)) {
							costumeEdit_CopyColorScale255(value, pGroup->currentColor);
							pGroup->bIsSet = false;
						}
						else {
							setVec4same(pGroup->currentColor, 255.0);
							pGroup->bIsSet = false;
						}
					}
				} else {
					// search the part's material constants for a match
					for(j=eaSize(&pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants)-1; j>=0; --j) {
						if (stricmp(pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants[j]->pcName, pGroup->pcName) == 0) {
							bFound = true;
							break;
						}
					}
					if (bFound) {
						// use the value defined in the costume part
						copyVec4(pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants[j]->values, pGroup->currentValue);
						pGroup->bIsSet = true;
					}
					else {
						// search the part material's material constants for a match
						pMatDef = GET_REF(pDoc->pColorButtonCurrentPart->hMatDef);
						if (pMatDef && pMatDef->pOptions) {
							for (j = eaSize(&pMatDef->pOptions->eaExtraConstants)-1; j >= 0; --j) {
								if (stricmp(pMatDef->pOptions->eaExtraConstants[j]->pcName, pGroup->pcName) == 0) {
									bFound = true;
									break;
								}
							}
						}

						if (bFound) {
							// use the base value defined in the costume part's material
							copyVec4(pMatDef->pOptions->eaExtraConstants[j]->values, pGroup->currentValue);
							pGroup->bIsSet = false;
						}
						else if (pCurrentMaterial && materialGetNamedConstantValue(pCurrentMaterial, pGroup->pcName, value)) {
							scaleVec4(value, costumeTailor_GetMatConstantScale(pGroup->pcName), pGroup->currentValue);
							pGroup->bIsSet = false;
						}
						else {
							zeroVec4(pGroup->currentValue);
							pGroup->bIsSet = false;
						}
					}
				}
			}
		} else {
			pGroup->bAvailable = false;
		}
	}

	eaQSort(pDoc->eaColorGroups, costumeEdit_CompareColorGroups);

	eaDestroy(&eaConstants);
	eaDestroy(&eaCurrentConstants);
}

static void costumeEdit_ScaleGroupPresetChooseCB(UIComboBox *pCombo, CostumeEditDoc *pDoc)
{
	int i, j, count = 0;
	int sel = ui_ComboBoxGetSelected(pCombo);
	PCSkeletonDef *pSkel;
	PCPresetScaleValueGroup *obj;
	bool changed = false;

	pSkel = GET_REF(pDoc->pCostume->hSkeleton);
	if (pSkel)
	{
		if (sel > 0)
		{
			obj = ui_ComboBoxGetSelectedObject(pCombo);

			if (obj)
			{
				for (i = eaSize(&obj->eaScaleValues)-1; i >= 0; --i)
				{
					PCScaleValue *pScaleValue = obj->eaScaleValues[i];
					NOCONST(PCScaleValue) *pDestScaleValue;
					for (j = eaSize(&pDoc->pCostume->eaScaleValues)-1; j >= 0; --j)
					{
						pDestScaleValue = pDoc->pCostume->eaScaleValues[j];
						if (!stricmp(pDestScaleValue->pcScaleName,pScaleValue->pcScaleName))
						{
							pDestScaleValue->fValue = pScaleValue->fValue;
							changed = true;
							break;
						}
					}
				}
			}
		}
	}

	ui_ComboBoxSetSelected(pCombo, 0);
	if (changed) costumeEdit_CostumeChanged(pDoc);
}

void costumeEdit_UIBoneScaleFieldChanged(MEField *pField, bool bFinished, CostumeScaleGroup *pGroup)
{
	UILabel *pLabel = pGroup ? pGroup->pLabel : NULL;
	CostumeEditDoc *pDoc = pGroup ? pGroup->pDoc : NULL;
	BoneScaleLimit *pSpeciesBoneScaleLimit = NULL;
	SpeciesDef *pSpecies = pDoc && pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL;
	PCSkeletonDef *pSkel = pDoc && pDoc->pCostume ? GET_REF(pDoc->pCostume->hSkeleton) : NULL;
	PCScaleInfo *pScale = NULL;
	F32 fValue;
	int j, k;

	if (!bFinished)
	{
		costumeEdit_CostumeChanged(pDoc);
		return;
	}

	if (!pSkel) return;
	if (!pDoc) return;
	if (!pLabel) return;
	if (!pField) return;
	if (!pGroup) return;

	if (pDoc->pCostume->eCostumeType != kPCCostumeType_Player)
	{
		ui_LabelSetText(pGroup->pLabel, pGroup->pcDisplayName);
		costumeEdit_CostumeChanged(pDoc);
		return;
	}

	fValue = ui_SliderTextEntryGetValue(pField->pUISliderText);

	for (j=eaSize(&pSkel->eaScaleInfoGroups)-1, k=-1; j >= 0 && k == -1; --j)
	{
		for (k=eaSize(&pSkel->eaScaleInfoGroups[j]->eaScaleInfo)-1; k >= 0; --k)
		{
			if(stricmp(pSkel->eaScaleInfoGroups[j]->eaScaleInfo[k]->pcName, pGroup->pcName) == 0)
			{
				pScale = pSkel->eaScaleInfoGroups[j]->eaScaleInfo[k];
			}
		}
	}
	if (!pScale)
	{
		for (k=eaSize(&pSkel->eaScaleInfo)-1; k >= 0; --k)
		{
			if(stricmp(pSkel->eaScaleInfo[k]->pcName, pGroup->pcName) == 0)
			{
				pScale = pSkel->eaScaleInfo[k];
			}
		}
	}

	pSpeciesBoneScaleLimit = NULL;
	if (pSpecies)
	{
		for (k=0; k < eaSize(&pSpecies->eaBoneScaleLimits); ++k)
		{
			if(stricmp(pSpecies->eaBoneScaleLimits[k]->pcName, pGroup->pcName) == 0)
			{
				pSpeciesBoneScaleLimit = pSpecies->eaBoneScaleLimits[k];
				break;
			}
		}
	}

	if (pSpeciesBoneScaleLimit && (fValue < pSpeciesBoneScaleLimit->fMin || fValue > pSpeciesBoneScaleLimit->fMax))
	{
		ui_LabelSetText(pGroup->pLabel, "!!Invalid!!");
	}
	else if (pScale && (fValue < pScale->fPlayerMin || fValue > pScale->fPlayerMax))
	{
		ui_LabelSetText(pGroup->pLabel, "!!Invalid!!");
	}
	else
	{
		ui_LabelSetText(pGroup->pLabel, pGroup->pcDisplayName);
	}

	costumeEdit_CostumeChanged(pDoc);
}

static void costumeEdit_BuildScaleGroups(CostumeEditDoc *pDoc)
{
	static PCPresetScaleValueGroup Choose = {"Choose a Preset", NULL, NULL};
	CostumeScaleGroup *pGroup;
	PCSkeletonDef *pSkel;
	BoneScaleLimit *pSpeciesBoneScaleLimit = NULL;
	SpeciesDef *pSpecies = GET_REF(pDoc->pCostume->hSpecies);
	NOCONST(PCScaleValue) *pValue, *pOrigValue;
	int i, j, k;
	int y=0, x=0;
	int margin = 0;

	// Clean out old scale group info
	for(i=eaSize(&pDoc->eaScaleGroups)-1; i>=0; --i) {
		pGroup = pDoc->eaScaleGroups[i];
		ui_WidgetQueueFree(UI_WIDGET(pGroup->pLabel));
		if (pGroup->pComboBox)
		{
			ui_WidgetQueueFree(UI_WIDGET(pGroup->pComboBox));
			if (eaSize(&pGroup->eaPresets) >= 1 && !stricmp(pGroup->eaPresets[0]->pcName,"Choose a Preset"))
			{
				eaRemove(&pGroup->eaPresets, 0);
			}
			eaDestroyStruct(&pGroup->eaPresets, parse_PCPresetScaleValueGroup);
		}
		if (pGroup->pSliderField) {
			MEFieldDestroy(pGroup->pSliderField);
		}
		free(pGroup);
	}
	eaDestroy(&pDoc->eaScaleGroups);

	if (pDoc->pNoScaleLabel) {
		ui_WidgetQueueFree(UI_WIDGET(pDoc->pNoScaleLabel));
		pDoc->pNoScaleLabel = NULL;
	}

	pSkel = GET_REF(pDoc->pCostume->hSkeleton);
	if (pSkel) {
		// Build scale groups up from the skeleton; info groups first, then ungrouped info
		for (i = 0; i < eaSize(&pSkel->eaScaleInfoGroups); i++) {
			PCPresetScaleValueGroup *pPreset = NULL;

			// create a dummy entry for the scale info group itself
			pGroup = calloc(1, sizeof(CostumeScaleGroup));
			pGroup->pDoc = pDoc;
			pGroup->pcName = pSkel->eaScaleInfoGroups[i]->pcName;
			pGroup->pcDisplayName = pSkel->eaScaleInfoGroups[i]->pcDisplayName;
			pGroup->bIsGroup = true;
			pGroup->bIsChild = false;
			eaPush(&pDoc->eaScaleGroups, pGroup);

			// Create a Preset pull-down if presets exist
			eaCopyStructs(&pSkel->eaPresets, &pGroup->eaPresets, parse_PCPresetScaleValueGroup);
			for (j = eaSize(&pSkel->eaPresets)-1; j >= 0; --j)
			{
				pPreset = pSkel->eaPresets[j];

				if (stricmp(pPreset->pcTag, pGroup->pcName))
				{
					eaRemove(&pGroup->eaPresets, j);
				}
			}
			if (eaSize(&pGroup->eaPresets))
			{
				eaInsert(&pGroup->eaPresets, &Choose, 0);
			}

			// iterate through each of the scale info group's children and create entries for them too
			for (j = 0; j < eaSize(&pSkel->eaScaleInfoGroups[i]->eaScaleInfo); j++) {
				pGroup = calloc(1, sizeof(CostumeScaleGroup));
				pGroup->pDoc = pDoc;
				pGroup->pcName = pSkel->eaScaleInfoGroups[i]->eaScaleInfo[j]->pcName;
				pGroup->pcDisplayName = pSkel->eaScaleInfoGroups[i]->eaScaleInfo[j]->pcDisplayName;
				pGroup->bIsGroup = false;
				pGroup->bIsChild = true;
				eaPush(&pDoc->eaScaleGroups, pGroup);
			}
		}
		for (i = 0; i < eaSize(&pSkel->eaScaleInfo); i++) {
			pGroup = calloc(1, sizeof(CostumeScaleGroup));
			pGroup->pDoc = pDoc;
			pGroup->pcName = pSkel->eaScaleInfo[i]->pcName;
			pGroup->pcDisplayName = pSkel->eaScaleInfo[i]->pcDisplayName;
			pGroup->bIsGroup = false;
			pGroup->bIsChild = false;
			eaPush(&pDoc->eaScaleGroups, pGroup);
		}

		// Remove unnecessary scale values from costume
		for(i=eaSize(&pDoc->pCostume->eaScaleValues)-1; i>=0; --i) {
			bool bFound = false;
			pValue = pDoc->pCostume->eaScaleValues[i];
			for(j=eaSize(&pDoc->eaScaleGroups)-1; j>=0; --j) {
				if (stricmp(pDoc->eaScaleGroups[j]->pcName, pValue->pcScaleName) == 0) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				StructDestroyNoConst(parse_PCScaleValue, pValue);
				eaRemove(&pDoc->pCostume->eaScaleValues, i);
			}
		}

		// Lay out scale labels first to establish necessary width
		for(i=0; i<eaSize(&pDoc->eaScaleGroups); ++i) {
			pGroup = pDoc->eaScaleGroups[i];
			margin = (pGroup->bIsChild ? SCALE_MARGIN : 0);	// indent labels for children
			pGroup->pLabel = ui_LabelCreate(pGroup->pcDisplayName, 15 + margin, y);
			emPanelAddChild(pDoc->pScalePanel, pGroup->pLabel, false);
			if ((pGroup->pLabel->widget.width + margin) > x) {
				x = pGroup->pLabel->widget.width + margin;
			}
			y += 28;
		}
		y = 0;
		x += 25;

		// Lay out sliders for each scale value
		for(i=0; i<eaSize(&pDoc->eaScaleGroups); ++i) {
			pGroup = pDoc->eaScaleGroups[i];
			pValue = NULL;
			pOrigValue = NULL;
			
			// only add sliders for real scale info, not groups
			if (pGroup->bIsGroup) {
				pGroup->pSliderField = NULL;
				if (eaSize(&pGroup->eaPresets) > 1)
				{
					pGroup->pComboBox = ui_ComboBoxCreate(100, y, 130, parse_PCPresetScaleValueGroup, &pGroup->eaPresets, "Name");
					ui_ComboBoxSetSelectedCallback(pGroup->pComboBox, costumeEdit_ScaleGroupPresetChooseCB, pDoc);
					ui_WidgetSetName(UI_WIDGET(pGroup->pComboBox), "PCPresetScaleValueGroup Presets");
					ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pComboBox), 0.9, UIUnitPercentage);
					emPanelAddChild(pDoc->pScalePanel, UI_WIDGET(pGroup->pComboBox), false);
				}
			}
			else {
				if (pDoc->pOrigCostume) {
					for(j=eaSize(&pDoc->pOrigCostume->eaScaleValues)-1; j>=0; --j) {
						if (stricmp(pGroup->pcName, pDoc->pOrigCostume->eaScaleValues[j]->pcScaleName) == 0) {
							pOrigValue = pDoc->pOrigCostume->eaScaleValues[j];
							break;
						}
					}
					if (!pOrigValue) {
						pOrigValue = StructCreateNoConst(parse_PCScaleValue);
						pOrigValue->pcScaleName = allocAddString(pGroup->pcName);
						pOrigValue->fValue = 0;
						eaPush(&pDoc->pOrigCostume->eaScaleValues, pOrigValue);
					}
				}
				for(j=eaSize(&pDoc->pCostume->eaScaleValues)-1; j>=0; --j) {
					if (stricmp(pGroup->pcName, pDoc->pCostume->eaScaleValues[j]->pcScaleName) == 0) {
						pValue = pDoc->pCostume->eaScaleValues[j];
						break;
					}
				}
				if (!pValue) {
					pValue = StructCreateNoConst(parse_PCScaleValue);
					pValue->pcScaleName = allocAddString(pGroup->pcName);
					pValue->fValue = 0;
					eaPush(&pDoc->pCostume->eaScaleValues, pValue);
				}

				pGroup->pSliderField = MEFieldCreateSimple(kMEFieldType_SliderText, pOrigValue, pValue, parse_PCScaleValue, "fValue");
				assert(pGroup->pSliderField);
				MEFieldSetChangeCallback(pGroup->pSliderField, costumeEdit_UIBoneScaleFieldChanged, pGroup);
				MEFieldAddToParent(pGroup->pSliderField, UI_WIDGET(emPanelGetExpander(pDoc->pScalePanel)), x, y);
				ui_SliderTextEntrySetRange(pGroup->pSliderField->pUISliderText, -100, 100, 1);
				ui_WidgetSetWidthEx(pGroup->pSliderField->pUIWidget, 1, UIUnitPercentage);
			}

			y += 28;
		}

		// Check Validation
		if (pDoc->pCostume->eCostumeType == kPCCostumeType_Player)
		{
			for(i=0; i<eaSize(&pDoc->eaScaleGroups); ++i)
			{
				pGroup = pDoc->eaScaleGroups[i];
				//See if the scale is invalid - if so show it
				if (pDoc->pOrigCostume)
				{
					for(j=eaSize(&pDoc->pOrigCostume->eaScaleValues)-1; j>=0; --j) {
						if (stricmp(pGroup->pcName, pDoc->pOrigCostume->eaScaleValues[j]->pcScaleName) == 0) {
							break;
						}
					}
				}
				else
				{
					for(j=eaSize(&pDoc->pCostume->eaScaleValues)-1; j>=0; --j) {
						if (stricmp(pGroup->pcName, pDoc->pCostume->eaScaleValues[j]->pcScaleName) == 0) {
							break;
						}
					}
				}
				if (j >= 0)
				{
					PCScaleInfo *pScale = NULL;
					pValue = pDoc->pOrigCostume ? pDoc->pOrigCostume->eaScaleValues[j] : pDoc->pCostume->eaScaleValues[j];

					for (j=eaSize(&pSkel->eaScaleInfoGroups)-1, k=-1; j >= 0 && k == -1; --j)
					{
						for (k=eaSize(&pSkel->eaScaleInfoGroups[j]->eaScaleInfo)-1; k >= 0; --k)
						{
							if(stricmp(pSkel->eaScaleInfoGroups[j]->eaScaleInfo[k]->pcName, pValue->pcScaleName) == 0)
							{
								pScale = pSkel->eaScaleInfoGroups[j]->eaScaleInfo[k];
							}
						}
					}
					if (!pScale)
					{
						for (k=eaSize(&pSkel->eaScaleInfo)-1; k >= 0; --k)
						{
							if(stricmp(pSkel->eaScaleInfo[k]->pcName, pValue->pcScaleName) == 0)
							{
								pScale = pSkel->eaScaleInfo[k];
							}
						}
					}

					pSpeciesBoneScaleLimit = NULL;
					if (pSpecies)
					{
						for (k=0; k < eaSize(&pSpecies->eaBoneScaleLimits); ++k)
						{
							if(stricmp(pSpecies->eaBoneScaleLimits[k]->pcName, pValue->pcScaleName) == 0)
							{
								pSpeciesBoneScaleLimit = pSpecies->eaBoneScaleLimits[k];
								break;
							}
						}
					}

					if (pSpeciesBoneScaleLimit && (pValue->fValue < pSpeciesBoneScaleLimit->fMin || pValue->fValue > pSpeciesBoneScaleLimit->fMax))
					{
						ui_LabelSetText(pGroup->pLabel, "!!Invalid!!");
					}
					else if (pScale && (pValue->fValue < pScale->fPlayerMin || pValue->fValue > pScale->fPlayerMax))
					{
						ui_LabelSetText(pGroup->pLabel, "!!Invalid!!");
					}
				}
			}
		}

		if (eaSize(&pDoc->eaScaleGroups) > 0) {
			if (!pDoc->pZeroScaleButton) {
				pDoc->pZeroScaleButton = ui_ButtonCreate("Zero All Scales", 15, y, costumeEdit_UIScaleZero, pDoc);
				ui_WidgetSetWidth(UI_WIDGET(pDoc->pZeroScaleButton), 120);
				emPanelAddChild(pDoc->pScalePanel, pDoc->pZeroScaleButton, false);
			}
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pZeroScaleButton), 15, y);
			y+=28;
		}
	}
	 
	if (eaSize(&pDoc->eaScaleGroups) == 0) {
		// No scales, so put up label
		pDoc->pNoScaleLabel = ui_LabelCreate("Not available on this skeleton", 15, 0);
		emPanelAddChild(pDoc->pScalePanel, pDoc->pNoScaleLabel, false);
		y += 28;
	}
	
	emPanelSetHeight(pDoc->pScalePanel, y);
}


static char *costumeEdit_CreateUniqueName(const char *pcDictName, const char *pcBaseName)
{
	char buf[260];
	int count = 1;

	strcpy(buf, pcBaseName);
	while(resGetInfo(pcDictName, buf)) {
		sprintf(buf, "%s_%d", pcBaseName, count);
		++count;
	}
	return StructAllocString(buf);
}


// This is called whenever any costume data changes to do cleanup
static void costumeEdit_CostumeChanged(CostumeEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFieldChanges) {
		costumeEdit_UpdateLists(pDoc);
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}

static void costumeEdit_CostumeDictChanged(enumResourceEventType eType, const char *pDictName, const char *pRefData, Referent pReferent, void *pUserData)
{
	const char *pcName = (const char *)pRefData;
	EMEditor *pEditor = costumeEditorEMGetEditor();

	if (!pcName) {
		return;
	}

	// If something is modified, removed, or added, need to scan for updates to the UI
	if ((eType == RESEVENT_RESOURCE_MODIFIED) ||
		(eType == RESEVENT_RESOURCE_REMOVED) ||
		(eType == RESEVENT_RESOURCE_ADDED)) {
		// See if any already open doc needs refreshing
		CostumeEditDoc *pDoc = costumeEditorEMGetOpenDoc(pcName);
		if (pDoc) {
			PlayerCostume *pCloneCostume; // compare the Referent to a cloned stripped down version of costume
			pCloneCostume = StructClone(parse_PlayerCostume,(PlayerCostume*)(pDoc->pCostume));
			costumeTailor_StripUnnecessary((void*) (pCloneCostume));
			if (StructCompare(parse_PlayerCostume, pCloneCostume, pReferent, 0, 0, 0) != 0) {
				costumeDefEdit_MismatchFileError(pDoc, "The costume file has changed. Reverting to file.");
				costumeEdit_CostumeRevert(pDoc);
			}
			StructDestroy(parse_PlayerCostume,pCloneCostume);
		}
	}
	if (eType == RESEVENT_INDEX_MODIFIED) {
		gIndexChanged = true;
	}	
}


void costumeEdit_CostumeRevert(CostumeEditDoc *pDoc)
{
	PlayerCostume *pDictCostume;
	char *pcName;

	pcName = strdup(pDoc->pCostume->pcName);

	if (pDoc->pOrigCostume) {
		pDictCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pDoc->pOrigCostume->pcName);
	} else {
		pDictCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pcName);
	}

	if (pDictCostume) {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pOrigCostume);
		pDoc->pOrigCostume = StructCloneDeConst(parse_PlayerCostume, pDictCostume);
		costumeTailor_FillAllBones(pDoc->pOrigCostume, (pDoc->pOrigCostume ? GET_REF(pDoc->pOrigCostume->hSpecies) : NULL), NULL/*powerFX*/, NULL, false, true, gUseDispNames);
		costumeEdit_FixMessages(pDoc->pOrigCostume);
	}
	StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pCostume);
	if (pDoc->pOrigCostume) {
		pDoc->pCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pOrigCostume);
	} else {
		pDoc->pCostume = costumeEdit_CreateCostume(pcName, NULL);
		costumeTailor_FillAllBones(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL/*powerFX*/, NULL, true, true, gUseDispNames);
		costumeEdit_FixMessages(pDoc->pCostume);
		pDoc->pCostume->pcName = allocAddString(pcName);
	}
	free(pcName);

	// Remove references to current part
	pDoc->pCurrentPart = NULL;
	pDoc->pOrigPart = NULL;
	pDoc->pColorButtonCurrentPart = NULL;
	costumeDefEdit_RemoveCurrentPartRefs();


	// Need to reset the list model after changing the costume instance
	assert(pDoc->pCostume);
	ui_ListSetModel(pDoc->pBoneList, NULL, &pDoc->pCostume->eaParts);
		
	// Reset to use the new costume
	costumeEdit_BuildScaleGroups(pDoc);
	costumeEdit_SelectBone(pDoc, NULL);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


static CEMenuData* costumeEdit_CreateMenuData(CostumeEditDoc *pDoc, char *pcMenuName)
{
	CEMenuData *pData = (CEMenuData*)calloc(1,sizeof(CEMenuData));
	pData->pDoc = pDoc;
	pData->pcMenuName = pcMenuName;
	return pData;
}


static NOCONST(PCPart) *costumeEdit_GetOrigPart(CostumeEditDoc *pDoc, PCBoneDef *pBoneDef)
{
	int i;

	for(i=eaSize(&pDoc->pOrigCostume->eaParts)-1; i>=0; --i) {
		if (GET_REF(pDoc->pOrigCostume->eaParts[i]->hBoneDef) == pBoneDef) {
			return pDoc->pOrigCostume->eaParts[i];
		}
	}
	return NULL;
}


static bool costumeEdit_IsCostumeDirty(CostumeEditDoc *pDoc)
{
	if (!pDoc->pOrigCostume) {
		return true;
	}
	return StructCompare(parse_PlayerCostume, pDoc->pOrigCostume, pDoc->pCostume, 0, 0, 0);
}


void costumeEdit_RandomCostume(CostumeEditDoc *pDoc)
{
	// Reset the costume randomly
	costumeRandom_FillRandom(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, NULL, NULL, NULL, gUseSymmetry, bBoneGroupMatching, gUseDispNames, gCEUnlockAll, true, true, true);
	costumeTailor_FillAllBones(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL/*powerFX*/, NULL, true, true, gUseDispNames);

	{
		PCBoneGroup *pBoneGroup = NULL;
		PCGeometryDef *pGeo = NULL;
		PCBoneDef *pBone = NULL;
		NOCONST(PCPart) *pPart = NULL;
		int j;
		PCSkeletonDef *skel = GET_REF(pDoc->pCostume->hSkeleton);

		if (!skel) return;
		for(j=eaSize(&skel->eaBoneGroups)-1; j>=0; --j)
		{
			if ((skel->eaBoneGroups[j]->eBoneGroupFlags & kPCBoneGroupFlags_LinkMaterials))
			{
				pBoneGroup = skel->eaBoneGroups[j];
				break;
			}
		}

		if (pBoneGroup)
		{
			for(j=eaSize(&pBoneGroup->eaBoneInGroup)-1; j>=0; --j)
			{
				pPart = costumeTailor_GetPartByBone(pDoc->pCostume, GET_REF(pBoneGroup->eaBoneInGroup[j]->hBone), NULL);
				if (!pPart) continue;
				costumeTailor_SetPartMaterialLinking(pDoc->pCostume, pPart, kPCColorLink_All, GET_REF(pDoc->pCostume->hSpecies), NULL, true);
			}
		}
	}

	// Remove references to current part
	pDoc->pCurrentPart = NULL;
	pDoc->pOrigPart = NULL;
		
	// Reset to use the new costume
	costumeEdit_BuildScaleGroups(pDoc);
	costumeEdit_SelectBone(pDoc, NULL);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


static void costumeEdit_SavePrefs(CostumeEditDoc *pDoc)
{
	if (pDoc->bIgnorePrefChanges) {
		return;
	}

	// Store column widths
	EditorPrefStoreInt(COSTUME_EDITOR, "Bone Column", "Bone Name", pDoc->pBoneList->eaColumns[0]->fWidth);

	EditorPrefStoreInt(COSTUME_EDITOR, "Bone Column", "Geometry", pDoc->pBoneList->eaColumns[1]->fWidth);

	EditorPrefStoreInt(COSTUME_EDITOR, "Bone Column", "Material", pDoc->pBoneList->eaColumns[2]->fWidth);

	EditorPrefStoreInt(COSTUME_EDITOR, "Bone Column", "Pattern", pDoc->pBoneList->eaColumns[3]->fWidth);

	EditorPrefStoreInt(COSTUME_EDITOR, "Bone Column", "Detail", pDoc->pBoneList->eaColumns[4]->fWidth);

	EditorPrefStoreInt(COSTUME_EDITOR, "Bone Column", "Specular", pDoc->pBoneList->eaColumns[5]->fWidth);

	EditorPrefStoreInt(COSTUME_EDITOR, "Bone Column", "Diffuse", pDoc->pBoneList->eaColumns[6]->fWidth);

	if (gDefDoc) {
		costumeDefEdit_SavePrefs(gDefDoc);
	}
}


void costumeEdit_SelectBone(CostumeEditDoc *pDoc, PCBoneDef *pBoneDef)
{
	int i;
	bool bFound;

	// If NULL, choose a bone
	if (!pBoneDef) {
		// Look for first bone in current region with something on it
		for(i=0; i<eaSize(&pDoc->pCostume->eaParts); ++i) {
			if (GET_REF(pDoc->pCostume->eaParts[i]->hGeoDef) && (stricmp("None", GET_REF(pDoc->pCostume->eaParts[i]->hGeoDef)->pcName) != 0)) {
				PCBoneDef *pBone = GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef);
				PCRegion *pRegion = pBone ? GET_REF(pBone->hRegion) : NULL;
				if (pBone && !pBone->bIsChildBone && (pRegion == pDoc->pCurrentRegion)) {
					pBoneDef = pBone;
					break;
				}
			}
		}
		if (!pBoneDef) {
			// If not, then look for first bone in the region at all
			for(i=0; i<eaSize(&pDoc->pCostume->eaParts); ++i) {
				PCBoneDef *pBone = GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef);
				PCRegion *pRegion = pBone ? GET_REF(pBone->hRegion) : NULL;
				if (pBone && !pBone->bIsChildBone && (pRegion == pDoc->pCurrentRegion)) {
					pBoneDef = pBone;
					break;
				}
			}
		}
		if (!pBoneDef) {
			// If not, then look for first bone with something on it
			for(i=0; i<eaSize(&pDoc->pCostume->eaParts); ++i) {
				PCBoneDef *pBone = GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef);
				if (pBone && !pBone->bIsChildBone && GET_REF(pDoc->pCostume->eaParts[i]->hGeoDef) && (stricmp("None", GET_REF(pDoc->pCostume->eaParts[i]->hGeoDef)->pcName) != 0)) {
					pBoneDef = pBone;
					break;
				}
			}
		}
		if (!pBoneDef && eaSize(&pDoc->pCostume->eaParts)) {
			// If not, then just first part that isn't a child bone
			for(i=0; i<eaSize(&pDoc->pCostume->eaParts); ++i) {
				PCBoneDef *pBone = GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef);
				if (pBone && !pBone->bIsChildBone) {
					pBoneDef = pBone;
					break;
				}
			}
		}
	}

	// Update the current part & the bone list
	bFound = false;
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		if (pBoneDef == GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef)) {
			PCBoneDef *pBone;

			// Set up the part
			pDoc->pCurrentPart = pDoc->pCostume->eaParts[i];
			if (pDoc->pOrigCostume && (GET_REF(pDoc->pOrigCostume->hSkeleton) == GET_REF(pDoc->pCostume->hSkeleton))) {
				pDoc->pOrigPart = pDoc->pOrigCostume->eaParts[i];
			} else {
				pDoc->pOrigPart = NULL;
			}

			// Set up the region/category
			pBone = GET_REF(pDoc->pCurrentPart->hBoneDef);
			pDoc->pCurrentRegion = pBone ? GET_REF(pBone->hRegion) : NULL;
			if (pDoc->pCurrentRegion) {
				pDoc->pCurrentCategory = costumeTailor_GetCategoryForRegion((PlayerCostume*)pDoc->pCostume, pDoc->pCurrentRegion);
			}

			// Set up the controls
			ui_ListSetSelectedRow(pDoc->pBoneList, i);
			ui_ListScrollToSelection(pDoc->pBoneList);
			bFound = true;
			break;
		}
	}
	if (!bFound) {
		pDoc->pCurrentPart = NULL;
		pDoc->pOrigPart = NULL;
		pDoc->pCurrentRegion = NULL;
		pDoc->pCurrentCategory = NULL;
		ui_ListSetSelectedRow(pDoc->pBoneList, -1);
	}

	// Update the other lists to reflect this bone
	costumeEdit_UpdateLists(pDoc);

	// Save prefs when change bone
	costumeEdit_SavePrefs(pDoc);
}


void costumeEdit_SetPartGeometry(CostumeEditDoc *pDoc, const char *pcBone, const char *pcGeo)
{
	NOCONST(PCPart) *pPart;
	int i;

	// Find part that matches current bone def
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		pPart = pDoc->pCostume->eaParts[i];
		if (GET_REF(pPart->hBoneDef) && stricmp(GET_REF(pPart->hBoneDef)->pcName, pcBone) == 0) {
			// Found the part, so change that part's geometry
			SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, pcGeo, pPart->hGeoDef);

			// Validate the part given this change
			costumeTailor_PickValidPartValues(pDoc->pCostume, pPart, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, true, true, NULL);

			// Update displays
			costumeEdit_UpdateLists(pDoc);
			costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
			break;
		}
	}
}


void costumeEdit_SetPartMaterial(CostumeEditDoc *pDoc, const char *pcBone, const char *pcGeo, const char *pcMat)
{
	NOCONST(PCPart) *pPart;
	int i;

	// Find part that matches current bone def
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		pPart = pDoc->pCostume->eaParts[i];
		if (GET_REF(pPart->hBoneDef) && stricmp(GET_REF(pPart->hBoneDef)->pcName, pcBone) == 0) {
			// Found the part, so change that part's geometry and material
			SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, pcGeo, pPart->hGeoDef);
			SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pcMat, pPart->hMatDef);

			// Validate the part given this change
			costumeTailor_PickValidPartValues(pDoc->pCostume, pPart, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, true, true, NULL);

			// Update displays
			costumeEdit_UpdateLists(pDoc);
			costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
			break;
		}
	}
}


void costumeEdit_SetPartTexture(CostumeEditDoc *pDoc, const char *pcBone, const char *pcGeo, const char *pcMat, const char *pcTex, PCTextureType eTexType)
{
	NOCONST(PCPart) *pPart;
	int i;

	// Find part that matches current bone def
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		pPart = pDoc->pCostume->eaParts[i];
		if (GET_REF(pPart->hBoneDef) && stricmp(GET_REF(pPart->hBoneDef)->pcName, pcBone) == 0) {
			// Found the part, so change that part's geometry, material, and appropriate texture
			SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, pcGeo, pPart->hGeoDef);
			SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pcMat, pPart->hMatDef);

			switch(eTexType) {
				case kPCTextureType_Pattern:
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcTex, pPart->hPatternTexture);
						break;
				case kPCTextureType_Detail:
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcTex, pPart->hDetailTexture);
						break;
				case kPCTextureType_Specular:
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcTex, pPart->hSpecularTexture);
						break;
				case kPCTextureType_Diffuse:
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcTex, pPart->hDiffuseTexture);
						break;
				case kPCTextureType_Movable:
						SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pcTex, pPart->pMovableTexture->hMovableTexture);
						break;
				case kPCTextureType_Other:	
						// The "Other" type is not placed directly on the part.  It is ignored.
						break;

				default:
					assertmsg(0, "unexpected value");
			}

			// Validate the part given this change
			costumeTailor_PickValidPartValues(pDoc->pCostume, pPart, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, true, true, NULL);

			// Update displays
			costumeEdit_UpdateLists(pDoc);
			costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
			break;
		}
	}
}


void costumeEdit_UpdateDisplayNameStatus(CostumeEditDoc *pDoc)
{
	int ii = 0;

	// Apply to non-field combos
	// This is a fragile hack to re-direct the internals of the combo box
	pDoc->pRegionCombo->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pRegionCombo->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pRegionCombo->bUseMessage = gUseDispNames;
	if (pDoc->pRegionCombo->pPopupList) {
		ui_ListColumnSetType(pDoc->pRegionCombo->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	}
	pDoc->pCategoryCombo->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pCategoryCombo->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pCategoryCombo->bUseMessage = gUseDispNames;
	if (pDoc->pCategoryCombo->pPopupList) {
		ui_ListColumnSetType(pDoc->pCategoryCombo->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	}
	pDoc->pBoneCombo->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pBoneCombo->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pBoneCombo->bUseMessage = gUseDispNames;
	if (pDoc->pBoneCombo->pPopupList) {
		ui_ListColumnSetType(pDoc->pBoneCombo->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	}
	pDoc->pLayerCombo->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pLayerCombo->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
	pDoc->pLayerCombo->bUseMessage = gUseDispNames;
	if (pDoc->pLayerCombo->pPopupList) {
		ui_ListColumnSetType(pDoc->pLayerCombo->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
	}
	
	// Apply to any combo-box-using body scales
	// This is a fragile hack to re-direct the internals of the combo box
	for (ii = 0; ii < eaSize(&pDoc->eaBodyScaleGroups); ii++) {
		if (pDoc->eaBodyScaleGroups[ii]->pScaleComboBox) {
			pDoc->eaBodyScaleGroups[ii]->pScaleComboBox->drawData = gUseDispNames ? "DisplayNameMsg" : "Name";
			pDoc->eaBodyScaleGroups[ii]->pScaleComboBox->pTextData = gUseDispNames ? "DisplayNameMsg" : "Name";
			pDoc->eaBodyScaleGroups[ii]->pScaleComboBox->bUseMessage = gUseDispNames;
			if (pDoc->eaBodyScaleGroups[ii]->pScaleComboBox->pPopupList) {
				ui_ListColumnSetType(pDoc->eaBodyScaleGroups[ii]->pScaleComboBox->pPopupList->eaColumns[0], gUseDispNames ? UIListPTMessage : UIListPTName, (intptr_t)(gUseDispNames ? "DisplayNameMsg" : "Name"), NULL);
			}
		}
	}

	// Apply to relevant fields
	MEFieldSetDictField(pDoc->pSkeletonField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pGeoField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pChildGeoField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pMatField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pPatternField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pDetailField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pSpecularField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pDiffuseField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	MEFieldSetDictField(pDoc->pMovableField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	if (pDoc->pStanceField) {
		MEFieldSetDictField(pDoc->pStanceField, "Name", gUseDispNames ? "DisplayNameMsg" : "Name", gUseDispNames);
	}

	//This does not work the way I expect it to so I am leaving it out for now ~DHOGBERG
	//MEFieldSetDictField(pDoc->pDyePackField, "DyePack", gUseDispNames ? "DisplayName" : "DyePack", false);

	if (gDefDoc) {
		costumeDefEdit_UpdateDisplayNameStatus(gDefDoc);
	}
}


//
// This procedure updates the list of controls in the artist colors UI
//
static void costumeEdit_UpdateArtistColors(CostumeEditDoc *pDoc)
{
	CostumeColorGroup *pGroup;
	int i, j;
	int y = 0, x = 0;

	// Ensure the proper color groups exist
	costumeEdit_BuildColorGroups(pDoc);

	// Build the UI

	// Add in checkbox labels first to determine dynamic width
	for(i=0; i<eaSize(&pDoc->eaColorGroups); ++i) {
		pGroup = pDoc->eaColorGroups[i];

		// Put in checkbox
		if (!pGroup->pCheck) {
			pGroup->pCheck = ui_CheckButtonCreate(15, y, pGroup->pcName, false);
			ui_CheckButtonSetToggledCallback(pGroup->pCheck, costumeEdit_UIColorGroupToggled, pGroup);
			emPanelAddChild(pDoc->pArtistColorsPanel, pGroup->pCheck, false);
		}
		ui_CheckButtonSetState(pGroup->pCheck, pGroup->bIsSet && pGroup->bAvailable);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pCheck), 15, y);
		ui_SetActive(UI_WIDGET(pGroup->pCheck), pGroup->bAvailable);

		if (pGroup->pCheck->widget.width > x) {
			x = pGroup->pCheck->widget.width;
		}
		
		y += 28;

		// add appropriate sub-labels if multiple values are used
		if (!pGroup->bIsColor && pGroup->bAvailable && costumeTailor_GetMatConstantNumValues(pGroup->pcName) > 1) {
			// list the sliders which might be used
			UILabel** labels[4] = { &pGroup->pSubLabel1,
									&pGroup->pSubLabel2,
									&pGroup->pSubLabel3,
									&pGroup->pSubLabel4 };

			for (j = 0; j < costumeTailor_GetMatConstantNumValues(pGroup->pcName); j++) {
				// create a new sub-label for the slider and add it to the panel
				if (!(*labels[j])) {
					(*labels[j]) = ui_LabelCreate(costumeTailor_GetMatConstantValueName(pGroup->pcName, j), 45, y);
				}
				if (!(*labels[j])->widget.group) {
					emPanelAddChild(pDoc->pArtistColorsPanel, *labels[j], false);
				}
				ui_WidgetSetPosition(UI_WIDGET(*labels[j]), 45, y);
				ui_SetActive(UI_WIDGET(*labels[j]), pGroup->bIsSet);
				y += 28;
			}
		}
		else {
			// remove unneeded labels from the group
			if (pGroup->pSubLabel1 && pGroup->pSubLabel1->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel1));
			}
			if (pGroup->pSubLabel2 && pGroup->pSubLabel2->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel2));
			}
			if (pGroup->pSubLabel3 && pGroup->pSubLabel3->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel3));
			}
			if (pGroup->pSubLabel4 && pGroup->pSubLabel4->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSubLabel4));
			}
		}
	}

	// Special for the no shadow field position
	ui_WidgetSetPosition(pDoc->pNoShadowField->pUIWidget, 15, y);
	y += 28;

	x += 20;
	y = 0;

	for(i=0; i<eaSize(&pDoc->eaColorGroups); ++i) {
		pGroup = pDoc->eaColorGroups[i];

		if (!pGroup->bAvailable) {
			// If not available, hide other controls and show label
			if (!pGroup->pLabel) {
				pGroup->pLabel = ui_LabelCreate("Not Available", x, y);
			}
			if (!pGroup->pLabel->widget.group) {
				emPanelAddChild(pDoc->pArtistColorsPanel, pGroup->pLabel, false);
			}
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pLabel), x, y);
			ui_SetActive(UI_WIDGET(pGroup->pCheck), pGroup->bIsSet);

			// remove conflicting widgets
			if (pGroup->pColorButton && pGroup->pColorButton->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pColorButton));
			}
			if (pGroup->pSlider1 && pGroup->pSlider1->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSlider1));
			}
			if (pGroup->pSlider2 && pGroup->pSlider2->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSlider2));
			}
			if (pGroup->pSlider3 && pGroup->pSlider3->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSlider3));
			}
			if (pGroup->pSlider4 && pGroup->pSlider4->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pSlider4));
			}

			y += 28;
		} else if (pGroup->bIsColor) {
			Vec4 value;
			// If color, show color button and hide the label
			if (!pGroup->pColorButton) {
				Vec4 v4Color;
				v4Color[0] = pGroup->currentColor[0];
				v4Color[1] = pGroup->currentColor[1];
				v4Color[2] = pGroup->currentColor[2];
				v4Color[3] = pGroup->currentColor[3];
				pGroup->pColorButton = ui_ColorButtonCreate(x, y, v4Color);
				pGroup->pColorButton->liveUpdate = true;
				//JE: Needed for tattoos: pGroup->pColorButton->min = -1;
				//JE: Needed for tattoos: pGroup->pColorButton->max = 10;
				ui_ColorButtonSetChangedCallback(pGroup->pColorButton, costumeEdit_UIColorGroupChanged, pGroup);
				ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pColorButton), 1, UIUnitPercentage);
				ui_WidgetSetPaddingEx(UI_WIDGET(pGroup->pColorButton), 0, 21, 0, 0);
			}
			if (!pGroup->pColorButton->widget.group) {
				emPanelAddChild(pDoc->pArtistColorsPanel, pGroup->pColorButton, false);
			}
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pColorButton), x, y);
			costumeEdit_CopyColorScale1(pGroup->currentColor, value);
			ui_ColorButtonSetColor(pGroup->pColorButton, value);
			ui_SetActive(UI_WIDGET(pGroup->pColorButton), pGroup->bIsSet);

			// remove conflicting widgets
			if (pGroup->pLabel && pGroup->pLabel->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pLabel));
			}

			y += 28;
		} else {
			// If not a color, show a slider and hide the label for each value; between one and four
			char buf[260];
			int numValues = CLAMP(costumeTailor_GetMatConstantNumValues(pGroup->pcName), 1, 4);
			int ii = 0;
			
			// list the sliders which might be used
			UISliderTextEntry** sliders[4] = { &pGroup->pSlider1,
											   &pGroup->pSlider2,
											   &pGroup->pSlider3,
											   &pGroup->pSlider4 };

			// if more than one value is used, leave a blank line above
			if (numValues > 1) {
				y += 28;
			}

			// iterate through as many sliders as there are values being used
			for (ii = 0; ii < numValues; ii++) {
				F64 fStep = 0.0f;

				if (!(*sliders[ii])) {
					(*sliders[ii]) = ui_SliderTextEntryCreateWithNoSnap("0", 0, 100, x, y, 120);
					ui_WidgetSetWidthEx(UI_WIDGET(*sliders[ii]), 1, UIUnitPercentage);
					ui_WidgetSetPaddingEx(UI_WIDGET(*sliders[ii]), 0, 21, 0, 0);
					ui_SliderTextEntrySetRange(*sliders[ii], 0, 100, 0.5);	// keep step for sliding
					ui_SliderTextEntrySetPolicy(*sliders[ii], UISliderContinuous);
					ui_SliderTextEntrySetChangedCallback(*sliders[ii], costumeEdit_UIValueGroupChanged, pGroup);
				}
				if (!(*sliders[ii])->widget.group) {
					emPanelAddChild(pDoc->pArtistColorsPanel, *sliders[ii], false);
				}
				ui_WidgetSetPosition(UI_WIDGET(*sliders[ii]), x, y);
				ui_SetActive(UI_WIDGET(*sliders[ii]), pGroup->bIsSet);
				sprintf(buf, "%g", pGroup->currentValue[ii]);
				
				// temporarily disable step when updating
				if ((*sliders[ii])->pSlider) {
					fStep = (*sliders[ii])->pSlider->step;
					(*sliders[ii])->pSlider->step = 0.0f;
				}
				ui_SliderTextEntrySetTextAndCallback(*sliders[ii], buf);
				if ((*sliders[ii])->pSlider) {
					(*sliders[ii])->pSlider->step = fStep;
				}

				y += 28;
			}

			// remove conflicting widgets
			if (pGroup->pLabel && pGroup->pLabel->widget.group) {
				ui_WidgetRemoveFromGroup(UI_WIDGET(pGroup->pLabel));
			}
		}
	}

	y += 28; // For the no-shadow flag

	emPanelSetHeight(pDoc->pArtistColorsPanel, y);
}


//
// This procedure updates the list of controls in the Bitslist UI
//
void costumeEdit_UpdateBits(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel = pDoc->pBitsPanel;
	CostumeBitsGroup *pBitsGroup;
	int i,numBits;
	int y=0;

	// Free memory for excess Bits groups
	for(i=eaSize(&pDoc->eaBitsGroups)-1; i>=eaSize(&pDoc->pCostume->pArtistData->eaConstantBits); --i) {
		if (!gConf.bNewAnimationSystem) MEFieldDestroy(pDoc->eaBitsGroups[i]->pField); //original animation system version
		else ui_WidgetQueueFree(UI_WIDGET(pDoc->eaBitsGroups[i]->pComboBox)); //new animation system version
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaBitsGroups[i]->pRemoveButton));
		free(pDoc->eaBitsGroups[i]);
		eaRemove(&pDoc->eaBitsGroups,i);
	}

	// Add or change FX entry controls
	numBits = eaSize(&pDoc->pCostume->pArtistData->eaConstantBits);
	for(i=0; i<numBits; ++i) {
		NOCONST(PCBitName) *pOldBits = NULL;

		if (eaSize(&pDoc->eaBitsGroups) <= i) {
			// Allocate new group
			pBitsGroup = calloc(1,sizeof(CostumeBitsGroup));
			pBitsGroup->pDoc = pDoc;
			eaPush(&pDoc->eaBitsGroups,pBitsGroup);
		} else {
			pBitsGroup = pDoc->eaBitsGroups[i];
		}

		if ((pDoc->pOrigCostume) && (i < eaSize(&pDoc->pOrigCostume->pArtistData->eaConstantBits))) {
			pOldBits = pDoc->pOrigCostume->pArtistData->eaConstantBits[i];
		}

		if (!gConf.bNewAnimationSystem)
		{
			//original animation system version
			if (!pBitsGroup->pField) {
				pBitsGroup->pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOldBits, pDoc->pCostume->pArtistData->eaConstantBits[i], parse_PCBitName, "Name");
				MEFieldAddToParent(pBitsGroup->pField, UI_WIDGET(emPanelGetExpander(pPanel)), 15, 0);
				MEFieldSetChangeCallback(pBitsGroup->pField, costumeEdit_UIBitsChange, pBitsGroup);
			} else {
				pBitsGroup->pField->pOld = pOldBits;
				pBitsGroup->pField->pNew = pDoc->pCostume->pArtistData->eaConstantBits[i];
			}
			ui_WidgetSetPosition(pBitsGroup->pField->pUIWidget, 15, y);
			ui_WidgetSetWidthEx(pBitsGroup->pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pBitsGroup->pField->pUIWidget, 0, 80, 0, 0);
			MEFieldRefreshFromData(pBitsGroup->pField);
		}
		else
		{
			//new animation system version
			if (!pBitsGroup->pComboBox)
			{
				pBitsGroup->pComboBox = ui_FilteredComboBoxCreate(15, y, 10, parse_DynAnimStanceData, &stance_list.eaStances, "Name");
				emPanelAddChild(pPanel, UI_WIDGET(pBitsGroup->pComboBox), false);
				ui_ComboBoxSetSelectedCallback((UIComboBox*)pBitsGroup->pComboBox, costumeEdit_UIBitsChangeNew, pBitsGroup);
			}
			else
			{
				bool bitExists = false;
				if (pOldBits) {
					FOR_EACH_IN_EARRAY(pDoc->pOrigCostume->pArtistData->eaConstantBits, NOCONST(PCBitName), checkBit) {
						if (checkBit->pcName == pDoc->pCostume->pArtistData->eaConstantBits[i]->pcName)
							bitExists = true;
					} FOR_EACH_END;
				}
				if (!pOldBits || !bitExists)
					ui_SetChanged(UI_WIDGET(pBitsGroup->pComboBox), true);
				ui_ComboBoxSetSelected((UIComboBox*)pBitsGroup->pComboBox, dynAnimStanceIndex(pDoc->pCostume->pArtistData->eaConstantBits[i]->pcName));
			}
			ui_WidgetSetWidthEx(UI_WIDGET(pBitsGroup->pComboBox), 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(UI_WIDGET(pBitsGroup->pComboBox), 0, 80, 0, 0);
		}

		if (!pBitsGroup->pRemoveButton) {
			pBitsGroup->pRemoveButton = ui_ButtonCreate("Remove",0,y,costumeEdit_UIBitsRemove,pBitsGroup);
			ui_WidgetSetWidth(UI_WIDGET(pBitsGroup->pRemoveButton), 70);
			emPanelAddChild(pPanel, pBitsGroup->pRemoveButton, false);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pBitsGroup->pRemoveButton), 0, y, 0, 0, UITopRight);

		y += 28;
	}

	// If no Bits, then a default string
	if (numBits == 0) {
		if (!pDoc->pBitsLabel) {
			pDoc->pBitsLabel = ui_LabelCreate("No control bits are assigned",15,y);
			emPanelAddChild(pPanel, pDoc->pBitsLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pBitsLabel), 15, y);
		y+=20;
	} else {
		if (pDoc->pBitsLabel) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pBitsLabel));
			pDoc->pBitsLabel = NULL;
		}
	}

	// Button to add Bits
	if (!pDoc->pBitsAddButton) {
		pDoc->pBitsAddButton = ui_ButtonCreate("Add Bits...", 15, y, costumeEdit_UIBitsAdd, pDoc );
		pDoc->pBitsAddButton->widget.width = 100;
		emPanelAddChild(pPanel, pDoc->pBitsAddButton, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pBitsAddButton), 15, y);
	y += 28;

	emPanelSetHeight(pPanel,y);
}


//
// This procedure updates the list of controls in the body settings UI
//
static void costumeEdit_UpdateBodySettings(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel = pDoc->pBodySettingsPanel;
	PCBodyScaleInfo **eaBodyScales = NULL;
	PCSkeletonDef *pSkel = NULL;
	const char **eaTexWordsKeys = NULL;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;
	F32 x = 15;
	int i,j;
	char buf[256];
	
	// Remove all children without freeing them
	eaClear(&emPanelGetExpander(pPanel)->widget.children);

	if (pDoc->pCostume) {
		pSkel = GET_REF(pDoc->pCostume->hSkeleton);
	}
	if (!pSkel) {
		return;
	}

	// Height
	if (pSkel->fHeightBase > 0.0) {
		if (!pDoc->pHeightField) {
			pLabel = ui_LabelCreate("Height", x, y);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Height");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, .5, 50, .1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pHeightLabel = pLabel;
			pDoc->pHeightField = pField;
		} else {
			pDoc->pHeightField->pOld = pDoc->pOrigCostume;
			pDoc->pHeightField->pNew = pDoc->pCostume;
			MEFieldAddToParent(pDoc->pHeightField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
		}

		ui_WidgetSetPosition(UI_WIDGET(pDoc->pHeightLabel), x, y);
		emPanelAddChild(pPanel, pDoc->pHeightLabel, false);

		ui_SliderTextEntrySetRange(pDoc->pHeightField->pUISliderText, pSkel->fHeightMin, pSkel->fHeightMax, .1);
		MEFieldRefreshFromData(pDoc->pHeightField);

		y += 28;
	}

	costumeTailor_GetValidBodyScales(pDoc->pCostume, pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL, &eaBodyScales, true);

	// Remove excess body scale groups
	for(i=eaSize(&pDoc->eaBodyScaleGroups)-1; i >= eaSize(&eaBodyScales); --i) {
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaBodyScaleGroups[i]->pScaleLabel));
		if (pDoc->eaBodyScaleGroups[i]->pScaleSliderField) {
			MEFieldDestroy(pDoc->eaBodyScaleGroups[i]->pScaleSliderField);
		}
		if (pDoc->eaBodyScaleGroups[i]->pScaleComboBox) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaBodyScaleGroups[i]->pScaleComboBox));
		}
		free(pDoc->eaBodyScaleGroups[i]);
		eaRemove(&pDoc->eaBodyScaleGroups, i);
	}
	// Update/create groups as necessary for skeleton
	for(i=0; i < eaSize(&eaBodyScales); ++i) {
		CostumeBodyScaleGroup *pGroup;

		if (eaSize(&pDoc->eaBodyScaleGroups) <= i) {
			// create a new group if needed
			pGroup = calloc(1, sizeof(CostumeBodyScaleGroup));
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaBodyScaleGroups, pGroup);
		}
		else {
			pGroup = pDoc->eaBodyScaleGroups[i];
		}
		
		// create a new label widget if necessary
		if (!pGroup->pScaleLabel) {
			pGroup->pScaleLabel = ui_LabelCreate(TranslateDisplayMessage(eaBodyScales[i]->displayNameMsg), x, y);
		} else {
			ui_LabelSetText(pGroup->pScaleLabel,TranslateDisplayMessage(eaBodyScales[i]->displayNameMsg));
		}
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pScaleLabel), x, y);
		emPanelAddChild(pPanel, pGroup->pScaleLabel, false);

		if (eaSize(&eaBodyScales[i]->eaValues) > 0) {
			// create or update the combo box
			if (!pGroup->pScaleComboBox) {
				pGroup->pScaleComboBox = ui_ComboBoxCreate(x+75, y, 1.f, parse_PCBodyScaleValue, &pGroup->eaValues, "Name");
				ui_ComboBoxSetSelectedCallback(pGroup->pScaleComboBox, costumeEdit_UIBodyScaleComboChanged, pGroup);
				ui_WidgetSetName(UI_WIDGET(pGroup->pScaleComboBox), "CostumeEdit = BodyScale");
				costumeEdit_UIBodyScaleComboInit(pGroup->pScaleComboBox, pGroup);
			}
			pGroup->iComboIndex = costumeTailor_GetMatchingBodyScaleIndex(pDoc->pCostume,eaBodyScales[i]);

			emPanelAddChild(pPanel, pGroup->pScaleComboBox, false);
			ui_WidgetSetPosition(UI_WIDGET(pGroup->pScaleComboBox), x+75, y);
			ui_WidgetSetWidthEx(UI_WIDGET(pGroup->pScaleComboBox), 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(UI_WIDGET(pGroup->pScaleComboBox), 0, 21, 0, 0);

			// Reset values on the combo
			for(j=0; j<eaSize(&pGroup->eaValues); ++j) {
				StructDestroy(parse_PCBodyScaleValue, pGroup->eaValues[j]);
			}
			eaDestroy(&pGroup->eaValues);
			for(j=0; j<eaSize(&eaBodyScales[i]->eaValues); ++j) {
				eaPush(&pGroup->eaValues, StructClone(parse_PCBodyScaleValue, eaBodyScales[i]->eaValues[j]));
			}

			// remove conflicting widgets
			if (pGroup->pScaleSliderField) {
				MEFieldDestroy(pGroup->pScaleSliderField);
				pGroup->pScaleSliderField = NULL;
			}
		} else {
			// create or update the slider
			if (!pGroup->pScaleSliderField) {
				pGroup->pScaleSliderField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "BodyScale");
				pGroup->pScaleSliderField->arrayIndex = costumeTailor_GetMatchingBodyScaleIndex(pDoc->pCostume,eaBodyScales[i]);
				MEFieldAddToParent(pGroup->pScaleSliderField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
				ui_SliderTextEntrySetRange(pGroup->pScaleSliderField->pUISliderText, 0, 100, 1);
				MEFieldSetChangeCallback(pGroup->pScaleSliderField, costumeEdit_UIFieldChanged, pDoc);
			} else {
				pGroup->pScaleSliderField->pOld = pDoc->pOrigCostume;
				pGroup->pScaleSliderField->pNew = pDoc->pCostume;
				pGroup->pScaleSliderField->arrayIndex = costumeTailor_GetMatchingBodyScaleIndex(pDoc->pCostume,eaBodyScales[i]);
				MEFieldAddToParent(pGroup->pScaleSliderField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
			}
			ui_WidgetSetPosition(pGroup->pScaleSliderField->pUIWidget, x+75, y);
			ui_WidgetSetWidthEx(pGroup->pScaleSliderField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pGroup->pScaleSliderField->pUIWidget, 0, 21, 0, 0);
			MEFieldRefreshFromData(pGroup->pScaleSliderField);

			// remove conflicting widgets
			if (pGroup->pScaleComboBox) {
				ui_WidgetQueueFree(UI_WIDGET(pGroup->pScaleComboBox));
				pGroup->pScaleComboBox = NULL;
			}
		}

		y += 28;
	}

	// Muscle
	if (!pSkel->bNoMuscle) {
		if (!pDoc->pMuscleField) {
			pLabel = ui_LabelCreate("Muscle", x, y);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Muscle");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pMuscleLabel = pLabel;
			pDoc->pMuscleField = pField;
		} else {
			pDoc->pMuscleField->pOld = pDoc->pOrigCostume;
			pDoc->pMuscleField->pNew = pDoc->pCostume;
			MEFieldAddToParent(pDoc->pMuscleField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
		}

		ui_WidgetSetPosition(UI_WIDGET(pDoc->pMuscleLabel), x, y);
		emPanelAddChild(pPanel, pDoc->pMuscleLabel, false);

		MEFieldRefreshFromData(pDoc->pMuscleField);

		y += 28;
	}

	// Stances
	if (eaSize(&pDoc->eaStances) > 1) {
		if (!pDoc->pStanceField) {
			pLabel = ui_LabelCreate("Stance", x, y);
			pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Stance", parse_PCStanceInfo, &pDoc->eaStances, "Name");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pStanceLabel = pLabel;
			pDoc->pStanceField = pField;
		} else {
			pDoc->pStanceField->pOld = pDoc->pOrigCostume;
			pDoc->pStanceField->pNew = pDoc->pCostume;
			MEFieldAddToParent(pDoc->pStanceField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
		}

		ui_WidgetSetPosition(UI_WIDGET(pDoc->pStanceLabel), x, y);
		emPanelAddChild(pPanel, pDoc->pStanceLabel, false);

		MEFieldRefreshFromData(pDoc->pStanceField);

		y += 28;
	}

	// TexWords
	costumeTailor_GetValidTexWordsKeys(pDoc->pCostume, &eaTexWordsKeys);

	// Update/create groups as necessary
	for(i=0; i < eaSize(&eaTexWordsKeys); ++i) {
		CostumeTexWordsGroup *pGroup;
		NOCONST(PCTexWords) *pTexWords;
		NOCONST(PCTexWords) *pOrigTexWords;

		// Find the group and create if necessary
		if (eaSize(&pDoc->eaTexWordsGroups) <= i) {
			pGroup = calloc(1, sizeof(CostumeTexWordsGroup));
			pGroup->pDoc = pDoc;
			eaPush(&pDoc->eaTexWordsGroups, pGroup);
		} else {
			pGroup = pDoc->eaTexWordsGroups[i];
		}

		// find the costume tex words and create if necessary
		if (eaSize(&pDoc->pCostume->eaTexWords) <= i) {
			pTexWords = StructCreateNoConst(parse_PCTexWords);
			pTexWords->pcKey = eaTexWordsKeys[i];
			eaPush(&pDoc->pCostume->eaTexWords, pTexWords);
		} else {
			pTexWords = pDoc->pCostume->eaTexWords[i];
			if (!pTexWords->pcKey && (stricmp(pTexWords->pcKey, eaTexWordsKeys[i]) != 0)) {
				// Find matching entry or add one
				for(j=i; j<eaSize(&pDoc->pCostume->eaTexWords); ++j) {
					if (pDoc->pCostume->eaTexWords[j]->pcKey && (stricmp(pDoc->pCostume->eaTexWords[j]->pcKey, eaTexWordsKeys[i]) == 0)) {
						const char *pcTempKey = pDoc->pCostume->eaTexWords[j]->pcKey;
						char *pcTempText = pDoc->pCostume->eaTexWords[j]->pcText;
						pDoc->pCostume->eaTexWords[j]->pcKey = pTexWords->pcKey;
						pDoc->pCostume->eaTexWords[j]->pcText = pTexWords->pcText;
						pTexWords->pcKey = pcTempKey;
						pTexWords->pcText = pcTempText;
						break;
					}
				}
				if (j >= eaSize(&pDoc->pCostume->eaTexWords)) {
					// Didn't find a match, so need to insert new entry
					pTexWords = StructCreateNoConst(parse_PCTexWords);
					pTexWords->pcKey = eaTexWordsKeys[i];
					eaInsert(&pDoc->pCostume->eaTexWords, pTexWords, i);
				}
			}
		}

		// Find the orig tex words (if any)
		pOrigTexWords = NULL;
		if (pDoc->pOrigCostume) {
			for(j=eaSize(&pDoc->pOrigCostume->eaTexWords)-1; j>=0; --j) {
				if (pDoc->pOrigCostume->eaTexWords[j]->pcKey && (stricmp(pDoc->pOrigCostume->eaTexWords[j]->pcKey, eaTexWordsKeys[i]) == 0)) {
					pOrigTexWords = pDoc->pOrigCostume->eaTexWords[j];
					break;
				}
			}
		}

		// create a new label widget if necessary
		if (!pGroup->pKeyLabel) {
			pGroup->pKeyLabel = ui_LabelCreate("", x, y);
		}
		sprintf(buf, "TexWords: %s", eaTexWordsKeys[i]);
		ui_LabelSetText(pGroup->pKeyLabel, buf);
		ui_WidgetSetPosition(UI_WIDGET(pGroup->pKeyLabel), x, y);
		emPanelAddChild(pPanel, pGroup->pKeyLabel, false);

		// create or update the text entry
		if (!pGroup->pField) {
			pGroup->pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pOrigTexWords, pTexWords, parse_PCTexWords, "Text");
			MEFieldAddToParent(pGroup->pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y);
			MEFieldSetChangeCallback(pGroup->pField, costumeEdit_UIFieldChanged, pDoc);
		}
		else {
			pGroup->pField->pOld = pOrigTexWords;
			pGroup->pField->pNew = pTexWords;
			MEFieldAddToParent(pGroup->pField, UI_WIDGET(emPanelGetExpander(pPanel)), x, y);
		}
		ui_WidgetSetPosition(pGroup->pField->pUIWidget, x+10+ui_WidgetGetWidth(UI_WIDGET(pGroup->pKeyLabel)), y);
		ui_WidgetSetWidthEx(pGroup->pField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pGroup->pField->pUIWidget, 0, 21, 0, 0);
		MEFieldRefreshFromData(pGroup->pField);

		y += 28;
	}
	// Remove excess tex words groups
	for(i=eaSize(&pDoc->eaTexWordsGroups)-1; i >= eaSize(&eaTexWordsKeys); --i) {
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaTexWordsGroups[i]->pKeyLabel));
		MEFieldSafeDestroy(&pDoc->eaTexWordsGroups[i]->pField);
		free(pDoc->eaTexWordsGroups[i]);
		eaRemove(&pDoc->eaTexWordsGroups, i);
	}
	// Remove excess tex words entries on the costume
	if (pDoc->pCostume->eaTexWords) {
		for(i=eaSize(&pDoc->pCostume->eaTexWords)-1; i >= eaSize(&eaTexWordsKeys); --i) {
			StructDestroyNoConst(parse_PCTexWords, pDoc->pCostume->eaTexWords[i]);
			eaRemove(&pDoc->pCostume->eaTexWords, i);
		}
		eaDestroy(&eaTexWordsKeys);
	}

	emPanelSetHeight(pPanel, y);
}


//
// This procedure updates the list of controls in the extra textures UI
//
void costumeEdit_UpdateExtraTextures(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel = pDoc->pArtistTexPanel;
	CostumeTexGroup *pTexGroup;
	int i,numTex;
	int y=0;

	numTex = pDoc->pColorButtonCurrentPart ? eaSize(&pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures) : 0;

	// Free memory for excess Tex groups
	for(i=eaSize(&pDoc->eaTexGroups)-1; i>=numTex; --i) {
		MEFieldDestroy(pDoc->eaTexGroups[i]->pField);
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaTexGroups[i]->pRemoveButton));
		free(pDoc->eaTexGroups[i]);
		eaRemove(&pDoc->eaTexGroups,i);
	}

	// Add or change Tex entry controls
	for(i=0; i<numTex; ++i) {
		NOCONST(PCTextureRef) *pOldTex = NULL;

		if (eaSize(&pDoc->eaTexGroups) <= i) {
			// Allocate new group
			pTexGroup = calloc(1,sizeof(CostumeTexGroup));
			pTexGroup->pDoc = pDoc;
			eaPush(&pDoc->eaTexGroups,pTexGroup);
		} else {
			pTexGroup = pDoc->eaTexGroups[i];
		}

		if ((pDoc->pOrigPart) && (i < eaSize(&pDoc->pOrigPart->pArtistData->eaExtraTextures))) {
			pOldTex = pDoc->pOrigPart->pArtistData->eaExtraTextures[i];
		}

		if (!pTexGroup->pField) {
			pTexGroup->pField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldTex, pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures[i], parse_PCTextureRef, "Name", "CostumeTexture", parse_PCTextureDef, "Name");
			MEFieldAddToParent(pTexGroup->pField, UI_WIDGET(emPanelGetExpander(pPanel)), 15, 0);
			MEFieldSetChangeCallback(pTexGroup->pField, costumeEdit_UIFieldChanged, pDoc);
		} else {
			pTexGroup->pField->pOld = pOldTex;
			pTexGroup->pField->pNew = pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures[i];
		}
		ui_WidgetSetPosition(pTexGroup->pField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pTexGroup->pField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pTexGroup->pField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pTexGroup->pField);

		if (!pTexGroup->pRemoveButton) {
			pTexGroup->pRemoveButton = ui_ButtonCreate("Remove",0,y,costumeEdit_UITexRemove,pTexGroup);
			ui_WidgetSetWidth(UI_WIDGET(pTexGroup->pRemoveButton), 70);
			emPanelAddChild(pPanel, pTexGroup->pRemoveButton, false);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pTexGroup->pRemoveButton), 0, y, 0, 0, UITopRight);

		y += 28;
	}

	// If no Texture, then a default string
	if (numTex == 0) {
		if (!pDoc->pTexLabel) {
			pDoc->pTexLabel = ui_LabelCreate("No extra textures are currently defined",15,y);
			emPanelAddChild(pPanel, pDoc->pTexLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pTexLabel), 15, y);
		y+=20;
	} else {
		if (pDoc->pTexLabel) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pTexLabel));
			pDoc->pTexLabel = NULL;
		}
	}

	// Button to add Textures
	if (!pDoc->pTexAddButton) {
		pDoc->pTexAddButton = ui_ButtonCreate("Add Texture...", 15, y, costumeEdit_UITexAdd, pDoc );
		pDoc->pTexAddButton->widget.width = 120;
		emPanelAddChild(pPanel, pDoc->pTexAddButton, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pTexAddButton), 15, y);
	y += 28;

	emPanelSetHeight(pPanel,y);
}

void costumeEdit_UpdateDismountFXDrawFromWidget(UIAnyWidget *pWidget, DynParamBlock *pBlock, CostumeEditDoc *pDoc)
{
	CostumeFxGroup *pFxGroup = pDoc->pDismountFXGroup;
	NOCONST(PCFX) *pFx = pDoc->pCostume->pArtistData->pDismountFX;
	int j;

	for(j = 0; j < eaSize(&pFxGroup->eaParamGroups); j++)
	{
		CostumeFxParamGroup *pFxParamGroup = pFxGroup->eaParamGroups[j];

		MultiVal *paramValue = NULL;

		if(ui_CheckButtonGetState(pFxParamGroup->pParamCheckButton))
		{
			switch(pFxParamGroup->type)
			{
			case edptString:
				{
					const char *str = NULL;
					if(pFxParamGroup->pcDictName)
					{
						UIDictionaryEntry* pEntry = (UIDictionaryEntry*)pFxParamGroup->pParamWidget;
						str = ui_DictionaryEntryGetText(pEntry);
					}
					else
					{
						UITextEntry* pEntry = (UITextEntry*)pFxParamGroup->pParamWidget;
						str = ui_TextEntryGetText(pEntry);
					}

					paramValue = MultiValCreate();
					MultiValSetString(paramValue, str);
				}
				xcase edptNumber:
				{
					UISpinnerEntry* pEntry = (UISpinnerEntry*)pFxParamGroup->pParamWidget;
					F64 val = ui_SpinnerEntryGetValue(pEntry);

					paramValue = MultiValCreate();
					MultiValSetFloat(paramValue, val);
				}
				xcase edptVector:
				{
					UIMultiSpinnerEntry* pEntry = (UIMultiSpinnerEntry*)pFxParamGroup->pParamWidget;
					Vec3 vec;
					ui_MultiSpinnerEntryGetValue(pEntry, vec, 3);

					paramValue = MultiValCreate();
					MultiValSetVec3(paramValue, &vec);
				}
				xcase edptVector4:
				{
					UIMultiSpinnerEntry* pEntry = (UIMultiSpinnerEntry*)pFxParamGroup->pParamWidget;
					Vec4 vec4;
					ui_MultiSpinnerEntryGetValue(pEntry, vec4, 4);

					paramValue = MultiValCreate();
					MultiValSetVec4(paramValue, &vec4);
				}
			}
		}

		if(paramValue)
		{
			DynDefineParam *param = StructCreate(parse_DynDefineParam);
			param->pcParamName = allocAddString(pFxParamGroup->pcName);
			MultiValCopy(&param->mvVal, paramValue);
			eaPush(&pBlock->eaDefineParams, param);

			MultiValDestroy(paramValue);
		}
	}

	if(eaSize(&pBlock->eaDefineParams))
	{
		char *parserStr = NULL;
		ParserWriteText(&parserStr, parse_DynParamBlock, pBlock, 0, 0, 0);
		StructCopyString(&pFx->pcParams, parserStr);
		estrDestroy(&parserStr);
	}
	else
	{
		// No properties in the block? Just remove it entirely.
		StructFreeStringSafe(&pFx->pcParams);
	}

	costumeEdit_CostumeChanged(pDoc);
}

void costumeEdit_UpdateFXDrawFromWidget(UIAnyWidget *pWidget, DynParamBlock *pBlock, CostumeEditDoc *pDoc)
{
	int i;

	for(i = 0; i < eaSize(&pDoc->eaFxGroups); i++)
	{
		CostumeFxGroup *pFxGroup = pDoc->eaFxGroups[i];
		NOCONST(PCFX) *pFx = pDoc->pCostume->pArtistData->eaFX[i];
		int j;

		for(j = 0; j < eaSize(&pFxGroup->eaParamGroups); j++)
		{
			CostumeFxParamGroup *pFxParamGroup = pFxGroup->eaParamGroups[j];

			MultiVal *paramValue = NULL;

			if(ui_CheckButtonGetState(pFxParamGroup->pParamCheckButton))
			{
				switch(pFxParamGroup->type)
				{
					case edptString:
					{
						const char *str = NULL;
						if(pFxParamGroup->pcDictName)
						{
							UIDictionaryEntry* pEntry = (UIDictionaryEntry*)pFxParamGroup->pParamWidget;
							str = ui_DictionaryEntryGetText(pEntry);
						}
						else
						{
							UITextEntry* pEntry = (UITextEntry*)pFxParamGroup->pParamWidget;
							str = ui_TextEntryGetText(pEntry);
						}

						paramValue = MultiValCreate();
						MultiValSetString(paramValue, str);
					}
					xcase edptNumber:
					{
						UISpinnerEntry* pEntry = (UISpinnerEntry*)pFxParamGroup->pParamWidget;
						F64 val = ui_SpinnerEntryGetValue(pEntry);

						paramValue = MultiValCreate();
						MultiValSetFloat(paramValue, val);
					}
					xcase edptVector:
					{
						UIMultiSpinnerEntry* pEntry = (UIMultiSpinnerEntry*)pFxParamGroup->pParamWidget;
						Vec3 vec;
						ui_MultiSpinnerEntryGetValue(pEntry, vec, 3);

						paramValue = MultiValCreate();
						MultiValSetVec3(paramValue, &vec);
					}
					xcase edptVector4:
					{
						UIMultiSpinnerEntry* pEntry = (UIMultiSpinnerEntry*)pFxParamGroup->pParamWidget;
						Vec4 vec4;
						ui_MultiSpinnerEntryGetValue(pEntry, vec4, 4);

						paramValue = MultiValCreate();
						MultiValSetVec4(paramValue, &vec4);
					}
				}
			}

			if(paramValue)
			{
				DynDefineParam *param = StructCreate(parse_DynDefineParam);
				param->pcParamName = allocAddString(pFxParamGroup->pcName);
				MultiValCopy(&param->mvVal, paramValue);
				eaPush(&pBlock->eaDefineParams, param);

				MultiValDestroy(paramValue);
			}
		}

		if(eaSize(&pBlock->eaDefineParams))
		{
			char *parserStr = NULL;
			ParserWriteText(&parserStr, parse_DynParamBlock, pBlock, 0, 0, 0);
			StructCopyString(&pFx->pcParams, parserStr);
			estrDestroy(&parserStr);
		}
		else
		{
			// No properties in the block? Just remove it entirely.
			StructFreeStringSafe(&pFx->pcParams);
		}
	}

	costumeEdit_CostumeChanged(pDoc);
}

static void OpenMaterialInEditor(UIDictionaryEntry *pEntry, UserData unused)
{
	emOpenFileEx(ui_DictionaryEntryGetText(pEntry), MATERIAL_DICT);
}

//
// This procedure updates the list of controls in the FX list UI
//
void costumeEdit_UpdateFX(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel = pDoc->pFxPanel;
	CostumeFxGroup *pFxGroup;
	int i,numFx;
	int y=0;
	NOCONST(PCFX) *pOldFx = NULL;

	// Free memory for excess FX groups
	for(i=eaSize(&pDoc->eaFxGroups)-1; i>=eaSize(&pDoc->pCostume->pArtistData->eaFX); --i) {
		MEFieldDestroy(pDoc->eaFxGroups[i]->pFxField);
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaFxGroups[i]->pRemoveButton));
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaFxGroups[i]->pSeparator));
		free(pDoc->eaFxGroups[i]);
		eaRemove(&pDoc->eaFxGroups,i);
	}

	// Add or change FX entry controls
	numFx = eaSize(&pDoc->pCostume->pArtistData->eaFX);
	for(i=0; i<numFx; ++i) {

		if (eaSize(&pDoc->eaFxGroups) <= i) {
			// Allocate new group
			pFxGroup = calloc(1,sizeof(CostumeFxGroup));
			pFxGroup->pDoc = pDoc;
			eaPush(&pDoc->eaFxGroups,pFxGroup);
		} else {
			pFxGroup = pDoc->eaFxGroups[i];
		}

		if ((pDoc->pOrigCostume) && (i < eaSize(&pDoc->pOrigCostume->pArtistData->eaFX))) {
			pOldFx = pDoc->pOrigCostume->pArtistData->eaFX[i];
		}

		if (!pFxGroup->pFxField) {
			pFxGroup->pFxField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFx, pDoc->pCostume->pArtistData->eaFX[i], parse_PCFX, "pcName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxGroup->pFxField, UI_WIDGET(emPanelGetExpander(pPanel)), 15, 0);
			MEFieldSetChangeCallback(pFxGroup->pFxField, costumeEdit_UIFieldFinishChanged, pDoc);
		} else {
			pFxGroup->pFxField->pOld = pOldFx;
			pFxGroup->pFxField->pNew = pDoc->pCostume->pArtistData->eaFX[i];
		}
		ui_WidgetSetPosition(pFxGroup->pFxField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxGroup->pFxField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxGroup->pFxField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxGroup->pFxField);

		if (!pFxGroup->pRemoveButton) {
			pFxGroup->pRemoveButton = ui_ButtonCreate("Remove",0,y + 14,costumeEdit_UIFXRemove,pFxGroup);
			ui_WidgetSetWidth(UI_WIDGET(pFxGroup->pRemoveButton), 70);
			emPanelAddChild(pPanel, pFxGroup->pRemoveButton, false);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pFxGroup->pRemoveButton), 0, y + 14, 0, 0, UITopRight);

		y += 28;

		{
			// FX Parameters.
			REF_TO(DynFxInfo) hInfo;
			
			// Get the info for this FX.
			SET_HANDLE_FROM_STRING(hDynFxInfoDict, pDoc->pCostume->pArtistData->eaFX[i]->pcName, hInfo);
			if (GET_REF(hInfo))
			{
				if (!pFxGroup->pFXButton)
				{
					pFxGroup->pFXButton = ui_FXButtonCreate(0, 0, GET_REF(hInfo), &(pDoc->pCostume->pArtistData->eaFX[i]->fHue), &pDoc->pCostume->pArtistData->eaFX[i]->pcParams);
					ui_WidgetSetPositionEx(UI_WIDGET(pFxGroup->pFXButton), 15, y, 0, 0, UITopLeft);
					ui_FXButtonSetChangedCallback(pFxGroup->pFXButton, costumeEdit_UpdateFXDrawFromWidget, pDoc);
					ui_FXButtonSetStopCallback(pFxGroup->pFXButton, costumeView_StopFx, pDoc->pGraphics);
					emPanelAddChild(pPanel, pFxGroup->pFXButton, false);
				}
				else
				{
					ui_FXButtonUpdate(pFxGroup->pFXButton, GET_REF(hInfo), &(pDoc->pCostume->pArtistData->eaFX[i]->fHue), &pDoc->pCostume->pArtistData->eaFX[i]->pcParams);
				}
			}
			else if (pFxGroup->pFXButton)
			{
				emPanelRemoveChild(pPanel, pFxGroup->pFXButton, false);
				ui_FXButtonDestroy(pFxGroup->pFXButton);
				pFxGroup->pFXButton = NULL;
			}
			REMOVE_HANDLE(hInfo);
		}

		y += 28;

		if (!pFxGroup->pSeparator) {
			pFxGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
			emPanelAddChild(pPanel, pFxGroup->pSeparator, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(pFxGroup->pSeparator), 15, y);
		y += 9;
	}

	// If no FX, then a default string
	if (numFx == 0) {
		if (!pDoc->pFxLabel) {
			pDoc->pFxLabel = ui_LabelCreate("No FX are currently defined",15,y);
			emPanelAddChild(pPanel, pDoc->pFxLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pFxLabel), 15, y);
		y+=20;
	} else {
		if (pDoc->pFxLabel) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pFxLabel));
			pDoc->pFxLabel = NULL;
		}
	}

	// Button to add FX
	if (!pDoc->pFxAddButton) {
		pDoc->pFxAddButton = ui_ButtonCreate("Add FX...", 15, y, costumeEdit_UIFXAdd, pDoc );
		pDoc->pFxAddButton->widget.width = 100;
		emPanelAddChild(pPanel, pDoc->pFxAddButton, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pFxAddButton), 15, y);
	y += 28;

	if (!pDoc->pDismountFxLabel) {
		pDoc->pDismountFxLabel = ui_LabelCreate("Dismount FX",15,y);
		emPanelAddChild(pPanel, pDoc->pDismountFxLabel, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pDismountFxLabel), 15, y);
	y+=20;

	pFxGroup = pDoc->pDismountFXGroup;
	if (!pFxGroup) {
		// Allocate new group
		pFxGroup = pDoc->pDismountFXGroup = calloc(1,sizeof(CostumeFxGroup));
		pFxGroup->pDoc = pDoc;
	}

	if (pDoc->pOrigCostume) {
		pOldFx = pDoc->pOrigCostume->pArtistData->pDismountFX;
	}

	if (!pFxGroup->pFxField && pDoc->pCostume->pArtistData->pDismountFX) {
		pFxGroup->pFxField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFx, pDoc->pCostume->pArtistData->pDismountFX, parse_PCFX, "pcName", "DynFxInfo", parse_DynFxInfo, "InternalName");
		MEFieldAddToParent(pFxGroup->pFxField, UI_WIDGET(emPanelGetExpander(pPanel)), 15, 0);
		MEFieldSetChangeCallback(pFxGroup->pFxField, costumeEdit_UIFieldFinishChanged, pDoc);
	} else {
		pFxGroup->pFxField->pOld = pOldFx;
		pFxGroup->pFxField->pNew = pDoc->pCostume->pArtistData->pDismountFX;
	}
	ui_WidgetSetPosition(pFxGroup->pFxField->pUIWidget, 15, y);
	ui_WidgetSetWidthEx(pFxGroup->pFxField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pFxGroup->pFxField->pUIWidget, 0, 80, 0, 0);
	MEFieldRefreshFromData(pFxGroup->pFxField);

	y += 28;

	{
		// FX Parameters.
		REF_TO(DynFxInfo) hInfo;

		// Get the info for this FX.
		SET_HANDLE_FROM_STRING(hDynFxInfoDict, pDoc->pCostume->pArtistData->pDismountFX->pcName, hInfo);
		if (GET_REF(hInfo))
		{
			if (!pFxGroup->pFXButton)
			{
				pFxGroup->pFXButton = ui_FXButtonCreate(0, 0, GET_REF(hInfo), &(pDoc->pCostume->pArtistData->pDismountFX->fHue), &pDoc->pCostume->pArtistData->pDismountFX->pcParams);
				ui_WidgetSetPositionEx(UI_WIDGET(pFxGroup->pFXButton), 15, y, 0, 0, UITopLeft);
				ui_FXButtonSetChangedCallback(pFxGroup->pFXButton, costumeEdit_UpdateDismountFXDrawFromWidget, pDoc);
				ui_FXButtonSetStopCallback(pFxGroup->pFXButton, costumeView_StopFx, pDoc->pGraphics);
				emPanelAddChild(pPanel, pFxGroup->pFXButton, false);
			}
			else
			{
				ui_FXButtonUpdate(pFxGroup->pFXButton, GET_REF(hInfo), &(pDoc->pCostume->pArtistData->pDismountFX->fHue), &pDoc->pCostume->pArtistData->pDismountFX->pcParams);
				ui_WidgetSetPositionEx(UI_WIDGET(pFxGroup->pFXButton), 15, y, 0, 0, UITopLeft);
			}
		}
		else if (pFxGroup->pFXButton)
		{
			emPanelRemoveChild(pPanel, pFxGroup->pFXButton, false);
			ui_FXButtonDestroy(pFxGroup->pFXButton);
			pFxGroup->pFXButton = NULL;
		}
		REMOVE_HANDLE(hInfo);
	}

	y += 28;

	if (!pFxGroup->pSeparator) {
		pFxGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
		emPanelAddChild(pPanel, pFxGroup->pSeparator, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pFxGroup->pSeparator), 15, y);
	y += 9;

	emPanelSetHeight(pPanel, y);
}


//
// This procedure updates the list of controls in the FX Swap list UI
//
void costumeEdit_UpdateFXSwap(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel = pDoc->pFxSwapPanel;
	CostumeFxSwapGroup *pFxSwapGroup;
	int i,numFxSwap;
	int y=0;

	// Free memory for excess FX groups
	for(i=eaSize(&pDoc->eaFxSwapGroups)-1; i>=eaSize(&pDoc->pCostume->pArtistData->eaFXSwap); --i) {
		MEFieldDestroy(pDoc->eaFxSwapGroups[i]->pFxOldField);
		MEFieldDestroy(pDoc->eaFxSwapGroups[i]->pFxNewField);
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaFxSwapGroups[i]->pRemoveButton));
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaFxSwapGroups[i]->pSeparator));
		free(pDoc->eaFxSwapGroups[i]);
		eaRemove(&pDoc->eaFxSwapGroups,i);
	}

	// Add or change FX Swap entry controls
	numFxSwap = eaSize(&pDoc->pCostume->pArtistData->eaFXSwap);
	for(i=0; i<numFxSwap; ++i) {
		NOCONST(PCFXSwap) *pOldFxSwap = NULL;

		if (eaSize(&pDoc->eaFxSwapGroups) <= i) {
			// Allocate new group
			pFxSwapGroup = calloc(1,sizeof(CostumeFxSwapGroup));
			pFxSwapGroup->pDoc = pDoc;
			eaPush(&pDoc->eaFxSwapGroups,pFxSwapGroup);
		} else {
			pFxSwapGroup = pDoc->eaFxSwapGroups[i];
		}

		if ((pDoc->pOrigCostume) && (i < eaSize(&pDoc->pOrigCostume->pArtistData->eaFXSwap))) {
			pOldFxSwap = pDoc->pOrigCostume->pArtistData->eaFXSwap[i];
		}

		if (!pFxSwapGroup->pFxOldField) {
			pFxSwapGroup->pFxOldField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFxSwap, pDoc->pCostume->pArtistData->eaFXSwap[i], parse_PCFXSwap, "pcOldName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxSwapGroup->pFxOldField, UI_WIDGET(emPanelGetExpander(pPanel)), 15, 0);
			MEFieldSetChangeCallback(pFxSwapGroup->pFxOldField, costumeEdit_UIFieldChanged, pDoc);
		} else {
			pFxSwapGroup->pFxOldField->pOld = pOldFxSwap;
			pFxSwapGroup->pFxOldField->pNew = pDoc->pCostume->pArtistData->eaFXSwap[i];
		}
		ui_WidgetSetPosition(pFxSwapGroup->pFxOldField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxSwapGroup->pFxOldField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxSwapGroup->pFxOldField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxSwapGroup->pFxOldField);

		if (!pFxSwapGroup->pRemoveButton) {
			pFxSwapGroup->pRemoveButton = ui_ButtonCreate("Remove",0,y + 14,costumeEdit_UIFXSwapRemove,pFxSwapGroup);
			ui_WidgetSetWidth(UI_WIDGET(pFxSwapGroup->pRemoveButton), 70);
			emPanelAddChild(pPanel,pFxSwapGroup->pRemoveButton, false);
		}
		ui_WidgetSetPositionEx(UI_WIDGET(pFxSwapGroup->pRemoveButton), 0, y + 14, 0, 0, UITopRight);

		y += 28;

		if (!pFxSwapGroup->pFxNewField) {
			pFxSwapGroup->pFxNewField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pOldFxSwap, pDoc->pCostume->pArtistData->eaFXSwap[i], parse_PCFXSwap, "pcNewName", "DynFxInfo", parse_DynFxInfo, "InternalName");
			MEFieldAddToParent(pFxSwapGroup->pFxNewField, UI_WIDGET(emPanelGetExpander(pPanel)), 15, 0);
			MEFieldSetChangeCallback(pFxSwapGroup->pFxNewField, costumeEdit_UIFieldChanged, pDoc);
		} else {
			pFxSwapGroup->pFxNewField->pOld = pOldFxSwap;
			pFxSwapGroup->pFxNewField->pNew = pDoc->pCostume->pArtistData->eaFXSwap[i];
		}
		ui_WidgetSetPosition(pFxSwapGroup->pFxNewField->pUIWidget, 15, y);
		ui_WidgetSetWidthEx(pFxSwapGroup->pFxNewField->pUIWidget, 1, UIUnitPercentage);
		ui_WidgetSetPaddingEx(pFxSwapGroup->pFxNewField->pUIWidget, 0, 80, 0, 0);
		MEFieldRefreshFromData(pFxSwapGroup->pFxNewField);

		y += 28;

		if (!pFxSwapGroup->pSeparator) {
			pFxSwapGroup->pSeparator = ui_SeparatorCreate(UIHorizontal);
			emPanelAddChild(pPanel, pFxSwapGroup->pSeparator, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(pFxSwapGroup->pSeparator), 15, y);
		y += 9;
	}

	// If no FX, then a default string
	if (numFxSwap == 0) {
		if (!pDoc->pFxSwapLabel) {
			pDoc->pFxSwapLabel = ui_LabelCreate("No FX Swaps are currently defined",15,y);
			emPanelAddChild(pPanel, pDoc->pFxSwapLabel, false);
		}
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pFxSwapLabel), 15, y);
		y += 20;
	} else {
		if (pDoc->pFxSwapLabel) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pFxSwapLabel));
			pDoc->pFxSwapLabel = NULL;
		}
	}

	// Button to add FX
	if (!pDoc->pFxSwapAddButton) {
		pDoc->pFxSwapAddButton = ui_ButtonCreate("Add FX Swap...", 15, y, costumeEdit_UIFXSwapAdd, pDoc );
		pDoc->pFxSwapAddButton->widget.width = 100;
		emPanelAddChild(pPanel, pDoc->pFxSwapAddButton, false);
	}
	ui_WidgetSetPosition(UI_WIDGET(pDoc->pFxSwapAddButton), 15, y);
	y += 28;

	emPanelSetHeight(pPanel, y);
}


static void costumeEdit_UpdateScale(CostumeEditDoc *pDoc)
{
	int i, j;

	// Refresh scale controls from data
	for(i=eaSize(&pDoc->pCostume->eaScaleValues)-1; i>=0; --i) {
		for(j=eaSize(&pDoc->eaScaleGroups)-1; j>=0; --j) {
			if (pDoc->eaScaleGroups[j]->pSliderField &&
				stricmp(pDoc->pCostume->eaScaleValues[i]->pcScaleName, pDoc->eaScaleGroups[j]->pcName) == 0) {
				MEFieldRefreshFromData(pDoc->eaScaleGroups[j]->pSliderField);
				break;
			}
		}
	}
}


static const char *costumeEdit_GetMatConstDispName(CostumeEditDoc *pDoc, PCMaterialDef *pMat, const char *pcName, int index)
{
	const char *name;
	static char temp[1024];

	if (!pcName) return NULL;
	if (index < 0) return NULL;
	if (costumeTailor_GetMatConstantNumValues(pcName) <= 1) return pcName;

	name = costumeTailor_GetMatConstantValueName(pcName, index);
	sprintf(temp, "%s %s", name, pcName);

	return temp;
}


static void costumeEdit_UIScaleSeperateChanged(UICheckButton *pCheck, CostumeEditDoc *pDoc)
{
	pDoc->bScaleSeperateOverride = ui_CheckButtonGetState(pCheck);

	// UpdateUI
	costumeEdit_CostumeChanged(pDoc);
}

static void costumeEdit_UIMovableOverrideChanged(UICheckButton *pCheck, CostumeEditDoc *pDoc)
{
	pDoc->bAllowMovableOverride = ui_CheckButtonGetState(pCheck);

	// UpdateUI
	costumeEdit_CostumeChanged(pDoc);
}

extern DictionaryHandle g_hItemDict;

static void costumeEdit_LoadDyePacks(void)
{
	RefDictIterator iter = {0};
	ItemDef *pDef;

	RefSystem_InitRefDictIterator(g_hItemDict, &iter);

	if (!s_eaDyePacks)
	{
		eaCreate(&s_eaDyePacks);
	} 
	else
	{
		eaClearStruct(&s_eaDyePacks, parse_CostumeEditDyePackStruct);
	}

	while(pDef = (ItemDef*)RefSystem_GetNextReferentFromIterator(&iter))
	{
		if(pDef->eType == kItemType_DyePack)
		{
			CostumeEditDyePackStruct *pDyePack = StructCreate(parse_CostumeEditDyePackStruct);
			pDyePack->pchDyePack = pDef->pchName;
			//This isn't used right now, so we might as well not waste time
			//pDyePack->pchDisplayName = allocAddString(TranslateDisplayMessage(pDef->displayNameMsg));
			eaPush(&s_eaDyePacks, pDyePack);
		}
	}
}

static void costumeEdit_UpdateLists(CostumeEditDoc *pDoc)
{
	PCRegion *pRegion = NULL;
	PCCategory *pCategory = NULL;
	PCBoneDef *pBone = NULL;
	PCSkeletonDef *pSkel;
	PCGeometryDef *pGeo = NULL; 
	PCGeometryDef *pChildGeo = NULL;
	NOCONST(PCPart) *pPart;
	NOCONST(PCPart) **eaChildParts = NULL;
	NOCONST(PCPart) *pChildPart = NULL;
	NOCONST(PCPart) *pLayerPart;
	NOCONST(PCPart) *pLayerOrigPart;
	PCGeometryDef *pLayerGeo = NULL;
	PCMaterialDef *pLayerMat = NULL;
	PCLayerType eLayerType;
	int i;
	bool bFound, bCreate;
	Vec4 color0Temp, color1Temp, color2Temp, color3Temp;
	Material *pMaterial = NULL;

	// Ignore change callbacks while in the process of updating the fields
	pDoc->bIgnoreFieldChanges = true;

	// Track whether or not skeleton changed to disable many functions
	if (pDoc->pOrigCostume && (GET_REF(pDoc->pOrigCostume->hSkeleton) == GET_REF(pDoc->pCostume->hSkeleton))) {
		pDoc->bSkelChanged = false;
	} else {
		pDoc->bSkelChanged = true;
	}

	// Collect info on current part
	pPart = pDoc->pCurrentPart;
	pSkel = GET_REF(pDoc->pCostume->hSkeleton);
	if (pPart) {
		pBone = GET_REF(pPart->hBoneDef);
	}

	// Populate skeleton list
	costumeTailor_GetValidSkeletons(pDoc->pCostume, NULL, &pDoc->eaSkels, false, gUseDispNames);
	if (eaSize(&pDoc->eaSkels)) {
		ui_SetActive(pDoc->pSkeletonField->pUIWidget, true);
	} else {
		ui_SetActive(pDoc->pSkeletonField->pUIWidget, false);
	}

	// Populate species list
	costumeTailor_GetValidSpecies(pDoc->pCostume, &pDoc->eaSpecies, gUseDispNames, true);
	if (eaSize(&pDoc->eaSpecies)) {
		ui_SetActive(pDoc->pSpeciesField->pUIWidget, true);
	} else {
		ui_SetActive(pDoc->pSpeciesField->pUIWidget, false);
	}

	// Populate voice list
	costumeTailor_GetValidVoices(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), &pDoc->eaVoices, gUseDispNames, NULL);
	if (eaSize(&pDoc->eaVoices)) {
		ui_SetActive(pDoc->pVoiceField->pUIWidget, true);
	} else {
		ui_SetActive(pDoc->pVoiceField->pUIWidget, false);
	}

	// Populate stance list
	costumeTailor_GetValidStances(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, &pDoc->eaStances, gUseDispNames, NULL, gCEUnlockAll);

	// Validate stance choice
	costumeTailor_PickValidStance(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, gCEUnlockAll);

	// Populate region list
	costumeTailor_GetValidRegions(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, NULL, &pDoc->eaRegions, CGVF_OMIT_EMPTY | (gUseDispNames ? CGVF_SORT_DISPLAY : 0) | (gCEUnlockAll ? CGVF_UNLOCK_ALL : 0));
	if (eaSize(&pDoc->eaRegions) > 1) {
		ui_SetActive(UI_WIDGET(pDoc->pRegionCombo), true);
	} else {
		ui_SetActive(UI_WIDGET(pDoc->pRegionCombo), false);
	}
	pRegion = pDoc->pCurrentRegion;
	if (pRegion && (eaFind(&pDoc->eaRegions, pRegion) < 0)) {
		pRegion = NULL;
	}

	// Populate category list
	if (pRegion) {
		costumeTailor_GetValidCategories(pDoc->pCostume, pRegion, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, NULL, &pDoc->eaCategories, CGVF_OMIT_EMPTY | (gUseDispNames ? CGVF_SORT_DISPLAY : 0) | (gCEUnlockAll ? CGVF_UNLOCK_ALL : 0));
	} else {
		eaClear(&pDoc->eaCategories);
	}
	if (eaSize(&pDoc->eaCategories) > 1) {
		ui_SetActive(UI_WIDGET(pDoc->pCategoryCombo), true);
	} else {
		ui_SetActive(UI_WIDGET(pDoc->pCategoryCombo), false);
	}
	pCategory = pDoc->pCurrentCategory;
	if (pCategory && (eaFind(&pDoc->eaCategories, pCategory) < 0)) {
		pCategory = NULL;
	}

	// Populate bone list
	if (pRegion && pCategory) {
		costumeTailor_GetValidBones(pDoc->pCostume, GET_REF(pDoc->pCostume->hSkeleton), pRegion, pCategory, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, &pDoc->eaBones, CGVF_OMIT_EMPTY | (gUseDispNames ? CGVF_SORT_DISPLAY : 0) | (gCEUnlockAll ? CGVF_UNLOCK_ALL : 0));
	} else {
		eaClear(&pDoc->eaBones);
	}
	if (eaSize(&pDoc->eaBones) > 1) {
		ui_SetActive(UI_WIDGET(pDoc->pBoneCombo), true);
	} else {
		ui_SetActive(UI_WIDGET(pDoc->pBoneCombo), false);
	}

	if (pBone && GET_REF(pBone->geometryFieldDispName.hMessage))
	{
		ui_LabelSetText(pDoc->pGeometryTextLabel, TranslateDisplayMessage(pBone->geometryFieldDispName));
	}
	else
	{
		ui_LabelSetText(pDoc->pGeometryTextLabel, "Geometry");
	}
	if (pBone && GET_REF(pBone->materialFieldDispName.hMessage))
	{
		ui_LabelSetText(pDoc->pMaterialTextLabel, TranslateDisplayMessage(pBone->materialFieldDispName));
	}
	else
	{
		ui_LabelSetText(pDoc->pMaterialTextLabel, "Material");
	}
	if (pBone && GET_REF(pBone->patternFieldDispName.hMessage))
	{
		ui_LabelSetText(pDoc->pPatternTextLabel, TranslateDisplayMessage(pBone->patternFieldDispName));
	}
	else
	{
		ui_LabelSetText(pDoc->pPatternTextLabel, "Pattern");
	}
	if (pBone && GET_REF(pBone->detailFieldDisplayName.hMessage))
	{
		ui_LabelSetText(pDoc->pDetailTextLabel, TranslateDisplayMessage(pBone->detailFieldDisplayName));
	}
	else
	{
		ui_LabelSetText(pDoc->pDetailTextLabel, "Detail");
	}
	if (pBone && GET_REF(pBone->specularFieldDisplayName.hMessage))
	{
		ui_LabelSetText(pDoc->pSpecularTextLabel, TranslateDisplayMessage(pBone->specularFieldDisplayName));
	}
	else
	{
		ui_LabelSetText(pDoc->pSpecularTextLabel, "Specular");
	}
	if (pBone && GET_REF(pBone->diffuseFieldDisplayName.hMessage))
	{
		ui_LabelSetText(pDoc->pDiffuseTextLabel, TranslateDisplayMessage(pBone->diffuseFieldDisplayName));
	}
	else
	{
		ui_LabelSetText(pDoc->pDiffuseTextLabel, "Diffuse");
	}
	if (pBone && GET_REF(pBone->movableFieldDisplayName.hMessage))
	{
		ui_LabelSetText(pDoc->pMovableTextLabel, TranslateDisplayMessage(pBone->movableFieldDisplayName));
	}
	else
	{
		ui_LabelSetText(pDoc->pMovableTextLabel, "Movable");
	}

	// Populate geometry list
	if (pBone && pCategory) {
		costumeTailor_GetValidGeos(pDoc->pCostume, GET_REF(pDoc->pCostume->hSkeleton), pBone, pCategory, GET_REF(pDoc->pCostume->hSpecies), NULL, &pDoc->eaGeos, false, false, gUseDispNames, gCEUnlockAll);
	} else {
		eaClear(&pDoc->eaGeos);
	}
	if (eaSize(&pDoc->eaGeos) > 1) {
		ui_SetActive(pDoc->pGeoField->pUIWidget, true);
	} else {
		ui_SetActive(pDoc->pGeoField->pUIWidget, false);
	}

	// Validate geometry choice
	if (pPart) {
		costumeTailor_PickValidGeometry(pDoc->pCostume, pPart, GET_REF(pDoc->pCostume->hSpecies), pDoc->eaGeos, NULL, gCEUnlockAll);
	}
	if (pPart) {
		pGeo = GET_REF(pPart->hGeoDef);
	}

	// Validate child geometry choices
	if (pPart) {
		for(i=eaSize(&pBone->eaChildBones)-1; i>=0; --i) {
			PCChildBone *pBoneInfo = pBone->eaChildBones[i];

			// Populate child geometry list
			if (pGeo && pCategory) {
				costumeTailor_GetValidChildGeos(pDoc->pCostume, pCategory, pGeo, GET_REF(pBoneInfo->hChildBone), GET_REF(pDoc->pCostume->hSpecies), NULL, &pDoc->eaChildGeos, gUseDispNames, gCEUnlockAll);
			} else {
				eaClear(&pDoc->eaChildGeos);
			}
			// Validate geometry choice
			costumeTailor_PickValidChildGeometry(pDoc->pCostume, pPart, GET_REF(pBoneInfo->hChildBone), GET_REF(pDoc->pCostume->hSpecies), pDoc->eaChildGeos, NULL, gCEUnlockAll);
		}
	}

	// Populate layer choice
	if (pGeo) {
		costumeTailor_GetValidLayers(pDoc->pCostume, pGeo, NULL, &pDoc->eaLayers, gUseDispNames);
	} else {
		eaClear(&pDoc->eaLayers);
	}
	if (eaSize(&pDoc->eaLayers) > 1) {
		ui_SetActive(UI_WIDGET(pDoc->pLayerCombo), true);
	} else {
		ui_SetActive(UI_WIDGET(pDoc->pLayerCombo), false);
	}

	// Validate layer choice
	if (pPart) {
		pDoc->pCurrentLayer = costumeTailor_PickValidLayer(pDoc->pCostume, pPart, pDoc->pCurrentLayer, pDoc->eaLayers);
	} else {
		pDoc->pCurrentLayer = NULL;
	}

	// Get right child part for chosen layer
	if (pPart && pDoc->pCurrentLayer) {
		costumeTailor_GetChildParts(pDoc->pCostume, pPart, pDoc->pCurrentLayer, &eaChildParts);
		if (eaSize(&eaChildParts)) {
			pChildPart = eaChildParts[0];
			pChildGeo = GET_REF(pChildPart->hGeoDef);
		}
		eaDestroy(&eaChildParts);
	}
	if (pChildGeo) {
		costumeTailor_GetValidChildGeos(pDoc->pCostume, pCategory, pGeo, GET_REF(pChildGeo->hBone), GET_REF(pDoc->pCostume->hSpecies), NULL, &pDoc->eaChildGeos, gUseDispNames, gCEUnlockAll);
	} else {
		eaClear(&pDoc->eaChildGeos);
	}
	if (eaSize(&pDoc->eaChildGeos) > 1) {
		ui_SetActive(pDoc->pChildGeoField->pUIWidget, true);
	} else {
		ui_SetActive(pDoc->pChildGeoField->pUIWidget, false);
	}

	// Update Geo link
	if (pPart) {
		//ui_CheckButtonSetState(pDoc->pGeoLinkButton, pPart->bGeoLink);
	}

	// The following fields and UI depend on the layer choice, so make layer-specific
	// variables to handle from here on

	if (!pDoc->pCurrentLayer || (pDoc->pCurrentLayer->eLayerArea == kPCLayerArea_Main)) {
		eLayerType = pDoc->pCurrentLayer ? pDoc->pCurrentLayer->eLayerType : kPCLayerType_Front;
		if (eLayerType == kPCLayerType_Front) {
			pLayerPart = pPart;
			pLayerOrigPart = pDoc->pOrigPart;
		} else {
			pLayerPart = pPart->pClothLayer;
			pLayerOrigPart = pDoc->pOrigPart ? pDoc->pOrigPart->pClothLayer : NULL;
		}
		pLayerGeo = pGeo;
	} else {
		eLayerType = pDoc->pCurrentLayer->eLayerType;
		costumeTailor_GetChildParts(pDoc->pOrigCostume, pDoc->pOrigPart, pDoc->pCurrentLayer, &eaChildParts);
		if (eaSize(&eaChildParts)) {
			pLayerOrigPart = eaChildParts[0];
		} else {
			pLayerOrigPart = NULL;
		}
		eaDestroy(&eaChildParts);
		if (eLayerType == kPCLayerType_Front) {
			pLayerPart = pChildPart;
		} else {
			pLayerPart = pChildPart->pClothLayer;
		}
		pLayerGeo = pChildGeo;
	}

	costumeEdit_LoadDyePacks();

	// Check color button part
	if (pDoc->pColorButtonCurrentPart != pLayerPart) {
		// Cancel any open color button windows with old part
		ui_ColorButtonCancelWindow(pDoc->pCButton0);
		ui_ColorButtonCancelWindow(pDoc->pCButton1);
		ui_ColorButtonCancelWindow(pDoc->pCButton2);
		ui_ColorButtonCancelWindow(pDoc->pCButton3);

		// Switch to using the correct part
		pDoc->pColorButtonCurrentPart = pLayerPart;
		if (pDoc->pColorButtonCurrentPart && !pDoc->pColorButtonCurrentPart->pArtistData) {
			pDoc->pColorButtonCurrentPart->pArtistData = StructCreateNoConst(parse_PCArtistPartData);
		}
	}

	// Populate materials list
	if (pLayerGeo) {
		costumeTailor_GetValidMaterials(pDoc->pCostume, pLayerGeo, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, NULL, &pDoc->eaMats, false, gUseDispNames, gCEUnlockAll);
	} else {
		eaClear(&pDoc->eaMats);
	}
	if (eaSize(&pDoc->eaMats) > 1) {
		ui_SetActive(pDoc->pMatField->pUIWidget, true);
	} else {
		ui_SetActive(pDoc->pMatField->pUIWidget, false);
	}

	// Validate material choice
	if (pLayerPart) {
		costumeTailor_PickValidMaterial(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), pLayerPart, NULL, pDoc->eaMats, gCEUnlockAll, false);
	}
	if (pLayerPart) {
		pLayerMat = GET_REF(pLayerPart->hMatDef);
	}

	// Populate texture lists
	if (pLayerMat && pLayerPart) {
		costumeTailor_GetValidTextures(pDoc->pCostume, pLayerMat, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, pGeo, NULL, NULL, kPCTextureType_Pattern, &pDoc->eaPatternTex, false, gUseDispNames, gCEUnlockAll);
		costumeTailor_GetValidTextures(pDoc->pCostume, pLayerMat, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, pGeo, NULL, NULL, kPCTextureType_Detail, &pDoc->eaDetailTex, false, gUseDispNames, gCEUnlockAll);
		costumeTailor_GetValidTextures(pDoc->pCostume, pLayerMat, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, pGeo, NULL, NULL, kPCTextureType_Specular, &pDoc->eaSpecularTex, false, gUseDispNames, gCEUnlockAll);
		costumeTailor_GetValidTextures(pDoc->pCostume, pLayerMat, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, pGeo, NULL, NULL, kPCTextureType_Diffuse, &pDoc->eaDiffuseTex, false, gUseDispNames, gCEUnlockAll);
		costumeTailor_GetValidTextures(pDoc->pCostume, pLayerMat, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, pGeo, NULL, NULL, kPCTextureType_Movable, &pDoc->eaMovableTex, false, gUseDispNames, gCEUnlockAll);
	} else {
		eaClear(&pDoc->eaPatternTex);
		eaClear(&pDoc->eaDetailTex);
		eaClear(&pDoc->eaSpecularTex);
		eaClear(&pDoc->eaDiffuseTex);
		eaClear(&pDoc->eaMovableTex);
	}

	// Validate texture choice.  Apply extra textures to clear sublists.
	if (pLayerPart) {
		costumeTailor_PickValidTexture(pDoc->pCostume, pLayerPart, GET_REF(pDoc->pCostume->hSpecies), kPCTextureType_Pattern, pDoc->eaPatternTex);
		if (costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hPatternTexture)) & kPCTextureType_Detail) {
			eaClear(&pDoc->eaDetailTex);
		}
		costumeTailor_PickValidTexture(pDoc->pCostume, pLayerPart, GET_REF(pDoc->pCostume->hSpecies), kPCTextureType_Detail, pDoc->eaDetailTex);
		if ((costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hPatternTexture)) & kPCTextureType_Specular) ||
			(costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hDetailTexture)) & kPCTextureType_Specular)) {
			eaClear(&pDoc->eaSpecularTex);
		}
		costumeTailor_PickValidTexture(pDoc->pCostume, pLayerPart, GET_REF(pDoc->pCostume->hSpecies), kPCTextureType_Specular, pDoc->eaSpecularTex);
		if ((costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hPatternTexture)) & kPCTextureType_Diffuse) ||
			(costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hDetailTexture)) & kPCTextureType_Diffuse) ||
			(costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hSpecularTexture)) & kPCTextureType_Diffuse)) {
			eaClear(&pDoc->eaDiffuseTex);
		}
		costumeTailor_PickValidTexture(pDoc->pCostume, pLayerPart, GET_REF(pDoc->pCostume->hSpecies), kPCTextureType_Diffuse, pDoc->eaDiffuseTex);
		if ((costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hPatternTexture)) & kPCTextureType_Movable) ||
			(costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hDetailTexture)) & kPCTextureType_Movable) ||
			(costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hSpecularTexture)) & kPCTextureType_Movable) ||
			(costumeTailor_GetTextureFlags(GET_REF(pLayerPart->hDiffuseTexture)) & kPCTextureType_Movable)) {
			eaClear(&pDoc->eaMovableTex);
		}
		costumeTailor_PickValidTexture(pDoc->pCostume, pLayerPart, GET_REF(pDoc->pCostume->hSpecies), kPCTextureType_Movable, pDoc->eaMovableTex);
	}

	if (pLayerPart && !pLayerPart->pTextureValues) {
		pLayerPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
	}

	// Enable controls if appropriate
	bCreate = false;
	if (eaSize(&pDoc->eaPatternTex) > 1) {
		ui_SetActive(pDoc->pPatternField->pUIWidget, true);
		if (pLayerPart
			&& GET_REF(pLayerPart->hPatternTexture)
			&& GET_REF(pLayerPart->hPatternTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hPatternTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hPatternTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	} else {
		ui_SetActive(pDoc->pPatternField->pUIWidget, false);
		if (eaSize(&pDoc->eaPatternTex) > 0
			&& pLayerPart
			&& GET_REF(pLayerPart->hPatternTexture)
			&& GET_REF(pLayerPart->hPatternTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hPatternTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hPatternTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	}
	if (bCreate)
	{
		F32 x = 15;
		if (!pDoc->pPatternValueField)
		{
			UILabel *pLabel;
			MEField *pField;
			EMPanel *pPanel = pDoc->pCostumePartPanel;
			F32 y = pDoc->pPatternField->pUIWidget->y+28;
			pLabel = ui_LabelCreate("Value", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL, pLayerPart->pTextureValues, parse_PCTextureValueInfo, "PatternValue");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pPatternValueLabel = pLabel;
			pDoc->pPatternValueField = pField;
		}
		else
		{
			pDoc->pPatternValueField->pOld = pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL;
			pDoc->pPatternValueField->pNew = pLayerPart->pTextureValues;
			MEFieldRefreshFromData(pDoc->pPatternValueField);
			ui_WidgetSetPosition(pDoc->pPatternValueField->pUIWidget, x+125, pDoc->pPatternField->pUIWidget->y+28);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pPatternValueLabel), x, pDoc->pPatternField->pUIWidget->y+28);
		}
		ui_LabelSetText(pDoc->pPatternValueLabel, costumeEdit_GetMatConstDispName(pDoc, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hPatternTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hPatternTexture)->pValueOptions->iValConstIndex));
		ui_WidgetSetPosition(pDoc->pDetailField->pUIWidget, pDoc->pPatternField->pUIWidget->x, pDoc->pPatternField->pUIWidget->y+28+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pDetailTextLabel), UI_WIDGET(pDoc->pPatternTextLabel)->x, UI_WIDGET(pDoc->pPatternTextLabel)->y+28+28);
	}
	else
	{
		if (pDoc->pPatternValueField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pPatternValueLabel));
			MEFieldDestroy(pDoc->pPatternValueField);
			pDoc->pPatternValueField = NULL;
		}
		ui_WidgetSetPosition(pDoc->pDetailField->pUIWidget, pDoc->pPatternField->pUIWidget->x, pDoc->pPatternField->pUIWidget->y+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pDetailTextLabel), UI_WIDGET(pDoc->pPatternTextLabel)->x, UI_WIDGET(pDoc->pPatternTextLabel)->y+28);
	}
	bCreate = false;
	if (eaSize(&pDoc->eaDetailTex) > 1) {
		ui_SetActive(pDoc->pDetailField->pUIWidget, true);
		if (pLayerPart
			&& GET_REF(pLayerPart->hDetailTexture)
			&& GET_REF(pLayerPart->hDetailTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hDetailTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hDetailTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	} else {
		ui_SetActive(pDoc->pDetailField->pUIWidget, false);
		if (eaSize(&pDoc->eaDetailTex) > 0
			&& pLayerPart
			&& GET_REF(pLayerPart->hDetailTexture)
			&& GET_REF(pLayerPart->hDetailTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hDetailTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hDetailTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	}
	if (bCreate)
	{
		F32 x = 15;
		if (!pDoc->pDetailValueField)
		{
			UILabel *pLabel;
			MEField *pField;
			EMPanel *pPanel = pDoc->pCostumePartPanel;
			F32 y = pDoc->pDetailField->pUIWidget->y+28;
			pLabel = ui_LabelCreate("Value", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL, pLayerPart->pTextureValues, parse_PCTextureValueInfo, "DetailValue");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pDetailValueLabel = pLabel;
			pDoc->pDetailValueField = pField;
		}
		else
		{
			pDoc->pDetailValueField->pOld = pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL;
			pDoc->pDetailValueField->pNew = pLayerPart->pTextureValues;
			MEFieldRefreshFromData(pDoc->pDetailValueField);
			ui_WidgetSetPosition(pDoc->pDetailValueField->pUIWidget, x+125, pDoc->pDetailField->pUIWidget->y+28);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pDetailValueLabel), x, pDoc->pDetailField->pUIWidget->y+28);
		}
		ui_LabelSetText(pDoc->pDetailValueLabel, costumeEdit_GetMatConstDispName(pDoc, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hDetailTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hDetailTexture)->pValueOptions->iValConstIndex));
		ui_WidgetSetPosition(pDoc->pSpecularField->pUIWidget, pDoc->pDetailField->pUIWidget->x, pDoc->pDetailField->pUIWidget->y+28+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pSpecularTextLabel), UI_WIDGET(pDoc->pDetailTextLabel)->x, UI_WIDGET(pDoc->pDetailTextLabel)->y+28+28);
	}
	else
	{
		if (pDoc->pDetailValueField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pDetailValueLabel));
			MEFieldDestroy(pDoc->pDetailValueField);
			pDoc->pDetailValueField = NULL;
		}
		ui_WidgetSetPosition(pDoc->pSpecularField->pUIWidget, pDoc->pDetailField->pUIWidget->x, pDoc->pDetailField->pUIWidget->y+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pSpecularTextLabel), UI_WIDGET(pDoc->pDetailTextLabel)->x, UI_WIDGET(pDoc->pDetailTextLabel)->y+28);
	}
	bCreate = false;
	if (eaSize(&pDoc->eaSpecularTex) > 1) {
		ui_SetActive(pDoc->pSpecularField->pUIWidget, true);
		if (pLayerPart
			&& GET_REF(pLayerPart->hSpecularTexture)
			&& GET_REF(pLayerPart->hSpecularTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hSpecularTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hSpecularTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	} else {
		ui_SetActive(pDoc->pSpecularField->pUIWidget, false);
		if (eaSize(&pDoc->eaSpecularTex) > 0
			&& pLayerPart
			&& GET_REF(pLayerPart->hSpecularTexture)
			&& GET_REF(pLayerPart->hSpecularTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hSpecularTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hSpecularTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	}
	if (bCreate)
	{
		F32 x = 15;
		if (!pDoc->pSpecularValueField)
		{
			UILabel *pLabel;
			MEField *pField;
			EMPanel *pPanel = pDoc->pCostumePartPanel;
			F32 y = pDoc->pSpecularField->pUIWidget->y+28;
			pLabel = ui_LabelCreate("Value", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL, pLayerPart->pTextureValues, parse_PCTextureValueInfo, "SpecularValue");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pSpecularValueLabel = pLabel;
			pDoc->pSpecularValueField = pField;
		}
		else
		{
			pDoc->pSpecularValueField->pOld = pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL;
			pDoc->pSpecularValueField->pNew = pLayerPart->pTextureValues;
			MEFieldRefreshFromData(pDoc->pSpecularValueField);
			ui_WidgetSetPosition(pDoc->pSpecularValueField->pUIWidget, x+125, pDoc->pSpecularField->pUIWidget->y+28);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pSpecularValueLabel), x, pDoc->pSpecularField->pUIWidget->y+28);
		}
		ui_LabelSetText(pDoc->pSpecularValueLabel, costumeEdit_GetMatConstDispName(pDoc, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hSpecularTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hSpecularTexture)->pValueOptions->iValConstIndex));
		ui_WidgetSetPosition(pDoc->pDiffuseField->pUIWidget, pDoc->pSpecularField->pUIWidget->x, pDoc->pSpecularField->pUIWidget->y+28+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pDiffuseTextLabel), UI_WIDGET(pDoc->pSpecularTextLabel)->x, UI_WIDGET(pDoc->pSpecularTextLabel)->y+28+28);
	}
	else
	{
		if (pDoc->pSpecularValueField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pSpecularValueLabel));
			MEFieldDestroy(pDoc->pSpecularValueField);
			pDoc->pSpecularValueField = NULL;
		}
		ui_WidgetSetPosition(pDoc->pDiffuseField->pUIWidget, pDoc->pSpecularField->pUIWidget->x, pDoc->pSpecularField->pUIWidget->y+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pDiffuseTextLabel), UI_WIDGET(pDoc->pSpecularTextLabel)->x, UI_WIDGET(pDoc->pSpecularTextLabel)->y+28);
	}
	bCreate = false;
	if (eaSize(&pDoc->eaDiffuseTex) > 1) {
		ui_SetActive(pDoc->pDiffuseField->pUIWidget, true);
		if (pLayerPart
			&& GET_REF(pLayerPart->hDiffuseTexture)
			&& GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	} else {
		ui_SetActive(pDoc->pDiffuseField->pUIWidget, false);
		if (eaSize(&pDoc->eaDiffuseTex) > 0
			&& pLayerPart
			&& GET_REF(pLayerPart->hDiffuseTexture)
			&& GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	}
	if (bCreate)
	{
		F32 x = 15;
		if (!pDoc->pDiffuseValueField)
		{
			UILabel *pLabel;
			MEField *pField;
			EMPanel *pPanel = pDoc->pCostumePartPanel;
			F32 y = pDoc->pDiffuseField->pUIWidget->y+28;
			pLabel = ui_LabelCreate("Value", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL, pLayerPart->pTextureValues, parse_PCTextureValueInfo, "DiffuseValue");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pDiffuseValueLabel = pLabel;
			pDoc->pDiffuseValueField = pField;
		}
		else
		{
			pDoc->pDiffuseValueField->pOld = pLayerOrigPart ? pLayerOrigPart->pTextureValues : NULL;
			pDoc->pDiffuseValueField->pNew = pLayerPart->pTextureValues;
			MEFieldRefreshFromData(pDoc->pDiffuseValueField);
			ui_WidgetSetPosition(pDoc->pDiffuseValueField->pUIWidget, x+125, pDoc->pDiffuseField->pUIWidget->y+28);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pDiffuseValueLabel), x, pDoc->pDiffuseField->pUIWidget->y+28);
		}
		ui_LabelSetText(pDoc->pDiffuseValueLabel, costumeEdit_GetMatConstDispName(pDoc, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hDiffuseTexture)->pValueOptions->iValConstIndex));
		ui_WidgetSetPosition(pDoc->pMovableField->pUIWidget, pDoc->pDiffuseField->pUIWidget->x, pDoc->pDiffuseField->pUIWidget->y+28+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovableTextLabel), UI_WIDGET(pDoc->pDiffuseTextLabel)->x, UI_WIDGET(pDoc->pDiffuseTextLabel)->y+28+28);
	}
	else
	{
		if (pDoc->pDiffuseValueField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pDiffuseValueLabel));
			MEFieldDestroy(pDoc->pDiffuseValueField);
			pDoc->pDiffuseValueField = NULL;
		}
		ui_WidgetSetPosition(pDoc->pMovableField->pUIWidget, pDoc->pDiffuseField->pUIWidget->x, pDoc->pDiffuseField->pUIWidget->y+28);
		ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovableTextLabel), UI_WIDGET(pDoc->pDiffuseTextLabel)->x, UI_WIDGET(pDoc->pDiffuseTextLabel)->y+28);
	}
	bCreate = false;
	if (eaSize(&pDoc->eaMovableTex) > 1) {
		ui_SetActive(pDoc->pMovableField->pUIWidget, true);
		if (pLayerPart
			&& GET_REF(pLayerPart->pMovableTexture->hMovableTexture)
			&& GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	} else {
		ui_SetActive(pDoc->pMovableField->pUIWidget, false);
		if (eaSize(&pDoc->eaMovableTex) > 0
			&& pLayerPart
			&& GET_REF(pLayerPart->pMovableTexture->hMovableTexture)
			&& GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions
			&& costumeTailor_IsSliderConstValid(GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions->iValConstIndex))
		{
			bCreate = true;
		}
	}
	if (bCreate)
	{
		F32 x = 15;
		if (!pDoc->pMovableValueField)
		{
			UILabel *pLabel;
			MEField *pField;
			EMPanel *pPanel = pDoc->pCostumePartPanel;
			F32 y = pDoc->pMovableField->pUIWidget->y+28;
			pLabel = ui_LabelCreate("Value", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL, pLayerPart->pMovableTexture, parse_PCMovableTextureInfo, "MovableValue");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pMovableValueLabel = pLabel;
			pDoc->pMovableValueField = pField;
		}
		else
		{
			pDoc->pMovableValueField->pOld = pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL;
			pDoc->pMovableValueField->pNew = pLayerPart->pMovableTexture;
			MEFieldRefreshFromData(pDoc->pMovableValueField);
			ui_WidgetSetPosition(pDoc->pMovableValueField->pUIWidget, 125, pDoc->pMovableField->pUIWidget->y+28);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovableValueLabel), x, pDoc->pMovableField->pUIWidget->y+28);
		}
		ui_LabelSetText(pDoc->pMovableValueLabel, costumeEdit_GetMatConstDispName(pDoc, GET_REF(pLayerPart->hMatDef), GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions->pcValueConstant, GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pValueOptions->iValConstIndex));
	}
	else
	{
		if (pDoc->pMovableValueField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pMovableValueLabel));
			MEFieldDestroy(pDoc->pMovableValueField);
			pDoc->pMovableValueField = NULL;
		}
	}
	if (SAFE_MEMBER(pLayerPart,pMovableTexture)
		&& GET_REF(pLayerPart->pMovableTexture->hMovableTexture)
		&& GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pcName
		&& stricmp("None",GET_REF(pLayerPart->pMovableTexture->hMovableTexture)->pcName))
	{
		UICheckButton *pCheck;
		UILabel *pLabel;
		MEField *pField;
		EMPanel *pPanel = pDoc->pCostumePartPanel;
		F32 x = 15;
		F32 y = pDoc->pMovableField->pUIWidget->y + 28 + (bCreate?28:0);
		F32 fMovableMinX, fMovableMaxX, fMovableMinY, fMovableMaxY;
		F32 fMovableMinScaleX, fMovableMaxScaleX, fMovableMinScaleY, fMovableMaxScaleY;
		bool bMovableCanEditPosition, bMovableCanEditRotation, bMovableCanEditScale;

		costumeTailor_GetTextureMovableValues((PCPart*)pLayerPart, GET_REF(pLayerPart->pMovableTexture->hMovableTexture), pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL,
										&fMovableMinX, &fMovableMaxX, &fMovableMinY, &fMovableMaxY,
										&fMovableMinScaleX, &fMovableMaxScaleX, &fMovableMinScaleY, &fMovableMaxScaleY,
										&bMovableCanEditPosition, &bMovableCanEditRotation, &bMovableCanEditScale);

		if (!pDoc->pMovPosXField) {
			pLabel = ui_LabelCreate("Tex Pos X", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL, pLayerPart->pMovableTexture, parse_PCMovableTextureInfo, "MovableX");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pMovPosXLabel = pLabel;
			pDoc->pMovPosXField = pField; 
			MEFieldRefreshFromData(pDoc->pMovPosXField);
		}
		else
		{
			pDoc->pMovPosXField->pOld = pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL;
			pDoc->pMovPosXField->pNew = pLayerPart->pMovableTexture;
			MEFieldRefreshFromData(pDoc->pMovPosXField);
			ui_WidgetSetPosition(pDoc->pMovPosXField->pUIWidget, x+125, y);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovPosXLabel), x, y);
		}
		y += 28;
		if (!pDoc->pMovPosYField) {
			pLabel = ui_LabelCreate("Tex Pos Y", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL, pLayerPart->pMovableTexture, parse_PCMovableTextureInfo, "MovableY");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pMovPosYLabel = pLabel;
			pDoc->pMovPosYField = pField;
			MEFieldRefreshFromData(pDoc->pMovPosYField);
		}
		else
		{
			pDoc->pMovPosYField->pOld = pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL;
			pDoc->pMovPosYField->pNew = pLayerPart->pMovableTexture;
			MEFieldRefreshFromData(pDoc->pMovPosYField);
			ui_WidgetSetPosition(pDoc->pMovPosYField->pUIWidget, x+125, y);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovPosYLabel), x, y);
		}
		y += 28;
		if (!pDoc->pMovRotField) {
			pLabel = ui_LabelCreate("Tex Rotation", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL, pLayerPart->pMovableTexture, parse_PCMovableTextureInfo, "MovRotation");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 0.01);
			MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
			pDoc->pMovRotLabel = pLabel;
			pDoc->pMovRotField = pField;
			MEFieldRefreshFromData(pDoc->pMovPosYField);
		}
		else
		{
			pDoc->pMovRotField->pOld = pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL;
			pDoc->pMovRotField->pNew = pLayerPart->pMovableTexture;
			MEFieldRefreshFromData(pDoc->pMovPosYField);
			ui_WidgetSetPosition(pDoc->pMovRotField->pUIWidget, x+125, y);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovRotLabel), x, y);
		}
		y += 28;
		if (!pDoc->pMovScaleXField) {
			pLabel = ui_LabelCreate("Tex Scale X", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL, pLayerPart->pMovableTexture, parse_PCMovableTextureInfo, "MovScaleX");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 0.01);
			MEFieldSetChangeCallback(pField, costumeEdit_UIScaleFieldChanged, pDoc);
			pDoc->pMovScaleXLabel = pLabel;
			pDoc->pMovScaleXField = pField;
			MEFieldRefreshFromData(pDoc->pMovScaleXField);
		}
		else
		{
			pDoc->pMovScaleXField->pOld = pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL;
			pDoc->pMovScaleXField->pNew = pLayerPart->pMovableTexture;
			MEFieldRefreshFromData(pDoc->pMovScaleXField);
			ui_WidgetSetPosition(pDoc->pMovScaleXField->pUIWidget, x+125, y);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovScaleXLabel), x, y);
		}
		y += 28;
		if (!pDoc->pMovScaleYField) {
			pLabel = ui_LabelCreate("Tex Scale Y", x, y);
			ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
			emPanelAddChild(pPanel, pLabel, false);
			pField = MEFieldCreateSimple(kMEFieldType_SliderText, pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL, pLayerPart->pMovableTexture, parse_PCMovableTextureInfo, "MovScaleY");
			MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
			ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
			ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
			ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 0.01);
			MEFieldSetChangeCallback(pField, costumeEdit_UIScaleFieldChanged, pDoc);
			pDoc->pMovScaleYLabel = pLabel;
			pDoc->pMovScaleYField = pField;
			MEFieldRefreshFromData(pDoc->pMovScaleYField);
		}
		else
		{
			pDoc->pMovScaleYField->pOld = pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL;
			pDoc->pMovScaleYField->pNew = pLayerPart->pMovableTexture;
			MEFieldRefreshFromData(pDoc->pMovScaleYField);
			ui_WidgetSetPosition(pDoc->pMovScaleYField->pUIWidget, x+125, y);
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovScaleYLabel), x, y);
		}
		y += 28;

		if (!pDoc->pScaleSeperateOption)
		{
			// Artist edit movable override
			pCheck = ui_CheckButtonCreate(x+5 , y, "Separate Scales", pDoc->bScaleSeperateOverride);
			ui_CheckButtonSetToggledCallback(pCheck, costumeEdit_UIScaleSeperateChanged, pDoc);
			emPanelAddChild(pPanel, pCheck, false);
			pDoc->pScaleSeperateOption = pCheck;
		}
		else
		{
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pScaleSeperateOption), x+5, y);
		}
		y += 28;

		if (!pDoc->pMovableOverride)
		{
			// Artist edit movable override
			pCheck = ui_CheckButtonCreate(x+5 , y, "Artist Override Enabled", pDoc->bAllowMovableOverride);
			ui_CheckButtonSetToggledCallback(pCheck, costumeEdit_UIMovableOverrideChanged, pDoc);
			emPanelAddChild(pPanel, pCheck, false);
			pDoc->pMovableOverride = pCheck;
		}
		else
		{
			ui_WidgetSetPosition(UI_WIDGET(pDoc->pMovableOverride), x+5, y);
		}
		y += 28;

		if (bMovableCanEditPosition || pDoc->bAllowMovableOverride)
		{
			ui_SetActive(pDoc->pMovPosXField->pUIWidget, true);
			ui_SetActive(pDoc->pMovPosYField->pUIWidget, true);
		}
		else
		{
			ui_SetActive(pDoc->pMovPosXField->pUIWidget, false);
			ui_SetActive(pDoc->pMovPosYField->pUIWidget, false);
		}
		if (bMovableCanEditRotation || pDoc->bAllowMovableOverride)
		{
			ui_SetActive(pDoc->pMovRotField->pUIWidget, true);
		}
		else
		{
			ui_SetActive(pDoc->pMovRotField->pUIWidget, false);
		}
		if (bMovableCanEditScale || pDoc->bAllowMovableOverride)
		{
			ui_SetActive(pDoc->pMovScaleXField->pUIWidget, true);
			ui_SetActive(pDoc->pMovScaleYField->pUIWidget, true);
		}
		else
		{
			ui_SetActive(pDoc->pMovScaleXField->pUIWidget, false);
			ui_SetActive(pDoc->pMovScaleYField->pUIWidget, false);
		}
	}
	else
	{
		if (pDoc->pMovPosXField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pMovPosXLabel));
			MEFieldDestroy(pDoc->pMovPosXField);
			pDoc->pMovPosXField = NULL;
		}
		if (pDoc->pMovPosYField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pMovPosYLabel));
			MEFieldDestroy(pDoc->pMovPosYField);
			pDoc->pMovPosYField = NULL;
		}
		if (pDoc->pMovRotField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pMovRotLabel));
			MEFieldDestroy(pDoc->pMovRotField);
			pDoc->pMovRotField = NULL;
		}
		if (pDoc->pMovScaleXField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pMovScaleXLabel));
			MEFieldDestroy(pDoc->pMovScaleXField);
			pDoc->pMovScaleXField = NULL;
		}
		if (pDoc->pMovScaleYField)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pMovScaleYLabel));
			MEFieldDestroy(pDoc->pMovScaleYField);
			pDoc->pMovScaleYField = NULL;
		}
		if (pDoc->pScaleSeperateOption)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pScaleSeperateOption));
			pDoc->pScaleSeperateOption = NULL;
		}
		if (pDoc->pMovableOverride)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->pMovableOverride));
			pDoc->pMovableOverride = NULL;
		}
	}

	emPanelUpdateHeight(pDoc->pCostumePartPanel);

	// Update Colors
	if (pLayerGeo) {
		costumeEdit_CopyColorScale1(pLayerPart->color0, color0Temp);
		costumeEdit_CopyColorScale1(pLayerPart->color1, color1Temp);
		costumeEdit_CopyColorScale1(pLayerPart->color2, color2Temp);
		if (pLayerMat && pLayerMat->bHasSkin) {
			costumeEdit_CopyColorScale1(pDoc->pCostume->skinColor, color3Temp);
			ui_SetActive(pDoc->pGlow3Field->pUIWidget, false);
		} else {
			costumeEdit_CopyColorScale1(pLayerPart->color3, color3Temp);
			ui_SetActive(pDoc->pGlow3Field->pUIWidget, true);
		}
		copyVec4(color0Temp, pDoc->pCButton0->color);
		copyVec4(color1Temp, pDoc->pCButton1->color);
		copyVec4(color2Temp, pDoc->pCButton2->color);
		copyVec4(color3Temp, pDoc->pCButton3->color);
		ui_SetActive(UI_WIDGET(pDoc->pCButton0), true);
		ui_SetActive(UI_WIDGET(pDoc->pCButton1), true);
		ui_SetActive(UI_WIDGET(pDoc->pCButton2), true);
		ui_SetActive(UI_WIDGET(pDoc->pCButton3), true);
		ui_SetActive(pDoc->pGlow0Field->pUIWidget, true);
		ui_SetActive(pDoc->pGlow1Field->pUIWidget, true);
		ui_SetActive(pDoc->pGlow2Field->pUIWidget, true);
		// Glow3 is set above with color3
		ui_SetActive(pDoc->pColorLinkField->pUIWidget, true);
		ui_SetActive(pDoc->pMaterialLinkField->pUIWidget, true);
		ui_SetActive(pDoc->pNoShadowField->pUIWidget, true);
		ui_SetActive(pDoc->pCustomizeReflectField->pUIWidget, pDoc->bAllowReflectOverride || (pLayerMat && pLayerMat->pColorOptions && (pLayerMat->pColorOptions->bAllowReflection[0] || pLayerMat->pColorOptions->bAllowReflection[1] || pLayerMat->pColorOptions->bAllowReflection[2] || pLayerMat->pColorOptions->bAllowReflection[3])));
		ui_SetActive(pDoc->pReflect0Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomReflection);
		ui_SetActive(pDoc->pReflect1Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomReflection);
		ui_SetActive(pDoc->pReflect2Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomReflection);
		ui_SetActive(pDoc->pReflect3Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomReflection);
		ui_SetActive(pDoc->pCustomizeSpecularField->pUIWidget, pDoc->bAllowReflectOverride || (pLayerMat && pLayerMat->pColorOptions && (pLayerMat->pColorOptions->bAllowSpecularity[0] || pLayerMat->pColorOptions->bAllowSpecularity[1] || pLayerMat->pColorOptions->bAllowSpecularity[2] || pLayerMat->pColorOptions->bAllowSpecularity[3])));
		ui_SetActive(pDoc->pSpecular0Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomSpecularity);
		ui_SetActive(pDoc->pSpecular1Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomSpecularity);
		ui_SetActive(pDoc->pSpecular2Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomSpecularity);
		ui_SetActive(pDoc->pSpecular3Field->pUIWidget, pLayerMat && pLayerMat->pColorOptions && pLayerPart->pCustomColors->bCustomSpecularity);
	} else {
		ui_SetActive(UI_WIDGET(pDoc->pCButton0), false);
		ui_SetActive(UI_WIDGET(pDoc->pCButton1), false);
		ui_SetActive(UI_WIDGET(pDoc->pCButton2), false);
		ui_SetActive(UI_WIDGET(pDoc->pCButton3), false);
		ui_SetActive(pDoc->pGlow0Field->pUIWidget, false);
		ui_SetActive(pDoc->pGlow1Field->pUIWidget, false);
		ui_SetActive(pDoc->pGlow2Field->pUIWidget, false);
		ui_SetActive(pDoc->pGlow3Field->pUIWidget, false);
		ui_SetActive(pDoc->pColorLinkField->pUIWidget, false);
		ui_SetActive(pDoc->pMaterialLinkField->pUIWidget, false);
		ui_SetActive(pDoc->pNoShadowField->pUIWidget, false);
		ui_SetActive(pDoc->pCustomizeReflectField->pUIWidget, false);
		ui_SetActive(pDoc->pReflect0Field->pUIWidget, false);
		ui_SetActive(pDoc->pReflect1Field->pUIWidget, false);
		ui_SetActive(pDoc->pReflect2Field->pUIWidget, false);
		ui_SetActive(pDoc->pReflect3Field->pUIWidget, false);
		ui_SetActive(pDoc->pCustomizeSpecularField->pUIWidget, false);
		ui_SetActive(pDoc->pSpecular0Field->pUIWidget, false);
		ui_SetActive(pDoc->pSpecular1Field->pUIWidget, false);
		ui_SetActive(pDoc->pSpecular2Field->pUIWidget, false);
		ui_SetActive(pDoc->pSpecular3Field->pUIWidget, false);
	}

	if (pLayerMat && pLayerMat->bHasSkin) {
		ui_LabelSetText(pDoc->pColor3Label, "Skin");
	} else {
		ui_LabelSetText(pDoc->pColor3Label, "Color 3");
	}

	if (pLayerMat) {
		// Find the material and check if it has the requested constant
		pMaterial = materialFind(pLayerMat->pcMaterial, 0);
		if (materialHasNamedConstant(pMaterial, "color1")) {
			ui_SetActive(UI_WIDGET(pDoc->pColor1Label), true);
		} else {
			ui_SetActive(UI_WIDGET(pDoc->pColor1Label), false);
		}
		if (materialHasNamedConstant(pMaterial, "color2")) {
			ui_SetActive(UI_WIDGET(pDoc->pColor2Label), true);
		} else {
			ui_SetActive(UI_WIDGET(pDoc->pColor2Label), false);
		}
		if (materialHasNamedConstant(pMaterial, "color3")) {
			ui_SetActive(UI_WIDGET(pDoc->pColor3Label), true);
		} else {
			ui_SetActive(UI_WIDGET(pDoc->pColor3Label), false);
		}
	}

	// Update the region combo
	bFound = false;
	for(i=eaSize(&pDoc->eaRegions)-1; i>=0; --i) {
		if (pDoc->pCurrentRegion && (pDoc->pCurrentRegion == pDoc->eaRegions[i])) {
			ui_ComboBoxSetSelected(pDoc->pRegionCombo, i);
			bFound = true;
			break;
		}
	}
	if (!bFound) {
		ui_ComboBoxSetSelected(pDoc->pRegionCombo, -1);
	}

	// Update the category combo
	bFound = false;
	for(i=eaSize(&pDoc->eaCategories)-1; i>=0; --i) {
		if (pDoc->pCurrentCategory && (pDoc->pCurrentCategory == pDoc->eaCategories[i])) {
			ui_ComboBoxSetSelected(pDoc->pCategoryCombo, i);
			bFound = true;
			break;
		}
	}
	if (!bFound) {
		ui_ComboBoxSetSelected(pDoc->pCategoryCombo, -1);
	}

	// Update the bone combo
	bFound = false;
	for(i=eaSize(&pDoc->eaBones)-1; i>=0; --i) {
		if (pPart && (GET_REF(pPart->hBoneDef) == pDoc->eaBones[i])) {
			ui_ComboBoxSetSelected(pDoc->pBoneCombo, i);
			bFound = true;
			break;
		}
	}
	if (!bFound) {
		ui_ComboBoxSetSelected(pDoc->pBoneCombo, -1);
	}

	// Update the layer combo
	bFound = false;
	for(i=eaSize(&pDoc->eaLayers)-1; i>=0; --i) {
		if (pDoc->pCurrentLayer && (pDoc->pCurrentLayer == pDoc->eaLayers[i])) {
			ui_ComboBoxSetSelected(pDoc->pLayerCombo, i);
			bFound = true;
			break;
		}
	}
	if (!bFound) {
		ui_ComboBoxSetSelected(pDoc->pLayerCombo, -1);
	}

	// Update the dynamic areas
	costumeEdit_UpdateBodySettings(pDoc);
	costumeEdit_UpdateBits(pDoc);
	costumeEdit_UpdateArtistColors(pDoc);
	costumeEdit_UpdateFX(pDoc);
	costumeEdit_UpdateFXSwap(pDoc);
	costumeEdit_UpdateExtraTextures(pDoc);
	costumeEdit_UpdateScale(pDoc);

	// Update the definition windows
	if (gDefDoc) {
		costumeDefEdit_DefUpdateLists(gDefDoc);
	}

	// Update Part Field Controls
	if (pLayerPart) {
		pDoc->pGeoField->pOld = pDoc->pOrigPart;
		pDoc->pGeoField->pNew = pDoc->pCurrentPart;
		MEFieldRefreshFromData(pDoc->pGeoField);

		costumeTailor_GetChildParts(pDoc->pOrigCostume, pDoc->pOrigPart, NULL, &eaChildParts);
		if (eaSize(&eaChildParts)) {
			pDoc->pChildGeoField->pOld = eaChildParts[0];
			eaDestroy(&eaChildParts);
		} else {
			pDoc->pChildGeoField->pOld = NULL;
		}
		pDoc->pChildGeoField->pNew = pChildPart ? pChildPart : gEmptyPart;
		MEFieldRefreshFromData(pDoc->pChildGeoField);

		pDoc->pMatField->pOld = pLayerOrigPart;
		pDoc->pMatField->pNew = pLayerPart;
		MEFieldRefreshFromData(pDoc->pMatField);

		pDoc->pPatternField->pOld = pLayerOrigPart;
		pDoc->pPatternField->pNew = pLayerPart;
		MEFieldRefreshFromData(pDoc->pPatternField);

		pDoc->pDetailField->pOld = pLayerOrigPart;
		pDoc->pDetailField->pNew = pLayerPart;
		MEFieldRefreshFromData(pDoc->pDetailField);

		pDoc->pSpecularField->pOld = pLayerOrigPart;
		pDoc->pSpecularField->pNew = pLayerPart;
		MEFieldRefreshFromData(pDoc->pSpecularField);

		pDoc->pDiffuseField->pOld = pLayerOrigPart;
		pDoc->pDiffuseField->pNew = pLayerPart;
		MEFieldRefreshFromData(pDoc->pDiffuseField);

		if (pLayerPart)
		{
			pDoc->pMovableField->pOld = pLayerOrigPart ? pLayerOrigPart->pMovableTexture : NULL;
			pDoc->pMovableField->pNew = pLayerPart->pMovableTexture;
			MEFieldRefreshFromData(pDoc->pMovableField);
		}

		{
			NOCONST(PCCustomColorInfo) * pColorInfo = pLayerPart ? pLayerPart->pCustomColors : gEmptyPart->pCustomColors;
			if(!pColorInfo)
			{
				pColorInfo = gEmptyPart->pCustomColors;
			}
			pDoc->pCustomizeReflectField->pOld = pLayerOrigPart ? pLayerOrigPart->pCustomColors : NULL;
			pDoc->pCustomizeReflectField->pNew = pColorInfo;
			MEFieldRefreshFromData(pDoc->pCustomizeReflectField);

			pDoc->pCustomizeSpecularField->pOld = pLayerOrigPart ? pLayerOrigPart->pCustomColors : NULL;
			pDoc->pCustomizeSpecularField->pNew = pColorInfo;
			MEFieldRefreshFromData(pDoc->pCustomizeSpecularField);
		}
	}

	pDoc->origGlow0.glow = pLayerOrigPart ? GET_PART_GLOWSCALE(pLayerOrigPart,0) : 0;
	pDoc->currentGlow0.glow = pLayerPart ? GET_PART_GLOWSCALE(pLayerPart,0) : 0;
	pDoc->pGlow0Field->pOld = pLayerOrigPart ? &pDoc->origGlow0 : NULL;
	pDoc->pGlow0Field->pNew = &pDoc->currentGlow0;
	ui_SetActive(pDoc->pGlow0Field->pUIWidget, pDoc->bAllowGlowOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowGlow[0] : 0));
	MEFieldRefreshFromData(pDoc->pGlow0Field);

	pDoc->origGlow1.glow = pLayerOrigPart ? GET_PART_GLOWSCALE(pLayerOrigPart,1) : 0;
	pDoc->currentGlow1.glow = pLayerPart ? GET_PART_GLOWSCALE(pLayerPart,1) : 0;
	pDoc->pGlow1Field->pOld = pLayerOrigPart ? &pDoc->origGlow1 : NULL;
	pDoc->pGlow1Field->pNew = &pDoc->currentGlow1;
	ui_SetActive(pDoc->pGlow0Field->pUIWidget, pDoc->bAllowGlowOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowGlow[1] : 0));
	MEFieldRefreshFromData(pDoc->pGlow1Field);

	pDoc->origGlow2.glow = pLayerOrigPart ? GET_PART_GLOWSCALE(pLayerOrigPart,2) : 0;
	pDoc->currentGlow2.glow = pLayerPart ? GET_PART_GLOWSCALE(pLayerPart,2) : 0;
	pDoc->pGlow2Field->pOld = pLayerOrigPart ? &pDoc->origGlow2 : NULL;
	pDoc->pGlow2Field->pNew = &pDoc->currentGlow2;
	ui_SetActive(pDoc->pGlow0Field->pUIWidget, pDoc->bAllowGlowOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowGlow[2] : 0));
	MEFieldRefreshFromData(pDoc->pGlow2Field);

	pDoc->origGlow3.glow = pLayerOrigPart ? GET_PART_GLOWSCALE(pLayerOrigPart,3) : 0;
	pDoc->currentGlow3.glow = pLayerPart ? GET_PART_GLOWSCALE(pLayerPart,3) : 0;
	pDoc->pGlow3Field->pOld = pLayerOrigPart ? &pDoc->origGlow3 : NULL;
	pDoc->pGlow3Field->pNew = &pDoc->currentGlow3;
	ui_SetActive(pDoc->pGlow0Field->pUIWidget, pDoc->bAllowGlowOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowGlow[3] : 0));
	MEFieldRefreshFromData(pDoc->pGlow3Field);

	pDoc->origColorLink.eColorLink = pLayerOrigPart ? pLayerOrigPart->eColorLink : 0;
	pDoc->pColorLinkField->pOld = pLayerOrigPart ? &pDoc->origColorLink : NULL;
	pDoc->currentColorLink.eColorLink = pLayerPart ? pLayerPart->eColorLink : 0;
	pDoc->pColorLinkField->pNew = &pDoc->currentColorLink;
	MEFieldRefreshFromData(pDoc->pColorLinkField);

	pDoc->origMaterialLink.eMaterialLink = pLayerOrigPart ? pLayerOrigPart->eMaterialLink : 0;
	pDoc->pMaterialLinkField->pOld = pLayerOrigPart ? &pDoc->origMaterialLink : NULL;
	pDoc->currentMaterialLink.eMaterialLink = pLayerPart ? pLayerPart->eMaterialLink : 0;
	pDoc->pMaterialLinkField->pNew = &pDoc->currentMaterialLink;
	MEFieldRefreshFromData(pDoc->pMaterialLinkField);

	pDoc->pNoShadowField->pOld = pDoc->pOrigPart ? pDoc->pOrigPart->pArtistData : NULL;
	pDoc->pNoShadowField->pNew = pDoc->pCurrentPart ? pDoc->pCurrentPart->pArtistData : gEmptyPart->pArtistData;
	MEFieldRefreshFromData(pDoc->pNoShadowField);

	if (pLayerPart) {
		if (!pLayerPart->pCustomColors || !pLayerPart->pCustomColors->bCustomReflection) {
			Vec4 value;
			if (pLayerMat && pLayerMat->pColorOptions && pLayerMat->pColorOptions->bCustomReflection) {
				pDoc->currentReflect0.reflection = pLayerMat->pColorOptions->defaultReflection[0];
				pDoc->currentReflect1.reflection = pLayerMat->pColorOptions->defaultReflection[1];
				pDoc->currentReflect2.reflection = pLayerMat->pColorOptions->defaultReflection[2];
				pDoc->currentReflect3.reflection = pLayerMat->pColorOptions->defaultReflection[3];
			} else if (pMaterial && materialGetNamedConstantValue(pMaterial, "ReflectionWeight", value)) {
				pDoc->currentReflect0.reflection = (value[0] + 1.0) * 50.0;
				pDoc->currentReflect1.reflection = (value[1] + 1.0) * 50.0;
				pDoc->currentReflect2.reflection = (value[2] + 1.0) * 50.0;
				pDoc->currentReflect3.reflection = (value[3] + 1.0) * 50.0;
			} else if (pLayerPart->pCustomColors) {
				pLayerPart->pCustomColors->reflection[0] = 0;
				pLayerPart->pCustomColors->reflection[1] = 0;
				pLayerPart->pCustomColors->reflection[2] = 0;
				pLayerPart->pCustomColors->reflection[3] = 0;
			}
		} else {
			pDoc->currentReflect0.reflection = pLayerPart->pCustomColors->reflection[0];
			pDoc->currentReflect1.reflection = pLayerPart->pCustomColors->reflection[1];
			pDoc->currentReflect2.reflection = pLayerPart->pCustomColors->reflection[2];
			pDoc->currentReflect3.reflection = pLayerPart->pCustomColors->reflection[3];
		}
	} else {
		pDoc->currentReflect0.reflection = 0;
		pDoc->currentReflect1.reflection = 0;
		pDoc->currentReflect2.reflection = 0;
		pDoc->currentReflect3.reflection = 0;
	}
	pDoc->origReflect0.reflection = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->reflection[0] : 0;
	pDoc->origReflect1.reflection = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->reflection[1] : 0;
	pDoc->origReflect2.reflection = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->reflection[2] : 0;
	pDoc->origReflect3.reflection = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->reflection[3] : 0;

	pDoc->pReflect0Field->pOld = pLayerOrigPart ? &pDoc->origReflect0 : NULL;
	pDoc->pReflect0Field->pNew = &pDoc->currentReflect0;
	ui_SetActive(pDoc->pReflect0Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomReflection && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions)? pLayerMat->pColorOptions->bAllowReflection[0] : 0)));
	MEFieldRefreshFromData(pDoc->pReflect0Field);

	pDoc->pReflect1Field->pOld = pLayerOrigPart ? &pDoc->origReflect1 : NULL;
	pDoc->pReflect1Field->pNew = &pDoc->currentReflect1;
	ui_SetActive(pDoc->pReflect1Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomReflection && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions)? pLayerMat->pColorOptions->bAllowReflection[1] : 0)));
	MEFieldRefreshFromData(pDoc->pReflect1Field);

	pDoc->pReflect2Field->pOld = pLayerOrigPart ? &pDoc->origReflect2 : NULL;
	pDoc->pReflect2Field->pNew = &pDoc->currentReflect2;
	ui_SetActive(pDoc->pReflect2Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomReflection && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions)? pLayerMat->pColorOptions->bAllowReflection[2] : 0)));
	MEFieldRefreshFromData(pDoc->pReflect2Field);

	pDoc->pReflect3Field->pOld = pLayerOrigPart ? &pDoc->origReflect3 : NULL;
	pDoc->pReflect3Field->pNew = &pDoc->currentReflect3;
	ui_SetActive(pDoc->pReflect3Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomReflection && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions)? pLayerMat->pColorOptions->bAllowReflection[3] : 0)));
	MEFieldRefreshFromData(pDoc->pReflect3Field);

	if (pLayerPart) {
		if (!pLayerPart->pCustomColors || !pLayerPart->pCustomColors->bCustomSpecularity) {
			Vec4 value;
			if (pLayerMat && pLayerMat->pColorOptions && pLayerMat->pColorOptions->bCustomSpecularity) {
				pDoc->currentReflect0.specularity = pLayerMat->pColorOptions->defaultSpecularity[0];
				pDoc->currentReflect1.specularity = pLayerMat->pColorOptions->defaultSpecularity[1];
				pDoc->currentReflect2.specularity = pLayerMat->pColorOptions->defaultSpecularity[2];
				pDoc->currentReflect3.specularity = pLayerMat->pColorOptions->defaultSpecularity[3];
			} else if (pMaterial && materialGetNamedConstantValue(pMaterial, "SpecularWeight", value)) {
				pDoc->currentReflect0.specularity = (value[0] + 1.0) * 50.0;
				pDoc->currentReflect1.specularity = (value[1] + 1.0) * 50.0;
				pDoc->currentReflect2.specularity = (value[2] + 1.0) * 50.0;
				pDoc->currentReflect3.specularity = (value[3] + 1.0) * 50.0;
			} else if (pLayerPart->pCustomColors) {
				pLayerPart->pCustomColors->specularity[0] = 0;
				pLayerPart->pCustomColors->specularity[1] = 0;
				pLayerPart->pCustomColors->specularity[2] = 0;
				pLayerPart->pCustomColors->specularity[3] = 0;
			}
		} else {
			pDoc->currentReflect0.specularity = pLayerPart->pCustomColors->specularity[0];
			pDoc->currentReflect1.specularity = pLayerPart->pCustomColors->specularity[1];
			pDoc->currentReflect2.specularity = pLayerPart->pCustomColors->specularity[2];
			pDoc->currentReflect3.specularity = pLayerPart->pCustomColors->specularity[3];
		}
	} else {
		pDoc->currentReflect0.specularity = 0;
		pDoc->currentReflect1.specularity = 0;
		pDoc->currentReflect2.specularity = 0;
		pDoc->currentReflect3.specularity = 0;
	}
	pDoc->origReflect0.specularity = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->specularity[0] : 0;
	pDoc->origReflect1.specularity = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->specularity[1] : 0;
	pDoc->origReflect2.specularity = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->specularity[2] : 0;
	pDoc->origReflect3.specularity = pLayerOrigPart && pLayerOrigPart->pCustomColors ? pLayerOrigPart->pCustomColors->specularity[3] : 0;

	pDoc->pSpecular0Field->pOld = pLayerOrigPart ? &pDoc->origReflect0 : NULL;
	pDoc->pSpecular0Field->pNew = &pDoc->currentReflect0;
	ui_SetActive(pDoc->pSpecular0Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomSpecularity && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowSpecularity[0] : 0)));
	MEFieldRefreshFromData(pDoc->pSpecular0Field);

	pDoc->pSpecular1Field->pOld = pLayerOrigPart ? &pDoc->origReflect1 : NULL;
	pDoc->pSpecular1Field->pNew = &pDoc->currentReflect1;
	ui_SetActive(pDoc->pSpecular0Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomSpecularity && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowSpecularity[1] : 0)));
	MEFieldRefreshFromData(pDoc->pSpecular1Field);

	pDoc->pSpecular2Field->pOld = pLayerOrigPart ? &pDoc->origReflect2 : NULL;
	pDoc->pSpecular2Field->pNew = &pDoc->currentReflect2;
	ui_SetActive(pDoc->pSpecular0Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomSpecularity && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowSpecularity[2] : 0)));
	MEFieldRefreshFromData(pDoc->pSpecular2Field);

	pDoc->pSpecular3Field->pOld = pLayerOrigPart ? &pDoc->origReflect3 : NULL;
	pDoc->pSpecular3Field->pNew = &pDoc->currentReflect3;
	ui_SetActive(pDoc->pSpecular0Field->pUIWidget, pLayerPart && pLayerPart->pCustomColors && pLayerPart->pCustomColors->bCustomSpecularity && (pDoc->bAllowReflectOverride || ((pLayerMat && pLayerMat->pColorOptions) ? pLayerMat->pColorOptions->bAllowSpecularity[3] : 0)));
	MEFieldRefreshFromData(pDoc->pSpecular3Field);

	pDoc->pDyePackField->pOld = pDoc->pCurrentDyePack;
	pDoc->pDyePackField->pOld = pDoc->pCurrentDyePack;
	ui_SetActive(pDoc->pDyePackField->pUIWidget, eaSize(&s_eaDyePacks) > 0);
	MEFieldRefreshFromData(pDoc->pDyePackField);

	if (pMaterial && materialHasNamedConstant(pMaterial, "ReflectionWeight")) {
		// Do nothing if UI is already set up
		if (!pDoc->pReflect0Field->pUIWidget->group) {
			// Put in reflection controls
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect0Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect1Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect2Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect3Field->pUIWidget);

			// Remove label
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), UI_WIDGET(pDoc->pReflectLabel));
		}
	} else {
		ui_SetActive(pDoc->pCustomizeReflectField->pUIWidget, false);
		if (pDoc->pReflect0Field->pUIWidget->group) {
			// Remove reflection controls
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect0Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect1Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect2Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pReflect3Field->pUIWidget);

			// Add label
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), UI_WIDGET(pDoc->pReflectLabel));
		}
	}

	if (pMaterial && materialHasNamedConstant(pMaterial, "SpecularWeight")) {
		// Do nothing if UI is already set up
		if (!pDoc->pSpecular0Field->pUIWidget->group) {
			// Put in reflection controls
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular0Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular1Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular2Field->pUIWidget);
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular3Field->pUIWidget);

			// Remove label
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), UI_WIDGET(pDoc->pReflectLabel));
		}
	} else {
		ui_SetActive(pDoc->pCustomizeSpecularField->pUIWidget, false);
		if (pDoc->pReflect0Field->pUIWidget->group) {
			// Remove reflection controls
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular0Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular1Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular2Field->pUIWidget);
			ui_WidgetRemoveChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), pDoc->pSpecular3Field->pUIWidget);

			// Add label
			ui_WidgetAddChild(UI_WIDGET(emPanelGetExpander(pDoc->pReflectPanel)), UI_WIDGET(pDoc->pReflectLabel));
		}
	}

	// Update Costume Field Controls
	pDoc->pNameField->pOld = pDoc->pOrigCostume;
	pDoc->pNameField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pNameField);

	pDoc->pScopeField->pOld = pDoc->pOrigCostume;
	pDoc->pScopeField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pScopeField);

	pDoc->pCostumeTypeField->pOld = pDoc->pOrigCostume;
	pDoc->pCostumeTypeField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pCostumeTypeField);

	pDoc->pSkeletonField->pOld = pDoc->pOrigCostume;
	pDoc->pSkeletonField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pSkeletonField);

	pDoc->pSpeciesField->pOld = pDoc->pOrigCostume;
	pDoc->pSpeciesField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pSpeciesField);

	pDoc->pVoiceField->pOld = pDoc->pOrigCostume;
	pDoc->pVoiceField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pVoiceField);

	pDoc->pColorLinkAllField->pOld = pDoc->pOrigCostume;
	pDoc->pColorLinkAllField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pColorLinkAllField);

	pDoc->pMatLinkAllField->pOld = pDoc->pOrigCostume;
	pDoc->pMatLinkAllField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pMatLinkAllField);

	pDoc->pAccountUnlockField->pOld = pDoc->pOrigCostume;
	pDoc->pAccountUnlockField->pNew = pDoc->pCostume;
	MEFieldRefreshFromData(pDoc->pAccountUnlockField);

	pDoc->pBodySockDisableField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL;
	pDoc->pBodySockDisableField->pNew = pDoc->pCostume->pArtistData;
	MEFieldRefreshFromData(pDoc->pBodySockDisableField);

	pDoc->pCollisionDisableField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL;
	pDoc->pCollisionDisableField->pNew = pDoc->pCostume->pArtistData;
	MEFieldRefreshFromData(pDoc->pCollisionDisableField);

	pDoc->pShellDisableField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL;
	pDoc->pShellDisableField->pNew = pDoc->pCostume->pArtistData;
	MEFieldRefreshFromData(pDoc->pShellDisableField);

	pDoc->pRagDollDisableField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL;
	pDoc->pRagDollDisableField->pNew = pDoc->pCostume->pArtistData;
	MEFieldRefreshFromData(pDoc->pRagDollDisableField);

	pDoc->pBodySockGeoField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockGeoField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockGeoField);

	pDoc->pBodySockPoseField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockPoseField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockPoseField);

	pDoc->pBodySockMinXField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockMinXField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockMinXField);

	pDoc->pBodySockMinYField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockMinYField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockMinYField);

	pDoc->pBodySockMinZField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockMinZField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockMinZField);

	pDoc->pBodySockMaxXField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockMaxXField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockMaxXField);

	pDoc->pBodySockMaxYField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockMaxYField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockMaxYField);

	pDoc->pBodySockMaxZField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL;
	pDoc->pBodySockMaxZField->pNew = pDoc->pCostume->pBodySockInfo;
	MEFieldRefreshFromData(pDoc->pBodySockMaxZField);

	pDoc->pCollisionGeoField->pOld = pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL;
	pDoc->pCollisionGeoField->pNew = pDoc->pCostume->pArtistData;
	MEFieldRefreshFromData(pDoc->pCollisionGeoField);

	ui_GimmeButtonSetName(pDoc->pGimmeButton, pDoc->pCostume ? pDoc->pCostume->pcName : NULL);
	ui_GimmeButtonSetReferent(pDoc->pGimmeButton, pDoc->pCostume);

	ui_LabelSetText(pDoc->pFilenameLabel, pDoc->pCostume->pcFileName);

	// Update asset manager dirty state
	if (costumeEdit_IsCostumeDirty(pDoc)) {
		pDoc->emDoc.saved = 0;
	} else {
		pDoc->emDoc.saved = 1;
	}

	// Clear hover costume (if any)
	StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

	// Ignore change callbacks while in the process of updating the fields
	pDoc->bIgnoreFieldChanges = false;
}


//---------------------------------------------------------------------------------------------------
// UI Callbacks
//---------------------------------------------------------------------------------------------------



static void costumeEdit_UIAnimChange(UIComboBox *pCombo, CostumeEditDoc *pDoc)
{
	int i = ui_ComboBoxGetSelected(pCombo);
	if (i < 0) {
		pDoc->pGraphics->costume.pcBits = NULL;
		pDoc->pGraphics->costume.pcAnimStanceWords = NULL;
		pDoc->pGraphics->costume.pcAnimKeyword = NULL;
		pDoc->pGraphics->costume.pcAnimMove = NULL;
		pDoc->pGraphics->costume.bNeedsResetToDefault = false;
	} else {
		pDoc->pGraphics->costume.pcBits = allocAddString(eaCostumeAnims[i]->pcBits);
		pDoc->pGraphics->costume.pcAnimStanceWords = allocAddString(eaCostumeAnims[i]->pcAnimStanceWords);
		pDoc->pGraphics->costume.pcAnimKeyword = allocAddString(eaCostumeAnims[i]->pcAnimKeyword);
		pDoc->pGraphics->costume.pcAnimMove = allocAddString(eaCostumeAnims[i]->pcAnimMove);
		pDoc->pGraphics->costume.bNeedsResetToDefault = true;
	}
}


static void costumeEdit_UIBitsChange(MEField *pField, bool bFinished, CostumeBitsGroup *pGroup)
{
	if (bFinished) {
		const char *pBits = ui_TextEntryGetText(pField->pUIText);
		if (pBits && strlen(pBits) && !dynBitIsValidName(pBits)) {
			// If bits are not valid, clear them
			CONTAINER_NOCONST(PCBitName, pField->pNew)->pcName = NULL;
			MEFieldRefreshFromData(pField);
		}

		// Update the costume and such
		costumeEdit_UIFieldChanged(pField, bFinished, pGroup->pDoc);
	}
}

static void costumeEdit_UIBitsChangeNew(UIComboBox *pComboBox, CostumeBitsGroup *pGroup)
{
	char *pcSelected = NULL;
	ui_ComboBoxGetSelectedsAsString(pComboBox, &pcSelected);
	if (pcSelected)
	{
		bool foundIt = false;
		const char *pcSelectedFromStringBase;
		pcSelectedFromStringBase = allocAddString(pcSelected);
		FOR_EACH_IN_EARRAY(pGroup->pDoc->pCostume->pArtistData->eaConstantBits, NOCONST(PCBitName), checkBit) {
			if (checkBit->pcName == pcSelectedFromStringBase) {
				foundIt = true;
				break;
			}
		} FOR_EACH_END;

		if (!foundIt)
		{
			FOR_EACH_IN_EARRAY(pGroup->pDoc->eaBitsGroups, CostumeBitsGroup, checkGroup) {
				if (checkGroup == pGroup) {
					pGroup->pDoc->pCostume->pArtistData->eaConstantBits[icheckGroupIndex]->pcName = pcSelectedFromStringBase;
				}
			} FOR_EACH_END;
		}
	}
	costumeEdit_UIFieldChanged(NULL, true, pGroup->pDoc);
}

void costumeEdit_UIRegionSelectedCombo(UIComboBox *pBox, CostumeEditDoc *pDoc)
{
	PCBoneDef *pBone;
	int iRow = ui_ComboBoxGetSelected(pBox);

	pDoc->pCurrentRegion = pDoc->eaRegions[iRow];

	// If current part is in this region, then no other change
	if (pDoc->pCurrentPart) {
		pBone = GET_REF(pDoc->pCurrentPart->hBoneDef);
		if (pBone && (pDoc->pCurrentRegion == GET_REF(pBone->hRegion))) {
			// Okay if current part is in this region.  Just clean up lists.
			costumeEdit_UpdateLists(pDoc);
			return;
		}
	}

	// Need to change to a different current part
	costumeEdit_SelectBone(pDoc, NULL);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


void costumeEdit_UICategorySelectedCombo(UIComboBox *pBox, CostumeEditDoc *pDoc)
{
	int iRow = ui_ComboBoxGetSelected(pBox);
	int i;

	// Update the current category
	pDoc->pCurrentCategory = pDoc->eaCategories[iRow];

	// Update the region/category assignment for the costume
	costumeTailor_SetRegionCategory(pDoc->pCostume, pDoc->pCurrentRegion, pDoc->pCurrentCategory);

	// Need to revalidate all category choices
	for(i=eaSize(&pDoc->pCostume->eaRegionCategories)-1; i>=0; --i) {
		PCRegion *pRegion = GET_REF(pDoc->pCostume->eaRegionCategories[i]->hRegion);
		PCCategory **eaCats = NULL;
		if (pRegion) {
			costumeTailor_GetValidCategories(pDoc->pCostume, pRegion, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, NULL, &eaCats, CGVF_OMIT_EMPTY | (gUseDispNames ? CGVF_SORT_DISPLAY : 0) | (gCEUnlockAll ? CGVF_UNLOCK_ALL : 0));
			costumeTailor_PickValidCategoryForRegion(pDoc->pCostume, pRegion, eaCats, false);
		}
	}

	// Need to revalidate all parts against this choice
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		PCBoneDef *pBone = GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef);
		if (pBone && !pBone->bIsChildBone) {
			costumeTailor_PickValidPartValues(pDoc->pCostume, pDoc->pCostume->eaParts[i], GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, true, true, NULL);
		}
	}

	// Update the UI
	costumeEdit_UpdateLists(pDoc);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


void costumeEdit_UIBoneSelectedCombo(UIComboBox *pBox, CostumeEditDoc *pDoc)
{
	int iRow = ui_ComboBoxGetSelected(pBox);
	costumeEdit_SelectBone(pDoc, pDoc->eaBones[iRow]);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


static void costumeEdit_UIBoneSelectedList(UIList *pList, CostumeEditDoc *pDoc)
{
	int iRow = ui_ListGetSelectedRow(pList);
	costumeEdit_SelectBone(pDoc, GET_REF(pDoc->pCostume->eaParts[iRow]->hBoneDef));
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


void costumeEdit_UICenterCamera(UIButton *pButton, EMEditor *pEditor)
{
	Vec3 zeropyr = {0, 0, 0};
	GfxCameraController *pCamera;
	CostumeEditDoc *pDoc;

	pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	pCamera = costumeView_GetCamera();
	pCamera->camdist = pDoc->pCostume->fHeight/6.0 * 10.0;
	pCamera->camcenter[0] = 0;
	pCamera->camcenter[1] = pDoc->pCostume->fHeight * 0.66666666;
	pCamera->camcenter[2] = 0;
	copyVec3(zeropyr,pCamera->campyr);
}


void costumeEdit_UICameraRotateLeftStart(UIButton *pButton, EMEditor *pEditor)
{
	// start the rotate going to the left
	gYawSpeed = 1.0f;
}


void costumeEdit_UICameraRotateRightStart(UIButton *pButton, EMEditor *pEditor)
{
	// start the rotate going to the right
	gYawSpeed = -1.0f;
}


void costumeEdit_UICameraRotateStop(UIButton *pButton, EMEditor *pEditor)
{
	// stop any rotation
	gYawSpeed = 0.0f;
}


static void costumeEdit_UIClearCostume(UIButton *pButton, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc;
	NOCONST(PlayerCostume) *pCostume;
	PCSkeletonDef *pSkel;
	int i;

	pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	// Create the new costume
	pCostume = StructCreateNoConst(parse_PlayerCostume);
	pCostume->pcName = allocAddString(pDoc->pCostume->pcName);
	pCostume->pcScope = allocAddString(pDoc->pCostume->pcScope);
	pCostume->pcFileName = allocAddString(pDoc->pCostume->pcFileName);
	SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, REF_STRING_FROM_HANDLE(pDoc->pCostume->hSkeleton), pCostume->hSkeleton);
	pCostume->eCostumeType = pDoc->pCostume->eCostumeType;
	costumeTailor_SetDefaultSkinColor(pCostume, GET_REF(pCostume->hSpecies), NULL);

	// Make it active
	StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pCostume);
	pDoc->pCostume = pCostume;

	// Set default height and such
	pSkel = GET_REF(pCostume->hSkeleton);
	if (pSkel && pSkel->fDefaultHeight) {
		pCostume->fHeight = pSkel->fDefaultHeight;
	} else {
		pCostume->fHeight = 6;
	}
	if (pSkel && pSkel->fDefaultMuscle) {
		pCostume->fMuscle = pSkel->fDefaultMuscle;
	} else {
		pCostume->fMuscle = 20;
	}
	if (pSkel) {
		for(i=0; i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
			if (i < eafSize(&pSkel->eafDefaultBodyScales)) {
				eafPush(&pCostume->eafBodyScales, pSkel->eafDefaultBodyScales[i]);
			} else {
				eafPush(&pCostume->eafBodyScales, 20);
			}
		}
	}

	// Fill with new parts
	costumeTailor_FillAllBones(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL/*powerFX*/, NULL, true, true, gUseDispNames);
	ui_ListSetModel(pDoc->pBoneList, NULL, &pDoc->pCostume->eaParts);

	// Reset to use the new costume
	costumeEdit_BuildScaleGroups(pDoc);
	costumeEdit_SelectBone(pDoc, NULL);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


void costumeEdit_UIColorChanged(UIColorButton *pButton, bool bFinished, CostumeEditDoc *pDoc)
{
	Vec4 color;
	U8 scaledColor[4];

	if (!pDoc || pDoc->bIgnoreFieldChanges) {
		return;
	}

	ui_ColorButtonGetColor(pButton, color);
	costumeEdit_CopyColorScale255(color, scaledColor);

	if (pButton == pDoc->pCButton0)
	{
		if (!ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton0->activeWindow)) || (pDoc->pCButton0->activeWindow->bOrphaned)) costumeEdit_UpdateColorButtons(pDoc);
		if (pDoc->pColorButtonCurrentPart->pClothLayer)
		{
			costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart->pClothLayer, 0, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,0));
		}
		costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart, 0, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,0));
	}
	else if (pButton == pDoc->pCButton1)
	{
		if (!ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton1->activeWindow)) || (pDoc->pCButton1->activeWindow->bOrphaned)) costumeEdit_UpdateColorButtons(pDoc);
		if (pDoc->pColorButtonCurrentPart->pClothLayer)
		{
			costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart->pClothLayer, 1, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,1));
		}
		costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart, 1, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,1));
	}
	else if (pButton == pDoc->pCButton2)
	{
		if (!ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton2->activeWindow)) || (pDoc->pCButton2->activeWindow->bOrphaned)) costumeEdit_UpdateColorButtons(pDoc);
		if (pDoc->pColorButtonCurrentPart->pClothLayer)
		{
			costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart->pClothLayer, 2, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,2));
		}
		costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart, 2, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,2));
	}
	else if (pButton == pDoc->pCButton3)
	{
		PCMaterialDef *pMatDef = GET_REF(pDoc->pColorButtonCurrentPart->hMatDef);
		if (!ui_WindowIsVisible(UI_WINDOW(pDoc->pCButton3->activeWindow)) || (pDoc->pCButton3->activeWindow->bOrphaned)) costumeEdit_UpdateColorButtons(pDoc);
		if (pMatDef && pMatDef->bHasSkin)
		{
			costumeTailor_SetSkinColor(pDoc->pCostume, scaledColor);
		}
		else
		{
			if (pDoc->pColorButtonCurrentPart->pClothLayer)
			{
				costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart->pClothLayer, 3, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,3));
			}
			costumeTailor_SetPartColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart, 3, scaledColor, GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,3));
		}
	}

	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIBodyScaleComboChanged(UIComboBox* pCombo, CostumeBodyScaleGroup* pGroup)
{
	PCBodyScaleValue* pSelValue = (PCBodyScaleValue*)ui_ComboBoxGetSelectedObject(pCombo);

	// set the costume data to reflect the value selected in the combo box
	if (pSelValue) {
		pGroup->pDoc->pCostume->eafBodyScales[pGroup->iComboIndex] = pSelValue->fValue;
	}
	costumeEdit_CostumeChanged(pGroup->pDoc);
}


static void costumeEdit_UIBodyScaleComboInit(UIComboBox* pCombo, CostumeBodyScaleGroup* pGroup)
{
	PCBodyScaleValue* pCurValue = NULL;
	PCBodyScaleValue* pCloseValue = NULL;
	F32 fStartVal = pGroup->pDoc->pCostume->eafBodyScales[pGroup->iComboIndex];
	F32 fCurDelta = 0.0f;
	F32 fCloseDelta = 0.0f;
	int ii = 0;
	
	// find the option closest to the pre-defined value
	for (ii = 0; ii < eaSize(pCombo->model); ii++) {
		pCurValue = ((PCBodyScaleValue**) *pCombo->model)[ii];
		if (pCurValue) {
			fCurDelta = ABS(fStartVal - pCurValue->fValue);
			if (!pCloseValue || (fCurDelta < fCloseDelta)) {
				pCloseValue = pCurValue;
				fCloseDelta = fCurDelta;
			}
		}
	}
	
	// if a match is found, set the combo box selection and costume data to match each other
	if (pCloseValue) {
		ui_ComboBoxSetSelectedObject(pCombo, pCloseValue);
		pGroup->pDoc->pCostume->eafBodyScales[pGroup->iComboIndex] = pCloseValue->fValue;
	}
}


static void costumeEdit_UIColorGroupChanged(UIColorButton *pButton, bool bFinished, CostumeColorGroup *pGroup)
{
	Vec4 value;

	ui_ColorButtonGetColor(pButton, value);
	costumeEdit_CopyColorScale255(value, pGroup->currentColor);

	// Also set it into the extra color if available
	if (pGroup->bIsSet) {
		int i;

		for(i=eaSize(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors)-1; i>=0; --i) {
			if (stricmp(pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors[i]->pcName, pGroup->pcName) == 0) {
				costumeEdit_CopyColorScale255(value, pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors[i]->color);
				break;
			}
		}
	}

	// Update the costume
	costumeEdit_CostumeChanged(pGroup->pDoc);
}


static void costumeEdit_UIColorGroupToggled(UICheckButton *pButton, CostumeColorGroup *pGroup)
{
	if (ui_CheckButtonGetState(pButton)) {
		if (pGroup->bIsColor) {
			NOCONST(PCMaterialColor) *pMColor;

			// Need to add the color group to the current part
			pMColor = StructCreateNoConst(parse_PCMaterialColor);
			pMColor->pcName = allocAddString(pGroup->pcName);
			COPY_COSTUME_COLOR(pGroup->currentColor, pMColor->color);
			eaPush(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors, pMColor);
		} else {
			NOCONST(PCMaterialConstant) *pMConstant;

			// Need to add the color group to the current part
			pMConstant = StructCreateNoConst(parse_PCMaterialConstant);
			pMConstant->pcName = allocAddString(pGroup->pcName);
			copyVec4(pGroup->currentValue, pMConstant->values);
			eaPush(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants, pMConstant);
		}
	} else {
		int i;

		if (pGroup->bIsColor) {
			// Need to remove the color entry
			for(i=eaSize(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors)-1; i>=0; --i) {
				if (stricmp(pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors[i]->pcName, pGroup->pcName) == 0) {
					StructDestroyNoConst(parse_PCMaterialColor, pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors[i]);
					eaRemove(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraColors, i);
					break;
				}
			}
		} else {
			// Need to remove the constant entry
			for(i=eaSize(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants)-1; i>=0; --i) {
				if (stricmp(pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants[i]->pcName, pGroup->pcName) == 0) {
					StructDestroyNoConst(parse_PCMaterialConstant, pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants[i]);
					eaRemove(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants, i);
					break;
				}
			}
		}
	}

	// Update the costume
	costumeEdit_CostumeChanged(pGroup->pDoc);
}


void costumeEdit_UIColorLinkChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	costumeTailor_SetPartColorLinking(pDoc->pCostume, pDoc->pColorButtonCurrentPart, pDoc->currentColorLink.eColorLink, GET_REF(pDoc->pCostume->hSpecies), NULL);
	costumeEdit_CostumeChanged(pDoc);
}

void costumeEdit_UIMaterialChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	costumeTailor_PickValidPartValues(pDoc->pCostume, pDoc->pCurrentPart, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, gUseDispNames, true, true, true, NULL);
	costumeEdit_CostumeChanged(pDoc);
}

void costumeEdit_UIMaterialLinkChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	costumeTailor_SetPartMaterialLinking(pDoc->pCostume, pDoc->pCurrentPart, pDoc->currentMaterialLink.eMaterialLink, GET_REF(pDoc->pCostume->hSpecies), NULL, true);
	costumeEdit_CostumeChanged(pDoc);
}


void costumeEdit_UICostumeClone(CostumeEditDoc *pDoc)
{
	// Ask editor manager to open a documet.  It will pull from the global costume
	gCostumeToClone = costumeEdit_CreateCostume(pDoc->pCostume->pcName, pDoc->pCostume);
	emNewDoc("_costume", NULL);
}


static void costumeEdit_UICostumeTypeChange(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	if (!pDoc->bIgnoreFieldChanges) {
		costumeEdit_BuildScaleGroups(pDoc);
		{
			emPanelUpdateHeight(pDoc->pMainSettingsPanel);
			if (pDoc->pCostume->eCostumeType != kPCCostumeType_Player)
			{
				//Hide "Color Link Default" and "Material Link Default" in non player types as it doesn't matter here
				emPanelSetHeight(pDoc->pMainSettingsPanel, emPanelGetHeight(pDoc->pMainSettingsPanel) - (28*2));
			}

			costumeEdit_UpdateColorButtons(pDoc);
		}
		costumeEdit_SelectBone(pDoc, NULL);
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}


void costumeEdit_UICustomReflectionChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	if (pDoc->pColorButtonCurrentPart->pCustomColors)
	{

		if (pDoc->pColorButtonCurrentPart->pCustomColors->bCustomReflection) {
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[0] = pDoc->currentReflect0.reflection;
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[1] = pDoc->currentReflect1.reflection;
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[2] = pDoc->currentReflect2.reflection;
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[3] = pDoc->currentReflect3.reflection;
		} else {
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[0] = 0;
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[1] = 0;
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[2] = 0;
			pDoc->pColorButtonCurrentPart->pCustomColors->reflection[3] = 0;
		}
		costumeEdit_CostumeChanged(pDoc);
	}
}


void costumeEdit_UICustomSpecularChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	if(pDoc->pColorButtonCurrentPart->pCustomColors)
	{
		if (pDoc->pColorButtonCurrentPart->pCustomColors->bCustomSpecularity) {
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[0] = pDoc->currentReflect0.specularity;
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[1] = pDoc->currentReflect1.specularity;
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[2] = pDoc->currentReflect2.specularity;
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[3] = pDoc->currentReflect3.specularity;
		} else {
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[0] = 0;
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[1] = 0;
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[2] = 0;
			pDoc->pColorButtonCurrentPart->pCustomColors->specularity[3] = 0;
		}
		costumeEdit_CostumeChanged(pDoc);
	}
}


static void costumeEdit_UIDisplayBoneName(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCBoneDef *pBone;
	CBox box;
	const char *pcText;

	pBone = GET_REF(pPart->hBoneDef);
	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pBone != GET_REF(pDoc->pOrigCostume->eaParts[index]->hBoneDef)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (gUseDispNames) {
		pcText = pBone ? TranslateDisplayMessage(pBone->displayNameMsg) : "Invalid Bone";
	} else {
		pcText = pBone ? pBone->pcName : "Invalid Bone";
	}
	x+=3;
	BuildCBox(&box, x, y, w-6, h);
	clipperPushRestrict(&box);
	gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
	clipperPop();
}


static void costumeEdit_UIDisplayBoneGeo(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCGeometryDef *pGeo;
	CBox box;

	pGeo = GET_REF(pPart->hGeoDef);
	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pGeo != GET_REF(pDoc->pOrigCostume->eaParts[index]->hGeoDef)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (pGeo && stricmp(pGeo->pcName, "None") != 0) {
		const char *pcText = pGeo->pcName;
		if (gUseDispNames) {
			pcText = TranslateDisplayMessage(pGeo->displayNameMsg);
		}
		x+=3;
		BuildCBox(&box, x, y, w-6, h);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
		clipperPop();
	}
}


static void costumeEdit_UIDisplayBoneMat(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCMaterialDef *pMat;
	CBox box;

	pMat = GET_REF(pPart->hMatDef);

	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pMat != GET_REF(pDoc->pOrigCostume->eaParts[index]->hMatDef)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (pMat && stricmp(pMat->pcName, "None") != 0) {
		const char *pcText = pMat->pcName;
		if (gUseDispNames) {
			pcText = TranslateDisplayMessage(pMat->displayNameMsg);
		}
		x+=3;
		BuildCBox(&box, x, y, w-6, h);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
		clipperPop();
	}
}


static void costumeEdit_UIDisplayBonePattern(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCTextureDef *pTex;
	CBox box;

	pTex = GET_REF(pPart->hPatternTexture);
	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pTex != GET_REF(pDoc->pOrigCostume->eaParts[index]->hPatternTexture)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (pTex && stricmp(pTex->pcName, "None") != 0) {
		const char *pcText = pTex->pcName;
		if (gUseDispNames) {
			pcText = TranslateDisplayMessage(pTex->displayNameMsg);
		}
		x+=3;
		BuildCBox(&box, x, y, w-6, h);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
		clipperPop();
	}
}


static void costumeEdit_UIDisplayBoneDetail(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCTextureDef *pTex;
	CBox box;

	pTex = GET_REF(pPart->hDetailTexture);
	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pTex != GET_REF(pDoc->pOrigCostume->eaParts[index]->hDetailTexture)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (pTex && stricmp(pTex->pcName, "None") != 0) {
		const char *pcText = pTex->pcName;
		if (gUseDispNames) {
			pcText = TranslateDisplayMessage(pTex->displayNameMsg);
		}
		x+=3;
		BuildCBox(&box, x, y, w-6, h);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
		clipperPop();
	}
}


static void costumeEdit_UIDisplayBoneSpecular(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCTextureDef *pTex;
	CBox box;

	pTex = GET_REF(pPart->hSpecularTexture);
	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pTex != GET_REF(pDoc->pOrigCostume->eaParts[index]->hSpecularTexture)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (pTex && stricmp(pTex->pcName, "None") != 0) {
		const char *pcText = pTex->pcName;
		if (gUseDispNames) {
			pcText = TranslateDisplayMessage(pTex->displayNameMsg);
		}
		x+=3;
		BuildCBox(&box, x, y, w-6, h);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
		clipperPop();
	}
}


static void costumeEdit_UIDisplayBoneDiffuse(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCTextureDef *pTex;
	CBox box;

	pTex = GET_REF(pPart->hDiffuseTexture);
	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pTex != GET_REF(pDoc->pOrigCostume->eaParts[index]->hDiffuseTexture)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (pTex && stricmp(pTex->pcName, "None") != 0) {
		const char *pcText = pTex->pcName;
		if (gUseDispNames) {
			pcText = TranslateDisplayMessage(pTex->displayNameMsg);
		}
		x+=3;
		BuildCBox(&box, x, y, w-6, h);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
		clipperPop();
	}
}


static void costumeEdit_UIDisplayBoneMovable(struct UIList *pList, struct UIListColumn *pCol, UI_MY_ARGS, F32 z, CBox *pBox, int index, CostumeEditDoc *pDoc)
{
	const PCPart *pPart = (*pList->peaModel)[index];
	PCTextureDef *pTex;
	CBox box;

	pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
	if (UI_GET_SKIN(pList) && (!pDoc->pOrigCostume || pDoc->bSkelChanged || (pTex != GET_REF(pDoc->pOrigCostume->eaParts[index]->pMovableTexture->hMovableTexture)))) {
		Color c = ColorLerp(UI_GET_SKIN(pList)->entry[1], UI_GET_SKIN(pList)->entry[3], 0.5);
		BuildCBox(&box, x, y, w, h);
		display_sprite_box((g_ui_Tex.white), &box, z + Z_DIST_BEFORE_SELECTION, RGBAFromColor(c));
	}

	if (pTex && stricmp(pTex->pcName, "None") != 0) {
		const char *pcText = pTex->pcName;
		if (gUseDispNames) {
			pcText = TranslateDisplayMessage(pTex->displayNameMsg);
		}
		x+=3;
		BuildCBox(&box, x, y, w-6, h);
		clipperPushRestrict(&box);
		gfxfont_Printf(x, y + h/2, z, scale, scale, CENTER_Y, "%s", pcText );
		clipperPop();
	}
}


static void costumeEdit_UIDispNameChange(UIMenuItem *pItem, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	// Get the state
	gUseDispNames = ui_MenuItemGetCheckState(pItem);

	EditorPrefStoreInt(COSTUME_EDITOR, "Option", "UseDisplayNames", gUseDispNames);

	// Update controls for the change
	costumeEdit_UpdateDisplayNameStatus(pDoc);

	// Update the lists for the new strings
	if (!pDoc->bIgnoreFieldChanges) {
		costumeEdit_UpdateLists(pDoc);
	}
}

static void costumeEdit_UIUnlockChange(UIMenuItem *pItem, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	// Get the state
	gCEUnlockAll = ui_MenuItemGetCheckState(pItem);

	EditorPrefStoreInt(COSTUME_EDITOR, "Option", "UnlockAll", gCEUnlockAll);

	// Update the lists for the new options
	if (!pDoc->bIgnoreFieldChanges) {
		costumeEdit_UpdateLists(pDoc);
	}
}

static void costumeEdit_UIColorLinkChange(UIMenuItem *pItem, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	// Get the state
	gCEColorLinkAll = ui_MenuItemGetCheckState(pItem);

	EditorPrefStoreInt(COSTUME_EDITOR, "Option", "ColorLink", gCEColorLinkAll);
}

static void costumeEdit_UIMatLinkChange(UIMenuItem *pItem, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	// Get the state
	gCEMatLinkAll = ui_MenuItemGetCheckState(pItem);

	EditorPrefStoreInt(COSTUME_EDITOR, "Option", "MatLink", gCEMatLinkAll);
}

static void costumeEdit_UISymmetryChange(UIMenuItem *pItem, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);

	// Get the state
	gUseSymmetry = ui_MenuItemGetCheckState(pItem);

	EditorPrefStoreInt(COSTUME_EDITOR, "Option", "UseSymmetry", gUseSymmetry);
}


void costumeEdit_UIFieldChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	costumeEdit_CostumeChanged(pDoc);
}

void costumeEdit_UIScaleFieldChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	static bool bInsideUIScaleFieldChanged = false;

	if(bInsideUIScaleFieldChanged)
		return;
	bInsideUIScaleFieldChanged = true;

	if (!pDoc->bScaleSeperateOverride)
	{
		if (pField == pDoc->pMovScaleXField)
		{
			NOCONST(PCPart) *pPart = pDoc->pCurrentPart;
			NOCONST(PCMovableTextureInfo) *pMovable = pPart ? pPart->pMovableTexture : NULL;
			if (pMovable && pDoc->pMovScaleYField)
			{
				pMovable->fMovableScaleY = pMovable->fMovableScaleX;
				MEFieldRefreshFromData(pDoc->pMovScaleYField);
			}
		}
		else if (pField == pDoc->pMovScaleYField)
		{
			NOCONST(PCPart) *pPart = pDoc->pCurrentPart;
			NOCONST(PCMovableTextureInfo) *pMovable = pPart ? pPart->pMovableTexture : NULL;
			if (pMovable && pDoc->pMovScaleXField)
			{
				pMovable->fMovableScaleX = pMovable->fMovableScaleY;
				MEFieldRefreshFromData(pDoc->pMovScaleXField);
			}
		}
	}

	costumeEdit_CostumeChanged(pDoc);

	bInsideUIScaleFieldChanged = false;
}

void costumeEdit_UITexFieldChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart = pDoc->pCurrentPart;
	PCTextureDef *pTex;

	if (!stricmp(pField->pchFieldName,"PatternTexture"))
	{
		pTex = GET_REF(pPart->hPatternTexture);
		if (pTex && pTex->pValueOptions)
		{
			F32 fMin = pTex->pValueOptions->fValueMin;
			F32 fMax = pTex->pValueOptions->fValueMax;
			costumeTailor_GetTextureValueMinMax((PCPart*)pPart, pTex, pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL, &fMin, &fMax);
			if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
			{
				pPart->pTextureValues->fPatternValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
			}
		}
	}
	else if (!stricmp(pField->pchFieldName,"DetailTexture"))
	{
		pTex = GET_REF(pPart->hDetailTexture);
		if (pTex && pTex->pValueOptions)
		{
			F32 fMin = pTex->pValueOptions->fValueMin;
			F32 fMax = pTex->pValueOptions->fValueMax;
			costumeTailor_GetTextureValueMinMax((PCPart*)pPart, pTex, pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL, &fMin, &fMax);
			if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
			{
				pPart->pTextureValues->fDetailValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
			}
		}
	}
	else if (!stricmp(pField->pchFieldName,"SpecularTexture"))
	{
		pTex = GET_REF(pPart->hSpecularTexture);
		if (pTex && pTex->pValueOptions)
		{
			F32 fMin = pTex->pValueOptions->fValueMin;
			F32 fMax = pTex->pValueOptions->fValueMax;
			costumeTailor_GetTextureValueMinMax((PCPart*)pPart, pTex, pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL, &fMin, &fMax);
			if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
			{
				pPart->pTextureValues->fSpecularValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
			}
		}
	}
	else if (!stricmp(pField->pchFieldName,"DiffuseTexture"))
	{
		pTex = GET_REF(pPart->hDiffuseTexture);
		if (pTex && pTex->pValueOptions)
		{
			F32 fMin = pTex->pValueOptions->fValueMin;
			F32 fMax = pTex->pValueOptions->fValueMax;
			costumeTailor_GetTextureValueMinMax((PCPart*)pPart, pTex, pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL, &fMin, &fMax);
			if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
			{
				pPart->pTextureValues->fDiffuseValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
			}
		}
	}
	else if (!stricmp(pField->pchFieldName,"MovableTexture"))
	{
		pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
		if (pTex && pTex->pValueOptions)
		{
			F32 fMin = pTex->pValueOptions->fValueMin;
			F32 fMax = pTex->pValueOptions->fValueMax;

			costumeTailor_GetTextureValueMinMax((PCPart*)pPart, pTex, pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL, &fMin, &fMax);

			if (fMax > fMin && pTex->pValueOptions->fValueDefault >= fMin && pTex->pValueOptions->fValueDefault <= fMax)
			{
				pPart->pMovableTexture->fMovableValue = (((pTex->pValueOptions->fValueDefault - fMin) * 200.0f)/(fMax - fMin)) - 100.0f;
			}
		}
		if (pTex && pTex->pMovableOptions)
		{
			F32 fMovableMinX, fMovableMaxX, fMovableMinY, fMovableMaxY;
			F32 fMovableMinScaleX, fMovableMaxScaleX, fMovableMinScaleY, fMovableMaxScaleY;
			bool bMovableCanEditPosition, bMovableCanEditRotation, bMovableCanEditScale;
			costumeTailor_GetTextureMovableValues((PCPart*)pPart, pTex, pDoc->pCostume ? GET_REF(pDoc->pCostume->hSpecies) : NULL,
				&fMovableMinX, &fMovableMaxX, &fMovableMinY, &fMovableMaxY,
				&fMovableMinScaleX, &fMovableMaxScaleX, &fMovableMinScaleY, &fMovableMaxScaleY,
				&bMovableCanEditPosition, &bMovableCanEditRotation, &bMovableCanEditScale);

			if (fMovableMaxX > fMovableMinX && pTex->pMovableOptions->fMovableDefaultX >= fMovableMinX && pTex->pMovableOptions->fMovableDefaultX <= fMovableMaxX)
			{
				pPart->pMovableTexture->fMovableX = (((pTex->pMovableOptions->fMovableDefaultX - fMovableMinX) * 200.0f)/(fMovableMaxX - fMovableMinX)) - 100.0f;
			}
			if (fMovableMaxY > fMovableMinY && pTex->pMovableOptions->fMovableDefaultY >= fMovableMinY && pTex->pMovableOptions->fMovableDefaultY <= fMovableMaxY)
			{
				pPart->pMovableTexture->fMovableY = (((pTex->pMovableOptions->fMovableDefaultY - fMovableMinY) * 200.0f)/(fMovableMaxY - fMovableMinY)) - 100.0f;
			}
			if (fMovableMaxScaleX > fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX >= fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX <= fMovableMaxScaleX)
			{
				pPart->pMovableTexture->fMovableScaleX = (((pTex->pMovableOptions->fMovableDefaultScaleX - fMovableMinScaleX) * 100.0f)/(fMovableMaxScaleX - fMovableMinScaleX));
			}
			if (fMovableMaxScaleY > fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY >= fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY <= fMovableMaxScaleY)
			{
				pPart->pMovableTexture->fMovableScaleY = (((pTex->pMovableOptions->fMovableDefaultScaleY - fMovableMinScaleY) * 100.0f)/(fMovableMaxScaleY - fMovableMinScaleY));
			}
			pPart->pMovableTexture->fMovableRotation = pTex->pMovableOptions->fMovableDefaultRotation;
		}
	}
	costumeEdit_CostumeChanged(pDoc);
}


void costumeEdit_UIFieldFinishChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	if (bFinished) {
		costumeEdit_CostumeChanged(pDoc);
	}
}


static void costumeEdit_UIBitsAdd(UIButton *pButton, CostumeEditDoc *pDoc)
{
	NOCONST(PCBitName) *pNameRef;

	// Add an empty bits structure to the costume
	pNameRef = StructCreateNoConst(parse_PCBitName);
	eaPush(&pDoc->pCostume->pArtistData->eaConstantBits, pNameRef);

	// Update the display
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIBitsRemove(UIButton *pButton, CostumeBitsGroup *pGroup)
{
	const char *pcSelectedFromStringBase = NULL;
	char *pcSelected = NULL;
	int i;

	if (gConf.bNewAnimationSystem) {
		//cache data for new animation system version
		ui_ComboBoxGetSelectedsAsString(pGroup->pComboBox, &pcSelected);
		if (pcSelected) {
			pcSelectedFromStringBase = allocAddString(pcSelected);
		}
	}

	//cycle, find, remove, exist
	for(i=eaSize(&pGroup->pDoc->pCostume->pArtistData->eaConstantBits)-1; i>=0; --i) {
		if (!gConf.bNewAnimationSystem) {
			//original animation system version
			if (pGroup->pDoc->pCostume->pArtistData->eaConstantBits[i] == pGroup->pField->pNew) {
				StructDestroyNoConst(parse_PCBitName, pGroup->pDoc->pCostume->pArtistData->eaConstantBits[i]);
				eaRemove(&pGroup->pDoc->pCostume->pArtistData->eaConstantBits,i);
				costumeEdit_CostumeChanged(pGroup->pDoc);
				return;
			}
		} else {
			//new animation system version
			if (pcSelected && pGroup->pDoc->pCostume->pArtistData->eaConstantBits[i]->pcName == pcSelectedFromStringBase) {
				StructDestroyNoConst(parse_PCBitName, pGroup->pDoc->pCostume->pArtistData->eaConstantBits[i]);
				eaRemove(&pGroup->pDoc->pCostume->pArtistData->eaConstantBits, i);
				costumeEdit_CostumeChanged(pGroup->pDoc);
				return;
			}
		}
	}

	if (gConf.bNewAnimationSystem) {
		//word not found for new animation system -> just remove empty box if it exist
		FOR_EACH_IN_EARRAY(pGroup->pDoc->eaBitsGroups, CostumeBitsGroup, checkGroup) {
			if (checkGroup == pGroup) {
				StructDestroyNoConst(parse_PCBitName, pGroup->pDoc->pCostume->pArtistData->eaConstantBits[icheckGroupIndex]);
				eaRemove(&pGroup->pDoc->pCostume->pArtistData->eaConstantBits, icheckGroupIndex);
				costumeEdit_CostumeChanged(pGroup->pDoc);
				return;
			}
		} FOR_EACH_END;
	}

	//word never found = problem
	assertmsg(0, "Missing field");
}


static void costumeEdit_UIFXAdd(UIButton *pButton, CostumeEditDoc *pDoc)
{
	PCFX *pFx;

	// Add an empty FX structure to the costume
	pFx = StructCreate(parse_PCFX);
	eaPush(&pDoc->pCostume->pArtistData->eaFX, CONTAINER_NOCONST(PCFX, pFx));

	// Update the display
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIFXRemove(UIButton *pButton, CostumeFxGroup *pGroup)
{
	int i;

	for(i=eaSize(&pGroup->pDoc->pCostume->pArtistData->eaFX)-1; i>=0; --i) {
		if (pGroup->pDoc->pCostume->pArtistData->eaFX[i] == pGroup->pFxField->pNew) {
			int j;
			StructDestroyNoConst(parse_PCFX, pGroup->pDoc->pCostume->pArtistData->eaFX[i]);
			eaRemove(&pGroup->pDoc->pCostume->pArtistData->eaFX,i);

			for(j = 0; j < eaSize(&pGroup->eaParamGroups); j++)
			{
				CostumeFxParamGroup *paramGroup = pGroup->eaParamGroups[j];

				ui_WidgetQueueFree(UI_WIDGET(paramGroup->pParamCheckButton));

				ui_WidgetQueueFree(paramGroup->pParamWidget);

				free(paramGroup);
			}
			eaClear(&pGroup->eaParamGroups);

			costumeEdit_CostumeChanged(pGroup->pDoc);
			return;
		}
	}
	assertmsg(0, "Missing field");
}


static void costumeEdit_UIFXSwapAdd(UIButton *pButton, CostumeEditDoc *pDoc)
{
	PCFXSwap *pFxSwap;

	// Add an empty FX structure to the costume
	pFxSwap = StructCreate(parse_PCFXSwap);
	eaPush(&pDoc->pCostume->pArtistData->eaFXSwap, CONTAINER_NOCONST(PCFXSwap, pFxSwap));

	// Update the display
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIFXSwapRemove(UIButton *pButton, CostumeFxSwapGroup *pGroup)
{
	int i;

	for(i=eaSize(&pGroup->pDoc->pCostume->pArtistData->eaFXSwap)-1; i>=0; --i) {
		if (pGroup->pDoc->pCostume->pArtistData->eaFXSwap[i] == pGroup->pFxOldField->pNew) {
			StructDestroyNoConst(parse_PCFXSwap, pGroup->pDoc->pCostume->pArtistData->eaFXSwap[i]);
			eaRemove(&pGroup->pDoc->pCostume->pArtistData->eaFXSwap,i);
			costumeEdit_CostumeChanged(pGroup->pDoc);
			return;
		}
	}
	assertmsg(0, "Missing field");
}


void costumeEdit_UIGeoChanged(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	costumeTailor_PickValidPartValues(pDoc->pCostume, pDoc->pCurrentPart, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, true, true, NULL);
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIGlowChanged(MEField *pField, bool bFinished, CostumeEditGlowStruct *pGlow)
{
	U8 color[4];

	// Apply glow value
	switch(pGlow->index) {
		case 0: COPY_COSTUME_COLOR(pGlow->pDoc->pColorButtonCurrentPart->color0, color); break;
		case 1: COPY_COSTUME_COLOR(pGlow->pDoc->pColorButtonCurrentPart->color1, color); break;
		case 2: COPY_COSTUME_COLOR(pGlow->pDoc->pColorButtonCurrentPart->color2, color); break;
		case 3: COPY_COSTUME_COLOR(pGlow->pDoc->pColorButtonCurrentPart->color3, color); break;
	}
	costumeTailor_SetPartColor(pGlow->pDoc->pCostume, GET_REF(pGlow->pDoc->pCostume->hSpecies), NULL, pGlow->pDoc->pColorButtonCurrentPart, pGlow->index, color, pGlow->glow);

	// Continue with change updates
	costumeEdit_UIFieldChanged(pField, bFinished, pGlow->pDoc);
}


static void costumeEdit_UIGlowOverrideChanged(UICheckButton *pCheck, CostumeEditDoc *pDoc)
{
	pDoc->bAllowGlowOverride = ui_CheckButtonGetState(pCheck);

	// UpdateUI
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIHoverGeo(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	int i;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);

		// Find the part in the hover costume
		for(i=eaSize(&pDoc->pHoverCostume->eaParts)-1; i>=0; --i) {
			if (GET_REF(pDoc->pHoverCostume->eaParts[i]->hBoneDef) == GET_REF(pDoc->pCurrentPart->hBoneDef)) {
				// Found the part, so change that part's geometry
				if (!REF_STRING_FROM_HANDLE(pDoc->pHoverCostume->eaParts[i]->hGeoDef) ||
					(stricmp(REF_STRING_FROM_HANDLE(pDoc->pHoverCostume->eaParts[i]->hGeoDef), pDoc->eaGeos[iRow]->pcName) != 0)) {
					// Geometry actually changed so perform the change
					SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, pDoc->eaGeos[iRow]->pcName, pDoc->pHoverCostume->eaParts[i]->hGeoDef);

					// Validate the part given this change
					costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pDoc->pHoverCostume->eaParts[i], GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

					// Display hover costume
					costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
				}
				break;
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}


static void costumeEdit_UIHoverChildGeo(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart;
	NOCONST(PCPart) *pChildPart = NULL;
	NOCONST(PCPart) **eaChildParts = NULL;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);
		assert(pDoc->pCurrentPart);

		// Find the part in the hover costume
		pPart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), pDoc->pCurrentLayer);
		costumeTailor_GetChildParts(pDoc->pHoverCostume, pPart, pDoc->pCurrentLayer, &eaChildParts);
		if (eaSize(&eaChildParts)) {
			pChildPart = eaChildParts[0];
			eaDestroy(&eaChildParts);
		}
		if (pChildPart) {
			// Found the part, now check child part
			if (!REF_STRING_FROM_HANDLE(pChildPart->hGeoDef) ||
				(stricmp(REF_STRING_FROM_HANDLE(pChildPart->hGeoDef), pDoc->eaChildGeos[iRow]->pcName) != 0)) {
				// Geometry actually changed so perform the change
				SET_HANDLE_FROM_STRING(g_hCostumeGeometryDict, pDoc->eaChildGeos[iRow]->pcName, pChildPart->hGeoDef);

				// Validate the part given this change
				costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pPart, GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

				// Display hover costume
				costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}


static void costumeEdit_UIHoverMat(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart, *pValidatePart;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);

		// Find the part in the hover costume
		pValidatePart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), NULL);
		pPart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), pDoc->pCurrentLayer);
		if (pPart) {
			// Found the part, so change that part's material
			if (!REF_STRING_FROM_HANDLE(pPart->hMatDef) ||
				(stricmp(REF_STRING_FROM_HANDLE(pPart->hMatDef), pDoc->eaMats[iRow]->pcName) != 0)) {
				// Material actually changed so perform the change
				SET_HANDLE_FROM_STRING(g_hCostumeMaterialDict, pDoc->eaMats[iRow]->pcName, pPart->hMatDef);

				// Validate the part given this change
				costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pValidatePart, GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

				// Display hover costume
				costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}

static void costumeEdit_UIHoverPatternTex(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart, *pValidatePart;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);

		// Find the part in the hover costume
		pValidatePart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), NULL);
		pPart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), pDoc->pCurrentLayer);
		if (pPart) {
			// Found the part, so change that part's texture
			if (!REF_STRING_FROM_HANDLE(pPart->hPatternTexture) ||
				(stricmp(REF_STRING_FROM_HANDLE(pPart->hPatternTexture), pDoc->eaPatternTex[iRow]->pcName) != 0)) {
				// Texture actually changed so perform the change
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pDoc->eaPatternTex[iRow]->pcName, pPart->hPatternTexture);

				// Validate the part given this change
				costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pValidatePart, GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

				// Display hover costume
				costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}

static void costumeEdit_UIHoverDetailTex(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart, *pValidatePart;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);

		// Find the part in the hover costume
		pValidatePart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), NULL);
		pPart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), pDoc->pCurrentLayer);
		if (pPart) {
			// Found the part, so change that part's texture
			if (!REF_STRING_FROM_HANDLE(pPart->hDetailTexture) ||
				(stricmp(REF_STRING_FROM_HANDLE(pPart->hDetailTexture), pDoc->eaDetailTex[iRow]->pcName) != 0)) {
				// Texture actually changed so perform the change
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pDoc->eaDetailTex[iRow]->pcName, pPart->hDetailTexture);

				// Validate the part given this change
				costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pValidatePart, GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

				// Display hover costume
				costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}

static void costumeEdit_UIHoverSpecularTex(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart, *pValidatePart;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);

		// Find the part in the hover costume
		pValidatePart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), NULL);
		pPart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), pDoc->pCurrentLayer);
		if (pPart) {
			// Found the part, so change that part's texture
			if (!REF_STRING_FROM_HANDLE(pPart->hSpecularTexture) ||
				(stricmp(REF_STRING_FROM_HANDLE(pPart->hSpecularTexture), pDoc->eaSpecularTex[iRow]->pcName) != 0)) {
				// Texture actually changed so perform the change
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pDoc->eaSpecularTex[iRow]->pcName, pPart->hSpecularTexture);

				// Validate the part given this change
				costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pValidatePart, GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

				// Display hover costume
				costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}

static void costumeEdit_UIHoverDiffuseTex(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart, *pValidatePart;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);

		// Find the part in the hover costume
		pValidatePart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), NULL);
		pPart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), pDoc->pCurrentLayer);
		if (pPart) {
			// Found the part, so change that part's texture
			if (!REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture) ||
				(stricmp(REF_STRING_FROM_HANDLE(pPart->hDiffuseTexture), pDoc->eaDiffuseTex[iRow]->pcName) != 0)) {
				// Texture actually changed so perform the change
				SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pDoc->eaDiffuseTex[iRow]->pcName, pPart->hDiffuseTexture);

				// Validate the part given this change
				costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pValidatePart, GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

				// Display hover costume
				costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}

static void costumeEdit_UIHoverMovableTex(UIComboBox *pCombo, S32 iRow, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pPart, *pValidatePart;

	if (iRow >= 0) {
		if (!pDoc->pHoverCostume) {
			pDoc->pHoverCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
		}
		assert(pDoc->pHoverCostume);

		// Find the part in the hover costume
		pValidatePart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), NULL);
		pPart = costumeTailor_GetPartByBone(pDoc->pHoverCostume, GET_REF(pDoc->pCurrentPart->hBoneDef), pDoc->pCurrentLayer);
		if (pPart) {
			// Found the part, so change that part's texture
			if (!REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture) ||
				(stricmp(REF_STRING_FROM_HANDLE(pPart->pMovableTexture->hMovableTexture), pDoc->eaMovableTex[iRow]->pcName) != 0)) {
					// Texture actually changed so perform the change
					SET_HANDLE_FROM_STRING(g_hCostumeTextureDict, pDoc->eaMovableTex[iRow]->pcName, pPart->pMovableTexture->hMovableTexture);

					// Validate the part given this change
					costumeTailor_PickValidPartValues(pDoc->pHoverCostume, pValidatePart, GET_REF(pDoc->pHoverCostume->hSpecies), NULL, NULL, gUseDispNames, gCEUnlockAll, false, true, NULL);

					// Display hover costume
					costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pHoverCostume, NULL);
			}
		}
	} else {
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pHoverCostume);

		// Display non-hover costume
		costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
	}
}


void costumeEdit_UILayerSelectedCombo(UIComboBox *pBox, CostumeEditDoc *pDoc)
{
	int iRow = ui_ComboBoxGetSelected(pBox);
	if (iRow >= 0) {
		pDoc->pCurrentLayer = pDoc->eaLayers[iRow];
	} else {
		pDoc->pCurrentLayer = NULL;
	}

	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIMenuSelectColorPastePart(NOCONST(PCPart) *pPart, CEMenuData *pData)
{
	if (stricmp(pData->pcMenuName,"color0") == 0) {
		costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pPart, 0, gColorClipboard, gColorGlowClipboard);
	} else if (stricmp(pData->pcMenuName,"color1") == 0) {
		costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pPart, 1, gColorClipboard, gColorGlowClipboard);
	} else if (stricmp(pData->pcMenuName,"color2") == 0) {
		costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pPart, 2, gColorClipboard, gColorGlowClipboard);
	} else if (stricmp(pData->pcMenuName,"color3") == 0) {
		costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pPart, 3, gColorClipboard, gColorGlowClipboard);
	}
}

static void costumeEdit_UIMenuSelectColorCopy(UIMenuItem *pItem, CEMenuData *pData)
{
	if (stricmp(pData->pcMenuName,"color0") == 0) {
		copyVec4(pData->pDoc->pColorButtonCurrentPart->color0, gColorClipboard);
		gColorGlowClipboard = GET_PART_GLOWSCALE(pData->pDoc->pColorButtonCurrentPart,0);
	} else if (stricmp(pData->pcMenuName,"color1") == 0) {
		copyVec4(pData->pDoc->pColorButtonCurrentPart->color1, gColorClipboard);
		gColorGlowClipboard = GET_PART_GLOWSCALE(pData->pDoc->pColorButtonCurrentPart,1);
	} else if (stricmp(pData->pcMenuName,"color2") == 0) {
		copyVec4(pData->pDoc->pColorButtonCurrentPart->color2, gColorClipboard);
		gColorGlowClipboard = GET_PART_GLOWSCALE(pData->pDoc->pColorButtonCurrentPart,2);
	} else if (stricmp(pData->pcMenuName,"color3") == 0) {
		copyVec4(pData->pDoc->pColorButtonCurrentPart->color3, gColorClipboard);
		gColorGlowClipboard = GET_PART_GLOWSCALE(pData->pDoc->pColorButtonCurrentPart,3);
	}
	bColorClipboardSet = true;
}


static void costumeEdit_UIMenuSelectColorCopyToMirror(UIMenuItem *pItem, CEMenuData *pData)
{
	PCBoneDef *pBone;
	PCBoneDef *pMirror = NULL;
	int i;

	pBone = GET_REF(pData->pDoc->pColorButtonCurrentPart->hBoneDef);
	if (pBone) {
		pMirror = GET_REF(pBone->hMirrorBone);
	}
	if (pMirror) {
		// First put on clipboard
		costumeEdit_UIMenuSelectColorCopy(pItem, pData);

		// Find the mirror part and paste the color
		for(i=eaSize(&pData->pDoc->pCostume->eaParts)-1; i>=0; --i) {
			if (pMirror == GET_REF(pData->pDoc->pCostume->eaParts[i]->hBoneDef)) {
				// If this part is not color linked, then unlink the mirror part before setting color
				if (pData->pDoc->pColorButtonCurrentPart->eColorLink == kPCColorLink_None) {
					pData->pDoc->pCostume->eaParts[i]->eColorLink = kPCColorLink_None;
				}

				costumeEdit_UIMenuSelectColorPastePart(pData->pDoc->pCostume->eaParts[i], pData);
			}
		}

		costumeEdit_CostumeChanged(pData->pDoc);
	}
}


static void costumeEdit_UIMenuSelectColorCopyToAll(UIMenuItem *pItem, CEMenuData *pData)
{
	int i;

	// First put on clipboard
	costumeEdit_UIMenuSelectColorCopy(pItem, pData);

	// Iterate all parts and paste the color
	for(i=eaSize(&pData->pDoc->pCostume->eaParts)-1; i>=0; --i) {
		costumeEdit_UIMenuSelectColorPastePart(pData->pDoc->pCostume->eaParts[i], pData);
	}

	costumeEdit_CostumeChanged(pData->pDoc);
}


static void costumeEdit_UIMenuSelectColorPaste(UIMenuItem *pItem, CEMenuData *pData)
{
	costumeEdit_UIMenuSelectColorPastePart(pData->pDoc->pColorButtonCurrentPart, pData);
	costumeEdit_CostumeChanged(pData->pDoc);
}


static void costumeEdit_UIMenuSelectColorRevert(UIMenuItem *pItem, CEMenuData *pData)
{
	NOCONST(PCPart) *pOrigPart;

	// Find part on original costume and copy it back
	pOrigPart = costumeEdit_GetOrigPart(pData->pDoc, GET_REF(pData->pDoc->pColorButtonCurrentPart->hBoneDef));
	if (pOrigPart) {
		if (stricmp(pData->pcMenuName,"color0") == 0) {
			costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pData->pDoc->pColorButtonCurrentPart, 0, pOrigPart->color0, GET_PART_GLOWSCALE(pOrigPart,0));
		} else if (stricmp(pData->pcMenuName,"color1") == 0) {
			costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pData->pDoc->pColorButtonCurrentPart, 1, pOrigPart->color1, GET_PART_GLOWSCALE(pOrigPart,1));
		} else if (stricmp(pData->pcMenuName,"color2") == 0) {
			costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pData->pDoc->pColorButtonCurrentPart, 2, pOrigPart->color2, GET_PART_GLOWSCALE(pOrigPart,2));
		} else if (stricmp(pData->pcMenuName,"color3") == 0) {
			costumeTailor_SetPartColor(pData->pDoc->pCostume, GET_REF(pData->pDoc->pCostume->hSpecies), NULL, pData->pDoc->pColorButtonCurrentPart, 3, pOrigPart->color3, GET_PART_GLOWSCALE(pOrigPart,3));
		}
		costumeEdit_CostumeChanged(pData->pDoc);
	}
}


static void costumeEdit_UIMenuSelectColorSetCopy(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	copyVec4(pDoc->pColorButtonCurrentPart->color0, gColorSetClipboard.color0);
	copyVec4(pDoc->pColorButtonCurrentPart->color1, gColorSetClipboard.color1);
	copyVec4(pDoc->pColorButtonCurrentPart->color2, gColorSetClipboard.color2);
	copyVec4(pDoc->pColorButtonCurrentPart->color3, gColorSetClipboard.color3);
	gColorSetClipboard.glowScale[0] = GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,0);
	gColorSetClipboard.glowScale[1] = GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,1);
	gColorSetClipboard.glowScale[2] = GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,2);
	gColorSetClipboard.glowScale[3] = GET_PART_GLOWSCALE(pDoc->pColorButtonCurrentPart,3);

	bColorSetClipboardSet = true;
}


static void costumeEdit_UIMenuSelectColorSetCopyToMirror(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	PCBoneDef *pBone;
	PCBoneDef *pMirror = NULL;
	int i;

	pBone = GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef);
	if (pBone) {
		pMirror = GET_REF(pBone->hMirrorBone);
	}
	if (pMirror) {
		// First put on clipboard
		costumeEdit_UIMenuSelectColorSetCopy(pItem, pDoc);

		// Find the mirror part and paste the color
		for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
			if (pMirror == GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef)) {
				// If this part is not color linked, then unlink the mirror part before setting color
				if (pDoc->pColorButtonCurrentPart->eColorLink == kPCColorLink_None) {
					pDoc->pCostume->eaParts[i]->eColorLink = kPCColorLink_None;
				}

				costumeTailor_SetPartColors(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pCostume->eaParts[i], 
									gColorSetClipboard.color0, gColorSetClipboard.color1, 
									gColorSetClipboard.color2, gColorSetClipboard.color3,
									gColorSetClipboard.glowScale);
				costumeEdit_UpdateColorButtons(pDoc);
			}
		}

		costumeEdit_CostumeChanged(pDoc);
	}
}


static void costumeEdit_UIMenuSelectColorSetCopyToAll(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	int i;

	// First put on clipboard
	costumeEdit_UIMenuSelectColorSetCopy(pItem, pDoc);

	// Iterate all parts and paste the color
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		costumeTailor_SetPartColors(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pCostume->eaParts[i], 
							gColorSetClipboard.color0, gColorSetClipboard.color1, 
							gColorSetClipboard.color2, gColorSetClipboard.color3,
							gColorSetClipboard.glowScale);
		costumeEdit_UpdateColorButtons(pDoc);
	}

	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIMenuSelectColorSetPaste(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	costumeTailor_SetPartColors(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart, 
						gColorSetClipboard.color0, gColorSetClipboard.color1, 
						gColorSetClipboard.color2, gColorSetClipboard.color3,
						gColorSetClipboard.glowScale);
	costumeEdit_UpdateColorButtons(pDoc);

	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIMenuSelectColorSetRevert(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pOrigPart;

	// Find part on original costume and copy it back
	pOrigPart = costumeEdit_GetOrigPart(pDoc, GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef));
	if (pOrigPart) {
		costumeTailor_SetPartColors(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart, 
							pOrigPart->color0, pOrigPart->color1, 
							pOrigPart->color2, pOrigPart->color3,
							pOrigPart->pCustomColors->glowScale);
		costumeEdit_UpdateColorButtons(pDoc);
		costumeEdit_CostumeChanged(pDoc);
	}
}

static void costumeEdit_UIApplyDyePack(UIWidget* pWidget, CostumeEditDoc *pDoc)
{
	ItemDef *pItem = item_DefFromName(pDoc->pCurrentDyePack->pchDyePack);

	if (pItem)
	{
		U8 color0[4];
		U8 color1[4];
		U8 color2[4];
		U8 color3[4];

		color0[0] = pItem->vDyeColor0[0] * 255 + .5;
		color0[1] = pItem->vDyeColor0[1] * 255 + .5;
		color0[2] = pItem->vDyeColor0[2] * 255 + .5;
		color0[3] = 255;

		color1[0] = pItem->vDyeColor1[0] * 255 + .5;
		color1[1] = pItem->vDyeColor1[1] * 255 + .5;
		color1[2] = pItem->vDyeColor1[2] * 255 + .5;
		color1[3] = 255;

		color2[0] = pItem->vDyeColor2[0] * 255 + .5;
		color2[1] = pItem->vDyeColor2[1] * 255 + .5;
		color2[2] = pItem->vDyeColor2[2] * 255 + .5;
		color2[3] = 255;

		color3[0] = pItem->vDyeColor3[0] * 255 + .5;
		color3[1] = pItem->vDyeColor3[1] * 255 + .5;
		color3[2] = pItem->vDyeColor3[2] * 255 + .5;
		color3[3] = 255;

		costumeTailor_SetPartColors(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pDoc->pColorButtonCurrentPart, 
			color0, color1, color2, color3,
			pDoc->pCurrentPart->pCustomColors->glowScale);
		costumeEdit_UpdateColorButtons(pDoc);
		costumeEdit_CostumeChanged(pDoc);
	}
}


static void costumeEdit_UIMenuSelectPartCopy(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	StructDestroyNoConstSafe(parse_PCPart, &gPartClipboard);
	gPartClipboard = StructCloneNoConst(parse_PCPart, pDoc->pCurrentPart);
}


static void costumeEdit_UIMenuSelectPartEditDef(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	if (pDoc->pCurrentPart) {
		PCGeometryDef *pGeo = NULL;
		PCMaterialDef *pMat = NULL;
		PCTextureDef *pTex = NULL;

		costumeDefEdit_SelectDefBone(gDefDoc, GET_REF(pDoc->pCurrentPart->hBoneDef));
		pGeo = GET_REF(pDoc->pCurrentPart->hGeoDef);
		if (pGeo) {
			costumeDefEdit_DefSetGeo(gDefDoc, pGeo);
			pMat = GET_REF(pDoc->pCurrentPart->hMatDef);
			if (pMat) {
				costumeDefEdit_DefSetMat(gDefDoc, pMat);
			}
		}
		costumeDefEdit_DefUpdateLists(gDefDoc);
	}
}


static void costumeEdit_UIMenuSelectPartPaste(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	if (gPartClipboard) {
		char buf[260];
		int i;

		// Find the part with the matching bone
		for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
			if (GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef) == GET_REF(gPartClipboard->hBoneDef)) {
				StructCopyAllNoConst(parse_PCPart, gPartClipboard, pDoc->pCostume->eaParts[i]);
				costumeEdit_CostumeChanged(pDoc);
				return;
			}
		}

		sprintf(buf, "Cannot paste costume part because this costume has no bone named '%s'", REF_STRING_FROM_HANDLE(gPartClipboard->hBoneDef));
		ui_DialogPopup("Paste Failed", buf);
	}
}


static void costumeEdit_UIMenuSelectPartRevert(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pOrigPart;

	// Find part on original costume and copy it back
	pOrigPart = costumeEdit_GetOrigPart(pDoc, GET_REF(pDoc->pCurrentPart->hBoneDef));
	if (pOrigPart) {
		StructCopyAllNoConst(parse_PCPart, pOrigPart, pDoc->pCurrentPart);
		costumeEdit_CostumeChanged(pDoc);
	}
}


static void costumeEdit_UIMenuSelectReflectPastePart(NOCONST(PCPart) *pPart, CEMenuData *pData)
{
	if (stricmp(pData->pcMenuName,"reflect0") == 0) {
		pPart->pCustomColors->reflection[0] = gReflectClipboard;
		pPart->pCustomColors->specularity[0] = gSpecularClipboard;
	} else if (stricmp(pData->pcMenuName,"reflect1") == 0) {
		pPart->pCustomColors->reflection[1] = gReflectClipboard;
		pPart->pCustomColors->specularity[1] = gSpecularClipboard;
	} else if (stricmp(pData->pcMenuName,"reflect2") == 0) {
		pPart->pCustomColors->reflection[2] = gReflectClipboard;
		pPart->pCustomColors->specularity[2] = gSpecularClipboard;
	} else if (stricmp(pData->pcMenuName,"reflect3") == 0) {
		pPart->pCustomColors->reflection[3] = gReflectClipboard;
		pPart->pCustomColors->specularity[3] = gSpecularClipboard;
	}
	pPart->pCustomColors->bCustomReflection = true;
	pPart->pCustomColors->bCustomSpecularity = true;
}

static void costumeEdit_UIMenuSelectReflectCopy(UIMenuItem *pItem, CEMenuData *pData)
{
	if (stricmp(pData->pcMenuName,"reflect0") == 0) {
		gReflectClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[0];
		gSpecularClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[0];
	} else if (stricmp(pData->pcMenuName,"reflect1") == 0) {
		gReflectClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[1];
		gSpecularClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[1];
	} else if (stricmp(pData->pcMenuName,"reflect2") == 0) {
		gReflectClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[2];
		gSpecularClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[2];
	} else if (stricmp(pData->pcMenuName,"reflect3") == 0) {
		gReflectClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[3];
		gSpecularClipboard = pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[3];
	}
	bReflectClipboardSet = true;
}


static void costumeEdit_UIMenuSelectReflectCopyToMirror(UIMenuItem *pItem, CEMenuData *pData)
{
	PCBoneDef *pBone;
	PCBoneDef *pMirror = NULL;
	int i;

	pBone = GET_REF(pData->pDoc->pColorButtonCurrentPart->hBoneDef);
	if (pBone) {
		pMirror = GET_REF(pBone->hMirrorBone);
	}
	if (pMirror) {
		// First put on clipboard
		costumeEdit_UIMenuSelectReflectCopy(pItem, pData);

		// Iterate all parts and paste the reflection to all
		for(i=eaSize(&pData->pDoc->pCostume->eaParts)-1; i>=0; --i) {
			if (pMirror == GET_REF(pData->pDoc->pCostume->eaParts[i]->hBoneDef)) {
				costumeEdit_UIMenuSelectReflectPastePart(pData->pDoc->pCostume->eaParts[i], pData);
			}
		}

		costumeEdit_CostumeChanged(pData->pDoc);
	}
}


static void costumeEdit_UIMenuSelectReflectCopyToAll(UIMenuItem *pItem, CEMenuData *pData)
{
	int i;

	// First put on clipboard
	costumeEdit_UIMenuSelectReflectCopy(pItem, pData);

	// Iterate all parts and paste the reflection to all
	for(i=eaSize(&pData->pDoc->pCostume->eaParts)-1; i>=0; --i) {
		costumeEdit_UIMenuSelectReflectPastePart(pData->pDoc->pCostume->eaParts[i], pData);
	}

	costumeEdit_CostumeChanged(pData->pDoc);
}

static void costumeEdit_UIMenuSelectReflectPaste(UIMenuItem *pItem, CEMenuData *pData)
{
	costumeEdit_UIMenuSelectReflectPastePart(pData->pDoc->pColorButtonCurrentPart, pData);
	costumeEdit_CostumeChanged(pData->pDoc);
}


static void costumeEdit_UIMenuSelectReflectRevert(UIMenuItem *pItem, CEMenuData *pData)
{
	NOCONST(PCPart) *pOrigPart;

	// Find part on original costume and copy it back
	pOrigPart = costumeEdit_GetOrigPart(pData->pDoc, GET_REF(pData->pDoc->pColorButtonCurrentPart->hBoneDef));
	if (pOrigPart) {
		if (stricmp(pData->pcMenuName,"reflect0") == 0) {
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[0] = pOrigPart->pCustomColors->reflection[0];
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[0] = pOrigPart->pCustomColors->specularity[0];
		} else if (stricmp(pData->pcMenuName,"reflect1") == 0) {
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[1] = pOrigPart->pCustomColors->reflection[1];
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[1] = pOrigPart->pCustomColors->specularity[1];
		} else if (stricmp(pData->pcMenuName,"reflect2") == 0) {
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[2] = pOrigPart->pCustomColors->reflection[2];
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[2] = pOrigPart->pCustomColors->specularity[2];
		} else if (stricmp(pData->pcMenuName,"reflect3") == 0) {
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[3] = pOrigPart->pCustomColors->reflection[3];
			pData->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[3] = pOrigPart->pCustomColors->specularity[3];
		}
		costumeEdit_CostumeChanged(pData->pDoc);
	}
}


static void costumeEdit_UIMenuSelectReflectSetCopy(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	copyVec4(pDoc->pColorButtonCurrentPart->pCustomColors->reflection, gReflectSetClipboard);
	copyVec4(pDoc->pColorButtonCurrentPart->pCustomColors->specularity, gSpecularSetClipboard);

	bReflectSetClipboardSet = true;
}


static void costumeEdit_UIMenuSelectReflectSetCopyToMirror(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	PCBoneDef *pBone;
	PCBoneDef *pMirror = NULL;
	int i;

	pBone = GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef);
	if (pBone) {
		pMirror = GET_REF(pBone->hMirrorBone);
	}
	if (pMirror) {
		// First put on clipboard
		costumeEdit_UIMenuSelectReflectSetCopy(pItem, pDoc);

		// Find the mirror and paste the reflection to all
		for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
			if (pMirror == GET_REF(pDoc->pCostume->eaParts[i]->hBoneDef)) {
				copyVec4(gReflectSetClipboard, pDoc->pCostume->eaParts[i]->pCustomColors->reflection);
				copyVec4(gSpecularSetClipboard, pDoc->pCostume->eaParts[i]->pCustomColors->specularity);
			}
		}

		costumeEdit_CostumeChanged(pDoc);
	}
}


static void costumeEdit_UIMenuSelectReflectSetCopyToAll(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	int i;

	// First copy to clipboard
	costumeEdit_UIMenuSelectReflectSetCopy(pItem, pDoc);

	// Iterate all parts and paste
	for(i=eaSize(&pDoc->pCostume->eaParts)-1; i>=0; --i) {
		copyVec4(gReflectSetClipboard, pDoc->pCostume->eaParts[i]->pCustomColors->reflection);
		copyVec4(gSpecularSetClipboard, pDoc->pCostume->eaParts[i]->pCustomColors->specularity);
	}

	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIMenuSelectReflectSetPaste(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	copyVec4(gReflectSetClipboard, pDoc->pColorButtonCurrentPart->pCustomColors->reflection);
	copyVec4(gSpecularSetClipboard, pDoc->pColorButtonCurrentPart->pCustomColors->specularity);

	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIMenuSelectReflectSetRevert(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pOrigPart;

	// Find part on original costume and copy it back
	pOrigPart = costumeEdit_GetOrigPart(pDoc, GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef));
	if (pOrigPart) {
		copyVec4(pOrigPart->pCustomColors->reflection, pDoc->pColorButtonCurrentPart->pCustomColors->reflection);
		copyVec4(pOrigPart->pCustomColors->specularity, pDoc->pColorButtonCurrentPart->pCustomColors->specularity);
		costumeEdit_CostumeChanged(pDoc);
	}
}


static void costumeEdit_UIMenuSelectSkelRevert(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	// Revert will cause callback that updates the skeleton
	MEFieldRevert(pDoc->pSkeletonField);
}


static void costumeEdit_UIMenuSelectSpeciesRevert(UIMenuItem *pItem, CostumeEditDoc *pDoc)
{
	// Revert will cause callback that updates the skeleton
	MEFieldRevert(pDoc->pSpeciesField);
}


static void costumeEdit_UIMenuPreopenColor(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	PCBoneDef *pBone = pDoc->pColorButtonCurrentPart ? GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef) : NULL;
	pMenu->items[1]->active = (pBone && GET_REF(pBone->hMirrorBone));
	pMenu->items[3]->active = bColorClipboardSet;
	pMenu->items[4]->active = (!pDoc->bSkelChanged && pDoc->pOrigCostume != NULL);
}


static void costumeEdit_UIMenuPreopenColorSet(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	PCBoneDef *pBone = pDoc->pColorButtonCurrentPart ? GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef) : NULL;
	pMenu->items[1]->active = (pBone && GET_REF(pBone->hMirrorBone));
	pMenu->items[3]->active = bColorSetClipboardSet;
	pMenu->items[4]->active = (!pDoc->bSkelChanged && pDoc->pOrigCostume != NULL);
}


static void costumeEdit_UIMenuPreopenCostume(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	pMenu->items[1]->active = (pDoc->pOrigCostume && (stricmp(pDoc->pOrigCostume->pcName,pDoc->pCostume->pcName) != 0));
	pMenu->items[3]->active = (pDoc->pOrigCostume != NULL);
	pMenu->items[5]->active = false;
}


static void costumeEdit_UIMenuPreopenEdit(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	pMenu->items[0]->active = false;
}


static void costumeEdit_UIMenuPreopenGeo(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	pMenu->items[2]->active = (!pDoc->bSkelChanged && pDoc->pOrigCostume != NULL);
}


static void costumeEdit_UIMenuPreopenReflect(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	PCBoneDef *pBone = pDoc->pColorButtonCurrentPart ? GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef) : NULL;
	pMenu->items[1]->active = (pBone && GET_REF(pBone->hMirrorBone));
	pMenu->items[3]->active = bReflectClipboardSet;
	pMenu->items[4]->active = (!pDoc->bSkelChanged && pDoc->pOrigCostume != NULL);
}


static void costumeEdit_UIMenuPreopenReflectSet(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	PCBoneDef *pBone = pDoc->pColorButtonCurrentPart ? GET_REF(pDoc->pColorButtonCurrentPart->hBoneDef) : NULL;
	pMenu->items[1]->active = (pBone && GET_REF(pBone->hMirrorBone));
	pMenu->items[3]->active = bReflectSetClipboardSet;
	pMenu->items[4]->active = (!pDoc->bSkelChanged && pDoc->pOrigCostume != NULL);
}


static void costumeEdit_UIMenuPreopenSkel(UIMenu *pMenu, CostumeEditDoc *pDoc)
{
	pMenu->items[1]->active = false;
}


static void costumeEdit_UIRandomCostume(UIButton *pButton, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);
	costumeEdit_RandomCostume(pDoc);
}

static void costumeEdit_UIMakeLegal(UIButton *pButton, EMEditor *pEditor)
{
	CostumeEditDoc *pDoc = (CostumeEditDoc*)emGetActiveEditorDoc();
	assert(pDoc);
	costumeTailor_MakeCostumeValid(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, false, gCEUnlockAll, true, NULL, false, NULL, false, NULL);

	// Remove references to current part
	pDoc->pCurrentPart = NULL;
	pDoc->pOrigPart = NULL;

	// Reset to use the new costume
	costumeEdit_BuildScaleGroups(pDoc);
	costumeEdit_SelectBone(pDoc, NULL);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}

static void costumeEdit_UIToggleHeightRuler(UICheckButton *pCheck, EMEditor *pEditor)
{
	EditorPrefStoreInt(COSTUME_EDITOR, "Option", "ShowHeightRuler", ui_CheckButtonGetState(pCheck));
}

static void costumeEdit_UIReflectChanged(MEField *pField, bool bFinished, CostumeEditReflectStruct *pReflect)
{
	if(!pReflect->pDoc->pColorButtonCurrentPart || !pReflect->pDoc->pColorButtonCurrentPart->pCustomColors)
		return;

	pReflect->pDoc->pColorButtonCurrentPart->pCustomColors->reflection[pReflect->index] = pReflect->reflection;
	pReflect->pDoc->pColorButtonCurrentPart->pCustomColors->specularity[pReflect->index] = pReflect->specularity;

	// Continue with change updates
	costumeEdit_UIFieldChanged(pField, bFinished, pReflect->pDoc);
}


static void costumeEdit_UIReflectOverrideChanged(UICheckButton *pCheck, CostumeEditDoc *pDoc)
{
	pDoc->bAllowReflectOverride = ui_CheckButtonGetState(pCheck);

	// UpdateUI
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UIScaleZero(UIButton *pButton, CostumeEditDoc *pDoc) 
{
	int i;

	for(i=eaSize(&pDoc->pCostume->eaScaleValues)-1; i>=0; --i) {
		pDoc->pCostume->eaScaleValues[i]->fValue = 0.0;
	}

	// Update UI
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UISetScope(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	// Update the filename appropriately
	resFixFilename(g_hPlayerCostumeDict, pDoc->pCostume->pcName, pDoc->pCostume);

	// Call on to do regular updates
	costumeEdit_UIFieldChanged(pField, bFinished, pDoc);
}


static void costumeEdit_UISetName(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	if (pDoc->pCostume->pcName && strlen(pDoc->pCostume->pcName) >= MAX_PATH)
	{
		char temp[MAX_PATH];
		temp[0] = '\0';
		strncat(temp, pDoc->pCostume->pcName, MAX_PATH - 1);
		pDoc->pCostume->pcName = allocAddString(temp);
	}

	// When the name changes, change the title of the window
	ui_WindowSetTitle(pDoc->pMainWindow, pDoc->pCostume->pcName);

	// Make sure the browser picks up the new costume name if the name changed
	// EDITOR MANAGER SPECIFIC
	sprintf(pDoc->emDoc.doc_name, "%s", pDoc->pCostume->pcName);
	sprintf(pDoc->emDoc.doc_display_name, "%s", pDoc->pCostume->pcName);
	pDoc->emDoc.name_changed = 1;

	// Call the scope function to avoid duplicating logic
	costumeEdit_UISetScope(pField, bFinished, pDoc);
}


static void costumeEdit_UISkeletonChange(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	PCSkeletonDef *pSkel;

	// Get the skeleton
	pSkel = GET_REF(pDoc->pCostume->hSkeleton);
	assertmsg(pSkel, "Costume must have a skeleton.");

	REMOVE_HANDLE(pDoc->pCostume->hSpecies);

	// Change the skeleton
	costumeTailor_ChangeSkeleton(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, pSkel);

	// Then fill the costume back up with the new skeleton
	costumeTailor_FillAllBones(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL/*powerFX*/, NULL, true, true, gUseDispNames);

	//Remove refs to current part to prevent horrible things
	
	pDoc->pCurrentPart = NULL;
	pDoc->pOrigPart = NULL;
	// Reset to use the new costume
	costumeEdit_BuildScaleGroups(pDoc);
	costumeEdit_SelectBone(pDoc, NULL);
	if (gDefDoc) {
		costumeDefEdit_SetSkeleton(gDefDoc, GET_REF(pDoc->pCostume->hSkeleton));
		costumeDefEdit_DefUpdateLists(gDefDoc);
	}
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);
}


static void costumeEdit_UISpeciesChange(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	SpeciesDef *pSpecies = GET_REF(pDoc->pCostume->hSpecies);
	PCSkeletonDef *pSkel;

	if (pDoc->bIgnoreFieldChanges) {
		return;
	}

	// Get the skeleton
	pSkel = GET_REF(pDoc->pCostume->hSkeleton);
	assertmsg(pSkel, "Costume must have a skeleton.");

	if (pSpecies && !strcmp(pSpecies->pcName,"None"))
	{
		REMOVE_HANDLE(pDoc->pCostume->hSpecies);
		pSpecies = NULL;
	}

	// Change the skeleton
	costumeTailor_ChangeSkeleton(pDoc->pCostume, pSpecies, NULL, pSkel);

	// Then fill the costume back up with the new skeleton
	costumeTailor_FillAllBones(pDoc->pCostume, pSpecies, NULL/*powerFX*/, NULL, true, true, gUseDispNames);

	pDoc->pCurrentPart = NULL;
	pDoc->pOrigPart = NULL;
	// Reset to use the new costume
	costumeEdit_BuildScaleGroups(pDoc);
	costumeEdit_SelectBone(pDoc, NULL);
	
	if (gDefDoc) {
		costumeDefEdit_SetSkeleton(gDefDoc, GET_REF(pDoc->pCostume->hSkeleton));
		costumeDefEdit_DefUpdateLists(gDefDoc);
	}
	costumeEdit_CostumeChanged(pDoc);
}

static void costumeEdit_UIVoiceChange(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
	//Perhaps play a sound?
}

static void costumeEdit_UILinkAllChange(MEField *pField, bool bFinished, CostumeEditDoc *pDoc)
{
}

static void costumeEdit_UITexAdd(UIButton *pButton, CostumeEditDoc *pDoc)
{
	NOCONST(PCTextureRef) *pNameRef;

	// Add an empty bits structure to the costume
	pNameRef = StructCreateNoConst(parse_PCTextureRef);
	eaPush(&pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures, pNameRef);

	// Update the display
	costumeEdit_CostumeChanged(pDoc);
}


static void costumeEdit_UITexRemove(UIButton *pButton, CostumeTexGroup *pGroup)
{
	int i;

	for(i=eaSize(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures)-1; i>=0; --i) {
		if (pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures[i] == pGroup->pField->pNew) {
			StructDestroyNoConst(parse_PCTextureRef, pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures[i]);
			eaRemove(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraTextures,i);
			costumeEdit_CostumeChanged(pGroup->pDoc);
			return;
		}
	}
	assertmsg(0, "Missing field");
}


void costumeEdit_UIValueGroupChanged(UISliderTextEntry *pSlider, bool bFinished, CostumeColorGroup *pGroup)
{
	F32 fValue;
	S32 sliderIdx = 0;
	S32 numValues = costumeTailor_GetMatConstantNumValues(pGroup->pcName);
	
	// determine which of this group's sliders was changed
	if (pSlider == pGroup->pSlider1) {
		sliderIdx = 0;
	}
	else if (pSlider == pGroup->pSlider2) {
		sliderIdx = 1;
	}
	else if (pSlider == pGroup->pSlider3) {
		sliderIdx = 2;
	}
	else if (pSlider == pGroup->pSlider4) {
		sliderIdx = 3;
	}
	else {
		// it's not one of this group's sliders after all
		return;
	}

	fValue = atof(ui_SliderTextEntryGetText(pSlider));
	if (fValue != pGroup->currentValue[sliderIdx]) {
		int idx;
		bool last = (sliderIdx == (numValues - 1));	// true if this is the highest used slider index

		pGroup->currentValue[sliderIdx] = fValue;
		if (last) {
			// populate unused values with copies of the last valid value
			for (idx = (sliderIdx+1); idx < 4; idx++) {
				pGroup->currentValue[idx] = fValue;
			}
		}

		// Also set it into the extra constant if available
		if (pGroup->bIsSet) {
			int i;
			bool bFound = false;

			for(i=eaSize(&pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants)-1; i>=0; --i) {
				if (stricmp(pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants[i]->pcName, pGroup->pcName) == 0) {
					pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants[i]->values[sliderIdx] = fValue;
					if (last) {
						// populate unused values with copies of the last valid value
						for (idx = (sliderIdx+1); idx < 4; idx++) {
							pGroup->pDoc->pColorButtonCurrentPart->pArtistData->eaExtraConstants[i]->values[idx] = fValue;
						}
					}
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				Alertf("Value group does not have the constant in place.");
			}
		}

		// Update the costume
		costumeEdit_CostumeChanged(pGroup->pDoc);
	}
}


//---------------------------------------------------------------------------------------------------
// UI Initialization
//---------------------------------------------------------------------------------------------------


UIWindow *costumeEdit_InitWinMain(CostumeEditDoc *pDoc)
{
	UIGimmeButton *pGimmeButton;
	UIWindow *pWin;
	UILabel *pLabel;
	MEField *pField;
	F32 y = 0;
	
	// Create the window
	pWin = ui_WindowCreate(pDoc->pCostume->pcName, 15, 50, 350, 95);

	// Costume Name
	pLabel = ui_LabelCreate("Costume Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(pWin), 100, y);
	pField->pUIText->widget.widthUnit = UIUnitPercentage;
	pField->pUIText->widget.width = 1.f;
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UISetName, pDoc);
	pDoc->pNameField = pField;
	y += 28;

	// Scope
	pLabel = ui_LabelCreate("Scope", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Scope", NULL, &geaScopes, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(pWin), 100, y);
	pField->pUIText->widget.widthUnit = UIUnitPercentage;
	pField->pUIText->widget.width = 1.f;
	ui_WidgetSetPaddingEx(UI_WIDGET(pField->pUIText), 0, 20, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UISetScope, pDoc);
	pDoc->pScopeField = pField;
	y += 28;

	// File Name
	pLabel = ui_LabelCreate("File Name", 0, y);
	ui_WindowAddChild(pWin, pLabel);
	pLabel = ui_LabelCreate(pDoc->pCostume->pcFileName, 100, y);
	ui_WindowAddChild(pWin, pLabel);
	pField->pUIText->widget.widthUnit = UIUnitPercentage;
	pField->pUIText->widget.width = 1.f;
	ui_WidgetSetPaddingEx(UI_WIDGET(pLabel), 0, 20, 0, 0);
	pDoc->pFilenameLabel = pLabel;

	pGimmeButton = ui_GimmeButtonCreate(2, y+2, "PlayerCostume", pDoc->pCostume->pcName, pDoc->pCostume);
	ui_WidgetSetPositionEx(UI_WIDGET(pGimmeButton), 2, y+2, 0, 0, UITopRight);
	ui_WindowAddChild(pWin, pGimmeButton);
	pDoc->pGimmeButton = pGimmeButton;

	y += 28;

	ui_WidgetSetHeight(UI_WIDGET(pWin), y);

	return pWin;
}


UIWindow *costumeEdit_InitWinDetail(CostumeEditDoc *pDoc)
{
	UIWindow *pWin;
	UIListColumn *pCol;
	
	// Create the window
	pWin = ui_WindowCreate("Costume Part Details", 15, 400, 450, 325);

	// Bone List
	pDoc->pBoneList = ui_ListCreate(NULL, &pDoc->pCostume->eaParts, 18 );
	pDoc->pBoneList->bDrawGrid = true;
	pDoc->pBoneList->bUseBackgroundColor = true;
	pDoc->pBoneList->backgroundColor = ColorWhite;

	ui_WidgetSetDimensionsEx(UI_WIDGET(pDoc->pBoneList), 1, 1, UIUnitPercentage, UIUnitPercentage);
	ui_ListSetSelectedCallback(pDoc->pBoneList, costumeEdit_UIBoneSelectedList, pDoc);

	pCol = ui_ListColumnCreateCallback("Bone Name", costumeEdit_UIDisplayBoneName, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Bone Name", 100);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	pCol = ui_ListColumnCreateCallback("Geometry", costumeEdit_UIDisplayBoneGeo, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Geometry", 160);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	pCol = ui_ListColumnCreateCallback("Material", costumeEdit_UIDisplayBoneMat, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Material", 200);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	pCol = ui_ListColumnCreateCallback("Pattern", costumeEdit_UIDisplayBonePattern, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Pattern", 200);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	pCol = ui_ListColumnCreateCallback("Detail", costumeEdit_UIDisplayBoneDetail, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Detail", 200);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	pCol = ui_ListColumnCreateCallback("Specular", costumeEdit_UIDisplayBoneSpecular, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Specular", 200);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	pCol = ui_ListColumnCreateCallback("Diffuse", costumeEdit_UIDisplayBoneDiffuse, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Diffuse", 200);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	pCol = ui_ListColumnCreateCallback("Movable", costumeEdit_UIDisplayBoneMovable, pDoc);
	pCol->fWidth = EditorPrefGetInt(COSTUME_EDITOR, "Bone Column", "Movable", 200);
	ui_ListAppendColumn(pDoc->pBoneList, pCol);

	ui_WindowAddChild(pWin, pDoc->pBoneList);

	pWin->show = false; // Make editor manager default to not showing this

	return pWin;
}


EMPanel *costumeEdit_InitPanelCostume(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;
	UILabel *pLabel;
	MEField *pField;
	F32 x = 15, y = 0;

	// Create the panel
	pPanel = emPanelCreate("Costume", "Costume", 0);

	// Costume Type
	pLabel = ui_LabelCreate("Type", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "CostumeType", PCCostumeTypeEnum);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UICostumeTypeChange, pDoc);
	pDoc->pCostumeTypeField = pField;
	y += 28;

	// Skeleton
	pLabel = ui_LabelCreate("Skeleton", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Skeleton", parse_PCSkeletonDef, &pDoc->eaSkels, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UISkeletonChange, pDoc);
	pDoc->pSkeletonField = pField;
	y += 28;

	// Species
	pLabel = ui_LabelCreate("Species", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Species", parse_SpeciesDef, &pDoc->eaSpecies, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UISpeciesChange, pDoc);
	pDoc->pSpeciesField = pField;
	y += 28;

	// Species
	pLabel = ui_LabelCreate("Voice", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_ValidatedTextEntry, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "Voice", parse_PCVoice, &pDoc->eaVoices, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UIVoiceChange, pDoc);
	pDoc->pVoiceField = pField;
	y += 28;

	// Color Link Default
	pLabel = ui_LabelCreate("Default Color Link All", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "DefaultColorLinkAll");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+150, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UILinkAllChange, pDoc);
	pDoc->pColorLinkAllField = pField;
	y += 28;

	// Material Link Default
	pLabel = ui_LabelCreate("Default Mat Link All", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "DefaultMaterialLinkAll");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+150, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	MEFieldSetChangeCallback(pField, costumeEdit_UILinkAllChange, pDoc);
	pDoc->pMatLinkAllField = pField;
	y += 28;

	emPanelSetHeight(pPanel, y - (pDoc->pCostume->eCostumeType != kPCCostumeType_Player ? (28*2) : 0));

	pDoc->pMainSettingsPanel = pPanel;

	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelBodySettings(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;

	pPanel = emPanelCreate("Costume", "Body Settings", 28);

	pDoc->pBodySettingsPanel = pPanel;

	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelPart(CostumeEditDoc *pDoc)
{
	NOCONST(PCPart) *pOrigChild = NULL;
	NOCONST(PCPart) *pChildPart = NULL;
	NOCONST(PCPart) **eaChildParts = NULL;
	EMPanel *pPanel;
	UILabel *pLabel;
	UIMenuButton *pMenuButton;
	UIComboBox *pCombo;
	MEField *pField;
	UICheckButton *pCheck;
	F32 x = 15, y = 0;

	// Create the panel
	pPanel = emPanelCreate("Costume", "Costume Part", 0);
	pDoc->pCostumePartPanel = pPanel;

	// Initialize current part
	pDoc->pCurrentPart = NULL;
	if (eaSize(&pDoc->pCostume->eaParts)) {
		pDoc->pCurrentPart = pDoc->pCostume->eaParts[0];
	}

	// Region choice
	pLabel = ui_LabelCreate("Region", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pCombo = ui_ComboBoxCreate(x+75, y, 120, parse_PCRegion, &pDoc->eaRegions, "Name");
	ui_ComboBoxSetSelectedCallback(pCombo, costumeEdit_UIRegionSelectedCombo, pDoc);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pCombo), 0, 21, 0, 0);
	ui_WidgetSetName(UI_WIDGET(pCombo), "CostumeEdit = Region");
    emPanelAddChild(pPanel, pCombo, false);
	pDoc->pRegionCombo = pCombo;
	y += 28;

	// Category choice
	pLabel = ui_LabelCreate("Category", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pCombo = ui_ComboBoxCreate(x+75+15, y, 120, parse_PCCategory, &pDoc->eaCategories, "Name");
	ui_ComboBoxSetSelectedCallback(pCombo, costumeEdit_UICategorySelectedCombo, pDoc);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pCombo), 0, 21, 0, 0);
	ui_WidgetSetName(UI_WIDGET(pCombo), "CostumeEdit = Category");
    emPanelAddChild(pPanel, pCombo, false);
	pDoc->pCategoryCombo = pCombo;
	y += 28;

	y += 10; // Provide visual space

	// Bone choice
	pLabel = ui_LabelCreate("Bone", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pCombo = ui_ComboBoxCreate(x+75, y, 120, parse_PCBoneDef, &pDoc->eaBones, "Name");
	ui_ComboBoxSetSelectedCallback(pCombo, costumeEdit_UIBoneSelectedCombo, pDoc);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pCombo), 0, 21, 0, 0);
	ui_WidgetSetName(UI_WIDGET(pCombo), "CostumeEdit = Bone");
    emPanelAddChild(pPanel, pCombo, false);
	pDoc->pBoneCombo = pCombo;
	y += 28;

	// Geo choice
	pDoc->pGeometryTextLabel = ui_LabelCreate("Geometry", x, y);
	emPanelAddChild(pPanel, pDoc->pGeometryTextLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "Geometry", parse_PCGeometryDef, &pDoc->eaGeos, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverGeo, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UIGeoChanged, pDoc);

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy Part",UIMenuCallback, costumeEdit_UIMenuSelectPartCopy, pDoc, NULL),
		ui_MenuItemCreate("Paste Part",UIMenuCallback, costumeEdit_UIMenuSelectPartPaste, pDoc, NULL),
		ui_MenuItemCreate("Revert Part",UIMenuCallback, costumeEdit_UIMenuSelectPartRevert, pDoc, NULL),
		ui_MenuItemCreate("Edit Definition",UIMenuCallback, costumeEdit_UIMenuSelectPartEditDef, pDoc, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenGeo, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);
	pDoc->pGeoField = pField;
	y += 28;

	// Child Geo choice
	pLabel = ui_LabelCreate("Child Geo", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	costumeTailor_GetChildParts(pDoc->pOrigCostume, pDoc->pOrigPart, NULL, &eaChildParts);
	if (eaSize(&eaChildParts)) {
		pOrigChild = eaChildParts[0];
		eaDestroy(&eaChildParts);
	}
	costumeTailor_GetChildParts(pDoc->pCostume, pDoc->pCurrentPart, NULL, &eaChildParts);
	if (eaSize(&eaChildParts)) {
		pChildPart = eaChildParts[0];
		eaDestroy(&eaChildParts);
	}
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pOrigChild, pChildPart ? pChildPart : gEmptyPart, parse_PCPart, "Geometry", parse_PCGeometryDef, &pDoc->eaChildGeos, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverChildGeo, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UIGeoChanged, pDoc);

	pDoc->pChildGeoField = pField;
	y += 28;

	// Layer choice
	pLabel = ui_LabelCreate("Layer", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pCombo = ui_ComboBoxCreate(x+75+15, y, 120, parse_PCLayer, &pDoc->eaLayers, "Name");
	ui_ComboBoxSetSelectedCallback(pCombo, costumeEdit_UILayerSelectedCombo, pDoc);
	ui_WidgetSetWidthEx(UI_WIDGET(pCombo), 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(UI_WIDGET(pCombo), 0, 21, 0, 0);
	ui_WidgetSetName(UI_WIDGET(pCombo), "CostumeEdit = Layer");
    emPanelAddChild(pPanel, pCombo, false);
	ui_SetActive(UI_WIDGET(pCombo), false);
	pDoc->pLayerCombo = pCombo;
	y += 28;

	// Geo Linking
	pCheck = ui_CheckButtonCreate(x+5 , y, "Link Mirror Geometry", 0);
	emPanelAddChild(pPanel, pCheck, false);
	ui_SetActive(UI_WIDGET(pCheck), false);
	pDoc->pGeoLinkButton = pCheck;
	y += 28;

	y += 10; // Provide visual space

	// Texture Label
	//pLabel = ui_LabelCreate("Texture Choices:", x, y);
	//emPanelAddChild(pWin, pLabel);
	//y += 25;

	// Material choice
	pDoc->pMaterialTextLabel = ui_LabelCreate("Material", x, y);
	emPanelAddChild(pPanel, pDoc->pMaterialTextLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "Material", parse_PCMaterialDef, &pDoc->eaMats, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverMat, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UIMaterialChanged, pDoc);
	pDoc->pMatField = pField;
	y += 28;

	// Material Linking Control
	pLabel = ui_LabelCreate("Link Mat To", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigPart ? &pDoc->origMaterialLink : NULL, &pDoc->currentMaterialLink, parse_CostumeEditMaterialLink, "MaterialLink", PCColorLinkEnum);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	ui_WidgetSetWidth(pField->pUIWidget, 150);
	MEFieldSetChangeCallback(pField, costumeEdit_UIMaterialLinkChanged, pDoc);
	pDoc->pMaterialLinkField = pField;
	y += 28;

	// Color Pattern Choice
	pDoc->pPatternTextLabel = ui_LabelCreate("Pattern", x, y);
	emPanelAddChild(pPanel, pDoc->pPatternTextLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "PatternTexture", parse_PCTextureDef, &pDoc->eaPatternTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverPatternTex, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UITexFieldChanged, pDoc);
	pDoc->pPatternField = pField;
	y += 28;

	//pLabel = ui_LabelCreate("Value", x, y);
	//ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
	//emPanelAddChild(pPanel, pLabel, false);
	//pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "fPatternValue");
	//MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	//ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	//ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	//ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
	//MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	//pDoc->pPatternValueLabel = pLabel;
	//pDoc->pPatternValueField = pField;
	//y += 28;

	// Detail Choice
	pDoc->pDetailTextLabel = ui_LabelCreate("Detail", x, y);
	emPanelAddChild(pPanel, pDoc->pDetailTextLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "DetailTexture", parse_PCTextureDef, &pDoc->eaDetailTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverDetailTex, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UITexFieldChanged, pDoc);
	pDoc->pDetailField = pField;
	y += 28;

	//pLabel = ui_LabelCreate("Value", x, y);
	//ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
	//emPanelAddChild(pPanel, pLabel, false);
	//pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "fDetailValue");
	//MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	//ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	//ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	//ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
	//MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	//pDoc->pDetailValueLabel = pLabel;
	//pDoc->pDetailValueField = pField;
	//y += 28;

	// Specular Choice
	pDoc->pSpecularTextLabel = ui_LabelCreate("Specular", x, y);
	emPanelAddChild(pPanel, pDoc->pSpecularTextLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "SpecularTexture", parse_PCTextureDef, &pDoc->eaSpecularTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverSpecularTex, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UITexFieldChanged, pDoc);
	pDoc->pSpecularField = pField;
	y += 28;

	//pLabel = ui_LabelCreate("Value", x, y);
	//ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
	//emPanelAddChild(pPanel, pLabel, false);
	//pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "fSpecularValue");
	//MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	//ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	//ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	//ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
	//MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	//pDoc->pSpecularValueLabel = pLabel;
	//pDoc->pSpecularValueField = pField;
	//y += 28;

	// Diffuse Choice
	pDoc->pDiffuseTextLabel = ui_LabelCreate("Diffuse", x, y);
	emPanelAddChild(pPanel, pDoc->pDiffuseTextLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "DiffuseTexture", parse_PCTextureDef, &pDoc->eaDiffuseTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverDiffuseTex, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UITexFieldChanged, pDoc);
	pDoc->pDiffuseField = pField;
	y += 28;

	//pLabel = ui_LabelCreate("Value", x, y);
	//ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
	//emPanelAddChild(pPanel, pLabel, false);
	//pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "fDiffuseValue");
	//MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	//ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	//ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	//ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
	//MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	//pDoc->pDiffuseValueLabel = pLabel;
	//pDoc->pDiffuseValueField = pField;
	//y += 28;

	// Movable Choice
	pDoc->pMovableTextLabel = ui_LabelCreate("Movable", x, y);
	emPanelAddChild(pPanel, pDoc->pMovableTextLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_Combo, pDoc->pOrigPart ? pDoc->pOrigPart->pMovableTexture : NULL, pDoc->pCurrentPart ? pDoc->pCurrentPart->pMovableTexture : gEmptyPart->pMovableTexture, parse_PCMovableTextureInfo, "MovableTexture", parse_PCTextureDef, &pDoc->eaMovableTex, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	ui_ComboBoxSetHoverCallback(pField->pUICombo, costumeEdit_UIHoverMovableTex, pDoc);
	MEFieldSetChangeCallback(pField, costumeEdit_UITexFieldChanged, pDoc);
	pDoc->pMovableField = pField;
	y += 28;

	//pLabel = ui_LabelCreate("Value", x, y);
	//ui_WidgetSetPosition(UI_WIDGET(pLabel), x, y);
	//emPanelAddChild(pPanel, pLabel, false);
	//pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart, pDoc->pCurrentPart ? pDoc->pCurrentPart : gEmptyPart, parse_PCPart, "fMovableValue");
	//MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	//ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	//ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	//ui_SliderTextEntrySetRange(pField->pUISliderText, -100, 100, 1);
	//MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	//pDoc->pMovableValueLabel = pLabel;
	//pDoc->pMovableValueField = pField;
	//y += 28;

	emPanelSetHeight(pPanel, y);

	emPanelSetOpened(pPanel, true);

	return pPanel;
}

extern ParseTable parse_ItemDef[];
#define TYPE_parse_ItemDef ItemDef

EMPanel *costumeEdit_InitPanelColor(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;
	UILabel *pLabel;
	UIColorButton *pColorButton;
	UIMenuButton *pMenuButton;
	UICheckButton *pCheck;
	UIButton *pButton;
	MEField *pField;
	CEMenuData *pData;
	F32 x = 15, y = 0;
	Vec4 colorTemp = { 255, 255, 255, 255 };

	// Create the panel
	pPanel = emPanelCreate("Costume", "Part Colors", 0);

	pLabel = ui_LabelCreate("Dye Pack", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pCurrentDyePack, pDoc->pCurrentDyePack, parse_CostumeEditDyePackStruct, "DyePack", parse_CostumeEditDyePackStruct, &s_eaDyePacks, "DyePack");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75+15, y);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	pDoc->pDyePackField = pField;
	y += 28;

	pButton = ui_ButtonCreate("Apply Dye Pack", x, y, costumeEdit_UIApplyDyePack, pDoc);
	emPanelAddChild(pPanel, pButton, false);
	y += 28;

	// Color Labels
	pLabel = ui_LabelCreate("Color Choices:", x, y);
	emPanelAddChild(pPanel, pLabel, false);

	pLabel = ui_LabelCreate("Glow:", x+155, y);
	emPanelAddChild(pPanel, pLabel, false);

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectColorSetCopy, pDoc, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectColorSetCopyToMirror, pDoc, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectColorSetCopyToAll, pDoc, NULL),
		ui_MenuItemCreate("Paste Color Set",UIMenuCallback, costumeEdit_UIMenuSelectColorSetPaste, pDoc, NULL),
		ui_MenuItemCreate("Revert Color Set",UIMenuCallback, costumeEdit_UIMenuSelectColorSetRevert, pDoc, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenColorSet, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);
	y += 25;

	// Color 0
	pLabel = ui_LabelCreate("Color 0", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pColorButton = ui_ColorButtonCreate(x+75, y, colorTemp);
	pColorButton->liveUpdate = true;
	ui_ColorButtonSetChangedCallback(pColorButton, costumeEdit_UIColorChanged, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pColorButton), 70);
	emPanelAddChild(pPanel, pColorButton, false);

	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigPart ? &pDoc->origGlow0 : NULL, &pDoc->currentGlow0, parse_CostumeEditGlowStruct, "Glow", CostumeEditGlowEnum);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+155, y);
	ui_WidgetSetWidth(pField->pUIWidget, 70);
	pDoc->currentGlow0.index = 0;
	pDoc->currentGlow0.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIGlowChanged, &pDoc->currentGlow0);
	pDoc->pGlow0Field = pField;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "color0");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectColorCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Color",UIMenuCallback, costumeEdit_UIMenuSelectColorPaste, pData, NULL),
		ui_MenuItemCreate("Revert Color",UIMenuCallback, costumeEdit_UIMenuSelectColorRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenColor, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);
	pDoc->pCButton0 = pColorButton;

	y += 28;

	// Color 1
	pLabel = ui_LabelCreate("Color 1", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pColorButton = ui_ColorButtonCreate(x+75, y, colorTemp);
	pColorButton->liveUpdate = true;
	ui_ColorButtonSetChangedCallback(pColorButton, costumeEdit_UIColorChanged, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pColorButton), 70);
	emPanelAddChild(pPanel, pColorButton, false);

	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigPart ? &pDoc->origGlow1 : NULL, &pDoc->currentGlow1, parse_CostumeEditGlowStruct, "Glow", CostumeEditGlowEnum);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+155, y);
	ui_WidgetSetWidth(pField->pUIWidget, 70);
	pDoc->currentGlow1.index = 1;
	pDoc->currentGlow1.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIGlowChanged, &pDoc->currentGlow1);
	pDoc->pGlow1Field = pField;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "color1");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectColorCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Color",UIMenuCallback, costumeEdit_UIMenuSelectColorPaste, pData, NULL),
		ui_MenuItemCreate("Revert Color",UIMenuCallback, costumeEdit_UIMenuSelectColorRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenColor, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);
	pDoc->pCButton1 = pColorButton;
	pDoc->pColor1Label = pLabel;

	y += 28;

	// Color 2
	pLabel = ui_LabelCreate("Color 2", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pColorButton = ui_ColorButtonCreate(x+75, y, colorTemp);
	pColorButton->liveUpdate = true;
	ui_ColorButtonSetChangedCallback(pColorButton, costumeEdit_UIColorChanged, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pColorButton), 70);
	emPanelAddChild(pPanel, pColorButton, false);

	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigPart ? &pDoc->origGlow2 : NULL, &pDoc->currentGlow2, parse_CostumeEditGlowStruct, "Glow", CostumeEditGlowEnum);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+155, y);
	ui_WidgetSetWidth(pField->pUIWidget, 70);
	pDoc->currentGlow2.index = 2;
	pDoc->currentGlow2.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIGlowChanged, &pDoc->currentGlow2);
	pDoc->pGlow2Field = pField;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "color2");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectColorCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Color",UIMenuCallback, costumeEdit_UIMenuSelectColorPaste, pData, NULL),
		ui_MenuItemCreate("Revert Color",UIMenuCallback, costumeEdit_UIMenuSelectColorRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenColor, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);
	pDoc->pCButton2 = pColorButton;
	pDoc->pColor2Label = pLabel;

	y += 28;

	// Color 3
	pLabel = ui_LabelCreate("Color 3", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pColorButton = ui_ColorButtonCreate(x+75, y, colorTemp);
	pColorButton->liveUpdate = true;
	ui_ColorButtonSetChangedCallback(pColorButton, costumeEdit_UIColorChanged, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pColorButton), 70);
	emPanelAddChild(pPanel, pColorButton, false);

	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigPart ? &pDoc->origGlow3 : NULL, &pDoc->currentGlow3, parse_CostumeEditGlowStruct, "Glow", CostumeEditGlowEnum);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+155, y);
	ui_WidgetSetWidth(pField->pUIWidget, 70);
	pDoc->currentGlow3.index = 3;
	pDoc->currentGlow3.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIGlowChanged, &pDoc->currentGlow3);
	pDoc->pGlow3Field = pField;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "color3");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectColorCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectColorCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Color",UIMenuCallback, costumeEdit_UIMenuSelectColorPaste, pData, NULL),
		ui_MenuItemCreate("Revert Color",UIMenuCallback, costumeEdit_UIMenuSelectColorRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenColor, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);
	pDoc->pCButton3 = pColorButton;
	pDoc->pColor3Label = pLabel;

	y += 28;

	// Color Linking Control
	pLabel = ui_LabelCreate("Link To", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleEnum(kMEFieldType_Combo, pDoc->pOrigPart ? &pDoc->origColorLink : NULL, &pDoc->currentColorLink, parse_CostumeEditColorLink, "ColorLink", PCColorLinkEnum);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+75, y);
	ui_WidgetSetWidth(pField->pUIWidget, 150);
	MEFieldSetChangeCallback(pField, costumeEdit_UIColorLinkChanged, pDoc);
	pDoc->pColorLinkField = pField;
	y += 28;

	// Artist glow override
	pCheck = ui_CheckButtonCreate(x+5 , y, "Artist Override Enabled", 0);
	ui_CheckButtonSetToggledCallback(pCheck, costumeEdit_UIGlowOverrideChanged, pDoc);
	emPanelAddChild(pPanel, pCheck, false);
	y += 28;

	emPanelSetHeight(pPanel, y);

	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelReflection(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;
	UILabel *pLabel;
	UICheckButton *pCheck;
	UIMenuButton *pMenuButton;
	MEField *pField;
	CEMenuData *pData;
	F32 x = 15, y = 0;

	// Create the panel
	pPanel = emPanelCreate("Costume", "Part Reflection & Specularity", 0);

	// Color Labels
	pLabel = ui_LabelCreate("Reflection:", x+20, y);
	emPanelAddChild(pPanel, pLabel, false);

	pLabel = ui_LabelCreate("Specularity:", x+125, y);
	emPanelAddChild(pPanel, pLabel, false);

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectReflectSetCopy, pDoc, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectReflectSetCopyToMirror, pDoc, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectReflectSetCopyToAll, pDoc, NULL),
		ui_MenuItemCreate("Paste Reflect/Specular Set",UIMenuCallback, costumeEdit_UIMenuSelectReflectSetPaste, pDoc, NULL),
		ui_MenuItemCreate("Revert Reflect/Specular Set",UIMenuCallback, costumeEdit_UIMenuSelectReflectSetRevert, pDoc, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenReflectSet, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);

	y += 25;

	// Customize
	pField = MEFieldCreateSimple(kMEFieldType_Check, pDoc->pOrigPart ? pDoc->pOrigPart->pCustomColors : NULL, pDoc->pCurrentPart ? pDoc->pCurrentPart->pCustomColors : gEmptyPart->pCustomColors, parse_PCCustomColorInfo, "CustomReflection");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+20, y);
	ui_CheckButtonSetText(pField->pUICheck, "Customize");
	MEFieldSetChangeCallback(pField, costumeEdit_UICustomReflectionChanged, pDoc);
	pDoc->pCustomizeReflectField = pField;

	pField = MEFieldCreateSimple(kMEFieldType_Check, pDoc->pOrigPart ? pDoc->pOrigPart->pCustomColors : NULL, pDoc->pCurrentPart ? pDoc->pCurrentPart->pCustomColors : gEmptyPart->pCustomColors, parse_PCCustomColorInfo, "CustomSpecularity");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
	ui_CheckButtonSetText(pField->pUICheck, "Customize");
	MEFieldSetChangeCallback(pField, costumeEdit_UICustomSpecularChanged, pDoc);
	pDoc->pCustomizeSpecularField = pField;

	y += 28;

	// Color 0
	pLabel = ui_LabelCreate("0", x, y);
	emPanelAddChild(pPanel, pLabel, false);

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect3 : NULL, &pDoc->currentReflect3, parse_CostumeEditReflectStruct, "Reflection");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+20, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	pDoc->currentReflect3.index = 3;
	pDoc->currentReflect3.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect3);
	pDoc->pReflect3Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect3 : NULL, &pDoc->currentReflect3, parse_CostumeEditReflectStruct, "Specularity");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect3);
	pDoc->pSpecular3Field = pField;

	pLabel = ui_LabelCreate("Not Available", x+20, y);
	pDoc->pReflectLabel = pLabel;

	pLabel = ui_LabelCreate("Not Available", x+125, y);
	pDoc->pSpecularLabel = pLabel;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "reflect3");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectPaste, pData, NULL),
		ui_MenuItemCreate("Revert Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenReflect, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);

	y += 28;

	// Color 1
	pLabel = ui_LabelCreate("1", x, y);
	emPanelAddChild(pPanel, pLabel, false);

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect0 : NULL, &pDoc->currentReflect0, parse_CostumeEditReflectStruct, "Reflection");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+20, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	pDoc->currentReflect0.index = 0;
	pDoc->currentReflect0.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect0);
	pDoc->pReflect0Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect0 : NULL, &pDoc->currentReflect0, parse_CostumeEditReflectStruct, "Specularity");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect0);
	pDoc->pSpecular0Field = pField;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "reflect0");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectPaste, pData, NULL),
		ui_MenuItemCreate("Revert Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenReflect, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);

	y += 28;

	// Color 2
	pLabel = ui_LabelCreate("2", x, y);
	emPanelAddChild(pPanel, pLabel, false);

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect1 : NULL, &pDoc->currentReflect1, parse_CostumeEditReflectStruct, "Reflection");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+20, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	pDoc->currentReflect1.index = 1;
	pDoc->currentReflect1.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect1);
	pDoc->pReflect1Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect1 : NULL, &pDoc->currentReflect1, parse_CostumeEditReflectStruct, "Specularity");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect1);
	pDoc->pSpecular1Field = pField;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "reflect1");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectPaste, pData, NULL),
		ui_MenuItemCreate("Revert Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenReflect, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);

	y += 28;

	// Color 3
	pLabel = ui_LabelCreate("3", x, y);
	emPanelAddChild(pPanel, pLabel, false);

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect2 : NULL, &pDoc->currentReflect2, parse_CostumeEditReflectStruct, "Reflection");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+20, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	pDoc->currentReflect2.index = 2;
	pDoc->currentReflect2.pDoc = pDoc;
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect2);
	pDoc->pReflect2Field = pField;

	pField = MEFieldCreateSimple(kMEFieldType_SliderText, pDoc->pOrigPart ? &pDoc->origReflect2 : NULL, &pDoc->currentReflect2, parse_CostumeEditReflectStruct, "Specularity");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+125, y);
	ui_SliderTextEntrySetRange(pField->pUISliderText, 0, 100, 1);
	ui_WidgetSetWidth(pField->pUIWidget, 100);
	MEFieldSetChangeCallback(pField, costumeEdit_UIReflectChanged, &pDoc->currentReflect2);
	pDoc->pSpecular2Field = pField;

	pMenuButton = ui_MenuButtonCreate(2, y+2);
	ui_WidgetSetPositionEx(UI_WIDGET(pMenuButton), 2, y+2, 0, 0, UITopRight);
	pData = costumeEdit_CreateMenuData(pDoc, "reflect2");
	ui_MenuButtonAppendItems(pMenuButton,
		ui_MenuItemCreate("Copy to Clipboard",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopy, pData, NULL),
		ui_MenuItemCreate("Copy to Mirror",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToMirror, pData, NULL),
		ui_MenuItemCreate("Copy to All",UIMenuCallback, costumeEdit_UIMenuSelectReflectCopyToAll, pData, NULL),
		ui_MenuItemCreate("Paste Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectPaste, pData, NULL),
		ui_MenuItemCreate("Revert Reflect/Specular",UIMenuCallback, costumeEdit_UIMenuSelectReflectRevert, pData, NULL),
		NULL);
	ui_MenuButtonSetPreopenCallback(pMenuButton, costumeEdit_UIMenuPreopenReflect, pDoc);
	emPanelAddChild(pPanel, pMenuButton, false);

	y += 28;

	// Artist reflection override
	pCheck = ui_CheckButtonCreate(x+5 , y, "Artist Override Enabled", 0);
	ui_CheckButtonSetToggledCallback(pCheck, costumeEdit_UIReflectOverrideChanged, pDoc);
	emPanelAddChild(pPanel, pCheck, false);
	y += 28;

	emPanelSetHeight(pPanel, y);

	emPanelSetOpened(pPanel, false);
	pDoc->pReflectPanel = pPanel;

	return pPanel;
}


EMPanel *costumeEdit_InitPanelArtistColor(CostumeEditDoc *pDoc)
{
	MEField *pField;
	EMPanel *pPanel;

	pPanel = emPanelCreate("Costume", "Artist Extended Settings", 28);

	pDoc->pArtistColorsPanel = pPanel;

	pField = MEFieldCreateSimple(kMEFieldType_Check, pDoc->pOrigPart ? pDoc->pOrigPart->pArtistData : NULL, pDoc->pCurrentPart ? pDoc->pCurrentPart->pArtistData : gEmptyPart->pArtistData, parse_PCArtistPartData, "NoShadow");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 0, 0);
	ui_CheckButtonSetText(pField->pUICheck, "Disable Shadow for Part");
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	pDoc->pNoShadowField = pField;

	emPanelSetOpened(pPanel, false);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelArtistTexture(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;

	pPanel = emPanelCreate("Costume", "Artist Extended Textures", 0);
	pDoc->pArtistTexPanel = pPanel;

	emPanelSetOpened(pPanel, false);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelAnimation(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;
	UILabel *pLabel;

	pPanel = emPanelCreate("Artist", "Animation", 28);

	pLabel = ui_LabelCreate("Animation",15,0);
	emPanelAddChild(pPanel, pLabel, false);
	pDoc->pAnimCombo = ui_ComboBoxCreate(pLabel->widget.width+20, 0, 1.f, parse_CostumeAnim, &eaCostumeAnims, "Name");
	ui_ComboBoxSetSelectedCallback(pDoc->pAnimCombo, costumeEdit_UIAnimChange, pDoc);
	ui_WidgetSetWidthEx(UI_WIDGET(pDoc->pAnimCombo), 1, UIUnitPercentage);
	ui_WidgetSetName(UI_WIDGET(pDoc->pAnimCombo), "CostumeEdit = Animation");
	ui_ComboBoxSetSelectedAndCallback(pDoc->pAnimCombo, 0);
	emPanelAddChild(pPanel, pDoc->pAnimCombo, false);

	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelSettings(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;
	UILabel *pLabel;
	MEField *pField;
	int y = 0;
	int x = 15;

	pPanel = emPanelCreate("Artist", "Settings", 28);

	// Account Unlock
	pLabel = ui_LabelCreate("Unlocks on Account", x, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigCostume, pDoc->pCostume, parse_PlayerCostume, "AccountUnlock");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), x+120, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1, UIUnitPercentage);
	ui_WidgetSetPaddingEx(pField->pUIWidget, 0, 21, 0, 0);
	pDoc->pAccountUnlockField = pField;
	y += 28;

	emPanelSetHeight(pPanel, y);
	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelBits(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;

	pPanel = emPanelCreate("Artist", "Control Bits", 28);

	pDoc->pBitsPanel = pPanel;

	emPanelSetOpened(pPanel, true);

	return pPanel;
}


static FileScanAction costumeEdit_GeoFileScanAction(char *dir, struct _finddata32_t *data, void *pUserData)
{
	static char *ext = ".ModelHeader";
	static int ext_len = 12; // strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR) {
		return FSA_EXPLORE_DIRECTORY;
	}
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0) {
		return FSA_EXPLORE_DIRECTORY;
	}

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// Clip off the "ModelHeader" ending
	filename[strlen(filename)-ext_len] = '\0';

	// Store the geometry file name
	eaPush(&g_eaGeoFileNames,allocAddFilename(filename));

	return FSA_EXPLORE_DIRECTORY;
}

static void costumeEdit_GeoFileChangeCallback(const char *relpath, int when)
{
	CostumeEditDoc **eaDocs = NULL;

	// When a ModelHeader file changes, reload the list
	eaClear(&g_eaGeoFileNames);
	fileScanAllDataDirs("character_library", costumeEdit_GeoFileScanAction, NULL);

	//// Have to regenerate the model pull-down list
	//if (gDefDoc && gDefDoc->pCurrentGeoDef && gDefDoc->pCurrentGeoDef->pcGeometry) {
	//	costumeDefEdit_GetValidModels(gDefDoc->pCurrentGeoDef->pcGeometry, &gDefDoc->eaModelNames);
	//	costumeDefEdit_DefUpdateLists(gDefDoc);
	//}
}

static FileScanAction costumeEdit_PoseFileScanAction(char *dir, struct _finddata32_t *data, void *pUserData)
{
	static char *ext = ".atrk";
	static int ext_len = 5; // strlen(ext);
	char filename[MAX_PATH];
	int len;

	if (data->name[0]=='_') return FSA_NO_EXPLORE_DIRECTORY;
	if (data->attrib & _A_SUBDIR) {
		return FSA_EXPLORE_DIRECTORY;
	}
	len = (int)strlen(data->name);
	if (len<ext_len || strnicmp(data->name + len - ext_len, ext, ext_len)!=0) {
		return FSA_EXPLORE_DIRECTORY;
	}

	STR_COMBINE_SSS(filename, dir, "/", data->name);

	// Clip off the "ModelHeader" ending
	filename[strlen(filename)-ext_len] = '\0';

	// Store the geometry file name
	eaPush(&g_eaPoseFileNames,allocAddFilename(filename));

	return FSA_EXPLORE_DIRECTORY;
}

static void costumeEdit_PoseFileChangeCallback(const char *relpath, int when)
{
	CostumeEditDoc **eaDocs = NULL;

	// When a ModelHeader file changes, reload the list
	eaClear(&g_eaPoseFileNames);
	fileScanAllDataDirs("animation_library", costumeEdit_PoseFileScanAction, NULL);

	//// Have to regenerate the model pull-down list
	//if (gDefDoc && gDefDoc->pCurrentGeoDef && gDefDoc->pCurrentGeoDef->pcGeometry) {
	//	costumeDefEdit_GetValidModels(gDefDoc->pCurrentGeoDef->pcGeometry, &gDefDoc->eaModelNames);
	//	costumeDefEdit_DefUpdateLists(gDefDoc);
	//}
}

EMPanel *costumeEdit_InitPanelCollision(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;
	UILabel *pLabel;
	MEField *pField;
	int y = 0;

	pPanel = emPanelCreate("Artist", "Collision and LOD", 362);

	// Must have artist data by this point or badness follows
	assert(pDoc->pCostume->pArtistData); 
	assert(!pDoc->pOrigCostume || pDoc->pOrigCostume->pArtistData);

	// Disable body sock
	pLabel = ui_LabelCreate("Disable Body Sock", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL, pDoc->pCostume->pArtistData, parse_PCArtistCostumeData, "NoBodySock");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDoc->pBodySockDisableField = pField;
	y += 28;

	// Disable collision
	pLabel = ui_LabelCreate("Disable Collision", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL, pDoc->pCostume->pArtistData, parse_PCArtistCostumeData, "NoCollision");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDoc->pCollisionDisableField = pField;
	y += 28;

	// Collision info
	pLabel = ui_LabelCreate("Collision Geometry", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDictionary(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL, pDoc->pCostume->pArtistData, parse_PCArtistCostumeData, "CollisionGeo", "CostumeGeometry", parse_PCGeometryDef, "Name");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pCollisionGeoField = pField;
	y += 28;

	// Shell geometry
	pLabel = ui_LabelCreate("Shell Collision", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL, pDoc->pCostume->pArtistData, parse_PCArtistCostumeData, "ShellColl");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDoc->pShellDisableField = pField;
	y += 28;

	// Shell geometry
	pLabel = ui_LabelCreate("Disable Ragdoll", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_BooleanCombo, pDoc->pOrigCostume ? pDoc->pOrigCostume->pArtistData : NULL, pDoc->pCostume->pArtistData, parse_PCArtistCostumeData, "NoRagdoll");
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidth(pField->pUIWidget, 80);
	pDoc->pRagDollDisableField = pField;
	y += 28;

	// Body Sock Geo
	pLabel = ui_LabelCreate("Body Sock Geo", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockGeo", NULL, &g_eaGeoFileNames, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockGeoField = pField;
	y += 28;

	// Body Sock Pose
	pLabel = ui_LabelCreate("Body Sock Pose", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimpleDataProvided(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockPose", NULL, &g_eaPoseFileNames, NULL);
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockPoseField = pField;
	y += 28;

	// Body Sock Min
	pLabel = ui_LabelCreate("Body Sock Min X", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockMin");
	pField->arrayIndex = 0;
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockMinXField = pField;
	y += 28;
	pLabel = ui_LabelCreate("Body Sock Min Y", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockMin");
	pField->arrayIndex = 1;
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockMinYField = pField;
	y += 28;
	pLabel = ui_LabelCreate("Body Sock Min Z", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockMin");
	pField->arrayIndex = 2;
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockMinZField = pField;
	y += 28;

	// Body Sock Max
	pLabel = ui_LabelCreate("Body Sock Max X", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockMax");
	pField->arrayIndex = 0;
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockMaxXField = pField;
	y += 28;
	pLabel = ui_LabelCreate("Body Sock Max Y", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockMax");
	pField->arrayIndex = 1;
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockMaxYField = pField;
	y += 28;
	pLabel = ui_LabelCreate("Body Sock Max Z", 15, y);
	emPanelAddChild(pPanel, pLabel, false);
	pField = MEFieldCreateSimple(kMEFieldType_TextEntry, pDoc->pOrigCostume ? pDoc->pOrigCostume->pBodySockInfo : NULL, pDoc->pCostume->pBodySockInfo, parse_PCBodySockInfo, "BodySockMax");
	pField->arrayIndex = 2;
	MEFieldAddToParent(pField, UI_WIDGET(emPanelGetExpander(pPanel)), 140, y);
	MEFieldSetChangeCallback(pField, costumeEdit_UIFieldChanged, pDoc);
	ui_WidgetSetWidthEx(pField->pUIWidget, 1.0, UIUnitPercentage);
	pDoc->pBodySockMaxZField = pField;
	y += 28;


	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelScale(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;

	pPanel = emPanelCreate("Artist", "Scaling", 28);

	pDoc->pScalePanel = pPanel;

	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelFX(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;

	pPanel = emPanelCreate("Artist", "Costume FX", 0);

	pDoc->pFxPanel = pPanel;

	emPanelSetOpened(pPanel, true);

	return pPanel;
}


EMPanel *costumeEdit_InitPanelFXSwap(CostumeEditDoc *pDoc)
{
	EMPanel *pPanel;

	pPanel = emPanelCreate("Artist", "Costume FX Swap", 0);

	pDoc->pFxSwapPanel = pPanel;

	emPanelSetOpened(pPanel, true);

	return pPanel;
}



void costumeEdit_InitDisplay(EMEditor *pEditor, CostumeEditDoc *pDoc)
{	
	pDoc->bIgnorePrefChanges = true; // Don't store prefs during window init

	// These weird lines are here to force this dictionary to actually have values in it
	resDictSetMaxUnreferencedResources("DynFxInfo", RES_DICT_KEEP_ALL);
	resRequestAllResourcesInDictionary("DynFxInfo");
	resRequestAllResourcesInDictionary(g_hItemDict);

	// Create the windows
	pDoc->pMainWindow = costumeEdit_InitWinMain(pDoc);
	pDoc->pDetailWindow = costumeEdit_InitWinDetail(pDoc);

	// EDITOR MANAGER needs to be told about the windows used
	ui_WindowPresent(pDoc->pMainWindow);
	pDoc->emDoc.primary_ui_window = pDoc->pMainWindow;
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pMainWindow);
	eaPush(&pDoc->emDoc.ui_windows, pDoc->pDetailWindow);

	// Create the side-panels
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelCostume(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelBodySettings(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelPart(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelColor(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelReflection(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelArtistColor(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelArtistTexture(pDoc));

	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelAnimation(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelSettings(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelCollision(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelBits(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelFX(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelFXSwap(pDoc));
	eaPush(&pDoc->emDoc.em_panels, costumeEdit_InitPanelScale(pDoc));


	// Setup the UI
	pDoc->bIgnoreFieldChanges = true;
	costumeDefEdit_InitDisplay(gDefDoc);
	costumeDefEdit_SetSkeleton(gDefDoc, GET_REF(pDoc->pCostume->hSkeleton));
	costumeEdit_BuildScaleGroups(pDoc);
	costumeEdit_UpdateDisplayNameStatus(pDoc);
	pDoc->bIgnoreFieldChanges = false;

	// Set initial selections
	costumeEdit_SelectBone(pDoc, NULL);
	costumeDefEdit_DefUpdateLists(gDefDoc);

	// Add def windows to the editor
	if (eaSize(&pEditor->shared_windows) == 0) {
		eaPush(&pEditor->shared_windows, gDefDoc->pGeoEditWin);
		eaPush(&pEditor->shared_windows, gDefDoc->pMatEditWin);
		eaPush(&pEditor->shared_windows, gDefDoc->pTexEditWin);
	}

	pDoc->bIgnorePrefChanges = false;
}


void costumeEdit_InitToolbars(EMEditor *pEditor)
{
	EMToolbar *pToolbar;
	UIButton *pButton;
	UICheckButton *pCheck;

	pToolbar = emToolbarCreateFileToolbar(EM_FILE_TOOLBAR_NEW|EM_FILE_TOOLBAR_OPEN|EM_FILE_TOOLBAR_SAVE);
	eaPush(&pEditor->toolbars, pToolbar);
	eaPush(&pEditor->toolbars, emToolbarCreateWindowToolbar());

	pToolbar = emToolbarCreate(505);

	pButton = ui_ButtonCreate("Random", 0, 0, costumeEdit_UIRandomCostume, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 70);
	emToolbarAddChild(pToolbar, pButton, false);

	pButton = ui_ButtonCreate("Center Camera", 75, 0, costumeEdit_UICenterCamera, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 110);
	emToolbarAddChild(pToolbar, pButton, false);

	pButton = ui_ButtonCreate("Clear Costume", 190, 0, costumeEdit_UIClearCostume, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 110);
	emToolbarAddChild(pToolbar, pButton, false);

	pButton = ui_ButtonCreate("Make Legal", 305, 0, costumeEdit_UIMakeLegal, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 95);
	emToolbarAddChild(pToolbar, pButton, false);

	pCheck = ui_CheckButtonCreate(405, 0, "Height Ruler", EditorPrefGetInt(COSTUME_EDITOR, "Option", "ShowHeightRuler", 0));
	ui_CheckButtonSetToggledCallback(pCheck, costumeEdit_UIToggleHeightRuler, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pCheck), 100);
	emToolbarAddChild(pToolbar, pCheck, false);

	eaPush(&pEditor->toolbars, pToolbar);
	
	pToolbar = emToolbarCreate(210);
	
	pButton = ui_ButtonCreateWithDownUp("Rotate Left", 0, 0, costumeEdit_UICameraRotateLeftStart, pEditor, costumeEdit_UICameraRotateStop, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	emToolbarAddChild(pToolbar, pButton, false);
	
	pButton = ui_ButtonCreateWithDownUp("Rotate Right", 105, 0, costumeEdit_UICameraRotateRightStart, pEditor, costumeEdit_UICameraRotateStop, pEditor);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	emToolbarAddChild(pToolbar, pButton, false);
	
	eaPush(&pEditor->toolbars, pToolbar);

	if (!gCostumeMOTToolbar)
	{
		gCostumeMOTToolbar = motCreateToolbar( MET_SKIES | MET_TIME | MET_UNLIT, pEditor->camera, zerovec3, zerovec3, 20, COSTUME_EDITOR );
		eaPush( &pEditor->toolbars, motGetToolbar( gCostumeMOTToolbar ));
	}
}


void costumeEdit_InitMenus(EMEditor *pEditor)
{
	UIMenuItem *pDispNames;
	UIMenuItem *pColorLink;
	UIMenuItem *pMatLink;
	UIMenuItem *pSymmetry;
	UIMenuItem *pUnlock;

	gUseDispNames = EditorPrefGetInt(COSTUME_EDITOR, "Option", "UseDisplayNames", 1);
	gCEColorLinkAll = EditorPrefGetInt(COSTUME_EDITOR, "Option", "ColorLink", 1);
	gCEMatLinkAll = EditorPrefGetInt(COSTUME_EDITOR, "Option", "MatLink", 0);
	gUseSymmetry = EditorPrefGetInt(COSTUME_EDITOR, "Option", "UseSymmetry", 1);
	gCEUnlockAll = EditorPrefGetInt(COSTUME_EDITOR, "Option", "UnlockAll", 1);

	// Display/System Name Toggle
	emMenuItemCreate(pEditor, "coe_usedispnames", "Use Display Names", NULL, NULL, NULL);
	pDispNames = ui_MenuItemCreate("Use Display Names", UIMenuCheckButton, costumeEdit_UIDispNameChange, pEditor, NULL);
	ui_MenuItemSetCheckState(pDispNames, gUseDispNames);
	emMenuItemSet(pEditor, "coe_usedispnames", pDispNames);

	// Color/Material Link Default Toggles
	emMenuItemCreate(pEditor, "coe_colorlinkdef", "Color Link Def All", NULL, NULL, NULL);
	pColorLink = ui_MenuItemCreate("Color Link Def All", UIMenuCheckButton, costumeEdit_UIColorLinkChange, pEditor, NULL);
	ui_MenuItemSetCheckState(pColorLink, gCEColorLinkAll);
	emMenuItemSet(pEditor, "coe_colorlinkdef", pColorLink);

	emMenuItemCreate(pEditor, "coe_matlinkdef", "Mat Link Def All", NULL, NULL, NULL);
	pMatLink = ui_MenuItemCreate("Mat Link Def All", UIMenuCheckButton, costumeEdit_UIMatLinkChange, pEditor, NULL);
	ui_MenuItemSetCheckState(pMatLink, gCEMatLinkAll);
	emMenuItemSet(pEditor, "coe_matlinkdef", pMatLink);

	// Symmetry Toggle
	emMenuItemCreate(pEditor, "coe_usesymmetry", "Use Symmetry", NULL, NULL, NULL);
	pSymmetry = ui_MenuItemCreate("Use Symmetry", UIMenuCheckButton, costumeEdit_UISymmetryChange, pEditor, NULL);
	ui_MenuItemSetCheckState(pSymmetry, gUseSymmetry);
	emMenuItemSet(pEditor, "coe_usesymmetry", pSymmetry);

	// Unlock All Toggle
	emMenuItemCreate(pEditor, "coe_unlockall", "Unlock All", NULL, NULL, NULL);
	pUnlock = ui_MenuItemCreate("Unlock All", UIMenuCheckButton, costumeEdit_UIUnlockChange, pEditor, NULL);
	ui_MenuItemSetCheckState(pUnlock, gCEUnlockAll);
	emMenuItemSet(pEditor, "coe_unlockall", pUnlock);

	// File menus
	emMenuItemCreate(pEditor, "coe_revertcostume", "Revert", NULL, NULL, "COE_RevertCostume");
	emMenuItemCreate(pEditor, "coe_clonecostume", "Clone", NULL, NULL, "COE_CloneCostume");

	// Register the menus against this editor
	emMenuRegister(pEditor, emMenuCreate(pEditor, "View", "coe_usedispnames", "coe_colorlinkdef", "coe_matlinkdef", "coe_usesymmetry", "coe_unlockall", NULL));
	emMenuRegister(pEditor, emMenuCreate(pEditor, "File", "coe_revertcostume", "coe_clonecostume", NULL));
}

static void costumeEdit_EnsureAnimList(void)
{
	int i;
	if(eaSize(&eaCostumeAnims)==0) {
		for( i = 0; i < ARRAY_SIZE(g_CostumeAnims); i++) {
			eaPush(&eaCostumeAnims,&g_CostumeAnims[i]);
		}
	}
}

static void costumeEdit_AnimListFileCallback(const char *relpath, int when)
{
	int i;
	loadstart_printf("Reloading Costume Editor Anim List...");
	eaClear(&eaCostumeAnims);
	StructDeInit(parse_CostumeAnimList, &g_CostumeAnimsFromDisk);
	if(fileExists("editors/CostumeEditorAnimList.txt")) {
		ParserLoadFiles(NULL, "editors/CostumeEditorAnimList.txt", "CostumeEditorAnimList.bin", 0, parse_CostumeAnimList, &g_CostumeAnimsFromDisk);	
		for ( i=0; i < eaSize(&g_CostumeAnimsFromDisk.ppAnims); i++ ) {
			eaPush(&eaCostumeAnims, g_CostumeAnimsFromDisk.ppAnims[i]);
		}
	}
	costumeEdit_EnsureAnimList();
	loadend_printf(" done.");
}

static void costumeEdit_LoadAnimList(void)
{
	int i;
	loadstart_printf("Loading Costume Editor Anim List...");
	if(fileExists("editors/CostumeEditorAnimList.txt")) {
		ParserLoadFiles(NULL, "editors/CostumeEditorAnimList.txt", "CostumeEditorAnimList.bin", 0, parse_CostumeAnimList, &g_CostumeAnimsFromDisk);
		for ( i=0; i < eaSize(&g_CostumeAnimsFromDisk.ppAnims); i++ ) {
			eaPush(&eaCostumeAnims, g_CostumeAnimsFromDisk.ppAnims[i]);
		}
	}
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "editors/CostumeEditorAnimList.txt", costumeEdit_AnimListFileCallback);
	costumeEdit_EnsureAnimList();
	loadend_printf(" done.");
}


// Load global data structures
void costumeEdit_InitData(EMEditor *pEditor)
{
	costumeEdit_LoadAnimList();

	if (!gDefDoc) {
		gDefDoc = calloc(1, sizeof(CostumeEditDefDoc));
		gDefDoc->pEditor = pEditor;
		gDefDoc->bGeoDefSaved = true;
		gDefDoc->bMatDefSaved = true;
		gDefDoc->bTexDefSaved = true;
	}

	if (!gInitializedEditor) {
		costumeEdit_InitMenus(pEditor);
		costumeEdit_InitToolbars(pEditor);

		// Register dictionary change listeners
		resDictRegisterEventCallback(g_hPlayerCostumeDict, costumeEdit_CostumeDictChanged, NULL);

		emAddDictionaryStateChangeHandler(pEditor, "PlayerCostume", NULL, NULL, costumeEdit_SaveCostumeContinue, costumeEdit_DeleteCostumeContinue, NULL); 
		
		resDictRegisterEventCallback(g_hCostumeSkeletonDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeBoneDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeGeometryDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeGeometryAddDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeMaterialDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeMaterialAddDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeTextureDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeCategoryDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(g_hCostumeRegionDict, costumeEdit_AssetDictChanged, NULL);
		resDictRegisterEventCallback(gMessageDict, costumeEdit_AssetDictChanged, NULL);

		// Manage the geo file list
		costumeEdit_GeoFileChangeCallback(NULL,0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "character_library/*.ModelHeader", costumeEdit_GeoFileChangeCallback);
		costumeEdit_PoseFileChangeCallback(NULL,0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, "character_library/*.atrk", costumeEdit_PoseFileChangeCallback);

		resGetUniqueScopes(g_hPlayerCostumeDict, &geaScopes);

		gInitializedEditor = true;

		gEmptyPart = StructCreateNoConst(parse_PCPart);
		gEmptyPart->pArtistData = StructCreateNoConst(parse_PCArtistPartData);
		gEmptyPart->pMovableTexture = StructCreateNoConst(parse_PCMovableTextureInfo);
		gEmptyPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
		gEmptyPart->pCustomColors = StructCreateNoConst(parse_PCCustomColorInfo);
	}

	costumeDefEdit_InitData(pEditor);
}


//---------------------------------------------------------------------------------------------------
// Public Interface
//---------------------------------------------------------------------------------------------------


NOCONST(PlayerCostume) *costumeEdit_CreateCostume(const char *pcName, NOCONST(PlayerCostume) *pCostumeToClone)
{
	NOCONST(PlayerCostume) *pCostume;
	PCSkeletonDef *pSkel = NULL;
	const char *pcBaseName;
	char nameBuf[260];
	char resultBuf[260];
	int count = 0;
	int i;

	if (pCostumeToClone) {
		pCostume = StructCloneNoConst(parse_PlayerCostume, pCostumeToClone);
		assert(pCostume);
		pcBaseName = pCostume->pcName;
	} else {
		pCostume = StructCreateNoConst(parse_PlayerCostume);
		pcBaseName = DEFAULT_COSTUME_NAME;
		pCostume->eCostumeType = kPCCostumeType_NPC;

		if (RefSystem_ReferentFromString(g_hCostumeSkeletonDict, "NW_Male")) {
			SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, "NW_Male", pCostume->hSkeleton);
		} else if (RefSystem_ReferentFromString(g_hCostumeSkeletonDict, "Male")) {
			SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, "Male", pCostume->hSkeleton);
		} else if (RefSystem_ReferentFromString(g_hCostumeSkeletonDict, "Human_Male")) {
			SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, "Human_Male", pCostume->hSkeleton);
		} else if (RefSystem_ReferentFromString(g_hCostumeSkeletonDict, "StarTrekMale")) {
			SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, "StarTrekMale", pCostume->hSkeleton);
		} else if (RefSystem_ReferentFromString(g_hCostumeSkeletonDict, "CoreMale")) {
			SET_HANDLE_FROM_STRING(g_hCostumeSkeletonDict, "CoreMale", pCostume->hSkeleton);
		} else {
			StructDestroyNoConst(parse_PlayerCostume, pCostume);
			ui_DialogPopup("No Male Skeleton", "Cannot create a new costume because there is no skeleton 'NW_Male', 'Male', 'Human_Male', 'STO_Male', or 'CoreMale' defined");
			return NULL;
		}

		// Set default height and such
		pSkel = GET_REF(pCostume->hSkeleton);
		if (pSkel && pSkel->fDefaultHeight) {
			pCostume->fHeight = pSkel->fDefaultHeight;
		} else {
			pCostume->fHeight = 6;
		}
		if (pSkel && pSkel->fDefaultMuscle) {
			pCostume->fMuscle = pSkel->fDefaultMuscle;
		} else {
			pCostume->fMuscle = 20;
		}
		if (pSkel) {
			for(i=0; i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
				if (i < eafSize(&pSkel->eafDefaultBodyScales)) {
					eafPush(&pCostume->eafBodyScales, pSkel->eafDefaultBodyScales[i]);
				} else {
					eafPush(&pCostume->eafBodyScales, 20);
				}
			}
		}

		for (i = eaSize(&pCostume->eaParts)-1; i >= 0; --i)
		{
			NOCONST(PCPart) *pPart = pCostume->eaParts[i];
			PCTextureDef *pTex;

			pTex = GET_REF(pPart->hPatternTexture);
			if (pTex && pTex->pValueOptions)
			{
				if (pTex->pValueOptions->fValueMax > pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault >= pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault <= pTex->pValueOptions->fValueMax)
				{
					pPart->pTextureValues->fPatternValue = (((pTex->pValueOptions->fValueDefault - pTex->pValueOptions->fValueMin) * 200.0f)/(pTex->pValueOptions->fValueMax - pTex->pValueOptions->fValueMin)) - 100.0f;
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor0, pPart->color0);}
				if (pTex->pColorOptions->bHasDefaultColor1) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor1, pPart->color1);}
				if (pTex->pColorOptions->bHasDefaultColor2) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor2, pPart->color2);}
				if (pTex->pColorOptions->bHasDefaultColor3) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor3, pPart->color3);}
			}
			pTex = GET_REF(pPart->hDetailTexture);
			if (pTex && pTex->pValueOptions)
			{
				if (pTex->pValueOptions->fValueMax > pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault >= pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault <= pTex->pValueOptions->fValueMax)
				{
					pPart->pTextureValues->fDetailValue = (((pTex->pValueOptions->fValueDefault - pTex->pValueOptions->fValueMin) * 200.0f)/(pTex->pValueOptions->fValueMax - pTex->pValueOptions->fValueMin)) - 100.0f;
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor0, pPart->color0);}
				if (pTex->pColorOptions->bHasDefaultColor1) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor1, pPart->color1);}
				if (pTex->pColorOptions->bHasDefaultColor2) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor2, pPart->color2);}
				if (pTex->pColorOptions->bHasDefaultColor3) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor3, pPart->color3);}
			}
			pTex = GET_REF(pPart->hSpecularTexture);
			if (pTex && pTex->pValueOptions)
			{
				if (pTex->pValueOptions->fValueMax > pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault >= pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault <= pTex->pValueOptions->fValueMax)
				{
					pPart->pTextureValues->fSpecularValue = (((pTex->pValueOptions->fValueDefault - pTex->pValueOptions->fValueMin) * 200.0f)/(pTex->pValueOptions->fValueMax - pTex->pValueOptions->fValueMin)) - 100.0f;
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor0, pPart->color0);}
				if (pTex->pColorOptions->bHasDefaultColor1) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor1, pPart->color1);}
				if (pTex->pColorOptions->bHasDefaultColor2) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor2, pPart->color2);}
				if (pTex->pColorOptions->bHasDefaultColor3) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor3, pPart->color3);}
			}
			pTex = GET_REF(pPart->hDiffuseTexture);
			if (pTex && pTex->pValueOptions)
			{
				if (pTex->pValueOptions->fValueMax > pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault >= pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault <= pTex->pValueOptions->fValueMax)
				{
					pPart->pTextureValues->fDiffuseValue = (((pTex->pValueOptions->fValueDefault - pTex->pValueOptions->fValueMin) * 200.0f)/(pTex->pValueOptions->fValueMax - pTex->pValueOptions->fValueMin)) - 100.0f;
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor0, pPart->color0);}
				if (pTex->pColorOptions->bHasDefaultColor1) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor1, pPart->color1);}
				if (pTex->pColorOptions->bHasDefaultColor2) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor2, pPart->color2);}
				if (pTex->pColorOptions->bHasDefaultColor3) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor3, pPart->color3);}
			}
			pTex = GET_REF(pPart->pMovableTexture->hMovableTexture);
			if (pTex && pTex->pValueOptions)
			{
				if (pTex->pValueOptions->fValueMax > pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault >= pTex->pValueOptions->fValueMin && pTex->pValueOptions->fValueDefault <= pTex->pValueOptions->fValueMax)
				{
					pPart->pMovableTexture->fMovableValue = (((pTex->pValueOptions->fValueDefault - pTex->pValueOptions->fValueMin) * 200.0f)/(pTex->pValueOptions->fValueMax - pTex->pValueOptions->fValueMin)) - 100.0f;
				}
			}
			if (pTex && pTex->pColorOptions)
			{
				if (pTex->pColorOptions->bHasDefaultColor0) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor0, pPart->color0);}
				if (pTex->pColorOptions->bHasDefaultColor1) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor1, pPart->color1);}
				if (pTex->pColorOptions->bHasDefaultColor2) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor2, pPart->color2);}
				if (pTex->pColorOptions->bHasDefaultColor3) {COPY_COSTUME_COLOR(pTex->pColorOptions->uDefaultColor3, pPart->color3);}
			}
			if (pTex && pTex->pMovableOptions)
			{
				if (pTex->pMovableOptions->fMovableMaxX > pTex->pMovableOptions->fMovableMinX && pTex->pMovableOptions->fMovableDefaultX >= pTex->pMovableOptions->fMovableMinX && pTex->pMovableOptions->fMovableDefaultX <= pTex->pMovableOptions->fMovableMaxX)
				{
					pPart->pMovableTexture->fMovableX = (((pTex->pMovableOptions->fMovableDefaultX - pTex->pMovableOptions->fMovableMinX) * 200.0f)/(pTex->pMovableOptions->fMovableMaxX - pTex->pMovableOptions->fMovableMinX)) - 100.0f;
				}
				if (pTex->pMovableOptions->fMovableMaxY > pTex->pMovableOptions->fMovableMinY && pTex->pMovableOptions->fMovableDefaultY >= pTex->pMovableOptions->fMovableMinY && pTex->pMovableOptions->fMovableDefaultY <= pTex->pMovableOptions->fMovableMaxY)
				{
					pPart->pMovableTexture->fMovableY = (((pTex->pMovableOptions->fMovableDefaultY - pTex->pMovableOptions->fMovableMinY) * 200.0f)/(pTex->pMovableOptions->fMovableMaxY - pTex->pMovableOptions->fMovableMinY)) - 100.0f;
				}
				if (pTex->pMovableOptions->fMovableMaxScaleX > pTex->pMovableOptions->fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX >= pTex->pMovableOptions->fMovableMinScaleX && pTex->pMovableOptions->fMovableDefaultScaleX <= pTex->pMovableOptions->fMovableMaxScaleX)
				{
					pPart->pMovableTexture->fMovableScaleX = (((pTex->pMovableOptions->fMovableDefaultScaleX - pTex->pMovableOptions->fMovableMinScaleX) * 100.0f)/(pTex->pMovableOptions->fMovableMaxScaleX - pTex->pMovableOptions->fMovableMinScaleX));
				}
				if (pTex->pMovableOptions->fMovableMaxScaleY > pTex->pMovableOptions->fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY >= pTex->pMovableOptions->fMovableMinScaleY && pTex->pMovableOptions->fMovableDefaultScaleY <= pTex->pMovableOptions->fMovableMaxScaleY)
				{
					pPart->pMovableTexture->fMovableScaleY = (((pTex->pMovableOptions->fMovableDefaultScaleY - pTex->pMovableOptions->fMovableMinScaleY) * 100.0f)/(pTex->pMovableOptions->fMovableMaxScaleY - pTex->pMovableOptions->fMovableMinScaleY));
				}
				pPart->pMovableTexture->fMovableRotation = pTex->pMovableOptions->fMovableDefaultRotation;
			}
		}
	}

	// Strip off trailing digits and underbar
	strcpy(nameBuf, pcBaseName);
	while(nameBuf[0] && (nameBuf[strlen(nameBuf)-1] >= '0') && (nameBuf[strlen(nameBuf)-1] <= '9')) {
		nameBuf[strlen(nameBuf)-1] = '\0';
	}
	if (nameBuf[0] && nameBuf[strlen(nameBuf)-1] == '_') {
		nameBuf[strlen(nameBuf)-1] = '\0';
	}

	// Generate new name
	do {
		++count;
		sprintf(resultBuf,"%s_%d",nameBuf,count);
	} while (costumeEditorEMIsDocOpen(resultBuf) || RefSystem_ReferentFromString(g_hPlayerCostumeDict,resultBuf));

	pCostume->pcName = allocAddString(resultBuf);

	sprintf(nameBuf, "defs/costumes/%s.costume", pCostume->pcName);
	pCostume->pcFileName = allocAddString(nameBuf);

	return pCostume;
}

static void costumeEdit_FixupParts(NOCONST(PlayerCostume) *pCostume)
{
	int i;
	for ( i=0; i < eaSize(&pCostume->eaParts); i++ )
	{
		NOCONST(PCPart) *pPart = pCostume->eaParts[i];
		if(!pPart->pTextureValues)
			pPart->pTextureValues = StructCreateNoConst(parse_PCTextureValueInfo);
	}
}

CostumeEditDoc *costumeEdit_InitDoc(NOCONST(PlayerCostume) *pCostume, bool bCreated)
{
	CostumeEditDoc *pDoc;

	// Initialize the structure
	pDoc = (CostumeEditDoc*)calloc(1,sizeof(CostumeEditDoc));

	// Set up graphics
	pDoc->pGraphics = costumeView_CreateGraphics();
	pDoc->pGraphics->bOverrideForceEditorRegion = true;

	// Fill in the costume data
	pDoc->pCostume = StructCloneNoConst(parse_PlayerCostume, pCostume);
	pDoc->bSaved = true;
	assert(pDoc->pCostume);
	if (bCreated) {
		costumeTailor_SetDefaultSkinColor(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL);
	}
	costumeTailor_FillAllBones(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL/*powerFX*/, NULL, bCreated, true, gUseDispNames);
	costumeEdit_FixupParts(pDoc->pCostume);
	costumeEdit_FixMessages(pDoc->pCostume);

	if (!bCreated) {
		PCSkeletonDef *pSkel;
		int i;

		// Set up the original if not newly created
		pDoc->pOrigCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);

		// Also check for possible region/category invalid
		if ((pDoc->pCostume->eCostumeType != kPCCostumeType_Unrestricted) && !costumeValidate_AreCategoriesValid(pDoc->pCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, true)) {
			pDoc->pCostume->eCostumeType = kPCCostumeType_Unrestricted;
			ui_DialogPopup("Costume Changed", "The costume includes region/category choices that are not legal for the current set of definitions.  Perhaps they were changed after the costume was created.  The costume's type has been changed to 'Unrestricted' so it will not be damaged by the editor.");
		}

		// Check for possible invalid restrictions
		if ((pDoc->pCostume->eCostumeType != kPCCostumeType_Unrestricted) && !costumeValidate_AreRestrictionsValid(pDoc->pCostume, NULL, true, NULL, NULL)) {
			pDoc->pCostume->eCostumeType = kPCCostumeType_Unrestricted;
			ui_DialogPopup("Costume Changed", "The costume includes choices that are restricted for the current set of definitions.  Perhaps they were changed after the costume was created.  The costume's type has been changed to 'Unrestricted' so it will not be damaged by the editor.");
		}

		pSkel = GET_REF(pDoc->pCostume->hSkeleton);
		if (pSkel) {
			for(i=eafSize(&pDoc->pCostume->eafBodyScales); i<eaSize(&pSkel->eaBodyScaleInfo); ++i) {
				if (i < eafSize(&pSkel->eafDefaultBodyScales)) {
					assert(pSkel->eafDefaultBodyScales);
					eafPush(&pDoc->pCostume->eafBodyScales, pSkel->eafDefaultBodyScales[i]);
				} else {
					eafPush(&pDoc->pCostume->eafBodyScales, 20);
				}
			}
		}
	}

	// Generate the graphics costume
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);

	pDoc->pCurrentDyePack = StructCreate(parse_CostumeEditDyePackStruct);
	return pDoc;
}


// This is called to open a costume edit document
CostumeEditDoc *costumeEdit_OpenCostume(EMEditor *pEditor, const char *pcName)
{
	CostumeEditDoc *pDoc = NULL;
	NOCONST(PlayerCostume) *pCostume = NULL;
	bool bCreated = false;

	if (pcName && resIsEditingVersionAvailable(g_hPlayerCostumeDict, pcName)) {
		// Simply open the object since it is in the dictionary
		pCostume = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pcName);
	} else if (pcName) {
		// Wait for object to show up so we can open it
		resSetDictionaryEditMode(g_hPlayerCostumeDict, true);
		resSetDictionaryEditMode(gMessageDict, true);
		emSetResourceState(pEditor, pcName, EMRES_STATE_OPENING);
		resRequestOpenResource(g_hPlayerCostumeDict, pcName);
	} else {
		// Create a new object since it is not in the dictionary
		pCostume = costumeEdit_CreateCostume(pcName, NULL);
		bCreated = true;
	}

	if (pCostume) {
		pDoc = costumeEdit_InitDoc(pCostume, bCreated);
		costumeEdit_InitDisplay(pEditor, pDoc);
		resFixFilename(g_hPlayerCostumeDict, pDoc->pCostume->pcName, pDoc->pCostume);

		costumeEdit_UpdateColorButtons(pDoc);
	}

	return pDoc;
}


// This is called to open a costume edit document
// If the name does not exist, returns NULL
CostumeEditDoc *costumeEdit_OpenCloneOfCostume(EMEditor *pEditor, const char *pcName)
{
	CostumeEditDoc *pDoc = NULL;
	NOCONST(PlayerCostume) *pCostume = NULL;

	if (pcName && stricmp("__AUTO_CLONE__", pcName) == 0) {
		pCostume = gCostumeToClone;
		gCostumeToClone = NULL;

		if (pCostume) {
			pDoc = costumeEdit_InitDoc(pCostume, true);
			costumeEdit_InitDisplay(pEditor, pDoc);
		}
	} else if (pcName && RefSystem_ReferentFromString(g_hPlayerCostumeDict, pcName)) {
		// Create the clone
		NOCONST(PlayerCostume) *pCostumeToClone = RefSystem_ReferentFromString(g_hPlayerCostumeDict, pcName);
		pCostume = costumeEdit_CreateCostume(pcName, pCostumeToClone);

		if (pCostume) {
			pDoc = costumeEdit_InitDoc(pCostume, true);
			costumeEdit_InitDisplay(pEditor, pDoc);
		}
	}

	return pDoc;
}


static void costumeEdit_UIDismissWindow(UIButton *pButton, CostumeEditDoc *pDoc)
{
	if (pGlobalWindow) {
		EditorPrefStoreWindowPosition(COSTUME_EDITOR, "Window Position", "Save Confirm", pGlobalWindow);

		// Free the window
		ui_WindowHide(pGlobalWindow);
		ui_WidgetQueueFreeAndNull(&pGlobalWindow);
	}

	// Clear window flags
	pDoc->bSaveOverwrite = false;
	pDoc->bSaveRename = false;
}


static void costumeEdit_UISaveOverwrite(UIButton *pButton, CostumeEditDoc *pDoc)
{
	costumeEdit_UIDismissWindow(pButton, pDoc);

	pDoc->bSaveOverwrite = true;
	emSaveDocAs(&pDoc->emDoc);
}


static void costumeEdit_UISaveRename(UIButton *pButton, CostumeEditDoc *pDoc)
{
	costumeEdit_UIDismissWindow(pButton, pDoc);

	pDoc->bSaveRename = true;
	emSaveDoc(&pDoc->emDoc);
}


static void costumeEdit_PromptForSave(CostumeEditDoc *pDoc, bool bNameCollision, bool bNameChanged)
{
	UILabel *pLabel;
	UIButton *pButton;
	char buf[1024];
	int y = 0;
	int width = 0;
	int x = 0;

	assert(!pGlobalWindow); // Global window is always supposed to be null'd when done with it

	pGlobalWindow = ui_WindowCreate("Confirm Save?", 200, 200, 300, 60);

	EditorPrefGetWindowPosition(COSTUME_EDITOR, "Window Position", "Save Confirm", pGlobalWindow);

	if (bNameChanged) {
		sprintf(buf, "The costume name was changed to a new name.  Did you want to rename or save as new?");
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameCollision) {
		sprintf(buf, "The costume name '%s' is already in use.  Did you want to overwrite it?", pDoc->pCostume->pcName);
		pLabel = ui_LabelCreate(buf, 0, y);
		ui_WindowAddChild(pGlobalWindow, pLabel);
		width = MAX(width, pLabel->widget.width + 20);
		y += 28;
	}

	if (bNameChanged) {
		if (bNameCollision) {
			pButton = ui_ButtonCreate("Save As New AND Overwrite", 0, 28, costumeEdit_UISaveOverwrite, pDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -260, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			pButton = ui_ButtonCreate("Rename AND Overwrite", 0, 28, costumeEdit_UISaveRename, pDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 200);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			x = 160;
			width = MAX(width, 540);
		} else {
			pButton = ui_ButtonCreate("Save As New", 0, 0, costumeEdit_UISaveOverwrite, pDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -160, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			pButton = ui_ButtonCreate("Rename", 0, 0, costumeEdit_UISaveRename, pDoc);
			ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
			ui_WidgetSetPositionEx(UI_WIDGET(pButton), -50, y, 0.5, 0, UITopLeft);
			ui_WindowAddChild(pGlobalWindow, pButton);

			x = 60;
			width = MAX(width, 340);
		}
	} else {
		pButton = ui_ButtonCreate("Overwrite", 0, 0, costumeEdit_UISaveOverwrite, pDoc);
		ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
		ui_WidgetSetPositionEx(UI_WIDGET(pButton), -105, y, 0.5, 0, UITopLeft);
		ui_WindowAddChild(pGlobalWindow, pButton);

		x = 5;
		width = MAX(width, 230);
	}

	pButton = ui_ButtonCreate("Cancel", 0, 0, costumeEdit_UIDismissWindow, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}


static bool costumeEdit_DeleteCostumeContinue(EMEditor *pEditor, const char *pcName, void *pCostume, EMResourceState eState, void *pData, bool bSuccess)
{
	if (bSuccess && (eState == EMRES_STATE_LOCK_SUCCEEDED)) {
		// Since we got the lock, continue by doing the delete save
		emSetResourceStateWithData(pEditor, pcName, EMRES_STATE_DELETING, pCostume);
		resRequestSaveResource(g_hPlayerCostumeDict, pcName, NULL);
	}

	return true;
}


static void costumeEdit_DeleteCostumeStart(EMEditor *pEditor, const char *pcName, void *pCostume)
{
	NOCONST(PlayerCostume) *pCostumeCopy = StructCloneNoConst(parse_PlayerCostume, pCostume);

	resSetDictionaryEditMode(g_hPlayerCostumeDict, true);
	resSetDictionaryEditMode(gMessageDict, true);
	resRequestLockResource(g_hPlayerCostumeDict, pcName, pCostumeCopy);

	// Go into lock state if we don't already have the lock
	if (!resGetLockOwner(g_hPlayerCostumeDict, pcName)) {
		emSetResourceStateWithData(pEditor, pcName, EMRES_STATE_LOCKING_FOR_DELETE, pCostumeCopy);
		//printf("Locking %s\n", pcName); // DEBUG
		return;
	}

	// Otherwise continue the delete
	costumeEdit_DeleteCostumeContinue(pEditor, pcName, pCostumeCopy, EMRES_STATE_LOCK_SUCCEEDED, NULL, true);
}


bool costumeEdit_SaveCostumeContinue(EMEditor *pEditor, const char *pcName, CostumeEditDoc *pDoc, EMResourceState eState, void *pData, bool bSuccess)
{
	if (bSuccess && (eState == EMRES_STATE_SAVE_SUCCEEDED)) {
		pDoc->bSaved = true;
	}
	return true;
}

static void costumeEdit_UIDismissErrorWindow(UIButton *pButton, CostumeEditDoc *pDoc)
{
	if (pGlobalWindow) {
		EditorPrefStoreWindowPosition(COSTUME_EDITOR, "Window Position", "Not Checked Out", pGlobalWindow);

		// Free the window
		ui_WindowHide(pGlobalWindow);
		ui_WidgetQueueFreeAndNull(&pGlobalWindow);
	}
}

static void costumeEdit_NotCheckedOutError(CostumeEditDoc *pDoc)
{
	UIButton *pButton;
	int y = 0;
	int width = 250;
	int x = 0;

	assert(!pGlobalWindow); // Global window is always supposed to be null'd when done with it

	pGlobalWindow = ui_WindowCreate("You can't save this. It is not checked out.", 200, 200, 300, 60);

	EditorPrefGetWindowPosition(COSTUME_EDITOR, "Window Position", "Not Checked Out", pGlobalWindow);

	pButton = ui_ButtonCreate("Cancel", 0, 0, costumeEdit_UIDismissErrorWindow, pDoc);
	ui_WidgetSetWidth(UI_WIDGET(pButton), 100);
	ui_WidgetSetPositionEx(UI_WIDGET(pButton), x, y, 0.5, 0, UITopLeft);
	ui_WindowAddChild(pGlobalWindow, pButton);

	y += 28;

	pGlobalWindow->widget.width = width;
	pGlobalWindow->widget.height = y;

	ui_WindowSetClosable(pGlobalWindow, false);
	ui_WindowSetModal(pGlobalWindow, true);

	ui_WindowPresent(pGlobalWindow);
}


// This is called to save the costume being edited
EMTaskStatus costumeEdit_SaveCostume(CostumeEditDoc *pDoc, bool bSaveAsNew)
{
	NOCONST(PlayerCostume) *pNewCostume;
	const char *pcName;
	EMTaskStatus status;

	// Deal with state changes
	pcName = pDoc->pCostume->pcName;
	if (emHandleSaveResourceState(pDoc->emDoc.editor, pcName, &status)) {
		if (status != EM_TASK_INPROGRESS) {
			pDoc->bSaveOverwrite = false;
			pDoc->bSaveRename = false;
		}
		return status;
	}

	// Do regular save if never saved before
	if (!pDoc->pOrigCostume) {
		bSaveAsNew = false;
	}

	// VALIDATION LOGIC START
	if (resGetInfo("PlayerCostume", pcName) && !resIsWritable(g_hPlayerCostumeDict, pDoc->pCostume->pcName)) {
		costumeEdit_NotCheckedOutError(pDoc);
		pDoc->bSaveOverwrite = false;
		pDoc->bSaveRename = false;
		return EM_TASK_FAILED;
	}

	// Can't save costume with the default name or file given by the "New Costume"
	if (stricmp(pcName,DEFAULT_COSTUME_NAME) == 0) {
		char buf[1024];
		sprintf(buf, "The costume name '%s' is a default value for a new costume.\nChange this to an actual costume name and then try saving again.",pcName);
		ui_DialogPopup("Failed to Save", buf);
		pDoc->bSaveOverwrite = false;
		pDoc->bSaveRename = false;
		return EM_TASK_FAILED;
	}
	if (strcmp(pDoc->pCostume->pcFileName,DEFAULT_COSTUME_FILE) == 0) {
		char buf[1024];
		sprintf(buf, "The costume file '%s' is a default value for a new costume.\nChoose a new file name and then try saving again.",pDoc->pCostume->pcFileName);
		ui_DialogPopup("Failed to Save", buf);
		pDoc->bSaveOverwrite = false;
		pDoc->bSaveRename = false;
		return EM_TASK_FAILED;
	}

	// Make a copy of the object we're saving
	pNewCostume = StructCloneNoConst(parse_PlayerCostume, pDoc->pCostume);
	assert(pNewCostume);

	// Strip out unnecessary data from the costume
	costumeTailor_StripUnnecessary(pNewCostume);

	// Set up messages properly on costume
	costumeEdit_FixMessages(pNewCostume);

	// First, verify the costume is good
	if (!costumeLoad_ValidatePlayerCostume(CONTAINER_RECONST(PlayerCostume, pNewCostume), GET_REF(pDoc->pCostume->hSpecies), true, false, true)) {
		char buf[1024];
		sprintf(buf, "Failed to save Costume %s: Errors present.", pNewCostume->pcName);
		ui_DialogPopup("Failed to Save", buf);

		StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
		pDoc->bSaveOverwrite = false;
		pDoc->bSaveRename = false;
		return EM_TASK_FAILED;
	}

	// Prompt if name already in use (new costume or save as new)
	if (!pDoc->bSaveOverwrite && (!pDoc->pOrigCostume || bSaveAsNew) && resGetInfo("PlayerCostume", pcName)) {
		costumeEdit_PromptForSave(pDoc, true, false);

		StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
		pDoc->bSaveOverwrite = false;
		pDoc->bSaveRename = false;
		return EM_TASK_FAILED;
	} else if (!pDoc->bSaveRename && !pDoc->bSaveOverwrite &&
			   pDoc->pOrigCostume && (stricmp(pDoc->pOrigCostume->pcName,pcName) != 0)) {
		// Name changed and may have collision
		costumeEdit_PromptForSave(pDoc, (resGetInfo("PlayerCostume", pcName) != NULL), true);

		StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
		pDoc->bSaveOverwrite = false;
		pDoc->bSaveRename = false;
		return EM_TASK_FAILED;
	}

	if (pNewCostume->eCostumeType == kPCCostumeType_Player) {
		char *estrReason=NULL;
		if (!costumeValidate_ValidatePlayerCreated((PlayerCostume*)pNewCostume, GET_REF(pDoc->pCostume->hSpecies), NULL, NULL, NULL, &estrReason, NULL, NULL, true)) {
			ui_DialogPopup("Player costume has validation errors (but will save anyway): ", estrReason);
		}
	}

	// VALIDATION LOGIC END

	resSetDictionaryEditMode(g_hPlayerCostumeDict, true);
	resSetDictionaryEditMode(gMessageDict, true);

	// LOCKING START

	// Check the lock
	if (!resGetLockOwner(g_hPlayerCostumeDict, pcName)) {
		// Don't have lock, so ask server to lock and go into locking state
		emSetResourceState(pDoc->emDoc.editor, pcName, EMRES_STATE_LOCKING_FOR_SAVE);
		resRequestLockResource(g_hPlayerCostumeDict, pcName, pDoc->pCostume);

		StructDestroyNoConst(parse_PlayerCostume, pNewCostume);
		return EM_TASK_INPROGRESS;
	}
	// Get here if have the main lock... now check for rename lock
	if (!bSaveAsNew && pDoc->pOrigCostume &&
		(strcmp(pDoc->pOrigCostume->pcName,pDoc->pCostume->pcName) != 0)) {
		costumeEdit_DeleteCostumeStart(pDoc->emDoc.editor, pDoc->pOrigCostume->pcName, pDoc->pOrigCostume);
	}

	// LOCKING END

	// If there is an original object and the name changed, 
	// need to explicitly delete the old object when saving
	if (!bSaveAsNew && pDoc->pOrigCostume &&
		(strcmp(pDoc->pOrigCostume->pcName,pDoc->pCostume->pcName) != 0)) {
		int iNameIndex = -1;

		// Need to alter name on orig object in order to catch dictionary change update
		assert(ParserFindColumn(parse_PlayerCostume,"Name",&iNameIndex));
		TokenStoreSetString(parse_PlayerCostume, iNameIndex, pDoc->pOrigCostume, 0, pDoc->pCostume->pcName, NULL, NULL, NULL, NULL);
	}

	// SAVE START
	
	if (bSaveAsNew) {
		// When save as new, remove original costume so revert acts on new costume
		StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pOrigCostume);
		pDoc->pOrigPart = NULL;

		// Need to update lists if wipe original or risk a crash
		costumeEdit_UpdateLists(pDoc);
	}

	pDoc->bSaved = false;

	// Send save to server
	emSetResourceStateWithData(pDoc->emDoc.editor, pcName, EMRES_STATE_SAVING, pDoc);
	resRequestSaveResource(g_hPlayerCostumeDict, pcName, pNewCostume);

	return EM_TASK_INPROGRESS;
}


// This is called to close the costume being edited
void costumeEdit_CloseCostume(CostumeEditDoc *pDoc)
{
	int i;

	costumeEdit_SavePrefs(pDoc);

	// Clean up the graphics
	costumeView_FreeGraphics(pDoc->pGraphics);

	// Costume Fields
	MEFieldDestroy(pDoc->pNameField);
	MEFieldDestroy(pDoc->pScopeField);
	MEFieldDestroy(pDoc->pCostumeTypeField);
	MEFieldDestroy(pDoc->pSkeletonField);
	MEFieldDestroy(pDoc->pSpeciesField);
	MEFieldDestroy(pDoc->pVoiceField);
	MEFieldDestroy(pDoc->pColorLinkAllField);
	MEFieldDestroy(pDoc->pMatLinkAllField);
	MEFieldDestroy(pDoc->pAccountUnlockField);
	MEFieldDestroy(pDoc->pBodySockDisableField);
	MEFieldDestroy(pDoc->pCollisionDisableField);
	MEFieldDestroy(pDoc->pShellDisableField);
	MEFieldDestroy(pDoc->pRagDollDisableField);
	MEFieldDestroy(pDoc->pBodySockGeoField);
	MEFieldDestroy(pDoc->pBodySockPoseField);
	MEFieldDestroy(pDoc->pBodySockMinXField);
	MEFieldDestroy(pDoc->pBodySockMinYField);
	MEFieldDestroy(pDoc->pBodySockMinZField);
	MEFieldDestroy(pDoc->pBodySockMaxXField);
	MEFieldDestroy(pDoc->pBodySockMaxYField);
	MEFieldDestroy(pDoc->pBodySockMaxZField);
	MEFieldDestroy(pDoc->pCollisionGeoField);
	if (pDoc->pHeightField) {
		MEFieldDestroy(pDoc->pHeightField);
	}
	if (pDoc->pMuscleField) {
		MEFieldDestroy(pDoc->pMuscleField);
	}
	if (pDoc->pStanceField) {
		MEFieldDestroy(pDoc->pStanceField);
	}

	// Part Fields
	MEFieldDestroy(pDoc->pGeoField);
	MEFieldDestroy(pDoc->pChildGeoField);
	MEFieldDestroy(pDoc->pMatField);
	MEFieldDestroy(pDoc->pPatternField);
	if (pDoc->pPatternValueField) MEFieldDestroy(pDoc->pPatternValueField);
	MEFieldDestroy(pDoc->pDetailField);
	if (pDoc->pDetailValueField) MEFieldDestroy(pDoc->pDetailValueField);
	MEFieldDestroy(pDoc->pSpecularField);
	if (pDoc->pSpecularValueField) MEFieldDestroy(pDoc->pSpecularValueField);
	MEFieldDestroy(pDoc->pDiffuseField);
	if (pDoc->pDiffuseValueField) MEFieldDestroy(pDoc->pDiffuseValueField);
	MEFieldDestroy(pDoc->pMovableField);
	if (pDoc->pMovableValueField) MEFieldDestroy(pDoc->pMovableValueField);
	if (pDoc->pMovPosXField) MEFieldDestroy(pDoc->pMovPosXField);
	if (pDoc->pMovPosYField) MEFieldDestroy(pDoc->pMovPosYField);
	if (pDoc->pMovRotField) MEFieldDestroy(pDoc->pMovRotField);
	if (pDoc->pMovScaleXField) MEFieldDestroy(pDoc->pMovScaleXField);
	if (pDoc->pMovScaleYField) MEFieldDestroy(pDoc->pMovScaleYField);
	MEFieldDestroy(pDoc->pGlow0Field);
	MEFieldDestroy(pDoc->pGlow1Field);
	MEFieldDestroy(pDoc->pGlow2Field);
	MEFieldDestroy(pDoc->pGlow3Field);
	MEFieldDestroy(pDoc->pColorLinkField);
	MEFieldDestroy(pDoc->pMaterialLinkField);
	MEFieldDestroy(pDoc->pNoShadowField);
	MEFieldDestroy(pDoc->pCustomizeReflectField);
	MEFieldDestroy(pDoc->pReflect0Field);
	MEFieldDestroy(pDoc->pReflect1Field);
	MEFieldDestroy(pDoc->pReflect2Field);
	MEFieldDestroy(pDoc->pReflect3Field);
	MEFieldDestroy(pDoc->pCustomizeSpecularField);
	MEFieldDestroy(pDoc->pSpecular0Field);
	MEFieldDestroy(pDoc->pSpecular1Field);
	MEFieldDestroy(pDoc->pSpecular2Field);
	MEFieldDestroy(pDoc->pSpecular3Field);
	MEFieldDestroy(pDoc->pDyePackField);

	// Clean up groups
	for(i=eaSize(&pDoc->eaBodyScaleGroups)-1; i>=0; --i) {
		if (pDoc->eaBodyScaleGroups[i]->pScaleSliderField) {
			MEFieldDestroy(pDoc->eaBodyScaleGroups[i]->pScaleSliderField);
		}
		if (pDoc->eaBodyScaleGroups[i]->pScaleComboBox) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaBodyScaleGroups[i]->pScaleComboBox));
		}
		free(pDoc->eaBodyScaleGroups[i]);
	}
	eaDestroy(&pDoc->eaBodyScaleGroups);

	for(i=eaSize(&pDoc->eaBitsGroups)-1; i>=0; --i) {
		if (!gConf.bNewAnimationSystem) MEFieldDestroy(pDoc->eaBitsGroups[i]->pField); //original animation system version
		else ui_WidgetQueueFree(UI_WIDGET(pDoc->eaBitsGroups[i]->pComboBox)); //new animation system version
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaBitsGroups[i]->pRemoveButton));
		free(pDoc->eaBitsGroups[i]);
	}
	eaDestroy(&pDoc->eaBitsGroups);

	for(i=eaSize(&pDoc->eaFxGroups)-1; i>=0; --i) {
		MEFieldDestroy(pDoc->eaFxGroups[i]->pFxField);
		//MEFieldDestroy(pDoc->eaFxGroups[i]->pHueField);
		free(pDoc->eaFxGroups[i]);
	}
	eaDestroy(&pDoc->eaFxGroups);

	for(i=eaSize(&pDoc->eaColorGroups)-1; i>=0; --i) {
		if (pDoc->eaColorGroups[i]->pCheck) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pCheck));
		}
		if (pDoc->eaColorGroups[i]->pLabel) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pLabel));
		}
		if (pDoc->eaColorGroups[i]->pColorButton) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pColorButton));
		}
		if (pDoc->eaColorGroups[i]->pSlider1) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider1));
		}
		if (pDoc->eaColorGroups[i]->pSlider2) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider2));
		}
		if (pDoc->eaColorGroups[i]->pSlider3) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider3));
		}
		if (pDoc->eaColorGroups[i]->pSlider4) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSlider4));
		}
		if (pDoc->eaColorGroups[i]->pSubLabel1) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel1));
		}
		if (pDoc->eaColorGroups[i]->pSubLabel2) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel2));
		}
		if (pDoc->eaColorGroups[i]->pSubLabel3) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel3));
		}
		if (pDoc->eaColorGroups[i]->pSubLabel4) {
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaColorGroups[i]->pSubLabel4));
		}
		free(pDoc->eaColorGroups[i]);
	}
	eaDestroy(&pDoc->eaColorGroups);

	for(i=eaSize(&pDoc->eaFxSwapGroups)-1; i>=0; --i) {
		MEFieldDestroy(pDoc->eaFxSwapGroups[i]->pFxOldField);
		MEFieldDestroy(pDoc->eaFxSwapGroups[i]->pFxNewField);
		free(pDoc->eaFxSwapGroups[i]);
	}
	eaDestroy(&pDoc->eaFxSwapGroups);

	for(i=eaSize(&pDoc->eaTexGroups)-1; i>=0; --i) {
		MEFieldDestroy(pDoc->eaTexGroups[i]->pField);
		free(pDoc->eaTexGroups[i]);
	}
	eaDestroy(&pDoc->eaTexGroups);

	for(i=eaSize(&pDoc->eaScaleGroups)-1; i>=0; --i) {
		ui_WidgetQueueFree(UI_WIDGET(pDoc->eaScaleGroups[i]->pLabel));
		if (pDoc->eaScaleGroups[i]->pComboBox)
		{
			ui_WidgetQueueFree(UI_WIDGET(pDoc->eaScaleGroups[i]->pComboBox));
			if (eaSize(&pDoc->eaScaleGroups[i]->eaPresets) >= 1 && !stricmp(pDoc->eaScaleGroups[i]->eaPresets[0]->pcName,"Choose a Preset"))
			{
				eaRemove(&pDoc->eaScaleGroups[i]->eaPresets, 0);
			}
			eaDestroyStruct(&pDoc->eaScaleGroups[i]->eaPresets, parse_PCPresetScaleValueGroup);
		}
		if (pDoc->eaScaleGroups[i]->pSliderField) {
			MEFieldDestroy(pDoc->eaScaleGroups[i]->pSliderField);
		}
		free(pDoc->eaScaleGroups[i]);
	}
	eaDestroy(&pDoc->eaScaleGroups);

	for(i=eaSize(&pDoc->eaTexWordsGroups)-1; i>=0; --i) {
		MEFieldDestroy(pDoc->eaTexWordsGroups[i]->pField);
		free(pDoc->eaTexWordsGroups[i]);
	}
	eaDestroy(&pDoc->eaTexWordsGroups);

	// Clean up the windows
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pMainWindow));
	ui_WidgetQueueFree(UI_WIDGET(pDoc->pDetailWindow));

	// Clean up temp data
	eaDestroy(&pDoc->eaSkels);
	eaDestroy(&pDoc->eaStances);
	eaDestroy(&pDoc->eaBones);
	eaDestroy(&pDoc->eaLayers);
	eaDestroy(&pDoc->eaGeos);
	eaDestroy(&pDoc->eaMats);
	eaDestroy(&pDoc->eaPatternTex);
	eaDestroy(&pDoc->eaDetailTex);
	eaDestroy(&pDoc->eaSpecularTex);
	eaDestroy(&pDoc->eaDiffuseTex);
	eaDestroy(&pDoc->eaMovableTex);

	// Clean up the costume
	StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pCostume);
	StructDestroyNoConstSafe(parse_PlayerCostume, &pDoc->pOrigCostume);
}

bool old_disable_sky_volume_value;

void costumeEdit_GotFocus(CostumeEditDoc *pDoc)
{
	motGotFocus(gCostumeMOTToolbar);

	old_disable_sky_volume_value = gfxGetDisableVolumeSkies();
	gfxSetDisableVolumeSkies(true);
	// Update in case display name status changed
	costumeEdit_UpdateDisplayNameStatus(pDoc);
	costumeEdit_UpdateLists(pDoc);
	costumeView_RegenCostume(pDoc->pGraphics, (PlayerCostume*)pDoc->pCostume, NULL);

	if (gDefDoc) {
		costumeDefEdit_SetSkeleton(gDefDoc, GET_REF(pDoc->pCostume->hSkeleton));
		gDefDoc->pCurrentBoneDef = pDoc->pPrevSelectedBoneDef;
		gDefDoc->pCurrentGeoDef = pDoc->pPrevSelectedGeoDef;
		gDefDoc->pOrigGeoDef = pDoc->pPrevSelectedOrigGeoDef;
		gDefDoc->pCurrentMatDef = pDoc->pPrevSelectedMatDef;
		gDefDoc->pOrigMatDef = pDoc->pPrevSelectedOrigMatDef;
		costumeDefEdit_DefUpdateLists(gDefDoc);
	}
}


void costumeEdit_LostFocus(CostumeEditDoc *pDoc)
{
	gfxSetDisableVolumeSkies(old_disable_sky_volume_value);
	if(gDefDoc) {
		pDoc->pPrevSelectedBoneDef = gDefDoc->pCurrentBoneDef;
		pDoc->pPrevSelectedGeoDef = gDefDoc->pCurrentGeoDef;
		pDoc->pPrevSelectedOrigGeoDef = gDefDoc->pOrigGeoDef;
		pDoc->pPrevSelectedMatDef = gDefDoc->pCurrentMatDef;
		pDoc->pPrevSelectedOrigMatDef = gDefDoc->pOrigMatDef;
		gDefDoc->pCurrentBoneDef = NULL;
		gDefDoc->pCurrentGeoDef = NULL;
		gDefDoc->pOrigGeoDef = NULL;
		gDefDoc->pCurrentMatDef = NULL;
		gDefDoc->pOrigMatDef = NULL;
	}

	motLostFocus(gCostumeMOTToolbar);
	costumeView_StopFx(pDoc->pGraphics);
}


//---------------------------------------------------------------------------------------------------
// Graphics Logic
//---------------------------------------------------------------------------------------------------


// This is run once per tick for drawing ghosts
void costumeEdit_DrawGhosts(CostumeEditDoc *pDoc)
{
	U32 iModelMemory, iMaterialMemory;

	// Disable indoor rendering for the costume editor. Otherwise the
	// sun will be excluded from whatever sky file is selected in the
	// editor.
	bool oldAllowIndoors = gfxGetCurrentActionAllowIndoors();
	gfxSetCurrentActionAllowIndoors(false);

	// Check if assets changed and UI needs updating
	costumeEdit_TickCheckChanges(pDoc);

	costumeView_DrawGhosts(pDoc->pGraphics);

	// Restore indoor lighting setting.
	gfxSetCurrentActionAllowIndoors(oldAllowIndoors);

	gfxGetSkeletonMemoryUsage(pDoc->pGraphics->costume.pDrawSkel, &iModelMemory, &iMaterialMemory);
	if (iModelMemory != pDoc->pGraphics->costume.iModelMemory || iMaterialMemory != pDoc->pGraphics->costume.iMaterialMemory) {
		char buf1[100], buf2[100];
		pDoc->pGraphics->costume.iModelMemory = iModelMemory;
		pDoc->pGraphics->costume.iMaterialMemory = iMaterialMemory;
		emStatusPrintf("Total geometry memory: %s    Total material memory: %s", friendlyBytesBuf(iModelMemory, buf1), friendlyBytesBuf(iMaterialMemory, buf2));
	}

	if( pDoc->pCostume && ugcResourceGetInfo( "PlayerCostume", pDoc->pCostume->pcName )) {
		F32 x, y, w, h;
		emGetCanvasSize( &x, &y, &w, &h );
		gfxfont_SetFontEx( &g_font_Sans, 0, 1, 1, 0, 0xBB0000FF, 0xBB0000FF );
		gfxfont_Printf( x + w / 2, y + 60, 100, 2, 2, CENTER_X, "%s is tagged for UGC", pDoc->pCostume->pcName );
	}
}

// This is run once per tick for drawing
void costumeEdit_Draw(CostumeEditDoc *pDoc)
{
	Vec3 v3Rot = {0, 0, 0};
	Quat qRot;

	// update the rotation value based on the current yaw speed
	quatToPYR(pDoc->pGraphics->costume.qSkelRot, v3Rot);
	v3Rot[1] += ROTATION_SPEED * gYawSpeed * gGCLState.frameElapsedTime;
	PYRToQuat(v3Rot, qRot);

	costumeView_SetRot(pDoc->pGraphics, qRot);

	costumeView_Draw(pDoc->pGraphics);

	if(EditorPrefGetInt(COSTUME_EDITOR, "Option", "ShowHeightRuler", 0))
		costumeView_DrawHeightRuler(SAFE_MEMBER(pDoc->pCostume, fHeight));
}

#endif

#include "CostumeEditor.h"
#include "AutoGen/CostumeCommonEnums_h_ast.h"
#include "AutoGen/CostumeCommon_h_ast.h"
#include "AutoGen/CostumeEditor_h_ast.c"
