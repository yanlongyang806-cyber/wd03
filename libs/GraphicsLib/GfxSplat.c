#include "GfxSplat.h"
#include "GfxGeo.h"
#include "GfxModelCache.h"
#include "GfxMaterials.h"
#include "GraphicsLib.h"
#include "wlModelInline.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "dynNodeInline.h"
#include "Materials.h"
#include "Color.h"
#include "partition_enums.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#define SPLAT_MAX_TRIANGLES (32*1024)
#define SPLAT_MAX_TRIANGLES_HARDLIMIT (512*1024)

typedef struct GfxSplatSetup
{
	GfxSplat * splat;
	int splat_flags;

	Mat44 splat_texture_space;
	Mat4 inv_world_space;
	F32 tex_tolerance;

	Vec3 * vertices;
	Vec2 * tex_coords;
	Vec3 * normals;
	int vertex_count;
	int vertex_alloc;
	bool shader_tex_project;
} GfxSplatSetup;

__forceinline static int texPolyFacesAway(const Vec2 tex_coords[3])
{
	Vec2 u, v;
	subVec2(tex_coords[1], tex_coords[0], u);
	subVec2(tex_coords[2], tex_coords[0], v);
	return crossVec2(u, v) > 0.0f;
}

void handleTriangles(void* userPointer, Vec3 (*tris)[3], U32 triCount)
{
	GfxSplatSetup * splat_setup = (GfxSplatSetup*)userPointer;
	Vec3 * vertex;
	Vec2 * tex_coords;
	Vec3 * normals;
	F32 min_tex = -splat_setup->tex_tolerance, max_tex = 1.0f + splat_setup->tex_tolerance;
	int vertex_count = splat_setup->vertex_count;
	int splat_flags = splat_setup->splat_flags;

	int tex_coord_count = splat_setup->vertex_count;
	int tex_coord_alloc = splat_setup->vertex_alloc;

	int normal_count = splat_setup->vertex_count;
	int normal_alloc = splat_setup->vertex_alloc;

	if(splat_setup->vertex_count > SPLAT_MAX_TRIANGLES_HARDLIMIT * 3)
		return;

	dynArrayAddStructs(splat_setup->vertices, splat_setup->vertex_count, splat_setup->vertex_alloc, triCount*3);
	
	if (!splat_setup->shader_tex_project)
		dynArrayAddStructs(splat_setup->tex_coords, tex_coord_count, tex_coord_alloc, triCount*3);

	dynArrayAddStructs(splat_setup->normals, normal_count, normal_alloc, triCount*3);

	vertex = splat_setup->vertices + vertex_count;
	
	if (!splat_setup->shader_tex_project)
		tex_coords = splat_setup->tex_coords + vertex_count;
	else
		tex_coords = 0;

	normals = splat_setup->normals + vertex_count;

	vertex_count = splat_setup->vertex_count;


	for (; triCount; --triCount, ++tris)
	{
		int v;
		FloatBranchCount clip_ns = 0;
		FloatBranchCount clip_ps = 0;
		FloatBranchCount clip_nt = 0;
		FloatBranchCount clip_pt = 0;
		Vec2 tri_tex_coords[3];

		for (v = 0; v < 3; ++v)
		{
			Vec3 tmpVec;
			if (splat_flags & GFX_SPLAT_RELATIVE_COORDS)
				mulVecMat4(tris[0][v], splat_setup->inv_world_space, vertex[v]);
			else
				copyVec3(tris[0][v], vertex[v]);

			mulVec3ProjMat44(tris[0][v], splat_setup->splat_texture_space, tmpVec);
			copyVec2(tmpVec, tri_tex_coords[v]);

			if (splat_flags & GFX_SPLAT_UNIT_TEXCOORD)
			{
				tri_tex_coords[v][0] = tri_tex_coords[v][0] * 0.5f + 0.5f;
				tri_tex_coords[v][1] = tri_tex_coords[v][1] * 0.5f + 0.5f;
			}


			clip_ns += FloatBranch(tri_tex_coords[v][0], min_tex, 0, 1);
			clip_ps += FloatBranch(max_tex, tri_tex_coords[v][0], 0, 1);

			clip_nt += FloatBranch(tri_tex_coords[v][1], min_tex, 0, 1);
			clip_pt += FloatBranch(max_tex, tri_tex_coords[v][1], 0, 1);
		}

		if ((splat_flags & GFX_SPLAT_CULL_UNIT_TEX) &&
			FloatBranch(clip_ns, 3, 1, 0) + FloatBranch(clip_ps, 3, 1, 0) +
			FloatBranch(clip_nt, 3, 1, 0) + FloatBranch(clip_pt, 3, 1, 0))
		{
			// out-of-range, stop processing this triangle & don't append it
			// to the mesh
			vertex_count -= 3;
			continue;
		}

		if (!(splat_flags & GFX_SPLAT_TWO_SIDED) && texPolyFacesAway(tri_tex_coords))
		{
			// facing away from start position, stop processing this triangle & don't append it
			// to the mesh
			vertex_count -= 3;
			continue;
		}

		{
			Vec3 a, b;
			subVec3(vertex[1], vertex[0], a);
			subVec3(vertex[2], vertex[0], b);
			crossVec3(a, b, normals[0]);
			normalizeCopyVec3(normals[0],normals[0]);

			copyVec3(normals[0], normals[1]);
			copyVec3(normals[0], normals[2]);
		}

		vertex += 3;

		if (!splat_setup->shader_tex_project)
		{
			copyVec2(tri_tex_coords[0], tex_coords[0]);
			copyVec2(tri_tex_coords[1], tex_coords[1]);
			copyVec2(tri_tex_coords[2], tex_coords[2]);
			tex_coords += 3;
		}

		normals += 3;
	}

	splat_setup->vertex_count = vertex_count;
}

