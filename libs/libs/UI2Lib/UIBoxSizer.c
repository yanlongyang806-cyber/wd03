/***************************************************************************



***************************************************************************/
#include "UIBoxSizer.h"

#include "EArray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct UIBoxSizerPrivateData
{
	int stretchable;
	int minWidth;
	int minHeight;
	int fixedWidth;
	int fixedHeight;
} UIBoxSizerPrivateData;

typedef struct UIBoxSizerPrivateChildData
{
	UI_INHERIT_FROM(UI_SIZER_CHILD_DATA_TYPE);

	int proportion;
	UIDirection direction;
	int border;
} UIBoxSizerPrivateChildData;

static void BoxSizerCalcMin(SA_PARAM_NN_VALID UIBoxSizer *pBoxSizer, Vec2 minSizeOut);
static void BoxSizerRecalcSizes(SA_PARAM_NN_VALID UIBoxSizer *pBoxSizer, F32 x, F32 y, F32 width, F32 height);

UIBoxSizer *ui_BoxSizerCreate(UIDirection orientation)
{
	UIBoxSizer *pBoxSizer = (UIBoxSizer *)calloc(1, sizeof(UIBoxSizer));
	UIBoxSizerPrivateData *privateData = (UIBoxSizerPrivateData *)calloc(1, sizeof(UIBoxSizerPrivateData));

	pBoxSizer->privateData = privateData;

	pBoxSizer->orientation = orientation;
	if(pBoxSizer->orientation != UIHorizontal && pBoxSizer->orientation != UIVertical)
		pBoxSizer->orientation = UIHorizontal;

	ui_SizerInitialize(UI_SIZER(pBoxSizer), BoxSizerCalcMin, BoxSizerRecalcSizes);

	return pBoxSizer;
}

void ui_BoxSizerFree(UIBoxSizer *pBoxSizer)
{
	ui_SizerFreeInternal(UI_SIZER(pBoxSizer));

	free(pBoxSizer->privateData);

	free(pBoxSizer);
}

//If adding an MEField, call MEFieldCreateThisWidget() on the MEField before passing the MEField's ->pUIWidget into this
void ui_BoxSizerAddWidget(UIBoxSizer *pBoxSizer, UIWidget *pChildWidget, int proportion, UIDirection direction, int border)
{
	UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)calloc(1, sizeof(UIBoxSizerPrivateChildData));

	ui_InitializeSizerChildDataWithWidget(UI_SIZER_CHILD_DATA(childData), pChildWidget);

	childData->proportion = proportion;
	childData->direction = direction;
	childData->border = border;

	eaPush(&UI_SIZER(pBoxSizer)->children, UI_SIZER_CHILD_DATA(childData));

	if(UI_SIZER(pBoxSizer)->pWidget)
		if(-1 == eaFind(&UI_SIZER(pBoxSizer)->pWidget->children, pChildWidget))
			ui_WidgetAddChild(UI_SIZER(pBoxSizer)->pWidget, pChildWidget);
}

void ui_BoxSizerAddSizer(UIBoxSizer *pBoxSizer, UISizer *pChildSizer, int proportion, UIDirection direction, int border)
{
	UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)calloc(1, sizeof(UIBoxSizerPrivateChildData));

	ui_InitializeSizerChildDataWithSizer(UI_SIZER_CHILD_DATA(childData), pChildSizer);

	childData->proportion = proportion;
	childData->direction = direction;
	childData->border = border;

	eaPush(&UI_SIZER(pBoxSizer)->children, UI_SIZER_CHILD_DATA(childData));

	pChildSizer->pWidget = UI_SIZER(pBoxSizer)->pWidget; // share the widget, if already set

	if (pChildSizer->pWidget)
	{
		ui_SizerAddChildWidgetsToWidgetRecurse(pChildSizer, pChildSizer->pWidget);
	}
}

void ui_BoxSizerAddSpacer(UIBoxSizer *pBoxSizer, int size)
{
	UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)calloc(1, sizeof(UIBoxSizerPrivateChildData));

	ui_InitializeSizerChildDataWithNone(UI_SIZER_CHILD_DATA(childData), size, size);

	childData->proportion = 0;
	childData->direction = UINoDirection;
	childData->border = 0;

	eaPush(&UI_SIZER(pBoxSizer)->children, UI_SIZER_CHILD_DATA(childData));
}

