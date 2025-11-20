#if _PS3
#elif !_XBOX
#include "windefinclude.h"
#include <wbemidl.h>
#ifdef _M_X64
#pragma comment(lib, "dxguidX64.lib")
#else
#pragma comment(lib, "dxguid.lib")
#endif
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

#include <stdio.h>

#ifndef ABS
#define ABS(a)		(((a)<0) ? (-(a)) : (a))
#endif

extern "C" {

#if _PS3

unsigned long getVideoMemory(void)
{
	unsigned long mem = 256;
	return mem << 20;

}

#elif _XBOX

unsigned long getVideoMemory(void)
{
	unsigned long mem = 256;
	return mem << 20;

}

#else

unsigned long getVideoMemory(void)
{

#ifndef SAFE_RELEASE
#define SAFE_RELEASE(p)      { if (p) { (p)->Release(); (p)=NULL; } }
#endif

	HRESULT hr;
	HRESULT hrCoInitialize = S_OK;
	IWbemLocator* pIWbemLocator = NULL;
	IWbemServices* pIWbemServices = NULL;
	BSTR pNamespace = NULL;

	DWORD dwAdapterRam = 0;

	hrCoInitialize = CoInitialize( 0 );

	hr = CoCreateInstance( CLSID_WbemLocator,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWbemLocator,
		( LPVOID* )&pIWbemLocator );
	if( FAILED( hr ) )
		printf( "WMI: CoCreateInstance failed: 0x%0.8x\n", hr );

	if( SUCCEEDED( hr ) && pIWbemLocator )
	{
		// Using the locator, connect to WMI in the given namespace.
		pNamespace = SysAllocString( L"\\\\.\\root\\cimv2" );

		hr = pIWbemLocator->ConnectServer( pNamespace, NULL, NULL, 0L,
			0L, NULL, NULL, &pIWbemServices );
		if( FAILED( hr ) )
			printf( "WMI: pIWbemLocator->ConnectServer failed: 0x%0.8x\n", hr );
		if( SUCCEEDED( hr ) && pIWbemServices != NULL )
		{
			HINSTANCE hinstOle32 = NULL;

			hinstOle32 = LoadLibraryW( L"ole32.dll" );
			if( hinstOle32 )
			{
				typedef BOOL ( WINAPI* PfnCoSetProxyBlanket )( IUnknown* pProxy, DWORD dwAuthnSvc, DWORD dwAuthzSvc,
					OLECHAR* pServerPrincName, DWORD dwAuthnLevel, DWORD dwImpLevel,
					RPC_AUTH_IDENTITY_HANDLE pAuthInfo, DWORD dwCapabilities );
				PfnCoSetProxyBlanket pfnCoSetProxyBlanket = NULL;

				pfnCoSetProxyBlanket = ( PfnCoSetProxyBlanket )GetProcAddress( hinstOle32, "CoSetProxyBlanket" );
				if( pfnCoSetProxyBlanket != NULL )
				{
					// Switch security level to IMPERSONATE. 
					pfnCoSetProxyBlanket( pIWbemServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
						RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0 );
				}

				FreeLibrary( hinstOle32 );
			}

			IEnumWbemClassObject* pEnumVideoControllers = NULL;
			BSTR pClassName = NULL;

			pClassName = SysAllocString( L"Win32_VideoController" );
			assert(pClassName);

			hr = pIWbemServices->CreateInstanceEnum( pClassName, 0,
				NULL, &pEnumVideoControllers );
			if( FAILED( hr ) )
				printf( "WMI: pIWbemServices->CreateInstanceEnum failed: 0x%0.8x\n", hr );

			if( SUCCEEDED( hr ) && pEnumVideoControllers )
			{
				IWbemClassObject* pVideoControllers[10] = {0};
				DWORD uReturned = 0;
				BSTR pPropName = NULL;

				// Get the first one in the list
				pEnumVideoControllers->Reset();
				hr = pEnumVideoControllers->Next( 5000,             // timeout in 5 seconds
					10,                  // return the first 10
					pVideoControllers,
					&uReturned );
				if( FAILED( hr ) )
					printf( "WMI: pEnumVideoControllers->Next failed: 0x%0.8x\n", hr );
				if( uReturned == 0 )
					printf( "WMI: pEnumVideoControllers uReturned == 0\n" );

				VARIANT var;
				if( SUCCEEDED( hr ) )
				{
					bool bFound = false;
					for( UINT iController = 0; iController < uReturned; iController++ )
					{
						pPropName = SysAllocString( L"PNPDeviceID" );
						assert(pVideoControllers[iController]);
						hr = pVideoControllers[iController]->Get( pPropName, 0L, &var, NULL, NULL );
						if( FAILED( hr ) )
							printf( "WMI: pVideoControllers[iController]->Get PNPDeviceID failed: 0x%0.8x\n", hr );
						if( SUCCEEDED( hr ) )
						{
							if( wcsncmp( var.bstrVal, L"ROOT", 4 ) != 0 )
								bFound = true;
						}
						VariantClear( &var );
						if( pPropName )
							SysFreeString( pPropName );

						if( bFound )
						{
							pPropName = SysAllocString( L"AdapterRAM" );
							hr = pVideoControllers[iController]->Get( pPropName, 0L, &var, NULL, NULL );
							if( FAILED( hr ) )
								printf( "WMI: pVideoControllers[iController]->Get AdapterRAM failed: 0x%0.8x\n", hr );
							if( SUCCEEDED( hr ) )
							{
								dwAdapterRam = var.ulVal;
							}
							VariantClear( &var );
							if( pPropName )
								SysFreeString( pPropName );
							break;
						}
						SAFE_RELEASE( pVideoControllers[iController] );
					}
				}
			}

			if( pClassName )
				SysFreeString( pClassName );
			SAFE_RELEASE( pEnumVideoControllers );
		}

		if( pNamespace )
			SysFreeString( pNamespace );
		SAFE_RELEASE( pIWbemServices );
	}

	SAFE_RELEASE( pIWbemLocator );

	if( SUCCEEDED( hrCoInitialize ) )
		CoUninitialize();

	return dwAdapterRam;
}

#endif

int getVideoMemoryMBs(void)
{
	int i;
	int mbs = getVideoMemory() >> 20;
	for (i=0; i<=8; i++)
	{
		int value = 1 << i;
		if (ABS(mbs - value) < 0.25 * value) {
			// within a threshold, it's probably actually this value
			return value;
		}
	}
	if (mbs < 256)
		return mbs;
	// round to nearest 128
	return (mbs + 64) / 128 * 128;
}

} // extern "C"