static bool gfxSplatSetupTextureProjection(GfxSplatSetup * splat_setup, 
	const Mat4 start, F32 start_radius, F32 aspect_y, const Vec3 direction, F32 end_radius)
{
	Vec3 delta;
	Mat4 camera;
	Mat44 projection;
	F32 length;
	F32 dist_to_camera;
	bool is_ortho;
	// set up the camera
	copyVec3(direction, delta);
	length = normalVec3(delta);

	copyMat4(start, camera);

	if (nearSameF32(start_radius, end_radius))
	{
		dist_to_camera = 0.0f;
		rdrSetupOrthographicProjection(projection, aspect_y, start_radius);
		is_ortho = true;
	}
	else
	{
		dist_to_camera = length * start_radius / (end_radius - start_radius);
		//the aspect is applied to scale the X dimension which is consistent with rdrSetupOrthographicProjection
		rdrSetupFrustumDX(projection, -start_radius * aspect_y, start_radius * aspect_y, -start_radius, start_radius, 
			dist_to_camera, dist_to_camera + length);
		is_ortho = false;
	}

	scaleAddVec3(delta, dist_to_camera, start[3], delta);
	transposeMat3(camera);
	mulVecMat3(delta, camera, camera[3]);
	negateVec3(camera[3], camera[3]);

	// figure out the projection
	// at depth 1 width is start_radius
	// at 'length' units away, the width is end_radius

	mulMat44Mat4(projection, camera, splat_setup->splat_texture_space);

	if (splat_setup->splat_flags & GFX_SPLAT_RELATIVE_COORDS)
	{
		copyMat4(start, camera);
		normalVec3(camera[1]);
		invertMat4Copy(camera, splat_setup->inv_world_space);
	}

	return is_ortho;
}

__forceinline static Vec3 * modelGetUnpackedVertices(SA_PARAM_NN_VALID ModelLOD *model)
{
	return model->unpack.verts;
}

__forceinline static Vec2 * modelGetUnpackedSTs(SA_PARAM_NN_VALID ModelLOD *model)
{
	return model->unpack.sts;
}

__forceinline static Vec3 * modelGetUnpackedNormals(SA_PARAM_NN_VALID ModelLOD *model)
{
	return model->unpack.norms;
}

__forceinline static bool modelHasData(SA_PARAM_NN_VALID ModelLOD *model)
{
	return model->data != NULL;
}

__forceinline static bool modelHasGeoRenderInfo(SA_PARAM_NN_VALID ModelLOD *model)
{
	return model->geo_render_info != NULL;
}

int dfxSplatAlwaysResplat = 0;
AUTO_CMD_INT(dfxSplatAlwaysResplat, dfxSplatAlwaysResplat);

