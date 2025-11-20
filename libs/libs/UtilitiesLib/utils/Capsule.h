
#pragma once
GCC_SYSTEM


// Capsule data type, defined this way because the fast functions use this format
AUTO_STRUCT;
typedef struct Capsule
{
	Vec3 vStart;
	Vec3 vDir;
	F32 fLength;
	F32 fRadius;
	U32 iType; // Opaque type, is used to restrict certain capsules
} Capsule;

AUTO_STRUCT AST_CONTAINER;
typedef struct SavedCapsule
{
	const Vec3 vStart; AST(PERSIST NAME(Start))
	const Vec3 vDir; AST(PERSIST NAME(Dir))
	const F32 fLength; AST(PERSIST NAME(Length))
	const F32 fRadius; AST(PERSIST NAME(Radius))
	const U32 iType; AST(PERSIST NAME(Type))
} SavedCapsule;

extern Capsule defaultCapsule;

extern ParseTable parse_Capsule[];
#define TYPE_parse_Capsule Capsule

// See if two capsules collide, and if they do set intersection points
// Intersection points are on the line, not the outside of the capsule
// PosOffset and RotOffset will adjust the capsules accordingly
bool CapsuleCapsuleCollide(const Capsule *cap1, const Vec3 posOffset1, const Quat rotOffset1, Vec3 L1isect, const Capsule *cap2,  const Vec3 posOffset2, const Quat rotOffset2, Vec3 L2isect, F32 *distOut, F32 addedCapsuleRadius);

// See if a point is within the capsule
bool CapsulePointCollide(const Capsule *cap1, const Vec3 posOffset1, const Quat rotOffset1, Vec3 L1isect, const Vec3 pos2, F32 *distOut);

// Returns the point along the midline of the capsule the distance from its root (including cap) a given percentage of its height, oriented correctly
void CapsuleMidlinePoint(const Capsule *cap1, const Vec3 posOffset1, const Quat rotOffset1, F32 percent, Vec3 point);

// Returns the distance between two multi capsule entities, currently uses the first source
// capsule against all the target ones
F32 CapsuleGetDistance(const Capsule*const* capsSource, const Vec3 posSource, const Quat rotSource, const Capsule*const* capsTarget, const Vec3 posTarget, const Quat rotTarget, Vec3 sourceOut, Vec3 targetOut, int xzOnly, U32 capsuleType);

// Returns the minimum distance from all source capsules to the line
F32 CapsuleLineDistance(const Capsule*const* capsSource, const Vec3 posSourceIn, const Quat rotSourceIn, const Vec3 pointSource, const Vec3 pointDir, F32 length, F32 radius, Vec3 targetOut, U32 capsuleType);

// Returns if the line intersects the source capsules, and the closest point is in collOut if so
int CapsuleLineCollision(const Capsule*const* caps, const Vec3 capsPos, const Quat capsRot, const Vec3 linePos, const Vec3 lineDir, F32 lineLen, Vec3 collOut);

int capsuleBoxCollision(Vec3 cap_start, Vec3 cap_dir, F32 length, F32 radius, Mat4 cap_world_mat, 
						Vec3 local_min, Vec3 local_max, Mat4 world_mat, Mat4 inv_world_mat);

int capsuleSphereCollision(	Vec3 cap_start, Vec3 cap_dir, F32 length, F32 capsule_radius, Mat4 cap_world_mat, 
							Vec3 world_mid, F32 sphere_radius);

// Gets bounding box of the capsule - not optimized
void CapsuleGetBounds(const Capsule *cap, Vec3 minOut, Vec3 maxOut);

void CapsuleGetWorldSpaceBounds(const Capsule *cap, const Vec3 vWorldPos, const Quat qRot, Vec3 vMinOut, Vec3 vMaxOut);


S32 CapsuleVsCylinder(	SA_PARAM_NN_VALID const Capsule *pcap,
						SA_PARAM_NN_VALID const Vec3 vCapPos, 
						SA_PARAM_NN_VALID const Quat qCapRot,	
						SA_PARAM_NN_VALID const Vec3 vCylinderSt,
						SA_PARAM_NN_VALID const Vec3 vCylinderDir,
						F32 fCylinderLength,
						F32 fCylinderRadius,
						SA_PARAM_OP_VALID Vec3 vHitPoint);

S32 CylinderVsPoint(SA_PARAM_NN_VALID const Vec3 vCylinderSt,
					SA_PARAM_NN_VALID const Vec3 vCylinderDir,
					F32 fCylinderLength,
					F32 fCylinderRadius,
					SA_PARAM_NN_VALID const Vec3 vPoint);