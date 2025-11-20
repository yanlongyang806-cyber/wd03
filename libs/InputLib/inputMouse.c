/***************************************************************************



***************************************************************************/

//////////////////////////////////////////////////////////////////////////
// Wrappers for looking at the mouse input buffer
#include "timing.h"
#include "input.h"
#include "inputMouse.h"
#include "RenderLib.h"
#include "MemoryPool.h"
#include "EArray.h"
#include "MatrixStack.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// FIXME: This is almost exactly the same as GraphicsLib's Clipper2D
// behavior. Both should probably be moved to UtilitiesLib or something.
typedef struct InputMouseClip
{
	CBox box;
} InputMouseClip;

MP_DEFINE(InputMouseClip);
static InputMouseClip **s_eaClipperStack;

TransformationMatrix **eaInputMatrixStack = NULL;

void transformMousePos(int iXIn,int iYYIn, int *pXOut, int *pYOut)
{
	Mat3 *pMatrix = inputMatrixGet();	
	if (pMatrix && gInput)
	{
		Vec3 v3Mouse = { iXIn, gInput->screenHeight - iYYIn, 1 };
		Vec3 v3Out;
		mulVecMat3(v3Mouse, *pMatrix, v3Out);
		if(pXOut)
			*pXOut = v3Out[0];
		if(pYOut)
			*pYOut = gInput->screenHeight - v3Out[1];
	}
	else
	{
		if(pXOut)
			*pXOut = iXIn;
		if(pYOut)
			*pYOut = iYYIn;
	}
}

bool mouseBoxCollisionTest(int x, int y, SA_PARAM_NN_VALID const CBox *pBox)
{
	Mat3* pMatrix = matrixStackGet(&eaInputMatrixStack);
	if (pMatrix)
	{
		int i;
		Vec3 v3in = { x, gInput->screenHeight - y, 1 };
		Vec3 v3out;
		Vec2 v3Corners[4] = 
		{
			{ pBox->lx, gInput->screenHeight - pBox->ly },
			{ pBox->hx, gInput->screenHeight - pBox->ly },
			{ pBox->hx, gInput->screenHeight - pBox->hy },
			{ pBox->lx, gInput->screenHeight - pBox->hy }
		};
		mulVecMat3(v3in, *pMatrix, v3out);
		for (i=0; i < ARRAY_SIZE_CHECKED(v3Corners); i++)
		{
			const Vec2 v2Corner1 = { v3Corners[i][0], v3Corners[i][1] };
			const Vec2 v2Corner2 = { v3Corners[(i+1)%4][0], v3Corners[(i+1)%4][1] };
			const Vec2 v2Edge = { v2Corner2[0]-v2Corner1[0], v2Corner2[1]-v2Corner1[1] };
			const Vec2 v2Mouse = { v3out[0]-v2Corner1[0], v3out[1]-v2Corner1[1] };

			if (dotVec2(v2Edge, v2Mouse) < 0)
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		return point_cbox_clsn(x, y, pBox);
	}
}

const CBox *mouseClipGet(void)
{
	InputMouseClip *pClip = (InputMouseClip*)eaGet(&s_eaClipperStack, eaSize(&s_eaClipperStack) - 1);
	return pClip ? &pClip->box : NULL;
}

void mouseClipPushRestrict(const CBox *pBox)
{
	if (pBox)
	{
		const CBox *pOldBox = mouseClipGet();
		if (pOldBox)
		{
			CBox newBox = *pOldBox;
			CBoxClipTo(pBox, &newBox);
			mouseClipPush(&newBox);
		}
		else
			mouseClipPush(pBox);
	}
	else
		mouseClipPush(pBox);
}

void mouseClipPush(const CBox *pBox)
{
	InputMouseClip *pClip;
	S32 iClipIndex;

	MP_CREATE(InputMouseClip, 16);

	pClip = MP_ALLOC(InputMouseClip);
	iClipIndex = eaPush(&s_eaClipperStack, pClip);

	// InputLib doesn't know the actual screen size, so we'll just make
	// a reasonable assumption...
	if (!pBox)
		BuildCBox(&pClip->box, 0, 0, 100000, 100000);
	else
		pClip->box = *pBox;
	devassertmsg(iClipIndex < 128, "Too many input clip areas pushed, someone is forgetting to pop them.");
}

void mouseClipPop(void)
{
	InputMouseClip *clipper = eaPop(&s_eaClipperStack);
	if (devassertmsg(clipper, "Trying to pop a non-existent clip area."))
		MP_FREE(InputMouseClip, clipper);
}

__forceinline static int mouseClipCollision(S32 x, S32 y)
{
	const CBox *pBox = mouseClipGet();
	return !pBox || mouseBoxCollisionTest(x, y, pBox);
}

// returns true if the mouse was pressed down
int mouseDown( MouseButton button )
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if (gInput->mouseInpBuf[i].states[button]  == MS_DOWN &&
			mouseClipCollision(gInput->mouseInpBuf[i].x, gInput->mouseInpBuf[i].y))
		{
			return true;
		}
	}

	return false;

}

