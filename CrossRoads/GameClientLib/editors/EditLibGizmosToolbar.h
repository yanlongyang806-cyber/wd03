#ifndef __EDITLIBGIZMOSTOOLBAR_H__
#define __EDITLIBGIZMOSTOOLBAR_H__
GCC_SYSTEM

#ifndef NO_EDITORS

typedef struct EditLibGizmosToolbar EditLibGizmosToolbar;
typedef struct TranslateGizmo TranslateGizmo;
typedef struct RotateGizmo RotateGizmo;
typedef struct UIScrollArea UIScrollArea;

typedef void (*EditLibGizmosToolbarCallback)(void*);

// Toolbar Main
SA_RET_NN_VALID EditLibGizmosToolbar *elGizmosToolbarCreate(SA_PARAM_OP_VALID EditLibGizmosToolbarCallback selectFunc, int height);
UIScrollArea *elGizmosToolbarGetWidget(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb);
void elGizmosToolbarUpdate(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb);
void elGizmosToolbarSetActiveGizmo(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb, SA_PARAM_NN_VALID void *gizmo);

// Translate Gizmo
void elGizmosToolbarAddTranslateGizmo(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb, SA_PARAM_NN_VALID TranslateGizmo *transGizmo, SA_PARAM_NN_STR const char *name, SA_PARAM_OP_VALID EditLibGizmosToolbarCallback changedFunc);
void elGizmosToolbarRemoveTranslateGizmo(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb, SA_PARAM_NN_VALID TranslateGizmo *transGizmo);

// Rotate Gizmo Toolbar
void elGizmosToolbarAddRotateGizmo(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb, SA_PARAM_NN_VALID RotateGizmo *rotGizmo, SA_PARAM_NN_STR const char *name, SA_PARAM_OP_VALID EditLibGizmosToolbarCallback changedFunc);
void elGizmosToolbarRemoveRotateGizmo(SA_PARAM_NN_VALID EditLibGizmosToolbar *tb, SA_PARAM_NN_VALID RotateGizmo *rotGizmo);

#endif // NO_EDITORS

#endif // __EDITLIBGIZMOSTOOLBAR_H__