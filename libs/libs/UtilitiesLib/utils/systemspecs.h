#pragma once
GCC_SYSTEM

typedef struct TriviaList TriviaList;

AUTO_ENUM;
typedef enum ShaderGraphFeatures
{
	SGFEAT_SM20			= 1 << 0,		//< Requires a Shader Model 2.0 card
	SGFEAT_SM20_PLUS	= 1 << 1,		//< Requires a Shader Model 2.0a/b card
	SGFEAT_SM30			= 1 << 2,		//< Requires a Shader Model 3.0 card
	SGFEAT_SM30_PLUS    = 1 << 3,		//< Requires better than strict Shader Model 3.0, even for 2 lights (think of as SM3.5)
	SGFEAT_SM30_HYPER   = 1 << 4,		//< A super-high end version of SM30_PLUS, for people who want really slow rendering.
	SGFEAT_ALL			= (1 << 5) - 1,

	SGFEAT_ALL_DEDUCED  = SGFEAT_SM20 | SGFEAT_SM20_PLUS | SGFEAT_SM30 | SGFEAT_SM30_PLUS,
} ShaderGraphFeatures;
extern StaticDefineInt ShaderGraphFeaturesEnum[];

AUTO_ENUM;
typedef enum RenderingHacks {
	REHA_NONE = 0,
} RenderingHacks;

typedef enum VideoDriverState
{
	VIDEODRIVERSTATE_OK,
	VIDEODRIVERSTATE_OLD,
	VIDEODRIVERSTATE_KNOWNBUGS,
};

AUTO_STRUCT;
typedef struct SystemSpecs
{
	U64		physicalMemoryMax;
	U64		physicalMemoryAvailable; //At time specs are fetched
	U64		virtualAddressSpace;
	char	videoCardName[256];
	int		videoCardNumeric; NO_AST
	bool	videoCardIsMobile; NO_AST
	int		videoCardVendorID;	AST( NAME(VideoCardVendorID) )
	int		videoCardDeviceID;	AST( NAME(VideoCardDeviceID) )
	F32		CPUSpeed;
	F32		RAMSpeedGBs;
	int		numVirtualCPUs;
	int		numRealCPUs;
	U32		cpuCacheSize;
	char	cpuIdentifier[256];
	char	videoDriverVersion[256];
	int		videoDriverVersionNumber; NO_AST
	U32		videoMemory;
	int		videoDriverState;
	int	    isUsingD3DDebug;
	// OS version:
	int		lowVersion; AST( NAME(OSVER0_LowVersion) )
	int		highVersion; AST( NAME(OSVER1_HighVersion) )
	int		build; AST( NAME(OSVER2_Build) )
	int		servicePackMajor; AST( NAME(OSVER3_ServicePackMajor) )
	int		servicePackMinor; AST( NAME(OSVER4_ServicePackMinor) )
	char	hostOSversion[256]; AST( NAME(OSVER5_HostVersion) ) // If running incompatibility mode, etc
//	char	bandwidth[256]; // from updater

	int		audioX64CheckSkipped;
	int		fmodVersion;
	char	audioDriverName[256];
	char	audioDriverOutput[256];
	char	audioDriverVersion[256];
	char	computerName[256];

	int		SVNBuildNumber;

	int		numMonitors;

	int		atiCrossfireGPUCount;
	int		nvidiaSLIGPUCount; // May be off, but will be >1 if the system has any SLI enabled

	RenderingHacks renderingHacks; AST( NAME(RenderingHacks) )
	bool		isFilledIn;	NO_AST
	bool		isRunningNortonAV;
	bool		isX64;
	bool		isVista;
	bool		hasSSE;
	bool		hasSSE2;
	// These two are an over-simplification.  I have added them like this so I can get SOME sense next time there's a hardware survey
	// with this in the build.  We should finish this feature.
	bool		hasSSE3;
	bool		hasSSE4;

    // these values can be modified at runtime...
	bool material_hardware_override; NO_AST
	ShaderGraphFeatures material_hardware_supported_features; AST( FLAGS )
	ShaderGraphFeatures material_supported_features; AST( FLAGS ) // Not necessarily "supported" but what's desired
	bool		isDx11Enabled;
	bool		isDx9ExEnabled;
	F32			supportedDXVersion; // Filled externally in apps that include DXVersionCheck.c
	U64			diskFree;
	U64			diskTotal;

	bool		isWine;
	bool		isTransgaming;
	char		transgamingInfo[1024];
	char		wineVersion[1024];

	bool		isUnsupportedSpecs;
} SystemSpecs;

extern SystemSpecs	system_specs;
extern bool nv_api_avail;
//is it older then 9.6? (eg no depth resolve support)
extern bool is_old_ati_catalyst;

#define VENDOR_ATI		0x1002
#define VENDOR_NV		0x10DE
#define VENDOR_INTEL	0x8086
#define VENDOR_S3G		0x5333
#define VENDOR_XBOX360	(int)0xB360ffff // Longer vendor ID to not conflict with possible PC vendor ID
#define VENDOR_WINE		(int)0xefffffff // For vanilla WINE. Not Transgaming. Cider can
										// correctly detect the GPU. WINE cannot.

void systemSpecsInit(void);
void systemSpecsUpdateMemTrivia(int bExact);
void systemSpecsUpdateString(void);
void systemSpecsGetString(char *buf, int buf_size);
void systemSpecsGetCSVString(char *buf, int buf_size);
void systemSpecsGetNameValuePairString(char *buf, int buf_size);
int getDriverVersion( char * version, int version_size, char * dllName );

__forceinline static ShaderGraphFeatures systemSpecsMaterialSupportedFeatures()
{
	return (ShaderGraphFeatures)(system_specs.material_supported_features
								 & system_specs.material_hardware_supported_features);
}

bool IsOldIntelDriverNoD3D11(void);

F32 RAMSpeedTest(void);
F32 RAMSpeedCached(void);
void systemSpecsTriviaPrintf(const char *key, FORMAT_STR const char *format, ...);
#define systemSpecsTriviaPrintf(key, format, ...) systemSpecsTriviaPrintf(key, FORMAT_STRING_CHECKED(format), __VA_ARGS__)
