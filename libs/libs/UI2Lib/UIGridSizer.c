/***************************************************************************



***************************************************************************/
#include "UIGridSizer.h"

#include "EArray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct UIGridSizerPrivateData
{
	int *rowProportions;
	int *colProportions;
	int *rowMinHeights;
	int *colMinWidths;

	int rowStretchable;
	int colStretchable;

	int *minWidths;
	int *minHeights;

	int totalFixedWidth;
	int totalFixedHeight;
	int totalMinWidth;
	int totalMinHeight;
} UIGridSizerPrivateData;

typedef struct UIGridSizerPrivateChildData
{
	UI_INHERIT_FROM(UI_SIZER_CHILD_DATA_TYPE);

	UIDirection direction;
	int border;
} UIGridSizerPrivateChildData;

static void GridSizerCalcMin(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, Vec2 minSizeOut);
static void GridSizerRecalcSizes(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, F32 x, F32 y, F32 width, F32 height);

UIGridSizer *ui_GridSizerCreate(int nrows, int ncols, int rowProportion, int colProportion, int rowMinHeight, int colMinWidth)
{
	UIGridSizer *pGridSizer = (UIGridSizer *)calloc(1, sizeof(UIGridSizer));
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)calloc(1, sizeof(UIGridSizerPrivateData));
	S32 i;

	pGridSizer->privateData = privateData;

	pGridSizer->nrows = nrows > 0 ? nrows : 1;
	pGridSizer->ncols = ncols > 0 ? ncols : 1;

	ui_SizerInitialize(UI_SIZER(pGridSizer), GridSizerCalcMin, GridSizerRecalcSizes);

	eaSetSize(&UI_SIZER(pGridSizer)->children, nrows * ncols);

	ea32SetSize(&privateData->rowProportions, nrows);
	ea32SetSize(&privateData->colProportions, ncols);
	ea32SetSize(&privateData->rowMinHeights, nrows);
	ea32SetSize(&privateData->colMinWidths, ncols);
	ea32SetSize(&privateData->minHeights, nrows);
	ea32SetSize(&privateData->minWidths, ncols);

	for(i = 0; i < nrows; i++)
	{
		privateData->rowProportions[i] = rowProportion;
		privateData->rowMinHeights[i] = rowMinHeight;
	}
	for(i = 0; i < ncols; i++)
	{
		privateData->colProportions[i] = colProportion;
		privateData->colMinWidths[i] = colMinWidth;
	}

	return pGridSizer;
}

void ui_GridSizerFree(UIGridSizer *pGridSizer)
{
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)pGridSizer->privateData;

	ui_SizerFreeInternal(UI_SIZER(pGridSizer));

	ea32Destroy(&privateData->rowProportions);
	ea32Destroy(&privateData->colProportions);
	ea32Destroy(&privateData->rowMinHeights);
	ea32Destroy(&privateData->colMinWidths);
	ea32Destroy(&privateData->minHeights);
	ea32Destroy(&privateData->minWidths);

	free(pGridSizer->privateData);

	free(pGridSizer);
}

void ui_GridSizerSetRowProportion(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int row, int proportion)
{
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)pGridSizer->privateData;
	assert(row >= 0 && row < pGridSizer->nrows);
	privateData->rowProportions[row] = proportion;
}

void ui_GridSizerSetColProportion(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int col, int proportion)
{
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)pGridSizer->privateData;
	assert(col >= 0 && col < pGridSizer->ncols);
	privateData->colProportions[col] = proportion;
}

void ui_GridSizerSetRowMinSize(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int row, int minSize)
{
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)pGridSizer->privateData;
	assert(row >= 0 && row < pGridSizer->nrows);
	privateData->rowMinHeights[row] = minSize;
}

void ui_GridSizerSetColMinSize(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int col, int minSize)
{
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)pGridSizer->privateData;
	assert(col >= 0 && col < pGridSizer->ncols);
	privateData->colMinWidths[col] = minSize;
}

