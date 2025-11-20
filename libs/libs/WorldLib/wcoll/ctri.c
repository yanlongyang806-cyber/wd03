#include <stdio.h>
#include "mathutil.h"
#include "ctri.h"
#include "tritri_isectline.h"

#if !_PS3
#pragma warning(disable:6011)
#endif

#define Y_TOLERANCE_TO_USE_XYZ_FOR_UVW (0.999999)
#define MAXDIST 	10E20
#define FLIPUV		1
#define NOFLIPUV	2
#define COL_COMPUVS 1

static       Mat4 downmat	= {{1.0,0,0},{0,-1.0,0},{0,0,-1.0},{0.0,0.0,0.0}};
static const Vec3 UPVec		= {0.0, 1.0, 0.0};
static const Vec3 DOWNVec	= {0.0,-1.0, 0.0};
static const Vec3 RIGHTVec	= {1.0, 0.0, 0.0};

static void matToNormScale(const Mat3 mat,Vec3 norm,F32 *scale)
{
	F32 denom;
	copyVec3(mat[1],norm);
	denom = fsqrt(norm[0]*norm[0] + norm[2]*norm[2]);
	if(denom > FLT_EPSILON){
		*scale = 1.0f/denom;
	}else{
		*scale = 1.0f;
	}
}

void normScaleToMat(const Vec3 n,F32 s,Mat3 m)
{
	F32 n1 = n[1];
	if (n1 >= 1) {
		copyMat3(unitmat, m);
	} else if (n1 <= -1) {
		copyMat3(downmat, m);
	} else {
		F32 n0,n0s,n2,n2s;
		n0=n[0]; n0s=n0*s;
		n2=n[2]; n2s=n2*s;
		m[0][0] = -n2s;		m[0][1] = 0.0;			m[0][2] =  n0s;
		m[1][0] =  n0;		m[1][1] = n1;			m[1][2] =  n2;
		m[2][0] = -n0s*n1;	m[2][1] = (1-n1*n1)*s;	m[2][2] = -n1*n2s;
	}
}

__forceinline static void WorldVectorNorm(const Vec3 bodvec,Vec3 uvec,const Vec3 n, F32 s) {
	F32 n1 = n[1];
	if (n1 >= 1) {
		copyVec3(bodvec, uvec);
	} else if (n1 <= -1) {
		uvec[0] = bodvec[0]; uvec[1] = -bodvec[1]; uvec[2] = -bodvec[2];
	} else {
		F32 n0,n0s,n2,n2s;
		F32 bv0=bodvec[0],bv1=bodvec[1],bv2=bodvec[2];
		n0=n[0]; n0s=n0*s;
		n2=n[2]; n2s=n2*s;
		uvec[0] = -bv0*n2s	+ bv1*n0	+ -bv2*n0s*n1;
		uvec[1] =      		  bv1*n1	+  bv2*(1-n1*n1)*s;
		uvec[2] =  bv0*n0s	+ bv1*n2	+ -bv2*n1*n2s;
	}
}

__forceinline static void BodyVectorNorm(const Vec3 uvec,Vec3 bodvec,const Vec3 n, F32 s)
{
	F32 n1 = n[1];

	if (n1 >= 1.f)
	{
		copyVec3(uvec, bodvec);
	}
	else if (n1 <= -1.f)
	{
		bodvec[0] = uvec[0]; bodvec[1] = -uvec[1]; bodvec[2] = -uvec[2];
	}
	else	
	{
		F32 n0s,n2s;

		n0s=n[0]*s;
		n2s=n[2]*s;
		bodvec[0] = -uvec[0]*n2s		      					+  uvec[2]*n0s;
		bodvec[1] =  uvec[0]*n[0] 		+ uvec[1]*n1 			+  uvec[2]*n[2];
		bodvec[2] = -uvec[0]*n0s*n1 	+ uvec[1]*(1-n1*n1)*s	+ -uvec[2]*n1*n2s;
	}
}

void expandCtriVerts(const CTri *ctri,Vec3 verts[3])
{
	Vec3	v2,xv2;

	v2[1] = 0;
	v2[0] = ctri->v2[0];
	v2[2] = ctri->v2[1];
	WorldVectorNorm(v2,xv2,ctri->norm,ctri->scale);
	addVec3(xv2,ctri->V1,verts[1]);
	v2[0] = ctri->v3[0];
	v2[2] = ctri->v3[1];
	WorldVectorNorm(v2,xv2,ctri->norm,ctri->scale);
	addVec3(xv2,ctri->V1,verts[2]);
	copyVec3(ctri->V1,verts[0]);
}

__forceinline static F32 CTri_PointLineDistSquared(const Vec3 pt, const Vec3 Lpt, const Vec3 Ldir, const F32 Llen, Vec3 col)
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
	return lengthVec3Squared(dvec);
}

F32 CTri_pointLineDistSquared(const Vec3 pt,const Vec3 start,const Vec3 end,Vec3 col)
{
Vec3	dv;
F32		len;

	subVec3(end,start,dv);
	len = normalVec3(dv);
	return CTri_PointLineDistSquared(pt,start,dv,len,col);
}

/* Point: px,pz Line: (lAx,lAz),(lAx+ldx,LAz+ldx) */
static __forceinline F32 CTri_PointLineDist2D(F32 px, F32 pz, F32 lAx, F32 lAz, F32 ldx, F32 ldz, F32 *lx, F32 *lz)
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

