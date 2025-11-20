#pragma once

#include "Organization.h"

// Forward defines
typedef struct SimpleWindow SimpleWindow;
typedef struct ShardInfo_Basic ShardInfo_Basic;

// Configuration stuffs
#define CRYPTICLAUNCHER_PATCHSERVER "patchserver." ORGANIZATION_DOMAIN
//#define CRYPTICLAUNCHER_PATCHSERVER "patchmaster.paragon." ORGANIZATION_DOMAIN
#define CRYPTICLAUNCHER_PATCHSERVER_PORT 7255
#define CRYPTICLAUNCHER_PATCHSERVER_TIMEOUT 600
#define CRYPTICLAUNCHER_AUTOUPDATE_TOKEN "CrypticLauncher"

// Connect and check to see if an autopatch update is available.
void autoPatch(U32 gameID);

// Main entry point for patching.
//   returns true if no patch is needed.
bool startPatch(SimpleWindow *window, ShardInfo_Basic *shard);

// Callback for the main action button in the launcher.
void doButtonAction(SimpleWindow *window, ShardInfo_Basic *shard);

// Tick function, call continually
//void patchTick(SimpleWindow *window);

//void connectToServer(SimpleWindow *window);

unsigned int __stdcall thread_Patch(SimpleWindow *window);