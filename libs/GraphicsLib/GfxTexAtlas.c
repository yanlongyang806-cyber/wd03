#include "GfxTexAtlas.h"
#include "Clipper.h" //include this before GfxSprite.h when inside GraphicsLib to get the fully inlined clipper functions
#include "GfxSprite.h"
#include "earray.h"
#include "GfxTexAtlasInternal.h"
#include "HashFunctions.h"
#include "GfxDebug.h"
#include "GfxTexturesInline.h"
#include "GfxDXT.h"
#include "inputMouse.h"
#include "ReferenceSystem.h"
#include "file.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define DEBUG_SPRITE_DRAW_ORDER 0

static StashTable named_atlas_textures = NULL;
static AtlasTex **atlas_textures_array = NULL;

static StashTable texture_atlases = NULL;
static TextureAtlas **texture_atlases_array = NULL;

AtlasTex *white_tex_atlas = NULL;

static int histogram_dirty = 1;

static bool atlas_allow_async=true;
static bool atlas_allow_async_stack[1];
static int atlas_allow_async_stack_index;

bool atlas_leak_textures=false; 
// Leaks atlas textures with garbage data instead of freeing them
AUTO_CMD_INT(atlas_leak_textures, atlas_leak_textures) ACMD_CATEGORY(Debug);

void atlasPushAllowAsync(bool allowAsync)
{
	assert(atlas_allow_async_stack_index>=0);
	assert(atlas_allow_async_stack_index<ARRAY_SIZE(atlas_allow_async_stack));
	atlas_allow_async_stack[atlas_allow_async_stack_index++] = atlas_allow_async;
	atlas_allow_async = allowAsync;
	filePushDiskAccessAllowedInMainThread(!allowAsync);
}

void atlasPopAllowAsync(void)
{
	filePopDiskAccessAllowedInMainThread();
	assert(atlas_allow_async_stack_index > 0);
	assert(atlas_allow_async_stack_index<=ARRAY_SIZE(atlas_allow_async_stack));
	atlas_allow_async_stack_index--;
	atlas_allow_async = atlas_allow_async_stack[atlas_allow_async_stack_index];
}


static int cmpPriority(const void *a, const void *b)
{
	AtlasTex *texA = *((AtlasTex **)a);
	AtlasTex *texB = *((AtlasTex **)b);

	return memcmp(&texB->data->priority, &texA->data->priority, sizeof(texA->data->priority));
}

////////////////////////////////////////////////////////////////////
// Texture Atlasing Functions
////////////////////////////////////////////////////////////////////

static void gatherAtlasTextures(TextureAtlas *atlas, AtlasTex ***tex_list)
{
	TextureAtlasRow *row;
	U32 num_cells = atlas->cells_per_side;
	U32 x, y;

	for (y = 0; y < num_cells; y++)
	{
		row = &(atlas->rows[y]);

		for (x = 0; x < num_cells; x++)
		{
			if (row->child_textures[x])
				eaPush(tex_list, row->child_textures[x]);
		}
	}
}


static int isInAtlas(AtlasTex *tex, TextureAtlas *atlas)
{
	TextureAtlasRow *row;
	U32 num_cells = atlas->cells_per_side;
	U32 x, y;

	for (y = 0; y < num_cells; y++)
	{
		row = &(atlas->rows[y]);

		for (x = 0; x < num_cells; x++)
		{
			if (row->child_textures[x] == tex)
				return 1;
		}
	}

	return 0;
}

