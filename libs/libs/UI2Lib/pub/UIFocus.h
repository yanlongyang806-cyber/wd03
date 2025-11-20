#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef UI_FOCUS_H
#define UI_FOCUS_H

typedef struct UIWidget UIWidget;

SA_RET_OP_VALID UIWidget *ui_FocusNextInGroup(SA_PARAM_NN_VALID UIWidget *groupWidget, SA_PARAM_NN_VALID UIWidget *oldFocus);
SA_RET_OP_VALID UIWidget *ui_FocusPrevInGroup(SA_PARAM_NN_VALID UIWidget *groupWidget, SA_PARAM_NN_VALID UIWidget *oldFocus);

SA_RET_OP_VALID UIWidget *ui_FocusUp(SA_PARAM_OP_VALID UIWidget *pWidget, SA_PARAM_NN_VALID UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight);
SA_RET_OP_VALID UIWidget *ui_FocusDown(SA_PARAM_OP_VALID UIWidget *pWidget, SA_PARAM_NN_VALID UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight);
SA_RET_OP_VALID UIWidget *ui_FocusLeft(SA_PARAM_OP_VALID UIWidget *pWidget, SA_PARAM_NN_VALID UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight);
SA_RET_OP_VALID UIWidget *ui_FocusRight(SA_PARAM_OP_VALID UIWidget *pWidget, SA_PARAM_NN_VALID UIWidgetGroup *pGroup, F32 fParentWidth, F32 fParentHeight);

#endif
