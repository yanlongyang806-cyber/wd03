#pragma once
GCC_SYSTEM

#include "WorldLibEnums.h"

typedef struct GMeshParsed GMeshParsed;
typedef struct GMesh GMesh;
typedef struct Model Model;

void modelFreeAllCache(WLUsageFlags unuse_type);
bool modelRebuildMaterials(void); // Returns true if any had to be rebuild

void modelFreeAllUnpacked(void);
void modelDataOncePerFrame(void);

