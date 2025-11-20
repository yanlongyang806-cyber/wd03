#include "rt_xFMV.h"
#include "../RdrFMVPrivate.h"
#include "xdevice.h"
#include "RdrFMV.h"
#if ENABLE_BINK
#include "bink.h"
#include "binktextures.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

extern int bink_extra_thread_index;

typedef struct RxbxFMV
{
	RdrFMV base_fmv;
#if ENABLE_BINK
	BINKTEXTURESET texture_set;
#endif
	bool bPlaying;
	bool bAutoPaused;
	bool bNeedsTextures;
	U32 resumeFrame;
} RxbxFMV;

// Called in main thread, just an allocator
RdrFMV *rxbxFMVCreate(RdrDevice *device)
{
#if ENABLE_BINK
	RxbxFMV *fmv;
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	fmv = callocStruct(RxbxFMV);
	return (RdrFMV*)fmv;
#else
	assertmsg(0, "Trying to allocate an FMV when Bink is not enabled.");
#endif
}

#if ENABLE_BINK

static void start_next_frame(RdrDeviceDX *device, RxbxFMV *fmv)
{
	//
	// Lock the textures.
	//

	if (device->d3d11_device)
		Lock_Bink_textures11( &fmv->texture_set, 0 );
	else
		Lock_Bink_textures9( &fmv->texture_set, 0 );


	//
	// Register our locked texture pointers with Bink
	//

	BinkRegisterFrameBuffers( fmv->base_fmv.bink, &fmv->texture_set.bink_buffers );

	//
	// start the first frame in the background immediately
	//

	BinkDoFrameAsync( fmv->base_fmv.bink, 0, bink_extra_thread_index ); 
}


//############################################################################
//##                                                                        ##
//## Checks and advanced the Bink video if necessary.                       ##
//##                                                                        ##
//############################################################################

static S32 Check_bink(RdrDeviceDX *device, RxbxFMV *fmv)
{
	S32 new_frame = 0;

	//
	// Is it time for a new Bink frame?
	//

	if ( ! BinkWait( fmv->base_fmv.bink ) )
	{
		//
		// is the previous frame done yet (wait for a ms)
		//   note that this logic assumes you already 
		//   have a frame decompressing the first time 
		//   this function is called
		//

		if ( BinkDoFrameAsyncWait( fmv->base_fmv.bink, 1000 ) )
		{
			//
			// do we need to skip a frame?
			//

			while ( BinkShouldSkip( fmv->base_fmv.bink ) && fmv->base_fmv.bink->FrameNum < fmv->base_fmv.bink->Frames-1)
			{
				BinkNextFrame( fmv->base_fmv.bink );
				BinkDoFrameAsync( fmv->base_fmv.bink, 0, bink_extra_thread_index );
				BinkDoFrameAsyncWait( fmv->base_fmv.bink, -1 );
			}

			//
			// Unlock the textures.
			//

			if (device->d3d11_device)
				Unlock_Bink_textures11( &fmv->texture_set, fmv->base_fmv.bink );
			else
				Unlock_Bink_textures9( device->d3d_device, &fmv->texture_set, fmv->base_fmv.bink );

			//
			// we have finished a frame, so set a flag to draw it
			//

			new_frame = 1;

			//
			// Keep playing the movie.
			//

			BinkNextFrame( fmv->base_fmv.bink );

			//
			// start decompressing the next frame
			//

			start_next_frame(device, fmv);
		}
	}

	return new_frame;
}

static void rxbxFMVReleaseForResetDirect(RdrDeviceDX *xdevice, RxbxFMV *fmv)
{
	if (fmv->bNeedsTextures)
		return; // already released

	// Wait for frame to finish
	BinkDoFrameAsyncWait(fmv->base_fmv.bink, -1);

	if (xdevice->d3d11_device)
		Unlock_Bink_textures11( &fmv->texture_set, fmv->base_fmv.bink );
	else
		Unlock_Bink_textures9( xdevice->d3d_device, &fmv->texture_set, fmv->base_fmv.bink );

	if (xdevice->d3d11_device)
		Free_Bink_textures11(&fmv->texture_set);
	else
		Free_Bink_textures9(xdevice->d3d_device, &fmv->texture_set);

	fmv->bNeedsTextures = true;
	fmv->bAutoPaused = true;
	fmv->resumeFrame = fmv->base_fmv.bink->FrameNum;
}

