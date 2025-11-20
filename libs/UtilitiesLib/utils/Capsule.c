#include "earray.h"
#include "linedist.h"
#include "Capsule.h"
#include "Quat.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Physics););

Capsule defaultCapsule = {	{0, 1.5, 0},
							{0, 1, 0},
							4.5, 
							1.5};

bool CapsuleCapsuleCollide(const Capsule *cap1, const Vec3 posOffset1, const Quat rotOffset1, Vec3 L1isect, const Capsule *cap2,  const Vec3 posOffset2, const Quat rotOffset2, Vec3 L2isect, F32 *distOut, F32 addRadius)
{
	Vec3 start1, start2, dir1, dir2;
	F32 capDist, distSquared;

	quatRotateVec3(rotOffset1, cap1->vStart, start1);
	addVec3(start1, posOffset1, start1);
	quatRotateVec3(rotOffset1, cap1->vDir, dir1);

	quatRotateVec3(rotOffset2, cap2->vStart, start2);
	addVec3(start2, posOffset2, start2);
	quatRotateVec3(rotOffset2, cap2->vDir, dir2);

	capDist = cap1->fRadius + cap2->fRadius + addRadius;
	distSquared = LineLineDistSquared(start1, dir1, cap1->fLength, L1isect, start2, dir2, cap2->fLength, L2isect);
	if (distOut)
	{
		*distOut = sqrt(distSquared);
	}
	if (distSquared < SQR(capDist))
	{
		return true;
	}
	return false;
}

bool CapsulePointCollide(const Capsule *cap1, const Vec3 posOffset1, const Quat rotOffset1, Vec3 L1isect, const Vec3 pos2, F32 *distOut)
{
	Vec3 start1, dir1;
	F32 capDist, distSquared;

	quatRotateVec3(rotOffset1, cap1->vStart, start1);
	addVec3(start1, posOffset1, start1);
	quatRotateVec3(rotOffset1, cap1->vDir, dir1);

	capDist = cap1->fRadius;
	distSquared = PointLineDistSquared(pos2, start1, dir1, cap1->fLength, L1isect);	
	if (distOut)
	{
		*distOut = sqrt(distSquared);
	}
	if (distSquared < SQR(capDist))
	{
		return true;
	}
	return false;
}

// Returns the point along the midline of the capsule the distance from its root (including cap) a given percentage of its height, oriented correctly
void CapsuleMidlinePoint(const Capsule *cap1, const Vec3 posOffset1, const Quat rotOffset1, F32 percent, Vec3 point)
{
	Vec3 start1, dir1;

	quatRotateVec3(rotOffset1, cap1->vStart, start1);
	addVec3(start1, posOffset1, start1);
	quatRotateVec3(rotOffset1, cap1->vDir, dir1);

	scaleVec3(dir1, ((2 * cap1->fRadius) + cap1->fLength) * percent - cap1->fRadius, dir1);

	addVec3(start1, dir1, point);
}

// Returns a bounding box for the capsule (for physx queries that won't take caps)
// Not at all optimized
void CapsuleGetBounds(const Capsule *cap, Vec3 minOut, Vec3 maxOut)
{
	int i;
	Vec3 tmp;
	copyVec3(cap->vStart, minOut);
	copyVec3(cap->vStart, maxOut);
	
	for(i=0; i<3; i++)
	{
		MIN1(minOut[i], cap->vStart[i]-cap->fRadius);
		MIN1(minOut[i], cap->vStart[i]+cap->fRadius);
		MAX1(maxOut[i], cap->vStart[i]-cap->fRadius);
		MAX1(maxOut[i], cap->vStart[i]+cap->fRadius);
	}

	scaleAddVec3(cap->vDir, cap->fLength, cap->vStart, tmp);
	for(i=0; i<3; i++)
	{
		MIN1(minOut[i], tmp[i]-cap->fRadius);
		MIN1(minOut[i], tmp[i]+cap->fRadius);
		MAX1(maxOut[i], tmp[i]-cap->fRadius);
		MAX1(maxOut[i], tmp[i]+cap->fRadius);
	}
}

