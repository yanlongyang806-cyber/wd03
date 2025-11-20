#include <stdio.h>
#include "fpcube.h"
#include "ScratchStack.h"
#include "GenericMesh.h"
#include "file.h"

#include "gridpoly.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););

#define NUMCELLBITS			1
#define NUMCELLS			2
#define NUMCELLS_SQUARE		4
#define NUMCELLS_CUBE		8
#define SETGRIDSIZE(g,s)	(g->size = (F32)s, g->inv_size = (F32)(1.f/s), g->num_bits = (int)log2((int)s))

#define NUM_POOLED_TRI_IDXS 256

static int gridInsertPolys(PolyGrid *grid,PolyCell *cell,GMesh *mesh,F32 meshradius,Vec3 pos,F32 cell_size,int *idxs,int count MEM_DBG_PARMS)
{
	Vec3		max,ppos;
	int			*in_idxs,in_count=0,i,x,y,z;
	PolyCell	*child,**child_ptr;
	F32			*v1,*v2,*v3,min_cell = 64;
	int			ret;
	Vec3		dv,tpos;
#define		SQUIDGE 0.1f

#if 0
	min_cell = pow(mesh->radius,0.5);
	if (min_cell < 2)
		min_cell = 2;
#endif
	in_idxs = ScratchAlloc(sizeof(int) * count);
	for(i=0;i<3;i++)
		max[i] = pos[i] + cell_size;
	for(i=0;i<count;i++)
	{
		Vec3 cell_size_vec;
		v1 = mesh->positions[mesh->tris[idxs[i]].idx[0]];
		v2 = mesh->positions[mesh->tris[idxs[i]].idx[1]];
		v3 = mesh->positions[mesh->tris[idxs[i]].idx[2]];

		setVec3(dv,SQUIDGE * 0.5f,SQUIDGE * 0.5f,SQUIDGE * 0.5f);
		subVec3(pos,dv,tpos);
		setVec3same(cell_size_vec, cell_size + SQUIDGE);
		ret = triCube(tpos,cell_size_vec,v1,v2,v3);
		if (ret)
			in_idxs[in_count++] = idxs[i];
	}
	if (!in_count)
	{
		MP_FREE_MEMBER(grid, PolyCell, cell);
		ScratchFree(in_idxs);
		return 0;
	}
	if (meshradius > 500)
		min_cell = 128;
	if (meshradius > 1000)
		min_cell = 256;
	if (meshradius > 2000)
		min_cell = 1024;
	if ((cell_size <= min_cell && in_count < 9) || cell_size <= 4.f)
	{
		if (in_count <= NUM_POOLED_TRI_IDXS)
			cell->tri_idxs = MP_ALLOC_MEMBER_SDBG(grid, PolyCellTriIdxs);
		else
			cell->tri_idxs = scalloc(sizeof(PolyCellTriIdxs) * in_count,1);
		cell->tri_count = in_count;
		for(i=0;i<in_count;i++)
		{
			cell->tri_idxs[i] = in_idxs[i];
		}
		ScratchFree(in_idxs);
		return 1;
	}
	cell->children = MP_ALLOC_MEMBER_SDBG(grid, PolyCellChildren);
	cell_size /= NUMCELLS;
	for(y=0;y<NUMCELLS;y++)
	{
		for(z=0;z<NUMCELLS;z++)
		{
			for(x=0;x<NUMCELLS;x++)
			{
				child_ptr = &cell->children[NUMCELLS_SQUARE * y + NUMCELLS * z + x];
				*child_ptr = MP_ALLOC_MEMBER_SDBG(grid, PolyCell);
				child = *child_ptr;
				ppos[0] = pos[0] + x * cell_size;
				ppos[1] = pos[1] + y * cell_size;
				ppos[2] = pos[2] + z * cell_size;
				if (!gridInsertPolys(grid,child,mesh,meshradius,ppos,cell_size,in_idxs,in_count MEM_DBG_PARMS_CALL))
					*child_ptr = 0;
			}
		}
	}
	ScratchFree(in_idxs);
	return 1;
}

int pdepth[100];

int bitnum(F32 size)
{
int		i,ival;

	ival = (int)size;
	for(i = 0;i<32;i++)
	{
		if ((1 << i) >= ival)
			break;
	}
	return i;
}

void pstats(PolyCell *cell,int depth)
{
int		i;

	if (!cell)
		return;
//	pdepth[bitnum(cell->size[0])] += cell->objCount;
	if (cell->children)
	{
		for(i=0;i<NUMCELLS_CUBE;i++)
			pstats(cell->children[i],depth+1);
	}
}

