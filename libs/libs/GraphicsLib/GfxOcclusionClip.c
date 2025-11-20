#if _XBOX
#include "wininclude.h"
#include <xboxmath.h>
#endif
#include "file.h"

#include "GfxOcclusionClip.h" 
#include "Vec4H.h"
#include "GfxOcclusionTypes.h"

__forceinline static int isFacing(ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2)
{
	Vec2 v1, v2;
	F32 r;

	v1[0] = p1->x - p0->x;
	v1[1] = p1->y - p0->y;
	v2[0] = p2->x - p0->x;
	v2[1] = p2->y - p0->y;

	r = crossVec2(v1, v2);

	return r < 0;
}

#define clip_func(name,sign,dir,dir1,dir2) \
	static float name(Vec4 c, const Vec4 start, const Vec4 end) \
	{\
		float t,den;\
		const float *startP,*endP;\
		Vec4 delta;\
		if(start[dir] > end[dir])\
		{\
			startP = &end[0];\
			endP = &start[0];\
		}\
		else\
		{\
			startP = &start[0];\
			endP = &end[0];\
		}\
	subVec4(endP, startP, delta);\
	den = -(sign delta[dir]) + delta[3];\
	if (den == 0)\
		t=0;\
	else\
		t = ( sign startP[dir] - startP[3]) / den;\
	c[dir1] = startP[dir1] + t * delta[dir1];\
	c[dir2] = startP[dir2] + t * delta[dir2];\
	c[3] = startP[3] + t * delta[3];\
	c[dir] = sign c[3];\
	return t;\
}

static float clip_zmin(Vec4 c, const Vec4 start, const Vec4 end)
{
	float t, den;
	const F32 *startP,*endP;
	Vec4 delta;
	if(start[2] > end[2])
	{
		startP = &end[0];
		endP = &start[0];
	}
	else
	{
		startP = &start[0];
		endP = &end[0];
	}
	subVec4(endP, startP, delta);
	den = delta[2];
	if (den == 0)
		t = 0.0f;
	else
		t = -startP[2] / den;
	c[0] = startP[0] + t * delta[0];
	c[1] = startP[1] + t * delta[1];
	c[2] = 0.0f;
	c[3] = startP[3] + t * delta[3];
	return t;
}


clip_func(clip_xmin,-,0,1,2)

clip_func(clip_xmax,+,0,1,2)

clip_func(clip_ymin,-,1,0,2)

clip_func(clip_ymax,+,1,0,2)

clip_func(clip_zmax,+,2,0,1)


static float (*clip_proc[6])(Vec4, Vec4, Vec4) =
{
	clip_xmin,clip_xmax,
	clip_ymin,clip_ymax,
	clip_zmin,clip_zmax
};

