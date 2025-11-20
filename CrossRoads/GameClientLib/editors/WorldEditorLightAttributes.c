#ifndef NO_EDITORS

#include "WorldEditorLightAttributes.h"
#include "WorldEditorAttributesPrivate.h"
#include "EditorManager.h"
#include "WorldGrid.h"
#include "EditLibUIUtil.h"
#include "WorldEditorUtil.h"
#include "wlLight.h"
#include "MultiEditFieldContext.h"
#include "StringCache.h"
#include "WorldLib.h"

#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorOperations.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

#include "autogen/WorldEditorLightAttributes_c_ast.h"

AUTO_STRUCT;
typedef struct LightPropsUI
{
	WleAELightType eLightType;				AST(NAME("LightType"))

	AST_STOP

	WorldLightProperties pPropCopy;		

	int iCommonLightType;

	int iHasParent;
	int iHasChild;

	GroupTracker *pTracker;

	F32 fMinOutRad;
	F32 fMaxInRad;

	F32 fMinOutAng1;
	F32 fMaxInAng1;
	F32 fMinOutAng2;
	F32 fMaxInAng2;

} LightPropsUI;
static LightPropsUI g_LightPropsUI = {0};

static void wleAELightPropRemove(GroupProperties *pNewProps, const char *pchFieldName)
{
	char pchPath[256];
	const WorldLightProperties *pDefaultProps = groupGetDefaultLightProperties();
	sprintf(pchPath, ".%s", pchFieldName);
	StructCopyField(parse_WorldLightProperties, pDefaultProps, pNewProps->light_properties, pchPath, 0, 0, 0);
	eaFindAndRemove(&pNewProps->light_properties->ppcSetFields, allocAddString(pchFieldName));
}

int wleAELightPropIsSet(WorldLightProperties **ppProps, const char *pchPropName)
{
	int i;
	int ret = WL_VAL_UNSET;
	for ( i=0; i < eaSize(&ppProps); i++ ) {
		wleAEWLVALSetFromBool(&ret, lightPropertyIsSet(ppProps[i], pchPropName));
	}
	return ret;
}

static void wleAELightPropsChangedCB(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	if(pField) {
		char pchPath[256];
		sprintf(pchPath, ".%s", pField->pchFieldName);
		StructCopyField(parse_WorldLightProperties, &g_LightPropsUI.pPropCopy, pNewProps->light_properties, pchPath, 0, 0, 0);
		eaPushUnique(&pNewProps->light_properties->ppcSetFields, allocAddString(pField->pchFieldName));
	}
	pNewProps->light_properties->eLightType = g_LightPropsUI.eLightType;

	if(g_LightPropsUI.eLightType != WleAELightController)
	{
		if (g_LightPropsUI.eLightType != WleAELightDirectional)
		{
			wleAELightPropRemove(pNewProps, "LightIsSun");
			wleAELightPropRemove(pNewProps, "LightSecondaryDiffuseHSV");
			wleAELightPropRemove(pNewProps, "LightSecondaryDiffuseMultiplier");
			wleAELightPropRemove(pNewProps, "LightSecondaryDiffuseOffset");
			wleAELightPropRemove(pNewProps, "LightShadowColorHSV");
			wleAELightPropRemove(pNewProps, "LightShadowColorMultiplier");
			wleAELightPropRemove(pNewProps, "LightShadowColorOffset");
		}
		if (g_LightPropsUI.eLightType != WleAELightSpot && 
			g_LightPropsUI.eLightType != WleAELightProjector)
		{
			wleAELightPropRemove(pNewProps, "LightConeInner");
			wleAELightPropRemove(pNewProps, "LightConeOuter");
			wleAELightPropRemove(pNewProps, "LightShadowNearDist");
		}
		if (g_LightPropsUI.eLightType != WleAELightProjector)
		{
			wleAELightPropRemove(pNewProps, "LightCone2Inner");
			wleAELightPropRemove(pNewProps, "LightCone2Outer");
			wleAELightPropRemove(pNewProps, "LightProjectedTexture");
		}
		if (!pNewProps->light_properties->bIsKey) 
		{
			wleAELightPropRemove(pNewProps, "LightCastsShadows");
			wleAELightPropRemove(pNewProps, "LightSpecularHSV");
			wleAELightPropRemove(pNewProps, "LightSpecularMultiplier");
			wleAELightPropRemove(pNewProps, "LightSpecularOffset");
		}
		if (!pNewProps->light_properties->bIsKey ||
			!pNewProps->light_properties->bCastsShadows ||
			g_LightPropsUI.eLightType != WleAELightDirectional)
		{
			wleAELightPropRemove(pNewProps, "LightInfiniteShadows");
			wleAELightPropRemove(pNewProps, "LightCloudTexture");
			wleAELightPropRemove(pNewProps, "LightCloudMultiplier1");
			wleAELightPropRemove(pNewProps, "LightCloudScale1");
			wleAELightPropRemove(pNewProps, "LightCloudScrollX1");
			wleAELightPropRemove(pNewProps, "LightCloudScrollY1");
			wleAELightPropRemove(pNewProps, "LightCloudMultiplier2");
			wleAELightPropRemove(pNewProps, "LightCloudScale2");
			wleAELightPropRemove(pNewProps, "LightCloudScrollX2");
			wleAELightPropRemove(pNewProps, "LightCloudScrollY2");
		}
	}
}

