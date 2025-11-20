#ifndef GFXCLIPPER_H
#define GFXCLIPPER_H
GCC_SYSTEM

#include "stdtypes.h"

#include "MemoryPool.h"
#include "earray.h"
#include "Cbox.h"
#include "GraphicsLib.h"

//-----------------------------------------------------------------------------------------------------------------
// Clipper2D
//-----------------------------------------------------------------------------------------------------------------
// Allows each piece of display code to set its own clipping rectangle without having to worry about wiping
// someone elses.
//
// Note that each clipperPush() *MUST* be followed by a corresponding clipperPop().  Otherwise, sprites and
// text will be clipped unexpectedly.
//
typedef struct Clipper2D
{
	CBox box;		// Box where a 2D element should be clipped to.
	CBox glBox;		// Same box in GL coordinates.
	char* filename;
	int lineno;
} Clipper2D;

extern MemoryPool MP_NAME(Clipper2D);
extern Clipper2D** ClipperStack;

//LM: These are all inline now for performance 
__forceinline static Clipper2D* clipperGetCurrent(void)
{
	return eaTail(&ClipperStack);
}


// Converts the box coordinates from top-left origin to bottom-left origin.
__forceinline static void clipperConvertToGLCoord(CBox *box)
{
	int clientWidth, clientHeight;
	float top, bottom;

	gfxGetActiveSurfaceSizeInline(&clientWidth, &clientHeight);
	top = clientHeight - box->bottom;
	bottom = clientHeight - box->top;
	box->top = top;
	box->bottom = bottom;
}

__forceinline static void clipperModifyCurrent(const CBox* box)
{
	Clipper2D* clipper = clipperGetCurrent();
	if(!box || !clipper)
		return;

	clipper->box = *box;
	clipper->glBox = *box;

	//if (clipper->box.left < 0.0f)
	//	clipper->box.left = 0.0f;
	//if (clipper->box.top < 0.0f)
	//	clipper->box.top = 0.0f;

	//if (clipper->glBox.left < 0.0f)
	//	clipper->glBox.left = 0.0f;
	//if (clipper->glBox.top < 0.0f)
	//	clipper->glBox.top = 0.0f;

	clipperConvertToGLCoord(&clipper->glBox);
	CBoxInt(&clipper->box);
	CBoxInt(&clipper->glBox);
}




__forceinline static void clipperPush(const CBox *box)
{	
	Clipper2D* clipper;
	int clipperIndex;

	MP_CREATE(Clipper2D, 16);

	clipper = MP_ALLOC(Clipper2D);
	clipperIndex = eaPush(&ClipperStack, clipper);

	if(!box) // no box, clip to fullscreen 
		// Pushing null onto stack leaves us in wierd state where we are trying to scissor (there is a stack)
		// but there is no box.
	{
		CBox fullscreen;
		int w,h;

		gfxGetActiveSurfaceSizeInline( &w, &h );
		BuildCBox(&fullscreen, 0, 0, w, h);
		clipperModifyCurrent(&fullscreen);
	}
	else
		clipperModifyCurrent(box);

	assert(clipperIndex < 128);		// Is someone forgetting to pop some clippers?
}

__forceinline static void clipperPushRestrict(const CBox *box)
{
	Clipper2D* clipper;
	float bLeft, bTop, bRight, bBottom;
	float cLeft, cTop, cRight, cBottom;
	CBox newClipBox = {0};

	// If no restriction box was given, remove all clipping restrictions temporarily.
	if(!box)
	{
		clipperPush(NULL);
		return;
	}

	clipper = clipperGetCurrent();
	if(!clipper)
	{
		clipperPush(box);
		return;
	}

	//uiBoxNormalize(box);
	bLeft = box->left;
	bTop = box->top;
	bRight = box->right;
	bBottom = box->bottom;

	cLeft = clipper->box.left;
	cTop = clipper->box.top;
	cRight = clipper->box.right;
	cBottom = clipper->box.bottom;

	// Rectangle intersection test.
	if(bLeft > cRight || bTop > cBottom || cLeft > bRight || cTop > bBottom)
	{
		// If the two rectangles do not intersect, shrink the clipper box to nothing.
		clipperPush(&newClipBox);
		return;
	}

	// The rectangles intersect in some way
	newClipBox.left = MAX(bLeft, cLeft);
	newClipBox.top = MAX(bTop, cTop);
	newClipBox.right = MIN(bRight, cRight);
	newClipBox.bottom = MIN(bBottom, cBottom);
	clipperPush(&newClipBox);
}

__forceinline static void clipperPop(void)
{
	Clipper2D* clipper;
	assert(eaSize(&ClipperStack) > 0);	// Is someone popping clippers that do not belong to them?
	clipper = eaPop(&ClipperStack);

	MP_FREE(Clipper2D, clipper);
}

__forceinline static int clipperGetCount(void)
{
	return eaSize(&ClipperStack);
}

__forceinline static int clipperIntersects(CBox *box)
{
	Clipper2D* clipper = clipperGetCurrent();
	if(!clipper)
		return 1;
	if(!box)
		return 0;

	return CBoxIntersects(&clipper->box, box);
}

__forceinline static CBox* clipperGetBox(Clipper2D* clipper)
{
	if(!clipper)
		return NULL;
	else
		return &clipper->box;
}

__forceinline static CBox* clipperGetGLBox(Clipper2D* clipper)
{
	if(!clipper)
		return NULL;
	else
		return &clipper->glBox;
}

__forceinline static void clipperSetCurrentDebugInfo(char* filename, int lineno)
{
	Clipper2D* clipper = clipperGetCurrent();
	if(!clipper)
		return;
	clipper->filename = filename;
	clipper->lineno = lineno;
}

__forceinline static const CBox* clipperGetCurrentCBox()
{
	Clipper2D* clipper = clipperGetCurrent();
	return &clipper->box;
}

__forceinline static int clipperTestValuesGLSpace(Clipper2D* clipper, Vec2 ul, Vec2 lr)
{
	if (!clipper)
		return 1;

	if (lr[0] <= clipper->glBox.lx)
		return 0;
	if (ul[0] >= clipper->glBox.hx)
		return 0;
	if (lr[1] >= clipper->glBox.hy)
		return 0;
	if (ul[1] <= clipper->glBox.ly)
		return 0;

	return 1;
}

#endif