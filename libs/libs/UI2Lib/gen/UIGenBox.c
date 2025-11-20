#include "UICore_h_ast.h"
#include "UIGen.h"
#include "UIGen_h_ast.h"
#include "MemoryPool.h"

#include "UIGenBox.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

MP_DEFINE(UIGenBox);

void ui_GenFitContentsSizeBox(UIGen *pGen, UIGenBox *pBox, CBox *pOut)
{
	// The native size of a box is the furthest point a fixed child expects to draw at.
	Vec2 v2Size = {0, 0};
	ui_GenInternalForEachChild(&pBox->polyp, ui_GenGetBounds, v2Size, UI_GEN_LAYOUT_ORDER);
	BuildCBox(pOut, 0, 0, v2Size[0], v2Size[1]);
}

AUTO_RUN;
void ui_GenRegisterBox(void)
{
	MP_CREATE(UIGenBox, 64);
	ui_GenRegisterType(kUIGenTypeBox, 
		UI_GEN_NO_VALIDATE, 
		UI_GEN_NO_POINTERUPDATE,
		UI_GEN_NO_UPDATE, 
		UI_GEN_NO_LAYOUTEARLY, 
		UI_GEN_NO_LAYOUTLATE, 
		UI_GEN_NO_TICKEARLY, 
		UI_GEN_NO_TICKLATE, 
		UI_GEN_NO_DRAWEARLY,
		ui_GenFitContentsSizeBox, 
		UI_GEN_NO_FITPARENTSIZE, 
		UI_GEN_NO_HIDE, 
		UI_GEN_NO_INPUT, 
		UI_GEN_NO_UPDATECONTEXT, 
		UI_GEN_NO_QUEUERESET);
}

#include "UIGenBox_h_ast.c"
