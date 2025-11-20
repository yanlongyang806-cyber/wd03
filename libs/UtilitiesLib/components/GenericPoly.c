#include "GenericPoly.h"
#include "GenericMesh.h"
#include "frustum.h"
#include "qsortG.h"
#include "utils.h"

__forceinline static void absVec3(Vec3 a)
{
	a[0] = fabsf(a[0]);
	a[1] = fabsf(a[1]);
	a[2] = fabsf(a[2]);
}

__forceinline static void maxAbsVec3(const Vec3 a, const Vec3 b, Vec3 r)
{
	r[0] = MAXF(fabsf(a[0]), fabsf(b[0]));
	r[1] = MAXF(fabsf(a[1]), fabsf(b[1]));
	r[2] = MAXF(fabsf(a[2]), fabsf(b[2]));
}

__forceinline static int nearSameVec3E(const Vec3 a, const Vec3 b)
{
	Vec3 m, d;
	subVec3(a, b, d);
	absVec3(d);
	maxAbsVec3(a, b, m);
	scaleVec3(m, FLT_EPSILON * 9, m);

	return d[0] <= m[0] && d[1] <= m[1] && d[2] <= m[2];
}

// true indicates the vertices are not collinear, false indicates vertices are collinear
__forceinline static int makePlaneE(const Vec3 p0, const Vec3 p1, const Vec3 p2, Vec4 plane)
{
	Vec3 a, b;
	float projection;
	float lengthsquared;

	subVec3(p1, p0, a);
	subVec3(p2, p0, b);
	crossVec3(a, b, plane);

	// p0->p1 and p1->p2 are collinear if point-to-line dist of p2 to <p0,p1> is less-than some relative error tolerance
	lengthsquared = lengthVec3Squared(a);
	projection = dotVec3(a, b) / AVOID_DIV_0(lengthsquared);
	scaleVec3(a, projection, a);
	if (nearSameVec3Tol(a,b,0.5))
//	if (nearSameVec3E(a,b))
		return 0;

	normalVec3(plane);
	plane[3] = dotVec3(plane, p0);

	return 1;
}

//////////////////////////////////////////////////////////////////////////

void gpolySetSize_dbg(GPoly *poly, int count MEM_DBG_PARMS)
{
	sdynArrayReserveStructs(poly->points, poly->max, count);
	poly->count = count;
}

void gpolyAddPoint_dbg(GPoly *poly, const Vec3 p MEM_DBG_PARMS)
{
	F32 *newp = sdynArrayAddStruct(poly->points, poly->count, poly->max);
	copyVec3(p, newp);
}

void gpolyAddUniquePoint_dbg(GPoly *poly, const Vec3 p MEM_DBG_PARMS)
{
	int i;
	for (i = 0; i < poly->count; i++)
	{
		if (nearSameVec3(poly->points[i], p))
			return;
	}

	gpolyAddPoint_dbg(poly, p MEM_DBG_PARMS_CALL);
}

void gpolyRemovePoint(GPoly *poly, int idx)
{
	if (idx < poly->count)
	{
		if (idx < poly->count-1)
			CopyStructs(&poly->points[idx], &poly->points[idx+1], poly->count - idx - 1);
		poly->count--;
	}
}

void gpolyFreeData(GPoly *poly)
{
	SAFE_FREE(poly->points);
	ZeroStruct(poly);
}

void gpolyCopy_dbg(GPoly *dst, const GPoly *src MEM_DBG_PARMS)
{
	gpolySetSize_dbg(dst, src->count MEM_DBG_PARMS_CALL);
	CopyStructs(dst->points, src->points, src->count);
}

void gpolyRemoveDuplicates(GPoly *poly)
{
	int i, j;
	for (i = 0; i < poly->count; i++)
	{
		for (j = i+1; j < poly->count; j++)
		{
			//if (nearSameVec3(poly->points[i], poly->points[j]))
			if (nearSameVec3(poly->points[i], poly->points[j]))
			{
				gpolyRemovePoint(poly, j);
				j--;
			}
		}
	}
}

void gpolyToPlane(const GPoly * poly, Vec4 plane)
{
	int poly_verts = poly->count;
	const Vec3 * vc = poly->points;
	const Vec3 * vb = vc + poly_verts - 1;
	const Vec3 * va = vb - 1;

	for (; poly_verts; --poly_verts, ++vc)
	{
		if (makePlaneE(va[0], vb[0], vc[0], plane))
			return;

		va = vb;
		vb = vc;
	}
}

