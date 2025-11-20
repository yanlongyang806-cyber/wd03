#include "Frustum.h"
#if !FRUSTUM_INLINE_ENABLE
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

void frustumCopy(Frustum *dst, const Frustum *src)
{
	hullFreeData(&dst->hull);
	*dst = *src;
	ZeroStructForce(&dst->hull);
	if (src->use_hull)
		hullCopy(&dst->hull, &src->hull);
}

void frustumSet(Frustum *w, F32 fovy, F32 aspect, F32 znear, F32 zfar)
{
	F32			fHalfAngleH, fHalfAngleV;

	hullFreeData(&w->hull);
	w->use_hull = 0;
	w->use_sphere = 0;

	w->fovy = fovy;

	fHalfAngleV = 0.5f*RAD(fovy);

	w->vtan = tanf(fHalfAngleV);
	w->htan = aspect*w->vtan;

	fHalfAngleH = atanf(w->htan);
	w->fovx = DEG(2.0f*fHalfAngleH);

	w->hcos	= cosf(fHalfAngleH);
	// hvam and vvam appear to be identical to htan and vtan.  What is a vam?  [RMARR - 3/22/11]
	w->hvam = (sinf(fHalfAngleH)/w->hcos);

	w->vcos	= cosf(fHalfAngleV);
	w->vvam	= (sinf(fHalfAngleV)/w->vcos);

	w->znear= -znear;
	w->zfar	= -zfar; 
}

void frustumSetOrtho(Frustum *w, F32 aspect, F32 ortho_zoom, F32 near_dist, F32 far_dist)
{
	F32 r = aspect*ortho_zoom;
	F32 t = ortho_zoom;
	F32 n = near_dist;
	F32 f = far_dist;
	Vec4 plane;

	hullFreeData(&w->hull);
	w->use_hull = 1;
	w->use_sphere = 0;

	setVec4(plane, 1, 0, 0, -r);
	hullAddPlane(&w->hull, plane);

	setVec4(plane, -1, 0, 0, -r);
	hullAddPlane(&w->hull, plane);

	setVec4(plane, 0, 1, 0, -t);
	hullAddPlane(&w->hull, plane);

	setVec4(plane, 0, -1, 0, -t);
	hullAddPlane(&w->hull, plane);

	setVec4(plane, 0, 0, 1, -f);
	hullAddPlane(&w->hull, plane);

	setVec4(plane, 0, 0, -1, n);
	hullAddPlane(&w->hull, plane);
}

void frustumSetSphere(Frustum *w, F32 radius)
{
	hullFreeData(&w->hull);
	w->use_hull = 0;
	w->use_sphere = 1;
	w->sphere_radius = radius;
	w->sphere_radius_sqrd = SQR(radius);
}

void frustumGetScreenPosition(const Frustum *w, int screen_width, int screen_height, const Vec3 pos_cameraspace, Vec2 screen_pos)
{
	screen_width >>= 1;
	screen_height >>= 1;
	screen_pos[0] = -pos_cameraspace[0] * screen_width  * (1.f/w->hvam) / pos_cameraspace[2] + screen_width;
	screen_pos[1] = pos_cameraspace[1] * screen_height * (1.f/w->vvam) / pos_cameraspace[2] + screen_height;
}

// len is how far to move endpoint
// start and end are outputs
void frustumGetWorldRay(const Frustum *w, int screen_width, int screen_height, const Vec2 screen_pos, F32 len, Vec3 start, Vec3 end)
{
	Mat4	mat;
	Vec3	dv;
	F32		yaw,pitch;

	copyMat4(w->inv_viewmat,mat);

	dv[0] = (screen_pos[0] - screen_width * 0.5f) / (screen_width * 0.5f);
	dv[1] = (screen_pos[1] - screen_height * 0.5f) / (screen_height * 0.5f);
	dv[2] = 1.f / w->hvam;

	getVec3YP(dv,&yaw,&pitch);
	yawMat3(yaw,mat);
	pitchMat3(pitch,mat);

	copyVec3(mat[3],start);
	copyVec3(mat[3],end);
	moveVinZ(start,mat,1.f);
	moveVinZ(end,mat,len);
}

