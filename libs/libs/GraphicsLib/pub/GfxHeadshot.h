//// Interface for generating headshots in-game.  Headshots are small,
//// textures that hold a picture of someone or somethings head, for
//// display to the user.
////
//// You should call one of the gfxHeadshotCapture*() functions just
//// once when you want that headshot, and then call
//// gfxHeadshotRelease() when you are done with displaying that
//// headshot.
////
//// The exception to this rule are all the functions
//// gfxHeadshotCapture*AndSave(), which internally manage the
//// textures.  They are completely fire and forget.
#ifndef GFXHEADSHOT_H
#define GFXHEADSHOT_H
GCC_SYSTEM

// This line depends on the default FOV being 55.  If that EVER
// changes, this code needs to change too.
#define DEFAULT_FOV 55.0
#define HEADSHOT_MAX_ANIMATED 2

typedef struct BasicTexture BasicTexture;
typedef struct DynBitFieldGroup DynBitFieldGroup;
typedef struct DynSkeleton DynSkeleton;
typedef struct GfxCameraController GfxCameraController;
typedef struct GroupChildParameter GroupChildParameter;
typedef struct GroupDef GroupDef;
typedef struct GroupTrackerChildSelect GroupTrackerChildSelect;
typedef struct Material Material;
typedef struct Model Model;
typedef struct WLCostume WLCostume;
typedef struct PCFXTemp PCFXTemp;
typedef struct TextureSwap TextureSwap;

typedef void (*HeadshotNotifyF)( UserData userData );
typedef void (*HeadshotNotifyBytesF)( UserData userData, U8* bytes, int width, int height, char **ppExtraInfo );
typedef void (*HeadshotUpdateCameraF)( UserData userData, DynSkeleton *pSkel, GroupDef* pDef, Vec3 camPos, Vec3 camDir );

// At least one of these must be called every frame.
void gfxHeadshotOncePerFrame( void );
void gfxHeadshotOncePerFrameMinimal( void );

void gfxUFHeadshotPostRendering( void );
void gfxUFHeadshotCleanup( void );

bool gfxHeadshotRaisePriority( BasicTexture* texture );
bool gfxHeadshotLowerPriority( BasicTexture* texture );
bool gfxHeadshotFlagForUI(BasicTexture* pTexture);

typedef enum GfxHeadshotObjectCamType 
{
	GFX_HEADSHOT_OBJECT_AUTO,
	GFX_HEADSHOT_OBJECT_FROM_ABOVE,
} GfxHeadshotObjectCamType;

#define gfxHeadshotCaptureCostume(...) gfxHeadshotCaptureCostume_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureAnimatedCostume(...) gfxHeadshotCaptureAnimatedCostume_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureAnimatedCostumeScene(...) gfxHeadshotCaptureAnimatedCostumeScene_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureCostumeScene(...) gfxHeadshotCaptureCostumeScene_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureGroup(...) gfxHeadshotCaptureGroup_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureAnimatedGroupScene(...) gfxHeadshotCaptureAnimatedGroupScene_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureScene(...) gfxHeadshotCaptureScene_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureConstructedScene(...) gfxHeadshotCaptureConstructedScene_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureModelEx(...) gfxHeadshotCaptureModelEx_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureModel(...) gfxHeadshotCaptureModel_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureMaterial(...) gfxHeadshotCaptureMaterial_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)


BasicTexture* gfxHeadshotCaptureCostume_dbg( const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename, Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance,  float fovY, const char* sky, PCFXTemp ***peaExtraFX, HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData MEM_DBG_PARMS );
BasicTexture* gfxHeadshotCaptureAnimatedCostume_dbg(
	const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename,
	Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance,  float fovY, const char* sky
	MEM_DBG_PARMS);
BasicTexture* gfxHeadshotCaptureAnimatedCostumeScene_dbg(
	const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename,
	Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance,  HeadshotUpdateCameraF updateCameraF, UserData updateCameraData, float fovY, const char* sky, PCFXTemp ***peaExtraFX
	MEM_DBG_PARMS );
BasicTexture* gfxHeadshotCaptureCostumeScene_dbg(
		const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename,
		Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char* pcAnimKeyword, const char* pchStance,  Vec3 camPos, Vec3 camDir, F32 fAnimationFrame, float fovY, const char* sky, PCFXTemp ***peaExtraFX
		MEM_DBG_PARMS );

BasicTexture* gfxHeadshotCaptureGroup_dbg(
		const char* headshotName, int width, int height, GroupDef* group, BasicTexture* background,
		Color bgColor, GfxHeadshotObjectCamType camType, GroupChildParameter **params, const char *sky, float near_plane, bool enable_shadows, bool show_headshot_groups,
		HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData
		MEM_DBG_PARMS );
BasicTexture* gfxHeadshotCaptureAnimatedGroupScene_dbg(
		const char* headshotName, int width, int height, GroupDef* group, BasicTexture* background,
		Color bgColor, GroupChildParameter **params, const char *sky, float near_plane, bool enable_shadows, bool show_headshot_groups,
		HeadshotUpdateCameraF updateCameraF, UserData updateCameraData
		MEM_DBG_PARMS );

