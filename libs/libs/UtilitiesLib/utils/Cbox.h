/***************************************************************************



***************************************************************************/

#ifndef CBox_H__
#define CBox_H__
#pragma once
GCC_SYSTEM

#include "mathutil.h"

typedef struct CBox
{
	union
	{
		struct
		{
			float lx;
			float ly;
		};
		struct
		{
			float left;
			float top;
		};
		Vec2 upperLeft;
	};

	union
	{
		struct
		{
			float hx;
			float hy;
		};
		struct
		{
			float right;
			float bottom;
		};
		Vec2 lowerRight;
	};
} CBox;

__forceinline static void BuildCBox(SA_PRE_NN_FREE SA_POST_NN_VALID CBox *CBox, float xp, float yp, float wd, float ht)
{
	CBox->lx = xp;
	CBox->ly = yp;
	CBox->hx = xp + wd;
	CBox->hy = yp + ht;
}

//alternate build cbox : xp,yp are not applied to lx,ly : instead xp,yp are the center coordinates
__forceinline static void BuildCBoxFromCenter(SA_PRE_NN_FREE SA_POST_NN_VALID CBox *CBox, float xp, float yp, float wd, float ht)
{
	F32 half_wd = wd*0.5f;
	F32 half_ht = ht*0.5f;
	
	CBox->lx = xp - half_wd;
	CBox->ly = yp - half_ht;
	CBox->hx = xp + half_wd;
	CBox->hy = yp + half_ht;
}

__forceinline static void BuildIntCBox(SA_PRE_NN_FREE SA_POST_NN_VALID CBox *CBox, float xp, float yp, float wd, float ht)
{
	CBox->lx = floorf(xp);
	CBox->ly = floorf(yp);
	CBox->hx = floorf(xp + wd);
	CBox->hy = floorf(yp + ht);
}

__forceinline static int CBoxIntersectsBounds(SA_PARAM_OP_VALID const CBox *box1, const Vec2 vMin, const Vec2 vMax)
{
	if(!box1)
		return false;

	if(vMin[0] > box1->hx || vMin[1] > box1->hy || box1->lx > vMax[0] || box1->ly > vMax[1])
		return false;

	return true;
}

__forceinline static int CBoxIntersects(SA_PARAM_OP_VALID const CBox *box1, SA_PARAM_OP_VALID const CBox *box2)
{
	if(!box2)
		return false;

	return CBoxIntersectsBounds(box1, box2->upperLeft, box2->lowerRight);
}

bool CBoxIntersectsRotated(SA_PARAM_NN_VALID CBox* box, SA_PARAM_NN_VALID CBox* boxRotate, float angle);

__forceinline static F32 CBoxWidth(SA_PARAM_NN_VALID const CBox *box)
{
	return box->hx - box->lx;
}

__forceinline static F32 CBoxHeight(SA_PARAM_NN_VALID const CBox *box)
{
	return box->hy - box->ly;
}

__forceinline static void CBoxMoveX(SA_PARAM_NN_VALID CBox *box, F32 newX)
{
	F32 width = CBoxWidth(box);
	box->lx = newX;
	box->hx = newX + width;
}

__forceinline static void CBoxMoveY(SA_PARAM_NN_VALID CBox *box, F32 newY)
{
	F32 height = CBoxHeight(box);
	box->ly = newY;
	box->hy = newY + height;
}

__forceinline static void CBoxSetWidth(SA_PARAM_NN_VALID CBox *box, F32 width)
{
	box->hx = box->lx + width;
}

__forceinline static void CBoxSetHeight(SA_PARAM_NN_VALID CBox *box, F32 height)
{
	box->hy = box->ly + height;
}

__forceinline static void CBoxSetX(SA_PARAM_NN_VALID CBox *box, F32 newX, F32 newWidth)
{
	box->lx = newX;
	box->hx = newX + newWidth;
}

__forceinline static void CBoxSetY(SA_PARAM_NN_VALID CBox *box, F32 newY, F32 newHeight)
{
	box->ly = newY;
	box->hy = newY + newHeight;
}

__forceinline static void CBoxNormalize(SA_PARAM_OP_VALID CBox* box)
{
	F32 tempf;
	if(!box)
		return;
	if (CBoxHeight(box) < 0)
	{
		tempf = box->ly;
		box->ly = box->hy;
		box->hy = tempf;
	}
	if (CBoxWidth(box) < 0)
	{
		tempf = box->lx;
		box->lx = box->hx;
		box->hx = tempf;
	}
}

__forceinline static bool point_cbox_clsn(int x, int y, SA_PARAM_NN_VALID const CBox * c)
{
	// Add a check for the current clipper here, if appropriate
	if (x >= c->lx && x < c->hx && y >= c->ly && y < c->hy)
		return true;

	return false;
}

