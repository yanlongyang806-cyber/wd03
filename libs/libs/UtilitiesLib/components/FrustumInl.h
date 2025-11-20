#ifndef _FRUSTUMINL_H_
#define _FRUSTUMINL_H_

#include "Vec4H.h"

FRUSTUM_INLINE int frustumCheckSphere(const Frustum *w, const Vec3 pos_cameraspace, F32 rad)
{
	F32			cx,cy,cz,dist,hplane,vplane;
	int			clip = FRUSTUM_CLIP_NONE;
#if _XBOX
	F32			clipFail;
	F32			clipNear, /* clipFar, */ clipLeft, clipRight, clipTop, clipBottom;
#endif

	if (w->use_hull)
		return hullIsSphereInside(&w->hull, pos_cameraspace, rad);

	if (w->use_sphere)
	{
		F32 dist_sqrd = lengthVec3Squared(pos_cameraspace);
		F32 effective_radius = w->sphere_radius + rad;
		if (dist_sqrd > SQR(effective_radius))
			return 0;
		effective_radius = w->sphere_radius - rad;
		if (effective_radius < 0)
			return FRUSTUM_CLIP_SPHERE_PARTIAL;
		return FloatBranch(dist_sqrd, SQR(effective_radius), FRUSTUM_CLIP_SPHERE_PARTIAL, FRUSTUM_CLIP_NONE);
	}

#if _XBOX
	// near plane
	cz = -pos_cameraspace[2];
	clipFail = FloatBranchFGE(-w->znear, cz+rad, 1, 0);
	clipNear = FloatBranchFL(cz-rad, -w->znear, FRUSTUM_CLIP_NEAR, 0);
	//if ((cz+rad) <= -w->znear )	 	/* behind camera		*/
	//	return 0;
	//else if (cz-rad < -w->znear)
	//	clip |= FRUSTUM_CLIP_NEAR;

	// far plane
	clipFail = FloatBranchFGE(cz-rad, -w->zfar, 1, clipFail);
	//if ((cz-rad) >= -w->zfar )	 	/* too far		*/
	//	return 0;

	hplane = cz * w->hvam;
	cx = pos_cameraspace[0];
	// Left edge
	dist = (cx - (-hplane)) * w->hcos;
	clipFail = FloatBranchFGE(-dist, rad, 1, clipFail);
	clipLeft = FloatBranchFG(rad, dist, FRUSTUM_CLIP_LEFT, 0);
	//if (-dist >= rad)					/* Left of view volume */
	//	return 0;
	//else if (rad > dist)
	//	clip |= FRUSTUM_CLIP_LEFT;

	// Right edge
	dist = (hplane - cx) * w->hcos;
	clipFail = FloatBranchFGE(-dist, rad, 1, clipFail);
	clipRight = FloatBranchFG(rad, dist, FRUSTUM_CLIP_RIGHT, 0);
	//if (-dist >= rad)
	//	return 0;
	//else if (rad > dist)
	//	clip |= FRUSTUM_CLIP_RIGHT;

	vplane = cz * w->vvam;
	cy = pos_cameraspace[1];
	// Bottom edge
	dist = (cy - (-vplane)) * w->vcos;
	clipFail = FloatBranchFGE(-dist, rad, 1, clipFail);
	clipBottom = FloatBranchFG(rad, dist, FRUSTUM_CLIP_BOTTOM, 0);
	//if (-dist >= rad)
	//	return 0;
	//else if (rad > dist)
	//	clip |= FRUSTUM_CLIP_BOTTOM;

	// top edge
	dist = (vplane - cy) * w->vcos;
	clipFail = FloatBranchFGE(-dist, rad, 1, clipFail);
	clipTop = FloatBranchFG(rad, dist, FRUSTUM_CLIP_TOP, 0);
	// clip = clipNear | clipFar | clipLeft | clipRight | clipTop | clipBottom;
	// clip = clip ? clip : FRUSTUM_CLIP_NONE;
	// clip = clipFail ? 0 : clip;
	clipNear = ( clipNear /* + clipFar */ ) + ( clipLeft + clipRight ) + ( clipTop + clipBottom );
	clipNear = FloatBranchFGEZ( -clipNear, FRUSTUM_CLIP_NONE, clipNear );
	clip = (int)FloatBranchFGEZ(-clipFail, clipNear, 0 );
	//if (-dist >= rad)
	//	return 0;
	//else if (rad > dist)
	//	clip |= FRUSTUM_CLIP_TOP;
#else
	// near plane
	cz = -pos_cameraspace[2];
	if ((cz+rad) <= -w->znear )	 	/* behind camera		*/
		return 0;
	else if (cz-rad < -w->znear)
		clip |= FRUSTUM_CLIP_NEAR;

	// far plane
	if ((cz-rad) >= -w->zfar )	 	/* too far		*/
		return 0;

	hplane = cz * w->hvam;
	cx = pos_cameraspace[0];
	// Left edge
	dist = (cx - (-hplane)) * w->hcos;
	if (-dist >= rad)					/* Left of view volume */
		return 0;
	else if (rad > dist)
		clip |= FRUSTUM_CLIP_LEFT;

	// Right edge
	dist = (hplane - cx) * w->hcos;
	if (-dist >= rad)
		return 0;
	else if (rad > dist)
		clip |= FRUSTUM_CLIP_RIGHT;

	vplane = cz * w->vvam;
	cy = pos_cameraspace[1];
	// Bottom edge
	dist = (cy - (-vplane)) * w->vcos;
	if (-dist >= rad)
		return 0;
	else if (rad > dist)
		clip |= FRUSTUM_CLIP_BOTTOM;

	// top edge
	dist = (vplane - cy) * w->vcos;
	if (-dist >= rad)
		return 0;
	else if (rad > dist)
		clip |= FRUSTUM_CLIP_TOP;
#endif

	return clip;
}