int mouseUnfilteredDown(MouseButton button)
{
	S32 i;
	if (!gInput)
		return false;
	for( i = 0; i < gInput->mouseBufSize; i++ )
		if (gInput->mouseInpBuf[i].states[button] == MS_DOWN)
			return true;
	return false;
}

int mouseUnfilteredUp(MouseButton button)
{
	S32 i;
	if (!gInput)
		return false;
	for( i = 0; i < gInput->mouseBufSize; i++ )
		if (gInput->mouseInpBuf[i].states[button] == MS_UP)
			return true;
	return false;
}

// returns true if the mouse was released 
int mouseUp( MouseButton button )
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if (gInput->mouseInpBuf[i].states[button] == MS_UP &&
			mouseClipCollision(gInput->mouseInpBuf[i].x, gInput->mouseInpBuf[i].y))
		{
			return true;
		}
	}
	return false;
}

// returns true if the mouse button is currently down
int mouseIsDown( MouseButton button )
{
	if (gInput && gInput->mouseInpCur.states[button] == MS_DOWN)
	{
		return true;
	}

	return false;
}

int filteredMouseIsDown( MouseButton button )
{
	if (!MouseInputIsAllowed())
		return false;

	return mouseIsDown(button);
}

// returns true if the mouse button was clicked
int mouseClick(MouseButton button)
{
	int i;
 
	if (!MouseInputIsAllowed())
		return false;

 	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if ( gInput->mouseInpBuf[i].states[button] == MS_CLICK )
		{
			return true;	
		}
	}

	return false;
}

// returns true if the mouse button was double clicked
int mouseDoubleClick(MouseButton button)
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if (gInput->mouseInpBuf[i].states[button] == MS_DBLCLICK &&
			mouseClipCollision(gInput->mouseInpBuf[i].x, gInput->mouseInpBuf[i].y))
		{
			return true;	
		}
	}

	return false;
}

int mouseDrag(MouseButton button)
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if (gInput->mouseInpBuf[i].states[button] == MS_DRAG &&
			mouseClipCollision(gInput->mouseInpBuf[i].x, gInput->mouseInpBuf[i].y))
		{
			return true;	
		}
	}

	return false;
}

int mouseStateCoords( MouseButton button, mouseState state, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y )
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if (gInput->mouseInpBuf[i].states[button] == state &&
			mouseClipCollision(gInput->mouseInpBuf[i].x, gInput->mouseInpBuf[i].y))
		{
			*x = gInput->mouseInpBuf[i].x;
			*y = gInput->mouseInpBuf[i].y;
			return true;	
		}
	}

	return false;
}

int mouseDownCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y )
{
	return mouseStateCoords( button, MS_DOWN, x, y );
}

int mouseUpCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y )
{
	return mouseStateCoords( button, MS_UP, x, y );
}