void drawZInitClipProc()
{
    clip_proc[0] = clip_xmin;
    clip_proc[1] = clip_xmax;
    clip_proc[2] = clip_ymin;
    clip_proc[3] = clip_ymax;
    clip_proc[4] = clip_zmin;
    clip_proc[5] = clip_zmax;
}

 
#if _PS3
static const vec_uint4 maskV[ 4 ] = { 
	{-1,  0,  0,  0},
	{-1, -1,  0,  0},
	{-1, -1, -1,  0},
	{-1, -1, -1, -1},
};
#else
extern Vec4H maskV[ 4 ];
#endif
__inline static void drawZLine(int x1, int x2, ZType *zline_ptr, Vec4HP zvecP, Vec4HP dzvecP)
{
	register int n = ( x2 + 3 ) / 4 - x1 / 4;
	register ZType *pz = zline_ptr + ( x1 & ~3 );

	// 0 = 1000
	// 1 = 1100
	// 2 = 1110
	// 3 = 1111
	Vec4H zvec = RVP(zvecP), dzvec = RVP(dzvecP), zV, zVOrig;
#if _PS3
	vec_uint4 maskPixelsV;
#else
	Vec4H maskPixelsV;
#endif
	// For the start mask we need:
	// 0 = 1111 = maskV[3] - ideally we would use no masking on the first vec4 in this case
	// 1 = 0111 = maskV[3]-maskV[0]
	// 2 = 0011 = maskV[3]-maskV[1]
	// 3 = 0001 = maskV[3]-maskV[2]

	U32 firstBlockMask = x1 & 3;
	U32 lastBlockMask = x2 & 3;

	if (!n)
		return;

	// handle first vec4: at least one f32 is in the range
	if (firstBlockMask > 0)
	{
		// start with all pixels
		maskPixelsV = maskV[ 3 ];
		if (firstBlockMask)
			// kill pixels before the first included pixel
			vecVecAndC4H(maskPixelsV, maskV[ firstBlockMask - 1 ], maskPixelsV);

		if (n == 1)
		{
			if (lastBlockMask>0)
				// also mask by the end mask, since the entire scanline is within a single vector
				vecVecAnd4H(maskPixelsV, maskV[ lastBlockMask - 1 ], maskPixelsV);

			// clear vars so we only do the single masked vector
			lastBlockMask = 0;
		}

		zV = *(const Vec4H*)pz;
		zVOrig = zV;
		zV = minVecExp4H(zvec, zV);
		// mask to the scanline using the select vector: zV = zV * (1 - maskPixelsV) + zVOrig
		vecSelect4H(zVOrig, zV, maskPixelsV, zV);

		// increment
		addVec4H(zvec, dzvec, zvec);
		// store completed vec of Z
		*(Vec4H*)pz = zV;
		pz += 4;
		--n;
	}

	if (n && lastBlockMask>0)
		// do one less whole vector
		n -= 1;

	while (n)
	{
		*(Vec4H*)pz = minVecExp4H(*(const Vec4H*)pz, zvec);
		addVec4H(zvec, dzvec, zvec);

		pz += 4;
		--n;
	}

	if (lastBlockMask > 0)
	{
		// do last vector masked
		maskPixelsV = maskV[lastBlockMask - 1 ];

		zV = *(const Vec4H*)pz;
		zVOrig = zV;
		zV = minVecExp4H(zvec, zV);
		// mask to the scanline using the select vector: zV = zV * (1 - maskPixelsV) + zVOrig
		vecSelect4H(zVOrig, zV, maskPixelsV, zV);

		// store completed vec of Z
		*(Vec4H*)pz = zV;
	}
} 
#if ZO_DRAW_INTERLEAVED
static void drawZTriangle(F32 *zoBufData, int zoBufWidth, ZBufferPoint *p0,ZBufferPoint *p1,ZBufferPoint *p2, int *trisDrawn, int intervalLine, int intervalSkip)
#else
static void drawZTriangle(F32 *zoBufData, int zoBufWidth, ZBufferPoint *p0,ZBufferPoint *p1,ZBufferPoint *p2, int *trisDrawn)
#endif
{
	 
	float fdx1,	fdx2, fdy1,	fdy2, d1, d2;
	ZType *pz, fz;
	int	dx1,dy1,dx2,dy2;
	int	x1,dx1dy1,x2,dx2dy2;
	int y0, y1, y2, zbwidth;
	ZType z1,dzdx,dzdy;

	Vec4H zvec, dzdxvec, dzdyvec, zincrementV;

	if ( trisDrawn )
	{
		(*trisDrawn)++;
	}

	// sort the vertices by y
	if (p1->y <	p0->y)
	{
		ZBufferPoint *t = p0;
		p0 = p1;
		p1 = t;
	}
	if (p2->y <	p0->y)
	{
		ZBufferPoint *t = p2;
		p2 = p1;
		p1 = p0;
		p0 = t;
	}
	else if (p2->y < p1->y)
	{
		ZBufferPoint *t =	p1;
		p1 = p2;
		p2 = t;
	}

	/* we compute dXdx and dXdy	for	all	interpolated values	*/

	y0 = p0->y;
	y1 = p1->y;
	y2 = p2->y;

	dx1 = p1->x - p0->x;
	dy1 = p1->y - p0->y;

	dx2 = p2->x - p0->x;
	dy2 = p2->y - p0->y;

	fdx1 = p1->x - p0->x;
	fdy1 = p1->y - p0->y;

	fdx2 = p2->x - p0->x;
	fdy2 = p2->y - p0->y;

	fz = fdx1 *	fdy2 - fdx2	* fdy1;
	if (fz == 0)
		return;

	fz = ZB_SCAN_RES / fz;

	fdx1 *=	fz;
	fdy1 *=	fz;
	fdx2 *=	fz;
	fdy2 *=	fz;

	d1 = p1->z - p0->z;
	d2 = p2->z - p0->z;
	dzdx = fdy2 * d1 - fdy1 * d2;
	dzdy = fdx1 * d2 - fdx2 * d1;

	dx2dy2 = dx2 * ZB_SCAN_RES / dy2;

	{
		static const Vec4H zincrementScale = { 0, 1, 2, 3 };
		setVec4sameH(dzdxvec, dzdx);
		setVec4sameH(dzdyvec, dzdy);

		copyVec4H(dzdxvec, zincrementV);
		vecVecMul4H(zincrementV, zincrementScale, zincrementV);

		// mul by 4
		addVec4H(dzdxvec, dzdxvec, dzdxvec);
		addVec4H(dzdxvec, dzdxvec, dzdxvec);
	}

	// get exact scan lines for the vertices of the triangle
	y0 = ( y0 + ZB_HALF_SCAN_RES ) / ZB_SCAN_RES;
	y1 = ( y1 + ZB_HALF_SCAN_RES ) / ZB_SCAN_RES;
	y2 = ( y2 + ZB_HALF_SCAN_RES ) / ZB_SCAN_RES;

	// now adjust the initial interpolated values to sit at exactly the center of the first scanline,
	// on the center x of the left-most pixel
	{
		// get the delta from the vertex y to the center of the first scanline
		int offset_to_center_y = y0 * ZB_SCAN_RES + ZB_HALF_SCAN_RES - p0->y, xleft;

		dx1dy1 = 0;
		if (dy1)
			dx1dy1 = dx1 * ZB_SCAN_RES / dy1;

		x1 = p0->x + (offset_to_center_y * dx1dy1) / ZB_SCAN_RES;
		x2 = p0->x + (offset_to_center_y * dx2dy2) / ZB_SCAN_RES;

		// move the z to the center of pixel at the start of the scanline
		xleft = ( ( ( x1 < x2 ? x1 : x2 ) + ZB_HALF_SCAN_RES ) & ~( ZB_SCAN_RES - 1 ) ) - ZB_HALF_SCAN_RES;
		z1 = p0->z + ( offset_to_center_y * dzdy - ( p0->x - ZB_HALF_SCAN_RES ) * dzdx ) * ZB_OO_SCAN_RES;
	}

	// pointer to the line in the zbuffer
	zbwidth = zoBufWidth;
	{
#if ZO_DRAW_INTERLEAVED
		pz = zoBufData + (y0/intervalSkip) * zbwidth;
#else
		pz = zoBufData + y0 * zbwidth;
#endif
		  
		for(; y0 < y1; ++y0)
		{
			int xp1 = x1, xp2 = x2;
			if (xp1>xp2)
			{
				xp1 = x2;
				xp2 = x1;
			}
			xp1 = ( xp1 + ZB_HALF_SCAN_RES   ) / ZB_SCAN_RES;
			xp2 = ( xp2 + ZB_HALF_SCAN_RES  ) / ZB_SCAN_RES;


			{
				// TODO DJR shift the z start calculation out of the loop to avoid the LHS penalty
				F32 zleft = z1 + dzdx * ( xp1 & ~3 );
				setVec4sameH(zvec, zleft);
				addVec4H(zvec, zincrementV, zvec);
				 
#if _FULLDEBUG
				assert(y0 >= 0 && y0 < ZB_DIM);
				assert(xp1 >= 0 && xp1 <= ZB_DIM);
				assert(xp2 >= 0 && xp2 <= ZB_DIM);
				assert(xp2 >= xp1);
#endif

#if ZO_DRAW_INTERLEAVED
				if(intervalLine == (y0 % intervalSkip))
				{
					pz = zoBufData + (y0/intervalSkip) * zbwidth;
					drawZLine(xp1, xp2, pz, PVP(zvec), PVP(dzdxvec));
				}
			}
#else
				 
				drawZLine(xp1, xp2, pz, PVP(zvec), PVP(dzdxvec));
			}
			 
			pz += zbwidth;
#endif

			addVec4H(zvec, dzdyvec, zvec);

			x1 += dx1dy1;
			x2 += dx2dy2;
			z1 += dzdy;
		}

	}


	// for the bottom part of the triangle, update the end edge
	// interpolated values to sit at exactly the center of the first scanline,
	if (y1 != y2)
	{
		// get the delta from the vertex y to the center of the first scanline
		int offset_to_center_y = y1 * ZB_SCAN_RES + ZB_HALF_SCAN_RES - p1->y;

		dx1 = p2->x - p1->x;
		dy1 = p2->y - p1->y;
		dx1dy1 = dx1 * ZB_SCAN_RES / dy1;

		x1 = p1->x + offset_to_center_y * dx1dy1 / ZB_SCAN_RES;
	
		{
		
#if ZO_DRAW_INTERLEAVED
			pz = zoBufData + (y1/intervalSkip) * zbwidth;
#else
			pz = zoBufData + y1 * zbwidth;
#endif

			for(; y1 < y2; ++y1)
			{
				int xp1 = x1, xp2 = x2;
				if (xp1>xp2)
				{
					xp1 = x2;
					xp2 = x1;
				}
				xp1 = ( xp1 + ZB_HALF_SCAN_RES ) / ZB_SCAN_RES;
				xp2 = ( xp2 + ZB_HALF_SCAN_RES ) / ZB_SCAN_RES;

				if(xp2 > 0 && xp1 < ZB_DIM)
				{
					// TODO DJR shift the z start calculation out of the loop to avoid the LHS penalty
					F32 zleft = z1 + dzdx * ( xp1 & ~3 );
					setVec4sameH(zvec, zleft);
					addVec4H(zvec, zincrementV, zvec);

					if(xp1 < 0)
					{
						int xskip = -xp1;
						zvec.m128_f32[0] += dzdxvec.m128_f32[0] * xskip;
						zvec.m128_f32[1] += dzdxvec.m128_f32[1] * xskip;
						zvec.m128_f32[2] += dzdxvec.m128_f32[2] * xskip;
						zvec.m128_f32[3] += dzdxvec.m128_f32[3] * xskip;

						xp1 = 0;
						
					}

					if(xp2 > ZB_DIM)
					{
						xp2 = ZB_DIM;
					}

#if _FULLDEBUG
					assert(y1 >= 0 && y1 < ZB_DIM);
					assert(xp1 >= 0 && xp1 <= ZB_DIM);
					assert(xp2 >= 0 && xp2 <= ZB_DIM);
					assert(xp2 >= xp1);
#endif

#if ZO_DRAW_INTERLEAVED
					if(intervalLine == (y1 % intervalSkip))
					{
						pz = zoBufData + (y1/intervalSkip) * zbwidth;
						drawZLine(xp1, xp2, pz, PVP(zvec), PVP(dzdxvec));
					}
				}
#else
					drawZLine(xp1, xp2, pz, PVP(zvec), PVP(dzdxvec));
				}
				else
				{
					static int what;
					what ++; 
				}
				pz += zbwidth;
#endif

				addVec4H(zvec, dzdyvec, zvec);

				x1 += dx1dy1;
				x2 += dx2dy2;
				z1 += dzdy;
			}
		}
		 
	}
}