static int addToSpecificAtlas(TextureAtlas *atlas, AtlasTex *tex)
{
	U32 x, y;
	TextureAtlasRow *row;
	AtlasTexInternal *data = tex->data;
	int x_offset, y_offset;
	float one_over_size; // This was double, but it was being used as a float, as far as I can tell

	U32 num_cells = atlas->cells_per_side;
	U32 size = atlas->key.cell_size;

	if (!atlas)
		return 0;

	// look for space
	for (y = 0; y < num_cells; y++)
	{
		row = &(atlas->rows[y]);

		if (row->num_used >= num_cells)
			continue;

		for (x = 0; x < num_cells; x++)
		{
			if (!row->child_textures[x])
				break;
		}

		row->num_used++;
		row->child_textures[x] = tex;

		// space found, add to row

		x_offset = x * size;
		y_offset = y * size;

		texGenUpdateRegion(atlas->texture, data->pixdata.image, x_offset, y_offset, 0, data->actual_width + BORDER_2, data->actual_height + BORDER_2, 0, RTEX_2D, data->pixdata.pixel_format);

		atlasTexClearPixData(tex);

		one_over_size = 1.0f / ((float)atlas->atlas_size);
		tex->u_mult = ((float)data->actual_width) * one_over_size;
		tex->v_mult = ((float)data->actual_height) * one_over_size;
		tex->u_offset = ((float)x_offset + BORDER_1) * one_over_size;
		tex->v_offset = ((float)y_offset + BORDER_1) * one_over_size;

		data->state = ATS_ATLASED;
		data->parent_atlas = atlas;
		tex->atlas_sort_id = (intptr_t)atlas;

		histogram_dirty = 1;

		return 1;
	}

	return 0;
}

static void addToAtlas(AtlasTex *tex)
{
	TextureAtlasKey key;
	TextureAtlas *atlas;
	TextureAtlas* pTempAtlas;
	U32 width, height;

	ZeroStruct(&key);

	key.pixel_format = tex->data->pixdata.pixel_format;

	width = 1 << log2(tex->width + BORDER_2);
	height = 1 << log2(tex->height + BORDER_2);

	key.cell_size = MAX(width, height);
	key.atlas_index = 0;

	// look for an atlas with available space
	while (stashFindPointer(texture_atlases, &key, &pTempAtlas))
	{
		if ( addToSpecificAtlas(pTempAtlas, tex))
			return;
		key.atlas_index++;
	}

	// no atlases found with available space, create a new one
	atlas = allocTextureAtlas(&key);
	eaPush(&texture_atlases_array, atlas);
	stashAddPointer(texture_atlases, &atlas->key, atlas, false);

	// add the texture to the atlas
	{
		int ret = addToSpecificAtlas(atlas, tex);
		assertmsg(ret, "Problem in texture atlasing!");
	}
}


////////////////////////////////////////////////////////////////////
// AtlasTex Helpers
////////////////////////////////////////////////////////////////////

static int texAtlasHash(const TextureAtlas* atlas, int hashSeed)
{
	return hashCalc((const char*)&atlas->key, sizeof(atlas->key), hashSeed);
}

static int texAtlasCmp(const TextureAtlas* atlas1, const TextureAtlas* atlas2)
{
	return memcmp((unsigned char*)&atlas1->key, (unsigned char*)&atlas2->key, sizeof(atlas1->key));
}

__forceinline static void atlasInit(void)
{
	if (named_atlas_textures)
		return;

	named_atlas_textures = stashTableCreateWithStringKeys(2048, StashDefault);
	texture_atlases = stashTableCreate(100, StashDeepCopyKeys_NeverRelease, StashKeyTypeFixedSize, sizeof(TextureAtlasKey));
}

static AtlasTex *getAtlasTex(const char *sprite_name)
{
	StashElement elem;
	char buf[MAX_PATH];
	atlasInit();

	texFixName(sprite_name, SAFESTR(buf));

	stashFindElement(named_atlas_textures, buf, &elem);

	if (elem)
		return (AtlasTex *)stashElementGetPointer(elem);

	return 0;
}

bool g_is_doing_atlas_tex=false;
bool isDoingAtlasTex(void)
{
	return g_is_doing_atlas_tex;
}


