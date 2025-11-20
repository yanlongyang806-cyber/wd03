#pragma once
GCC_SYSTEM
#ifndef ui_BarGraph_H
#define ui_BarGraph_H

#include "UICore.h"
#include "UIPane.h"

typedef struct UITextEntry UITextEntry;
typedef struct UISpinner UISpinner;
typedef struct UILabel UILabel;

//////////////////////////////////////////////////////////////////////////
// A UIBarGraph is a widget that displays an array of data, using a model
// passed in, so it needn't keep a copy; it is able to draw the data around
// a midpoint value, allowing negatives

typedef struct UIBarGraph
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);

	char *pchLabelX;
	char *pchLabelY;

	Vec2 v2LowerBound;
	Vec2 v2UpperBound;

	bool bDrawScale : 1;
	
	// Draw this many ticks along the axis. This does not include the origin,
	// but does include the upper bound.
	U8 chResolutionX;
	U8 chResolutionY;

	union {
		float *fmodel;
		int *imodel;
	};

	union {
		float fmid;
		float imid;
	};
	
	int length;				// Size of the model
	int model_stride;		// Distance between data points for interleaved data

	U32 model_float : 1;	// Determines if the model is floats or ints
} UIBarGraph;

SA_RET_NN_VALID UIBarGraph *ui_BarGraphCreate(SA_PARAM_OP_STR const char *pchLabelX, SA_PARAM_OP_STR const char *pchLabelY, const Vec2 v2Lower, const Vec2 v2Upper);
void ui_BarGraphFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UIBarGraph *pGraph);
void ui_BarGraphTick(SA_PARAM_NN_VALID UIBarGraph *pGraph, UI_PARENT_ARGS);
void ui_BarGraphDraw(SA_PARAM_NN_VALID UIBarGraph *pGraph, UI_PARENT_ARGS);

void ui_BarGraphSetModel(SA_PARAM_NN_VALID UIBarGraph *pGraph, void* model, int stride, int length, U32 floats);

void ui_BarGraphSetResolution(SA_PARAM_NN_VALID UIBarGraph *pGraph, U8 chResolutionX, U8 chResolutionY);
void ui_BarGraphSetBounds(SA_PARAM_NN_VALID UIBarGraph *pGraph, const Vec2 v2Lower, const Vec2 v2Upper);

void ui_BarGraphSetDrawScale(SA_PARAM_NN_VALID UIBarGraph *pGraph, bool bDrawScale);

void ui_BarGraphSetLabels(SA_PARAM_NN_VALID UIBarGraph *pGraph, SA_PARAM_OP_STR const char *pchLabelX, SA_PARAM_OP_STR const char *pchLabelY);

#endif