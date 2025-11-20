#pragma once
GCC_SYSTEM
#ifndef UI_GEN_CHECKBUTTON_H
#define UI_GEN_CHECKBUTTON_H

#include "UIGen.h"

AUTO_STRUCT;
typedef struct UIGenCheckButton
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeCheckButton))
	UIGenAction *pOnClicked;
	REF_TO(UITextureAssembly) hBorderAssembly; AST(NAME(BorderAssembly))
	REF_TO(UITextureAssembly) hCheckAssembly; AST(NAME(CheckAssembly))
	REF_TO(UITextureAssembly) hInconsistentAssembly; AST(NAME(InconsistentAssembly))
	S32 iButtonWidth; AST(DEFAULT(UI_DSTEP))
	S32 iButtonHeight; AST(DEFAULT(UI_DSTEP))
	UIGenBundleText TextBundle; AST(EMBEDDED_FLAT)
	REF_TO(Message) hTruncate; AST(NAME(Truncate) NON_NULL_REF)
	F32 fSpacing;
} UIGenCheckButton;

AUTO_STRUCT;
typedef struct UIGenCheckButtonState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeCheckButton))
	char *pchString; AST(ESTRING)
	bool bChecked;
	bool bInconsistent;
	UIGenBundleTruncateState Truncate;
} UIGenCheckButtonState;

#endif