static F32 calcMinEnclosingSphere(const Vec3 p0, const Vec3 p1, const Vec3 p2, const Vec3 p3, Vec3 center)
{
	/*
	F64 d11, d12, d13, d14;//, d15;
	DMat44 m;

	setMat3Row(m, 0, p0);
	setMat3Row(m, 1, p1);
	setMat3Row(m, 2, p2);
	setMat3Row(m, 3, p3);
	m[3][0] = m[3][1] = m[3][2] = m[3][3] = 1;
	d11 = dmat44Determinant(m);
*/
	// =(  this algorithm is not numerically stable, so just use the fallback method
//	if (ABS(d11) < 0.01f)
	{
		F32 r, rtemp;

		// coplanar, just average the points
		addVec3(p0, p1, center);
		addVec3(p2, center, center);
		addVec3(p3, center, center);
		scaleVec3(center, 0.25f, center);

		r = distance3Squared(p0, center);
		rtemp = distance3Squared(p1, center);
		MAX1(r, rtemp);
		rtemp = distance3Squared(p2, center);
		MAX1(r, rtemp);
		rtemp = distance3Squared(p3, center);
		MAX1(r, rtemp);

		return sqrtf(r);
	}
/*
	setVec4(m[0], dotVec3(p0, p0), dotVec3(p1, p1), dotVec3(p2, p2), dotVec3(p3, p3));
	d12 = dmat44Determinant(m);

	setVec4(m[1], p0[0], p1[0], p2[0], p3[0]);
	d13 = dmat44Determinant(m);

	setVec4(m[2], p0[1], p1[1], p2[1], p3[1]);
	d14 = dmat44Determinant(m);

	// 	setVec4(m[3], p0[2], p1[2], p2[2], p3[2]);
	// 	d15 = mat44Determinant(m);

	center[0] = (F32)(0.5 * d12 / d11);
	center[1] = (F32)(-0.5 * d13 / d11);
	center[2] = (F32)(0.5 * d14 / d11);

	return distance3(p0, center);
	*/
}

// Gets an ABBB in the space specified by the Matrix of a slice of the frustum
void frustumGetSliceAABB(const Frustum *w, const Mat4 dest_space, F32 znear, F32 zfar, Vec3 vMin, Vec3 vMax)
{
	Vec3 p0, p;
	Mat4 transform_mat;
	F32 scale = zfar / znear;
	F32 tanhalffovx = ftan(RAD(w->fovx * 0.5f));
	F32 tanhalffovy = ftan(RAD(w->fovy * 0.5f));
	F32 top = znear * tanhalffovy;
	F32 bottom = -top;
	F32 right = znear * tanhalffovx;
	F32 left = -right;

	mulMat4(dest_space,w->inv_viewmat,transform_mat);

	setVec3(p, left, top, -znear);
	mulVecMat4(p, transform_mat, p0);
	copyVec3(p0,vMin);
	copyVec3(p0,vMax);

	setVec3(p, right, bottom, -znear);
	mulVecMat4(p, transform_mat, p0);
	vec3RunningMin(p0,vMin);
	vec3RunningMax(p0,vMax);

	setVec3(p, right, top, -znear);
	mulVecMat4(p, transform_mat, p0);
	vec3RunningMin(p0,vMin);
	vec3RunningMax(p0,vMax);

	setVec3(p, left, bottom, -znear);
	mulVecMat4(p, transform_mat, p0);
	vec3RunningMin(p0,vMin);
	vec3RunningMax(p0,vMax);

	setVec3(p, right * scale, top * scale, -zfar);
	mulVecMat4(p, transform_mat, p0);
	vec3RunningMin(p0,vMin);
	vec3RunningMax(p0,vMax);

	setVec3(p, left * scale, bottom * scale, -zfar);
	mulVecMat4(p, transform_mat, p0);
	vec3RunningMin(p0,vMin);
	vec3RunningMax(p0,vMax);

	setVec3(p, left * scale, top * scale, -zfar);
	mulVecMat4(p, transform_mat, p0);
	vec3RunningMin(p0,vMin);
	vec3RunningMax(p0,vMax);

	setVec3(p, right * scale, bottom * scale, -zfar);
	mulVecMat4(p, transform_mat, p0);
	vec3RunningMin(p0,vMin);
	vec3RunningMax(p0,vMax);

}