void gfxInitializeSplat(
	const Mat4 start, const Mat4 textureProjection,
	F32 start_radius, F32 aspect_y, const Vec3 direction, 
	F32 end_radius, GfxSplat * splat, bool bForceResplat, F32 fFadePlanePt,
	bool projectionInShader)
{
	S32 triangles;
	GfxSplatSetup splat_setup = { 0 };
	Vec3 end;
	F32 distanceToInitial;
	bool aspect_changed = false;
	F32 thresholdTestDistance;

	int i;
	bool hasAllSplatModelData = true;

	PERFINFO_AUTO_START_FUNC();


	// If we have different matrices for projection and collision, and we aren't just generating splat texture
	// coordinates in a shader, force a resplat so we have sane texture coordinates.
	if(!projectionInShader) {
		for(i = 0; i < 16; i++) {
			if(textureProjection[i] != start[i]) {
				break;
			}
		}
		if(i != 16) {
			aspect_changed = true;
		}
	}

	distanceToInitial = distance3(start[3], splat->initial_start[3]);

	// Track the end point, too. See how far it's moved. If it's more than the origin, use
	// that distance for the resplat threshold test.
	{
		Vec3 currentEnd;
		Vec3 initialEnd;
		F32 distanceToInitialEnd;
		addVec3(start[3], direction, currentEnd);
		addVec3(splat->initial_start[3], splat->initial_direction, initialEnd);
		distanceToInitialEnd = distance3(currentEnd, initialEnd);
		distanceToInitial = MAXF(distanceToInitial, distanceToInitialEnd);
	}

	copyVec3(textureProjection[3], splat->center);
	addVec3(start[3], direction, end);

	// FIXME: The fade plane point is placed between the start of the projection and end of the collision area of the
	// splat. This might need to change if the FX artists have trouble with it.
	lerpVec3(textureProjection[3], fFadePlanePt, end, splat->fade_plane_pt);

	splat->radius = start_radius;

	//if the aspect changed force resplat
	if (splat->aspect_y && !nearSameF32Tol(splat->aspect_y, aspect_y, 0.05f))
		aspect_changed = true;

	splat->aspect_y = aspect_y;

	splat_setup.splat = splat;
	splat_setup.splat_flags = splat->splat_flags;

	if ( splat->tolerance )
		splat_setup.tex_tolerance = fsqrt(splat->tolerance) / start_radius;

	splat->shader_tex_project = projectionInShader;

	gfxSplatSetupTextureProjection(&splat_setup, textureProjection, start_radius, aspect_y, direction, end_radius);
	copyMat44(splat_setup.splat_texture_space, splat->texture_proj_mat);

	// Movement threshold should also take into consideration how far a splat's radius has
	// expanded or contracted since it originally got the triangles.
	thresholdTestDistance = (distanceToInitial + (splat->radius - splat->initial_radius));
	thresholdTestDistance *= thresholdTestDistance;

	for(i = 0; i < gfxSplatGetNumModels(splat); i++) {
		Model *model = gfxSplatGetModel(splat, i);
		ModelLOD *model_lod = modelGetLOD(model, 0);
		if(!modelHasData(model_lod) || !modelHasGeoRenderInfo(model_lod)) {
			hasAllSplatModelData = false;
			break;
		}
	}

	if (eaSize(&splat->splat_models) &&
		hasAllSplatModelData &&
		thresholdTestDistance <= splat->tolerance && !(bForceResplat || dfxSplatAlwaysResplat))
	{
		// Splat moved, but not enough to justify resplatting.

		if (distanceToInitial > 0.05f || !nearSameF32Tol(start[0][0], splat->initial_start[0][0], 0.05f) 
			|| !nearSameF32Tol(start[0][1], splat->initial_start[0][1], 0.05f) || !nearSameF32Tol(start[0][2], splat->initial_start[0][2], 0.05f) || aspect_changed)
		{

			// Splat DID move enough to justify new texture coordinates.

			if (!splat->shader_tex_project) //if we aren't ortho we need to do the projection on the cpu since we need to / by w
			{

				int currentModelNum;
				for(currentModelNum = 0; currentModelNum < gfxSplatGetNumModels(splat); currentModelNum++) {

					Model *model = splat->splat_models[currentModelNum];
					ModelLOD *model_lod = modelGetLOD(model, 0);

					// recalculate the texture projection
					Vec3 * vertex =  modelGetUnpackedVertices(model_lod);
					Vec2 * tex_coords = modelGetUnpackedSTs(model_lod);
					int vertex_count = model_lod->vert_count;
					Mat4 world_space;

					assert(model_lod);

					if (splat_setup.splat_flags & GFX_SPLAT_RELATIVE_COORDS)
					{
						copyMat4(splat->initial_start, world_space);
						normalVec3(world_space[1]);
					}

					for (; vertex_count; --vertex_count)
					{
						Vec3 tex_coord;
						if (splat_setup.splat_flags & GFX_SPLAT_RELATIVE_COORDS)
						{
							Vec3 v;
							mulVecMat4(vertex[0], world_space, v);
							mulVec3ProjMat44(v, splat_setup.splat_texture_space, tex_coord);
						}
						else
						{
							mulVec3ProjMat44(vertex[0], splat_setup.splat_texture_space, tex_coord);
						}

						if (splat_setup.splat_flags & GFX_SPLAT_UNIT_TEXCOORD)
						{
							tex_coord[0] = tex_coord[0] * 0.5f + 0.5f;
							tex_coord[1] = tex_coord[1] * 0.5f + 0.5f;
						}
						copyVec2(tex_coord, tex_coords[0]);

						++vertex;
						++tex_coords;
					}

					gfxModelLODUpdateFromRawGeometry(
						model_lod,
						RUSE_POSITIONS|RUSE_NORMALS|RUSE_TEXCOORDS | ((splat_setup.splat_flags & GFX_SPLAT_NO_TANGENTSPACE) ? 0 : (RUSE_TANGENTS|RUSE_BINORMALS)), 
						true);
				}
			}
		}
	}
	else
	{
		Vec3 startPlusTolerance;
		Vec3 endPlusTolerance;
		Vec3 normalizedDir;
		F32 sqrtTolerance = fsqrt(splat->tolerance);
		F32 fExtendLength;

		copyVec3(direction, normalizedDir);

		// Save initial values for position, radius, and direction.
		copyMat4(start, splat->initial_start);
		splat->initial_radius = MAXF(start_radius, end_radius);
		copyVec3(direction, splat->initial_direction);

		// Push our start and end positions out by the tolerance value.
		fExtendLength = SPLAT_ENDCAP_TOLERANCE_RADIUS_SCALE * splat->initial_radius;
		normalVec3(normalizedDir);
		scaleAddVec3(normalizedDir, -fExtendLength, start[3], startPlusTolerance);
		scaleAddVec3(normalizedDir, fExtendLength, end, endPlusTolerance);

		// regenerate the entire splat
		triangles = wcQueryTrianglesInYAxisCylinder(
			worldGetActiveColl(PARTITION_CLIENT),
			WC_FILTER_BIT_FX_SPLAT,
			startPlusTolerance,
			endPlusTolerance,
			MAXF(start_radius, end_radius) + sqrtTolerance,
			handleTriangles,
			&splat_setup);

		if(splat_setup.vertex_count > SPLAT_MAX_TRIANGLES * 3) {
			ErrorDetailsf(
				"Location: %f, %f, %f\nRadius: %f\nTriangles: %d\n",
				start[3][0], start[3][1], start[3][2],
				splat->initial_radius, splat_setup.vertex_count / 3);
			Errorf("Splat resulted in too many triangles.");
		}

		{
			const int vertsPerModel = 65535; // Must be a multiple of 3! (Can't split triangles across models.)
			int currentModelNum;
			for(currentModelNum = 0; currentModelNum < (splat_setup.vertex_count / vertsPerModel)+1; currentModelNum++) {

				Model *model = NULL;
				ModelLOD *model_lod = NULL;
				int vertOffset = currentModelNum * vertsPerModel;
				int vertsThisModel =
					(currentModelNum == splat_setup.vertex_count / vertsPerModel) ?
					splat_setup.vertex_count - currentModelNum * vertsPerModel :
					vertsPerModel;

				Vec3 *tmpVerts     = calloc(vertsThisModel, sizeof(Vec3));
				Vec2 *tmpTexCoords = calloc(vertsThisModel, sizeof(Vec2));
				Vec3 *tmpNormals   = calloc(vertsThisModel, sizeof(Vec3));

				memcpy(tmpVerts,     splat_setup.vertices   + vertOffset, vertsThisModel * sizeof(Vec3));
				memcpy(tmpTexCoords, splat_setup.tex_coords + vertOffset, vertsThisModel * sizeof(Vec2));
				memcpy(tmpNormals,   splat_setup.normals    + vertOffset, vertsThisModel * sizeof(Vec3));

				if(currentModelNum >= eaSize(&splat->splat_models)) {
					eaPush(&splat->splat_models, tempModelAlloc("Splat", &splat->material, 1, WL_FOR_FX));
				}

				model = splat->splat_models[currentModelNum];
				model_lod = modelGetLOD(model, 0);

				gfxModelLODFromRawGeometry(
					model_lod,
					tmpVerts,
					vertsThisModel / 3,
					tmpTexCoords,
					tmpNormals,
					RUSE_POSITIONS|RUSE_NORMALS|( !splat->shader_tex_project ? RUSE_TEXCOORDS : 0 ) | ((splat_setup.splat_flags & GFX_SPLAT_NO_TANGENTSPACE) ? 0 : (RUSE_TANGENTS|RUSE_BINORMALS)),
					true);

				assert(model_lod->materials);
				if (!model_lod->materials)
					model_lod->materials = calloc(sizeof(model_lod->materials[0]), 1);
				model_lod->materials[0] = splat->material;

			}

			free(splat_setup.vertices);
			free(splat_setup.tex_coords);
			free(splat_setup.normals);
			splat_setup.vertices = NULL;
			splat_setup.tex_coords = NULL;
			splat_setup.normals = NULL;

			// Clean up extra models.
			{
				int currentModelToDeleteNum = currentModelNum;
				while(currentModelToDeleteNum < eaSize(&splat->splat_models)) {
					tempModelFree(&splat->splat_models[currentModelToDeleteNum]);
					currentModelToDeleteNum++;
				}
				eaSetSize(&splat->splat_models, currentModelNum);
			}


		}
	}

	PERFINFO_AUTO_STOP();
}