static AtlasTex *addAtlasTex(const char *sprite_name, AtlasTex * tex, bool copy_white, bool load)
{

	BasicTexture *tex_header=0;

	tex_header = texFind(sprite_name, true);

	if (!tex_header)
	{
		if (copy_white)
			return atlasMakeWhiteCopy(sprite_name);
		else
			return white_tex_atlas;
	}

	if( !tex )
	{
		const char *name;
		char buf[MAX_PATH];
		tex = allocAtlasTex();

		atlasInit();

		name = texFindName(tex_header);
		texFixName(sprite_name, SAFESTR(buf));
		if (stricmp(buf, name)==0) // just a texture name, or texturename.tga
			sprite_name = name;
		else
			sprite_name = allocAddCaseSensitiveString(sprite_name); // Probably "\DynamicTexWord\String replacement"
		stashAddPointer(named_atlas_textures, sprite_name, tex, false);
		eaPush(&atlas_textures_array, tex);
		tex->name = sprite_name;
	} else {
		// This is a reload
	}

	tex->width = tex->data->actual_width = tex_header->actualTexture->width;
	tex->height = tex->data->actual_height = tex_header->actualTexture->height;

	tex->data->state = ATS_HAVE_FILENAME;

	// don't atlas it if the texture is too big or if it is a texWord or a compressed format
	if (texGetTexWord(tex_header) || (tex->height + BORDER_2) > MAX_SLOT_HEIGHT || (tex->width + BORDER_2) > MAX_SLOT_HEIGHT || gfx_state.debug.dont_atlas ||
		(tex_header->rdr_format != RTEX_BGRA_U8 && tex_header->rdr_format != RTEX_BGR_U8) ||
		tex_header->name[0] == '#' || texIsCubemap(tex_header) || texIsVolume(tex_header))
		atlasTexToBasicTexture(tex, sprite_name, load);

	histogram_dirty = 1;

	return tex;
}

static AtlasTex *genAtlasTex(U8 *bitmap, U32 width, U32 height, PixelType pixel_type, const char *name, TTFMCacheElement *font_element)
{
	AtlasTex *tex = allocAtlasTex();

	atlasInit();

	eaPush(&atlas_textures_array, tex);

	if (name && name[0])
		tex->name = allocAddString(name);
	else
		tex->name = "AUTOGEN_ATLASTEX";

	tex->width = tex->data->actual_width = width;
	tex->height = tex->data->actual_height = height;

	tex->data->is_gentex = 1;

	// don't atlas it if the texture is too big
	if ((tex->height + BORDER_2) > MAX_SLOT_HEIGHT || (tex->width + BORDER_2) > MAX_SLOT_HEIGHT || gfx_state.debug.dont_atlas || (pixel_type & PIX_DONTATLAS))
	{
		//atlasTexToBasicTextureFromPixData(tex, bitmap, width, height, pixel_type);
		atlasTexCopyPixData(tex, bitmap, width, height, pixel_type | PIX_QUEUE_FOR_BTEX);
		tex->data->pixdata.pixel_format = pixel_type;
	} else
		atlasTexCopyPixData(tex, bitmap, width, height, pixel_type);

	histogram_dirty = 1;

	return tex;
}

static AtlasTex *getOrAddAtlasTex(const char *sprite_name, bool copy_white, bool load)
{
	AtlasTex *tex = getAtlasTex(sprite_name);
	if (tex)
		return tex;

	return addAtlasTex(sprite_name, NULL, copy_white, load);
}


////////////////////////////////////////////////////////////////////
// Stats
////////////////////////////////////////////////////////////////////
#define HIST_SIZE 128
#define INC_AMOUNT 25

