#pragma once
GCC_SYSTEM
#ifndef UI_GEN_LAYOUT_BOX_H
#define UI_GEN_LAYOUT_BOX_H

#include "UIGen.h"

//////////////////////////////////////////////////////////////////////////
// A LayoutBox is a special kind of container for gens that is halfway
// between a box and a list. Like a list, it copies the same gen templates
// over and over, but like a box, it can contain any type of gen.
//
// The LayoutBox makes copies of the given template for each item in
// its model, and then positions them based on the size of the template.
// This allows the templates to grow/move around without pushing around
// all the other children.
//
// The OffsetFrom of the template determines how things are laid out:
//					 1	         1 2 3
// Left: 1 2 3	Top: 2	TopLeft: 4 5 6
//					 3           7 
//
// Things that are children of a LayoutBox have a few more variables
// available in their expression context. The variable "GenInstanceData"
// contains a pointer to the thing in the model and can be used like
// "GenData". There is also an "GenInstanceNumber" integer variable.
//
// Percentage sizes are handled differently. If a template has a percentage
// size, the spacing is instead copied into the margins, and the PercentX /
// PercentY variables are used for positioning.
//
// Note that it is possible to give a LayoutBox a set of unmeetable constraints
// (e.g. force an AspectRatio of 1 but require each child to have 100% width).
// Don't do this.
//
// TODO:
// * Instancing makes focusing weird, a LayoutBox needs to know how to handle focus.

#define UI_GEN_LAYOUTBOX_NO_SELECTION -9999

AUTO_STRUCT;
typedef struct UIGenLayoutBox
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeLayoutBox))
	REF_TO(UIGen) hTemplate; AST(NAME(Template))
	Expression *pTemplateExpr;	AST(NAME(TemplateExprBlock) REDUNDANT_STRUCT(TemplateExpr, parse_Expression_StructParam) LATEBIND)
	UIGenChild **eaTemplateChild; AST(NAME(TemplateChild) NAME(TemplateChildren))

	F32 fAspectRatio;

	Expression *pColumns; AST(NAME(ColumnsBlock) REDUNDANT_STRUCT(Columns, parse_Expression_StructParam) REDUNDANT_STRUCT(MaxColumns, parse_Expression_StructParam) LATEBIND)
	U8 uiRows; AST(NAME(Rows) NAME(MaxRows))
	U8 eLayoutDirection; AST(SUBTABLE(UIDirectionEnum))
	U8 eLayoutOrder; AST(DEFAULT(UIHorizontal) SUBTABLE(UIDirectionEnum))

	U8 bVariableSize : 1;

	// If this is set then TemplateChildren are appended to the list model
	U8 bAppendTemplateChildren : 1;

	S8 fHorizontalSpacing; AST(SUBTABLE(UISizeEnum))
	S8 fVerticalSpacing; AST(SUBTABLE(UISizeEnum))

	Expression *pCalculateX; AST(NAME(CalculateXBlock) REDUNDANT_STRUCT(CalculateX, parse_Expression_StructParam) LATEBIND)
	Expression *pCalculateY; AST(NAME(CalculateYBlock) REDUNDANT_STRUCT(CalculateY, parse_Expression_StructParam) LATEBIND)

	// The size of the model determines how many children to instance.
	Expression *pModel; AST(NAME(ModelBlock) REDUNDANT_STRUCT(Model, parse_Expression_StructParam) LATEBIND)
	Expression *pFilter; AST(NAME(FilterBlock) REDUNDANT_STRUCT(Filter, parse_Expression_StructParam) LATEBIND)

} UIGenLayoutBox;

AUTO_STRUCT;
typedef struct UIGenLayoutBoxInstance
{
	// The gen instance created for this layout box.
	// If the instance represents a template child,
	// this will be NULL.
	UIGen *pGen;

	// The template of the gen created for this layout
	// box, or a pointer to the template child gen if
	// the instance represents a template child.
	UIGen *pTemplate; AST(UNOWNED STRUCT_NORECURSE)

	// The index into the EArray of instances of this gen
	int iInstanceNumber;

	// The index into the model EArray for this gen. If
	// the gen is a TemplateChild, then this is -1.
	int iDataNumber;

	// The row of this instance.
	//
	// If the layout box is non-variable, then this is
	// deterministic based on the instance number, the
	// layout order, the number of rows, and the number
	// of columns.
	//
	// For variable layout boxes, if the layout order is
	// Horizontal then the row this instance flowed onto.
	// Otherwise it's the number of the element in the
	// column from the starting edge.
	S16 iRow;

	// The column of this instance.
	//
	// If the layout box is non-variable, then this is
	// deterministic based on the instance number, the
	// layout order, the number of rows, and the number
	// of columns.
	//
	// For variable layout boxes, if the layout order is
	// Vertical then the column this instance flowed onto.
	// Otherwise it's the number of the element in the
	// row from the starting edge.
	S16 iColumn;

	// Whether or not this is a new instance
	bool bNewInstance;
} UIGenLayoutBoxInstance;

AUTO_STRUCT;
typedef struct UIGenNamedInternal
{
	const char *pchName; AST(KEY POOL_STRING)
	UIGenInternal *pInternal; AST(STRUCT(parse_UIGenInternal))
} UIGenNamedInternal;

AUTO_STRUCT;
typedef struct UIGenLayoutBoxState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeLayoutBox))
	UIGenLayoutBoxInstance **eaInstances; AST(STRUCT_NORECURSE NO_INDEX NAME(Instances))
	UIGen **eaInstanceGens; NO_AST
	UIGenChild **eaTemplateGens; AST(NO_INDEX)
	UIGenNamedInternal **eaInternals;
	S32 iNewInstances;

	void **eaFilteredList; NO_AST

	const char **eaBadTemplateNames; AST(POOL_STRING)

	U8 eLayoutDirection;
	S32 iSelected; AST(DEFAULT(UI_GEN_LAYOUTBOX_NO_SELECTION)) // special value to indicate no selection management
	S16 iRows;
	S16 iColumns;
	F32 fBoundWidth;
	F32 fBoundHeight;
} UIGenLayoutBoxState;

#endif