void ui_BoxSizerAddFiller(UIBoxSizer *pBoxSizer, int proportion)
{
	UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)calloc(1, sizeof(UIBoxSizerPrivateChildData));

	ui_InitializeSizerChildDataWithNone(UI_SIZER_CHILD_DATA(childData), -1, -1);

	childData->proportion = proportion;
	childData->direction = UIAnyDirection;
	childData->border = 0;

	eaPush(&UI_SIZER(pBoxSizer)->children, UI_SIZER_CHILD_DATA(childData));
}

static void BoxSizerCalcMin(UIBoxSizer *pBoxSizer, Vec2 minSizeOut)
{
	UIBoxSizerPrivateData *privateData = (UIBoxSizerPrivateData *)pBoxSizer->privateData;
	int childCount;
	int i;
	int maxMinSize;

	ui_SizerCalcMinSizeRecurse(UI_SIZER(pBoxSizer));

	privateData->stretchable = 0;
	privateData->minWidth = 0;
	privateData->minHeight = 0;
	privateData->fixedWidth = 0;
	privateData->fixedHeight = 0;

	// count proportions
	childCount = eaSize(&UI_SIZER(pBoxSizer)->children);
	for(i = 0; i < childCount; i++)
	{
		UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)UI_SIZER(pBoxSizer)->children[i];

		privateData->stretchable += childData->proportion;
	}

	// Total minimum size (width or height) of sizer
	maxMinSize = 0;

	for(i = 0; i < childCount; i++)
	{
		UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)UI_SIZER(pBoxSizer)->children[i];

		if(childData->proportion != 0)
		{
			int stretch = childData->proportion;
			Vec2 size = { UI_SIZER_CHILD_DATA(childData)->minSize[0] + 2 * childData->border, UI_SIZER_CHILD_DATA(childData)->minSize[1] + 2 * childData->border };
			int minSize;

			if(pBoxSizer->orientation == UIHorizontal)
				minSize = size[0] * privateData->stretchable / stretch;
			else
				minSize = size[1] * privateData->stretchable / stretch;

			if(minSize > maxMinSize)
				maxMinSize = minSize;
		}
	}

	for(i = 0; i < childCount; i++)
	{
		UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)UI_SIZER(pBoxSizer)->children[i];

		Vec2 size = { UI_SIZER_CHILD_DATA(childData)->minSize[0] + 2 * childData->border, UI_SIZER_CHILD_DATA(childData)->minSize[1] + 2 * childData->border };
		if(childData->proportion != 0)
		{
			if(pBoxSizer->orientation == UIHorizontal)
				size[0] = (maxMinSize * childData->proportion) / privateData->stretchable;
			else
				size[1] = (maxMinSize * childData->proportion) / privateData->stretchable;
		}
		else
		{
			if(pBoxSizer->orientation == UIHorizontal)
			{
				privateData->fixedWidth += size[0];
				privateData->fixedHeight = MAX(privateData->fixedHeight, size[1]);
			}
			else
			{
				privateData->fixedHeight += size[1];
				privateData->fixedWidth = MAX(privateData->fixedWidth, size[0]);
			}
		}

		if(pBoxSizer->orientation == UIHorizontal)
		{
			privateData->minWidth += size[0];
			privateData->minHeight = MAX(privateData->minHeight, size[1]);
		}
		else
		{
			privateData->minHeight += size[1];
			privateData->minWidth = MAX(privateData->minWidth, size[0]);
		}
	}

	if(minSizeOut)
	{
		minSizeOut[0] = privateData->minWidth;
		minSizeOut[1] = privateData->minHeight;
	}
}