#endif

void rxbxFMVReleaseAllForResetDirect(RdrDeviceDX *xdevice)
{
#if ENABLE_BINK
	int i;
	for (i=0; i<eaSize(&xdevice->active_fmvs); i++)
	{
		rxbxFMVReleaseForResetDirect(xdevice, xdevice->active_fmvs[i]);
	}
#endif
}

void rxbxFMVInitDirect(RdrDevice *device, RxbxFMV **pfmv, WTCmdPacket *cmd)
{
#if ENABLE_BINK
	RxbxFMV *fmv = *pfmv;
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	assert(fmv->base_fmv.device->device == device);

	if (!xdevice->fmv_inited)
	{
		xdevice->fmv_inited = 1;

		if (xdevice->d3d11_device)
		{
			if (!Create_Bink_shaders11(xdevice->d3d11_device))
				rxbxFatalHResultErrorf(xdevice, D3DERR_DRIVERINVALIDCALL, "initializing Bink", "%s", BinkGetError());
		} else {
			if (!Create_Bink_shaders9(xdevice->d3d_device))
				rxbxFatalHResultErrorf(xdevice, D3DERR_DRIVERINVALIDCALL, "initializing Bink", "%s", BinkGetError());
		}
	}

	BinkGetFrameBuffersInfo(fmv->base_fmv.bink, &fmv->texture_set.bink_buffers);

	if (xdevice->d3d11_device)
	{
		if (!Create_Bink_textures11(xdevice->d3d11_device, xdevice->d3d11_imm_context, &fmv->texture_set))
			rxbxFatalHResultErrorf(xdevice, D3DERR_DRIVERINVALIDCALL, "creating Bink textures", "%s", BinkGetError());
	} else {
		if (!Create_Bink_textures9(xdevice->d3d_device, &fmv->texture_set))
			rxbxFatalHResultErrorf(xdevice, D3DERR_DRIVERINVALIDCALL, "creating Bink textures", "%s", BinkGetError());
	}
	fmv->bNeedsTextures = false;

	start_next_frame(xdevice, fmv);
		
	eaPush(&xdevice->active_fmvs, fmv);
#endif

}

void rxbxFMVSetParamsDirect(RdrDevice *device, RdrFMVParams *fmv_params, WTCmdPacket *cmd)
{
	RxbxFMV *fmv = (RxbxFMV*)fmv_params->fmv;
	assert(fmv->base_fmv.device->device == device);
	fmv->base_fmv.x = fmv_params->x;
	fmv->base_fmv.y = fmv_params->y;
	fmv->base_fmv.x_scale = fmv_params->x_scale;
	fmv->base_fmv.y_scale = fmv_params->y_scale;
	fmv->base_fmv.alpha_level = fmv_params->alpha_level;
}

void rxbxFMVPlayDirect(RdrDevice *device, RxbxFMV **pfmv, WTCmdPacket *cmd)
{
	RxbxFMV *fmv = *pfmv;
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	assert(fmv->base_fmv.device->device == device);
	if (!fmv->bPlaying)
	{
		fmv->bPlaying = 1;
	}
}

