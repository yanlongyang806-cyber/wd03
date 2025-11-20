#include "UILib.h"

// Useful macros
#define elUINextX(w) ((w)->widget.x + ((w)->widget.width * (w)->widget.scale))
#define elUINextY(w) ((w)->widget.y + ((w)->widget.height * (w)->widget.scale))

// Definitions
typedef void (*ElUIConfirmFunc)(UserData);
typedef bool (*ElUICheckFunc)(UserData);
typedef float (*ElUIProgressFunc)(UserData);

// Generic callbacks
void elUIListTextDisplay(UIList *list, UIListColumn *column, int row, UserData userData, char **output);
bool elUIWindowClose(UIWidget *thing, UIWindow *window);

// Useful functions
void elUICenterWindow(UIWindow *window);
int elUIGetEndX(UIWidgetGroup group);
int elUIGetEndY(UIWidgetGroup group);
void elUITreeExpandAll(UITreeNode *root);
bool elUITreeExpandToNode(UITreeNode *root, UserData searchContents);
UIButton *elUIAddCancelOkButtons(SA_PARAM_NN_VALID UIWindow *window, SA_PARAM_OP_VALID UIActivationFunc cancelF, UserData cancelData, SA_PARAM_OP_VALID UIActivationFunc okF, UserData okData);
void elUIWaitDialog(SA_PARAM_NN_STR const char *title, SA_PARAM_NN_STR const char *msg, int timeout, SA_PARAM_NN_VALID ElUICheckFunc checkFunc, UserData checkData, SA_PARAM_OP_VALID ElUIConfirmFunc execFunc, UserData execData, SA_PARAM_OP_VALID ElUIConfirmFunc timeoutFunc, UserData timeoutData);
void elUIProgressDialog(SA_PARAM_NN_STR const char *title, SA_PARAM_NN_STR const char *msg, SA_PARAM_NN_VALID ElUIProgressFunc progFunc, UserData progData, SA_PARAM_NN_VALID ElUIConfirmFunc execFunc, UserData execData, int timeout);