void ui_GridSizerAddWidget(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, SA_PARAM_NN_VALID UIWidget *pChildWidget, int row, int col, UIDirection direction, int border)
{
	UIGridSizerPrivateChildData *childData = NULL;
	assert(row >= 0 && row < pGridSizer->nrows && col >= 0 && col < pGridSizer->ncols);
	childData = (UIGridSizerPrivateChildData *)calloc(1, sizeof(UIGridSizerPrivateChildData));

	ui_InitializeSizerChildDataWithWidget(UI_SIZER_CHILD_DATA(childData), pChildWidget);

	childData->direction = direction;
	childData->border = border;

	eaSet(&UI_SIZER(pGridSizer)->children, UI_SIZER_CHILD_DATA(childData), row * pGridSizer->ncols + col);

	if(UI_SIZER(pGridSizer)->pWidget)
		if(-1 == eaFind(&UI_SIZER(pGridSizer)->pWidget->children, pChildWidget))
			ui_WidgetAddChild(UI_SIZER(pGridSizer)->pWidget, pChildWidget);
}

void ui_GridSizerAddSizer(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, SA_PARAM_NN_VALID UISizer *pChildSizer, int row, int col, UIDirection direction, int border)
{
	UIGridSizerPrivateChildData *childData = NULL;
	assert(row >= 0 && row < pGridSizer->nrows && col >= 0 && col < pGridSizer->ncols);
	childData = (UIGridSizerPrivateChildData *)calloc(1, sizeof(UIGridSizerPrivateChildData));

	ui_InitializeSizerChildDataWithSizer(UI_SIZER_CHILD_DATA(childData), pChildSizer);

	childData->direction = direction;
	childData->border = border;

	eaSet(&UI_SIZER(pGridSizer)->children, UI_SIZER_CHILD_DATA(childData), row * pGridSizer->ncols + col);

	pChildSizer->pWidget = UI_SIZER(pGridSizer)->pWidget; // share the widget, if already set

	ui_SizerAddChildWidgetsToWidgetRecurse(pChildSizer, pChildSizer->pWidget);
}