static void wleAELightTypeChangedCB(MEField *pField, bool bFinished, WleAEPropStructData *pPropData)
{
	if(MEContextExists())
		return;
	if(g_LightPropsUI.eLightType == WleAELightNone) {
		wleAERemovePropsToSelection(NULL, pPropData);
	} else {
		wleAEAddPropsToSelection(NULL, pPropData);
		wleAECallFieldChangedCallback(NULL, wleAELightPropsChangedCB);
	}
}

static void wleAELightPropsUnspecify(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	wleAELightPropRemove(pNewProps, pField->pchFieldName);
}

static void wleAELightPropsUnspecifyCB(UIButton *pButton, MEFieldContextEntry *pEntry)
{
	MEField *pField = ENTRY_FIELD(pEntry);
	wleAECallFieldChangedCallback(pField, wleAELightPropsUnspecify);
}

static void wleAELightPropsSpecify(MEField *pField, GroupTracker *pTracker, const GroupProperties *pOldProps, GroupProperties *pNewProps)
{
	eaPushUnique(&pNewProps->light_properties->ppcSetFields, allocAddString(pField->pchFieldName));
}

static void wleAELightPropsSpecifyCB(UIButton *pButton, MEFieldContextEntry *pEntry)
{
	MEField *pField = ENTRY_FIELD(pEntry);
	wleAECallFieldChangedCallback(pField, wleAELightPropsSpecify);
}

static void wleAELightPropsGotoParentCB(UIButton *pButton, GroupTracker *sourceTracker)
{
	EditorObject *pObj = editorObjectCreate(trackerHandleCreate(sourceTracker), sourceTracker->def->name_str, sourceTracker->parent_layer, EDTYPE_TRACKER);
	editorObjectRef(pObj);
	edObjSelect(pObj, true, false);
	editorObjectDeref(pObj);
}

