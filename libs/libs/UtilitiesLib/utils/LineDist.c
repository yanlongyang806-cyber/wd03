#include "LineDist.h"
#include "mathutil.h"

#define EPSILON 0.00001f

void PointLineSegClosestPoint(Vec3 p, Vec3 a, Vec3 b, F32* t, Vec3 d)
{
	Vec3 ab, pa;
	F32 ab2;
	subVec3(b, a, ab);
	subVec3(p, a, pa);
	ab2 = dotVec3(ab, ab);

	if (ab2 > 0.0f) {
		(*t) = dotVec3(pa, ab) / ab2;
		MINMAX1((*t), 0.0f, 1.0f);
		scaleAddVec3(ab, (*t), a, d);
	} else {
		(*t) = 0.0f;
		copyVec3(a, d);
	}
}

void PointLineClosestPoint(const Vec3 pt, const Vec3 Lpt, const Vec3 Ldir, const F32 Llen, Vec3 closePt)
{
	Vec3 dvec,tvec;
	F32 dist;
	if (!closePt)
	{
		closePt = tvec;
	}
	subVec3(pt, Lpt, dvec);
	dist = dotVec3(dvec, Ldir);
	if (dist < 0) {
		copyVec3(Lpt, closePt);
	} else if (dist >= Llen) {
		scaleAddVec3(Ldir, Llen, Lpt, closePt);
	} else {
		scaleAddVec3(Ldir, dist, Lpt, closePt);
	}
}

F32 PointLineDistSquared(const Vec3 pt, const Vec3 Lpt, const Vec3 Ldir, const F32 Llen, Vec3 col)
{
	Vec3 dvec, closePt;
	PointLineClosestPoint(pt, Lpt, Ldir, Llen, closePt);
	subVec3(closePt, pt, dvec);
	if (col)
	{
		copyVec3(closePt, col);
	}
	return lengthVec3Squared(dvec);
}

F32 PointLineDistSquaredXZ(const Vec3 pt, const Vec3 Lpt, const Vec3 Ldir, const F32 Llen, Vec3 col)
{
	Vec3 dvec,tvec;
	F32 dist;
	if (!col)
	{
		col = tvec;
	}
	subVec3(pt, Lpt, dvec);
	dist = dotVec3(dvec, Ldir);
	if (dist < 0) {
		copyVec3(Lpt, col);
	} else if (dist >= Llen) {
		scaleAddVec3(Ldir, Llen, Lpt, col);
	} else {
		scaleAddVec3(Ldir, dist, Lpt, col);
	}
	subVec3(col, pt, dvec);
	return lengthVec3SquaredXZ(dvec);
}

F32 pointLineDistSquaredXZ(const Vec3 pt, const Vec3 start, const Vec3 end, Vec3 col)
{
	Vec3 dir;
	F32 len;
	subVec3(end, start, dir);
	len = normalVec3(dir);

	return PointLineDistSquaredXZ(pt, start, dir, len, col);
}

F32 pointLineDistSquared(const Vec3 pt,const Vec3 start,const Vec3 end, Vec3 col)
{
Vec3	dv;
F32		len;

	subVec3(end,start,dv);
	len = normalVec3(dv);
	return PointLineDistSquared(pt,start,dv,len,col);
}

/* Point: px,pz Line: (lAx,lAz),(lAx+ldx,LAz+ldx) */
F32 PointLineDist2DSquared(F32 px, F32 pz, F32 lAx, F32 lAz, F32 ldx, F32 ldz, F32 *lx, F32 *lz)
{
	F32 dx,dz, nldx,nldz;
	F32 dist,llen,invllen;

	llen = fsqrt(ldx*ldx + ldz*ldz);
	if (llen == 0.0) { /* point-point dist */
		dx = lAx - px;
		dz = lAz - pz;
		*lx = lAx; *lz = lAz;
		return dx*dx+dz*dz;
	}
	invllen = 1.0 / llen;
	dx = px - lAx;		dz = pz - lAz;
	nldx = ldx*invllen; nldz = ldz*invllen;
	dist = (dx * nldx + dz * nldz); /* dot product */
	if (dist < 0) {
		*lx = lAx; *lz = lAz;
	} else if (dist >= llen) {
		*lx = lAx + ldx; *lz = lAz + ldz;
	} else {
		*lx = lAx + nldx*dist; *lz = lAz + nldz*dist;
	}
	dx = px - *lx;	dz = pz - *lz;
	return dx*dx + dz*dz;
}

