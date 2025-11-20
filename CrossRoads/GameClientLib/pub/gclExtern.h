#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GCLEXTERN_H_
#define GCLEXTERN_H_

// This file gives function definitions for all the functions that must
// be defined in a solution for GameClientLib to link properly

#include "EntityExtern.h"

typedef struct GfxCameraController GfxCameraController;

// Sets up the camera
void gclExternInitCamera(GfxCameraController *camera);

// Run once per frame
void gclExternOncePerFrame(F32 elapsed, bool drawWorld);

// Initialize and cleanup entities
void gclExternInitializeEntity(Entity * ent, bool isReloading);
void gclExternCleanupEntity(Entity * ent, bool isReloading);

#endif