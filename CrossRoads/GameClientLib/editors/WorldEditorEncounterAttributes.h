#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EditorObject EditorObject;
typedef struct EMPanel EMPanel;
typedef struct EncounterActorProperties EncounterActorProperties;
typedef struct EncounterTemplate EncounterTemplate;
typedef struct WorldActorProperties WorldActorProperties;
typedef struct UIRebuildableTreeNode UIRTNode;

int wleAEEncounterReload(EMPanel *panel, EditorObject *edObj);
void wleAEEncounterCreate(EMPanel *panel);

void wleAEEncounterShowActor(UIRTNode *pRoot, EncounterTemplate *pTemplate, EncounterActorProperties *pActor, WorldActorProperties *pWActor, int i, int iAlign);

#endif // NO_EDITORS