GfxSplat * gfxCreateSplatMtx(
	const Mat4 start, const Mat4 textureProjection, F32 start_radius, F32 aspect_y,
	const Vec3 direction, F32 end_radius, int splat_options, Material * material,
	F32 motion_tolerance, F32 fFadePlanePt)
{
	GfxSplat * splat;
	F32 splatLen = lengthVec3(direction);
	PERFINFO_AUTO_START_FUNC();
	splat = (GfxSplat*)calloc(1, sizeof(GfxSplat));

	splat->splat_flags = splat_options | (gfx_state.noTangentSpace?GFX_SPLAT_NO_TANGENTSPACE:0);
	splat->material = material;
	splat->splat_models = NULL;
	splat->tolerance = MIN(splatLen, MIN(start_radius, end_radius)) * motion_tolerance;

	gfxInitializeSplat(start, textureProjection, start_radius, aspect_y, direction, end_radius, splat, false, fFadePlanePt, true);

	PERFINFO_AUTO_STOP();
	return splat;
}

void gfxUpdateSplatMtx(const Mat4 start, const Mat4 textureProjection, F32 start_radius, F32 aspect_y,
	const Vec3 direction, F32 end_radius, GfxSplat * splat, bool bForceResplat,
	F32 fFadePlanePt) {

	gfxInitializeSplat(start, textureProjection, start_radius, aspect_y, direction, end_radius, splat, bForceResplat, fFadePlanePt, true);
}

static const IVec3 rotate_90_swizzle = { 1, 0, 2 };

__forceinline static void permuteVec3(const Vec3 v, const IVec3 swizzle, Vec3 r)
{
	setVec3(r, v[swizzle[0]], v[swizzle[1]], v[swizzle[2]]);
}

__forceinline static void rotate90XYVec3(const Vec3 v, Vec3 r)
{
	permuteVec3(v, rotate_90_swizzle, r);
	r[0] = -r[0];
}

static void mat3FromFwdVector_old(const Vec3 fwdVec, Mat3 mat)
{
	// We need an orientation matrix that has a certain fwd vector,
	// but we just don't care about the X or Y vector (as long as it's orthonormal)
	copyVec3( fwdVec, mat[2] );

	// This is just to insure that the X-axis vector is not collinear with the y-axis vector
	if ( fabs(fwdVec[2]) >= 1.0f - 1e-7f )
	{
		copyVec3(sidevec, mat[0]);
		copyVec3(upvec, mat[1]);

		if (fwdVec[2] < 0)
			mat[0][0] = -1.0f;
	}
	else
	{
		rotate90XYVec3(fwdVec, mat[0]);
		mat[0][2] = 0.0f;
		normalVec3(mat[0]);
		crossVec3( mat[2], mat[0], mat[1] );
	}
}

__forceinline static void textureTransformFromDirection(const Vec3 direction, Mat3 matrix)
{
	Vec3 unit_direction;
	copyVec3(direction, unit_direction);
	normalVec3(unit_direction);
	mat3FromFwdVector_old(unit_direction, matrix);
}

GfxSplat * gfxCreateSplat(
	const Vec3 start, const Mat4 textureProjection,
	F32 start_radius, F32 aspect_y, const Vec3 direction, F32 end_radius,
	int splat_options, Material * material, F32 motion_tolerance)
{
	Mat4 startMtx;
	textureTransformFromDirection(direction, startMtx);
	copyVec3(start, startMtx[3]);

	return gfxCreateSplatMtx(startMtx, textureProjection, start_radius, aspect_y, direction, end_radius,
		splat_options | GFX_SPLAT_USE_SPECIFIED_TRANSFORM, material, motion_tolerance, 0);
}