typedef enum
{
	CBAD_LEFT	= (1 << 0),
	CBAD_TOP	= (1 << 1),
	CBAD_RIGHT	= (1 << 2),
	CBAD_BOTTOM= (1 << 3),
	CBAD_HORIZ = CBAD_LEFT | CBAD_RIGHT,
	CBAD_VERT = CBAD_TOP | CBAD_BOTTOM,
	CBAD_ALL = CBAD_HORIZ | CBAD_VERT,
} CBoxAlterDirection;

typedef enum
{
	CBAT_GROW = 1,
	CBAT_SHRINK = -1,
} CBoxAlterType;

__forceinline static void CBoxAlter(SA_PARAM_OP_VALID CBox* box, CBoxAlterType type, CBoxAlterDirection direction, float magnitudeIn)
{
	float magnitude = magnitudeIn * type;
	if(!box)
		return;
	CBoxNormalize(box);

	if(direction & CBAD_LEFT)
	{
		box->left -= magnitude;
		if(box->left > box->right)
			box->left = box->right;
	}
	if(direction & CBAD_TOP)
	{
		box->top -= magnitude;
		if(box->top > box->bottom)
			box->top = box->bottom;
	}
	if(direction & CBAD_RIGHT)
	{
		box->right += magnitude;
		if(box->right < box->left)
			box->right = box->left;
	}
	if(direction & CBAD_BOTTOM)
	{
		box->bottom += magnitude;
		if(box->bottom < box->top)
			box->bottom = box->top;
	}
}

__forceinline static void CBoxClipTo(SA_PARAM_OP_VALID const CBox *parent, SA_PARAM_OP_VALID CBox *child)
{
	if (parent && child)
	{
		child->lx = max(parent->lx, child->lx);
		child->ly = max(parent->ly, child->ly);
		child->hx = min(parent->hx, child->hx);
		child->hy = min(parent->hy, child->hy);
	}
}

//A modified version of CBoxClipTo that does not do the clipping in-place, but rather alters a designated intersection
//box "i". Also, this function checks for intersection and avoids further calculations if no overlap is found.
__forceinline static int CBoxIntersectClip(	SA_PARAM_NN_VALID const CBox *box1, 
											SA_PARAM_NN_VALID const CBox *box2, 
											SA_PARAM_NN_VALID CBox *i)
{
	if ( CBoxIntersects( box1, box2 ) == 0 ) 
		return 0;

	i->lx = max(box1->lx, box2->lx);
	i->ly = max(box1->ly, box2->ly);
	i->hx = min(box1->hx, box2->hx);
	i->hy = min(box1->hy, box2->hy);

	return 1;
}

//this is the exact opposite operation of clipping
__forceinline static void CBoxCombine(	SA_PARAM_NN_VALID const CBox *box1, 
										SA_PARAM_NN_VALID const CBox *box2,
										SA_PARAM_NN_VALID CBox *c )
{
	c->lx = min(box1->lx, box2->lx);
	c->ly = min(box1->ly, box2->ly);
	c->hx = max(box1->hx, box2->hx);
	c->hy = max(box1->hy, box2->hy);
}

__forceinline static void CBoxSet( SA_PRE_NN_FREE SA_POST_NN_VALID CBox* pBox, F32 topLeftX, F32 topLeftY, F32 botRightX, F32 botRightY )
{
	pBox->lx = topLeftX;
	pBox->ly = topLeftY;
	pBox->hx = botRightX;
	pBox->hy = botRightY;
}

__forceinline static void CBoxGetCenter(SA_PARAM_NN_VALID const CBox *box, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *cx, SA_PRE_NN_FREE SA_POST_NN_VALID F32 *cy)
{
	*cx = (box->left + box->right) / 2;
	*cy = (box->top + box->bottom ) / 2;
}

__forceinline static void CBoxSetCenter(SA_PARAM_NN_VALID CBox *box, F32 newCenterX, F32 newCenterY)
{
	CBoxMoveX(box, newCenterX - CBoxWidth(box) / 2);
	CBoxMoveY(box, newCenterY - CBoxHeight(box) / 2);
}

__forceinline static void CBoxScalePosition(SA_PARAM_NN_VALID CBox *pFrom, SA_PARAM_NN_VALID CBox *pTo, const Vec2 v2In, Vec2 v2Out)
{
	F32 fPercentX = (v2In[0] - pFrom->lx) / CBoxWidth(pFrom);
	F32 fPercentY = (v2In[1] - pFrom->ly) / CBoxHeight(pFrom);
	v2Out[0] = pTo->lx + CBoxWidth(pTo) * fPercentX;
	v2Out[1] = pTo->ly + CBoxHeight(pTo) * fPercentY;
}

__forceinline static void CBoxFloor(SA_PARAM_NN_VALID CBox *pBox)
{
	pBox->lx = floorf(pBox->lx);
	pBox->ly = floorf(pBox->ly);
	pBox->hx = floorf(pBox->hx);
	pBox->hy = floorf(pBox->hy);
}

