
GCC_SYSTEM
#pragma once

#include "inputLibEnums.h"

typedef struct ManagedThread ManagedThread;

static const int RawInput_GenericDesktopPage = 1;

typedef enum RawInput_GenericDesktopPage_Usages
{
	RIGDPU_Mouse		=2,
	RIGDPU_Joystick		=4,
	RIGDPU_Gamepad		=5,
	RIGDPU_Keyboard		=6,
} RawInput_GenericDesktopPage_Usages;

void RawInputBeginIgnoringInput();
void RawInputStopIgnoringInput();

bool RawInputStartup(const ManagedThread *pInputLocaleSourceThread);
void RawInputUpdate();
void RawInputShutdown();
