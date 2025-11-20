#pragma once

#if _XBOX

#include "wininclude.h"

void InitXliveLib(void);
int XLSP_getInitialized(void);

#ifdef __cplusplus
extern "C" {
#endif

	extern XNADDR gxnaddr;
	extern XUID gxuid;

#ifdef __cplusplus
}
#endif

#endif