#if ZO_DRAW_INTERLEAVED
void drawZTriangleClip(F32 *zoBufData, int zoBufWidth, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2, int clip_bit, int check_facing, 
	F32 ydim, F32 yoffset, int* trisDrawn, int intervalLine, int intervalSkip)
#else
void drawZTriangleClip(F32 *zoBufData, int zoBufWidth, ZBufferPoint *p0, ZBufferPoint *p1, ZBufferPoint *p2, int clip_bit, int check_facing, 
	F32 ydim, F32 yoffset, int* trisDrawn)
#endif
{
	int co, c_and, co1, cc[3], clip_mask;
	ZBufferPoint tmp1, tmp2, *q[3];

	cc[0] = p0->clipcode;
	cc[1] = p1->clipcode;
	cc[2] = p2->clipcode;

	co = cc[0] | cc[1] | cc[2];
	if (co == 0)
	{
		if (!check_facing || isFacing(p0, p1, p2))
#if ZO_DRAW_INTERLEAVED
            drawZTriangle(zoBufData,zoBufWidth, p0, p1, p2, trisDrawn, intervalLine, intervalSkip);
#else
            drawZTriangle(zoBufData,zoBufWidth, p0, p1, p2, trisDrawn);
#endif
	}
	else
	{
		c_and=cc[0] & cc[1] & cc[2];

		/* the triangle is completely outside */
		if (c_and != 0)
			return;

		/* find the next direction to clip */
		while (clip_bit < 6 && (co & (1 << clip_bit)) == 0)
			clip_bit++;

		/* this test can be true only in case of rounding errors */
		if (clip_bit == 6)
			return;

		clip_mask = 1 << clip_bit;
		co1 = (cc[0] ^ cc[1] ^ cc[2]) & clip_mask;

		if (co1)
		{ 
			/* one point outside */
			if (cc[0] & clip_mask)
			{
				q[0]=p0;
				q[1]=p1;
				q[2]=p2;
			}
			else if (cc[1] & clip_mask)
			{
				q[0]=p1;
				q[1]=p2;
				q[2]=p0;
			}
			else
			{
				q[0]=p2;
				q[1]=p0;
				q[2]=p1;
			}

#if 0 && SPU
            switch(clip_bit)
            {
            case 0:
			    clip_xmin(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_xmin(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 1:
			    clip_xmax(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_xmax(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 2:
			    clip_ymin(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_ymin(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 3:
			    clip_ymax(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_ymax(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 4:
			    clip_zmin(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_zmin(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            default:
            case 5:
			    clip_zmax(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_zmax(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            }
#else
			clip_proc[clip_bit](tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
			transformPoint2(&tmp1, ydim, yoffset);

			clip_proc[clip_bit](tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			transformPoint2(&tmp2, ydim, yoffset);
#endif

#if ZO_DRAW_INTERLEAVED
			drawZTriangleClip(zoBufData,zoBufWidth, &tmp1, q[1], q[2], clip_bit+1, check_facing, ydim, yoffset, trisDrawn, intervalLine, intervalSkip);
			drawZTriangleClip(zoBufData,zoBufWidth, &tmp2, &tmp1, q[2], clip_bit+1, check_facing, ydim, yoffset, trisDrawn, intervalLine, intervalSkip);
#else
			drawZTriangleClip(zoBufData,zoBufWidth, &tmp1, q[1], q[2], clip_bit+1, check_facing, ydim, yoffset, trisDrawn);
			drawZTriangleClip(zoBufData,zoBufWidth, &tmp2, &tmp1, q[2], clip_bit+1, check_facing, ydim, yoffset, trisDrawn);
#endif
		}
		else
		{
			/* two points outside */
			if ((cc[0] & clip_mask)==0)
			{
				q[0]=p0;
				q[1]=p1;
				q[2]=p2;
			}
			else if ((cc[1] & clip_mask)==0)
			{
				q[0]=p1;
				q[1]=p2;
				q[2]=p0;
			} 
			else
			{
				q[0]=p2;
				q[1]=p0;
				q[2]=p1;
			}

#if SPU
            switch(clip_bit)
            {
            case 0:
			    clip_xmin(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_xmin(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 1:
			    clip_xmax(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_xmax(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 2:
			    clip_ymin(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_ymin(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 3:
			    clip_ymax(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_ymax(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            case 4:
			    clip_zmin(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_zmin(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            default:
            case 5:
			    clip_zmax(tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		    transformPoint2(&tmp1, ydim, yoffset);
			    clip_zmax(tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			    transformPoint2(&tmp2, ydim, yoffset);
                break;
            }
#else
			clip_proc[clip_bit](tmp1.hClipPos, q[0]->hClipPos, q[1]->hClipPos);
    		transformPoint2(&tmp1, ydim, yoffset);

			clip_proc[clip_bit](tmp2.hClipPos, q[0]->hClipPos, q[2]->hClipPos);
			transformPoint2(&tmp2, ydim, yoffset);
#endif
#if ZO_DRAW_INTERLEAVED
			drawZTriangleClip(zoBufData,zoBufWidth, q[0], &tmp1, &tmp2, clip_bit+1, check_facing, ydim, yoffset, trisDrawn, intervalLine, intervalSkip);
#else
			drawZTriangleClip(zoBufData,zoBufWidth, q[0], &tmp1, &tmp2, clip_bit+1, check_facing, ydim, yoffset, trisDrawn);
#endif
		}
	}
}



