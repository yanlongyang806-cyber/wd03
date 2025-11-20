//////////////////////////////////////////////////////////////////////////
// Bink TODO:
//   Depending on usage: change it to render immediately before all sprites?  Or at a fixed z-pos?


//////////////////////////////////////////////////////////////////////////
// For test, I put this at the end of gclLogin_BeginFrame:
// 	{
// 		static bool bDoneOnce=false;
// 		if (!bDoneOnce)
// 		{
// 			bDoneOnce = true;
// 			gfxFMVPlayFullscreen("C:/temp/test.bik");
// 		}
// 		if (gfxFMVDone())
// 		{
// 			gfxFMVClose();
// 			gfxXYprintf(10, 10, "FMV Done.");
// 		}
// 	}


#include "RdrFMV.h"
#include "RdrFMVPrivate.h"
#include "RdrDevice.h"
#include "cpu_count.h"
#include "error.h"
#include "file.h"
#include "GlobalTypes.h"
#include "sysutil.h"
#include "UTF8.h"

#if ENABLE_BINK

#include "bink.h"
#pragma comment(lib, "binkw32.lib")

#else

#define BinkSetMemory(...)
#define BinkStartAsyncThread(...)
#define BinkSoundUseDirectSound(...)
#define BinkSetVolume(...)
#define BinkOpen(...) NULL
#define BinkGetError(...) "Bink not enabled"
#define RADLINK

#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

int bink_extra_thread_index;
static bool bink_enabled=false;
static bool bink_force_disable;

// Disables FMVs
AUTO_CMD_INT(bink_force_disable, noBink) ACMD_CMDLINE;

void * RADLINK bink_malloc(U32 size)
{
	return aligned_malloc(size, 32);
}

void RADLINK bink_free(void *mem)
{
	free(mem);
}


static void rdrFMVStartup(void)
{
	static bool doneonce=false;
	HANDLE hDLL;

	if (doneonce)
		return;
	doneonce = true;

	loadstart_printf("Initializing Bink...");

	if (bink_force_disable)
	{
		loadend_printf(" disabled on command-line.");
	}
	else if(!gConf.bEnableFMV)
	{
		loadend_printf(" disabled via global config.");
	}
	else
	{
		char binkPath[CRYPTIC_MAX_PATH];

		getExecutableDir(binkPath);
		strcat(binkPath, "/binkw32.dll");

		hDLL = LoadLibrary_UTF8(binkPath);
		if (!hDLL)
			hDLL = LoadLibrary(L"../../3rdparty/bink/binkw32.dll");

		if (!hDLL)
			bink_enabled = false;
		else
			bink_enabled = true;

		if (bink_enabled)
		{
			BinkSetMemory(bink_malloc, bink_free);

			BinkStartAsyncThread( 0, 0 );

			// we use one background thread regardless, and if we have
			//   at least two CPUs, we'll use another background one

			if ( getNumVirtualCpus() <= 1 )
			{
				bink_extra_thread_index = 0;
			}
			else
			{
				BinkStartAsyncThread( 1, 0 );
				bink_extra_thread_index = 1;
			}

			//
			// Tell Bink to use DirectSound (must be before BinkOpen)!
			//

			BinkSoundUseDirectSound( 0 );

			loadend_printf(" done.");
		} else {
			loadend_printf(" binkw32.dll not found, disabling.");
		}
	}
}

RdrFMV *rdrFMVOpen(RdrDevice *device, const char *name)
{
	rdrFMVStartup();
	if (!bink_enabled)
	{
		Errorf("Error opening FMV '%s' : Bink disabled", name);
		return NULL;
	} else {
		RdrFMV *fmv;
		HBINK bink;
		FILE *file = fileOpen(name, "rb");
		HANDLE hFile;

		if (!file)
		{
			Errorf("Error opening FMV '%s' : file failed to open", name);
			return NULL;
		}
		hFile = fileDupHandle(file);
		fileClose(file);

		bink = BinkOpen(hFile, BINKSNDTRACK | BINKNOFRAMEBUFFERS | BINKALPHA | BINKFILEHANDLE);
		if (!bink)
		{
			Errorf("Error opening FMV '%s' : %s", name, BinkGetError());
			return NULL;
		}

		fmv = device->fmvCreate(device);
		fmv->handleToClose = hFile;
		fmv->device = device;
		fmv->bink = bink;
		fmv->x_scale = fmv->y_scale = fmv->alpha_level = 1;
		wtQueueCmd(device->worker_thread, RDRCMD_FMV_INIT, &fmv, sizeof(fmv));

		return fmv;
	}
}

void rdrFMVGetSize(RdrFMV *fmv, int *w, int *h)
{
	assert(fmv);
#if ENABLE_BINK
	if (w)
		*w = fmv->bink->Width;
	if (h)
		*h = fmv->bink->Height;
#endif
}


void rdrFMVSetDrawParams(RdrFMV *fmv, F32 x, F32 y, F32 x_scale, F32 y_scale, F32 alpha_level)
{
	RdrFMVParams params = {0};
	// send to thread
	assert(fmv);
	params.fmv = fmv;
	params.x = x;
	params.y = y;
	params.x_scale = x_scale;
	params.y_scale = y_scale;
	params.alpha_level = alpha_level;
	wtQueueCmd(fmv->device->worker_thread, RDRCMD_FMV_SETPARAMS, &params, sizeof(params));
}

void rdrFMVPlay(RdrFMV *fmv, F32 x, F32 y, F32 x_scale, F32 y_scale, F32 alpha_level)
{
	rdrFMVSetDrawParams(fmv, x, y, x_scale, y_scale, alpha_level);
	// Send something to the renderer if it's not already playing
	wtQueueCmd(fmv->device->worker_thread, RDRCMD_FMV_PLAY, &fmv, sizeof(fmv));
}

void rdrFMVSetVolume(RdrFMV *fmv, F32 volume)
{
#if ENABLE_BINK
	S32 binkVolume = volume * 0x7fff;
	int i;

    for (i = 0; i < fmv->bink->NumTracks; i++)
	{
		BinkSetVolume(fmv->bink, fmv->bink->trackIDs[i], binkVolume);
	}
#endif
}

void rdrFMVClose(RdrFMV **fmv)
{
	wtQueueCmd((*fmv)->device->worker_thread, RDRCMD_FMV_CLOSE, fmv, sizeof(*fmv));
	*fmv = NULL;
}

bool rdrFMVDone(RdrFMV *fmv)
{
	return fmv->bDone;
}