static __forceinline F32 CTri_LineLineDist(	const Vec3 L1pt, const Vec3 L1dir, F32 L1len, Vec3 L1isect,
											const Vec3 L2pt, const Vec3 L2dir, F32 L2len, Vec3 L2isect )
{
	Vec3 tempL1isect, tempL2isect;
	Vec3 L12,v_cross,near1,near2,dvec;
	F32	v_cross_mag,inv_v_cross_mag,scale_line1,scale_line2,distsq1;

	if (!L1isect)
		L1isect = tempL1isect;
	if (!L2isect)
		L2isect = tempL2isect;

	subVec3(L2pt ,L1pt, L12);

	crossVec3(L1dir, L2dir,v_cross);
	v_cross_mag = lengthVec3Squared(v_cross);

	if(v_cross_mag > FLT_EPSILON)
	{
		// The lines aren't parallel.

		const F32* pt1;
		const F32* pt2;

		inv_v_cross_mag = 1.0 / v_cross_mag;

		scale_line1 = det3Vec3(L12, L2dir, v_cross) * inv_v_cross_mag;

		if (scale_line1 <= 0)
		{
			// L1's start is closest to L2, do pt line dist.

			pt1 = L1pt;	
		}
		else if (scale_line1 >= L1len)
		{
			// L1's end is closest to L2, do pt line dist.

			scaleAddVec3(L1dir, L1len, L1pt, near1);
			pt1 = near1;	
		}
		else
		{
			pt1 = NULL;
		}

		scale_line2 = det3Vec3(L12, L1dir, v_cross) * inv_v_cross_mag;

		if (scale_line2 <= 0)
		{
			// L2's start is closest to L1, do pt line dist.

			pt2 = L2pt; 
		}
		else if (scale_line2 >= L2len)
		{
			// L2's end is closest to L1, do pt line dist.

			scaleAddVec3(L2dir, L2len, L2pt, near2);
			pt2 = near2;
		}
		else
		{
			pt2 = NULL;
		}

		if(pt1){
			distsq1 = CTri_PointLineDistSquared(pt1, L2pt, L2dir, L2len, L2isect);

			if(!pt2){
				// Distance from pt1 to L2 is smaller than distance from pt2 to L1.
				copyVec3(pt1, L1isect);
			}else{
				F32 distsq2 = CTri_PointLineDistSquared(pt2, L1pt, L1dir, L1len, L1isect);

				if(distsq1 < distsq2){
					// Distance from pt1 to L2 is smaller than distance from pt2 to L1.
					copyVec3(pt1, L1isect);
				}else{
					// Distance from pt2 to L1 is smaller than distance from pt1 to L2.

					copyVec3(pt2, L2isect);

					distsq1 = distsq2;
				}
			}
		}
		else if(pt2){
			// Distance from pt2 to L1 is smaller than distance from pt1 to L2.

			copyVec3(pt2, L2isect);

			distsq1 = CTri_PointLineDistSquared(pt2, L1pt, L1dir, L1len, L1isect);
		}else{
			scaleAddVec3(L1dir,scale_line1,L1pt,L1isect);
			scaleAddVec3(L2dir,scale_line2,L2pt,L2isect);
			subVec3(L2isect,L1isect,dvec);
			distsq1 = lengthVec3Squared(dvec);
		}
	}
	else
	{
		// Zero cross product means lines are parallel.

		F32 dist;
		dist = dotVec3(L12, L1dir);
		if (dist < 0) {
			// L1 start is nearest L2.

			distsq1 = CTri_PointLineDistSquared(L1pt, L2pt, L2dir, L2len, L2isect);
			copyVec3(L1pt, L1isect);
		} else if (dist >= L1len) {
			// L1 end is nearest L2.
			// MS: This is completely wrong, I don't understand why it's seemed okay so far.

			scaleAddVec3(L1dir, L1len, L1pt, L1isect);
			distsq1 = CTri_PointLineDistSquared(L1isect, L2pt, L2dir, L2len, L2isect);
		} else {
			// L2 start is somewhere between L1 start and L1 end.

			scaleAddVec3(L1dir, dist, L1pt, L1isect);
			subVec3(L1isect, L2pt, dvec);
			distsq1 = lengthVec3Squared(dvec);
			copyVec3(L2pt, L2isect);

		}
	}
	return distsq1;
}


