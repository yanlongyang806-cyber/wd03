#include "GfxAlien.h"
#include "windefinclude.h"
#include "../../3rdparty/AlienFX/include/LFX2.h"
#include "sysutil.h"
#include "utils.h"
#include "utf8.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Unknown););

static HMODULE gfxAlienModule;

static struct {
	LFX2INITIALIZE lfx2InitializeFunc;
	LFX2RELEASE    lfx2ReleaseFunc;
	LFX2UPDATE     lfx2UpdateFunc;
	LFX2RESET      lfx2ResetFunc;
	LFX2LIGHT      lfx2LightFunc;
} gfxAlienFuncs = {0};

void gfxAlienInit(void)
{
	char path[CRYPTIC_MAX_PATH];
	getExecutableDir(path);
	strcat(path, "/LightFX.dll");
	gfxAlienModule = LoadLibrary_UTF8(path);

	if (gfxAlienModule)	{

		gfxAlienFuncs.lfx2InitializeFunc = (LFX2INITIALIZE)GetProcAddress(gfxAlienModule, LFX_DLL_INITIALIZE);
		gfxAlienFuncs.lfx2ResetFunc      = (LFX2RESET)GetProcAddress(gfxAlienModule, LFX_DLL_RESET);
		gfxAlienFuncs.lfx2ReleaseFunc    = (LFX2RELEASE)GetProcAddress(gfxAlienModule, LFX_DLL_RELEASE);
		gfxAlienFuncs.lfx2LightFunc      = (LFX2LIGHT)GetProcAddress(gfxAlienModule, LFX_DLL_LIGHT);
		gfxAlienFuncs.lfx2UpdateFunc     = (LFX2UPDATE)GetProcAddress(gfxAlienModule, LFX_DLL_UPDATE);

		if(gfxAlienFuncs.lfx2InitializeFunc && gfxAlienFuncs.lfx2ResetFunc) {

			LFX_RESULT initResult;
			initResult = gfxAlienFuncs.lfx2InitializeFunc();
			if(initResult == LFX_FAILURE || initResult == LFX_ERROR_NODEVS) {
				gfxAlienClose();
				return;
			}

			gfxAlienFuncs.lfx2ResetFunc();
		}
	}
}

#define COLOR_TO_ARGB(c) (((unsigned int)c.a << 24) | ((unsigned int)c.r << 16) | ((unsigned int)c.g << 8) | ((unsigned int)c.b))

void gfxAlienChangeColor(Color c)
{
	if (gfxAlienModule && gfxAlienFuncs.lfx2LightFunc && gfxAlienFuncs.lfx2UpdateFunc) {
		unsigned int color = COLOR_TO_ARGB(c);
		gfxAlienFuncs.lfx2LightFunc(LFX_ALL, color);
		gfxAlienFuncs.lfx2UpdateFunc();
	}
}

void gfxAlienClose(void)
{
	if (gfxAlienModule) {

		if(gfxAlienFuncs.lfx2ReleaseFunc) {
			gfxAlienFuncs.lfx2ReleaseFunc();
		}

		FreeLibrary(gfxAlienModule);
		gfxAlienModule = NULL;
	}

	memset(&gfxAlienFuncs, 0, sizeof(gfxAlienFuncs));
}