F32 powSnapCeil(F32 val,F32 pow)
{
int		i;
F32		pow_val=1;

	for(i=0;i<32;i++)
	{
		if (pow_val >= val)
			return pow_val;
		pow_val *= pow;
	}
	return -1;
}

void powCubeSnap(Vec3 v,F32 pow)
{
int		i;
F32		max = 0;

	for(i=0;i<3;i++)
	{
		v[i] = powSnapCeil(v[i],pow);
		if (v[i] > max)
			max = v[i];
	}
	for(i=0;i<3;i++)
	v[i] = max;
}

void gridPolys(PolyGrid *grid,GMesh *mesh MEM_DBG_PARMS)
{
	int		i,j;
	int		*idxs;
	Vec3	min,max,dv;
	F32		*v, meshradius;

	assert(!grid->cell);

	MP_CREATE_MEMBER_SDBG(grid, PolyCell, 256);
	MP_CREATE_MEMBER_EX_SDBG(grid, PolyCellChildren, 256, NUMCELLS_CUBE*sizeof(PolyCellChildren));
	MP_CREATE_MEMBER_EX_SDBG(grid, PolyCellTriIdxs, 256, NUM_POOLED_TRI_IDXS*sizeof(PolyCellTriIdxs));

	setVec3(min,10e10,10e10,10e10);
	setVec3(max,-10e10,-10e10,-10e10);
	for(i=0;i<mesh->vert_count;i++)
	{
		v = mesh->positions[i];
		for(j=0;j<3;j++)
		{
			if (v[j] < min[j])
				min[j] = v[j];
			if (v[j] > max[j])
				max[j] = v[j];
		}
	}

	grid->cell = MP_ALLOC_MEMBER_SDBG(grid, PolyCell);
	copyVec3(min,grid->pos);
	subVec3(max,min,dv);
	meshradius = lengthVec3(dv) * 0.5f;
	powCubeSnap(dv,NUMCELLS);
	SETGRIDSIZE(grid,dv[0]);
	idxs = ScratchAlloc(sizeof(int) * mesh->tri_count);
	for(i=0;i<mesh->tri_count;i++)
		idxs[i] = i;
	if (mesh->tri_count)
	{
		if (!gridInsertPolys(grid,grid->cell,mesh,meshradius,grid->pos,grid->size,idxs,mesh->tri_count MEM_DBG_PARMS_CALL))
		{
			grid->cell = NULL;
		}
	}
	ScratchFree(idxs);
}


PolyCell *polyGridFindCell(PolyGrid *grid, const Vec3 pos, Vec3 cell_min, Vec3 cell_max)
{
	PolyCell	*cell,*child;
	int			bit_count,bit,i,idx[3],oct_bits[3],bad_mask,cell_size;
	Vec3		min_pos;

	if (!grid->cell)
		return 0;

	subVec3(pos, grid->pos, min_pos);
	qtruncVec3NoFPCWChange(min_pos,oct_bits);
	bit_count = grid->num_bits;
	bad_mask = ~((1 << bit_count) - 1);
	if ((oct_bits[0] |  oct_bits[1] | oct_bits[2]) & bad_mask)
		return 0; // not in grid
	cell = grid->cell;
	cell_size = qtrunc(grid->size);
	copyVec3(grid->pos, min_pos);
	for (bit = bit_count-1;bit >= 0;bit--)
	{
		if (!cell->children)
		{
			bit++;
			break;
		}
		for (i=0;i<3;i++)
			idx[i] = (oct_bits[i] >> bit) & 1;
		child = cell->children[(idx[1] << (NUMCELLBITS*2)) + (idx[2] << NUMCELLBITS) + idx[0]];
		if (!child)
			break;

		cell = child;
		cell_size = cell_size >> 1;
		scaleAddVec3(idx, cell_size, min_pos, min_pos);
	}

	if (cell_min)
		copyVec3(min_pos, cell_min);
	if (cell_max)
		addVec3same(min_pos, cell_size, cell_max);

	return cell;
}