static void showHistogram(void)
{
	int oktorotate = 0;
	int i, x, y, idx;
	int totalSize = HIST_SIZE * HIST_SIZE * 4;
	U8 *buffer;
	static BasicTexture *histogram = 0;
	int num = eaSize(&atlas_textures_array);

	if (!histogram)
		histogram = texGenNew(HIST_SIZE, HIST_SIZE, "TextureAtlas Histogram", TEXGEN_NORMAL, WL_FOR_UTIL);

	if (histogram_dirty)
	{
		buffer = memrefAlloc(totalSize * sizeof(U8));

		for (i = 0; i < num; i++)
		{
			x = atlas_textures_array[i]->width;
			y = atlas_textures_array[i]->height;

			if (oktorotate && x < y)
			{
				int t = y;
				y = x;
				x = t;
			}

			if (x >= HIST_SIZE)
				x = HIST_SIZE - 1;
			if (y >= HIST_SIZE)
				y = HIST_SIZE - 1;

			idx = (x + (y * HIST_SIZE)) * 4;
			if (buffer[idx] < 255 - INC_AMOUNT)
				buffer[idx] += INC_AMOUNT;
		}

		for (i = 0; i < totalSize; i += 4)
		{
			buffer[i + 1] = buffer[i + 2] = buffer[i];
			buffer[i + 3] = 255;
		}

		texGenUpdate(histogram, buffer, RTEX_2D, RTEX_BGRA_U8, 1, true, false, false, true);
		memrefDecrement(buffer);

		histogram_dirty = 0;
	}

	display_sprite_tex(histogram, 20, 140, -2, 2, 2, 0xffffffff);

}

static void calcStats(int *atlas_tex_count, int *btex_count, int *not_loaded, int *atlas_total, int *atlas_used, int *atlas_unused, int *btex_total, int *btex_used, int *btex_unused)
{
	int i;
	int num = eaSize(&atlas_textures_array);

	int atex_size = 0;
	int atex_used_size = 0;
	int btex_size = 0;
	int btex_used_size = 0;
	int num_atlased = 0;
	int num_btexed = 0;
	int not_loaded_count = 0;

	AtlasTex *tex;
	TextureAtlas *atlas;

	for (i = 0; i < num; i++)
	{
		tex = atlas_textures_array[i];

		switch (tex->data->state)
		{
		case ATS_ATLASED:
			atex_used_size += tex->width * tex->height;
			num_atlased++;
			break;

		case ATS_BTEXED:
            btex_used_size += tex->width * tex->height;
			btex_size += tex->data->basic_texture->realWidth * tex->data->basic_texture->realHeight;
			num_btexed++;
			break;

		default:
			not_loaded_count++;
			break;
		}
	}

	num = eaSize(&texture_atlases_array);
	for (i = 0; i < num; i++)
	{
		atlas = texture_atlases_array[i];
		atex_size += atlas->atlas_size * atlas->atlas_size;
	}

	if (atlas_total)
		*atlas_total = atex_size;
	if (atlas_used)
		*atlas_used = atex_used_size;
	if (atlas_unused)
		*atlas_unused = atex_size - atex_used_size;

	if (btex_total)
		*btex_total = btex_size;
	if (btex_used)
		*btex_used = btex_used_size;
	if (btex_unused)
		*btex_unused = btex_size - btex_used_size;

	if (atlas_tex_count)
		*atlas_tex_count = num_atlased;
	if (btex_count)
		*btex_count = num_btexed;
	if (not_loaded)
		*not_loaded = not_loaded_count;
}

static void displayAtlasInfo(TextureAtlas *atlas)
{
	int x = 5, y = 50;
	int mouseX, mouseY;

	display_sprite_tex(atlas->texture, 380, 140, -2, 2, 2, 0xffffffff);

	gfxXYprintf(x, y++, "Atlas cell size:          %d", atlas->key.cell_size);

	if (atlas->key.pixel_format == RTEX_BGRA_U8)
		gfxXYprintf(x, y++, "Atlas format:             RGBA");
	else if (atlas->key.pixel_format == RTEX_BGR_U8)
		gfxXYprintf(x, y++, "Atlas format:             RGB");

	gfxXYprintf(x, y++, "Atlas index:              %d", atlas->key.atlas_index);

	y++;

	gfxXYprintf(x, y++, "Atlas size:               %d", atlas->atlas_size);
	gfxXYprintf(x, y++, "Atlas rows:               %d", atlas->cells_per_side);

	mousePos(&mouseX, &mouseY);
	if (1)
	{
		mouseX -= 380;
		mouseY -= 140;

		if (mouseX >= 0 && mouseY >= 0)
		{
			mouseX /= atlas->key.cell_size * 2;
			mouseY /= atlas->key.cell_size * 2;

			if (mouseX < (int)atlas->cells_per_side && mouseY < (int)atlas->cells_per_side)
			{
				AtlasTex *tex = atlas->rows[mouseY].child_textures[mouseX];

				if (tex)
				{
					y++;

					gfxXYprintf(x, y++, "AtlasTex name:            %s", tex->name);
					gfxXYprintf(x, y++, "AtlasTex size:            %d x %d", tex->width, tex->height);
					gfxXYprintf(x, y++, "AtlasTex priority:        %d", tex->data->priority);
				}
			}
		}
	}
}