void CapsuleGetWorldSpaceBounds(const Capsule *cap, const Vec3 vWorldPos, const Quat qRot, Vec3 vMinOut, Vec3 vMaxOut)
{
	Capsule tmpCap;

	tmpCap.fLength = cap->fLength;
	tmpCap.fRadius = cap->fRadius;
	quatRotateVec3(qRot, cap->vStart, tmpCap.vStart);
	addVec3(tmpCap.vStart, vWorldPos, tmpCap.vStart);
	quatRotateVec3(qRot, cap->vDir, tmpCap.vDir);

	CapsuleGetBounds(&tmpCap, vMinOut, vMaxOut);
}

// For now, use the first capsule for the source, and check against all capsules for dest
F32 CapsuleGetDistance(const Capsule*const* capsSource, const Vec3 posSourceIn, const Quat rotSource, const Capsule*const* capsTarget, const Vec3 posTargetIn, const Quat rotTarget, Vec3 sourceOut, Vec3 targetOut, int xzOnly, U32 capsuleType)
{
	Vec3 posSource;
	Vec3 posTarget;

	copyVec3(posSourceIn, posSource);
	copyVec3(posTargetIn, posTarget);

	// this should probably be implemented better when someone gets around to it :)
	if(xzOnly)
		posSource[1] = posTarget[1];

	if (capsSource && eaSize(&capsSource))
	{
		const Capsule *capSource = capsSource[0];
		if (capsTarget && eaSize(&capsTarget))
		{
			Vec3 minSourceOut = {0};
			Vec3 minTargetOut = {0};
			F32 minDist = 9e9;
			int i;
			for (i = 0; i < eaSize(&capsTarget); i++)
			{
				Vec3 tempSourceOut;
				Vec3 tempTargetOut;
				F32 tempDist;
				const Capsule *capTarget = capsTarget[i];

				if (capTarget->iType != capsuleType) continue;

				CapsuleCapsuleCollide(capSource, posSource, rotSource, tempSourceOut, capTarget, posTarget, rotTarget, tempTargetOut, &tempDist, 0);
				
				tempDist = tempDist - capSource->fRadius - capTarget->fRadius;
				if (tempDist <= 0)
				{
					if(sourceOut)
						copyVec3(tempSourceOut, sourceOut);
					if(targetOut)
						copyVec3(tempTargetOut, targetOut);
					return 0;
				}
				else if (tempDist < minDist)
				{
					minDist = tempDist;
					copyVec3(tempSourceOut, minSourceOut);
					copyVec3(tempTargetOut, minTargetOut);
				}
			}
			if (sourceOut)
			{
				copyVec3(minSourceOut, sourceOut);
			}
			if (targetOut)
			{
				copyVec3(minTargetOut, targetOut);
			}
			return minDist;
		}
		else
		{
			F32 minDist = FLT_MAX;
			int i;

			for(i=eaSize(&capsSource)-1; i>=0; i--)
			{
				F32 dist = 0;
				
				if (capsSource[i]->iType != capsuleType) continue;

				CapsulePointCollide(capsSource[i], posSource, rotSource, NULL, posTarget, &dist);
				dist -= capsSource[i]->fRadius;

				if(dist<=0)
				{
					if(sourceOut)
						copyVec3(posSource, sourceOut);
					if(targetOut)
						copyVec3(posTarget, targetOut);
					return 0;
				}

				if(dist<minDist)
				{
					minDist = dist;
					if(sourceOut)
						copyVec3(posSource, sourceOut);
					if(targetOut)
						copyVec3(posTarget, targetOut);
				}
			}
			return minDist;
		}
	}
	else if (capsTarget && eaSize(&capsTarget))
	{
		Vec3 minTargetOut = {0};
		F32 minDist = 9e9;			
		int i;
		for (i = 0; i < eaSize(&capsTarget); i++)
		{
			Vec3 tempTargetOut;
			F32 tempDist;
			const Capsule *capTarget = capsTarget[i];

			if (capTarget->iType != capsuleType) continue;

			CapsulePointCollide(capTarget, posTarget, rotTarget, tempTargetOut, posSource, &tempDist);

			tempDist = tempDist - capTarget->fRadius;
			if (tempDist <= 0)
			{
				copyVec3(tempTargetOut, minTargetOut);
				return 0;
			}
			else if (tempDist < minDist)
			{
				minDist = tempDist;
				copyVec3(tempTargetOut, minTargetOut);
			}
		}
		if (sourceOut)
		{
			copyVec3(posSource, sourceOut);
		}
		if (targetOut)
		{
			copyVec3(minTargetOut, targetOut);
		}
		return minDist;
	}
	else
	{
		if (sourceOut)
		{
			copyVec3(posSource, sourceOut);
		}
		if (targetOut)
		{
			copyVec3(posTarget, targetOut);
		}
		return distance3(posSource, posTarget);
	}
}

