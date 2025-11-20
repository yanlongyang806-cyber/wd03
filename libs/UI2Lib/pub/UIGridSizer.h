/***************************************************************************



***************************************************************************/

#ifndef UI_GRID_SIZER_H
#define UI_GRID_SIZER_H
GCC_SYSTEM

#include "UISizer.h"

/************************************************************************/
/* Grid Sizer - for laying out widgets in a resizable/stretchable grid.
/************************************************************************/
typedef struct UIGridSizer
{
	UI_INHERIT_FROM(UI_SIZER_TYPE);

	int nrows;
	int ncols;

	PrivateData privateData;
} UIGridSizer;

// Create a Grid Sizer given the number of rows and columns, the default row and column proportions, and the default row and column sizes.
SA_RET_NN_VALID UIGridSizer *ui_GridSizerCreate(int nrows, int ncols, int rowProportion, int colProportion, int rowMinHeight, int colMinWidth);
void ui_GridSizerFree(SA_PARAM_NN_VALID UIGridSizer *pGridSizer);

void ui_GridSizerSetRowProportion(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int row, int proportion);
void ui_GridSizerSetColProportion(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int col, int proportion);
void ui_GridSizerSetRowMinSize(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int row, int minSize);
void ui_GridSizerSetColMinSize(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, int col, int minSize);

void ui_GridSizerAddWidget(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, SA_PARAM_NN_VALID UIWidget *pChildWidget, int row, int col, UIDirection direction, int border);
void ui_GridSizerAddSizer(SA_PARAM_NN_VALID UIGridSizer *pGridSizer, SA_PARAM_NN_VALID UISizer *pChildSizer, int row, int col, UIDirection direction, int border);

#endif
