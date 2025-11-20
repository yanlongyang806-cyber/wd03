#include "entity.h"
#include "gclWorldDebug.h"

// Cheating libification because this is code debugging the below...

// For the debug window!


AUTO_COMMAND ACMD_CLIENTCMD;
void worldDebugAddPointServer(Entity *e, const Vec3 point, U32 color)
{
	worldDebugAddPoint(point, color);
}

AUTO_COMMAND ACMD_CLIENTCMD;
void worldDebugAddLineServer(Entity *e, const Vec3 start, const Vec3 end, U32 color)
{
	worldDebugAddLine(start, end, color);
}

AUTO_COMMAND ACMD_CLIENTCMD;
void worldDebugAddBoxServer(Entity *e, const Vec3 local_min, const Vec3 local_max, const Mat4 mat, U32 color)
{
	worldDebugAddBox(local_min, local_max, mat, color);
}

AUTO_COMMAND ACMD_CLIENTCMD;
void worldDebugAddTriServer(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, U32 color, bool filled)
{
	worldDebugAddTri(p1, p2, p3, color, filled);
}

AUTO_COMMAND ACMD_CLIENTCMD;
void worldDebugAddQuadServer(Entity *e, const Vec3 p1, const Vec3 p2, const Vec3 p3, const Vec3 p4, U32 color)
{
	worldDebugAddQuad(p1, p2, p3, p4, color);
}