void gfxUpdateSplat(
	const Vec3 start, const Mat4 textureProjection,
	F32 start_radius, F32 aspect_y, const Vec3 direction, F32 end_radius,
	GfxSplat * splat, bool bForceResplat, F32 fFadePlanePt)
{
	Mat4 startMtx;
	textureTransformFromDirection(direction, startMtx);
	copyVec3(start, startMtx[3]);

	splat->splat_flags |= GFX_SPLAT_USE_SPECIFIED_TRANSFORM;
	gfxInitializeSplat(startMtx, textureProjection, start_radius, aspect_y, direction, end_radius, splat, bForceResplat, fFadePlanePt, true);
}

void gfxDestroySplat(GfxSplat * splat)
{
	int i;
	for(i = 0; i < eaSize(&splat->splat_models); i++) {
		tempModelFree(&splat->splat_models[i]);
	}
	eaDestroy(&splat->splat_models);
	free(splat);
}

Model* gfxSplatGetModel(GfxSplat* splat, int index)
{
	if(index >= 0 && index < eaSize(&splat->splat_models))
		return splat->splat_models[index];

	return NULL;
}

int gfxSplatGetNumModels(const GfxSplat* splat) {
	return eaSize(&splat->splat_models);
}

void gfxSplatGetWorldMatrix(GfxSplat* splat, Mat4 world_matrix)
{
	if (splat->splat_flags & GFX_SPLAT_RELATIVE_COORDS)
	{
		copyMat4(splat->initial_start, world_matrix);
		normalVec3(world_matrix[1]);
	}
	else
	{
		copyMat4(unitmat, world_matrix);
	}
}

void gfxSplatGetTextureMatrix( GfxSplat* splat, Mat44 tex_matrix )
{
	

	 //this will only work with ortho stuff, but right now that's all that uses this
	if (splat->splat_flags & GFX_SPLAT_UNIT_TEXCOORD)
	{
		Mat44 tempMat;
		identityMat44(tempMat);
		

		tempMat[0][0] = 0.5f;
		tempMat[1][1] = 0.5f;
		tempMat[2][2] = 0.5f;

		tempMat[3][0] = 0.5f;
		tempMat[3][1] = 0.5f;
		tempMat[3][2] = 0.5f;

		mulMat44Inline(tempMat, splat->texture_proj_mat, tex_matrix);
	}
	else
	{
		copyMat44(splat->texture_proj_mat, tex_matrix);
	}
}

bool gfxSplatIsUsingShaderProjection(GfxSplat* splat)
{
	return splat->shader_tex_project;
}

static GfxSplat * gfxCreateUpdateSkeletonShadowSplatInternal(GfxSplat* pSplat, const DynSkeleton* pSkeleton, F32 motion_tolerance, bool invalidate)
{
	Vec3 vMin, vMax, vMid;
	Vec3 vDir;
	Mat4 mSplatMat;
	F32 fRadius, fAspectY;
	Mat4 mNodeMat;
	F32 fRadiusInner, fRadiusOuter;
	Vec3 vPlanePt, vHeading;

	dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mNodeMat, false);

	copyVec3(pSkeleton->vCurrentGroupExtentsMin, vMin);
	copyVec3(pSkeleton->vCurrentGroupExtentsMax, vMax);

	centerVec3(vMin, vMax, vMid);
	//we base the size on the z axis since the x size is radius * aspect 
	fRadius = 1.0f * (vMax[2]-vMin[2]);

	if (!fRadius)
	{
		// DJR default radius in case skeleton extent is not available
		fRadius = 1.5f;
		fAspectY = 0.8f;
	}
	else
	{
		fAspectY = ABS(vMax[0] - vMin[0]) / ABS(vMax[2] - vMin[2]);
	}

	//limit the aspect to reasonable values
	MAX1(fAspectY, 1.0/4.0);
	MIN1(fAspectY, 4.0);

	//if we can, smooth out the radius and aspect
	if (pSplat)
	{
		fRadius = (pSplat->radius + fRadius) / 2.0f;
		fAspectY = (pSplat->aspect_y + fAspectY) / 2.0f;
	}

	//compose the matrix to rotate the splat
	mulVecMat3(forwardvec, mNodeMat, vHeading);
	if (lengthVec3XZ(vHeading) < 0.001)
	{
		setVec3(vHeading, 0, 0, 1);
	}
	else
	{
		vHeading[1] = 0;
		normalVec3(vHeading);
	}

	{
		float angle = atan2f(vHeading[0], vHeading[2]);
		Mat3  mRotationMat;
		Mat3 mSplatOrientMat = {{1, 0, 0}, {0, 0, 1} , {0, -1, 0}};
		mat3FromAxisAngle(mRotationMat, forwardvec, angle);
		mulMat3(mSplatOrientMat, mRotationMat, mSplatMat);
	}

	mulVecMat4(vMid, mNodeMat, mSplatMat[3]);

	fRadiusInner = fRadius + 2;
	fRadiusOuter = fRadiusInner * 2;
	setVec3(vDir, 0, -fRadiusOuter, 0);

	if (pSplat)
	{
		gfxUpdateSplatMtx(mSplatMat, mSplatMat, fRadius, fAspectY, vDir, fRadius, pSplat, invalidate, 0);
	}
	else
	{
		pSplat = gfxCreateSplatMtx(mSplatMat, mSplatMat, fRadius, fAspectY, vDir, fRadius, GFX_SPLAT_CULL_UNIT_TEX | GFX_SPLAT_UNIT_TEXCOORD | GFX_SPLAT_NO_TANGENTSPACE, materialFind("SplatShadow", WL_FOR_ENTITY), motion_tolerance, 0);
	}

	//override the plane pt
	copyVec3(vMid, vPlanePt);
	vPlanePt[1] = vMin[1] + 1.0f;
	mulVecMat4(vPlanePt, mNodeMat, pSplat->fade_plane_pt);

	return pSplat;
}

