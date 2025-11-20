#include <memory.h>
#include "DynCloth.h"
#include "dynClothMesh.h"
#include "dynClothPrivate.h"
#include "mathutil.h"
#include "wlModel.h"
#include "wlModelInline.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

//////////////////////////////////////////////////////////////////////////////
// dynClothCreateMeshIndices() builds a list of vertex indices describing a
//   Triangle List or Mesh.
// NOTE: DynClothMesh->TextureData points to game specific texture information
//   This data is neither allocated nor destroyed by this code.

#define TRIANGLE_LISTS 1

// Creates mesh index data
DynClothMesh *dynClothCreateMeshIndices(DynCloth *cloth, Model *model, int lod) {
	ModelLOD *modelLOD = modelLODLoadAndMaybeWait(model, lod, true);

	if(modelLOD) {
		DynClothMesh *mesh = dynClothMeshCreate();
		int nump = dynClothNumRenderedParticles(&cloth->commonData);
		int numidx = modelLOD->tri_count * 3;
		int idx;
		dynClothMeshSetPoints(mesh, nump, cloth->renderData.RenderPos, cloth->renderData.Normals, cloth->renderData.TexCoords, cloth->renderData.BiNormals, cloth->renderData.Tangents);
		dynClothMeshCreateStrips(mesh, 1, CLOTHMESH_TRILIST);
		dynClothStripCreateIndices(&mesh->Strips[0], numidx, -1);

		modelLockUnpacked(modelLOD);
		for(idx = 0; idx < numidx; idx++) {
			const U32 *tris = modelGetTris(modelLOD);
			mesh->Strips[0].IndicesCCW[idx] = tris[idx];
		}
		modelUnlockUnpacked(modelLOD);

		dynClothMeshCalcMinMax(mesh);

		return mesh;
	} else {
		return NULL;
	}
}

//////////////////////////////////////////////////////////////////////////////

void dynClothStripCreateIndices(DynClothStrip *strip, int num, int newtype)
{
	if (newtype >= 0)
		strip->Type = newtype;
	strip->NumIndices = num;
	strip->IndicesCCW = CLOTH_MALLOC(S16, num);
}

//////////////////////////////////////////////////////////////////////////////

DynClothMesh *dynClothMeshCreate()
{
	DynClothMesh *mesh = CLOTH_MALLOC(DynClothMesh, 1);
	memset(mesh, 0, sizeof(*mesh));

	// Rendering
	setVec3(mesh->Color,1.f,1.f,1.f);
	mesh->Alpha = 1.0f;

	return mesh;
}

void dynClothMeshDelete(DynClothMesh *mesh)
{
	int i;
	for (i=0; i<mesh->NumStrips; i++)
	{
		DynClothStrip *strip = mesh->Strips + i;
		CLOTH_FREE(strip->IndicesCCW);
	}
	CLOTH_FREE(mesh->Strips);
	if (mesh->Allocate)
	{
		CLOTH_FREE(mesh->Points);
		CLOTH_FREE(mesh->Normals);
		CLOTH_FREE(mesh->BiNormals);
		CLOTH_FREE(mesh->Tangents);
		CLOTH_FREE(mesh->TexCoords);
	}
	CLOTH_FREE(mesh);
}

void dynClothMeshAllocate(DynClothMesh *mesh, int npoints)
{
	mesh->NumPoints = npoints;
	mesh->Allocate = 1;
	mesh->Points = CLOTH_MALLOC(Vec3, npoints);
	mesh->Normals = CLOTH_MALLOC(Vec3, npoints);
	mesh->BiNormals = CLOTH_MALLOC(Vec3, npoints);
	mesh->Tangents = CLOTH_MALLOC(Vec3, npoints);
	mesh->TexCoords = CLOTH_MALLOC(Vec2, npoints);
	assert(mesh->Points);
	assert(mesh->Normals);
	assert(mesh->BiNormals);
	assert(mesh->Tangents);
	assert(mesh->TexCoords);
}

//////////////////////////////////////////////////////////////////////////////

void dynClothMeshSetColorAlpha(DynClothMesh *mesh, Vec3 c, F32 a)
{
	if (c)
		copyVec3(c, mesh->Color);
	if (a >= 0.0f)
		mesh->Alpha = a;
}

void dynClothMeshSetPoints(DynClothMesh *mesh, int npoints, Vec3 *pts, Vec3 *norms, Vec2 *tcoords, Vec3 *binorms, Vec3 *tangents)
{
	mesh->NumPoints = npoints;
	mesh->Points = pts;
	if (norms)
		mesh->Normals = norms;
	if (tcoords)
		mesh->TexCoords = tcoords;
	if (binorms)
		mesh->BiNormals = binorms;
	if (tangents)
		mesh->Tangents = tangents;
}

void dynClothMeshCreateStrips(DynClothMesh *mesh, int num, int type)
{
	int i;
	mesh->NumStrips = num;
	mesh->Strips = CLOTH_MALLOC(DynClothStrip, num);
	memset(mesh->Strips, 0, num*sizeof(DynClothStrip));
	for (i=0; i<num; i++)
		mesh->Strips[i].Type = type;
}

void dynClothMeshCalcMinMax(DynClothMesh *mesh)
{
	int i,j;
	DynClothStrip *Strips = mesh->Strips;
	for (i=0; i<mesh->NumStrips; i++)
	{
		Strips[i].MinIndex = Strips[i].IndicesCCW[0];
		Strips[i].MaxIndex = Strips[i].IndicesCCW[0];
		for (j=1; j<Strips[i].NumIndices; j++)
		{
			if (Strips[i].MinIndex > Strips[i].IndicesCCW[j])
				Strips[i].MinIndex = Strips[i].IndicesCCW[j];
			if (Strips[i].MaxIndex < Strips[i].IndicesCCW[j])
				Strips[i].MaxIndex = Strips[i].IndicesCCW[j];
		}
	}
}

////////////////////////////////////////