////////////////////////////////////////////////////////////////////
// Public
////////////////////////////////////////////////////////////////////

void atlasDisplayStats(void)
{
	int x = 5, y = 10;
	int num_atlases, i, j;
	int atex_count, atex_used, atex_unused, atex_total, btex_count, notloaded_count;
	TextureAtlas *atlas;
	TextureAtlasKey key;

	calcStats(&atex_count, &btex_count, &notloaded_count, &atex_total, &atex_used, &atex_unused, 0, 0, 0);

	gfxXYprintf(x, y++, "Number of atlased textures:   %d / %d", atex_count, eaSize(&atlas_textures_array));
	gfxXYprintf(x, y++, "Number of btexed textures:    %d / %d", btex_count, eaSize(&atlas_textures_array));
	gfxXYprintf(x, y++, "Number of notloaded textures: %d / %d", notloaded_count, eaSize(&atlas_textures_array));
	gfxXYprintf(x, y++, "Number of texture atlases:    %d", eaSize(&texture_atlases_array));
	gfxXYprintf(x, y++, "Total atlas area:             %d", atex_total);
	gfxXYprintf(x, y++, "Used atlas area:              %d (%d%%)", atex_used, atex_total ? (100 * atex_used / atex_total) : 0);
	gfxXYprintf(x, y++, "Unused atlas area:            %d (%d%%)", atex_unused, atex_total ? (100 * atex_unused / atex_total) : 0);

	showHistogram();

	if (gfx_state.debug.atlas_display < 0)
		gfx_state.debug.atlas_display = 0;
	if (gfx_state.debug.atlas_display >= eaSize(&texture_atlases_array))
		gfx_state.debug.atlas_display = eaSize(&texture_atlases_array) - 1;


	num_atlases = eaSize(&texture_atlases_array);
	j = 0;
	for (i = 0; i < num_atlases; i++)
	{
		atlas = texture_atlases_array[i];
		if (atlas->key.atlas_index == 0)
		{
			memcpy(&key, &atlas->key, sizeof(key));

			while (atlas)
			{
				if (j == gfx_state.debug.atlas_display)
					displayAtlasInfo(atlas);
				
				j++;
				key.atlas_index++;
				if (!stashFindPointer(texture_atlases, &key, &atlas))
					atlas = NULL;
			}
		}
	}
}