F32 CapsuleLineDistance(const Capsule*const* capsSource, const Vec3 posSourceIn, const Quat rotSourceIn, const Vec3 pointSource, const Vec3 pointDir, F32 length, F32 radius, Vec3 targetOut, U32 capsuleType)
{
	Capsule sourceCapsule;
	Vec3 nullVec = {0};
	F32 minDist = 9e9;

	if(capsSource && eaSize(&capsSource))
	{
		Vec3 minTargetOut = {0};
		int i;

		copyVec3(pointSource, sourceCapsule.vStart);
		copyVec3(pointDir, sourceCapsule.vDir);
		sourceCapsule.fLength = length;
		sourceCapsule.fRadius = radius;

		for (i = 0; i < eaSize(&capsSource); i++)
		{
			Vec3 tempTargetOut;
			F32 tempDist;
			const Capsule *capTarget = capsSource[i];

			if (capTarget->iType != capsuleType) continue;
			CapsuleCapsuleCollide(&sourceCapsule, nullVec, unitquat, NULL, capTarget, posSourceIn, rotSourceIn, tempTargetOut, &tempDist, 0);

			tempDist = tempDist - capTarget->fRadius;
			if (tempDist < minDist)
			{
				minDist = tempDist;
				copyVec3(tempTargetOut, minTargetOut);
			}
		}
		if (targetOut)
		{
			copyVec3(minTargetOut, targetOut);
		}
	}
	else
		minDist = sqrt(PointLineDistSquared(posSourceIn, pointSource, pointDir, length, targetOut));

	return minDist;
}

// Returns if the line intersects the source capsules, and the closest point is in collOut if so
int CapsuleLineCollision(const Capsule*const* caps, const Vec3 capsPos, const Quat capsRot, const Vec3 linePos, const Vec3 lineDir, F32 lineLen, Vec3 collOut)
{
	F32 fMinDist = FLT_MAX;
	int i;
	if(caps && (i=eaSize(&caps)))
	{
		Vec3 vLineEnd;
		
		scaleAddVec3(lineDir,lineLen,linePos,vLineEnd);

		for(i=i-1; i>=0; i--)
		{
			const Capsule* cap = caps[i];
			Vec3 vSphereStart, vSphereEnd, vCapDirRot,vHit;

			quatRotateVec3(capsRot, cap->vStart, vSphereStart);
			addVec3(vSphereStart, capsPos, vSphereStart);
			quatRotateVec3(capsRot, cap->vDir, vCapDirRot);
			scaleAddVec3(vCapDirRot,cap->fLength,vSphereStart,vSphereEnd);

			// Test against the spheres at the ends of the capsule
			if(sphereLineCollisionWithHitPoint(linePos,vLineEnd,vSphereStart,cap->fRadius,vHit))
			{
				F32 fDist;
				Vec3 vTemp;
				subVec3(vHit,linePos,vTemp);
				fDist = lengthVec3(vTemp);
				if(fDist<fMinDist)
				{
					fMinDist = fDist;
					if(collOut)
						copyVec3(vHit,collOut);
				}
			}

			if(sphereLineCollisionWithHitPoint(linePos,vLineEnd,vSphereEnd,cap->fRadius,vHit))
			{
				F32 fDist;
				Vec3 vTemp;
				subVec3(vHit,linePos,vTemp);
				fDist = lengthVec3(vTemp);
				if(fDist<fMinDist)
				{
					fMinDist = fDist;
					if(collOut)
						copyVec3(vHit,collOut);
				}
			}

			// Test against the cylinder
			if(cylinderLineCollisionWithHitPoint(linePos,lineDir,lineLen,vSphereStart,vCapDirRot,cap->fLength,cap->fRadius,vHit))
			{
				F32 fDist;
				Vec3 vTemp;
				subVec3(vHit,linePos,vTemp);
				fDist = lengthVec3(vTemp);
				if(fDist<fMinDist)
				{
					fMinDist = fDist;
					if(collOut)
						copyVec3(vHit,collOut);
				}
			}
		}
	}
	return fMinDist != FLT_MAX;
}