GfxSplat * gfxCreateSkeletonShadowSplat(const DynSkeleton* pSkeleton, F32 motion_tolerance)
{
	return gfxCreateUpdateSkeletonShadowSplatInternal(0, pSkeleton, motion_tolerance, false);
}

void gfxUpdateSkeletonShadowSplat(const DynSkeleton* pSkeleton, GfxSplat * splat, bool bForceResplat)
{
	gfxCreateUpdateSkeletonShadowSplatInternal(splat, pSkeleton, 0.0f, bForceResplat);
}

static GfxSplat * gfxCreateUpdateRagdollPartShadowSplatInternal(GfxSplat* pSplat, const DynSkeleton* pSkeleton, DynRagdollPartState* pRagdollPart, F32 motion_tolerance, bool invalidate)
{
	Vec3 vMid;
	Vec3 vDir;
	Mat4 mSplatMat;
	F32 fRadius, fAspectY;
	Mat4 mNodeMat;
	F32 fRadiusInner, fRadiusOuter;
	Vec3 vPlanePt, vHeading;
	
	dynNodeGetWorldSpaceMat(dynSkeletonFindNode(pSkeleton,pRagdollPart->pcBoneName), mNodeMat, false);
	copyVec3(pRagdollPart->vCenterOfGravity,vMid);

	if (pRagdollPart->eShape == eRagdollShape_Box)
	{
		Vec3 vxDirWS, vyDirWS, vzDirWS;
		F32 fxLengthProj, fyLengthProj, fzLengthProj;
		F32 T1, T2;

		mulVecMat3(sidevec, mNodeMat, vxDirWS);
		normalVec3(vxDirWS);
		T1 = dotVec3(vxDirWS, forwardvec);
		T2 = dotVec3(vxDirWS, sidevec);
		fxLengthProj = SQR(T1) + SQR(T2);

		mulVecMat3(upvec, mNodeMat, vyDirWS);
		normalVec3(vyDirWS);
		T1 = dotVec3(vyDirWS, forwardvec);
		T2 = dotVec3(vyDirWS, sidevec);
		fyLengthProj = SQR(T1) + SQR(T2);

		mulVecMat3(forwardvec, mNodeMat, vzDirWS);
		normalVec3(vzDirWS);
		T1 = dotVec3(vzDirWS, forwardvec);
		T2 = dotVec3(vzDirWS, sidevec);
		fzLengthProj = SQR(T1) + SQR(T2);
		
		if (fxLengthProj >= fyLengthProj && fxLengthProj >= fzLengthProj)
		{
			fRadius = 1.2f * (fxLengthProj*pRagdollPart->vBoxDimensions[0]);
			copyVec3(vxDirWS, vHeading);
			if (fyLengthProj > fzLengthProj) {
				fAspectY = 1.2f * (fyLengthProj*pRagdollPart->vBoxDimensions[1])/fRadius;
			} else {
				fAspectY = 1.2f * (fzLengthProj*pRagdollPart->vBoxDimensions[2])/fRadius;
			}
		}
		else if (fyLengthProj >= fxLengthProj && fyLengthProj >= fzLengthProj)
		{
			fRadius = 1.2f * (fyLengthProj*pRagdollPart->vBoxDimensions[1]);
			copyVec3(vyDirWS, vHeading);
			if (fxLengthProj > fzLengthProj) {
				fAspectY = 1.2f * (fxLengthProj*pRagdollPart->vBoxDimensions[0])/fRadius;
			} else {
				fAspectY = 1.2f * (fzLengthProj*pRagdollPart->vBoxDimensions[2])/fRadius;
			}
		} else {
			fRadius = 1.2f * (fzLengthProj*pRagdollPart->vBoxDimensions[2]);
			copyVec3(vzDirWS, vHeading);
			if (fxLengthProj > fyLengthProj) {
				fAspectY = 1.2f * (fxLengthProj*pRagdollPart->vBoxDimensions[0])/fRadius;
			} else {
				fAspectY = 1.2f * (fyLengthProj*pRagdollPart->vBoxDimensions[1])/fRadius;
			}
		}
		//fAspectY = 1.f;
	}
	else if (pRagdollPart->eShape == eRagdollShape_Capsule)
	{
		F32 fLengthProj;
		Vec3 vDirWS;
		F32 T1, T2;
		
		mulVecMat3(pRagdollPart->vCapsuleDir, mNodeMat, vDirWS);
		normalVec3(vDirWS);
		copyVec3(vDirWS, vHeading);

		T1 = dotVec3(vDirWS, forwardvec);
		T2 = dotVec3(vDirWS, sidevec);
		fLengthProj = SQR(T1) + SQR(T2);
		fRadius = .5f*fLengthProj*pRagdollPart->fCapsuleLength + pRagdollPart->fCapsuleRadius;
		fAspectY = pRagdollPart->fCapsuleRadius / fRadius;

		fRadius  *= 1.8f;
		fAspectY = sqrtf(fAspectY);
	}
	else
	{
		return NULL;
	}

	//limit the aspect to reasonable values
	MAX1(fAspectY, 1.0/4.0);
	MIN1(fAspectY, 4.0);

	//if we can, smooth out the radius and aspect
	if (pSplat)
	{
		fRadius = 0.25f*pSplat->radius + 0.75f*fRadius;
		fAspectY = 0.25f*pSplat->aspect_y + 0.75f*fAspectY;
	}

	{
		float angle = atan2f(vHeading[0], vHeading[2]);
		Mat3  mRotationMat;
		Mat3 mSplatOrientMat = {{1, 0, 0}, {0, 0, 1} , {0, -1, 0}};
		mat3FromAxisAngle(mRotationMat, forwardvec, angle);
		mulMat3(mSplatOrientMat, mRotationMat, mSplatMat);
	}

	mulVecMat4(vMid, mNodeMat, mSplatMat[3]);

	fRadiusInner = fRadius + 2;
	fRadiusOuter = fRadiusInner * 2;
	setVec3(vDir, 0, -fRadiusOuter, 0);

	if (pSplat)
	{
		gfxUpdateSplatMtx(mSplatMat, mSplatMat, fRadius, fAspectY, vDir, fRadius, pSplat, invalidate, 0);
	}
	else
	{
		pSplat = gfxCreateSplatMtx(mSplatMat, mSplatMat, fRadius, fAspectY, vDir, fRadius, GFX_SPLAT_CULL_UNIT_TEX | GFX_SPLAT_UNIT_TEXCOORD | GFX_SPLAT_NO_TANGENTSPACE, materialFind("SplatShadow", WL_FOR_ENTITY), motion_tolerance, 0);
	}

	//override the plane pt
	copyVec3(vMid, vPlanePt);
	vPlanePt[1] = 1.0f;
	mulVecMat4(vPlanePt, mNodeMat, pSplat->fade_plane_pt);

	return pSplat;
}

