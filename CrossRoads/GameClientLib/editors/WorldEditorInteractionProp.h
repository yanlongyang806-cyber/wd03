#ifndef __WORLDEDITORINTERACTIONPROP_H__
#define __WORLDEDITORINTERACTIONPROP_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;
typedef struct WorldInteractionPropertyEntry WorldInteractionPropertyEntry;
typedef struct UIRebuildableTreeNode UIRTNode;
typedef struct UIAutoWidgetParams UIAutoWidgetParams;

int wleAEInteractionPropReload(EMPanel *panel, EditorObject *edObj);
void wleAEInteractionPropCreate(EMPanel *panel);

void wleAEInteractionPropAdd(void *unused, void *unused2);
void wleAEInteractionPropRemove(void *unused, void *unused2);

void wleAEInteractionPropShowEntry(UIRTNode *pRoot, WorldInteractionPropertyEntry* pPropEntry, int index, const char* keyPrefix, UIAutoWidgetParams *baseParams);

/*void wleAEInteraction_ShowEntryClass(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams);
void wleAEInteraction_ShowEntryTiming(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams);
void wleAEInteraction_ShowEntryActions(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams);
void wlAEInteraction_ShowEntryRewards(UIRTNode *root, WorldInteractionPropertyEntry* pPropEntry, int iInteractionIndex, const char* keyPrefix, UIAutoWidgetParams* baseParams);*/

#endif // NO_EDITORS

#endif // __WORLDEDITORINTERACTIONPROP_H__