#ifndef __WORLDEDITORAPPEARANCEATTRIBUTES_H__
#define __WORLDEDITORAPPEARANCEATTRIBUTES_H__
GCC_SYSTEM

#ifndef NO_EDITORS

#include "StashTable.h"
#include "WorldEditorAttributesHelpers.h"
#include "WorldEditorAppearanceSwaps.h"
#include "UIAutoWidget.h"
#include "UIFXButton.h"

typedef struct EMPanel EMPanel;
typedef struct EditorObject EditorObject;
typedef struct GroupTracker GroupTracker;

typedef struct UIList UIList;
typedef struct UIComboBox UIComboBox;
typedef struct Model Model;
typedef struct WorldPlanetProperties WorldPlanetProperties;
typedef struct GroupDef GroupDef;
typedef struct Material Material;
typedef struct TextureSwap TextureSwap;

typedef enum eDynParamType eDynParamType;

typedef struct WleAEFXParam {
	eDynParamType type;
	const char *name;
	WleAEParamCombo *fx;
	union {
		WleAEParamText  paramText;
		WleAEParamFloat paramFloat;
		WleAEParamVec3  paramVec3;
		WleAEParamDictionary paramDict;
	};
} WleAEFXParam;

typedef struct WleAEAppearanceUI
{
	EMPanel *panel;
	UIRebuildableTree *autoWidget;
	UIScrollArea *scrollArea;

	bool allowOffsets;

	WleAESwapUI swaps;

	struct
	{
		WleAEParamHSV tint0;
		WleAEParamVec3 tint0Offset;
		WleAEParamHSV tint1;
		WleAEParamHSV tint2;
		WleAEParamHSV tint3;

		WleAEParamFloat alpha;

		UIFXButton *pFXButton;
		WleAEParamCombo fx;
		WleAEParamCombo fxCondition;
		WleAEParamHue fxHueShift;
		WleAEParamBool fxHasTarget;
		WleAEParamBool fxTargetNoAnim;
		WleAEParamVec3 fxTargetPos;
		WleAEParamVec3 fxTargetPyr;
		WleAEFXParam **fxParams;
		WleAEParamDictionary fxFaction;

		WleAEParamBool noVertexLighting;
		WleAEParamBool useCharacterLighting;
		WleAEParamBool hasAnimation;
		WleAEParamBool localSpace;
		WleAEParamVec3 translationAmount;
		WleAEParamVec3 translationTime;
		WleAEParamBool translationLoop;
		WleAEParamVec3 swayAngle;
		WleAEParamVec3 swayTime;
		WleAEParamVec3 rotationTime;
		WleAEParamVec3 scaleAmount;
		WleAEParamVec3 scaleTime;
		WleAEParamBool randomTimeOffset;
		WleAEParamFloat timeOffset;

	} data;

	//for FX
	char **triggerConditionNames;
} WleAEAppearanceUI;

AUTO_STRUCT;
typedef struct MaterialTextureAssoc
{
	const char *orig_name;			AST(POOL_STRING)
	const char *replace_name;		AST(POOL_STRING)

	TextureSwap **textureSwaps;
} MaterialTextureAssoc;

extern ParseTable parse_MaterialTextureAssoc[];
#define TYPE_parse_MaterialTextureAssoc MaterialTextureAssoc

/********************
* UTIL
********************/
void wleGetModelTexMats(Model *model, int lod_idx, MaterialTextureAssoc ***assocsOut, bool applySwaps);
void wleGetPlanetTexMats(WorldPlanetProperties *planet, MaterialTextureAssoc ***assocsOut);
void wleGetTrackerTexMats(GroupTracker *tracker, StashTable matStash, StashTable texStash, MaterialTextureAssoc ***matTexAssocsOut, bool applySwaps);

/********************
* MAIN
********************/
int wleAEAppearanceReload(EMPanel *panel, EditorObject *edObj);
void wleAEAppearanceCreate(EMPanel *panel);

void wleAEAppearanceUICreate(EMPanel *panel, WleAEAppearanceUI *ui);
void wleAEAppearanceRebuildUI(WleAEAppearanceUI *ui, StashTable materials, StashTable textures, EditorObject *edObj);

#endif // NO_EDITORS

#endif // __WORLDEDITORAPPEARANCEATTRIBUTES_H__