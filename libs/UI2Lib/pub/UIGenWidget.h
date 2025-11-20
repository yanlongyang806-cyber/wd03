#pragma once
GCC_SYSTEM
#ifndef UI_GEN_WIDGET_H

#include "UICore.h"
#include "UIGen.h"

// To make UIGen widgets actually useful, we embed them inside a special UI2Lib
// widget that acts as a "canvas".
typedef struct UIGenWidget
{
	UI_INHERIT_FROM(UI_WIDGET_TYPE);

	// The root UIGen node in this tree.
	REF_TO(UIGen) hGen;
	char chLayer;

	// Used to pretend to be a valid UIGen parent.
	UIGen fake;

	bool bModalLayer : 1;

} UIGenWidget;

void ui_GenWidgetTick(UIGenWidget *pGenWidget, UI_PARENT_ARGS);

#endif
