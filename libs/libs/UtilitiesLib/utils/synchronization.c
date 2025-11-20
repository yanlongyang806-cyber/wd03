#include "mutex.h"
#include "synchronization.h"
#include "wininclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

typedef struct CriticalSectionWrapper
{
	CRITICAL_SECTION criticalSection;
} CriticalSectionWrapper;

void InitializeCriticalSection_wrapper(CriticalSectionWrapper **wrapper)
{
	assert(wrapper);
	*wrapper = callocStruct(CriticalSectionWrapper);
	InitializeCriticalSection(&(*wrapper)->criticalSection);
}

void EnterCriticalSection_wrapper(CriticalSectionWrapper *wrapper)
{
	EnterCriticalSection(&wrapper->criticalSection);
}

void LeaveCriticalSection_wrapper(CriticalSectionWrapper *wrapper)
{
	LeaveCriticalSection(&wrapper->criticalSection);
}

void DeleteCriticalSection_wrapper(CriticalSectionWrapper **wrapper)
{
	DeleteCriticalSection(&(*wrapper)->criticalSection);
	SAFE_FREE(wrapper);
}
