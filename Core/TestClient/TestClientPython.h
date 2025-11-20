#pragma once

#define HAVE_SNPRINTF
#undef _DEBUG
#include "Python.h"
#define _DEBUG

void TestClient_InitPython(void);
void TestClient_PythonTick(void);
void TestClient_PythonExecute(const char *script);