static void GridSizerCalcMin(UIGridSizer *pGridSizer, Vec2 minSizeOut)
{
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)pGridSizer->privateData;
	int i, j;
	int *maxMinWidths = NULL;
	int *maxMinHeights = NULL;
	int *cellWidths = NULL;
	int *cellHeights = NULL;
	int *maxCellWidths = NULL;
	int *maxCellHeights = NULL;
	int *fixedWidths = NULL;
	int *fixedHeights = NULL;

	ea32SetSize(&maxMinHeights, pGridSizer->nrows);
	ea32SetSize(&maxMinWidths, pGridSizer->ncols);
	ea32SetSize(&cellHeights, pGridSizer->nrows * pGridSizer->ncols);
	ea32SetSize(&cellWidths, pGridSizer->nrows * pGridSizer->ncols);
	ea32SetSize(&maxCellHeights, pGridSizer->nrows);
	ea32SetSize(&maxCellWidths, pGridSizer->ncols);
	ea32SetSize(&fixedHeights, pGridSizer->nrows);
	ea32SetSize(&fixedWidths, pGridSizer->ncols);

	ui_SizerCalcMinSizeRecurse(UI_SIZER(pGridSizer));

	// initialize
	privateData->rowStretchable = 0;
	privateData->colStretchable = 0;
	privateData->totalFixedWidth = 0;
	privateData->totalFixedHeight = 0;
	privateData->totalMinHeight = 0;
	privateData->totalMinWidth = 0;

	// initialize and count proportions
	for(i = 0; i < pGridSizer->ncols; i++)
	{
		privateData->colStretchable += privateData->colProportions[i];
		privateData->minWidths[i] = 0;
		fixedWidths[i] = 0;
		maxMinWidths[i] = 0;
	}

	for(i = 0; i < pGridSizer->nrows; i++)
	{
		privateData->rowStretchable += privateData->rowProportions[i];
		privateData->minHeights[i] = 0;
		fixedHeights[i] = 0;
		maxMinHeights[i] = 0;
	}

	// Compute maxMinWidths for proportioned columns
	for(i = 0; i < pGridSizer->ncols; i++)
	{
		if(privateData->colProportions[i] != 0)
		{
			int minSize = privateData->colMinWidths[i] * privateData->colProportions[i] / privateData->colStretchable;

			if(minSize > maxMinWidths[i])
				maxMinWidths[i] = minSize;

			for(j = 0; j < pGridSizer->nrows; j++)
			{
				UIGridSizerPrivateChildData *childData = (UIGridSizerPrivateChildData *)UI_SIZER(pGridSizer)->children[j * pGridSizer->ncols + i];
				Vec2 size = { UI_SIZER_CHILD_DATA(childData)->minSize[0] + 2 * childData->border, UI_SIZER_CHILD_DATA(childData)->minSize[1] + 2 * childData->border };

				minSize = size[0] * privateData->colProportions[i] / privateData->colStretchable;

				if(minSize > maxMinWidths[i])
					maxMinWidths[i] = minSize;
			}
		}
	}

	// Compute maxMinHeights for proportioned rows
	for(i = 0; i < pGridSizer->nrows; i++)
	{
		if(privateData->rowProportions[i] != 0)
		{
			int minSize = privateData->rowMinHeights[i] * privateData->rowProportions[i] / privateData->rowStretchable;

			if(minSize > maxMinHeights[i])
				maxMinHeights[i] = minSize;

			for(j = 0; j < pGridSizer->ncols; j++)
			{
				UIGridSizerPrivateChildData *childData = (UIGridSizerPrivateChildData *)UI_SIZER(pGridSizer)->children[i * pGridSizer->ncols + j];
				Vec2 size = { UI_SIZER_CHILD_DATA(childData)->minSize[0] + 2 * childData->border, UI_SIZER_CHILD_DATA(childData)->minSize[1] + 2 * childData->border };

				minSize = size[1] * privateData->rowProportions[i] / privateData->colStretchable;

				if(minSize > maxMinHeights[i])
					maxMinHeights[i] = minSize;
			}
		}
	}

	// Compute each cell's effective width and height desired, accounting for proportions
	for(i = 0; i < pGridSizer->ncols; i++)
	{
		for(j = 0; j < pGridSizer->nrows; j++)
		{
			UIGridSizerPrivateChildData *childData = (UIGridSizerPrivateChildData *)UI_SIZER(pGridSizer)->children[j * pGridSizer->ncols + i];

			if(privateData->colProportions[i] != 0)
				cellWidths[j * pGridSizer->ncols + i] = (maxMinWidths[i] * privateData->colProportions[i]) / privateData->colStretchable;
			else
				cellWidths[j * pGridSizer->ncols + i] = UI_SIZER_CHILD_DATA(childData)->minSize[0] + 2 * childData->border;

			if(privateData->rowProportions[i] != 0)
				cellHeights[j * pGridSizer->ncols + i] = (maxMinHeights[i] * privateData->rowProportions[i]) / privateData->rowStretchable;
			else
				cellHeights[j * pGridSizer->ncols + i] = UI_SIZER_CHILD_DATA(childData)->minSize[1] + 2 * childData->border;

			maxCellWidths[i] = MAX(maxCellWidths[i], cellWidths[j * pGridSizer->ncols + i]);
			maxCellHeights[j] = MAX(maxCellHeights[j], cellHeights[j * pGridSizer->ncols + i]);
		}
	}

	// Calculate min and fixed widths for each column
	for(i = 0; i < pGridSizer->ncols; i++)
	{
		if(privateData->colProportions[i] == 0)
			fixedWidths[i] += maxCellWidths[i];
		privateData->minWidths[i] = MAX(privateData->minWidths[i], maxCellWidths[i]);
	}

	// Calculate min and fixed heights for each row
	for(i = 0; i < pGridSizer->nrows; i++)
	{
		if(privateData->rowProportions[i] == 0)
			fixedHeights[i] += maxCellHeights[i];
		privateData->minHeights[i] = MAX(privateData->minHeights[i], maxCellHeights[i]);
	}

	// Total up column widths
	for(i = 0; i < pGridSizer->ncols; i++)
	{
		privateData->totalFixedWidth += fixedWidths[i];
		privateData->totalMinWidth += privateData->minWidths[i];
	}

	// Total up row heights
	for(i = 0; i < pGridSizer->nrows; i++)
	{
		privateData->totalFixedHeight += fixedHeights[i];
		privateData->totalMinHeight += privateData->minHeights[i];
	}

	if(minSizeOut)
	{
		minSizeOut[0] = privateData->totalMinWidth;
		minSizeOut[1] = privateData->totalMinHeight;
	}

	ea32Destroy(&maxMinHeights);
	ea32Destroy(&maxMinWidths);
	ea32Destroy(&cellHeights);
	ea32Destroy(&cellWidths);
	ea32Destroy(&maxCellHeights);
	ea32Destroy(&maxCellWidths);
	ea32Destroy(&fixedHeights);
	ea32Destroy(&fixedWidths);
}

