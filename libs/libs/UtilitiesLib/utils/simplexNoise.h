#pragma once
GCC_SYSTEM


typedef struct SimplexNoiseTable
{
	U32 uiTableSize; // must be power of 2
	U32 uiMask; // uiTableSize - 1
	U8* puiTable;
} SimplexNoiseTable;

SimplexNoiseTable* simplexNoiseTableCreate(U32 uiTableSize);
void simplexNoiseTableDestroy(SimplexNoiseTable* pTable);

// special case for 4 simultaneous 3D simplex : noise table 256
typedef U32 SimplexNoise3DTable_x4[256];

void simplexNoise3DTable_x4Init(SimplexNoise3DTable_x4 table);
void simplexNoise3D_x4(const SimplexNoise3DTable_x4 table, F32 xin, F32 yin, F32 zin, Vec4 dest);

F32 simplexNoise2D(SimplexNoiseTable* pTable, F32 xin, F32 yin);
F32 simplexNoise3D(SimplexNoiseTable* pTable, F32 xin, F32 yin, F32 zin);
F32 simplexNoise4D(SimplexNoiseTable* pTable, F32 x, F32 y, F32 z, F32 w);