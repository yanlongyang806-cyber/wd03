#pragma once

typedef struct StructInitInfo StructInitInfo;

void StructInit_dbg(ParseTable pti[],void *structptr, const char *pCreationComment MEM_DBG_PARMS);

void DestroyStructInitInfo(StructInitInfo *pInfo, ParseTable *pTPI);

//first step of destroying... cleans up anything which might have dependencies on other parse tables
void PreDestroyStructInitInfo(StructInitInfo *pInfo, ParseTable *pTPI);


void StructInitPreAutoRunInit(void);

//for debugging purposes, tracking creating/destruction of certain TPI types
LATELINK;
ParseTable *GetParseTableForCreateBreak(void);

LATELINK;
ParseTable *GetParseTableForDestroyBreak(void);