GfxSplat * gfxCreateRagdollPartShadowSplat(const DynSkeleton *pSkeleton, DynRagdollPartState *pRagdollPart, F32 motion_tolerance)
{
	return gfxCreateUpdateRagdollPartShadowSplatInternal(0, pSkeleton, pRagdollPart, motion_tolerance, false);
}

void gfxUpdateRagdollPartShadowSplat(const DynSkeleton *pSkeleton, DynRagdollPartState *pRagdollPart, GfxSplat *pSplat, bool bForceResplat)
{
	gfxCreateUpdateRagdollPartShadowSplatInternal(pSplat, pSkeleton, pRagdollPart, 0.f, bForceResplat);
}

static MaterialNamedConstant **shadowSplatNamedConstants = NULL;

AUTO_RUN;
void gfxInitShadowSplatMaterialParams(void)
{
	MaterialNamedConstant* pConstant = calloc(sizeof(MaterialNamedConstant), 1);
	pConstant->name = allocAddStaticString("Alphaplanefade");
	eaPush(&shadowSplatNamedConstants, pConstant);

	//used the by the ao splats
	pConstant = calloc(sizeof(MaterialNamedConstant), 1);
	pConstant->name = allocAddStaticString("Maxamount"); 
	eaPush(&shadowSplatNamedConstants, pConstant);
}

void gfxQueueShadowOrAOSplatToDrawList(GfxSplat* splat, F32 alpha, F32 max_alpha, bool frustum_visible, RdrDrawList* pDrawList)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int numSplatModels = gfxSplatGetNumModels(splat);
	int currentSplatModel;
	for(currentSplatModel = 0; currentSplatModel < numSplatModels; currentSplatModel++) {

		ModelToDraw models[NUM_MODELTODRAWS];
		Model* pModel;
		int model_count;

		pModel = gfxSplatGetModel(splat, currentSplatModel);

		model_count = gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 0, 1, 0, NULL, pModel->radius);
		if (model_count)
		{
			RdrAddInstanceParams instance_params={0};
			RdrInstancePerDrawableData per_drawable_data;
			int k;
			F32 zdist;

			gfxSplatGetWorldMatrix(splat, instance_params.instance.world_matrix);
			addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);
			mulVecMat4(pModel->mid, instance_params.instance.world_matrix, instance_params.instance.world_mid);
			setVec4same(instance_params.instance.color, 1);
			instance_params.distance_offset = pModel->radius;
			instance_params.wireframe = gfx_state.wireframe & 3;
			instance_params.frustum_visible = frustum_visible;
			setVec3same(instance_params.ambient_multiplier, 1);

			zdist = rdrDrawListCalcZDist(pDrawList, instance_params.instance.world_mid);

			// Draw each LOD model
			for (k=0; k<model_count; k++) 
			{
				RdrDrawableGeo *geo_draw;
				int i, num_vs_consts;
				F32 fRadiusInner, fRadiusOuter;

				if (!models[k].geo_handle_primary) {
					assert(0);
					continue;
				}

				instance_params.instance.color[3] = models[k].alpha * alpha;
				instance_params.instance.morph = models[k].morph;

				num_vs_consts = splat->shader_tex_project ? 5 : 1;
				geo_draw = rdrDrawListAllocGeo(pDrawList, RTYPE_MODEL, models[k].model, models[k].model->geo_render_info->subobject_count, num_vs_consts, 0);

				if (!geo_draw)
					continue;

				makePlane2(splat->fade_plane_pt, upvec, geo_draw->vertex_shader_constants[0]);

				instance_params.add_material_flags = RMATERIAL_DECAL | RMATERIAL_DEPTHBIAS | RMATERIAL_ALPHA_FADE_PLANE | RMATERIAL_NOINSTANCE;

				if (splat->shader_tex_project) //if we are ortho we are using the shader with projection in it
				{
					instance_params.add_material_flags |= RMATERIAL_VS_TEXCOORD_SPLAT;
					gfxSplatGetTextureMatrix(splat, &geo_draw->vertex_shader_constants[1]); //using 1-5 to store a 4x4 mat
				}

				fRadiusInner = splat->radius + 2;
				fRadiusOuter = fRadiusInner * 2;

				geo_draw->geo_handle_primary = models[k].geo_handle_primary;

				SETUP_INSTANCE_PARAMS;

				setVec4(shadowSplatNamedConstants[0]->value, fRadiusInner, 1.f / (fRadiusOuter - fRadiusInner), 0, 0);
				setVec4same(shadowSplatNamedConstants[1]->value, max_alpha);

				RDRALLOC_SUBOBJECTS(pDrawList, instance_params, models[k].model, i);
				for (i = 0; i < geo_draw->subobject_count; i++)
					gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[i], models[k].model->materials[i], NULL, shadowSplatNamedConstants, NULL, NULL, instance_params.per_drawable_data[i].instance_param, zdist - pModel->radius, models[k].model->uv_density);

				rdrDrawListAddGeoInstance(pDrawList, geo_draw, &instance_params, RST_AUTO, ROC_CHARACTER, true);

				gfxGeoIncrementUsedCount(models[k].model->geo_render_info, geo_draw->subobject_count, true);
			}

		}
	}
}

