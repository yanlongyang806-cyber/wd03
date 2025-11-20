#pragma once
GCC_SYSTEM
#ifndef UI_GEN_BOX_H
#define UI_GEN_BOX_H

#include "UIGen.h"

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenBox
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeBox))

	// Boxes are special and cannot contain anything other than the poly base type.
	// This is because anything can borrow from them.
} UIGenBox;

#endif
