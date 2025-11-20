#pragma once
GCC_SYSTEM

// Box collision code works, but has not proven useful
// and requires some additional memory, so disable for now
#define CLOTH_SUPPORT_BOX_COL 1
#include "DynCloth.h"

typedef enum CLOTH_COLTYPE CLOTH_COLTYPE;

struct DynClothCol
{
	CLOTH_COLTYPE Type;
	S16 MinSubLOD;
	S16 MaxSubLOD;
	Vec3 Center;
	Vec3 Norm;
#if CLOTH_SUPPORT_BOX_COL
	Vec3 XVec; // Box Only
	Vec3 YVec; // Box Only
#endif
	F32 PlaneD;
	F32 HLength; // Half length
	F32 Radius;  // X radius for Box
#if CLOTH_SUPPORT_BOX_COL
	F32 YRadius; // Box Only
#endif
	bool insideVolume;
	bool pushToSkinnedPos;
	// debug
	DynClothMesh *Mesh;
};



void dynClothColUpdate(DynClothCol *clothcol, Vec3 c, Vec3 n, F32 len, F32 rad, F32 frac);
	
void dynClothColSetSphere(DynClothCol *clothcol, Vec3 c, F32 rad, F32 frac);
void dynClothColSetPlane(DynClothCol *clothcol, Vec3 c, Vec3 dir, F32 frac);
void dynClothColSetCylinder(DynClothCol *clothcol, Vec3 p1, Vec3 p2, F32 rad, F32 exten1, F32 exten2, F32 frac);
void dynClothColSetBalloon(DynClothCol *clothcol, Vec3 p1, Vec3 p2, F32 rad, F32 exten1, F32 exten2, F32 frac);
#if CLOTH_SUPPORT_BOX_COL
void dynClothColSetBox(DynClothCol *clothcol, Vec3 p1, Vec3 norm, Vec3 xvec, Vec3 yvec, F32 height, F32 xrad, F32 yrad, F32 frac);
#endif

void dynClothColSetSubLOD(DynClothCol *clothcol, int min, int max);
F32 dynClothColSphereBalloonCol(DynClothCol *clothcol, Vec3 p, F32 rad);
F32 dynClothColSphereSphereCol(DynClothCol *clothcol, Vec3 p, F32 rad);
F32 dynClothColSphereCylinderCol(DynClothCol *clothcol, Vec3 p, F32 rad);
F32 dynClothColSpherePlaneCol(DynClothCol *clothcol, Vec3 p, F32 rad);
#if CLOTH_SUPPORT_BOX_COL
F32 dynClothColSphereBoxCol(DynClothCol *clothcol, Vec3 p, F32 rad);
#endif

F32 dynClothColCollideSphere(DynClothCol *clothcol, Vec3 p, F32 rad);

void dynClothColGetMatrix(DynClothCol *clothcol, Mat4 mat);

// Debug
void dynClothColCreateMesh(DynClothCol *clothcol, int detail);