int mouseClickCoords( MouseButton button, int *x, int *y )
{
	return mouseStateCoords( button, MS_CLICK, x, y );
}

int mouseDoubleClickCoords( MouseButton button, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *x, SA_PRE_NN_FREE SA_POST_NN_VALID S32 *y )
{
	return mouseStateCoords( button, MS_DBLCLICK, x, y );
}


int mouseDragCoords( MouseButton button, int *x, int *y )
{
	return mouseStateCoords( button, MS_DRAG, x, y );
}

int mouseScrollHit( SA_PARAM_NN_VALID CBox *box )
{
	int i;

	if (!MouseScrollInputIsAllowed())
		return false;

	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if ( gInput->mouseInpBuf[i].states[MS_WHEELDOWN] == MS_CLICK || 
			 gInput->mouseInpBuf[i].states[MS_WHEELUP] == MS_CLICK )
		{
			int xp = gInput->mouseInpBuf[i].x;
			int yp = gInput->mouseInpBuf[i].y;

			if (mouseBoxCollisionTest(xp, yp, box) && mouseClipCollision(xp, yp))
				return true;
		}
	}

	return false;
}

// returns true if the mouse button is dragging inside the given box
int mouseDragHit( MouseButton button, CBox *box )
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if ( gInput->mouseInpBuf[i].states[button] == MS_DRAG )
		{
			int xp = gInput->mouseInpBuf[i].x;
			int yp = gInput->mouseInpBuf[i].y;

			if (mouseBoxCollisionTest(xp, yp, box) && mouseClipCollision(xp, yp))
				return true;
		}
	}

	return false;
}

// returns true if the mouse button was clicked while over a given box
int mouseDownHit( MouseButton button,  CBox *box )
{
 	int i;

	if (!MouseInputIsAllowed())
		return false;

 	for ( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if ( gInput->mouseInpBuf[i].states[button] == MS_DOWN )
		{
			int xp = gInput->mouseInpBuf[i].x;
			int yp = gInput->mouseInpBuf[i].y;

			if ((!box || mouseBoxCollisionTest(xp, yp, box)) && mouseClipCollision(xp, yp))
				return true;
		}
	}

	return false;
}

// returns true if the mouse button was released while over a given box
int mouseUpHit( MouseButton button, CBox *box )
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for ( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if ( gInput->mouseInpBuf[i].states[button] == MS_UP )
		{
    		int xp = gInput->mouseInpBuf[i].x;
			int yp = gInput->mouseInpBuf[i].y;
			if ((!box || mouseBoxCollisionTest(xp, yp, box)) && mouseClipCollision(xp, yp))
				return true;
		}
	}
	return false;
}

//
//
int mouseClickHit( MouseButton button, CBox *box )
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for ( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if ( gInput->mouseInpBuf[i].states[button] == MS_CLICK )
		{
			int xp = gInput->mouseInpBuf[i].x;
			int yp = gInput->mouseInpBuf[i].y;
			if ((!box || mouseBoxCollisionTest(xp, yp, box)) && mouseClipCollision(xp, yp))
				return true;
		}
	}

	return false;
}

int mouseDoubleClickHit( MouseButton button, CBox *box )
{
	int i;

	if (!MouseInputIsAllowed())
		return false;

	for ( i = 0; i < gInput->mouseBufSize; i++ )
	{
		if ( gInput->mouseInpBuf[i].states[button] == MS_DBLCLICK )
		{
			int xp = gInput->mouseInpBuf[i].x;
			int yp = gInput->mouseInpBuf[i].y;
			if ((!box || mouseBoxCollisionTest(xp, yp, box)) && mouseClipCollision(xp, yp))
				return true;
		}
	}

	return false;
}

// returns true if the mouse did collide
//
int mouseCollision(const CBox * box)
{
	int xp, yp;

	if (!MouseInputIsAllowed())
		return false;

   	xp = gInput->mouseInpCur.x;
 	yp = gInput->mouseInpCur.y;

	return ((!box || mouseBoxCollisionTest(xp, yp, box)) && mouseClipCollision(xp, yp));
}

