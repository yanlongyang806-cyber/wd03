#ifndef CURSORMODE_H
#define CURSORMODE_H
GCC_SYSTEM

typedef void (*OnClickHandler)(bool bButtonDown);
typedef void (*OnEnterExitModeHandler)();
typedef void (*OnUpdateHandler)();

void gclCursorMode_Register(SA_PARAM_NN_VALID const char *pchModeName, SA_PARAM_NN_VALID const char *pchCursorName, 
								SA_PARAM_NN_VALID OnClickHandler clickHandler, 
								SA_PARAM_OP_VALID OnEnterExitModeHandler enterHander,
								SA_PARAM_OP_VALID OnEnterExitModeHandler exitHandler,
								SA_PARAM_OP_VALID OnUpdateHandler updateHandler);

void gclCursorMode_SetDefault(const char *pchModeName);
void gclCursorMode_ChangeToDefault();
bool gclCursorMode_IsDefault();
const char* gclCursorMode_GetCurrent();

bool gclCursorMode_SetModeByName(const char *pchModeName);

void gclCursorMode_OnClick(bool bButtonDown);

void gclCursorMode_OncePerFrame();

void gclCursorModeAllowThisClick(bool bAllow);

#endif 