#include "WorldEditorExporter.h"
#include "AutoGen/WorldEditorExporter_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

const char* WRL_TYPE_STRINGS[] = {"Transform"};

void Wrl_SetDefType(WrlDef *wrlDef, WrlDefType wrlDefType) {
	wrlDef->defType = WRL_TYPE_STRINGS[wrlDefType];
}

WrlDef* Wrl_CreateDef(WrlDefType wrlDefType) {
	WrlDef *wrlDef = StructCreate(parse_WrlDef);
	Wrl_SetDefType(wrlDef, wrlDefType);
	return wrlDef;
}