void atlasDoFrame(void)
{
	int i, num_textures;
#if 0
	int j, k, num_atlases, tex_per_atlas;
	TextureAtlas *atlas, *texatlas;
	TextureAtlasKey key;
	AtlasTex **tex_list = 0;

	// rearrange atlases
	num_atlases = eaSize(&texture_atlases_array);
	for (i = 0; i < num_atlases; i++)
	{
		texatlas = texture_atlases_array[i];
		if (texatlas->key.atlas_index == 0)
		{
			tex_per_atlas = atlas->cells_per_side * atlas->cells_per_side;

			memcpy(&key, &texatlas->key, sizeof(key));

			eaSetSize(&tex_list, 0);
			
			atlas = texatlas;
			while (atlas)
			{
				gatherAtlasTextures(atlas, &tex_list);

                key.atlas_index++;
				stashFindPointer(texture_atlases, &key, &atlas);
			}

			num_textures = eaSize(&tex_list);
			qsortG(tex_list, num_textures, sizeof(*tex_list), cmpPriority);

			atlas = texatlas;
			j = 0;
			while (atlas && j < num_textures)
			{
				for (k = j; k < j + tex_per_atlas && k < num_textures; k++)
				{
					if (!isInAtlas(tex_list[k], atlas))
					{
						moveToAtlas(tex_list[k], atlas);
					}
				}

				j += tex_per_atlas;

                key.atlas_index++;
				stashFindPointer(texture_atlases, &key, &atlas);
			}
		}
	}

	eaDestroy(&tex_list);
#endif
	if (gfx_state.debug.atlas_stats)
	{
		// reset priority - currently only used for debugging
		num_textures = eaSize(&atlas_textures_array);
		for (i = 0; i < num_textures; i++)
			atlas_textures_array[i]->data->priority = 0;
	}
}

AtlasTex *atlasLoadTexture(const char *sprite_name)
{
	AtlasTex *tex;
	PERFINFO_AUTO_START_FUNC();
	tex = getOrAddAtlasTex(NULL_TO_EMPTY(sprite_name), false, true);
	PERFINFO_AUTO_STOP();
	return tex;
}

AtlasTex *atlasFindTexture(const char *sprite_name) // Doesn't load
{
	AtlasTex *tex;
	PERFINFO_AUTO_START_FUNC();
	tex = getOrAddAtlasTex(NULL_TO_EMPTY(sprite_name), false, false);
	PERFINFO_AUTO_STOP();
	return tex;
}


AtlasTex *atlasGenTextureEx(U8 *src_bitmap, U32 width, U32 height, PixelType pixel_type, const char *name, TTFMCacheElement *font_element)
{
	AtlasTex *tex;

	PERFINFO_AUTO_START_FUNC();

	tex = genAtlasTex(src_bitmap, width, height, pixel_type, name, font_element);

	PERFINFO_AUTO_STOP();

	return tex;
}

void atlasUpdateTexture(AtlasTex *tex, U8 *src_bitmap)
{
	PERFINFO_AUTO_START_FUNC();

	assertmsg(0, "atlasUpdateTexture not implemented yet!  Bug Conor if you need it.");

	// TODO

	PERFINFO_AUTO_STOP();
}

#if 0 // Not supported in RenderLib/DirectX
void atlasUpdateTextureFromScreen(AtlasTex *tex, U32 x, U32 y, U32 width, U32 height)
{
	assert(tex->data->state == ATS_BTEXED);
	tex->width = tex->data->actual_width = width;
	tex->height = tex->data->actual_height = height;

	texGenUpdateFromScreen( tex->data->basic_texture, x, y, width, height, false);
}
#endif


