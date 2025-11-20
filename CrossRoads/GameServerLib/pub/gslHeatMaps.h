#pragma once



typedef struct gslHeatMapCBHandle gslHeatMapCBHandle;

typedef bool gslHeatMapGatherDataCB(gslHeatMapCBHandle *pHandle, char **ppErrorString);
bool gslHeatMapIsPointIn(const gslHeatMapCBHandle *pHandle, const Vec3 vec);

void gslHeatMapAddPoint(gslHeatMapCBHandle *pHandle, const Vec3 vec, int iAmount);
// note: this could use a line thickness at some point
void gslHeatMapAddLine(gslHeatMapCBHandle *pHandle, const Vec3 start, const Vec3 end, int iAmount);
void gslHeatMapAddLineEx(gslHeatMapCBHandle *pHandle, const Vec3 start, const Vec3 dir, F32 len, int iAmount);

void gslHeatMapRegisterType(char *pTypeName, gslHeatMapGatherDataCB *pCB);

// Utilities to get the bounding area that will be used for the heatmap
bool gslHeatmapGetRegionBounds(SA_PARAM_NN_VALID Vec3 min, SA_PARAM_NN_VALID Vec3 max, SA_PARAM_OP_VALID const char *pRegionName);

bool gslWriteJpegHeatMap(SA_PARAM_NN_VALID const char *pOutFileName, SA_PARAM_NN_VALID const char *pTypeName, 
						 SA_PARAM_OP_VALID const char *pRegionName, 
						 int iGameUnitsPerPixel, int iPenRadius, int iYellowCutoff, int iRedCutoff, int iMinOutputPixelSize, 
						 SA_PRE_NN_OP_STR char **ppErrorString);

bool gslWriteJpegHeatMapEx(SA_PARAM_NN_VALID const char *pOutFileName, SA_PARAM_NN_VALID const char *pTypeName, 
						 const Vec3 boundingMin, const Vec3 boundingMax, 
						 int iGameUnitsPerPixel, int iPenRadius, int iYellowCutoff, int iRedCutoff, int iMinOutputPixelSize, 
						 SA_PRE_NN_OP_STR char **ppErrorString);

