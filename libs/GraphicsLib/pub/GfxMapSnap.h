#ifndef _GFXMAPSNAP_H_
#define _GFXMAPSNAP_H_
GCC_SYSTEM

typedef struct GfxCameraController GfxCameraController;
typedef struct mapRoomPhoto mapRoomPhoto;
typedef struct mapRoomPhotoCell mapRoomPhotoCell;
typedef struct mapRoomPhotoAction mapRoomPhotoAction;
typedef struct RoomInstanceMapSnapAction RoomInstanceMapSnapAction;
typedef struct TextParserBinaryBlock TextParserBinaryBlock;
typedef struct AtlasTex AtlasTex;
typedef struct GfxTempSurface GfxTempSurface;
typedef struct MapSnapRoomPartitionData MapSnapRoomPartitionData;

typedef	struct mapRoomPhotoAction
{
	RoomInstanceMapSnapAction *action;
	mapRoomPhotoCell *cell;
	bool waiting_for_headshot:1;
	bool waiting_for_downsample:1;
	BasicTexture *headshot_tex;
} mapRoomPhotoAction;

typedef	struct mapRoomPhotoCell
{
	mapRoomPhoto *parent;
	int off_x, off_y;
	int w, h;
	U8 *raw_data;
	F32 world_w, world_h;
	Vec3 center;
} mapRoomPhotoCell;

typedef struct mapRoomPhoto
{
	char *prefix;
	mapRoomPhotoCell **cells;
	U8 *ov_raw_data;
	GfxTempSurface * pOverviewTextureBuffer;
	RoomInstanceMapSnapAction **action_list;
	Vec3 vPartitionMin;
	Vec3 min, max;
	F32 fFocusHeight;
	int full_w, full_h;
	bool started;
	WorldRegion *region;
	bool override_image;
	const char *override_name;
	const char *zmap_path;
	// Output data
	MapSnapRoomPartitionData * pPartitionData;
} mapRoomPhoto;

#define MAX_FRAMES_PER_PIC 100000

#define MS_OV_IMAGE_SIZE 512
#define MS_IMAGE_CUT_SIZE 512
#define MS_DEFAULT_IMAGE_CELL_SIZE 256

mapRoomPhoto* gfxMakeMapPhoto(const char *image_prefix, MapSnapRoomPartitionData * pPartitionData, WorldRegion *region, Vec3 room_min, Vec3 room_mid, Vec3 room_max, RoomInstanceMapSnapAction **action_list, const char *debug_filename, const char *debug_def_name, bool override_image, const char *override_name);
void gfxAddMapPhoto(const char *image_prefix, MapSnapRoomPartitionData * pPartitionData, Vec3 room_min, Vec3 room_mid, Vec3 room_max, RoomInstanceMapSnapAction **action_list, WorldRegion *region, const char *debug_filename, const char *debug_def_name, bool override_image, const char *override_name);
bool gfxTakeMapPhotos(const char *path, char ***output_list, bool debug_run);
typedef void (*GfxMapPhotoCellFinishedFunc)(mapRoomPhoto *room_photo, mapRoomPhotoCell *cell, int idx);
bool gfxMapPhotoProcess(GfxCameraController *camera, mapRoomPhoto *room_photo, mapRoomPhotoAction *photo_action, int *frame_count, GfxMapPhotoCellFinishedFunc cell_finished);
AtlasTex* gfxMapPhotoRegister(const char *texture_name);
void gfxMapPhotoUnregister(AtlasTex *tex);
void gfxMapRoomPhotoDestroy(mapRoomPhoto *room_photo);
void gfxUpdateMapPhoto(U8 *new_data, U8 *old_data, int data_size);
void gfxDownRezMapPhoto(U8 *data, int *data_size);

bool gfxIsTakingMapPhotos(void);

GfxTempSurface *gfxMapSnapCreateTempSurface(int iWidth, int iHeight);

void gfxMapFinishOverviewPhoto(mapRoomPhoto *map_room_photo);

LATELINK;
void gfxMapSnapSetupOptions(WorldRegion * pRegion);

LATELINK;
void gfxMapSnapPhotoScaleOptions(WorldRegion * pRegion);

extern bool g_bMapSnapUseSunIndoors;

extern float g_fMapSnapMaximumPixelsPerWorldUnit;
extern float g_fMapSnapMinimumPixelsPerWorldUnit;
extern float g_fMapSnapMaximumSizeThreshold;
extern float g_fMapSnapMinimumSizeThreshold;

#endif //_GFXMAPSNAP_H_