__forceinline static F32 LineLineDist2DSquared(F32 l1Ax, F32 l1Az, F32 l1Bx, F32 l1Bz, F32 *x1, F32 *z1,
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
		F32 dp,tx,tz;
		d = fsqrt(l1dx*l1dx+l1dz*l1dz);
		if (d == 0.0) { /* point line intersection */
			d = CTri_PointLineDist2D(l1Ax, l1Az, l2Ax, l2Az, l2dx, l2dz, &tx, &tz);
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

/* Calcs intersection pont on l2 (tri) */
__forceinline static F32 LineLineDist3D2D(Vec3 l1A, Vec3 l1B, Vec3 col,
									   F32 l2Ax, F32 l2Az, F32 l2Bx, F32 l2Bz,
									   int onplane) {
	F32 l1dx = l1B[0] - l1A[0];
	F32 l1dz = l1B[2] - l1A[2];
	F32 l2dx = l2Bx - l2Ax;
	F32 l2dz = l2Bz - l2Az;
	F32 dx = l2Ax - l1A[0];
	F32 dz = l2Az - l1A[2];
	F32 dy,d,invd;
	F32 l1len,l2len;

	l1len = fsqrt(l1dx*l1dx+l1dz*l1dz);
	if (l1len < .001) { /* point line intersection */
		F32 y, tx, tz;
		d = CTri_PointLineDist2D(l1A[0], l1A[2], l2Ax, l2Az, l2dx, l2dz, &tx, &tz);
		y = onplane ? 0 : fabs(l1A[1]) <= fabs(l1B[1]) ? l1A[1] : l1B[1];
		col[0] = tx;
		col[1] = 0;
		col[2] = tz;
		return (d + y*y);
	}
	else
	{
		Vec3 l1dir,l2pt,l2dir;

		l2pt[0] = l2Ax;
		l2pt[1] = 0;
		l2pt[2] = l2Az;
		l2len = fsqrt(l2dx*l2dx + l2dz*l2dz);
		if (l2len < 0.001)
			l2len = 0.001;
		invd = 1.0 / l2len;
		l2dir[0] = l2dx*invd;
		l2dir[1] = 0;
		l2dir[2] = l2dz*invd;
		
		dy = l1B[1] - l1A[1];
		l1len = fsqrt(l1len*l1len+dy*dy);
		if (l1len < 0.001)
			l1len = 0.001;
		invd = 1.0 / l1len;
		l1dir[0] = l1dx*invd;
		l1dir[1] = dy*invd;
		l1dir[2] = l1dz*invd;

		d = CTri_LineLineDist(l2pt, l2dir, l2len, col, l1A, l1dir, l1len, NULL);
		return d;
	}
}

F32 ctriCollideRad(const CTri *tri,const Vec3 A,const Vec3 B,Vec3 col,F32 rad,CtriState *state)
{
	Vec3	tcol;
	F32		v1x,v1z,v2x,v2z,tpx=0,tpz=0;
	F32		d,da,cross,dist,mindistsq=0.0;
	int		onplane;
	Vec3	tmp,tpA,tpB;

	// Transform points into FOR of polygon
	subVec3(A,tri->V1,tmp);
	BodyVectorNorm(tmp,tpA,tri->norm,tri->scale);

	state->backside = 0;
	// Should this be a more elaborate test involving the radius?
	if (tpA[1] < 0.0) /* collision from back side */
	{
		if (state->early_exit)
			return -1;
		else
			state->backside = 1;
	}

	subVec3(B,tri->V1,tmp);
	BodyVectorNorm(tmp,tpB,tri->norm,tri->scale);

	if (tpA[1] < tpB[1]) /* collision from back side */
	{
		if (state->early_exit)
			return -1;
		else
			state->backside = 1;
	}

	if (1)
	{
		F32		len;
		Vec3	T,E,L,l;

		len = state->linelen2;

		L[0] = state->inv_linemag * (tpB[0] - tpA[0]);
		T[0] = tri->midpt[0] - (L[0] * len + tpA[0]);
		E[0] = tri->extents[0] + rad;
		l[0] = fabs(L[0]);
		if( fabs(T[0]) - E[0] > len * l[0] )
			return -1;

		L[1] = state->inv_linemag * (tpB[1] - tpA[1]);
		T[1] = -(L[1] * len + tpA[1]);
		E[1] = rad;
		l[1] = fabs(L[1]);
		if( fabs(T[1]) - E[1] > len * l[1] )
			return -1;

		L[2] = state->inv_linemag * (tpB[2] - tpA[2]);
		T[2] = tri->midpt[1] - (L[2] * len + tpA[2]);
		E[2] = tri->extents[1] + rad;
		l[2] = fabs(L[2]);
		if( fabs(T[2]) - E[2] > len * l[2] )
			return -1;

		//l.cross(x-axis)?
		if( fabs(T[1]*L[2] - T[2]*L[1]) > E[1]*l[2] + E[2]*l[1] )
			return -1;

		//l.cross(y-axis)?
		if( fabs(T[2]*L[0] - T[0]*L[2]) > E[0]*l[2] + E[2]*l[0] )
			return -1;

		//l.cross(z-axis)?
		if( fabs(T[0]*L[1] - T[1]*L[0]) > E[0]*l[1] + E[1]*l[0] )
			return -1;
	}

	/***** Are points on opposite sides of the plane *****/
	if(	(tpA[1] > 0.0 &&
		tpB[1] > 0.0)
		||
		(tpA[1] < 0.0 &&
		tpB[1] < 0.0) )
	{
#if 0
		if (tpA[1] > rad &&
			tpB[1] > rad
			||
			tpA[1] < -rad &&
			tpB[1] < -rad)
		{
			return -1.0; /* > rad above or below tri */
		}
#endif
		onplane = 0;
	}
	else
	{
		onplane = 1;
		/***** Obtain vector from tpA to tpB in the FOR of the plane *****/
		/***** and projected onto the plane.                       *****/
		v1x = tpB[0]-tpA[0];
		v1z = tpB[2]-tpA[2];
		d = fsqrt(SQR(v1x) + SQR(v1z));
		if(d > 0.001) { /* pA != pB */
			/** Find the distance from tpA to the collision position - similar triangles **/
			dist = fabsf(tpA[1] - tpB[1]);
			if (dist == 0.0) {
				onplane = 0; /* Entire line is on the plane, treat as line off the plane */
			} else {
				da = (fabsf(tpA[1])) / dist;
				/* v1 does not need to be normalized because it gets multiplied back by d in da */
				/***** Find the collision position on the plane *****/
				tpx = tpA[0] + v1x*da;
				tpz = tpA[2] + v1z*da;
			}
		}
		else // pA = pB
		{
			tpx = tpA[0];
			tpz = tpA[2];
		}
	}

	if (onplane)
	{
		int intri = 1;
		F32 distsq;
		Vec3 ttcol;

#if 0
		if (0)
		{
			F32		r2;

			r2 = rad * (d+dist) / dist;

			if (tpx > tri->maxpt[0]+r2 || tpx < tri->minpt[0]-r2)
				return -1;
			if (tpz > tri->maxpt[1]+r2 || tpz < tri->minpt[1]-r2)
				return -1;
		}
#endif
	/***** Perform cross products around poly to see if the poly encloses the collision point *****/
		
		v1x = tri->v2[0];	/* get vector from 0,0 (v1) to v2 */
		v1z = tri->v2[1];	
		v2x = tpx;	/* get vector from tp(collision position) to 0,0 */
		v2z = tpz;
		cross = v1x*v2z - v1z*v2x; /* cross v1 with v2 */
		if(cross > 0.0)
		{
			mindistsq = LineLineDist3D2D(tpA, tpB, tcol,0,0, tri->v2[0],tri->v2[1],1);
			intri = 0;
		}
		
		v1x = tri->v3[0] - tri->v2[0];	/* get vector from v2 to v3 */
		v1z = tri->v3[1] - tri->v2[1];	
		v2x = tpx - tri->v2[0];		/* get vector from tp(collision position) to v2 */
		v2z = tpz - tri->v2[1];
		cross = v1x*v2z - v1z*v2x;	/* cross v1 with v2 */
		if(cross > 0.0) {
			distsq = LineLineDist3D2D(tpA, tpB, ttcol, tri->v2[0],tri->v2[1], tri->v3[0],tri->v3[1],1);
			if (intri || distsq < mindistsq) {
				mindistsq = distsq;
				copyVec3(ttcol, tcol);
			}
			intri = 0;
		}
		
		v1x = 0 - tri->v3[0];	/* get 2d vector from v3 to 0,0 */
		v1z = 0 - tri->v3[1];	
		v2x = tpx - tri->v3[0];	/* get 2d vector from tp(collision position) to v3 */
		v2z = tpz - tri->v3[1];
		cross = v1x*v2z - v1z*v2x; /* cross v1 with v2 */
		if(cross > 0.0) {
			distsq = LineLineDist3D2D(tpA, tpB, ttcol, 0,0, tri->v3[0],tri->v3[1],1);
			if (intri || distsq < mindistsq) {
				mindistsq = distsq;
				copyVec3(ttcol,tcol);
			}
			intri = 0;
		}
		if (!intri) { /* tcol already defined */
			if (mindistsq > rad*rad) return -1.0;
		} else { /* Point in/on tri */
			tcol[0] = tpx;
			tcol[1] = 0;
			tcol[2] = tpz;
		}
	} else {
		int Aintri = 1,Bintri = 1, abymin;

		/* line does not intersect plane.
		   Check each point for being in tri; if lowest point is in tri, it wins,
		   else check dist btwn line and tri edges */
		abymin = (fabs(tpA[1]) < fabs(tpB[1])) ? 1 : tpA[1] == tpB[1] ? 3 : 2;
		do {
			v1x = tri->v2[0];
			v1z = tri->v2[1];
			v2x = tpA[0];
			v2z = tpA[2];
			cross = v1x*v2z - v1z*v2x;
			if(cross > 0.0) {Aintri = 0; if (!(abymin&2)) break;}
			v2x = tpB[0];
			v2z = tpB[2];
			cross = v1x*v2z - v1z*v2x;
			if(cross > 0.0) {Bintri = 0; if (!(abymin&1)) break;}
			
			v1x = tri->v3[0] - tri->v2[0];
			v1z = tri->v3[1] - tri->v2[1];	
			v2x = tpA[0] - tri->v2[0];
			v2z = tpA[2] - tri->v2[1];
			cross = v1x*v2z - v1z*v2x;
			if(cross > 0.0) {Aintri = 0; if (!(abymin&2)) break;}
			v2x = tpB[0] - tri->v2[0];
			v2z = tpB[2] - tri->v2[1];
			cross = v1x*v2z - v1z*v2x;
			if(cross > 0.0) {Bintri = 0; if (!(abymin&1)) break;}
		
			v1x = 0 - tri->v3[0];
			v1z = 0 - tri->v3[1];	
			v2x = tpA[0] - tri->v3[0];
			v2z = tpA[2] - tri->v3[1];
			cross = v1x*v2z - v1z*v2x;
			if(cross > 0.0) {Aintri = 0; if (!(abymin&2)) break;}
			v2x = tpB[0] - tri->v3[0];
			v2z = tpB[2] - tri->v3[1];
			cross = v1x*v2z - v1z*v2x;
			if(cross > 0.0) {Bintri = 0;}
		} while(0);

		if (Aintri && (abymin & 1)) {
			copyVec3(tpA,tcol);
			tcol[1] = 0.0;
			mindistsq = CTri_pointLineDistSquared(tcol,tpA,tpB,0);
			/* mindistsq = 0.0 */ /* already init to 0 */
		} else if (Bintri && (abymin & 2)) {
			copyVec3(tpB,tcol);
			tcol[1] = 0.0;
			mindistsq = CTri_pointLineDistSquared(tcol,tpA,tpB,0);
			/* mindistsq = 0.0 */ /* already init to 0 */
		} else { /* Now test the distance between the line and each edge */
			Vec3 ttcol;
			F32 distsq;

			mindistsq = LineLineDist3D2D(tpA, tpB, tcol, 0,0, tri->v2[0],tri->v2[1],0);
			
			distsq = LineLineDist3D2D(tpA, tpB, ttcol, tri->v2[0],tri->v2[1], tri->v3[0],tri->v3[1],0);
			if (distsq < mindistsq) {
				mindistsq = distsq;
				copyVec3(ttcol, tcol);
			}
			distsq = LineLineDist3D2D(tpA, tpB, ttcol, 0,0, tri->v3[0],tri->v3[1],0);
			if (distsq < mindistsq) {
				mindistsq = distsq;
				copyVec3(ttcol, tcol);
			}
			if (mindistsq > rad*rad) return -1.0;
		}
	}
	
	/** Take colision point out of the FOR of the polygon **/
	/* WorldVector + addVec3 */
	if (col)
	{
		WorldVectorNorm(tcol, col, tri->norm,tri->scale);
		addVec3(col, tri->V1, col);
	}
	return mindistsq;
}

F32 ctriCollide(const CTri *cfan,const Vec3 A,const Vec3 B,Vec3 col)
{
int			good=0;
F32 		v1x,v1z,v2x,v2z,tpx,tpz,vtx,vtz;
F32			d,da,dist;
Vec3		tpA,tpB,tmp;

	subVec3(A,cfan->V1,tmp);
	BodyVectorNorm(tmp,tpA,cfan->norm,cfan->scale);
	subVec3(B,cfan->V1,tmp);
	BodyVectorNorm(tmp,tpB,cfan->norm,cfan->scale);

	// Are points on opposite sides of the plane
	if((tpA[1] > 0.0) && (tpB[1] > 0.0))
		return -1;
	else if((tpA[1] < 0.0) && (tpB[1] < 0.0))
		return -1;

	// Obtain vector from tpA to tpB in the FOR of the plane
	// and projected onto the plane
	v1x = tpB[0]-tpA[0];
	v1z = tpB[2]-tpA[2];
	d = fsqrt(SQR(v1x) + SQR(v1z));
	if(d > 0.01) /* pA != pB */
	{
		/** Find the distance from tpA to the collision position - similar triangles **/
		dist = fabsf(tpA[1]) + fabsf(tpB[1]);
		if (dist == 0.0F) 
			da = (fabsf(tpA[1])) * 1000.0;
		else
			da = (fabsf(tpA[1])) / dist;
		/* v1 does not need to be normalized because it gets multiplied back by d in da */
		/***** Find the collision position on the plane *****/
		tpx = tpA[0] + v1x*da;
		tpz = tpA[2] + v1z*da;
	} else /* pA = pB */ {
		tpx = tpA[0];
		tpz = tpA[2];
	}

	/***** Perform cross products around poly to see if the poly encloses the collision point *****/
	/*** get vector from 0,0 (v1) to v2 ***/
	v1x = cfan->v2[0];
	v1z = cfan->v2[1];
	/*** get vector from tp(collision position) to 0,0 ***/
	if (v1x*tpz - v1z*tpx > 0.f)
		goto bad;
	
	/*** get vector from tp(collision position) to v2 ***/
	v2x = tpx - v1x;
	v2z = tpz - v1z;
	/*** get vector from v2 to v3 ***/
	vtx = cfan->v3[0];
	vtz = cfan->v3[1];
	v1x = vtx - v1x;
	v1z = vtz - v1z;
	if (v1x*v2z - v1z*v2x > 0.f)
		goto bad;

	/*** get 2d vector from v3 to 0,0 ***/
	/*** get 2d vector from tp(collision position) to v3 ***/
	v2x = tpx - vtx;
	v2z = tpz - vtz;
	if (-vtx*v2z + vtz*v2x > 0.f)
		goto bad;
	good = 1;
bad:
	if (!good)
	{
		return -1;
#if 0
		if (!(cfan->flags & TAG_QUAD))
			return -1;
		v1x = cfan->p2[1][0];
		v1z = cfan->p2[1][1];
		/*** get vector from tp(collision position) to 0,0 ***/
		if (v1x*tpz - v1z*tpx > 0.f)
			return -1;

		/*** get vector from tp(collision position) to v2 ***/
		v2x = tpx - v1x;
		v2z = tpz - v1z;
		/*** get vector from v2 to v3 ***/
		vtx = cfan->p2[2][0];
		vtz = cfan->p2[2][1];
		v1x = vtx - v1x;
		v1z = vtz - v1z;
		if (v1x*v2z - v1z*v2x > 0.f)
			return -1;

		/*** get 2d vector from v4 to 0,0 ***/
		/*** get 2d vector from tp(collision position) to v4 ***/
		v2x = tpx - vtx;
		v2z = tpz - vtz;
		if (-vtx*v2z + vtz*v2x > 0.f)
			return -1;
#endif
	}
	/**** Take colision point out of the FOR of the polygon ****/
	/* WorldVector + addVec3 */
	if (col)
	{
	Vec3 tcol;

		tcol[0] = tpx;
		tcol[1] = 0;
		tcol[2] = tpz;
		WorldVectorNorm(tcol, col, cfan->norm,cfan->scale);
		addVec3(col, cfan->V1, col);
	}
	return 1;
}


static int CreateTriUVW(Mat3 mat,const Vec3 v0,const Vec3 v1,const Vec3 v2)
{
	Vec3 Zvec, Yvec, Xvec, Tvec;
	F32 dist,ydiff;
	
	/**** construct uvs ****/
	subVec3(v1,v0,Zvec);	/* Take an arbritrary Z vector from an edge */
	dist = normalVec3(Zvec);
	if (!dist) return 0;
	
	subVec3(v2,v0,Tvec);	/* Take another vector from an edge */
	dist = normalVec3(Tvec);
	if (!dist) return 0;

	crossVec3(Zvec,Tvec,Yvec);			/* CP creates the correct Y vector */
	dist = normalVec3(Yvec);
	if (!dist) return 0;

	ydiff = fabs(MAX(MAX(v0[1],v1[1]),v2[1]) - MIN(MIN(v0[1],v1[1]),v2[1]));

#if 0
	printf("YVEC: %f %f %f\n",VEC3(Yvec));
#endif
	if(	Yvec[1] > Y_TOLERANCE_TO_USE_XYZ_FOR_UVW &&
		ydiff < 0.01f
		||
		Yvec[1] >= 1.f)
	{
		/* XVec not in XZ plane (0 slope) */
		copyVec3(UPVec,Yvec);
		copyVec3(RIGHTVec,Xvec);		/* 0 slope so use arbitrary X vector */
	}
	else if(Yvec[1] < -Y_TOLERANCE_TO_USE_XYZ_FOR_UVW &&
			ydiff < 0.01f
			||
			Yvec[1] <= -1.f)
	{
		/* XVec not in XZ plane (0 slope) */
		copyVec3(DOWNVec,Yvec);
		copyVec3(RIGHTVec,Xvec);		/* 0 slope so use arbitrary X vector */
	} else {
		crossVec3(Yvec,UPVec,Xvec);			/* CP YVec and UPVec to get correct X vector */
		dist = normalVec3(Xvec);
		if(!dist){
			if(Yvec[1] >= 0){
				copyVec3(UPVec,Yvec);
				copyVec3(RIGHTVec,Xvec);		/* 0 slope so use arbitrary X vector */
			}else{
				copyVec3(DOWNVec,Yvec);
				copyVec3(RIGHTVec,Xvec);		/* 0 slope so use arbitrary X vector */
			}
		}
	}
	crossVec3(Xvec,Yvec,Zvec);			/* CP of XVec and YVec creates correct Z vector */
	dist = normalVec3(Zvec);
	if (!dist) {
#if 0
		fprintf(stderr,"Bad UVS:\n  V1: %f %f %f\n  V2: %f %f %f\n  V3: %f %f %f\n",
				VEC3(v0),VEC3(v1),VEC3(v2));
#endif
		return 0;
	}
#if 0
	printf("UVS:\n  X: %f %f %f\n  Y: %f %f %f\n  Z: %f %f %f\n",
		   VEC3(Xvec),VEC3(Yvec),VEC3(Zvec));
#endif
	copyVec3(Xvec, mat[0]);
	copyVec3(Yvec, mat[1]);
	copyVec3(Zvec, mat[2]);

	if ((fabs(Xvec[0])<.0000001 && fabs(Xvec[1])<.0000001 && fabs(Xvec[2])<.0000001) ||
		(fabs(Yvec[0])<.0000001 && fabs(Yvec[1])<.0000001 && fabs(Yvec[2])<.0000001) ||
		(fabs(Zvec[0])<.0000001 && fabs(Zvec[1])<.0000001 && fabs(Zvec[2])<.0000001))
	{
#if 0
		fprintf(stderr,"Bad UVS:\n  V1: %f %f %f\n  V2: %f %f %f\n  V3: %f %f %f\n",
				VEC3(v0),VEC3(v1),VEC3(v2));
		fprintf(stderr,"  XUV:%f %f %f\n  YUV:%f %f %f\n  ZUV:%f %f %f\n",
				VEC3(Xvec),VEC3(Yvec),VEC3(Zvec));
#endif
		return 0;
	}
	return 1;
}

void allocCTri_dbg(CTri **ctriOut MEM_DBG_PARMS)
{
	if(ctriOut)
		*ctriOut = scalloc(sizeof(CTri), 1);
}

int createCTri(CTri *ctri, const Vec3 v0,const Vec3 v1,const Vec3 v2)
{
Vec3	tvec,bvec;
Mat3	mat;
int		j;
Vec3	minpt,maxpt;

	if (!CreateTriUVW(mat, v0, v1, v2))
		return 0;

	copyVec3(v0, ctri->V1);
//	copyVec3(v1, ctri->V2);
//	copyVec3(v2, ctri->V3);
	
	subVec3(v1,v0,tvec);
	mulVecMat3Transpose(tvec,mat,bvec);
	ctri->v2[0] = bvec[0];
	ctri->v2[1] = bvec[2];
	subVec3(v2,v0,tvec);
	mulVecMat3Transpose(tvec,mat,bvec);
	ctri->v3[0] = bvec[0];
	ctri->v3[1] = bvec[2];

	if (((fabs(ctri->v2[0]) < .000001) && (fabs(ctri->v2[1]) < .000001)) ||
		((fabs(ctri->v3[0]) < .000001) && (fabs(ctri->v3[1]) < .000001)))
	{
		return 0;
	}
	for(j=0;j<2;j++)
	{
		maxpt[j] = MAX(ctri->v2[j],ctri->v3[j]);
		maxpt[j] = MAX(maxpt[j],0);

		minpt[j] = MIN(ctri->v2[j],ctri->v3[j]);
		minpt[j] = MIN(minpt[j],0);

		ctri->extents[j] = (maxpt[j] - minpt[j]) * 0.5;
		ctri->midpt[j] = minpt[j] + ctri->extents[j];
	}

	matToNormScale(mat,ctri->norm,&ctri->scale);
	return 1;
}

static int pointNearTri(Vec3 orig,Vec3 end,Vec3 v0,Vec3 v1,Vec3 v2,F32 maxdist,F32 *dist)
{
Vec3	tvec1,tvec2,norm;
F32		d1,d2;

	/* Calc face normal */
	subVec3(v1,v0,tvec1);
	subVec3(v2,v1,tvec2);
	crossVec3(tvec1,tvec2,norm);
	normalVec3(norm);

	d1 = pointPlaneDist(v0,norm,orig);
	d2 = pointPlaneDist(v0,norm,end);
	if (d1 > maxdist && d2 > maxdist)
		return 0;
	return 1;
}


// Find the point on the line segment nearest the given point.
static int	pointLinePos(Vec3 lna,Vec3 lnb,Vec3 pt,Vec3 line_pos)
{
F32		d,line_len;
Vec3	dv_p1,dv_ln;

	subVec3(pt,lna,dv_p1);
	subVec3(lnb,lna,dv_ln);
	line_len = normalVec3(dv_ln);

	d = dotVec3(dv_p1,dv_ln);
	if (d < 0)
	{
		copyVec3(lna,line_pos);
		return 1;
	}
	if (d > line_len)
	{
		copyVec3(lnb,line_pos);
		return 1;
	}

	// Get point on line closest to given point
	scaleVec3(dv_ln,d,dv_ln);
	addVec3(dv_ln,lna,line_pos);

	return 1;
}

static int checkTriPlane(	Vec3 orig_hit_pt,
							const Vec3 pt,
							const Vec3 norm,
							F32 radius,
							const Vec3 v0,
							const Vec3 v1,
							const Vec3 v2,
							F32 *sqdistp)
{
	Vec3	cyp0,cyp1,cyp2,isectpt0,isectpt1,cypt,hit_pt;
	Mat4	mat;
	int		coplanar,ret;
	F32		d,sqdist;

	d = pointPlaneDist(pt,norm,orig_hit_pt);
	if (d < 0)
		return -1; // point above plane, collision point is within cylinder part of capsule

	// turn plane into triangle, cuz i found a tri-tri intersector, but not a tri-plane intersector :)
	camLookAt(norm,mat);
	copyVec3(pt,cyp0);
	copyVec3(pt,cyp1);
	copyVec3(pt,cyp2);
	copyVec3(pt,cypt);

	moveVinX(cyp0,mat,-radius * 40);
	moveVinY(cyp0,mat,-radius * 40);

	moveVinX(cyp1,mat, radius * 40);
	moveVinY(cyp1,mat,-radius * 40);

	moveVinX(cyp2,mat, 0);
	moveVinY(cyp2,mat, radius * 40);

	ret = tri_tri_intersect_with_isectline(v0,v1,v2,cyp0,cyp1,cyp2,&coplanar,
				     isectpt0,isectpt1);
	if (!ret || coplanar) // triangle doesnt intersect cylinder endcap
		return 0;

	// Now we have line seg of triangle intersection with cylinder endcap plane
	// find point closest to line seg
	if (!pointLinePos(isectpt0,isectpt1,cypt,hit_pt))
		return 0;
	sqdist = distance3Squared(hit_pt,cypt);
	if (sqdist > radius * radius)
		return 0;
	copyVec3(hit_pt,orig_hit_pt);
	*sqdistp = sqdist;
	return 1;
}

int ctriCylCheck(	Vec3 orig_hit_pt,
					const Vec3 obj_start,
					const Vec3 obj_end,
					F32 radius,
					const Vec3 verts[3],
					F32 *sqdistp)
{
	Vec3	dv;
	int		ret;

	subVec3(obj_end,obj_start,dv);
	normalVec3(dv);
	ret = checkTriPlane(orig_hit_pt,obj_end,dv,radius,verts[0],verts[1],verts[2],sqdistp);
	if (ret >= 0)
		return ret;
	subVec3(zerovec3,dv,dv);
	return checkTriPlane(orig_hit_pt,obj_start,dv,radius,verts[0],verts[1],verts[2],sqdistp);
}

#define HUGE		1.0e21		/* Huge value			*/


static __forceinline int intcyl(	const Vec3 raybase,
									const Vec3 raydir,
									const Vec3 base,
									const Vec3 axis,
									F32 radius,
									F32 *in,
									F32 *out)
{
	int		hit;		/* True if ray intersects cyl	*/
	Vec3	RC;			/* Ray base to cylinder base	*/
	F32		d;			/* Shortest distance between	*/
						/*   the ray and the cylinder	*/
	F32		t, s;		/* Distances along the ray	*/
	Vec3	n, D, O;
	F32		ln;
const	F32		pinf = HUGE;	/* Positive infinity		*/


	subVec3(raybase, base, RC);
	crossVec3(raydir,axis,n);

	if  ( (ln = normalVec3(n)) == 0. ) {	/* ray parallel to cyl	*/
	    d	 = dotVec3(RC,axis);
	    scaleAddVec3(axis, -d, RC, D);
	    d	 = lengthVec3(D);
	    *in	 = -pinf;
	    *out =  pinf;
	    return (d <= radius);		/* true if ray is in cyl*/
	}

	d    = fabs(dotVec3(RC,n));		/* shortest distance	*/
	hit  = (d <= radius);

	if  (hit) {				/* if ray hits cylinder */
	    crossVec3(RC,axis,O);
	    t = - dotVec3(O,n) / ln;
	    crossVec3(n,axis,O);
	    normalVec3(O);
	    s = fabs(sqrt(radius*radius - d*d) / dotVec3(raydir,O));
	    *in	 = t - s;			/* entering distance	*/
	    *out = t + s;			/* exiting  distance	*/
	}
	return (hit);
}

//static F32	pointPlaneDist(Vec3 v,Vec3 n,Vec3 pt)

static void clipRayToTriangle(	const Vec3 ray_base,
								const Vec3 ray_dir,
								F32 ray_len,
								const Vec3 verts[3],
								Vec3 col_pt)
{
	int		i;
	Vec3	col,vert_dir;
	F32		vert_len,dist_sq;

	for(i=0;i<3;i++)
	{
		subVec3(verts[(i+1)%3],verts[i],vert_dir);
		vert_len = normalVec3(vert_dir);
		dist_sq = CTri_LineLineDist(ray_base, ray_dir, ray_len, col, verts[i], vert_dir, vert_len, NULL );
		if (dist_sq < 0.01)
		{
			copyVec3(col,col_pt);
		}
	}
}

F32 ctriGetPointClosestToStart(	Vec3 orig_hit_pt,
								const Vec3 obj_start,
								const Vec3 obj_end,
								F32 radius,
								const Vec3 verts[3],
								const Vec3 tri_norm,
								const Mat4 obj_mat)
{
	int			i,j,ret,line_outside=0;
	Vec3		ray_dir,cyl_dir,test_pt,new_hit_pt,col;
	F32			line_dist[2],dist,len,d2,closest,dist_from_line_sq,t;
	const F32*	ray_base;

	copyVec3(orig_hit_pt,new_hit_pt);
	closest = distance3Squared(obj_start,orig_hit_pt);
	subVec3(obj_end,obj_start,cyl_dir);
	normalVec3(cyl_dir);
	dist_from_line_sq = CTri_PointLineDistSquared(orig_hit_pt,obj_start,cyl_dir,1000,col);

	for(i=0;i<3;i++)
	{
		ray_base = verts[i];
		subVec3(verts[(i+1)%3],ray_base,ray_dir);
		len = normalVec3(ray_dir);

		ret = intcyl(ray_base,ray_dir,obj_start,cyl_dir,radius,&line_dist[0],&line_dist[1]);
		if (ret)
		{
			line_dist[0] = MAX(0,line_dist[0]);
			line_dist[1] = MIN(len,line_dist[1]);
			if (line_dist[1] < line_dist[0])
				ret = 0;
		}
		if (ret)
		{
			for(j=0;j<2;j++)
			{
				dist = line_dist[j];
				scaleAddVec3(ray_dir,dist,ray_base,test_pt);
				t = CTri_PointLineDistSquared(test_pt,obj_start,cyl_dir,1000,col);
				d2 = distance3Squared(obj_start,test_pt) - t;
				if (d2 < closest)
				{
					closest = d2;
					copyVec3(test_pt,new_hit_pt);
					dist_from_line_sq = t;
				}
			}
		}
		else
		{
			line_outside++;
		}
	}
	if (line_outside > 1)
	{
		Vec3	n,T,T2;
		Mat4	inv_mat;

		ray_base = orig_hit_pt;
		mulVecMat3(tri_norm,obj_mat,n);
		T[0] = n[0]*n[1];
		T[2] = n[2]*n[1];
		T[1] = -(n[0]*n[0]+n[2]*n[2]);
		transposeMat3Copy(obj_mat,inv_mat);
		mulVecMat3(T,inv_mat,T2);
		subVec3(zerovec3,T2,ray_dir);
		if (!normalVec3(ray_dir))
		{
			// pick an arbitrary vector that is NOT cyl_dir
			ray_dir[0] = cyl_dir[1];
			ray_dir[1] = cyl_dir[2];
			ray_dir[2] = cyl_dir[0];
		}
		ret = intcyl(orig_hit_pt,ray_dir,obj_start,cyl_dir,radius,&line_dist[0],&line_dist[1]);

		dist = line_dist[1];
		scaleAddVec3(ray_dir,dist,ray_base,test_pt);
		clipRayToTriangle(ray_base,ray_dir,dist,verts,test_pt);
		t = CTri_PointLineDistSquared(test_pt,obj_start,cyl_dir,1000,col);
		d2 = distance3Squared(obj_start,test_pt) - t;
		if (d2 < closest)
		{
			closest = d2;
			copyVec3(test_pt,new_hit_pt);
			dist_from_line_sq = t;
		}
	}
	copyVec3(new_hit_pt,orig_hit_pt);
	return dist_from_line_sq;
}

