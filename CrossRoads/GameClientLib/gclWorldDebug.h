#ifndef _WORLD_DEBUG_H
#define _WORLD_DEBUG_H

typedef struct GMesh GMesh;

void gclWorldDebugOncePerFrame(void);

void worldDebugAddPoint(const Vec3 point, U32 color);
void worldDebugAddLine(const Vec3 start, const Vec3 end, U32 color);
void worldDebugAddBox(const Vec3 local_min, const Vec3 local_max, const Mat4 mat, U32 color);
void worldDebugAddTri(const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled);
void worldDebugAddQuad(const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color);
void worldDebugAddGMesh(const GMesh *gmesh);
void worldDebugClear(void);

#endif