#pragma once

typedef struct DirectionalIndicatorFX_State
{
	int iShow;
	F32 fAlpha;
} DirectionalIndicatorFX_State;

extern DirectionalIndicatorFX_State difx_State;
extern DirectionalIndicatorFX_State difx_DefaultState;

bool DirectionalIndicatorFX_Enabled(void);
void DirectionalIndicatorFX_Destroy();
void DirectionalIndicatorFX_OncePerFrame();
