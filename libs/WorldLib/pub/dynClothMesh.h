#pragma once
GCC_SYSTEM

#include "stdtypes.h"

enum
{
	CLOTHMESH_TRISTRIP = 0,
	CLOTHMESH_TRILIST = 2
};

typedef struct DynClothStrip
{
	S32 Type; // 0=strip, 1=fan
	S32 NumIndices;
	S16 *IndicesCCW;		// allocated
	S32 MinIndex;
	S32 MaxIndex;
} DynClothStrip;

typedef struct DynClothMesh
{
	S32 Allocate;
	S32 NumStrips;
	DynClothStrip *Strips;	// allocated
	S32 NumPoints;
	Vec3 *Points;		// pointer to pool
	Vec3 *Normals;		// pointer to pool
	Vec3 *BiNormals;	// pointer to pool
	Vec3 *Tangents;		// pointer to pool
	Vec2 *TexCoords;	// pointer to pool
	Vec3 Color;
	F32  Alpha;
	// Implementation specific data
	//TextureOverride TextureData[2];
	U8   colors[4][4];
} DynClothMesh;

extern DynClothMesh *dynClothMeshCreate();
extern void dynClothMeshDelete(DynClothMesh *mesh);
extern void dynClothMeshPrimitiveCreateBox(DynClothMesh *mesh, F32 dx, F32 dy, F32 dz, int detail);
extern void dynClothMeshPrimitiveCreatePlane(DynClothMesh *mesh, F32 rad, int detail);
extern void dynClothMeshPrimitiveCreateBalloon(DynClothMesh *mesh, F32 rad, F32 hlen, int detail);
extern void dynClothMeshPrimitiveCreateCylinder(DynClothMesh *mesh, F32 rad, F32 hlen, int detail);
extern void dynClothMeshPrimitiveCreateSphere(DynClothMesh *mesh, F32 rad, int detail);