static void wleAELightFieldAddedCB(MEFieldContextEntry *pEntry, void *pUserData)
{
	MEField *pField = ENTRY_FIELD(pEntry);
	WorldLightProperties *pProp;
	bool bHasParent = g_LightPropsUI.iHasParent == WL_VAL_TRUE;
	bool bHasChild = g_LightPropsUI.iHasChild == WL_VAL_TRUE;

	assert(g_LightPropsUI.pTracker && pField && pField->pTable == parse_WorldLightProperties);
	pProp = g_LightPropsUI.pTracker->def->property_structs.light_properties;

	if(pField->eType == kMEFieldType_Color) {
		pField->pUIColor->noAlpha = true;
		pField->pUIColor->noRGB = true;
		pField->pUIColor->bForceHDR = true;
		pField->pUIColor->liveUpdate = true;
		pField->pUIColor->min = -25;
		pField->pUIColor->max = 25;
		pField->eColorType = kMEFieldColorType_HSV;
	}

	if(bHasParent || bHasChild) {
		const char *pchToolTip;
		if(lightPropertyIsSet(pProp, pField->pchFieldName)) {
			if(bHasParent)
				pchToolTip = "Currently overriding parent, click to inherit from parent or default.";
			else 
				pchToolTip = "Children without overrides currently use this value, click to make them use defaults.";
			MEContextEntryAddActionButton(pEntry, NULL, "eui_icon_linkbroken", wleAELightPropsUnspecifyCB, pEntry, 0, pchToolTip);
		} else {
			bool bParentHasProps;
			GroupTracker *pParent;
			if(bHasParent)
				pchToolTip = "Currently inheriting from parent or default, click to override.";
			else 
				pchToolTip = "Children currently use default settings, click to make them use this value.";
			MEContextEntryAddActionButton(pEntry, NULL, "eui_icon_link", wleAELightPropsSpecifyCB, pEntry, 0, pchToolTip);

			if(bHasParent) {
				pParent = g_LightPropsUI.pTracker;
				bParentHasProps = false;
				while(pParent->parent) {
					GroupTracker *pNextParent = pParent->parent;
					if(!pNextParent->def || !pNextParent->def->property_structs.light_properties)
						break;
					pParent = pNextParent;
					if(lightPropertyIsSet(pParent->def->property_structs.light_properties, pField->pchFieldName)) {
						bParentHasProps = true;
						break;
					}
				}

				if(bParentHasProps) {
					char pchPath[256];
					sprintf(pchPath, ".%s", pField->pchFieldName);
					StructCopyField(parse_WorldLightProperties, pParent->def->property_structs.light_properties, &g_LightPropsUI.pPropCopy, pchPath, 0, 0, 0);
					MEContextEntryAddActionButton(pEntry, NULL, "button_center", wleAELightPropsGotoParentCB, pParent, 0, "Click to select the parent from which we are inheriting this value.");
					MEContextEntryMakeHighlighted(pEntry, WL_INHERITED_BG_COLOR);
				}
			}
		}
	}
}

bool wleAELightExcludeCB(GroupTracker *pTracker)
{
	int i;
	GroupDef *pDef = pTracker->def;
	WleAELightType eLightType;
	bool bHasChild = false;
	WorldLightProperties *pProp;
	if(wleNeedsEncounterPanels(pDef) || pDef->property_structs.physical_properties.bOnlyAVolume)
		return true;

	g_LightPropsUI.pTracker = pTracker;
	pProp = pDef->property_structs.light_properties;
	
	eLightType = (pProp ? pProp->eLightType : WleAELightNone);
	if(	g_LightPropsUI.iCommonLightType == WL_VAL_UNSET) {
		g_LightPropsUI.iCommonLightType = eLightType;
	} else if (g_LightPropsUI.iCommonLightType != eLightType) {
		g_LightPropsUI.iCommonLightType = WL_VAL_DIFF;
	}

	if(pProp) {
		MAX1(g_LightPropsUI.fMaxInRad, pProp->fRadiusInner);
		MIN1(g_LightPropsUI.fMinOutRad, pProp->fRadius);

		MAX1(g_LightPropsUI.fMaxInAng1, pProp->fConeInner);
		MIN1(g_LightPropsUI.fMinOutAng1, pProp->fConeOuter);
		MAX1(g_LightPropsUI.fMaxInAng2, pProp->fCone2Inner);
		MIN1(g_LightPropsUI.fMinOutAng2, pProp->fCone2Outer);
	}

	for ( i=0; i < pTracker->child_count; i++ ) {
		if(pTracker->children[i]->def && pTracker->children[i]->def->property_structs.light_properties) {
			bHasChild = true;
			break;
		}
	}
	wleAEWLVALSetFromBool(&g_LightPropsUI.iHasChild, bHasChild);
	wleAEWLVALSetFromBool(&g_LightPropsUI.iHasParent, (pTracker->parent && 
														pTracker->parent->def && 
														pTracker->parent->def->property_structs.light_properties));

	return false;
}

