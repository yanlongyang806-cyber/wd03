#include "ExpressionFunc.h"
#include "Expression.h"
#include "Entity.h"
#include "earray.h"
#include "textparser.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_COMMAND ACMD_PRIVATE ACMD_SERVERCMD;
void exprEdInitSendFunctions(Entity *e)
{
	ExprFuncDescContainer *container = exprGetAllFuncs();
	ClientCmd_exprEdInitAddFunctions(e, container);
	eaClear(&container->funcs);
	StructDestroy(parse_ExprFuncDescContainer, container);
}