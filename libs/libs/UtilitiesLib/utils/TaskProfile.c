#include "TaskProfile.h"
#include "wininclude.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

S64 GetCPUTicks64()
{
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return (S64)li.QuadPart;
}

F32 GetCPUTicksMsScale()
{
	S64 frequency;
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	frequency = (S64)li.QuadPart;
	return 1000.0f / frequency;
}

#include "TaskProfile_h_ast.c"
