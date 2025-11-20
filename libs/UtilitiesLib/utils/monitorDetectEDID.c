#include "monitorDetectEDID.h"
#include "estring.h"
#include "UTF8.h"

typedef signed char S8;
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;

#include <initguid.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <setupapi.h>

#pragma comment(lib, "setupapi.lib")

#define NAME_SIZE 128

DEFINE_GUID (GUID_CLASS_MONITOR, 0x4d36e96e, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);

// EDID declarations
// See: VESA ENHANCED EXTENDED DISPLAY IDENTIFICATION DATA STANDARD Release A, Revision 1, February 9, 2000

typedef U16 CompressedAscii3Char;

void DecompressAscii3Char(CompressedAscii3Char data, char out[ 4 ])
{
	out[0] = ( ( data >> 2 ) & 0x1f ) + 'A' - 1;
	out[1] = ( ( ( data & 0x3 ) << 3 ) | ( ( data >> 13 ) & 0x7 ) ) + 'A' - 1;
	out[2] = ( ( data >> 3 ) & 0x1f ) + 'A' - 1;
	out[3] = '\0';
}

#pragma pack(push)
#pragma pack(1)

typedef struct EDIDVendorProdID
{
	CompressedAscii3Char idManufacturer;
	U16 idProduct;
	U32 idSerialNum;
	U8 WeekOfManufacture;
	U8 YearOfManufacturePast1990;
} EDIDVendorProdID;

typedef struct EDIDVersion
{
	U8 Version;
	U8 Revision;
} EDIDVersion;

typedef struct EDIDBasicDisplay
{
	U8 VideoInputDefinition;
	U8 MaxHorzImageSizeCm;
	U8 MaxVertImageSizeCm;
	U8 DisplayGammaX100; // fixed point with 100 base, 0xff -> undefined here
	U8 FeatureSupportDPMS;
} EDIDBasicDisplay;

typedef struct EDIDChromaticity
{
	U8 RedGreenLowBits;
	U8 BlueWhiteLowBits;
	U8 Red_x;
	U8 Red_y;
	U8 Green_x;
	U8 Green_y;
	U8 Blue_x;
	U8 Blue_y;
	U8 White_x;
	U8 White_y;
} EDIDChromaticity;

typedef struct EDIDEstablishedTimings
{
	U8 timingIBitfield;
	U8 timingIIBitfield;
	U8 manufacturerTimingIIBitfield;
} EDIDEstablishedTimings;

typedef struct EDIDStandardTimingIdentification
{
	U8 horzActivePixels;
	U8 imageAspectRatioAndRefreshHzMinus60Bitfield;
} EDIDStandardTimingIdentification;

typedef struct EDIDStandardTimings
{
	EDIDStandardTimingIdentification ids[8];
} EDIDStandardTimings;

typedef struct EDIDDetailedTimings
{
	U16 pixelClockPer10K;

	U8 horzActiveLower8BitsPixels;
	U8 horzBlankLower8BitsPixels;
	U8 horzActiveUpper4BitsPixels : 4;
	U8 horzBlankUpper4BitsPixels : 4;

	U8 vertActiveLower8BitsPixels;
	U8 vertBlankLower8BitsPixels;
	U8 vertBlankUpper4BitsPixels : 4;
	U8 vertActiveUpper4BitsPixels : 4;

	U8 horzSyncOfsLow8BitsPixels;
	U8 horzSyncOfsPulseLow8BitsPixels;

	U8 vertSyncOfsPulseLow4BitsPixels : 4;
	U8 vertSyncOfsLow4BitsPixels : 4;

	U8 vertSyncOfsPulseHi2BitsPixels : 2;
	U8 vertSyncOfsHi2BitsPixels : 2;
	U8 horzSyncOfsPulseHi2BitsPixels : 2;
	U8 horzSyncOfsHi2BitsPixels : 2;

	U8 horzImageSizeLow8BitsMM;
	U8 vertImageSizeLow8BitsMM;
	U8 vertImageSizeHi4BitsMM : 4;
	U8 horzImageSizeHi4BitsMM : 4;

	U8 horzBorderPixels;
	U8 vertBorderLines;

	U8 flags;
} EDIDDetailedTimings;

#define EDID_DESCRIPTION_LENGTH 13

typedef struct EDIDDescriptor
{
	U16 flagAEq0;
	U8 flagBEq0;
	U8 dataTypeTag;
	U8 flagCEq0;
	S8 descriptorData[ EDID_DESCRIPTION_LENGTH ];
} EDIDDescriptor;

typedef union EDIDDetailedTimingsOrDescriptor
{
	EDIDDetailedTimings timing;
	EDIDDescriptor desc;
} EDIDDetailedTimingsOrDescriptor;

__forceinline bool edidIsDescriptor(const EDIDDetailedTimingsOrDescriptor * details)
{
	return details->desc.flagAEq0 == 0 && details->desc.flagBEq0 == 0 && details->desc.flagCEq0 == 0;
}

typedef struct EDIDExtensionFlag
{
	U8 extensionCount;
	U8 checksum;
} EDIDExtensionFlag;

typedef struct EDIDHeader
{
	byte header[ 8 ];
} EDIDHeader;

#define EDID_TOTAL_DETAILED_TIMINGS 4

typedef struct EDIDData
{
	EDIDHeader header;
	EDIDVendorProdID ids;
	EDIDVersion version;
	EDIDBasicDisplay basic_display;
	EDIDChromaticity chromaticity;
	EDIDEstablishedTimings est_timings;
	EDIDStandardTimings timings;
	EDIDDetailedTimingsOrDescriptor detailed_timings[ EDID_TOTAL_DETAILED_TIMINGS ];
	EDIDExtensionFlag extensions_and_checksum;
} EDIDData;