void rxbxFMVGoDirect(RdrDeviceDX *xdevice, RxbxFMV *fmv)
{
#if ENABLE_BINK
	assert(fmv->base_fmv.device == &xdevice->device_base);

	if (!fmv->bPlaying)
		return;

	if (fmv->bNeedsTextures)
	{
		if (xdevice->d3d11_device)
		{
			if (!Create_Bink_textures11(xdevice->d3d11_device, xdevice->d3d11_imm_context, &fmv->texture_set))
				rxbxFatalHResultErrorf(xdevice, D3DERR_DRIVERINVALIDCALL, "recreating Bink textures", "%s", BinkGetError());
		} else {
			if (!Create_Bink_textures9(xdevice->d3d_device, &fmv->texture_set))
				rxbxFatalHResultErrorf(xdevice, D3DERR_DRIVERINVALIDCALL, "recreating Bink textures", "%s", BinkGetError());
		}
		fmv->bNeedsTextures = false;

		if (fmv->bAutoPaused)
		{
			// Resume where we were
			assert(fmv->resumeFrame);
			// Jeff Roberts said we could manually do some blits here to restore the textures
			//   if we want to resume smoothly.
			if (xdevice->d3d11_device)
				Lock_Bink_textures11(&fmv->texture_set, 1);
			else
				Lock_Bink_textures9(&fmv->texture_set, 1);
			BinkRegisterFrameBuffers(fmv->base_fmv.bink, &fmv->texture_set.bink_buffers );
			BinkGoto(fmv->base_fmv.bink, 0, BINKGOTOQUICKSOUND);
			BinkGoto(fmv->base_fmv.bink, fmv->resumeFrame, 0);
			BinkDoFrameAsyncWait(fmv->base_fmv.bink, -1);
			if (xdevice->d3d11_device)
				Unlock_Bink_textures11(&fmv->texture_set, fmv->base_fmv.bink);
			else
				Unlock_Bink_textures9(xdevice->d3d_device, &fmv->texture_set, fmv->base_fmv.bink);

			start_next_frame(xdevice, fmv);

			fmv->bAutoPaused = false;
		}
	}

	Check_bink(xdevice, fmv);

	// draw
	if (xdevice->d3d11_device)
	{
		// For some reason the DX10/11 interface takes rather different parameters?
		Draw_Bink_textures11(&fmv->texture_set,
			xdevice->primary_surface.width_thread, xdevice->primary_surface.height_thread,
			fmv->base_fmv.x, fmv->base_fmv.y,
			fmv->base_fmv.x_scale * fmv->base_fmv.bink->Width / xdevice->primary_surface.width_thread,
			fmv->base_fmv.y_scale * fmv->base_fmv.bink->Height / xdevice->primary_surface.height_thread, 
			fmv->base_fmv.alpha_level, 1 );
	} else {
		Draw_Bink_textures9(xdevice->d3d_device,
			&fmv->texture_set,
			fmv->base_fmv.bink->Width, fmv->base_fmv.bink->Height,
			fmv->base_fmv.x, fmv->base_fmv.y,
			fmv->base_fmv.x_scale, fmv->base_fmv.y_scale,
			fmv->base_fmv.alpha_level, 1 );
	}

	if (fmv->base_fmv.bink->FrameNum == fmv->base_fmv.bink->Frames)
	{
		// Video is done
		fmv->bPlaying = false;
		fmv->base_fmv.bDone = true;
	}
#endif
}

void rxbxFMVCloseDirect(RdrDevice *device, RxbxFMV **pfmv, WTCmdPacket *cmd)
{
#if ENABLE_BINK
	RxbxFMV *fmv = *pfmv;
	RdrDeviceDX *xdevice = (RdrDeviceDX*)device;
	assert(fmv->base_fmv.device->device == device);

	// Wait for background to finish
	if (!fmv->bNeedsTextures)
	{
		BinkDoFrameAsyncWait(fmv->base_fmv.bink, -1);
		if (xdevice->d3d11_device)
			Free_Bink_textures11(&fmv->texture_set);
		else
			Free_Bink_textures9(xdevice->d3d_device, &fmv->texture_set);
	}

	BinkClose(fmv->base_fmv.bink);
	if (fmv->base_fmv.handleToClose)
	{
		CloseHandle(fmv->base_fmv.handleToClose);
		fmv->base_fmv.handleToClose = NULL;
	}
	eaFindAndRemove(&xdevice->active_fmvs, fmv);
	free(fmv);
#endif
}
