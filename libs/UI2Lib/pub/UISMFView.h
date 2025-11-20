/***************************************************************************



***************************************************************************/

#ifndef UI_SMFVIEW_H
#define UI_SMFVIEW_H
GCC_SYSTEM

#include "UICore.h"

typedef struct SMFTree		SMFTree;
typedef struct SMFBlock		SMFBlock;
typedef struct TextAttribs	TextAttribs;

//////////////////////////////////////////////////////////////////////////
// 
//		UISMFView
//	
//	  This widget is a similar to an "HTML" viewer.   It takes text, 
// parses it using an SMF Engine, then draws it.  In addition it handles
// mouse over and mouse click actions on the links inside.
//

typedef struct UISMFView
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);
	U8 maxAlpha;
	SMFBlock *pTree;
	UIActivationFunc reflowF;
	UserData reflowData;
	TextAttribs *pAttribs;

	bool drawBackground;
} UISMFView;


//Create an SMFView
SA_RET_NN_VALID UISMFView *ui_SMFViewCreate(U32 x, U32 y, U32 w, U32 h);

//Free an SMFView
void ui_SMFViewFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UISMFView *view);

//Change the text on this View
void ui_SMFViewSetText(SA_PARAM_NN_VALID UISMFView *view, SA_PARAM_NN_STR const char *txt, SA_PARAM_OP_VALID TextAttribs* attribs);

//Make the background draw
void ui_SMFViewSetDrawBackground(SA_PARAM_NN_VALID UISMFView *view, bool drawBackground);

//Reflow the text with this width
bool ui_SMFViewReflow(SA_PARAM_NN_VALID UISMFView *view, F32 w);

void ui_SMFViewSetMaxAlpha(SA_PARAM_NN_VALID UISMFView *view, U8 cAlpha);

//Draw the view
void ui_SMFViewDraw(SA_PARAM_NN_VALID UISMFView *view, UI_PARENT_ARGS);

void ui_SMFViewSetReflowCallback(SA_PARAM_NN_VALID UISMFView *view, UIActivationFunc reflowF, UserData reflowData);

void ui_SMFViewUpdateDimensions(SA_PARAM_NN_VALID UISMFView *view); // Updates widget->width, widget->height fields

F32 ui_SMFViewGetHeight(SA_PARAM_NN_VALID UISMFView *pView);

void ui_SMFViewTick(UISMFView *view, UI_PARENT_ARGS);

#endif