static void GridSizerRecalcSizes(UIGridSizer *pGridSizer, F32 x, F32 y, F32 width, F32 height)
{
	UIGridSizerPrivateData *privateData = (UIGridSizerPrivateData *)pGridSizer->privateData;
	int childCount = eaSize(&UI_SIZER(pGridSizer)->children);
	int colStretchable = privateData->colStretchable;
	int rowStretchable = privateData->rowStretchable;
	int i, j;
	int deltaWidth = 0;
	int deltaHeight = 0;
	Vec2 pt;
	int *widths = NULL;
	int *heights = NULL;

	ea32SetSize(&heights, pGridSizer->nrows);
	ea32SetSize(&widths, pGridSizer->ncols);

	// Determine the delta for stretching/shrinking columns
	if(privateData->colStretchable)
		deltaWidth = width - privateData->totalFixedWidth;

	// Determine the delta for stretching/shrinking rows
	if(privateData->rowStretchable)
		deltaHeight = height - privateData->totalFixedHeight;

	// Compute actual column widths, taking into account stretching/shrinking
	for(i = 0; i < pGridSizer->ncols; i++)
	{
		if(privateData->colProportions[i])
		{
			// Since at least one column has non-zero proportion, stretchable will never be zero
			widths[i] = (deltaWidth * privateData->colProportions[i]) / privateData->colStretchable;
			deltaWidth -= widths[i];
			colStretchable -= privateData->colStretchable;
		}
		else
			widths[i] = privateData->minWidths[i];
	}

	// Compute actual row heights, taking into account stretching/shrinking
	for(i = 0; i < pGridSizer->nrows; i++)
	{
		if(privateData->rowProportions[i])
		{
			// Since at least one row has non-zero proportion, stretchable will never be zero
			heights[i] = (deltaHeight * privateData->rowProportions[i]) / privateData->rowStretchable;
			deltaHeight -= heights[i];
			rowStretchable -= privateData->rowStretchable;
		}
		else
			heights[i] = privateData->minHeights[i];
	}

	// Finally, position each child within its cell
	pt[0] = x;
	for(i = 0; i < pGridSizer->ncols; i++)
	{
		pt[1] = y;
		for(j = 0; j < pGridSizer->nrows; j++)
		{
			UIGridSizerPrivateChildData *childData = (UIGridSizerPrivateChildData *)UI_SIZER(pGridSizer)->children[j * pGridSizer->ncols + i];
			Vec2 size = { UI_SIZER_CHILD_DATA(childData)->minSize[0] + 2 * childData->border, UI_SIZER_CHILD_DATA(childData)->minSize[1] + 2 * childData->border };

			if(UI_SIZER_CHILD_DATA(childData)->type != UISizerChildType_None)
			{
				Vec2 child_pos = { pt[0], pt[1] };
				Vec2 child_size = { size[0], size[1] };

				if((childData->direction & UILeft) && (childData->direction & UIRight)) // stretch to fill horizontal space
					child_size[0] = widths[i];
				else if(childData->direction & UIRight) // align to right
					child_pos[0] += widths[i] - size[0];
				else if(!(childData->direction & UILeft)) // horizontal center (default is align to left)
					child_pos[0] += (widths[i] - size[0]) / 2;

				if((childData->direction & UITop) && (childData->direction & UIBottom)) // stretch to fill vertical space
					child_size[1] = heights[j];
				else if(childData->direction & UIBottom) // align to bottom
					child_pos[1] += heights[j] - size[1];
				else if(!(childData->direction & UITop)) // vertical center (default is align to top)
					child_pos[1] += (heights[j] - size[1]) / 2;

				if(UI_SIZER_CHILD_DATA(childData)->type == UISizerChildType_Widget)
				{
					ui_WidgetSetPosition(UI_SIZER_CHILD_DATA(childData)->pWidget, child_pos[0] + childData->border, child_pos[1] + childData->border);
					ui_WidgetSetDimensionsEx(UI_SIZER_CHILD_DATA(childData)->pWidget, child_size[0] - 2 * childData->border, child_size[1] - 2 * childData->border, UIUnitFixed, UIUnitFixed);
				}
				else if(UI_SIZER_CHILD_DATA(childData)->type == UISizerChildType_Sizer)
					UI_SIZER_CHILD_DATA(childData)->pSizer->recalcSizesF(UI_SIZER_CHILD_DATA(childData)->pSizer, child_pos[0] + childData->border, child_pos[1] + childData->border,
						child_size[0] - 2 * childData->border, child_size[1] - 2 * childData->border);
			}

			pt[1] += heights[j];
		}
		pt[0] += widths[i];
	}

	ea32Destroy(&heights);
	ea32Destroy(&widths);
}
