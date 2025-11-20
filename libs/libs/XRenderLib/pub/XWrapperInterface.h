#pragma once

#if !PLATFORM_CONSOLE

#include "../../XWrapper/XWrapper.h"

// Compiles a shader, on the PC for the Xbox

int XWrapperCompileShader(XWrapperCompileShaderData *data);

#endif