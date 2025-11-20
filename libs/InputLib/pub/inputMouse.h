/***************************************************************************



***************************************************************************/

#ifndef INPUT_MOUSE_H
#define INPUT_MOUSE_H

#include "MatrixStack.h"
#include "inputData.h"
#include "CBox.h"

SA_RET_OP_VALID const CBox *mouseClipGet(void);
void mouseClipPush(SA_PARAM_OP_VALID const CBox *pBox);
void mouseClipPushRestrict(SA_PARAM_OP_VALID const CBox *pBox);
void mouseClipPop(void);

// Tests if the mouse collides with a box, respecting the current transformation matrix
bool mouseBoxCollisionTest(int x, int y, SA_PARAM_NN_VALID const CBox *pBox);

// Was a mouse button down event just generated?
int mouseDown( MouseButton button );
// Was a mouse button up event just generated?
int mouseUp( MouseButton button );
// Is the mouse button down at this exact moment?
int mouseIsDown( MouseButton button );
int filteredMouseIsDown( MouseButton button );
// Was a mouse click event (up and down) just generated?
int mouseClick( MouseButton button);
// Was a mouse double click event just generated?
int mouseDoubleClick(MouseButton button);
// Was a mouse drag event just generated?
int mouseDrag( MouseButton button);

#define mouseDownAny(button) (mouseDown(button) || mouseClick(button) || mouseDoubleClick(button) || mouseDrag(button))
#define mouseDownAnyHit(button, box) (mouseDownHit(button, box) || mouseClickHit(button, box) || mouseDoubleClickHit(button, box) || mouseDragHit(button, box))

#define mouseDownAnyButton() (mouseDown(MS_LEFT) || mouseDown(MS_MID) || mouseDown(MS_RIGHT) || mouseDown(MS_WHEELUP) || mouseDown(MS_WHEELDOWN))
#define mouseDownAnyButtonHit(box) (mouseDownHit(MS_LEFT, box) || mouseDownHit(MS_MID, box) || mouseDownHit(MS_RIGHT, box) || mouseDownHit(MS_WHEELUP, box) || mouseDownHit(MS_WHEELDOWN, box))

// Was a mouse click event (up and down) just generated? If so, return the location
int mouseDownCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y );
int mouseUpCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y );
int mouseClickCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y );
int mouseDoubleClickCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y );
int mouseDragCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y );

// Get the last location where the mouse was clicked (for either a click or a drag)
void mouseDownPos(MouseButton button, int *xp, int *yp);

// Was a mouse event generated inside the given box?
int mouseDownHit( MouseButton button, SA_PARAM_NN_VALID CBox *box );
int mouseUpHit( MouseButton button, SA_PARAM_NN_VALID CBox *box );
int mouseClickHit( MouseButton button, SA_PARAM_NN_VALID CBox *box );
int mouseDoubleClickHit(MouseButton button, SA_PARAM_NN_VALID CBox *box );
int mouseDragHit( MouseButton button, SA_PARAM_NN_VALID CBox *box);
int mouseScrollHit( SA_PARAM_NN_VALID CBox *box );

int mouseUnfilteredUp(MouseButton button);
int mouseUnfilteredDown(MouseButton button);

// Is the mouse currently inside the given box?
int mouseCollision( SA_PARAM_OP_VALID const CBox *box );
int unfilteredMouseCollision(SA_PARAM_NN_VALID CBox *box);

#define mouseClipperCollision() mouseCollision(mouseClipGet())

// Return the difference in mouse movement between now and the last frame
void mouseDiff(SA_PRE_NN_FREE SA_POST_NN_VALID S32 *xp, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *yp );
void mouseDiffNormalized(SA_PRE_NN_FREE SA_POST_NN_VALID F32 *xp, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *yp );
void mouseDiffLegacy(SA_PRE_NN_FREE SA_POST_NN_VALID S32 *xp, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *yp );

// Return the current mouse position
void mousePos(SA_PRE_NN_FREE SA_POST_NN_VALID S32 *xp, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *yp );
void mousePosCurrent( int * xp, int * yp );

// Clear out the mouse input buffer
void mouseClear(void);

// Set the lock state of the mouse on or off
void mouseLockThisFrame(void);
// Is the mouse currently locked?
int mouseIsLocked(void);

// How many clicks the scroll wheel scrolled this frame (+ for up, - for down)
int mouseZ(void);

void mouseCaptureZ(void);

// True if we got any mouse events this frame.
bool mouseDidAnything(void);

bool inpHasMouse(void);

// Returns true if the last button press was within the CBox, and the given button is
// still down and still within the CBox.
bool mouseDownOver(MouseButton button, const CBox *pBox);

// Returns true if the mouse is currently down and went down over the given box,
// but is not necessarily still within it.
bool mouseDownWasOver(MouseButton button, const CBox *pBox);

// Scan the entire mouse input buffer and see what happened in a particular area.
bool mouseGetEvents(const CBox *pBox,
					SA_PRE_NN_FREE SA_POST_NN_VALID bool *pbCollision,
					SA_PRE_NN_ELEMS(MS_MAXBUTTON) bool abIsDown[MS_MAXBUTTON],
					SA_PRE_NN_ELEMS(MS_MAXBUTTON) bool abDown[MS_MAXBUTTON],
					SA_PRE_NN_ELEMS(MS_MAXBUTTON) bool abDownWasOver[MS_MAXBUTTON],
					SA_PRE_NN_ELEMS(MS_MAXBUTTON) bool abUp[MS_MAXBUTTON],
					SA_PRE_NN_ELEMS(MS_MAXBUTTON) bool abClick[MS_MAXBUTTON], 
					SA_PRE_NN_ELEMS(MS_MAXBUTTON) bool abDoubleClick[MS_MAXBUTTON],
					SA_PRE_NN_ELEMS(MS_MAXBUTTON) bool abDrag[MS_MAXBUTTON]);

// returns true if the mouse is over this box, or is down/clicked over this box.
int mouseInvolvedWith(const CBox *pBox);

extern TransformationMatrix **eaInputMatrixStack;

void transformMousePos(int iXIn,int iYYIn, int *pXOut, int *pYOut);
void mouseSetScreenPercent(F32 xPct, F32 yPct);
void mouseSetScreen(int x, int y);
#define inputMatrixPop() matrixStackPop(&eaInputMatrixStack)
#define inputMatrixPush() matrixStackPush(&eaInputMatrixStack)
#define inputMatrixGet() matrixStackGet(&eaInputMatrixStack)

F32 mouseGetTimesliceMS();

#endif