FRUSTUM_INLINE int frustumCheckSphereWorld(const Frustum *w, const Vec3 pos_worldspace, F32 rad)
{
	if (w->use_sphere)
	{
		F32 dist_sqrd = distance3Squared(w->cammat[3], pos_worldspace);
		F32 effective_radius = w->sphere_radius + rad;
		if (dist_sqrd > SQR(effective_radius))
			return 0;
		effective_radius = w->sphere_radius - rad;
		if (effective_radius < 0)
			return FRUSTUM_CLIP_SPHERE_PARTIAL;
		return FloatBranch(dist_sqrd, SQR(effective_radius), FRUSTUM_CLIP_SPHERE_PARTIAL, FRUSTUM_CLIP_NONE);
	}
	else
	{
		Vec3 pos_cameraspace;
		mulVecMat4(pos_worldspace, w->viewmat, pos_cameraspace);
		return frustumCheckSphere(w, pos_cameraspace, rad);
	}
}

FRUSTUM_INLINE int frustumCheckSphereWorldFast(const Frustum *w, Mat44H * pViewMat,const Vec3 pos_worldspace, F32 rad)
{
	if (w->use_sphere)
	{
		F32 dist_sqrd = distance3Squared(w->cammat[3], pos_worldspace);
		F32 effective_radius = w->sphere_radius + rad;
		if (dist_sqrd > SQR(effective_radius))
			return 0;
		effective_radius = w->sphere_radius - rad;
		if (effective_radius < 0)
			return FRUSTUM_CLIP_SPHERE_PARTIAL;
		return FloatBranch(dist_sqrd, SQR(effective_radius), FRUSTUM_CLIP_SPHERE_PARTIAL, FRUSTUM_CLIP_NONE);
	}
	else
	{
		Vec4H pos_cameraspace;
		pos_cameraspace = mulVec3Mat44H(pos_worldspace, pViewMat);
		return frustumCheckSphere(w, Vec4HToVec4(pos_cameraspace), rad);
	}
}