static void BoxSizerRecalcSizes(UIBoxSizer *pBoxSizer, F32 x, F32 y, F32 width, F32 height)
{
	UIBoxSizerPrivateData *privateData = (UIBoxSizerPrivateData *)pBoxSizer->privateData;
	int childCount;
	int i;
	int delta = 0;
	int stretchable = privateData->stretchable;
	Vec2 pt = { x, y };

	if(privateData->stretchable)
	{
		if(pBoxSizer->orientation == UIHorizontal)
			delta = width - privateData->fixedWidth;
		else
			delta = height - privateData->fixedHeight;
	}

	childCount = eaSize(&UI_SIZER(pBoxSizer)->children);
	for(i = 0; i < childCount; i++)
	{
		UIBoxSizerPrivateChildData *childData = (UIBoxSizerPrivateChildData *)UI_SIZER(pBoxSizer)->children[i];

		Vec2 size = { UI_SIZER_CHILD_DATA(childData)->minSize[0] + 2 * childData->border, UI_SIZER_CHILD_DATA(childData)->minSize[1] + 2 * childData->border };

		if(pBoxSizer->orientation == UIHorizontal)
		{
			F32 w = size[0];
			if(childData->proportion)
			{
				// Since at least one visible item has non-zero proportion, stretchable will never be zero
				w = (delta * childData->proportion) / stretchable;
				delta -= w;
				stretchable -= childData->proportion;
			}

			if(UI_SIZER_CHILD_DATA(childData)->type != UISizerChildType_None)
			{
				Vec2 child_pos = { pt[0], pt[1] };
				Vec2 child_size = { w, size[1] };

				if((childData->direction & UITop) && (childData->direction & UIBottom)) // stretch to fill vertical space
					child_size[1] = height;
				else if(childData->direction & UIBottom) // align to bottom
					child_pos[1] += height - size[1];
				else if(!(childData->direction & UITop)) // center (default is align to top)
					child_pos[1] += (height - size[1]) / 2;

				if(UI_SIZER_CHILD_DATA(childData)->type == UISizerChildType_Widget)
				{
					ui_WidgetSetPosition(UI_SIZER_CHILD_DATA(childData)->pWidget, child_pos[0] + childData->border, child_pos[1] + childData->border);
					ui_WidgetSetDimensionsEx(UI_SIZER_CHILD_DATA(childData)->pWidget, child_size[0] - 2 * childData->border, child_size[1] - 2 * childData->border, UIUnitFixed, UIUnitFixed);
				}
				else if(UI_SIZER_CHILD_DATA(childData)->type == UISizerChildType_Sizer)
					UI_SIZER_CHILD_DATA(childData)->pSizer->recalcSizesF(UI_SIZER_CHILD_DATA(childData)->pSizer, child_pos[0] + childData->border, child_pos[1] + childData->border,
						child_size[0] - 2 * childData->border, child_size[1] - 2 * childData->border);
			}

			pt[0] += w;
		}
		else
		{
			F32 h = size[1];
			if(childData->proportion)
			{
				// Since at least one visible item has non-zero proportion, stretchable will never be zero
				h = (delta * childData->proportion) / stretchable;
				delta -= h;
				stretchable -= childData->proportion;
			}

			if(UI_SIZER_CHILD_DATA(childData)->type != UISizerChildType_None)
			{
				Vec2 child_pos = { pt[0], pt[1] };
				Vec2 child_size = { size[0], h };

				if((childData->direction & UILeft) && (childData->direction & UIRight)) // stretch to fill horizontal space
					child_size[0] = width;
				else if(childData->direction & UIRight) // align to right
					child_pos[0] += width - size[0];
				else if(!(childData->direction & UILeft)) // center (default is align to left)
					child_pos[0] += (width - size[0]) / 2;

				if(UI_SIZER_CHILD_DATA(childData)->type == UISizerChildType_Widget)
				{
					ui_WidgetSetPosition(UI_SIZER_CHILD_DATA(childData)->pWidget, child_pos[0] + childData->border, child_pos[1] + childData->border);
					ui_WidgetSetDimensionsEx(UI_SIZER_CHILD_DATA(childData)->pWidget, child_size[0] - 2 * childData->border, child_size[1] - 2 * childData->border, UIUnitFixed, UIUnitFixed);
				}
				else if(UI_SIZER_CHILD_DATA(childData)->type == UISizerChildType_Sizer)
					UI_SIZER_CHILD_DATA(childData)->pSizer->recalcSizesF(UI_SIZER_CHILD_DATA(childData)->pSizer, child_pos[0] + childData->border, child_pos[1] + childData->border,
						child_size[0] - 2 * childData->border, child_size[1] - 2 * childData->border);
			}

			pt[1] += h;
		}
	}
}