int mouseInvolvedWith(const CBox *pBox)
{
	int i;
	int xp, yp;
	const CBox *pClipBox = mouseClipGet();
	CBox Box = *pBox;
	if (!MouseInputIsAllowed())
		return false;

	CBoxClipTo(pClipBox, &Box);

	xp = gInput->mouseInpCur.x;
	yp = gInput->mouseInpCur.y;

	if (mouseBoxCollisionTest(gInput->mouseInpCur.x, gInput->mouseInpCur.y, &Box))
		return true;
	else
	{
		for (i = 0; i < ARRAY_SIZE(gInput->buttons); i++)
		{
			if ((gInput->buttons[i].state == MS_DOWN || gInput->buttons[i].drag)
				&& mouseBoxCollisionTest(gInput->buttons[i].downx, gInput->buttons[i].downy, &Box))
				return true;
		}
	}
	return false;
}

int unfilteredMouseCollision( CBox * box )
{
	int xp, yp;
	if (gInput)
	{
		xp = gInput->mouseInpCur.x;
		yp = gInput->mouseInpCur.y;
		return mouseBoxCollisionTest( xp, yp, box );
	}
	else
		return false;
}

void mouseDiffLegacy( int * xp, int * yp )
{
	if (gInput)
	{
		*xp = gInput->mouse_dx;
		*yp = gInput->mouse_dy;
	}
	else
	{
		*xp = 0;
		*yp = 0;
	}
}

void mouseDiffNormalized(SA_PRE_NN_FREE SA_POST_NN_VALID F32 *xp, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *yp )
{
	if (gInput)
	{
		*xp = (F32)gInput->mouse_dx / 512.f;
		*yp = (F32)gInput->mouse_dy / 512.f;
	}
	else
	{
		*xp = 0;
		*yp = 0;
	}
}

F32 mouseGetTimesliceMS()
{
	return gInput->fMouseTimeDeltaMS;
}

void mouseDiff( int * xp, int * yp )
{
	if (gInput)
	{
		//mouse_dx and mouse_dy are inaccurate apparently because direct input
		//commonly miss diff messages making them less than the actual diff.  
		//cur - last is more accurate but doesn't work on the edge of the screen.
		//So, we use cur - last until we hit the edge of the screen and
		//then switch to mouse_dx and mouse_dy
		*xp = gInput->mouseInpCur.x - gInput->mouseInpLast.x;
		*yp = gInput->mouseInpCur.y - gInput->mouseInpLast.y;

		if(*xp == 0)
			*xp = gInput->mouse_dx;
		if(*yp == 0)
			*yp = gInput->mouse_dy;
	}
	else
	{
		*xp = 0;
		*yp = 0;
	}
}

void mousePos( int * xp, int * yp )
{
	if (gInput->mouse_lock_this_frame)
	{
		*xp = gInput->mouseInpSaved.x;
		*yp = gInput->mouseInpSaved.y;
	}
	else
	{
		// changed from inpMousePos because this is a lot faster,
		// but will be ~0.5 frames behind.
		inpLastMousePos(xp, yp);
	}
}

void mousePosCurrent( int * xp, int * yp )
{
	if (gInput->mouse_lock_this_frame)
	{
		*xp = gInput->mouseInpSaved.x;
		*yp = gInput->mouseInpSaved.y;
	}
	else
	{
		inpMousePos(xp, yp);
	}
}

// This function is similar to mouseDragCoords and mouseClickCoords
// It gets the last location where the mouse button was clicked
void mouseDownPos(MouseButton button, int *xp, int *yp)
{
	if (gInput)
	{
		if (xp)
			*xp = gInput->buttons[button].downx;
		if (yp)
			*yp = gInput->buttons[button].downy;
	}
	else
	{
		if (xp)
			*xp = -1;
		if (yp)
			*yp = -1;
	}
}

