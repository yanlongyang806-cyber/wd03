#ifndef _GRIDPOLY_H
#define _GRIDPOLY_H
GCC_SYSTEM

#include "MemoryPool.h"

typedef struct GMesh GMesh;
typedef struct PolyCell PolyCell;
typedef PolyCell *PolyCellChildren;
typedef U16 PolyCellTriIdxs;

typedef struct PolyCell
{
	struct PolyCell **children;
	U16		*tri_idxs;
	int		tri_count;
	int		tag;
} PolyCell;

typedef struct PolyGrid
{
	PolyCell	*cell;
	Vec3		pos;			// minimum point of cell, not midpoint
	F32			size;			// width of cell (not radius)
	F32			inv_size;		// = 1 / size
	int			last_tag;		// used for searching to not double-visit a cell
	int			num_bits;		// = log2(size)
	MP_DEFINE_MEMBER(PolyCell);
	MP_DEFINE_MEMBER(PolyCellChildren);
	MP_DEFINE_MEMBER(PolyCellTriIdxs);
} PolyGrid;

int bitnum(F32 size);
void pstats(PolyCell *cell,int depth);
F32 powSnapCeil(F32 val,F32 pow);
void powCubeSnap(Vec3 v,F32 pow);
void gridPolys(PolyGrid *grid,GMesh *mesh MEM_DBG_PARMS);
PolyCell *polyGridFindCell(PolyGrid *grid, const Vec3 pos, Vec3 cell_min, Vec3 cell_max);
int *polyGridFindTris(PolyGrid *grid, const Vec3 min, const Vec3 max); // returns an int earray
bool polyGridIntersectRay(PolyGrid *grid, const GMesh *mesh, const Vec3 ray_start, const Vec3 ray_vec, F32 *collision_time, Vec3 collision_tri_normal);
void polyGridFree(PolyGrid *grid);
void writePolyGridDebugFile(PolyGrid *grid, char *fname);

#endif