int wleAELightReload(EMPanel *panel, EditorObject *edObj)
{
	static WleAEPropStructData pPropData = {"LightProperties", parse_WorldLightProperties};
	Vec3 oMin = {-360, -1, -10}, oMax=  {360, 1, 10}, oStep={1.f, 0.1f, 0.1f};
	WorldLightProperties **ppProps = NULL;
	WorldLightProperties *pProp = NULL;
	MEFieldContext *pContext;
	MEFieldContext *pTypeContext;
	U32 iRetFlags;

	g_LightPropsUI.pTracker = NULL;
	g_LightPropsUI.iHasChild = WL_VAL_UNSET;
	g_LightPropsUI.iHasParent = WL_VAL_UNSET;
	g_LightPropsUI.iCommonLightType = WL_VAL_UNSET;
	g_LightPropsUI.fMinOutRad = FLT_MAX;
	g_LightPropsUI.fMinOutAng1 = FLT_MAX;
	g_LightPropsUI.fMinOutAng2 = FLT_MAX;
	g_LightPropsUI.fMaxInRad = 0;
	g_LightPropsUI.fMaxInAng1 = 0;
	g_LightPropsUI.fMaxInAng2 = 0;
	ppProps = (WorldLightProperties**)wleAEGetSelectedDataFromPath("LightProperties", wleAELightExcludeCB, &iRetFlags);
	if(iRetFlags & WleAESelectedDataFlags_Failed)
		return WLE_UI_PANEL_INVALID;

	if(eaSize(&ppProps) > 0 && !(iRetFlags & WleAESelectedDataFlags_SomeMissing)) {
		pProp = ppProps[0];
		StructCopy(parse_WorldLightProperties, pProp, &g_LightPropsUI.pPropCopy, 0, 0, 0);
	}

	if(g_LightPropsUI.iCommonLightType >= 0)
		g_LightPropsUI.eLightType = g_LightPropsUI.iCommonLightType;
	else
		g_LightPropsUI.eLightType = WleAELightNone;

	pContext = MEContextPush("WorldEditor_LightProperties", &g_LightPropsUI.pPropCopy, &g_LightPropsUI.pPropCopy, parse_WorldLightProperties);
	pContext->pUIContainer = emPanelGetUIContainer(panel);
	pContext->bDontSortComboEnums = true;
	pContext->cbFieldAdded = NULL;
	wleAEAddFieldChangedCallback(pContext, NULL);

	pTypeContext = MEContextPush("LightType", &g_LightPropsUI.eLightType, &g_LightPropsUI.eLightType, parse_LightPropsUI);
	pTypeContext->cbChanged = wleAELightTypeChangedCB;
	pTypeContext->pChangedData = &pPropData;
	MEContextAddEnum(kMEFieldType_Combo, WleAELightTypeEnum, "LightType", "Light Type", "The type of light.");
	MEContextPop("LightType");

	if(pProp && g_LightPropsUI.eLightType != WleAELightNone) {
		int iType = g_LightPropsUI.iCommonLightType;
		bool bIsKey = (!MEContextFieldDiff("LightIsKey") && pProp->bIsKey);
		bool bCastsShadows = (bIsKey && !MEContextFieldDiff("LightCastsShadows") && pProp->bCastsShadows);
		bool isControler = (iType == WleAELightController);
		bool bHasParent = g_LightPropsUI.iHasParent == WL_VAL_TRUE;

		wleAEAddFieldChangedCallback(pContext, wleAELightPropsChangedCB);
		if(eaSize(&ppProps) == 1)
			pContext->cbFieldAdded = wleAELightFieldAddedCB;

		MEContextAddEnum(kMEFieldType_Combo, LightAffectTypeEnum,							"LightAffects",						"Affected Types",		"The types of objects this light affects.");
		MEContextAddSimple(kMEFieldType_Check,												"LightIsKey",						"Key Light",			"Key lights are rendered into the scene at runtime and can cast shadows and create specular highlights.");
		MEContextAddSimple(kMEFieldType_Check,												"LightIsSun",						"Is Sun",				"Sun lights only affect outdoor objects and also use the character lighting offsets from the sky file.");
	
		if(	isControler || bIsKey )
			MEContextAddSimple(kMEFieldType_Check,											"LightCastsShadows",				"Casts Shadows",		"Shadow casting lights are more expensive.");
		
		if(	isControler || (iType == WleAELightDirectional && bCastsShadows))
			MEContextAddSimple(kMEFieldType_Check,											"LightInfiniteShadows",				"Infinite Shadows",		"If this is not set, the light casts shadows only from the position of the light.");
		
		if(	isControler || iType == WleAELightSpot || iType == WleAELightProjector)
			MEContextAddMinMax(kMEFieldType_Spinner, 0, g_LightPropsUI.fMinOutRad, 0.5f,		"LightShadowNearDist",				"Shadow Dist",			"Distance from the light source at which shadows start casting.  Also affects the midpoint of the light used for querying rooms and indoor volumes.");

		MEContextAddLabel("Radius", "Radius:", NULL);
		MEContextIndentRight();
		MEContextAddMinMax(kMEFieldType_Spinner, 0, g_LightPropsUI.fMinOutRad, 0.5f,			"LightRadiusInner",					"Inner",				"Inside the inner radius the light color is constant.");
		MEContextAddMinMax(kMEFieldType_Spinner, g_LightPropsUI.fMaxInRad, 5000, 0.5f,		"LightRadius",						"Outer",				"Between the inner radius and outer radius the light color falls off.");
		MEContextIndentLeft();

		if (iType != WleAELightProjector) {
			MEContextAddSimple(kMEFieldType_Color,											"LightAmbientHSV",					"Ambient Color",		"Light ambient color.");
			if (bHasParent && wleAELightPropIsSet(ppProps, "LightAmbientHSV") == WL_VAL_FALSE) {
				MEFieldContextEntry *pEntry;
				MEContextIndentRight();
				MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 50.0f, 0.1f,				"LightAmbientMultiplier",			"Ambient Multiplier",	"Light ambient color multiplier.");
				pEntry = MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 0, 0,				"LightAmbientOffset",				"Ambient Offset",		"Light ambient color offset.");
				ui_MultiSpinnerEntrySetVecBounds(ENTRY_FIELD(pEntry)->pUIMultiSpinner, oMin, oMax, oStep);
				MEContextIndentLeft();
			}
		}

		{
			MEContextAddSimple(kMEFieldType_Color,											"LightDiffuseHSV",					"Diffuse Color",		"Light diffuse color.");
			if (bHasParent && wleAELightPropIsSet(ppProps, "LightDiffuseHSV") == WL_VAL_FALSE) {
				MEFieldContextEntry *pEntry;
				MEContextIndentRight();
				MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 50.0f, 0.1f,				"LightDiffuseMultiplier",			"Diffuse Multiplier",	"Light diffuse color multiplier.");
				pEntry = MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 0, 0,				"LightDiffuseOffset",				"Diffuse Offset",		"Light diffuse color offset.");
				ui_MultiSpinnerEntrySetVecBounds(ENTRY_FIELD(pEntry)->pUIMultiSpinner, oMin, oMax, oStep);
				MEContextIndentLeft();
			}
		}

		if(isControler || iType == WleAELightDirectional) {
			MEContextAddSimple(kMEFieldType_Color,											"LightSecondaryDiffuseHSV",			"Back Diff. Color",		"Light diffuse color.");
			if (bHasParent && wleAELightPropIsSet(ppProps, "LightSecondaryDiffuseHSV") == WL_VAL_FALSE) {
				MEFieldContextEntry *pEntry;
				MEContextIndentRight();
				MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 50.0f, 0.1f,				"LightSecondaryDiffuseMultiplier",	"Back Diff. Multiplier","Light secondary diffuse color multiplier.");
				pEntry = MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 0, 0,				"LightSecondaryDiffuseOffset",		"Back Diff. Offset",	"Light secondary diffuse color offset.");
				ui_MultiSpinnerEntrySetVecBounds(ENTRY_FIELD(pEntry)->pUIMultiSpinner, oMin, oMax, oStep);
				MEContextIndentLeft();
			}
		}

		if(isControler || bIsKey) {
			MEContextAddSimple(kMEFieldType_Color,											"LightSpecularHSV",					"Specular Color",		"Light specular color.");
			if (bHasParent && wleAELightPropIsSet(ppProps, "LightSpecularHSV") == WL_VAL_FALSE) {
				MEFieldContextEntry *pEntry;
				MEContextIndentRight();
				MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 50.0f, 0.1f,				"LightSpecularMultiplier",			"Specular Multiplier",	"Light specular color multiplier.");
				pEntry = MEContextAddMinMax(kMEFieldType_MultiSpinner, 0, 0, 0,				"LightSpecularOffset",				"Specular Offset",		"Light specular color offset.");
				ui_MultiSpinnerEntrySetVecBounds(ENTRY_FIELD(pEntry)->pUIMultiSpinner, oMin, oMax, oStep);
				MEContextIndentLeft();
			}
		}

		if(isControler || iType == WleAELightSpot || iType == WleAELightProjector) {
			MEContextAddLabel("Angle", "Angle:", NULL);
			MEContextIndentRight();
			MEContextAddMinMax(kMEFieldType_Spinner, 0, g_LightPropsUI.fMinOutAng1, 0.5f,	"LightConeInner",				"Inner",				"Inside the inner angle the light color is constant.");
			MEContextAddMinMax(kMEFieldType_Spinner, g_LightPropsUI.fMaxInAng1, 89.5, 0.5f,	"LightConeOuter",				"Outer",				"Between the inner angle and the outer angle the light color falls off.");
			MEContextIndentLeft();
		}

		if(isControler || iType == WleAELightProjector) {
			MEContextAddLabel("Angle2", "Angle 2:", NULL);
			MEContextIndentRight();
			MEContextAddMinMax(kMEFieldType_Spinner, 0, g_LightPropsUI.fMinOutAng2, 0.5f,	"LightCone2Inner",				"Inner",				"Inside the inner angle the light color is constant.");
			MEContextAddMinMax(kMEFieldType_Spinner, g_LightPropsUI.fMaxInAng2, 89.5, 0.5f,	"LightCone2Outer",				"Outer",				"Between the inner angle and the outer angle the light color falls off.");
			MEContextIndentLeft();
			
			MEContextAddSimple(kMEFieldType_Texture,										"LightProjectedTexture",			"Projected Texture",	"The projected texture is multiplied by the light diffuse color.");
		}

		if(isControler || (iType == WleAELightDirectional && bCastsShadows)) {

			MEContextAddSimple(kMEFieldType_Texture,										"LightCloudTexture",				"Cloud Texture",		"The projected shadow texture.");

			MEContextAddLabel("CloudLayer1", "Cloud Layer 1:", NULL);
			MEContextIndentRight();
			MEContextAddMinMax(kMEFieldType_SliderText, -20,   20,    0.1f,					"LightCloudMultiplier1",			"Multiplier",			"Cloud layer 1 multiplier (can be negative).");
			MEContextAddMinMax(kMEFieldType_SliderText, 0.1,   10000, 0.1f,					"LightCloudScale1",					"Scale",				"Cloud layer 1 texture coord scale.");
			MEContextAddMinMax(kMEFieldType_SliderText, -1000, 1000,  0.1f,					"LightCloudScrollX1",				"Scroll X",				"Cloud layer 1 scroll rate, X axis.");
			MEContextAddMinMax(kMEFieldType_SliderText, -1000, 1000,  0.1f,					"LightCloudScrollY1",				"Scroll Y",				"Cloud layer 1 scroll rate, Y axis.");
			MEContextIndentLeft();

			MEContextAddLabel("CloudLayer2", "Cloud Layer 2:", NULL);
			MEContextIndentRight();
			MEContextAddMinMax(kMEFieldType_SliderText, -20,   20,    0.1f,					"LightCloudMultiplier2",			"Multiplier",			"Cloud layer 2 multiplier (can be negative).");
			MEContextAddMinMax(kMEFieldType_SliderText, 0.1,   10000, 0.1f,					"LightCloudScale2",					"Scale",				"Cloud layer 2 texture coord scale.");
			MEContextAddMinMax(kMEFieldType_SliderText, -1000, 1000,  0.1f,					"LightCloudScrollX2",				"Scroll X",				"Cloud layer 2 scroll rate, X axis.");
			MEContextAddMinMax(kMEFieldType_SliderText, -1000, 1000,  0.1f,					"LightCloudScrollY2",				"Scroll Y",				"Cloud layer 2 scroll rate, Y axis.");
			MEContextIndentLeft();
		}

		MEContextAddMinMax(kMEFieldType_SliderText, 0.0f, 10.0f, 0.01f,						"LightVisualLODScale",				"LOD Scale",			"Increases or decreases maximum visible distance of the light.");
 	}

	MEContextAddSpacer();
	emPanelSetHeight(panel, pContext->iYPos);
	emPanelSetActive(panel, !(iRetFlags & WleAESelectedDataFlags_Inactive));

	MEContextPop("WorldEditor_LightProperties");

	return (pProp ? WLE_UI_PANEL_OWNED : WLE_UI_PANEL_UNOWNED);	
}

#include "autogen/WorldEditorLightAttributes_c_ast.c"

#endif