// Get the closest points [vC1, vC2] and the parametric values [pfS, pfT] between two line segments [vP1, vQ1] and [vP2, vQ2]
// Unlike LineLineDistSquared, explicitly takes two line segments
// returns the squared distance between the closest points
F32 LineSegLineSegDistSquared(const Vec3 vP1, const Vec3 vQ1, const Vec3 vP2, const Vec3 vQ2, F32* pfS, F32* pfT, Vec3 vC1, Vec3 vC2)
{
	Vec3 vD1, vD2, vDC, vR;
	F32 a, e, f;
	
	subVec3(vQ1, vP1, vD1);
	subVec3(vQ2, vP2, vD2);
	a = dotVec3(vD1, vD1);
	e = dotVec3(vD2, vD2);

	if (a <= EPSILON && e <= EPSILON)
	{
		(*pfS) = 0.0f;
		(*pfT) = 0.0f;
		copyVec3(vP1, vC1);
		copyVec3(vP2, vC2);
		subVec3(vC1, vC2, vDC);
		return dotVec3(vDC, vDC);
	}

	subVec3(vP1, vP2, vR);
	f = dotVec3(vD2, vR);

	if (a <= EPSILON)
	{
		(*pfS) = 0.0f;
		(*pfT) = f / e;
		(*pfT) = CLAMPF32(*pfT, 0.0f, 1.0f);
	}
	else
	{
		F32 c = dotVec3(vD1, vR);

		if (e <= EPSILON)
		{
			(*pfT) = 0.0f;
			(*pfS) = CLAMPF32(-c / a, 0.0f, 1.0f);
		}
		else
		{
			F32 fTNom, b, fDenom;

			b = dotVec3(vD1, vD2);
			fDenom = a*e - b*b;

			if (!nearSameF32(fDenom, 0.0f))
			{
				(*pfS) = CLAMPF32((b*f - c*e) / fDenom, 0.0f, 1.0f);
			}
			else
			{
				(*pfS) = 0.0f;
			}

			fTNom = b * (*pfS) + f;
			if (fTNom < 0.0f)
			{
				(*pfT) = 0.0f;
				(*pfS) = CLAMPF32(-c / a, 0.0f, 1.0f);
			}
			else if (fTNom > e)
			{
				(*pfT) = 1.0f;
				(*pfS) = CLAMPF32((b - c) / a, 0.0f, 1.0f);
			}
			else
			{
				(*pfT) = fTNom / e;
			}
		}
	}

	scaleAddVec3(vD1, *pfS, vP1, vC1);
	scaleAddVec3(vD2, *pfT, vP2, vC2);
	subVec3(vC1, vC2, vDC);
	return dotVec3(vDC, vDC);
}

F32 LineLineDistSquared(const Vec3 L1pt, const Vec3 L1dir, F32 L1len, Vec3 L1isect,
						const Vec3 L2pt, const Vec3 L2dir, F32 L2len, Vec3 L2isect)
{
	Vec3 L1endpt, L2endpt, tempL1isect, tempL2isect;
	F32 f1, f2;

	scaleAddVec3(L1dir, L1len, L1pt, L1endpt);
	scaleAddVec3(L2dir, L2len, L2pt, L2endpt);

	if (!L1isect)
		L1isect = tempL1isect;
	if (!L2isect)
		L2isect = tempL2isect;

	return LineSegLineSegDistSquared(L1pt, L1endpt, L2pt, L2endpt, &f1, &f2, L1isect, L2isect);
}

