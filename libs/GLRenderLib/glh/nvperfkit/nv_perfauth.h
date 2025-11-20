#ifndef _NV_PERFAUTH_H_
#define _NV_PERFAUTH_H_

#include <windows.h>

/*
 * nv_perfauth.h
 * 
 * Author: Jeff Kiel, NVIDIA Corporation
 * 
 * Purpose: Define the interface for the NVPerfKit security mechanism, nv_perfauth
 * 
 * Copyright NVIDIA Corporation 2005
 * TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED *AS IS* AND NVIDIA AND
 * AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO,
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA
 * OR ITS SUPPLIERS BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER
 * INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF
 * BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR INABILITY TO USE THIS
 * SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 */
 
#ifdef __cplusplus
extern "C" {
#endif

#define POINTER_UNINITIALIZED 0xbaadfaad

// This is the heartbeat function, called once per frame
#define NVPHB_D3D 0x00000001
#define NVPHB_OGL 0x00000002
typedef int (*NVPerfHeartBeatFunc)(unsigned long ulFlags);

// This is the nv_perfuth heartbeat function entry point (initialized when the app is authorized)
extern NVPerfHeartBeatFunc NVPerfHeartBeat;

#ifdef __cplusplus
};
#endif

// Use these convience macros to make the calls...
#define NVPerfHeartbeatD3D() {if(NVPerfHeartBeat != (NVPerfHeartBeatFunc) POINTER_UNINITIALIZED) NVPerfHeartBeat(NVPHB_D3D);}
#define NVPerfHeartbeatOGL() {if(NVPerfHeartBeat != (NVPerfHeartBeatFunc) POINTER_UNINITIALIZED) NVPerfHeartBeat(NVPHB_OGL);}

#ifndef NV_PERFAUTH_EXPORTS
// These are here to make project setup easier, with fewer changes.
// Ensures the proper nv_perfauthXX lib is linked and it's symbols
// remain in the final binary even if optimizations are turned on.
#pragma message("  Note: Including libs for NVPerfKit\n")
#if defined(_MT)
	#if defined(_DLL)
		#pragma comment( lib, "nv_perfauthMTDLL.lib" )
	#else
		#pragma comment( lib, "nv_perfauthMT.lib" )
	#endif
#else
#pragma comment( lib, "nv_perfauthST.lib" )
#endif

#ifdef _USRDLL
#pragma comment(linker, "/include:_NVInitializePerfDLL@12")
#else
#pragma comment(linker, "/include:_NVInitializePerf")
#endif
#pragma comment(linker, "/include:_NVPerfHeartBeat")
#pragma comment(linker, "/include:_G_ulNVAppSig")
#endif

#endif