FRUSTUM_INLINE int frustumCheckPoint(const Frustum *w, const Vec3 pos_cameraspace)
{
	F32			cx,cy,cz,dist,hplane,vplane;
	int			clip = 0;
#if _XBOX
	F32 clipNear, clipFar, clipLeft, clipRight, clipTop, clipBottom;
#endif

	if (w->use_hull)
		return hullIsPointInside(&w->hull, pos_cameraspace) ? 0 : FRUSTUM_CLIP_NEAR;

	if (w->use_sphere)
		return FloatBranch(lengthVec3Squared(pos_cameraspace), w->sphere_radius_sqrd, 0, FRUSTUM_CLIP_NONE);

#if _XBOX
	cz = -pos_cameraspace[2];
	clipNear = FloatBranchFLE(cz, -w->znear, FRUSTUM_CLIP_NEAR, 0);
	//if ( cz <= -w->znear )
	//	clip |= FRUSTUM_CLIP_NEAR;

	clipFar = FloatBranchFGE(cz, -w->zfar, FRUSTUM_CLIP_FAR, 0);
	//if ( cz >= -w->zfar )
	//	clip |= FRUSTUM_CLIP_FAR;

	hplane = cz * w->hvam;
	cx = pos_cameraspace[0];
	dist = (cx - (-hplane)) * w->hcos;
	clipLeft = FloatBranchFGEZ(dist, 0, FRUSTUM_CLIP_LEFT);
	//if (dist < 0)	
	//	clip |= FRUSTUM_CLIP_LEFT;

	dist = (hplane - cx) * w->hcos;
	clipRight = FloatBranchFGEZ(dist, 0, FRUSTUM_CLIP_RIGHT);
	//if (dist < 0)
	//	clip |= FRUSTUM_CLIP_RIGHT;

	vplane = cz * w->vvam;
	cy = pos_cameraspace[1];
	dist = (cy - (-vplane)) * w->vcos;
	clipBottom = FloatBranchFGEZ(dist, 0, FRUSTUM_CLIP_BOTTOM);
	//if (dist< 0)
	//	clip |= FRUSTUM_CLIP_BOTTOM;

	dist = (vplane - cy) * w->vcos;
	clipTop = FloatBranchFGEZ(dist, 0, FRUSTUM_CLIP_TOP);
	//if (dist < 0)
	//	clip |= FRUSTUM_CLIP_TOP;

	clip = (int)( ( clipNear + clipFar ) + ( clipLeft + clipRight ) + ( clipTop + clipBottom ) );
#else
	cz = -pos_cameraspace[2];
	if ( cz <= -w->znear )
		clip |= FRUSTUM_CLIP_NEAR;

	if ( cz >= -w->zfar )
		clip |= FRUSTUM_CLIP_FAR;

	hplane = cz * w->hvam;
	cx = pos_cameraspace[0];
	dist = (cx - (-hplane)) * w->hcos;
	if (dist < 0)	
		clip |= FRUSTUM_CLIP_LEFT;

	dist = (hplane - cx) * w->hcos;
	if (dist < 0)
		clip |= FRUSTUM_CLIP_RIGHT;

	vplane = cz * w->vvam;
	cy = pos_cameraspace[1];
	dist = (cy - (-vplane)) * w->vcos;
	if (dist< 0)
		clip |= FRUSTUM_CLIP_BOTTOM;

	dist = (vplane - cy) * w->vcos;
	if (dist < 0)
		clip |= FRUSTUM_CLIP_TOP;
#endif

	return clip;
}

FRUSTUM_INLINE int frustumCheckBoxNearClipped(const Frustum *w, const Vec4 bounds_cameraspace[8])
{
	int		idx;
	int		clipped = 0;

	if (w->use_sphere || w->use_hull)
		return 0;

	for (idx=0;idx<8;idx++)
	{
		if (-bounds_cameraspace[idx][2] <= -w->znear)
			clipped++;
	}

	if (clipped == 8)
		return 2;
	if (clipped)
		return 1;
	return 0;
}