TexHandle atlasDemandLoadTexture(AtlasTex *tex)
{
	AtlasTexInternal *data = tex->data;
	static int prev_texid = 0;
	TexHandle tex_handle = 0;

	data->priority++;

	// demand load the unatlased texture
	if (tex->data->state == ATS_BTEXED)
	{
		tex_handle = texDemandLoadFixedInvis(tex->data->basic_texture);
		if (tex->u_mult == 0 && tex->data->basic_texture->tex_is_loaded)
		{
			if (tex->data->actual_width > tex->data->basic_texture->actualTexture->realWidth)
			{
				// Must be a MIP'd texture running at a lower MIP
				// MIP'd textures must be power of 2
				tex->u_mult = ((float)tex->data->actual_width) / ((float)pow2(tex->data->actual_width));
				tex->v_mult = ((float)tex->data->actual_height) / ((float)pow2(tex->data->actual_height));
			} else {
				tex->u_mult = ((float)tex->data->actual_width) / ((float)tex->data->basic_texture->actualTexture->realWidth);
				tex->v_mult = ((float)tex->data->actual_height) / ((float)tex->data->basic_texture->actualTexture->realHeight);
			}
		}
		goto finish_demand_load;
	}

	// atlas textures if needed
	if (data->state == ATS_HAVE_FILENAME)
	{
		TexReadInfo *rawInfo = 0;
		BasicTexture *tex_header;

		tex_header = texFind(tex->name, true);

		if (tex_header && tex_header->rdr_format != RTEX_BGRA_U8 && tex_header->rdr_format != RTEX_BGR_U8)
		{
			ErrorFilenamef(tex_header->fullname, "Invalid texture format used for sprite!");
			tex_header = NULL;
		}

		if (!tex_header)
		{
			tex_handle = texDemandLoadFixed(white_tex);
			goto finish_demand_load;
		}

		tex_header = texLoadRawData(tex->name, atlas_allow_async?TEX_LOAD_IN_BACKGROUND:TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_UI);
		if (!tex_header)
		{
			tex_handle = texDemandLoadFixed(white_tex);
			goto finish_demand_load;
		}
		if (!(tex_header->tex_is_loaded & RAW_DATA_BITMASK))
		{
			// Still loading
			// Decrease the reference count in case we never need it again, and to not leak reference counts
			texUnloadRawData(tex_header);

			// Load the low mips, if there are any, otherwise use White
			tex_handle = texDemandLoadLowMIPs(tex_header, invisible_tex);
			goto finish_demand_load;
		}

		rawInfo = texGetRareData(tex_header->actualTexture)->bt_rawInfo;
		if (!rawInfo)
		{
			// Bad texture data, probably bad patching, will probably crash later, but let's let it slide...
			tex_handle = texDemandLoadFixed(white_tex);
			goto finish_demand_load;
		}

		verify(uncompressRawTexInfo(rawInfo,textureMipsReversed(tex_header->actualTexture)));

		while (rawInfo->width < tex->data->actual_width || rawInfo->height < tex->data->actual_height)
		{
			tex->data->actual_width /= 2;
			tex->data->actual_height /= 2;
		}

		if (rawInfo->tex_format == RTEX_BGRA_U8)
			atlasTexCopyPixData(tex, rawInfo->texture_data, rawInfo->width, rawInfo->height, PIX_RGBA);
		else if (rawInfo->tex_format == RTEX_BGR_U8) // Will not ever happen
			atlasTexCopyPixData(tex, rawInfo->texture_data, rawInfo->width, rawInfo->height, PIX_RGB);

		texUnloadRawData(tex_header);
	}

	if (data->state == ATS_HAVE_PIXDATA)
		addToAtlas(tex);
	if (data->state == ATS_HAVE_PIXDATA_FOR_BTEX)
	{
		atlasTexToBasicTextureFromPixData(tex, tex->data->pixdata.image, tex->width, tex->height, tex->data->pixdata.pixel_format);
		SAFE_FREE(tex->data->pixdata.image);
		assert(tex->data->state == ATS_BTEXED);
		tex_handle = texDemandLoadFixedInvis(tex->data->basic_texture);
		goto finish_demand_load;
	}

	assert(tex->data->state == ATS_ATLASED);

	// return the texture atlas handle
	tex_handle = tex->data->parent_atlas->texture->tex_handle;
	tex->data->parent_atlas->texture->tex_last_used_time_stamp = gfx_state.client_frame_timestamp;
	if (g_needTextureBudgetInfo)
	{
		tex->data->parent_atlas->texture->loaded_data->tex_last_used_count++;
	}
    tex->data->parent_atlas->texture->loaded_data->min_draw_dist = 0.0f;
	tex->data->parent_atlas->texture->loaded_data->uv_density = GMESH_LOG_MIN_UV_DENSITY;



finish_demand_load:
#if DEBUG_SPRITE_DRAW_ORDER
	if (game_state.test1)
	{
		TextureAtlas *atlas = tex->data->state == ATS_ATLASED ? tex->data->parent_atlas : 0;
		if (atlas)
		{
			log_printf("texture_atlas", "%sTexId: 0x%04d   Size: %2d x %2d   Texture: %35s   CellSize: %2d", prev_texid != tex_handle ? "  **" : "    ", tex_handle, tex->width, tex->height, tex->name, atlas->key.cell_size);
		}
		else
			log_printf("texture_atlas", "%sTexId: 0x%04d   Size: %2d x %2d   Texture: %35s", prev_texid != tex_handle ? "  **" : "    ", tex_handle, tex->width, tex->height, tex->name);
		prev_texid = tex_handle;
	}
#endif

	return tex_handle;
}