// 2.27.09
// TODO: (RRP wants to fix) this function appears to be broken (LineLineDist2DSquared)
// following input was producing incorrect results: (using X, Z only, obviously)
// line1: start
//	[0]	4098.9326	float
//	[1]	246.54158	float
//  [2]	1912.2936	float
// line1: end
// [0]	4081.9321	float
// [1]	246.63785	float
// [2]	1983.2936	float

// line2: start
// [0]	4114.4487	float
// [1]	246.49335	float
// [2]	1873.1664	float
// line2: end
// [0]	4101.4487	float
// [1]	246.54466	float
// [2]	1914.1664	float

F32 LineLineDist2DSquared(F32 l1Ax, F32 l1Az, F32 l1Bx, F32 l1Bz, F32 *x1, F32 *z1,
									 F32 l2Ax, F32 l2Az, F32 l2Bx, F32 l2Bz) {
	F32 l1dx = l1Bx - l1Ax;
	F32 l1dz = l1Bz - l1Az;
	F32 l2dx = l2Bx - l2Ax;
	F32 l2dz = l2Bz - l2Az;
	F32 dx = l2Ax - l1Ax;
	F32 dz = l2Az - l1Az;
	F32 d,r,s,invd;

	d = l1dx * l2dz - l1dz * l2dx;
	if (d != 0.0) {
		invd = 1.0 / d;
		r = (dx * l2dz - dz * l2dx) * invd;
		s = (dx * l1dz - dz * l1dx) * invd;
		if (r < 0) r = 0; if (r > 1) r = 1;
		if (s < 0) s = 0; if (s > 1) s = 1;
		*x1 = (l1Ax + r*(l1dx));
		*z1 = (l1Az + r*(l1dz));
		dx = *x1 - (l2Ax + s*(l2dx));
		dz = *z1 - (l2Az + s*(l2dz));
	} else {
		F32 dp;
		d = fsqrt(l1dx*l1dx+l1dz*l1dz);
		if (d == 0.0) { /* point line intersection */
			F32 tx=0,tz=0;
			d = PointLineDist2DSquared(l1Ax, l1Az, l2Ax, l2Az, l2dx, l2dz, &tx, &tz);
			*x1 = l1Ax;
			*z1 = l1Az;
			return d;
		}
		invd = 1.0 / d;
		l1dx *= invd;
		l1dz *= invd;
		dp = dx*l1dx + dz*l1dz; /* dot product */
		if (dp >= d) {
			*x1 = l1Bx;
			*z1 = l1Bz;
		} else if (dp >= 0.0) {
			*x1 = l1Ax + dp*l1dx;
			*z1 = l1Az + dp*l1dz;
		} else {
			*x1 = l1Ax;
			*z1 = l1Az;
		}
		dx = *x1 - l2Ax;
		dz = *z1 - l2Az;
	}
	return (dx*dx + dz*dz);
}

// see comment above for LineLineDist2DSquared
F32 LineLineDistSquaredXZ(const Vec3 a1, const Vec3 a2, Vec3 acoll, const Vec3 b1, const Vec3 b2)
{
	Vec3 aStart, aDir, bStart, bDir;
	F32 aLen, bLen;
	F32* t;
	Vec3 c;
	if(!acoll)
		t = c;
	else 
		t = acoll;
	
	copyVec3(a1, aStart);
	subVec3(a2, a1, aDir);
	aStart[1] = 0.0f;
	aDir[1] = 0.0f;
	aLen = normalVec3(aDir);
	
	copyVec3(b1, bStart);
	subVec3(b2, b1, bDir);
	bStart[1] = 0.0f;
	bDir[1] = 0.0f;
	bLen = normalVec3(bDir);
		
	return LineLineDistSquared(aStart, aDir, aLen, acoll, bStart, bDir, bLen, NULL);
	//return LineLineDist2DSquared(a1[0], a1[2], a2[0], a2[2], &t[0], &t[2], b1[0], b1[2], b2[0], b2[2]);
}
