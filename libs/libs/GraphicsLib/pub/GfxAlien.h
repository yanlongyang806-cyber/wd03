#pragma once

#include "Color.h"

//Called once to initialize the Alienware keyboard and mouse color modification system
void gfxAlienInit(void);
//The alpha value determines the brightness of the color selected
void gfxAlienChangeColor(Color c);
//Called once when closing the application to clean up
void gfxAlienClose(void);