static void findTrisRecurse(PolyCell *cell, Vec3 cell_min, float cell_size, const Vec3 search_min, const Vec3 search_max, int **tri_results)
{
	Vec3 cell_max;

	cell_max[0] = cell_min[0] + cell_size;
	cell_max[1] = cell_min[1] + cell_size;
	cell_max[2] = cell_min[2] + cell_size;
	
	if (!boxBoxCollision(search_min, search_max, cell_min, cell_max))
		return;

	if (cell->children)
	{
		PolyCell *child;
		int x, y, z;
		Vec3 ppos;

		cell_size /= NUMCELLS;
		for(y=0;y<NUMCELLS;y++)
		{
			for(z=0;z<NUMCELLS;z++)
			{
				for(x=0;x<NUMCELLS;x++)
				{
					child = cell->children[NUMCELLS_SQUARE * y + NUMCELLS * z + x];
					if (child)
					{
						ppos[0] = cell_min[0] + x * cell_size;
						ppos[1] = cell_min[1] + y * cell_size;
						ppos[2] = cell_min[2] + z * cell_size;
						findTrisRecurse(child, ppos, cell_size, search_min, search_max, tri_results);
					}
				}
			}
		}
	}
	else
	{
		int i;
		for (i = 0; i < cell->tri_count; i++)
			eaiPushUnique(tri_results, cell->tri_idxs[i]);
	}
}

int *polyGridFindTris(PolyGrid *grid, const Vec3 min, const Vec3 max)
{
	int *tri_results = NULL;
	findTrisRecurse(grid->cell, grid->pos, grid->size, min, max, &tri_results);
	return tri_results;
}

bool polyGridIntersectRay(PolyGrid *grid, const GMesh *mesh, const Vec3 ray_start_in, const Vec3 ray_vec, F32 *collision_time, Vec3 collision_tri_normal)
{
	bool hit = false;
	Vec3 ray_start, ray_end, new_ray_end, grid_min, grid_max;

	if (!grid->cell)
		return false;

	copyVec3(ray_start_in, ray_start);
	copyVec3(grid->pos, grid_min);
	addVec3same(grid_min, grid->size, grid_max);

	if (!pointBoxCollision(ray_start, grid_min, grid_max))
	{
		// find where ray first hits the grid bounds (if at all)
		F32 dist = sqrtf(distanceToBoxSquared(grid_min, grid_max, ray_start));
		scaleAddVec3(ray_vec, 2 * dist, ray_start, ray_end);
		if (!lineBoxCollision(ray_start, ray_end, grid_min, grid_max, new_ray_end))
			return false;
		scaleAddVec3(ray_vec, 0.001f, new_ray_end, ray_start);
	}

	// find where ray leaves the grid bounds
	scaleAddVec3(ray_vec, 2 * mesh->grid.size, ray_start, ray_end);
	scaleAddVec3(unitvec3, -2, grid_min, grid_min);
	scaleAddVec3(unitvec3, 2, grid_max, grid_max);
	if (lineBoxCollisionHollow(ray_start, ray_end, grid_min, grid_max, new_ray_end, 0))
	{
		Vec3 diff;
		F32 t = 0;
		subVec3(new_ray_end, ray_start, diff);
		if (ray_vec[0])
			MAX1(t, diff[0] / ray_vec[0]);
		if (ray_vec[1])
			MAX1(t, diff[1] / ray_vec[1]);
		if (ray_vec[2])
			MAX1(t, diff[2] / ray_vec[2]);
		scaleAddVec3(ray_vec, t, ray_start, ray_end);
	}

	*collision_time = 8e16;

	++grid->last_tag;

	while (!hit)
	{
		Vec3 cell_min, cell_max;
		PolyCell *cell = polyGridFindCell(grid, ray_start, cell_min, cell_max);
		int i;

		if (!cell)
			break;

		if (cell->tag != grid->last_tag)
		{
			cell->tag = grid->last_tag;

			for (i = 0; i < cell->tri_count; ++i)
			{
				GTriIdx *tri = &mesh->tris[cell->tri_idxs[i]];
				Vec3 tri_verts[3];
				Vec3 intersection_point;

				copyVec3(mesh->positions[tri->idx[0]], tri_verts[0]);
				copyVec3(mesh->positions[tri->idx[1]], tri_verts[1]);
				copyVec3(mesh->positions[tri->idx[2]], tri_verts[2]);

				if (triangleLineIntersect2(ray_start_in, ray_end, tri_verts, intersection_point))
				{
					Vec3 intersect_vec;
					F32 time;

					subVec3(intersection_point, ray_start_in, intersect_vec);
					time = lengthVec3(intersect_vec);
					if (time < PLANE_EPSILON)
						time = 0;

					if (time < *collision_time && (time == 0 || dotVec3(intersect_vec, ray_vec) > 0))
					{
						*collision_time = time;
						makePlaneNormal(tri_verts[0], tri_verts[1], tri_verts[2], collision_tri_normal);
						hit = true;
					}

					// no need to continue checking if collision point is at the ray start
					if (time == 0)
						break;
				}
			}
		}

		if (!hit)
		{
			// advance ray
			if (cell->children)
			{
				// if the cell has children we need to collide with their bounds
				Vec3 dv;
				subVec3(cell_max, cell_min, dv);
				scaleVec3(dv, 0.5f, dv);
				for (i = 0; i < NUMCELLS_CUBE; ++i)
				{
					Vec3 child_min, child_max;
					copyVec3(cell_min, child_min);
					copyVec3(cell_max, child_max);
					if (i & 1)
						child_min[0] += dv[0];
					else
						child_max[0] -= dv[0];
					if (i & 2)
						child_min[1] += dv[1];
					else
						child_max[1] -= dv[1];
					if (i & 4)
						child_min[2] += dv[2];
					else
						child_max[2] -= dv[2];
					if (lineBoxCollisionHollow(ray_start, ray_end, child_min, child_max, new_ray_end, 0))
						break;
				}
				if (i == NUMCELLS_CUBE)
					break;
				scaleAddVec3(ray_vec, 0.001f, new_ray_end, ray_start);
			}
			else
			{
				// collide with cell bounds to find the next ray position
				if (!lineBoxCollisionHollow(ray_start, ray_end, cell_min, cell_max, new_ray_end, 0))
					break;
				scaleAddVec3(ray_vec, 0.001f, new_ray_end, ray_start);
			}
		}
	}

	return hit;
}