__forceinline static void CBoxInt(SA_PARAM_NN_VALID CBox *pBox)
{
	// Grow box to the nearest integer approximation of its size, making sure 0x0 stays 0x0.
	if (pBox->lx == pBox->hx)
	{
		pBox->lx = floorf(pBox->lx);
		pBox->hx = floorf(pBox->hx);
	}
	else
	{
		pBox->lx = floorf(pBox->lx);
		pBox->hx = ceilf(pBox->hx);
	}
	if (pBox->ly == pBox->hy)
	{
		pBox->ly = floorf(pBox->ly);
		pBox->hy = floorf(pBox->hy);
	}
	else
	{
		pBox->ly = floorf(pBox->ly);
		pBox->hy = ceilf(pBox->hy);
	}
}

__forceinline static void CBoxScaleToFit(SA_PARAM_NN_VALID CBox *pBox, SA_PARAM_OP_VALID const CBox *pContainer, bool bGrow)
{
	if (pContainer)
	{
		F32 fScaleWidth = CBoxWidth(pContainer) / CBoxWidth(pBox);
		F32 fScaleHeight = CBoxHeight(pContainer) / CBoxHeight(pBox);
		F32 fScale = min(fScaleWidth, fScaleHeight);
		if (bGrow || fScale < 1.f)
			BuildCBox(pBox, pBox->lx, pBox->ly, CBoxWidth(pBox) * fScale, CBoxHeight(pBox) * fScale);
	}
}

__forceinline static bool CBoxContains(SA_PARAM_NN_VALID const CBox *pParent, SA_PARAM_NN_VALID const CBox *pChild)
{
	return (pParent->lx <= pChild->lx
		&& pParent->hx >= pChild->hx
		&& pParent->ly <= pChild->ly
		&& pParent->hy >= pChild->hy);
}

__forceinline static void CBoxRotate(SA_PARAM_NN_VALID const CBox* box, float angle, 
									 float tl[2], float tr[2], float br[2], float bl[2])
{
	float c, s, cx, cy, lcx, lcy, hcx, hcy, lcxc, lcxs, lcys, lcyc, hcxc, hcxs, hcys, hcyc;

	CBoxGetCenter(box,&cx,&cy);

	sincosf(angle,&s,&c);

	lcx = box->lx-cx;
	lcy = box->ly-cy;
	hcx = -lcx;
	hcy = -lcy;

	lcxc = lcx * c;
	lcxs = lcx * s;
	lcyc = lcy * c;
	lcys = lcy * s;
	hcxc = hcx * c;
	hcxs = hcx * s;
	hcyc = hcy * c;	
	hcys = hcy * s;
	
	tl[0] = cx + lcxc - lcys;
	tl[1] = cy + lcxs + lcyc;
	tr[0] = cx + hcxc - lcys;
	tr[1] = cy + hcxs + lcyc;
	br[0] = cx + hcxc - hcys;
	br[1] = cy + hcxs + hcyc;
	bl[0] = cx + lcxc - hcys;
	bl[1] = cy + lcxs + hcyc;
}

__forceinline static float CBoxDistanceToPoint(CBox* pBox, const Vec2 vPoint)
{
	float dist, d;

	if (vPoint[0] < pBox->lx)
		d = vPoint[0] - pBox->lx;
	else if (vPoint[0] > pBox->hx)
		d = vPoint[0] - pBox->hx;
	else
		d = 0;
	dist = SQR(d);

	if (vPoint[1] < pBox->ly)
		d = vPoint[1] - pBox->ly;
	else if (vPoint[1] > pBox->hy)
		d = vPoint[1] - pBox->hy;
	else
		d = 0;
	dist += SQR(d);

	return dist;
}

__forceinline static bool CBoxIntersectsCircle(SA_PARAM_NN_VALID const CBox *c, F32 fX, F32 fY, F32 fRadius)
{
	Vec2 vPoint, vMid, vMin, vMax;
	F32 fRadiusBox;

	setVec2(vPoint, fX, fY);
	fRadiusBox = sqrt(distance2Squared(c->upperLeft, c->lowerRight)) * 0.5f;
	CBoxGetCenter(c, &vMid[0], &vMid[1]);

	if (distance2Squared(vMid, vPoint) > SQR(fRadius + fRadiusBox))
	{
		return false;
	}

	addVec2same(vPoint, -fRadius, vMin);
	addVec2same(vPoint, fRadius, vMax);
	return CBoxIntersectsBounds(c, vMin, vMax);
}

__forceinline static bool CBoxIntersectsPoint(SA_PARAM_NN_VALID const CBox *c, F32 fX, F32 fY)
{
	if (fX < c->left || fX > c->right || fY < c->top || fY > c->bottom) 
		return false;
	return true;
}

#endif
