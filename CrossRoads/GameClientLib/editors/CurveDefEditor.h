#pragma once
GCC_SYSTEM
#ifndef NO_EDITORS

typedef struct UIWindow UIWindow;
typedef struct UITextEntry UITextEntry;
typedef struct UIList UIList;
typedef struct UIComboBox UIComboBox;
typedef struct UICheckButton UICheckButton;
typedef struct UIMenu UIMenu;
typedef struct CurveDef CurveDef;
typedef struct Curve Curve;

void roadRegenerateDef(CurveDef *def); // Regenerate all curves with def
void roadRegenerateRelated(Curve *curve); // Regenerate this curve and all attached curves
//void roadMarkUnsaved();

#endif