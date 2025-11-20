/***************************************************************************



***************************************************************************/

#ifndef UI_COORDINATE_H
#define UI_COORDINATE_H
GCC_SYSTEM

#include "UICore.h"

typedef struct UICoordinate UICoordinate;

typedef void (*UICoordinateDoubleClickCallback) (UICoordinate *pCoord, F32 fX, F32 fY, UserData doubleClickData);

//////////////////////////////////////////////////////////////////////////
// A UICoordinate is a simple point placed on an X and Y unit axis.
// FIXME: This is basically a degenerate UIGraph.
typedef struct UICoordinate
{
	UIWidget widget;

	// The current position, stored as [0..1].
	F32 fX;
	F32 fY;

	UIActivationFunc movedF;
	UserData movedData;

	UICoordinateDoubleClickCallback doubleClickF;
	UserData doubleClickData;

	bool bDrawCenter : 1;
} UICoordinate;

SA_RET_NN_VALID UICoordinate *ui_CoordinateCreate(void);
void ui_CoordinateInitialize(SA_PARAM_NN_VALID UICoordinate *pCoord);
void ui_CoordinateTick(SA_PARAM_NN_VALID UICoordinate *pCoord, UI_PARENT_ARGS);
void ui_CoordinateDraw(SA_PARAM_NN_VALID UICoordinate *pCoord, UI_PARENT_ARGS);
void ui_CoordinateFreeInternal(SA_PRE_NN_VALID SA_POST_P_FREE UICoordinate *pCoord);

void ui_CoordinateDrawCenter(SA_PARAM_NN_VALID UICoordinate *pCoord, bool bDrawCenter);
void ui_CoordinateGetLocation(SA_PARAM_NN_VALID UICoordinate *pCoord, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *pfX, SA_PRE_OP_FREE SA_POST_OP_VALID F32 *pfY);
void ui_CoordinateSetLocation(SA_PARAM_NN_VALID UICoordinate *pCoord, F32 fX, F32 fY);
void ui_CoordinateSetLocationAndCallback(SA_PARAM_NN_VALID UICoordinate *pCoord, F32 fX, F32 fY);
void ui_CoordinateSetMovedCallback(SA_PARAM_NN_VALID UICoordinate *pCoord, UIActivationFunc movedF, UserData movedData);
void ui_CoordinateSetDoubleClickCallback(SA_PARAM_NN_VALID UICoordinate *pCoord, UICoordinateDoubleClickCallback doubleClickF, UserData doubleClickData);

#endif