void atlasMakeWhite(void)
{
	white_tex_atlas = allocAtlasTex();

	atlasInit();

	white_tex_atlas->name = allocAddString("white");

	stashAddPointer(named_atlas_textures, white_tex_atlas->name, white_tex_atlas, false);
	eaPush(&atlas_textures_array, white_tex_atlas);

	white_tex_atlas->data->state = ATS_HAVE_FILENAME;

	atlasTexToBasicTexture(white_tex_atlas, white_tex_atlas->name, true);

	histogram_dirty = 1;
}

AtlasTex *atlasMakeWhiteCopy(const char *name)
{
	AtlasTex *white_copy;
	white_copy = allocAtlasTex();

	atlasInit();

	name = allocAddString(name);
	stashAddPointer(named_atlas_textures, name, white_copy, false);
	eaPush(&atlas_textures_array, white_copy);

	white_copy->name = allocAddString("white");
	white_copy->data->state = ATS_HAVE_FILENAME;

	atlasTexToBasicTexture(white_copy, "white", true);

	histogram_dirty = 1;
	return white_copy;
}

void atlasFreeAll(void)
{
	int i;
	AtlasTex *tex;
	TextureAtlas *tex_atlas;

	while (tex_atlas = eaPop(&texture_atlases_array))
	{
		assert(tex_atlas->texture->flags & TEX_VOLATILE_TEXGEN);
		freeTextureAtlas(tex_atlas);
	}
	stashTableClear(texture_atlases);

#if 0
	while (tex = eaPop(&atlas_textures_array)) {
		freeAtlasTex(tex);
	}
	stashTableClear(named_atlas_textures);
	white_tex_atlas = NULL;
#else
	for (i=eaSize(&atlas_textures_array)-1; i>=0; i--) {
		tex = atlas_textures_array[i];
		if (tex->data->state == ATS_BTEXED) {
			// Fine
		} else if (tex->data->state == ATS_ATLASED) {
			tex->data->state = ATS_HAVE_FILENAME;
		} else {
			// HAVE_PIXDATA and HAVE_FILENAME, fine?
		}
	}
#endif

	atlasMakeWhite();

	gfxClearSpriteLists(); // Since they are pointing to freed atlas sprites

	// Done in gfxlib where this gets called
	//texGenDestroyVolatile(); // Free the actual data
	//fontCacheFreeAll(); // Pointing to freed atlases
}

void atlasPurge(const char *sprite_name)
{
	AtlasTex *tex = getAtlasTex(sprite_name);
	if (tex)
	{
		assertmsg(tex->data->state != ATS_ATLASED, "If it was atlased, we're not going to get the memory back!");
		stashRemovePointer(named_atlas_textures, sprite_name, NULL);
		eaFindAndRemoveFast(&atlas_textures_array, tex);
		freeAtlasTex(tex);
	}
}


void atlasTexIsReloaded(const char *sprite_name)
{
	AtlasTex *tex = getAtlasTex(sprite_name);
	if (tex) {
		addAtlasTex(sprite_name, tex, false, true);
	}
}

bool atlasTexIsFullyLoaded(const AtlasTex * tex)
{
	const AtlasTexInternal * data = NULL;
	if (!tex || !tex->data)
		return false;
	data = tex->data;
	if (data->state == ATS_ATLASED)
		return texIsFullyLoadedInline(data->parent_atlas->texture);
	if (data->state == ATS_BTEXED)
		return texIsFullyLoadedInline(data->basic_texture);
	return false;
}