void gfxQueueShadowSplat(GfxSplat* splat, F32 alpha, bool frustum_visible)
{
	gfxQueueShadowOrAOSplatToDrawList(splat, alpha, 1.0f, frustum_visible, gfx_state.currentAction->gdraw.draw_list);
}

void gfxQueueAOShadowSplat( GfxSplat* splat, F32 alpha, F32 max_alpha, bool frustum_visible )
{
	gfxQueueShadowOrAOSplatToDrawList(splat, alpha, max_alpha, frustum_visible, gfx_state.currentAction->gdraw.draw_list_ao);
}

static GfxSplat * gfxCreateUpdateNodeShadowSplatInternal(GfxSplat* pSplat, const DynNode* pNode, Vec2 vSize, F32 motion_tolerance, bool useOrientation, bool invalidate)
{
	Vec3 vFwdDir, vUpDir, vRightDir, vDir;
	Mat4 mSplatMat;
	F32 fRadius, fAspectY;
	Mat4 mNodeMat;
	F32 fRadiusInner, fRadiusOuter;

	dynNodeGetWorldSpaceMat(pNode, mNodeMat, false);

	//we base the size on the z axis since the x size is radius * aspect 
	fRadius = vSize[1];
	fAspectY = ABS(vSize[0]) / ABS(vSize[1]);

	//if we can, smooth out the radius and aspect
	if (pSplat)
	{
		fRadius = (pSplat->radius + fRadius) / 2.0f;
		fAspectY = (pSplat->aspect_y + fAspectY) / 2.0f;
	}

	//limit the aspect to reasonable values
	MAX1(fAspectY, 1.0/4.0);
	MIN1(fAspectY, 4.0);


	setVec3(vRightDir, 1.0f, 0, 0);
	setVec3(vUpDir, 0, 0, 1.0f);
	setVec3(vFwdDir, 0, 1.0f, 0);

	if (useOrientation)
	{
		Vec3 vHeading;
		//compose the matrix to rotate the splat
		mulVecMat3(forwardvec, mNodeMat, vHeading);
		if (lengthVec3XZ(vHeading) < 0.001)
		{
			setVec3(vHeading, 0, 0, 1);
		}
		else
		{
			vHeading[1] = 0;
			normalVec3(vHeading);
		}

		{
			float angle = atan2f(vHeading[0], vHeading[2]);
			Mat3  mRotationMat;
			Mat3 mSplatOrientMat = {{1, 0, 0}, {0, 0, 1} , {0, -1, 0}};
			mat3FromAxisAngle(mRotationMat, forwardvec, angle);
			mulMat3(mSplatOrientMat, mRotationMat, mSplatMat);
		}

	}
	else
	{
		copyVec3(vRightDir, mSplatMat[0]);
		copyVec3(vUpDir, mSplatMat[1]);
		copyVec3(vFwdDir, mSplatMat[2]);
	}

	
	mulVecMat4(zerovec3, mNodeMat, mSplatMat[3]);

	fRadiusInner = fRadius + 2;
	fRadiusOuter = fRadiusInner * 2;
	setVec3(vDir, 0, -fRadiusOuter, 0);

	if (pSplat)
	{
		gfxUpdateSplatMtx(mSplatMat, mSplatMat, fRadius, fAspectY, vDir, fRadius, pSplat, invalidate, 0);
	}
	else
	{
		pSplat = gfxCreateSplatMtx(mSplatMat, mSplatMat, fRadius, fAspectY, vDir, fRadius, GFX_SPLAT_CULL_UNIT_TEX | GFX_SPLAT_UNIT_TEXCOORD | GFX_SPLAT_NO_TANGENTSPACE, materialFind("SplatShadow", WL_FOR_ENTITY), motion_tolerance, 0);
	}

	return pSplat;
}



GfxSplat * gfxCreateNodeShadowSplat( const DynNode* pNode, Vec2 vSize, F32 motion_tolerance, bool useOrientation )
{
	return gfxCreateUpdateNodeShadowSplatInternal(0, pNode, vSize, motion_tolerance, useOrientation, true);
}

void gfxUpdateNodeShadowSplat( const DynNode* pNode, GfxSplat * splat, Vec2 vSize, bool useOrientation, bool bForceResplat )
{
	gfxCreateUpdateNodeShadowSplatInternal(splat, pNode, vSize, 0, useOrientation, bForceResplat);
}

Material* gfxSplatGetMaterial( GfxSplat* splat )
{
	return splat->material;
}

void gfxSplatSetMaterial( GfxSplat* splat, Material* mat )
{
	splat->material = mat;
}