BasicTexture* gfxHeadshotCaptureScene_dbg( const char* headshotName, int width, int height, Vec3 camPos, Vec3 camDir MEM_DBG_PARMS );
BasicTexture* gfxHeadshotCaptureConstructedScene_dbg(
		const char* headshotName, int width, int height, GfxCameraController* camController,
		BasicTexture* background, Vec4 bgTexCoords, Color bgColor, bool allowOutlines,
		HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData MEM_DBG_PARMS );

BasicTexture* gfxHeadshotCaptureModelEx_dbg(
		const char* headshotName, int width, int height, Model* model, Material* material,
		Color bgColor, BasicTexture **eaTextureSwaps MEM_DBG_PARMS );
__forceinline BasicTexture* gfxHeadshotCaptureModel_dbg(
		const char* headshotName, int width, int height, Model* model, Material* material,
		Color bgColor MEM_DBG_PARMS )
{
	return gfxHeadshotCaptureModelEx_dbg(headshotName, width, height, model, material, bgColor, NULL MEM_DBG_PARMS_CALL );
}

BasicTexture* gfxHeadshotCaptureMaterial_dbg( const char* headshotName, int width, int height, Material* material, Color bgColor MEM_DBG_PARMS );

const char *gfxHeadshotGetCostumeName( BasicTexture* texture );
bool gfxHeadshotGetCostumeBounds( BasicTexture* texture, Vec3 Min, Vec3 Max );
bool gfxHeadshotIsAnimatedCostume( BasicTexture* texture );

#define gfxHeadshotCaptureCostumeAndSave(...) gfxHeadshotCaptureCostumeAndSave_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureCostumeSceneAndSave(...) gfxHeadshotCaptureCostumeSceneAndSave_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureGroupAndSave(...) gfxHeadshotCaptureGroupAndSave_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)
#define gfxHeadshotCaptureSceneAndSave(...) gfxHeadshotCaptureSceneAndSave_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)

void gfxHeadshotCaptureCostumeAndSave_dbg(
		const char* destFile, int width, int height, WLCostume* costume,
		BasicTexture* background, const char* framename, Color bgColor, bool forceBodyshot,
		DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance,  float fovY, const char* sky, HeadshotNotifyF notifyF, UserData notifyData,
		char **ppExtraInfo MEM_DBG_PARMS );
void gfxHeadshotCaptureCostumeSceneAndSave_dbg(
		const char* destFile, int width, int height, WLCostume* costume,
		BasicTexture* background, Color bgColor, DynBitFieldGroup* bfg, const char* pcAnimKeyword, const char* pchStance, 
		Vec3 camPos, Vec3 camDir, float fovY, const char* sky,
		HeadshotNotifyF notifyF, UserData notifyData, float animDelta, bool forceBodyshot,
		char **ppExtraInfo MEM_DBG_PARMS );
void gfxHeadshotCaptureGroupAndSave_dbg(
		const char* destFile, int width, int height, GroupDef* group,
		BasicTexture* background, Color bgColor, HeadshotNotifyF notifyF,
		UserData notifyData MEM_DBG_PARMS );
void gfxHeadshotCaptureSceneAndSave_dbg(
		const char* destFile, int width, int height, Vec3 camPos, Vec3 camDir,
		HeadshotNotifyF notifyF, UserData notifyData MEM_DBG_PARMS );
		
bool gfxHeadshotIsFinishedCostume( BasicTexture* texture );
void gfxHeadshotReleaseFreeTexture( BasicTexture* texture, bool bFreeTexture );
void gfxHeadshotRelease( BasicTexture* texture );
void gfxHeadshotReleaseAll(bool bOnlyUI);

void gfxHeadshotRefreshAll( void );

extern bool gfxHeadshotDisableValidateSize;
extern bool gfxHeadshotIsTakingResourceSnapPhotos;
extern bool gfxHeadshotAnimationIsPaused;

/// Headshot debugging interface
void gfxHeadshotDebugOncePerFrame( WLCostume* playerCostume, float realTimeElapsed );
BasicTexture* gfxHeadshotDebugTexture( void );
bool gfxGetHeadshotDebug();

extern int gfxHeadshotFramesToWait;
extern bool gfxHeadshotDoBadImageChecks;
extern bool gfxHeadshotUseSunIndoors;

bool gfxIsHeadshotServer(void);

void gfxHeadshotForceAnimatedRecreation(void);

void gfxHeadshotUpdateAnimBits(BasicTexture* texture, DynBitFieldGroup* bfg);

// ----------------------------------------------------------------------
// Headshots for binning
// ----------------------------------------------------------------------

typedef struct BakedMaterialBinningAction BakedMaterialBinningAction;
BakedMaterialBinningAction *gfxHeadshotCreateBinningAction(void);

#define gfxHeadshotCaptureMaterialForBinning(...) gfxHeadshotCaptureMaterialForBinning_dbg(__VA_ARGS__ MEM_DBG_PARMS_INIT)

BasicTexture* gfxHeadshotCaptureMaterialForBinning_dbg(
	const char* headshotName, const char *prefix, const char *uniqueName,
	int width, int height, Material* material, TextureSwap **eaTextureSwaps,
	Color bgColor, BakedMaterialBinningAction *action, char **finalPath MEM_DBG_PARMS);

bool gfxHeadshotBinningActionHasTexturesLeft(BakedMaterialBinningAction *action);

#endif