static void polyGridFreeCell(PolyGrid *grid, PolyCell *cell)
{
	if (cell->children)
	{
		int i;
		for(i=0;i<NUMCELLS_CUBE;i++)
		{
			if (cell->children[i])
			{
				polyGridFreeCell(grid, cell->children[i]);
				MP_FREE_MEMBER(grid, PolyCell, cell->children[i]);
			}
		}

		MP_FREE_MEMBER(grid, PolyCellChildren, cell->children);
	}
	else if (cell->tri_idxs)
	{
		if (cell->tri_count <= NUM_POOLED_TRI_IDXS)
		{
			MP_FREE_MEMBER(grid, PolyCellTriIdxs, cell->tri_idxs);
		}
		else
		{
			free(cell->tri_idxs);
		}
	}
}

void polyGridFree(PolyGrid *grid)
{
	if (grid->cell)
	{
		polyGridFreeCell(grid, grid->cell);
		MP_FREE_MEMBER(grid, PolyCell, grid->cell);
	}

	MP_DESTROY_MEMBER(grid, PolyCell);
	MP_DESTROY_MEMBER(grid, PolyCellChildren);
	MP_DESTROY_MEMBER(grid, PolyCellTriIdxs);

	memset(grid, 0, sizeof(*grid));
}

//////////////////////////////////////////////////////////////////////////

static void writeSpaces(FILE *f, int count)
{
	int i;
	for (i = 0; i < count; i++)
		fprintf(f, " ");
}

static void writePolyCell(FILE *f, int depth, PolyCell *cell)
{
	int i;

	writeSpaces(f, depth);
	fprintf(f, "PolyCell\n");
	depth++;

	writeSpaces(f, depth);
	fprintf(f, "TriCount: %d\n", cell->tri_count);
	for (i = 0; i < cell->tri_count; i++)
	{
		writeSpaces(f, depth);
		fprintf(f, " %d\n", cell->tri_idxs[i]);
	}
	if (cell->children)
	{
		for (i = 0; i < 8; i++)
		{
			if (cell->children[i])
			{
				writePolyCell(f, depth, cell->children[i]);
			}
			else
			{
				writeSpaces(f, depth);
				fprintf(f, "EmptyChild\n");
			}
		}
	}
}

void writePolyGridDebugFile(PolyGrid *grid, char *fname)
{
	FILE *f = fopen(fname, "wt");
	if (!f)
		return;
	fprintf(f, "PolyGrid\n");
	fprintf(f, "Pos: %f, %f, %f\n", grid->pos[0], grid->pos[1], grid->pos[2]);
	fprintf(f, "Size: %f\n", grid->size);
	fprintf(f, "\n");
	writePolyCell(f, 1, grid->cell);
	fclose(f);
}