static bool gpolyToPlaneTransformed(const GPoly * poly, const Mat4 transform, Vec4 plane)
{
	int poly_verts = poly->count;
	Vec3 va, vb, vc;
	mulVecMat4(poly->points[0], transform, vb);
	mulVecMat4(poly->points[1], transform, vc);
	for (; poly_verts; )
	{
		--poly_verts;
		mulVecMat4(poly->points[poly_verts], transform, va);
		if (makePlaneE(va, vb, vc, plane))
			return true;

		copyVec3(vb, vc);
		copyVec3(va, vb);
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////

void gpsetReserve_dbg(GPolySet *set, int max MEM_DBG_PARMS)
{
	if (max > set->max)
	{
		sdynArrayReserveStructs(set->polys, set->max, max);
	}
}

void gpsetSetSize_dbg(GPolySet *set, int count MEM_DBG_PARMS)
{
	int i;
	sdynArrayReserveStructs(set->polys, set->max, count);
	for (i = count; i < set->count; i++)
		gpolySetSize_dbg(&set->polys[i], 0 MEM_DBG_PARMS_CALL);
	set->count = count;
}

GPoly *gpsetAddPoly_dbg(GPolySet *set MEM_DBG_PARMS)
{
	GPoly *dst = sdynArrayAddStruct_no_memset(set->polys, set->count, set->max);
	gpolySetSize_dbg(dst, 0 MEM_DBG_PARMS_CALL);
	return dst;
}

GPoly *gpsetAddPolyCopy_dbg(GPolySet *set, const GPoly *src MEM_DBG_PARMS)
{
	GPoly *dst = gpsetAddPoly_dbg(set MEM_DBG_PARMS_CALL);
	gpolySetSize_dbg(dst, src->count MEM_DBG_PARMS_CALL);
	CopyStructs(dst->points, src->points, src->count);
	return dst;
}

void gpsetRemovePoly(GPolySet *set, int idx)
{
	if (idx >= 0 && idx < set->count)
	{
		if (idx < set->count-1)
		{
			GPoly temp;
			CopyStructs(&temp, &set->polys[idx], 1);
			CopyStructs(&set->polys[idx], &set->polys[idx+1], set->count - idx - 1);
			CopyStructs(&set->polys[set->count-1], &temp, 1);
		}
		set->count--;
	}
}

void gpsetFreeData(GPolySet *set)
{
	int i;
	for (i = 0; i < set->max; i++)
	{
		SAFE_FREE(set->polys[i].points);
	}

	SAFE_FREE(set->polys);
	ZeroStruct(set);
}

void gpsetCopy_dbg(GPolySet *dst, const GPolySet *src MEM_DBG_PARMS)
{
	int i;
	gpsetSetSize_dbg(dst, src->count MEM_DBG_PARMS_CALL);
	for (i = 0; i < src->count; i++)
		gpolyCopy_dbg(&dst->polys[i], &src->polys[i] MEM_DBG_PARMS_CALL);
}

//////////////////////////////////////////////////////////////////////////

void gpsetMakeBox_dbg(GPolySet *set, const Vec3 boxmin, const Vec3 boxmax MEM_DBG_PARMS)
{
	Vec3 points[8];

	setVec3(points[0], boxmin[0], boxmin[1], boxmin[2]);
	setVec3(points[1], boxmax[0], boxmin[1], boxmin[2]);
	setVec3(points[2], boxmax[0], boxmax[1], boxmin[2]);
	setVec3(points[3], boxmin[0], boxmax[1], boxmin[2]);
	setVec3(points[4], boxmin[0], boxmin[1], boxmax[2]);
	setVec3(points[5], boxmax[0], boxmin[1], boxmax[2]);
	setVec3(points[6], boxmax[0], boxmax[1], boxmax[2]);
	setVec3(points[7], boxmin[0], boxmax[1], boxmax[2]);

	gpsetSetSize_dbg(set, 6 MEM_DBG_PARMS_CALL);

	// make polys face inward

	// near
	gpolySetSize_dbg(&set->polys[0], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[0], set->polys[0].points[0]);
	copyVec3(points[1], set->polys[0].points[1]);
	copyVec3(points[2], set->polys[0].points[2]);
	copyVec3(points[3], set->polys[0].points[3]);

	// far
	gpolySetSize_dbg(&set->polys[1], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[7], set->polys[1].points[0]);
	copyVec3(points[6], set->polys[1].points[1]);
	copyVec3(points[5], set->polys[1].points[2]);
	copyVec3(points[4], set->polys[1].points[3]);

	// left
	gpolySetSize_dbg(&set->polys[2], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[0], set->polys[2].points[0]);
	copyVec3(points[3], set->polys[2].points[1]);
	copyVec3(points[7], set->polys[2].points[2]);
	copyVec3(points[4], set->polys[2].points[3]);

	// top
	gpolySetSize_dbg(&set->polys[3], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[1], set->polys[3].points[0]);
	copyVec3(points[0], set->polys[3].points[1]);
	copyVec3(points[4], set->polys[3].points[2]);
	copyVec3(points[5], set->polys[3].points[3]);

	// right
	gpolySetSize_dbg(&set->polys[4], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[1], set->polys[4].points[0]);
	copyVec3(points[5], set->polys[4].points[1]);
	copyVec3(points[6], set->polys[4].points[2]);
	copyVec3(points[2], set->polys[4].points[3]);

	// bottom
	gpolySetSize_dbg(&set->polys[5], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[2], set->polys[5].points[0]);
	copyVec3(points[6], set->polys[5].points[1]);
	copyVec3(points[7], set->polys[5].points[2]);
	copyVec3(points[3], set->polys[5].points[3]);
}

void gpsetMakeFrustum_dbg(GPolySet *set, const Mat4 cammat, F32 fovx, F32 fovy, F32 znear, F32 zfar, F32 scale MEM_DBG_PARMS)
{
	Vec3 p, points[8];
	F32 t, b, l, r;
	F32 tanhalffovx = tanf(fovx * PI / 360.0);
	F32 tanhalffovy = tanf(fovy * PI / 360.0);

	znear = ABS(znear);
	zfar = ABS(zfar);

	t = znear * tanhalffovy;
	b = -t;
	r = znear * tanhalffovx;
	l = -r;

	setVec3(p, l, t, -znear);
	mulVecMat4(p, cammat, points[0]);

	setVec3(p, r, t, -znear);
	mulVecMat4(p, cammat, points[1]);

	setVec3(p, r, b, -znear);
	mulVecMat4(p, cammat, points[2]);

	setVec3(p, l, b, -znear);
	mulVecMat4(p, cammat, points[3]);

	t = zfar * tanhalffovy;
	b = -t;
	r = zfar * tanhalffovx;
	l = -r;

	setVec3(p, l, t, -zfar);
	mulVecMat4(p, cammat, points[4]);

	setVec3(p, r, t, -zfar);
	mulVecMat4(p, cammat, points[5]);

	setVec3(p, r, b, -zfar);
	mulVecMat4(p, cammat, points[6]);

	setVec3(p, l, b, -zfar);
	mulVecMat4(p, cammat, points[7]);

	if (scale != 1.f)
	{
		int i;

		// calculate center of points
		setVec3same(p, 0);
		for (i = 0; i < 8; ++i)
			addVec3(p, points[i], p);
		scaleVec3(p, 1.f / 8.f, p);

		for (i = 0; i < 8; ++i)
		{
			Vec3 diff;
			subVec3(points[i], p, diff);
			scaleAddVec3(diff, scale - 1.f, points[i], points[i]);
		}
	}

	gpsetSetSize_dbg(set, 6 MEM_DBG_PARMS_CALL);

	// make polys face inward

	// near
	gpolySetSize_dbg(&set->polys[0], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[0], set->polys[0].points[0]);
	copyVec3(points[1], set->polys[0].points[1]);
	copyVec3(points[2], set->polys[0].points[2]);
	copyVec3(points[3], set->polys[0].points[3]);

	// far
	gpolySetSize_dbg(&set->polys[1], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[7], set->polys[1].points[0]);
	copyVec3(points[6], set->polys[1].points[1]);
	copyVec3(points[5], set->polys[1].points[2]);
	copyVec3(points[4], set->polys[1].points[3]);

	// left
	gpolySetSize_dbg(&set->polys[2], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[0], set->polys[2].points[0]);
	copyVec3(points[3], set->polys[2].points[1]);
	copyVec3(points[7], set->polys[2].points[2]);
	copyVec3(points[4], set->polys[2].points[3]);

	// top
	gpolySetSize_dbg(&set->polys[3], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[1], set->polys[3].points[0]);
	copyVec3(points[0], set->polys[3].points[1]);
	copyVec3(points[4], set->polys[3].points[2]);
	copyVec3(points[5], set->polys[3].points[3]);

	// right
	gpolySetSize_dbg(&set->polys[4], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[1], set->polys[4].points[0]);
	copyVec3(points[5], set->polys[4].points[1]);
	copyVec3(points[6], set->polys[4].points[2]);
	copyVec3(points[2], set->polys[4].points[3]);

	// bottom
	gpolySetSize_dbg(&set->polys[5], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[2], set->polys[5].points[0]);
	copyVec3(points[6], set->polys[5].points[1]);
	copyVec3(points[7], set->polys[5].points[2]);
	copyVec3(points[3], set->polys[5].points[3]);
}

void gpsetMakeFrustumFromPoints_dbg(GPolySet *set, const Vec3 points[8] MEM_DBG_PARMS)
{
	gpsetSetSize_dbg(set, 6 MEM_DBG_PARMS_CALL);

	// make polys face inward

	// near
	gpolySetSize_dbg(&set->polys[0], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[0], set->polys[0].points[0]);
	copyVec3(points[1], set->polys[0].points[1]);
	copyVec3(points[2], set->polys[0].points[2]);
	copyVec3(points[3], set->polys[0].points[3]);

	// far
	gpolySetSize_dbg(&set->polys[1], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[7], set->polys[1].points[0]);
	copyVec3(points[6], set->polys[1].points[1]);
	copyVec3(points[5], set->polys[1].points[2]);
	copyVec3(points[4], set->polys[1].points[3]);

	// left
	gpolySetSize_dbg(&set->polys[2], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[0], set->polys[2].points[0]);
	copyVec3(points[3], set->polys[2].points[1]);
	copyVec3(points[7], set->polys[2].points[2]);
	copyVec3(points[4], set->polys[2].points[3]);

	// top
	gpolySetSize_dbg(&set->polys[3], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[1], set->polys[3].points[0]);
	copyVec3(points[0], set->polys[3].points[1]);
	copyVec3(points[4], set->polys[3].points[2]);
	copyVec3(points[5], set->polys[3].points[3]);

	// right
	gpolySetSize_dbg(&set->polys[4], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[1], set->polys[4].points[0]);
	copyVec3(points[5], set->polys[4].points[1]);
	copyVec3(points[6], set->polys[4].points[2]);
	copyVec3(points[2], set->polys[4].points[3]);

	// bottom
	gpolySetSize_dbg(&set->polys[5], 4 MEM_DBG_PARMS_CALL);
	copyVec3(points[2], set->polys[5].points[0]);
	copyVec3(points[6], set->polys[5].points[1]);
	copyVec3(points[7], set->polys[5].points[2]);
	copyVec3(points[3], set->polys[5].points[3]);
}

static GPoly *clipcollecter = 0;
int gpolyClipPlane_dbg(GPoly *poly, const Vec4 clipplane MEM_DBG_PARMS)
{
	Vec3 p;
	int idx, previdx;
	GPoly clipped = {0};
	F32 *distances = _alloca(sizeof(F32) * poly->count);
	//int crossCount = 0;

	clipped.max = poly->count * 2 + 1;
	clipped.points = (Vec3*)_alloca(sizeof(Vec3) * (poly->count * 2 + 1));
	for (idx = 0; idx < poly->count; idx++)
		distances[idx] = distanceToPlane(poly->points[idx], clipplane);

	idx = 0;
	previdx = poly->count - 1;
	while (idx < poly->count)
	{
		if ((distances[previdx] < -0.001 && distances[idx] > 0.001) || (distances[previdx] > 0.001 && distances[idx] < -0.001))
		{
			//++crossCount;
			//assert(crossCount <= 2 );
			intersectPlane2(poly->points[previdx], poly->points[idx], clipplane, distances[previdx], distances[idx], p);
			gpolyAddPoint_dbg(&clipped, p MEM_DBG_PARMS_CALL);
			if (clipcollecter)
				gpolyAddPoint_dbg(clipcollecter, p MEM_DBG_PARMS_CALL);
		}
		if (distances[idx] >= -0.001)
		{
			gpolyAddPoint_dbg(&clipped, poly->points[idx] MEM_DBG_PARMS_CALL);
		}

		previdx = idx;
		idx++;
	}

	gpolyRemoveDuplicates(&clipped);

	sdynArrayReserveStructs(poly->points, poly->max, clipped.count);
	poly->count = clipped.count;
	memcpy(poly->points, clipped.points, sizeof(Vec3) * clipped.count);

	if (poly->count < 3 && clipcollecter)
	{
		for (idx = 0; idx < poly->count; idx++)
			gpolyAddPoint_dbg(clipcollecter, poly->points[idx] MEM_DBG_PARMS_CALL);
	}

	return poly->count > 2;
}

static int gpolyComparePointWithAngle(const Vec4 lhs, const Vec4 rhs)
{
	float difference = lhs[3] - rhs[3];
	int comparison = 0;

	if (difference > 0.0f)
		comparison = 1;
	else
	if (difference < 0.0f)
		comparison = -1;

	return comparison;
}

static void gpolyConvexHull(GPoly *poly, const Vec4 plane)
{
	const Vec3 * poly_points = poly->points;
	int poly_count = poly->count;
	Vec3 c = { 0 }, u, v, n;
	Vec4 * polyWithAngles;
	int i;
	F32 u_mag2;

	if (!poly_count)
		return;

	polyWithAngles = (Vec4*)alloca(sizeof(Vec4) * poly_count);

	// sort points around appx centroid
	for (i = 0; i < poly_count; ++i)
		addVec3(poly_points[i], c, c);
	scaleVec3(c, 1.0f / poly_count, c);

	// get basis angle for 0 deg
	copyVec3(poly_points[0], polyWithAngles[0]);
	polyWithAngles[0][3] = 0.0f;
	subVec3(poly_points[0], c, u);
	//normalVec3(u);
	u_mag2 = lengthVec3Squared(u);

	// get angles
	for (i = 1; i < poly_count; ++i)
	{
		copyVec3(poly_points[i], polyWithAngles[i]);
		subVec3(poly_points[i], c, v);
		//normalVec3(v);
		crossVec3(u,v,n);
		polyWithAngles[i][3] = 1 - CLAMPF32(dotVec3(u,v) / sqrtf(u_mag2 * lengthVec3Squared(v)), -1.0f, 1.0f);
		if (dotVec3(n, plane) < 0.0f)
			polyWithAngles[i][3] = -1.0f - polyWithAngles[i][3];
	}

	// sort points by angle
	qsortG(polyWithAngles, poly_count, sizeof(Vec4), gpolyComparePointWithAngle);

	// return sorted points
	for (i = 0; i < poly_count; ++i)
		copyVec3(polyWithAngles[i], poly->points[i]);
}

void gpsetClipPlane_dbg(GPolySet *set, const Vec4 clipplane MEM_DBG_PARMS)
{
	int i;
	static GPoly newpoly={0};

	newpoly.count = 0;

	sdynArrayReserveStructs(newpoly.points, newpoly.max, set->count*2);
	clipcollecter = &newpoly;
	for (i = 0; i < set->count; i++)
	{
		GPoly *poly = &set->polys[i];
		if (!gpolyClipPlane_dbg(poly, clipplane MEM_DBG_PARMS_CALL))
		{
			gpsetRemovePoly(set, i);
			i--;
		}
	}
	clipcollecter = 0;

	// remove any duplicate clipped points
	gpolyRemoveDuplicates(&newpoly);

	// add the new poly to the set
	if (newpoly.count > 2)
	{
		// order the clipped points so that they create a polygon facing the direction of the clipplane
		gpolyConvexHull(&newpoly, clipplane);

		// remove any duplicate clipped points
		gpolyRemoveDuplicates(&newpoly);

		gpsetAddPolyCopy_dbg(set, &newpoly MEM_DBG_PARMS_CALL);
	}
}

void gpsetClipBox_dbg(GPolySet *set, const Vec3 clipmin, const Vec3 clipmax MEM_DBG_PARMS)
{
	Vec4 clipplane;

	setVec4(clipplane, 1, 0, 0, clipmin[0]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	setVec4(clipplane, -1, 0, 0, -clipmax[0]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, 1, 0, clipmin[1]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, -1, 0, -clipmax[1]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, 0, 1, clipmin[2]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, 0, -1, -clipmax[2]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);
}

// Use this function to examine each step of the box-polyset clipping.
void gpsetClipBoxDebug_dbg(GPolySet *set, const Vec3 clipmin, const Vec3 clipmax, GPolySet * debugOutput, int nPlaneIndex MEM_DBG_PARMS)
{
	Vec4 clipplane;

	setVec4(clipplane, 1, 0, 0, clipmin[0]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	if (nPlaneIndex == 0)
		gpsetCopy_dbg(debugOutput, set MEM_DBG_PARMS_CALL);

	setVec4(clipplane, -1, 0, 0, -clipmax[0]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	if (nPlaneIndex == 1)
		gpsetCopy_dbg(debugOutput, set MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, 1, 0, clipmin[1]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	if (nPlaneIndex == 2)
		gpsetCopy_dbg(debugOutput, set MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, -1, 0, -clipmax[1]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	if (nPlaneIndex == 3)
		gpsetCopy_dbg(debugOutput, set MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, 0, 1, clipmin[2]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	if (nPlaneIndex == 4)
		gpsetCopy_dbg(debugOutput, set MEM_DBG_PARMS_CALL);

	setVec4(clipplane, 0, 0, -1, -clipmax[2]);
	gpsetClipPlane_dbg(set, clipplane MEM_DBG_PARMS_CALL);

	if (nPlaneIndex == 5)
		gpsetCopy_dbg(debugOutput, set MEM_DBG_PARMS_CALL);
}

void gpolyBounds(const GPoly *poly, Vec3 min, Vec3 max)
{
	int i;
	setVec3(min, 8e16, 8e16, 8e16);
	setVec3(max, -8e16, -8e16, -8e16);

	for (i = 0; i < poly->count; i++)
		vec3RunningMinMax(poly->points[i], min, max);
}

void gpolyTransform(GPoly *poly, const Mat4 mat)
{
	int i;
	Vec3 v;
	for (i = 0; i < poly->count; i++)
	{
		mulVecMat4(poly->points[i], mat, v);
		copyVec3(v, poly->points[i]);
	}
}

void gpolyTransformToBounds(const GPoly *poly, const Mat4 mat, Vec3 min, Vec3 max)
{
	int i;
	Vec3 v;
	setVec3(min, 8e16, 8e16, 8e16);
	setVec3(max, -8e16, -8e16, -8e16);

	for (i = 0; i < poly->count; i++)
	{
		mulVecMat4(poly->points[i], mat, v);
		vec3RunningMinMax(v, min, max);
	}
}

void gpolyTransformToBounds44(const GPoly *poly, const Mat44 mat, Vec3 min, Vec3 max)
{
	int i;
	Vec4 v;
	setVec3(min, 8e16, 8e16, 8e16);
	setVec3(max, -8e16, -8e16, -8e16);

	for (i = 0; i < poly->count; i++)
	{
		mulVecMat44(poly->points[i], mat, v);
		if (v[3] > 0)
		{
			F32 scale = 1.f / v[3];
			scaleVec3(v, scale, v);
			vec3RunningMinMax(v, min, max);
		}
	}
}

void gpsetBounds(const GPolySet *set, Vec3 min, Vec3 max)
{
	int i, j;
	setVec3(min, 8e16, 8e16, 8e16);
	setVec3(max, -8e16, -8e16, -8e16);

	for (i = 0; i < set->count; i++)
	{
		for (j = 0; j < set->polys[i].count; j++)
			vec3RunningMinMax(set->polys[i].points[j], min, max);
	}
}

void gpsetTransform(GPolySet *set, const Mat4 mat)
{
	int i;
	for (i = 0; i < set->count; i++)
		gpolyTransform(&set->polys[i], mat);
}

void gpsetTransformToBounds(const GPolySet *set, const Mat4 mat, Vec3 min, Vec3 max)
{
	int i, j;
	Vec3 v;
	setVec3(min, 8e16, 8e16, 8e16);
	setVec3(max, -8e16, -8e16, -8e16);

	for (i = 0; i < set->count; i++)
	{
		for (j = 0; j < set->polys[i].count; j++)
		{
			mulVecMat4(set->polys[i].points[j], mat, v);
			vec3RunningMinMax(v, min, max);
		}
	}
}

void gpsetTransformToBounds44(const GPolySet *set, const Mat44 mat, Vec3 min, Vec3 max)
{
	int i, j;
	Vec4 v;
	setVec3(min, 8e16, 8e16, 8e16);
	setVec3(max, -8e16, -8e16, -8e16);

	for (i = 0; i < set->count; i++)
	{
		for (j = 0; j < set->polys[i].count; j++)
		{
			mulVecMat44(set->polys[i].points[j], mat, v);
			if (v[3] > 0)
			{
				F32 scale = 1.f / v[3];
				scaleVec3(v, scale, v);
				vec3RunningMinMax(v, min, max);
			}
		}
	}
}

void gpsetCollapse_dbg(const GPolySet *set, GPoly *pointlist MEM_DBG_PARMS)
{
	int i, j;
	gpolyFreeData(pointlist);
	for (i = 0; i < set->count; i++)
	{
		for (j = 0; j < set->polys[i].count; j++)
			gpolyAddUniquePoint_dbg(pointlist, set->polys[i].points[j] MEM_DBG_PARMS_CALL);
	}
}

static int gpolyContainsPoints(const GPoly *poly, const Vec3 p0, const Vec3 p1)
{
	int i;
	int pcount = 0;
	for (i = 0; i < poly->count; i++)
	{
		if (nearSameVec3(poly->points[i], p0))
			pcount++;
		if (nearSameVec3(poly->points[i], p1))
			pcount++;
	}

	return pcount >= 2;
}

void gpsetExtrudeConvexHull_dbg(GPolySet *dst, const GPolySet *src, const Vec3 extrude MEM_DBG_PARMS)
{
	Vec4 plane;
	int i, j, k, countEdges = 0;
	Vec3 extrude_dir;
	F32 *cosGammas = _alloca(sizeof(F32) * src->count);

	copyVec3(extrude, extrude_dir);
	normalVec3(extrude_dir);

	gpsetSetSize_dbg(dst, 0 MEM_DBG_PARMS_CALL);
	gpsetReserve_dbg(dst, src->count MEM_DBG_PARMS_CALL);

	for (i = 0; i < src->count; i++)
	{
		GPoly *poly = &src->polys[i];
		GPoly *polyOut;

		if (poly->count < 3)
			continue;
		gpolyToPlane(poly, plane);
		cosGammas[i] = -dotVec3(extrude_dir, plane); // negative because convex hull plane normals point inward
		polyOut = gpsetAddPolyCopy_dbg(dst, poly MEM_DBG_PARMS_CALL);

		if (cosGammas[i] > 0.001f)
		{
			// facing in extrude direction, move it
			for (j = 0; j < polyOut->count; j++)
			{
				addVec3(polyOut->points[j], extrude, polyOut->points[j]);
			}
		}
		else
			++countEdges;
	}

	gpsetReserve_dbg(dst, dst->count + countEdges * 3 MEM_DBG_PARMS_CALL);

	// now fill in the separated edges with quads
	for (i = 0; i < src->count; i++)
	{
		int prevJ;
		GPoly *poly = &src->polys[i];
		if (poly->count < 3 || cosGammas[i] <= 0.001f)
			continue;

		prevJ = poly->count-1;
		for (j = 0; j < poly->count; j++)
		{
			// find edges that were extruded
			for (k = 0; k < src->count; k++)
			{
				// only find neighbor polys that were moved
				if (i == k || poly->count < 3 || cosGammas[k] > 0.001f)
					continue;

				if (gpolyContainsPoints(&src->polys[k], poly->points[prevJ], poly->points[j]))
				{
					GPoly *polyOut = gpsetAddPoly_dbg(dst MEM_DBG_PARMS_CALL);
					gpolySetSize_dbg(polyOut, 4 MEM_DBG_PARMS_CALL);
					copyVec3(poly->points[j], polyOut->points[3]);
					copyVec3(poly->points[prevJ], polyOut->points[2]);
					addVec3(poly->points[prevJ], extrude, polyOut->points[1]);
					addVec3(poly->points[j], extrude, polyOut->points[0]);
					break;
				}
			}

			prevJ = j;
		}
	}
}

void gpsetToConvexHull_dbg(const GPolySet *set, GConvexHull *hull, int eyespace MEM_DBG_PARMS)
{
	int i;
	Vec4 plane;

	hull->count = 0;

	for (i = 0; i < set->count; i++)
	{
		GPoly *poly = &set->polys[i];
		if (poly->count < 3)
			continue;
		gpolyToPlane(poly, plane);
		if (eyespace)
			flipPlane(plane);

		hullAddPlane_dbg(hull, plane MEM_DBG_PARMS_CALL);
	}
}

void gpsetToConvexHullTransformed_dbg(const GPolySet *set, GConvexHull *hull, int eyespace, Mat4 transform MEM_DBG_PARMS)
{
	int i;
	Vec4 plane;

	hull->count = 0;

	for (i = 0; i < set->count; i++)
	{
		GPoly *poly = &set->polys[i];
		if (poly->count < 3)
			continue;
		if (!gpolyToPlaneTransformed(poly, transform, plane))
			continue;
		if (eyespace)
			flipPlane(plane);
		hullAddPlane_dbg(hull, plane MEM_DBG_PARMS_CALL);
	}
}

void gpsetInvertNormals(GPolySet *set)
{
	int i, j;
	for (i = 0; i < set->count; i++)
	{
		GPoly *poly = &set->polys[i];
		int half_count = poly->count / 2;
		if (poly->count < 3)
			continue;
		for (j = 0; j < half_count; j++)
		{
			Vec3 temp;
			copyVec3(poly->points[j], temp);
			copyVec3(poly->points[poly->count - j - 1], poly->points[j]);
			copyVec3(temp, poly->points[poly->count - j - 1]);
		}
	}
}



//////////////////////////////////////////////////////////////////////////

void hullSetSize_dbg(GConvexHull *hull, int count MEM_DBG_PARMS)
{
	sdynArrayFitStructs(&hull->planes, &hull->max, count);
	hull->count = count;
}

void hullAddPlane_dbg(GConvexHull *hull, const Vec4 plane MEM_DBG_PARMS)
{
	F32 *newp = sdynArrayAddStruct(hull->planes, hull->count, hull->max);
	copyVec4(plane, newp);
}

void hullFreeData(GConvexHull *hull)
{
	if (!hull)
		return;

	SAFE_FREE(hull->planes);
	ZeroStruct(hull);
}

void hullDestroy(GConvexHull *hull)
{
	if(!hull)
		return;

	hullFreeData(hull);
	free(hull);
}

void hullCopy_dbg(GConvexHull *dst, const GConvexHull *src MEM_DBG_PARMS)
{
	hullSetSize_dbg(dst, src->count MEM_DBG_PARMS_CALL);
	CopyStructs(dst->planes, src->planes, dst->count);
}

int hullIsPointInside(const GConvexHull *hull, const Vec3 point)
{
	int i;

	if (!hull->count)
		return 0;

	for (i = 0; i < hull->count; i++)
	{
		F32 dist = distanceToPlaneNoEpsilon(point, hull->planes[i]);
		if (dist < 0)
            return 0;
	}

	return 1;
}

#if _PS3
// the following is an optimizied implementation of hullIsSphereInside(). Profiling showed this function being called 
// 4300 times per frame for a total of 1.3ms per frame.  The big complex function is aprox 10% - 20% faster than the little 
// readable one :).  
#include "Vec4H.h"

typedef vector unsigned char vUInt8;

inline vector int vec_loadAndSplatInt( const int *pI )
{
    vUInt8 splatMap = vec_lvsl( 0, pI );
    vector int result = vec_lde( 0, pI );
    splatMap = (vUInt8) vec_splat( (vector int) splatMap, 0 );

    return vec_perm( result, result, splatMap );
}

int hullIsSphereInside(const GConvexHull *hull, const Vec3 center, F32 radius)
{
	int i;
	int insideCount = 0;

	// load up and do 4 planes at a time
	{
		const vector float		centerV = { center[0], center[1], center[2], 0.0f };
		const vector float		radiusV = vec_splats(radius);
		vector float*			pV = (vector float *)hull->planes;
		const vector unsigned char	permuteVector = vec_lvsl( 0, (int*)pV );
		vector int				insideCountV;
		const vector int		hullCountV = vec_loadAndSplatInt(&hull->count); 
		
		const vector int	maskLo = { 0x40000000, 0x40000000, 0x40000000, 0x40000000 };
		const vector int	maskHi = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
		const vector bool iMaskW = { 0, 0, 0, 0xffffffff };
		const vector unsigned int iShiftR = {31, 31, 31, 31};
		vector int	iZeroV;

		insideCountV = vec_xor(insideCountV, insideCountV);		// zero
		iZeroV		 = vec_xor(iZeroV, iZeroV);
		
		// check planes 4 at a time		
		for(i=0;i < hull->count-3; i +=4, pV += 4)
		{
			vector float plane0 = vec_perm( vec_ld( 0, pV ), vec_ld( 16, pV ), permuteVector );
			vector float plane1 = vec_perm( vec_ld( 16, pV ), vec_ld( 32, pV ), permuteVector );
			vector float plane2 = vec_perm( vec_ld( 32, pV ), vec_ld( 48, pV ), permuteVector );
			vector float plane3 = vec_perm( vec_ld( 48, pV ), vec_ld( 64, pV ), permuteVector );
			
			vector float dist0 = vec_madd(centerV, plane0, __vzero());
			vector float dist1 = vec_madd(centerV, plane1, __vzero());
			vector float dist2 = vec_madd(centerV, plane2, __vzero());
			vector float dist3 = vec_madd(centerV, plane3, __vzero());

			vector int cmpRes;

			// sum the products by shfting and adding
			dist0 = vec_add(dist0, vec_sld(dist0, dist0, 4));
			dist1 = vec_add(dist1, vec_sld(dist1, dist1, 4));
			dist2 = vec_add(dist2, vec_sld(dist2, dist2, 4));
			dist3 = vec_add(dist3, vec_sld(dist3, dist3, 4));
			dist0 = vec_add(dist0, vec_sld(dist0, dist0, 8));
			dist1 = vec_add(dist1, vec_sld(dist1, dist1, 8));
			dist2 = vec_add(dist2, vec_sld(dist2, dist2, 8));
			dist3 = vec_add(dist3, vec_sld(dist3, dist3, 8));

			dist0 = vec_and(vec_sub(dist0, plane0), iMaskW );		// dist.w now has distance
			dist1 = vec_and(vec_sub(dist1, plane1), iMaskW );		// dist.w now has distance
			dist2 = vec_and(vec_sub(dist2, plane2), iMaskW );		// dist.w now has distance
			dist3 = vec_and(vec_sub(dist3, plane3), iMaskW );		// dist.w now has distance

			// get the 4 distances into dist0
			dist0 = vec_add( dist0, vec_sld(dist1, dist1, 4));
			dist2 = vec_add( dist2, vec_sld(dist3, dist3, 4));
			dist0 = vec_add( dist0, vec_sld(dist2, dist2, 8));

			// compare to radius and shift such that cmpRes ends up with the 4 results
			cmpRes = vec_cmpb(dist0, radiusV);

			if ( vec_any_ne( iZeroV, vec_and(cmpRes, maskLo)) )
				return 0;

			insideCountV = vec_add( insideCountV, vec_sr(cmpRes, iShiftR) );
		}

		// finish off remainder
		for(;i < hull->count; i ++, pV++)
		{
			vector float plane = vec_perm( vec_ld( 0, pV ), vec_ld( 16, pV ), permuteVector );
			
			vector float dist = vec_madd(centerV, plane, __vzero());
			vector int cmpRes;

			dist = vec_add(dist, vec_sld(dist, dist, 4));
			dist = vec_add(dist, vec_sld(dist, dist, 8));
			dist = vec_sub(dist, plane);					// dist.w now has distance

			cmpRes = vec_and(vec_cmpb(dist, radiusV), iMaskW);
			if ( vec_any_ne( iZeroV, vec_and(cmpRes, maskLo)) )
				return 0;

			insideCountV = vec_add( insideCountV, vec_sr(cmpRes, iShiftR) );
		}

		// combine 4 inside counters
		insideCountV = vec_add(insideCountV, vec_sld(insideCountV, insideCountV, 8));
		insideCountV = vec_add(insideCountV, vec_sld(insideCountV, insideCountV, 4));

		if ( vec_all_ge( insideCountV, hullCountV) )
			return(FRUSTUM_CLIP_NONE);
		else
			return(FRUSTUM_CLIP_SPHERE_PARTIAL);
	}
}

#else
int hullIsSphereInside(const GConvexHull *hull, const Vec3 center, F32 radius)
{
	int i;
	FloatBranchCount insideCount = 0;

	if (!hull->count)
		return 0;

	for (i = 0; i < hull->count; i++)
	{
		F32 dist = distanceToPlaneNoEpsilon(center, hull->planes[i]);
		if (dist < -radius)
			return 0;
		insideCount += FloatBranch(dist, radius, 1, 0);
	}

	return FloatBranch(insideCount, hull->count, FRUSTUM_CLIP_NONE, FRUSTUM_CLIP_SPHERE_PARTIAL);
}
#endif

bool hullBoxCollision(const GConvexHull *hull, const GMesh *mesh, const Vec3 local_min, const Vec3 local_max, const Mat4 world_mat, const Mat4 inv_world_mat)
{
	int i, j, k;

	Vec3 box_points[8];
	Vec4 box_planes[6];

	if (!hull->count)
		return false;

	// find transformed points and planes
	//   4-------0
	//  /|      /|
	// 5-------1 |
	// | 6-----|-2
	// |/      |/
	// 7-------3
	for (i = 0; i < 2; i++)
	{
		for (j = 0; j < 2; j++)
		{
			for (k = 0; k < 2; k++)
			{
				Vec3 temp = {i ? local_min[0] : local_max[0],
					j ? local_min[1] : local_max[1],
					k ? local_min[2] : local_max[2]};
				mulVecMat4(temp, world_mat, box_points[(i << 2) + (j << 1) + k]);
			}
		}
	}
	if (inv_world_mat)
	{
		// In box coordinates, the plane normals are the standard basis vectors, and the d coefficients are the local min/max on each axis.
		// [+/- 1 0 0 -d], [0  +/- 1 0 -d], [0 0 +/- 1 -d]
		// To transform the plane equations from box local to world coordinates, multiply them by the inverse transpose world_matrix. 
		// Since the plane normals are the standard basis vectors, they "select" one row from the inverse transpose matrix, possibly
		// negate it, and then add the inverted translation multiplied by the d coefficient. The d coefficient remains unchanged.
		for (i = 0, j = 0; i < 3; ++i, j += 2)
		{
			getMat3Row(inv_world_mat, i, box_planes[j+0]);
			getMat3Row(inv_world_mat, i, box_planes[j+1]);
			negateVec3(box_planes[j+0], box_planes[j+0]);
			box_planes[j+0][3] = inv_world_mat[3][i] + local_min[i];
			box_planes[j+1][3] = -inv_world_mat[3][i] - local_max[i];
		}
	}
	else
	{
		makePlane(box_points[7], box_points[6], box_points[3], box_planes[0]);
		makePlane(box_points[7], box_points[5], box_points[6], box_planes[1]);
		makePlane(box_points[7], box_points[3], box_points[5], box_planes[2]);
		makePlane(box_points[0], box_points[4], box_points[1], box_planes[3]);
		makePlane(box_points[0], box_points[2], box_points[4], box_planes[4]);
		makePlane(box_points[0], box_points[1], box_points[2], box_planes[5]);
	}

	// check if there exists a plane on the hull such that all points on the box lie outside of it
	for (i = 0; i < hull->count; i++)
	{
		for (j = 0; j < 8; j++)
		{
			F32 dist = distanceToPlaneNoEpsilon(box_points[j], hull->planes[i]);
			if (dist > 0)
				break;
		}
		if (j >= 8)
			return false;
	}

	// check the opposite - i.e. if there exists a plane on the box such that all points on the hull are outside of it
	for (i = 0; i < 6; i++)
	{
		for (j = 0; j < mesh->vert_count; j++)
		{
			F32 dist = distanceToPlaneNoEpsilon(mesh->positions[j], box_planes[i]);
			if (dist > 0)
				break;
		}
		if (j >= mesh->vert_count)
			return false;
	}

	// a collision exists
	return true;
}

int hullCapsuleCollision(GConvexHull *hull, Vec3 cap_start, Vec3 cap_dir, F32 length, F32 radius, Mat4 world_mat)
{
	int i;
	Vec3 world_start, temp, world_end;

	if (!hull->count)
		return 0;

	mulVecMat4(cap_start, world_mat, world_start);
	scaleAddVec3(cap_dir, length, cap_start, temp);
	mulVecMat4(temp, world_mat, world_end);

	for(i=0; i<hull->count; i++)
	{
		F32 dist_start, dist_end;

		dist_start = distanceToPlaneNoEpsilon(world_start, hull->planes[i]);
		dist_end = distanceToPlaneNoEpsilon(world_end, hull->planes[i]);

		if(dist_start < -radius && dist_end < -radius)
		{
			// Both out, convexity means none can be in
			return 0;
		}
	}

	return 1;
}