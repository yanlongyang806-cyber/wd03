#pragma once

typedef struct DynamicPatchInfo DynamicPatchInfo;

// Dynamic patching
DynamicPatchInfo *gclPatching_GetPatchInfo(void);
void gclPatching_SetPatchInfo(DynamicPatchInfo *info);

// gclMain functions
void gclPatching_RunPrePatch(void);

// Return true if dynamic patching should be used.
bool gclPatching_DynamicPatchingEnabled(void);
