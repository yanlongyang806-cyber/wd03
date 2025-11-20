#pragma once

typedef struct CriticalSectionWrapper CriticalSectionWrapper;

void InitializeCriticalSection_wrapper(CriticalSectionWrapper **wrapper);
void EnterCriticalSection_wrapper(CriticalSectionWrapper *wrapper);
void LeaveCriticalSection_wrapper(CriticalSectionWrapper *wrapper);
void DeleteCriticalSection_wrapper(CriticalSectionWrapper **wrapper);
