#pragma once
GCC_SYSTEM

typedef struct DynFxManager DynFxManager;
typedef struct DynDrawSkeleton DynDrawSkeleton;

void dynStartup(void);
void dynSystemUpdate(F32 fDeltaTime);
void dfxTestFx( const char* fx_name );