static __forceinline void calcBoxPoint(int i, int j, int k, Vec3 local_min, Vec3 local_max, Mat4 world_mat, Vec3 box_points[8])
{
	Vec3 temp2 = {i ? local_min[0] : local_max[0],
		j ? local_min[1] : local_max[1],
		k ? local_min[2] : local_max[2]};
	mulVecMat4(temp2, world_mat, box_points[(i << 2) + (j << 1) + k]);
}

int capsuleBoxCollision(Vec3 cap_start, Vec3 cap_dir, F32 length, F32 radius, Mat4 cap_world_mat,
						Vec3 local_min, Vec3 local_max, Mat4 world_mat, Mat4 inv_world_mat)
{
	int i;

	Vec3 box_points[8];
	Vec4 box_plane;
	Vec3 world_start, world_end;
	Vec3 temp;

	scaleAddVec3(cap_dir, length, cap_start, temp);
	mulVecMat4(cap_start, cap_world_mat, world_start);
	mulVecMat4(temp, cap_world_mat, world_end);

	// find transformed points and planes
	//   4-------0
	//  /|      /|
	// 5-------1 |
	// | 6-----|-2
	// |/      |/
	// 7-------3

	for(i=0; i<6; i++)
	{
		switch(i)
		{
		xcase 0:
			// 7
			calcBoxPoint(1, 1, 1, local_min, local_max, world_mat, box_points);
			// 6
			calcBoxPoint(1, 1, 0, local_min, local_max, world_mat, box_points);
			// 3
			calcBoxPoint(0, 1, 1, local_min, local_max, world_mat, box_points);
			makePlane(box_points[7], box_points[6], box_points[3], box_plane);
		xcase 1: 
			// 5
			calcBoxPoint(1, 0, 1, local_min, local_max, world_mat, box_points);
			makePlane(box_points[7], box_points[5], box_points[6], box_plane);
		xcase 2:
			makePlane(box_points[7], box_points[3], box_points[5], box_plane);
		xcase 3:
			// 0
			calcBoxPoint(0, 0, 0, local_min, local_max, world_mat, box_points);
			// 1
			calcBoxPoint(0, 0, 1, local_min, local_max, world_mat, box_points);
			makePlane(box_points[0], box_points[5], box_points[1], box_plane);
		xcase 4:
			// 2
			calcBoxPoint(0, 1, 0, local_min, local_max, world_mat, box_points);
			makePlane(box_points[0], box_points[2], box_points[6], box_plane);
		xcase 5:
			makePlane(box_points[0], box_points[1], box_points[3], box_plane);
		}

		// TODO need line check? :)
		if(distanceToPlaneNoEpsilon(world_start, box_plane)<-radius &&
			distanceToPlaneNoEpsilon(world_end, box_plane)<-radius)
		{
			return 0;
		}
	}

	return 1;
}

int capsuleSphereCollision(	Vec3 cap_start, Vec3 cap_dir, F32 length, F32 capsule_radius, Mat4 cap_world_mat,
						   Vec3 world_mid, F32 sphere_radius)
{
	Vec3 dummy;
	Vec3 world_start, world_dir;
	F32 dist;

	mulVecMat4(cap_start, cap_world_mat, world_start);
	mulVecMat3(cap_dir, cap_world_mat, world_dir);
	dist = PointLineDistSquared(world_mid, world_start, world_dir, length, dummy);

	return dist<SQR(sphere_radius+capsule_radius);
}


