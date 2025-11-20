#ifndef UI_GEN_LIST_H
#define UI_GEN_LIST_H
GCC_SYSTEM

//////////////////////////////////////////////////////////////////////////
// The list is a tricky widget since it needs to contain (virtually)
// several thousand items. So the Gens specified in data actually need 
// to be templates for the rows.

#include "UIGen.h"
#include "UIGenScrollbar.h"

typedef struct SMFBlock SMFBlock;
typedef struct TextAttribs TextAttribs;

AUTO_ENUM;
typedef enum UIGenListFitParentMode
{
	// Will size the list rows such that all rows take up the same amount of height,
	// and will fill the list box completely.
	kUIGenListFitParentMode_Fill,

	// Will size the unselected list rows to the given fixed height and the selected
	// list row to fill the remaining space.
	kUIGenListFitParentMode_Accordion,
} UIGenListFitParentMode;

AUTO_STRUCT;
typedef struct UIGenListFitParent
{
	// How FitParent behaves for the list
	UIGenListFitParentMode eMode; AST(STRUCTPARAM)

	// A parameter for the FitParent mode.
	// For the Accordion mode, Value represents the minimum height of a list row.
	F32 fValue; AST(STRUCTPARAM)
} UIGenListFitParent;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenListSelectionGroup
{
	void* pvGen; NO_AST // For pointer comparison only, do not deref
	S32 iSelectedRow;
} UIGenListSelectionGroup;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenListColumn
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeListColumn))

	//Text in the column's header, evaluated once per column
	REF_TO(Message) hSMF; AST(NAME(SMF) NAME(Text) NON_NULL_REF)
	Expression *pTextExpr; AST(NAME(SMFExpr) REDUNDANT_STRUCT(TextExpr, parse_Expression_StructParam) LATEBIND WIKI(AUTO))
	REF_TO(Message) hTruncate; AST(NAME(Truncate) NON_NULL_REF)
	REF_TO(UIStyleFont) hFont; AST(NAME(Font))

	REF_TO(UIGen) hCellTemplate; AST(NAME(CellTemplate) NAME(Template))
	UIGenChild **eaTemplateChild; AST(NAME(TemplateChild) NAME(TemplateChildren))

	//Used in conjunction with the sortable flag to determine how to sort it
	const char *pchTPIField; AST(POOL_STRING)

	UIGenAction *pOnActivated;
	UIGenAction *pOnSelected;

	U8 eAlignment; AST(NAME(Alignment) DEFAULT(UITopLeft) SUBTABLE(UIDirectionEnum))
	U8 iStretchyRatio; AST(NAME(StretchyRatio))

	bool bSortable : 1; AST(NAME(Sortable) DEFAULT(1))
	bool bResizable : 1; AST(NAME(Resizable) DEFAULT(1))

	// SMF control
	bool bShrinkToFit : 1; // Shrinks down to fit, won't scale up.
	bool bScaleToFit : 1;  // Shrinks down or scales up to fit.
	bool bNoWrap : 1;      // Don't do any word wrapping
	bool bPlainText : 1;   // Don't treat the text as SMF. This allows Truncate to work.
	bool bSafeMode : 1;    // Only parse safe SMF tags.
	bool bFilterProfanity : 1; // Filter profanity
} UIGenListColumn;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenListColumnState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeListColumn))
	UIGen *pList; AST(UNOWNED STRUCT_NORECURSE)
	UIGen *pCellTemplate; AST(UNOWNED STRUCT_NORECURSE)
	UIGen **eaRows; AST(STRUCT_NORECURSE NO_INDEX UNOWNED)
	UIGen **eaOwnedRows; AST(STRUCT_NORECURSE NO_INDEX)
	UIGenChild **eaTemplateGens; AST(NO_INDEX)
	S32 iColumn;
	S32 iTPICol;	AST(DEFAULT(-1))
	char *estrPlainString; AST(NAME(PlainString) ESTRING)
	SMFBlock *pBlock; NO_AST
	TextAttribs *pAttribs; NO_AST
	UIGenBundleTruncateState Truncate;
} UIGenListColumnState;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenListRow
{
	UIGenInternal polyp; AST(POLYCHILDTYPE(kUIGenTypeListRow))
	REF_TO(Message) hSMF; AST(NAME(SMF) NAME(Text) NON_NULL_REF)
	Expression *pTextExpr; AST(NAME(SMFExpr) REDUNDANT_STRUCT(TextExpr, parse_Expression_StructParam) LATEBIND WIKI(AUTO))
	REF_TO(Message) hTruncate; AST(NAME(Truncate) NON_NULL_REF)
	REF_TO(UIStyleFont) hFont; AST(NAME(Font))
	UIGenAction *pOnActivated;
	UIGenAction *pOnSelected;

	U8 eAlignment; AST(NAME(Alignment) DEFAULT(UITopLeft) SUBTABLE(UIDirectionEnum))

	bool bAllowSelect : 1; AST(DEFAULT(1))
	// SMF control
	bool bShrinkToFit : 1; // Shrinks down to fit, won't scale up.
	bool bScaleToFit : 1;  // Shrinks down or scales up to fit.
	bool bNoWrap : 1;      // Don't do any word wrapping
	bool bPlainText : 1;   // Don't treat the text as SMF. This allows Truncate to work.
	bool bSafeMode : 1;    // Only parse safe SMF tags.
	bool bFilterProfanity : 1; // Filter profanity
	bool bAllowInteract : 1; // Allow clicking href links
} UIGenListRow;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenListRowState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeListRow))
	UIGen *pList; AST(UNOWNED STRUCT_NORECURSE)
	UIGen *pRowTemplate; AST(UNOWNED STRUCT_NORECURSE)
	S32 iRow;
	S32 iCol;
	S32 iDataRow;
	char *estrPlainString; AST(NAME(PlainString) ESTRING)
	void *pModelData; NO_AST
	SMFBlock *pBlock; NO_AST
	TextAttribs *pAttribs; NO_AST
	UIGenBundleTruncateState Truncate;
} UIGenListRowState;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenList
{
	UIGenInternal polyp;			AST(POLYCHILDTYPE(kUIGenTypeList))
	Expression *pGetModel;			AST(NAME(ModelBlock) REDUNDANT_STRUCT(Model, parse_Expression_StructParam) LATEBIND)
	Expression *pFilter;			AST(NAME(FilterBlock) REDUNDANT_STRUCT(Filter, parse_Expression_StructParam) LATEBIND)

	UIGenScrollbar scrollbar;		AST(EMBEDDED_FLAT)
	
	//If you have a sortable list with columns, this will be displayed when it is sorted ascending
	//UIGenBundleTexture SortAscIndicatorBundle; AST(EMBEDDED_FLAT(SortAscIndicator))
	const char *pchSortAscIndicatorTexture; AST(NAME(SortAscIndicatorTexture) RESOURCEDICT(Texture) POOL_STRING)
	UIDirection eSortAscIndicatorAlignment; AST(NAME(SortAscIndicatorAlignment))
	//Same but descending
	//UIGenBundleTexture SortDesIndicatorBundle; AST(EMBEDDED_FLAT(SortDesIndicator))
	const char *pchSortDesIndicatorTexture; AST(NAME(SortDesIndicatorTexture) RESOURCEDICT(Texture) POOL_STRING)
	UIDirection eSortDesIndicatorAlignment; AST(NAME(SortDesIndicatorAlignment))

	//Offset the sort indicators by this many pixels, scaled by the gen's scale
	S16 iSortAscIndicatorX; AST(NAME(SortAscIndicatorX))
	S16 iSortAscIndicatorY; AST(NAME(SortAscIndicatorY))
	S16 iSortDesIndicatorX; AST(NAME(SortDesIndicatorX))
	S16 iSortDesIndicatorY; AST(NAME(SortDesIndicatorY))

	//The color line drawn when moving columns
	U32 uMoveColor; AST(SUBTABLE(ColorEnum) FORMAT_COLOR NAME(MoveColor) NAME(MoveColColor) DEFAULT(0x003366ff))

	UIGenChild **eaColumnChildren;	AST(NAME(Columns) NAME(Column))
	REF_TO(UIGen) hRowTemplate;		AST(NAME(RowTemplate) NAME(Template))
	Expression *pRowTemplateExpr;	AST(NAME(RowTemplateExprBlock) REDUNDANT_STRUCT(RowTemplateExpr, parse_Expression_StructParam) LATEBIND)
	UIGenChild **eaTemplateChild; AST(NAME(TemplateChild) NAME(TemplateChildren))

	const char* pchSelectionGroup;	AST(POOL_STRING)
	REF_TO(UIGen) hNextList;		AST(NAME(NextList))
	REF_TO(UIGen) hPreviousList;	AST(NAME(PreviousList))

	S16 iRowSpacing;				AST(SUBTABLE(UISizeEnum))       // Space between rows
	S16 iListTopMargin;				AST(SUBTABLE(UISizeEnum))    // Space above first row
	S16 iListBottomMargin;			AST(SUBTABLE(UISizeEnum)) // Space below last row
	S16 iDefaultSortCol;			AST(DEFAULT(-1))
	UISortType eDefaultSortMode;	AST(DEFAULT(UISortDescending))

	Expression *pAllowSelect;		AST(NAME(AllowSelectBlock) REDUNDANT_STRUCT(AllowSelect, parse_Expression_StructParam) LATEBIND)

	UIGenListFitParent FitParentMode;

	bool bSlowSync : 1;  // Synchronizes with model more carefully so that the correct rows are added/removed

	bool bPreserveOrder : 1;		AST(DEFAULT(1))

	// Determines whether or not bForceShowSelectedRow gets set when a row is selected
	bool bShowSelectedRow : 1;		AST(DEFAULT(1))
	
	// If this is set and this is a nested list, row selection will update the scrollbar of the parent list
	bool bUpdateParentScrollBar : 1;

	bool bMovableColumns : 1;		AST(NAME(MovableColumns) DEFAULT(1))

	// If this is set then TemplateChildren or (the column's TemplateChildren) are appended to the list model
	bool bAppendTemplateChildren : 1;

	// Version number of this gen. Increase this to invalidate the user's
	// preferred size and ordering of the columns
	S32 iVersion;
} UIGenList;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct UIGenListState
{
	UIGenPerTypeState polyp; AST(POLYCHILDTYPE(kUIGenTypeList))

	S32 iSelectedRow; AST(DEFAULT(-1))
	S16 iResizeCol; AST(DEFAULT(-1))
	S16 iMoveCol; AST(DEFAULT(-1))
	S16 iDestCol; AST(DEFAULT(-1))
	S16 iSortCol; AST(DEFAULT(-1))
	UISortType eSortMode;

	S32 iMouseInsideRow;
	S16 iMouseInsideCol;

	S32 iLastMouseInsideRow; AST(DEFAULT(-1))
	S16 iLastMouseInsideCol;

	UIGenBundleTexture SortAscIndicatorBundleTexture;
	UIGenBundleTexture SortDesIndicatorBundleTexture;

	UIGenBundleTextureState SortAscIndicatorBundle;
	UIGenBundleTextureState SortDesIndicatorBundle;

	UIGenScrollbarState scrollbar;

	void **eaFilteredList; NO_AST

	const char **eaBadTemplateNames; AST(POOL_STRING)

	UIGen **eaRows; AST(STRUCT_NORECURSE NO_INDEX UNOWNED)
	UIGen **eaOwnedRows; AST(STRUCT_NORECURSE NO_INDEX)
	UIGenChild **eaTemplateGens; AST(NO_INDEX)

	UIColumn **eaColumnOrder; AST(STRUCT_NORECURSE NO_INDEX)
	UIGen **eaCols; AST(STRUCT_NORECURSE NO_INDEX)

	//Specifies which side of the iDestCol the line is being drawn on
	bool bDestDrawHigh;

	bool bForceShowSelectedRow;

	// If true, we don't actually make the selected row appear/act selected, but
	// we still remember it so up/down will behave as in Windows.
	bool bSelectedRowNotSelected;

	// If true, this list is unselected entirely.
	bool bUnselected;

	// If no row was available when we tried to select it, save the row we wanted to
	// select until we build the list. This probably does not cooperate with
	// unselectable rows in lists.
	S32 iDesiredRowSelection; AST(DEFAULT(-1))
} UIGenListState;

void ui_GenListSetSelectedRow(UIGen *pGen, S32 iRow);

void ui_GenListActivateSelectedRow(UIGen *pGen);

S32 ui_GenListNextSelectableRow(UIGen *pGen, UIGenList *pList, UIGenListState *pState, S32 iRow);
S32 ui_GenListPreviousSelectableRow(UIGen *pGen, UIGenList *pList, UIGenListState *pState, S32 iRow);
S32 ui_GenListNearSelectableRow(UIGen *pGen, UIGenList *pList, UIGenListState *pState, S32 iRow);

bool ui_GenListSyncWithModel(UIGen *pGen, UIGenList *pList, UIGenListState *pState);

S32 ui_GenListGetSize(UIGenListState *pState);

#endif
