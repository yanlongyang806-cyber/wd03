/***************************************************************************



***************************************************************************/
#ifndef UI_SIZER_H
#define UI_SIZER_H
GCC_SYSTEM

#include "UICore.h"

#define UI_SIZER_TYPE UISizer sizer;
#define UI_SIZER(s) (&(s)->sizer)

#define UI_SIZER_CHILD_DATA_TYPE UISizerChildData childData;
#define UI_SIZER_CHILD_DATA(s) (&(s)->childData)

typedef struct UISizer UISizer;
typedef void UIAnySizer;

typedef void (*SizerCalcMinFunction)(SA_PARAM_NN_VALID UIAnySizer *pSizer, Vec2 minSizeOut);

typedef void (*SizerRecalcSizesFunction)(SA_PARAM_NN_VALID UIAnySizer *pSizer, F32 x, F32 y, F32 width, F32 height);

typedef enum UISizerChildType
{
	UISizerChildType_None,
	UISizerChildType_Widget,
	UISizerChildType_Sizer
} UISizerChildType;

typedef struct UISizerChildData
{
	UISizerChildType type;

	union {
		UIWidget *pWidget;
		UISizer *pSizer;
	};

	int proportion;
	UIDirection direction;
	int border;

	Vec2 minSize; // calculated and cached minimum size
} UISizerChildData;

/**
 * The "abstract base class" for all sizers.
 *
 * These are never created directly by client code. Use ui_BoxSizerCreate and ui_GridSizerCreate to create a UIBoxSizer and a UIGridSizer, respectively.
 * Run the ui_Tests command and check out the Sizers demos for working examples of these Sizers.
 *
 * Sizers offer an alternative means of positioning and sizing Widgets, other than fixed position/size and parent-relative percentage position/size. Sizers
 * are a more traditional widget layout mechanism that relies on the ability to compute the minimum size required by each widget in a hierarchy of widgets.
 *
 * At runtime, Sizers exist as an object hierarchy parallel to the widget object hierarchy. Therefore, Sizers are lightweight and do not interfere with the
 * Widget hierarchy chain of responsibility event propagation nor the Widget draw calls.
 *
 * For example, let's assume you have a dialog box made up of a UIWindow containing a UIPane. Imagine this UIPane has all of the buttons and other controls
 * for the dialog as immediate children of the UIPane. A Sizer hierarchy can be constructed that is parallel to, but independent of, the widget hierarchy.
 * This Sizer hierarchy has the sole responsibility of managing the size and position of all the controls in the dialog, regardless of the dialog's size.
 *
 * This independence allows for Sizers to be constructed for existing Widget hierarchies without modifying the Widget hierarchy.
 *
 * Here's some pseudo-code that takes the example further. Assume the dialog's UIPane has "Ok" and "Cancel" UIButtons at the bottom-right and a UIList taking
 * up the majority of the dialog.
 *
 *		UIWindow *window = ui_WindowCreate(...)
 *		UIPane *pane = ui_PaneCreate(...)
 *		UIList *list = ui_ListCreate(...)
 *		UIButton *okButton = ui_ButtonCreate(...)
 *		UIButton *cancelButton = ui_ButtonCreate(...)
 *
 *		ui_WidgetAddChild(UI_WIDGET(window), UI_WIDGET(pane));
 *		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(list));
 *		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(okButton));
 *		ui_WidgetAddChild(UI_WIDGET(pane), UI_WIDGET(cancelButton));
 *
 *		UIBoxSizer *verticalBoxSizer = ui_BoxSizerCreate(UIVertical);
 *		UIBoxSizer *horizontalBoxSizer = ui_BoxSizerCreate(UIHorizontal);
 *
 *		ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(list), 1, UIWidth, 5); // stretches to fill vertical and horizontal space
 *		ui_BoxSizerAddSizer(verticalBoxSizer, UI_SIZER(horizontalBoxSizer), 0, UIWidth, 5); // stretches to fill horizontal space only
 *
 *		ui_BoxSizerAddFiller(horizontalBoxSizer, 1); // this takes up all excess space to the left of the buttons
 *		ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(cancelButton), 0, UINoDirection, 5); // non-stretching, centered button with 5px border
 *		ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(okButton), 0, UINoDirection, 5); // non-stretching, centered button with 5px border
 *
 *		ui_WidgetSetSizer(UI_WIDGET(pane), UI_SIZER(verticalBoxSizer)); // attach the Sizer hierarchy to the Pane widget to manage its children's position and size
 *
 * The construction of a widget hierarchy and its layout can be made more succinct by attaching the Sizer to the Widget *before* adding the widgets to the Sizer
 * hierarchy. When adding Widgets to a Sizer, they are automatically added to the Widget the Sizer is attached to if they are not already in the group.
 *
 *		UIWindow *window = ui_WindowCreate(...)
 *		UIPane *pane = ui_PaneCreate(...)
 *		UIList *list = ui_ListCreate(...)
 *		UIButton *okButton = ui_ButtonCreate(...)
 *		UIButton *cancelButton = ui_ButtonCreate(...)
 *
 *		ui_WidgetAddChild(UI_WIDGET(window), UI_WIDGET(pane));
 *
 *		ui_WidgetSetSizer(UI_WIDGET(pane), UI_SIZER(verticalBoxSizer)); // attach the Sizer hierarchy to the Pane widget early
 *
 *		UIBoxSizer *verticalBoxSizer = ui_BoxSizerCreate(UIVertical);
 *		UIBoxSizer *horizontalBoxSizer = ui_BoxSizerCreate(UIHorizontal);
 *
 *		ui_BoxSizerAddWidget(verticalBoxSizer, UI_WIDGET(list), 1, UIWidth, 5);
 *		ui_BoxSizerAddSizer(verticalBoxSizer, UI_SIZER(horizontalBoxSizer), 0, UIWidth, 5);
 *
 *		ui_BoxSizerAddFiller(horizontalBoxSizer, 1);
 *		ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(cancelButton), 0, UINoDirection, 5); // automatically adds the button to the pane widget
 *		ui_BoxSizerAddWidget(horizontalBoxSizer, UI_WIDGET(okButton), 0, UINoDirection, 5); // automatically adds the button to the pane widget
 *
 * Widgets can be added/removed to the Widget hierarchy (or the Sizer hierarchy) and the Sizer will simply adjust its computations appropriately. When the root
 * of the Widget hierarchy is destroyed (using ui_WidgetFreeInternal) the Sizer is automatically destroyed. However, if the Sizer hierarchy is destroyed, it will
 * simply be detached from the Widget. The Widget hierarchy will not be destroyed.
 */