// -------------------------------------------------------------------------------------------------------------------
S32 CapsuleVsCylinder(	const Capsule *pcap,
						const Vec3 vCapPos, 
						const Quat qCapRot,	
						const Vec3 vCylinderSt,
						const Vec3 vCylinderDir,
						F32 fCylinderLength,
						F32 fCylinderRadius,
						Vec3 vHitPointOut)
{
	Vec3 vCapStart, vCapEnd, vCapDir;
	Vec3 vCylinderEnd;
	Vec3 vCylinderHitPt1, vCylinderHitPt2, vCapHitPt;
	Vec3 vCylinderLine0, vCylinderLine1;
	Vec3 vCylPtToCapPt, vCylinderPerpDir;
	F32 fDistSQR1, fDistSQR2;
	F32 f1, f2;

	quatRotateVec3(qCapRot, pcap->vStart, vCapStart);
	addVec3(vCapStart, vCapPos, vCapStart);
	quatRotateVec3(qCapRot, pcap->vDir, vCapDir);
	scaleAddVec3(vCapDir, pcap->fLength, vCapStart, vCapEnd);

	scaleAddVec3(vCylinderDir, fCylinderLength, vCylinderSt, vCylinderEnd);

	// first check the distance between the capsule and cylinder start/end points
	fDistSQR1 = LineSegLineSegDistSquared(vCapStart, vCapEnd, vCylinderSt, vCylinderEnd, &f1, &f2, vCapHitPt, vCylinderHitPt2);
	if (fDistSQR1 > SQR(fCylinderRadius + pcap->fRadius))
	{	// the distance between the line segments is too large, reject collision
		return false;
	}

	// compute a tangent segment based on the closest point on the cylinder to the sphere, 
	// then see how far that segment is from the capsule. 
	// 
	subVec3(vCapHitPt, vCylinderHitPt2, vCylPtToCapPt);

	perpedicularProjectionNormalizedAOntoVec3(vCylinderDir, vCylPtToCapPt, vCylinderPerpDir);
	normalVec3(vCylinderPerpDir);
	scaleAddVec3(vCylinderPerpDir, fCylinderRadius, vCylinderHitPt2, vCylinderLine0);
	scaleAddVec3(vCylinderPerpDir, -fCylinderRadius, vCylinderHitPt2, vCylinderLine1);

	fDistSQR1 = LineSegLineSegDistSquared(vCapStart, vCapEnd, vCylinderLine0, vCylinderLine1, &f1, &f2, vCapHitPt, vCylinderHitPt1);

	// follow up by taking a line segment on the cylinder from top to bottom on the closest edge of the clyinder to the capsule
	scaleAddVec3(vCylinderPerpDir, fCylinderRadius, vCylinderSt, vCylinderLine0);
	scaleAddVec3(vCylinderPerpDir, fCylinderRadius, vCylinderEnd, vCylinderLine1);

	fDistSQR2 = LineSegLineSegDistSquared(vCapStart, vCapEnd, vCylinderLine0, vCylinderLine1, &f1, &f2, vCapHitPt, vCylinderHitPt2);

	// get the lowest distance, and if that is within the radius of the capsule, we have a collision
	if (fDistSQR1 < fDistSQR2)
	{
		if (fDistSQR1 > SQR(pcap->fRadius))
			return false;

		if (vHitPointOut) 
			copyVec3(vCylinderHitPt1, vHitPointOut);
	}
	else
	{
		if (fDistSQR2 > SQR(pcap->fRadius))
			return false;

		if (vHitPointOut) 
			copyVec3(vCylinderHitPt2, vHitPointOut);
	}

	return true;
}

// -------------------------------------------------------------------------------------------------------------------
S32 CylinderVsPoint(const Vec3 vCylinderSt,
					const Vec3 vCylinderDir,
					F32 fCylinderLength,
					F32 fCylinderRadius,
					const Vec3 vPoint)
{
	Vec3 vCylStartToPoint;
	
	F32 fToCylDotCylDir;
	subVec3(vPoint, vCylinderSt, vCylStartToPoint);
	fToCylDotCylDir = dotVec3(vCylStartToPoint, vCylinderDir);
	if (fToCylDotCylDir < 0.f || fToCylDotCylDir > fCylinderLength)
	{	// point is outside of the cylinder's segment
		return false;
	}

	if (! vec3IsZeroXZ(vCylinderDir))
	{
		F32 fDistToLine = -(vCylinderDir[2] * vCylStartToPoint[0]) + (vCylinderDir[0] * vCylStartToPoint[2]);
		return ABS(fDistToLine) <= fCylinderRadius;
	}
	else
	{	// cylinder direction is directly up
		Vec3 vDiag = {0.707f, 0.f, 0.707f};
		F32 fDistToLine = (vDiag[0] * vCylStartToPoint[0]) + (vDiag[0] * vCylStartToPoint[2]);
		return ABS(fDistToLine) <= fCylinderRadius;
	}
	
	return 0;
}

#include "AutoGen/Capsule_h_ast.c"