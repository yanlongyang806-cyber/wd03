#include "Clipper.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("GfxClipper.h", BUDGET_UISystem););

MemoryPool MP_NAME(Clipper2D) = 0;
Clipper2D** ClipperStack;

//LM: The functions all inline now for performance 
