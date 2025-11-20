#ifndef PARSEPE_H
#define PARSEPE_H

#if !PLATFORM_CONSOLE

C_DECLARATIONS_BEGIN

bool GetDebugInfo( const char *fileName, void *moduleBase, void *pModuleInfo, const char **failureReason, int *failureErrorCode );

C_DECLARATIONS_END

#endif

#endif