void frustumGetBounds(const Frustum *w, Vec3 min, Vec3 max)
{
	Vec3 p1, p;
	F32 scale = w->zfar / w->znear;
	F32 tanhalffovx = ftan(RAD(w->fovx * 0.5f));
	F32 tanhalffovy = ftan(RAD(w->fovy * 0.5f));
	F32 top = w->znear * tanhalffovy;
	F32 bottom = -top;
	F32 right = w->znear * tanhalffovx;
	F32 left = -right;

	if (w->world_max[0] > -8e15)
	{
		copyVec3(w->world_min, min);
		copyVec3(w->world_max, max);
		return;
	}

	copyVec3(w->inv_viewmat[3], min);
	copyVec3(w->inv_viewmat[3], max);

	setVec3(p1, left, top, -w->znear);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);

	setVec3(p1, right, top, -w->znear);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);

	setVec3(p1, right, bottom, -w->znear);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);

	setVec3(p1, left, bottom, -w->znear);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);

	setVec3(p1, left * scale, top * scale, -w->zfar);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);

	setVec3(p1, right * scale, top * scale, -w->zfar);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);

	setVec3(p1, right * scale, bottom * scale, -w->zfar);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);

	setVec3(p1, left * scale, bottom * scale, -w->zfar);
	mulVecMat4(p1, w->inv_viewmat, p);
	vec3RunningMinMax(p, min, max);
}

void makeViewMatrix(const Mat4 camera_matrix, Mat4 viewmat, Mat4 inv_viewmat)
{
	Vec3 dv;
	Mat4 matx;
	Mat4 unitfix = 
	{
		-1, +0, +0,
		+0, +1, +0,
		+0, +0, +1,
		+0, +0, +0,
	};

	// fast-invert camera rotation to get view rotation
	copyMat4(camera_matrix, viewmat);
	transposeMat3(viewmat);

	// fixup handedness of coordinate system
	mulMat3(unitfix, viewmat, matx);
	copyMat3(matx, viewmat);

	// invert camera translation to get view translation
	negateVec3(viewmat[3], dv);
	mulVecMat3(dv, viewmat, viewmat[3]);

	// slow invert
	invertMat4Copy(viewmat, inv_viewmat);
}

#define MAX_CAMDIST_ORIGIN (1e6)
#define MAX_CAMDIST_ORIGIN_SQR (3*MAX_CAMDIST_ORIGIN*MAX_CAMDIST_ORIGIN)

#define CHECK_CAMPOS(vInput)	\
	devassertmsg(lengthVec3Squared((vInput)) < MAX_CAMDIST_ORIGIN_SQR, "Invalid Camera position.")

void frustumSetCameraMatrix(Frustum *f, const Mat4 camera_matrix)
{
	assert(FINITEVEC3(camera_matrix[0]));
	assert(FINITEVEC3(camera_matrix[1]));
	assert(FINITEVEC3(camera_matrix[2]));
	assert(FINITEVEC3(camera_matrix[3]));
	CHECK_CAMPOS(camera_matrix[3]);
	copyMat4(camera_matrix, f->cammat);
	makeViewMatrix(camera_matrix, f->viewmat, f->inv_viewmat);
}

int frustumCheckBoundingBoxNonInline(const Frustum *w, const Vec3 vmin, const Vec3 vmax, const Mat4 local_to_world_mat, bool bReturnFullClip)
{
	return frustumCheckBoundingBox(w, vmin, vmax, local_to_world_mat, bReturnFullClip);
}
