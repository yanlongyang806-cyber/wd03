#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#include "input.h"

typedef struct _DEV_BROADCAST_DEVICEINTERFACE_W DEV_BROADCAST_DEVICEINTERFACE_W;
typedef DEV_BROADCAST_DEVICEINTERFACE_W DEV_BROADCAST_DEVICEINTERFACE;

bool IsXInputDevice( const GUID* pGuidProductFromDirectInput );

void JoystickStartup(void);
void JoystickShutdown(void);

void JoystickMonitorDeviceChange(WPARAM wParam, DEV_BROADCAST_DEVICEINTERFACE *pDev);

void inpJoystickState();
