#pragma once

typedef void (*PatchCompletionQueryingCB)(void *pUserData);

void aslPatchCompletionQuerying_Begin(const char *pPatchingNameSpace, int iSecondsBetweenQueryPerNamespace);
void aslPatchCompletionQuerying_Update(void);
void aslPatchCompletionQuerying_Query(char *pNameSpace, PatchCompletionQueryingCB pSuccessCB, void *pUserData);

