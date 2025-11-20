#pragma once

void UGCProductViewer_Enter(void);
void UGCProductViewer_Leave(void);
void UGCProductViewer_BeginFrame(void);

//////////////////////////////////////////////////////////////////////
// Per-game functions exposed, in STUGCProductViewer.c or NNOUGCProductViewer.c
void UGCProductViewer_SetPurchaseResult( U32 uEntID, const char* pchProductDef, bool bSuccess );
void UGCProductViewer_Refresh( void );
void UGCProductViewer_Destroy( void );
void UGCProductViewer_OncePerFrame( void );
