#include "DXVersionCheck.h"
#define COBJMACROS
#include <d3d11.h>
#include "systemspecs.h"
#include "memlog.h"

#pragma comment(lib, "d3d11.lib")

// The following enum and function are from the MS sample D3D11InstallHelper.dll code, to use
// their recommended method for determining D3D 11 support. It may have the same vulnerability
// to crashing in LoadLibrary, since it uses LoadLibrary, however, it does not leak
// DLL references, and does not use DXGI as part of the detection. On systems where
// D3D11 is guaranteed to be present, this will not call LoadLibrary, so should be less
// likely to experience that crash.
typedef enum D3D11IH_STATUS
{
	D3D11IH_STATUS_INSTALLED = 0,
	// Direct3D 11 is already installed

	D3D11IH_STATUS_NOT_SUPPORTED = 1,
	// Direct3D 11 not supported on this OS

	D3D11IH_STATUS_REQUIRES_UPDATE = 2,
	// Direct3D 11 is not yet installed, needs update package applied

	D3D11IH_STATUS_NEED_LATEST_SP = 3,
	// Direct3D 11 cannot be installed on this system without a Service Pack update

	D3D11IH_STATUS_MAX
} D3D11IH_STATUS;

//--------------------------------------------------------------------------------------
// Checks the system for the current status of the Direct3D 11 Runtime.
//--------------------------------------------------------------------------------------
HRESULT CheckDirect3D11Status( UINT *pStatus )
{
	OSVERSIONINFOEX osinfo;
	HMODULE hd3d = INVALID_HANDLE_VALUE;

	if ( !pStatus )
		return E_INVALIDARG;

	// OS Version check tells us most of what we need to know
	osinfo.dwOSVersionInfoSize = sizeof(osinfo);
	if ( !GetVersionEx( (OSVERSIONINFO*)&osinfo ) )
	{
		HRESULT hr = HRESULT_FROM_WIN32( GetLastError() );
		return hr;
	}

	if ( osinfo.dwMajorVersion > 6
		|| ( osinfo.dwMajorVersion == 6 && osinfo.dwMinorVersion >= 1 ) )
	{
		// Windows 7/Server 2008 R2 (6.1) and later versions of OS already have Direct3D 11
		*pStatus = D3D11IH_STATUS_INSTALLED;
		return S_OK;
	}

	if ( osinfo.dwMajorVersion < 6 )
	{
		// Windows XP, Windows Server 2003, and earlier versions of OS do not support Direct3D 11
		*pStatus = D3D11IH_STATUS_NOT_SUPPORTED;
		return S_OK;
	}

	// We should only get here for version number 6.0

	if ( osinfo.dwBuildNumber > 6002 )
	{
		// Windows Vista/Server 2008 Service Packs after SP2 should already include Direct3D 11
		*pStatus = D3D11IH_STATUS_INSTALLED;
		return S_OK;
	}

	if ( osinfo.dwBuildNumber < 6002 )
	{
		// Windows Vista/Server 2008 SP2 is a prerequisite
		*pStatus = D3D11IH_STATUS_NEED_LATEST_SP;
		return S_OK;
	}

	// Should only get here for Windows Vista or Windows Server 2008 SP2 (6.0.6002)

	hd3d = LoadLibrary( L"D3D11.DLL" );
	if ( hd3d )
	{
		FreeLibrary( hd3d );

		// If we find D3D11, we'll assume the Direct3D 11 Runtime is installed
		// (incl. Direct3D 11, DXGI 1.1, WARP10, 10level9, Direct2D, DirectWrite, updated Direct3D 10.1)

		*pStatus = D3D11IH_STATUS_INSTALLED;
		return S_OK;
	}
	else
	{
		// Did not find the D3D11.DLL, so we need KB971644

		// Verify it is a supported architecture for KB971644
		SYSTEM_INFO sysinfo;
		GetSystemInfo( &sysinfo );

		switch( sysinfo.wProcessorArchitecture )
		{
		case PROCESSOR_ARCHITECTURE_INTEL:
		case PROCESSOR_ARCHITECTURE_AMD64:
			*pStatus = D3D11IH_STATUS_REQUIRES_UPDATE;
			break;

		default:
			*pStatus = D3D11IH_STATUS_NOT_SUPPORTED;
			break;
		}

		return S_OK;
	}
}

static bool HasDX11Installed(void)
{
	HMODULE d3d11_module;
	d3d11_module = LoadLibrary(L"d3d11.dll");
	if (d3d11_module != NULL)
	{
		ANALYSIS_ASSUME(d3d11_module != NULL);
		FreeLibrary(d3d11_module);
	}
	return d3d11_module != NULL;
}

static D3D_FEATURE_LEVEL GetD3D11FeatureLevel(void)
{
	HRESULT hr;
	D3D_FEATURE_LEVEL feature_levels[] =
	{
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1,
	};
	int feature_levels_size = ARRAY_SIZE(feature_levels);
	D3D_FEATURE_LEVEL feature_level;

	// Don't specify the device or context pointers to just query the feature level
	// support without creating a device & context. This is because the
	// device release sometimes crashes [NNO-14237]. Additionally, we don't need the
	// device or context here, so creating them is just a waste.
	hr = D3D11CreateDevice(
		NULL,
		D3D_DRIVER_TYPE_HARDWARE,
		NULL,
		D3D11_CREATE_DEVICE_SINGLETHREADED,
		feature_levels,
		feature_levels_size,
		D3D11_SDK_VERSION,
		NULL,
		&feature_level,
		NULL
		);
	if  (FAILED(hr))
	{
		return 0;
	}
	return feature_level;
}

F32 GetSupportedDX11Version(void)
{
	D3D_FEATURE_LEVEL feature_level = 0x0100;
	UINT dx11_status = D3D11IH_STATUS_NOT_SUPPORTED;
	HRESULT hrMSDX11Check = S_OK;

	hrMSDX11Check = system_specs.isDx11Enabled ? CheckDirect3D11Status(&dx11_status) : S_OK;
	if (FAILED(hrMSDX11Check))
	{
		// Yikes, Windows version check failed!
		// All XP code paths, I think
		feature_level = 0x0100;
	}
	else
	if (dx11_status == D3D11IH_STATUS_INSTALLED)
	{
		// skip check (known to crash) on certain Intel drivers
		if (IsOldIntelDriverNoD3D11())
		{
			feature_level = 0x0180;
		}
		else
			feature_level = GetD3D11FeatureLevel();
	}
	else
	{
		if (IsOldIntelDriverNoD3D11())
		{
			feature_level = 0x0180;
		}
		else
		if (!system_specs.isDx11Enabled)
		{
			// DX11 code path disabled by command-line switch
			feature_level = 0x0200;
		}
		else
		if (dx11_status == D3D11IH_STATUS_NOT_SUPPORTED)
		{
			// All XP code paths, I think
			feature_level = 0x0100;
		}
		else
		if (dx11_status == D3D11IH_STATUS_REQUIRES_UPDATE)
		{
			// Vista but needs the special update
			// No DX11 installed, might still be a DX 10 or 11 card
			feature_level = 0x0200;
		}
		else
		if (dx11_status == D3D11IH_STATUS_NEED_LATEST_SP)
		{
			// Vista but needs the service pack and special update
			// No DX11 installed, might still be a DX 10 or 11 card
			feature_level = 0x0200;
		}
	}
	
	return (feature_level >> 12) + ((feature_level & 0xfff) >> 8) / 10.0f;
}
