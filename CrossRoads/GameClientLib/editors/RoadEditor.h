#pragma once
GCC_SYSTEM

typedef struct UIMenu UIMenu;
typedef struct EditorObject EditorObject;
typedef struct ZoneMapLayer ZoneMapLayer;

void layerNewRoad(UIMenu *menu, ZoneMapLayer *layer);

void roadUISetSwaps(EditorObject *object, const char * const *texSwaps, const char * const *matSwaps);
void roadUISetTints(EditorObject *object, bool enable, const Vec3 tint1, const Vec3 tint2);

void roadUISetRoadParameters(UIComboBox *combo, void *userdata);
