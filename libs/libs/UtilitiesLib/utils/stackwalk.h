#ifndef STACKWALK_H
#define STACKWALK_H

void stackWalkPreloadDLLs(void);
void stackWalkDumpStackToBuffer(char* buffer, int bufferSize, /* PCONTEXT */ void *stack, /* NT_TEB */ void *tib, void *boundingFramePointer, char *pCallstackReport);

// Report a problem related to stack walking.
void stackWalkWarning(void *file, const char *string, int code);

#endif
