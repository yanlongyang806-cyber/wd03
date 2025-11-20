//Because this is an executable project, here is where all the crazy web of include
//stuff for exprCodes goes
#include "globaltypes.h"
#include "expressionfunc.h"
#include "ExpressionMinimal.h"
#include "structDefines.h"
#include "mathutil.h"
#define WANT_EXPRCODE_ENUMS_NNOGAMESERVER
typedef enum exprCodeEnum_AutoGen
{
EXPRCODE_ENUM_PLACEHOLDERZERO,
#include "autogen\NNOGameServer_exprCodes_autogen.h"
} exprCodeEnum_AutoGen;
#undef WANT_EXPRCODE_ENUMS_NNOGAMESERVER
#define WANT_EXPRCODE_STATICDEFINE_NNOGAMESERVER
StaticDefineInt enumExprCodeEnum_Autogen[]=
{
	DEFINE_INT
#include "autogen\NNOGameServer_exprCodes_autogen.h"
DEFINE_END
};
#undef WANT_EXPRCODE_STATICDEFINE_NNOGAMESERVER
#define WANT_EXPRCODE_PROTOTYPES_NNOGAMESERVER
#include "autogen\NNOGameServer_exprCodes_autogen.h"
#undef WANT_EXPRCODE_PROTOTYPES_NNOGAMESERVER
#define WANT_EXPRCODE_SWITCH_CASES_NNOGAMESERVER
ExprFuncReturnVal exprCodeEvaluate_Autogen(MultiVal** args, MultiVal* retval, ExprContext* context, char** errEString, ExprFuncDesc *pFuncDesc, void *pFuncPtr)
{
 switch (pFuncDesc->eExprCodeEnum) {
#include "autogen\NNOGameServer_exprCodes_autogen.h"
}
assertmsgf(0, "Unhandled exprCode case %s", pFuncDesc->pExprCodeName); return 0;
}
#undef WANT_EXPRCODE_SWITCH_CASES_NNOGAMESERVER
#include "ExpressionEvaluateForAutogenIncluding.c"