#pragma pack(pop)

STATIC_ASSERT( sizeof( EDIDData ) == 128 );

// Assumes hDevRegKey is valid
bool GetMonitorStatsFromEDID(const HKEY hDevRegKey, MONITORINFOEX2 * monitorInfo)
{
	DWORD dwType, AcutalValueNameLength = NAME_SIZE;
	TCHAR valueName[NAME_SIZE];
	LONG i, retValue;

	for (i = 0, retValue = ERROR_SUCCESS; retValue != ERROR_NO_MORE_ITEMS; i++)
	{
		unsigned char EDIDdata[1024];
		EDIDData edidDataTest = { 0 };

		DWORD edidsize=sizeof(EDIDdata);

		retValue = RegEnumValue(hDevRegKey, i, &valueName[0],
			&AcutalValueNameLength, NULL, &dwType,
			EDIDdata, // buffer
			&edidsize); // buffer size

		if (retValue == ERROR_SUCCESS)
		{
			if (!wcscmp(valueName, L"EDID"))
			{
				int desc_block;
				memcpy(&edidDataTest, EDIDdata, sizeof( edidDataTest ));

				// search detailed timing/descriptor blocks for an ASCII monitor description block
				for (desc_block = 0; desc_block < EDID_TOTAL_DETAILED_TIMINGS; ++desc_block)
				{
					if (edidIsDescriptor( edidDataTest.detailed_timings + desc_block ) && edidDataTest.detailed_timings[ desc_block ].desc.dataTypeTag == 0xFC )
					{
						char monname[ EDID_DESCRIPTION_LENGTH + 1 ];
						char * findLastWS;

						memcpy(monname, edidDataTest.detailed_timings[desc_block].desc.descriptorData, 13);
						monname[ EDID_DESCRIPTION_LENGTH ] = '\0';

						findLastWS = monname + EDID_DESCRIPTION_LENGTH - 1; 
						while (isspace((unsigned char)*findLastWS) && findLastWS >= monname)
						{
							*findLastWS = '\0';
							--findLastWS;
						}

						sprintf(monitorInfo->description, "%s %d:%d", monname, 
							edidDataTest.basic_display.MaxHorzImageSizeCm, 
							edidDataTest.basic_display.MaxVertImageSizeCm);
					}
				}
				return true; // EDID found
			}
		}
	}

	return false; // EDID not found
}

bool monitorGetEDIDData(const char * TargetDevID, MONITORINFOEX2 * monitorInfo)
{
	bool bRes = false;
	ULONG i = 0;
	HDEVINFO devInfo = INVALID_HANDLE_VALUE;

	devInfo = SetupDiGetClassDevsEx(
		&GUID_CLASS_MONITOR, //class GUID
		NULL, //enumerator
		NULL, //HWND
		DIGCF_PRESENT, // Flags //DIGCF_ALLCLASSES|
		NULL, // device info, create a new one.
		NULL, // machine name, local machine
		NULL);// reserved

	if (NULL == devInfo)
		return false;

	for (i=0; ERROR_NO_MORE_ITEMS != GetLastError(); ++i)
	{
		SP_DEVINFO_DATA devInfoData;
		memset(&devInfoData,0,sizeof(devInfoData));
		devInfoData.cbSize = sizeof(devInfoData);

		if (SetupDiEnumDeviceInfo(devInfo,i,&devInfoData))
		{
			HKEY hDevRegKey = SetupDiOpenDevRegKey(devInfo,&devInfoData,
				DICS_FLAG_GLOBAL, 0, DIREG_DEV,KEY_READ);

			if (!hDevRegKey || (hDevRegKey == INVALID_HANDLE_VALUE))
				continue;
			bRes = GetMonitorStatsFromEDID(hDevRegKey, monitorInfo);

			RegCloseKey(hDevRegKey);
		}
	}
	return bRes;
}

char * strcpyrange(char * dest, const char * src, const char * srcend)
{
	char * out = dest;
	while (src < srcend && (*out = *src) != '\0')
	{
		++src;
		++out;
	}
	// in case null terminator of src is past srcend
	*out = '\0';
	return dest;
}

bool monitorDetectEDIDInfo(MONITORINFOEX2 * monitorInfo)
{
	DISPLAY_DEVICE ddMon;
	DWORD devMon = 0;
	char DeviceID[ 1024 ];
	bool bFoundDevice = false;

	ZeroMemory(&ddMon, sizeof(ddMon));
	ddMon.cb = sizeof(ddMon);

	while (EnumDisplayDevices(monitorInfo->base.szDevice, devMon, &ddMon, 0) && !bFoundDevice)
	{
		if (ddMon.StateFlags & DISPLAY_DEVICE_ACTIVE &&
			!(ddMon.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER))
		{
			char *pDeviceIDTemp = NULL;
			estrStackCreate(&pDeviceIDTemp);
			UTF16ToEstring(ddMon.DeviceID, 0, &pDeviceIDTemp);

			strcpyrange(DeviceID, pDeviceIDTemp + 8, strchr(pDeviceIDTemp + 9, '\\'));
			//DeviceID = DeviceID.Mid (8, DeviceID.Find ("\\", 9) - 8);
			bFoundDevice = true;

			monitorGetEDIDData(DeviceID, monitorInfo);

			estrDestroy(&pDeviceIDTemp);
		}
		devMon++;

		ZeroMemory(&ddMon, sizeof(ddMon));
		ddMon.cb = sizeof(ddMon);
	}

	return bFoundDevice;
}
