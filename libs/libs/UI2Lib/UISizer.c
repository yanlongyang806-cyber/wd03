/***************************************************************************



***************************************************************************/
#include "UISizer.h"

#include "EArray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

void ui_SizerInitialize(UISizer *pSizer, SizerCalcMinFunction calcMinF, SizerRecalcSizesFunction recalcSizesF)
{
	pSizer->pWidget = NULL;
	pSizer->calcMinF = calcMinF;
	pSizer->recalcSizesF = recalcSizesF;
	pSizer->children = NULL;
}

void ui_SizerAddChildWidgetsToWidgetRecurse(UISizer *pSizer, UIWidget *pWidget)
{
	int i;
	int childCount = eaSize(&pSizer->children);
	for(i = 0; i < childCount; i++)
	{
		UISizerChildData *childData = pSizer->children[i];
		if(childData->type == UISizerChildType_Widget)
		{
			if(-1 == eaFind(&pWidget->children, childData->pWidget))
				ui_WidgetAddChild(pWidget, childData->pWidget);
		}
		else if(childData->type == UISizerChildType_Sizer)
			ui_SizerAddChildWidgetsToWidgetRecurse(childData->pSizer, pWidget);
	}
}

static ui_SizerChildDataFreeInternal(UISizerChildData *childData)
{
	if(childData->type == UISizerChildType_Sizer)
		ui_SizerFreeInternal(childData->pSizer);

	free(childData);
}

void ui_SizerFreeInternal(UISizer *pSizer)
{
	if(pSizer->pWidget && pSizer->pWidget->pSizer == pSizer)
		pSizer->pWidget->pSizer = NULL;

	eaDestroyEx(&pSizer->children, ui_SizerChildDataFreeInternal);
}

void ui_InitializeSizerChildDataWithNone(SA_PARAM_NN_VALID UISizerChildData *pSizerChildData, int minWidth, int minHeight)
{
	pSizerChildData->type = UISizerChildType_None;
	pSizerChildData->minSize[0] = minWidth;
	pSizerChildData->minSize[1] = minHeight;
}

void ui_InitializeSizerChildDataWithWidget(SA_PARAM_NN_VALID UISizerChildData *pSizerChildData, SA_PARAM_NN_VALID UIWidget *pWidget)
{
	pSizerChildData->type = UISizerChildType_Widget;
	pSizerChildData->pWidget = pWidget;
	// IMPORTANT! Assuming widget size initialized with is min size. // TODO: Implement minSize as a Widget attribute?
	pSizerChildData->minSize[0] = pWidget->width;
	pSizerChildData->minSize[1] = pWidget->height;
}

void ui_InitializeSizerChildDataWithSizer(SA_PARAM_NN_VALID UISizerChildData *pSizerChildData, SA_PARAM_NN_VALID UISizer *pSizer)
{
	pSizerChildData->type = UISizerChildType_Sizer;
	pSizerChildData->pSizer = pSizer;
	pSizerChildData->minSize[0] = -1;
	pSizerChildData->minSize[1] = -1;
}

void ui_SizerLayout(UISizer *pSizer, F32 width, F32 height)
{
	if(pSizer->calcMinF)
		pSizer->calcMinF(pSizer, NULL);

	if(pSizer->recalcSizesF)
		pSizer->recalcSizesF(pSizer, 0, 0, width, height);
}

void ui_SizerCalcMinSizeRecurse(SA_PARAM_NN_VALID UISizer *pSizer)
{
	int i;
	int childCount = eaSize(&pSizer->children);
	for(i = 0; i < childCount; i++)
	{
		UISizerChildData *childData = (UISizerChildData *)pSizer->children[i];

		switch(childData->type)
		{
			case UISizerChildType_Widget: break; // IMPORTANT! Assuming widget size initialized with is min size. // TODO: Implement minSize as a Widget attribute?
			case UISizerChildType_Sizer:
				if(childData->pSizer->calcMinF)
					childData->pSizer->calcMinF(childData->pSizer, childData->minSize);
				break;
		}
	}
}

void ui_SizerGetMinSize(SA_PARAM_NN_VALID UISizer *pSizer, Vec2 minSizeOut)
{
	if(pSizer->calcMinF)
		pSizer->calcMinF(pSizer, minSizeOut);
}

UISizerChildData *ui_SizerGetWidgetChildData(UISizer *pSizer, UIWidget *pChildWidget)
{
	int i;
	int childCount = eaSize(&pSizer->children);
	for(i = 0; i < childCount; i++)
	{
		UISizerChildData *childData = pSizer->children[i];
		if(childData->type == UISizerChildType_Widget && childData->pWidget == pChildWidget)
			return childData;
		else if(childData->type == UISizerChildType_Sizer)
		{
			childData = ui_SizerGetWidgetChildData(childData->pSizer, pChildWidget);
			if(childData)
				return childData;
		}
	}
	return NULL;
}