void mouseClear(void) 
{
	PERFINFO_AUTO_START_FUNC();
	if (gInput)
	{
		int i;
		for (i = 0; i < ARRAY_SIZE_CHECKED(gInput->buttons); i++)
		{
			gInput->buttons[i].drag = false;
			gInput->buttons[i].state = MS_NONE;
		}

		gInput->mouseInpCur.states[MS_LEFT] = gInput->mouseInpCur.states[MS_MID] = gInput->mouseInpCur.states[MS_RIGHT] = MS_NONE;

		memset(&gInput->mouseInpBuf, 0, sizeof(mouse_input) * MOUSE_INPUT_SIZE );
		gInput->mouseBufSize = 0;
	}
	PERFINFO_AUTO_STOP();
}

void mouseLockThisFrame(void)
{
	if (!gInput)
		return;

	if( !mouseIsLocked() ) {
		RECT rcDlg;
		gInput->mouseInpSaved.x = gInput->mouseInpCur.x;
		gInput->mouseInpSaved.y = gInput->mouseInpCur.y;
		GetWindowRect(gInput->hwnd, &rcDlg);
		gInput->mouseInpCur.x = (rcDlg.right - rcDlg.left) / 2;
		gInput->mouseInpCur.y = (rcDlg.bottom - rcDlg.top) / 2;
	}

	gInput->mouse_lock_this_frame = true;
}

void mouseSetScreenPercent(F32 xPct, F32 yPct)
{
	RECT rcDlg;
	POINT	pCursor = {0};
	GetWindowRect(gInput->hwnd, &rcDlg);
	gInput->mouseInpSaved.x = gInput->mouseInpCur.x = pCursor.x = (rcDlg.right - rcDlg.left) * xPct;
	gInput->mouseInpSaved.y = gInput->mouseInpCur.y = pCursor.y = (rcDlg.bottom - rcDlg.top) * yPct;
	ClientToScreen(gInput->hwnd, &pCursor);
	SetCursorPos(pCursor.x, pCursor.y);
}

void mouseSetScreen(int x, int y)
{
	POINT	pCursor = {0};
	gInput->mouseInpSaved.x = gInput->mouseInpCur.x = pCursor.x = x;
	gInput->mouseInpSaved.y = gInput->mouseInpCur.y = pCursor.y = y;
	ClientToScreen(gInput->hwnd, &pCursor);
	SetCursorPos(pCursor.x, pCursor.y);
}

int mouseIsLocked(void)
{
	if( !gInput || inpIsInactiveApp( &gInput->dev )) {
		return false;
	}
	
	return gInput->mouse_lock_this_frame || gInput->mouse_lock_last_frame;
}

int mouseZ(void)
{
	return gInput ? gInput->mouseInpCur.z : 0;
}

void mouseCaptureZ(void)
{
	if (gInput)
	{
		inpCapture(INP_MOUSEWHEEL_BACKWARD);
		inpCapture(INP_MOUSEWHEEL_FORWARD);
		gInput->mouseInpCur.z = 0;
	}
}

bool mouseDidAnything(void)
{
	return (gInput &&
		(gInput->mouseBufSize > 0
		|| gInput->mouse_dx
		|| gInput->mouse_dy
		|| gInput->mouseInpCur.states[MS_LEFT]
		|| gInput->mouseInpCur.states[MS_MID]
		|| gInput->mouseInpCur.states[MS_RIGHT]
		));
}

bool inpHasMouse(void)
{
	return (gInput && gInput->MouseDev);
}

bool mouseDownOver(MouseButton button, const CBox *pBox)
{
	S32 iX, iY;
	mouseDownPos(button, &iX, &iY);
	return (mouseIsDown(button) && mouseCollision(pBox) && mouseBoxCollisionTest(iX, iY, pBox));
}

bool mouseDownWasOver(MouseButton button, const CBox *pBox)
{
	S32 iX, iY;
	mouseDownPos(button, &iX, &iY);
	return (mouseIsDown(button) && mouseBoxCollisionTest(iX, iY, pBox));
}