typedef struct UISizer
{
	// The Widget that this Sizer manages the layout for.
	UIWidget *pWidget;

	// This method must be implemented by all sizers. Here, the sizer will do the actual calculation of its children's minimal sizes.
	SizerCalcMinFunction calcMinF;

	// This method must be implemented by all sizers. Here, the sizer will do the actual calculation of its children's positions and sizes.
	SizerRecalcSizesFunction recalcSizesF;

	UISizerChildData **children;
} UISizer;

// Used by Sizer implementations to initialize the Sizer and set the callback functions
void ui_SizerInitialize(SA_PARAM_NN_VALID UISizer *pSizer, SA_PARAM_NN_VALID SizerCalcMinFunction calcMinF, SA_PARAM_NN_VALID SizerRecalcSizesFunction recalcSizesF);

/* Frees the Sizer data that is common to all Sizer implementations. Does not actually free the Sizer itself.
 *
 * If the Sizer is attached to a Widget, it will be detached.
 */
void ui_SizerFreeInternal(UISizer *pSizer);

void ui_InitializeSizerChildDataWithNone(SA_PARAM_NN_VALID UISizerChildData *pSizerChildData, int minWidth, int minHeight);
void ui_InitializeSizerChildDataWithWidget(SA_PARAM_NN_VALID UISizerChildData *pSizerChildData, SA_PARAM_NN_VALID UIWidget *pWidget);
void ui_InitializeSizerChildDataWithSizer(SA_PARAM_NN_VALID UISizerChildData *pSizerChildData, SA_PARAM_NN_VALID UISizer *pSizer);

// Utility function that calculates the minimum sizes for all Sizers in the Sizer hierarchy.
void ui_SizerGetMinSize(SA_PARAM_NN_VALID UISizer *pSizer, Vec2 minSizeOut);

// Perform the layout for the Sizer hierarchy. This calls the Sizer's calcMin function and recalcSizes function in that order.
void ui_SizerLayout(SA_PARAM_NN_VALID UISizer *pSizer, F32 width, F32 height);

// Calculates and returns the minimum size desired for the sizer and all its child widgets and sizers. Should be invoked at the beginning of each calcMinF callback.
void ui_SizerCalcMinSizeRecurse(SA_PARAM_NN_VALID UISizer *pSizer);

// Utility function for Sizers to have their child Widgets added as children of the provided Widget.
void ui_SizerAddChildWidgetsToWidgetRecurse(UISizer *pSizer, UIWidget *pWidget);

// Obtains the child Widget's sizer child data, if the child Widget is in fact managed by this Sizer hierarchy. Otherwise, NULL is returned.
SA_RET_OP_VALID UISizerChildData *ui_SizerGetWidgetChildData(SA_PARAM_NN_VALID UISizer *pSizer, SA_PARAM_OP_VALID UIWidget *pChildWidget);

#endif