// This is not an optimal implementation.  This function checks every point against every plane, in the worst case.  It is possible to test only one point
// against each plane and get the same result, in theory.  Also, like most OBB versus frustum systems, this returns false positives. [RMARR - 9/27/13]
FRUSTUM_INLINE int frustumCheckBoxWorld(const Frustum *w, int clip, const Vec3 min, const Vec3 max, const Mat4 local_to_world_mat, bool bReturnFullClip)
{
	int		x, y, z;
	int		fullClip = 0;
	Vec3	extents[2];
	Mat4	local_to_camera_mat;

	if (w->use_sphere)
	{
		Vec3 sphere_mid_localspace;
		if (local_to_world_mat)
		{
			Mat4 world_to_local_mat;
			invertMat4Copy(local_to_world_mat, world_to_local_mat);
			mulVecMat4(w->cammat[3], world_to_local_mat, sphere_mid_localspace);
		}
		else
		{
			copyVec3(w->cammat[3], sphere_mid_localspace);
		}
		return boxSphereCollision(min, max, sphere_mid_localspace, w->sphere_radius); //this might need to be setup for bReturnFullClip
	}

	if (local_to_world_mat)
		mulMat4Inline(w->viewmat, local_to_world_mat, local_to_camera_mat);
	else
		copyMat4(w->viewmat, local_to_camera_mat);

	copyVec3(min,extents[0]);
	copyVec3(max,extents[1]);

	for(x=0;x<2;x++)
	{
		for(y=0;y<2;y++)
		{
			for(z=0;z<2;z++)
			{
				Vec3	pos;
				Vec3	tpos;
				int		pointClip;

				pos[0] = extents[x][0];
				pos[1] = extents[y][1];
				pos[2] = extents[z][2];
				mulVecMat4(pos,local_to_camera_mat,tpos);
				pointClip = frustumCheckPoint(w, tpos);

				clip     &= pointClip;
				fullClip |= pointClip;

				if (!bReturnFullClip && !clip)
					return 1; // We have at least one point on the inside of every frustum plane
			}
		}
	}

	if (bReturnFullClip && !clip)
		return fullClip ? fullClip : FRUSTUM_CLIP_NONE;
	return 0;
}

FRUSTUM_INLINE int frustumCheckBoundingBox(const Frustum *w, const Vec3 vmin, const Vec3 vmax, const Mat4 local_to_world_mat, bool bReturnFullClip)
{
	Vec3 vmid;
	int boxClip;
	int sphereClip;
	F32 rad;

	subVec3(vmax, vmin, vmid);
	rad = lengthVec3(vmid);
	scaleAddVec3(vmid, 0.5f, vmin, vmid);

	if (local_to_world_mat)
	{
		Vec3 v;
		mulVecMat4(vmid, local_to_world_mat, v);
		copyVec3(v, vmid);
	}

	if (!(sphereClip = frustumCheckSphereWorld(w, vmid, rad)))
		return 0;

	if (sphereClip == FRUSTUM_CLIP_NONE)
		return bReturnFullClip ? FRUSTUM_CLIP_NONE : 1;

	if (!(boxClip = frustumCheckBoxWorld(w, sphereClip, vmin, vmax, local_to_world_mat, bReturnFullClip)))
		return 0;

	return bReturnFullClip ? boxClip : 1;
}

FRUSTUM_INLINE int frustumCheckPointWorld(const Frustum *w, const Vec3 pos_worldspace)
{
	Vec3 pos_cameraspace;
	mulVecMat4(pos_worldspace, w->viewmat, pos_cameraspace);
	return frustumCheckPoint(w, pos_cameraspace);
}

FRUSTUM_FORCE_INLINE void frustumResetBounds(Frustum *w)
{
	setVec3(w->world_min, FLT_MAX, FLT_MAX, FLT_MAX);
	setVec3(w->world_max, -FLT_MAX, -FLT_MAX, -FLT_MAX);
}

FRUSTUM_FORCE_INLINE void frustumUpdateBounds(Frustum *w, const Vec3 world_min, const Vec3 world_max)
{
	vec3RunningMin(world_min, w->world_min);
	vec3RunningMax(world_max, w->world_max);
}

FRUSTUM_INLINE bool frustumCheckBoxNearClippedInView(const Frustum *w, const Vec3 vmin, const Vec3 vmax, const Mat4 tocameramat)
{
	//Mat4 ident;
	Vec3 vMin,vMax;
	bool bResult;
	//identityMat4(ident);

	vMin[0] = w->znear*w->hvam;
	vMax[0] = -w->znear*w->hvam;
	vMin[1] = w->znear*w->vvam;
	vMax[1] = -w->znear*w->vvam;
	vMin[2] = w->znear;
	vMax[2] = w->znear;

	bResult = orientBoxRectCollision(vmin,vmax,tocameramat,vMin,vMax);

	//assert (orientBoxBoxCollision(vmin,vmax,tocameramat,vMin,vMax,ident) == bResult);

	return bResult;
}

#endif
