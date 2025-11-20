/***************************************************************************



***************************************************************************/

#include "Color.h"


#include "GraphicsLibPrivate.h"
#include "GfxTexAtlas.h"
#include "GfxTexAtlasPrivate.h"
#include "GfxCursor.h"
#include "GfxSprite.h"
#include "GfxTextures.h"
#include "GfxDXT.h"
#include "RdrDevice.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem);); // Should be 0 at run-time

#define CURSOR_BUF_SIZE RDR_CURSOR_SIZE

static U8 *cursor_buffer;

void gfxCursorClear(void)
{
	assert(!cursor_buffer);
	cursor_buffer = calloc(CURSOR_BUF_SIZE*CURSOR_BUF_SIZE*4, 1);
}

static U32 sampleTextureInBounds(TexReadInfo *raw_info, int x, int y)
{
	return *(U32*)&raw_info->texture_data[(x + y*raw_info->width)*4];
}

static U32 sampleTexture(TexReadInfo *raw_info, F32 x, F32 y)
{
	int ix0 = (int)x;
	int iy0 = (int)y;
	int ix1 = ix0+1;
	int iy1 = iy0+1;
	F32 xparam, yparam;
	U32 c0, c1, c2;
	ix0 = CLAMP(ix0, 0, raw_info->width-1);
	iy0 = CLAMP(iy0, 0, raw_info->height-1);
	if (ix0 == x && iy0 == y) // No interpolation
		return *(U32*)&raw_info->texture_data[(ix0 + iy0*raw_info->width)*4];
	ix1 = CLAMP(ix1, 0, raw_info->width-1);
	iy1 = CLAMP(iy1, 0, raw_info->height-1);
	xparam = CLAMP(x - ix0, 0, 1);
	yparam = CLAMP(y - iy0, 0, 1);
	c0 = sampleTextureInBounds(raw_info, ix0, iy0);
	c1 = sampleTextureInBounds(raw_info, ix1, iy0);
	c2 = lerpRGBAColors(c0, c1, xparam);
	c0 = sampleTextureInBounds(raw_info, ix0, iy1);
	c1 = sampleTextureInBounds(raw_info, ix1, iy1);
	c1 = lerpRGBAColors(c0, c1, xparam);
	c0 = lerpRGBAColors(c2, c1, yparam);
	return c0;
}

void gfxCursorBlit(AtlasTex *atlas, int x0, int y0, F32 scale, Color clr)
{
	BasicTexture *tex;
	TexReadInfo *raw_info;
	int x, y;
	int i;
	F32 inv_scale = 1.f/scale;
	BasicTextureRareData *rare_data;

	// Allow negative, but clip

	assert(cursor_buffer);

	tex = texLoadRawData(atlas->name, TEX_LOAD_NOW_CALLED_FROM_MAIN_THREAD, WL_FOR_UI);
	rare_data = texGetRareData(tex);
	raw_info = SAFE_MEMBER(rare_data, bt_rawInfo);
	if (!raw_info)
		return; // Bad image specified, hopefully errored elsewhere
	assert(raw_info && raw_info->texture_data);
	verify(uncompressRawTexInfo(raw_info,textureMipsReversed(tex)));

	// loop over input pixels
	for (y=MAX(0,-y0); y<tex->height*scale && y0+y < CURSOR_BUF_SIZE; y++)
	{		
		for (x=MAX(0,-x0); x<tex->width*scale && x0+x < CURSOR_BUF_SIZE; x++)
		{
			// sample texture
			U32 cValue = sampleTexture(raw_info, x*inv_scale, y*inv_scale);
			U8 *c = (U8*)&cValue;
			U8 *cOut = &cursor_buffer[(x0+x + (y0+y)*CURSOR_BUF_SIZE)*4];
			F32 alpha = c[3] * (1.f/255.f);

			for (i=0; i<4; i++)
			{
				cOut[i] = (U8)((1-alpha) * cOut[i] + alpha*c[i]);
			}
		}
	}
	texUnloadRawData(tex);
}

void gfxCursorSet(RdrDevice *device, const char *name, int iHotX, int iHotY)
{
	RdrCursorData data = {0};
	int x, y;
	assert(cursor_buffer);
	// Swap buffer appropriately
	for (y=0; y<CURSOR_BUF_SIZE/2; y++)
	{
		Color* row0 = (Color*)&cursor_buffer[y*CURSOR_BUF_SIZE*4];
		Color* row1 = (Color*)&cursor_buffer[(CURSOR_BUF_SIZE-y-1)*CURSOR_BUF_SIZE*4];
		for (x=0; x<CURSOR_BUF_SIZE; x++, row0++, row1++)
		{
			Color t;
			Color c0 = *row0;
			Color c1 = *row1;
			t = c0;
			c0.r = c1.b;
			c0.g = c1.g;
			c0.b = c1.r;
			c0.a = c1.a;
			c1.r = t.b;
			c1.g = t.g;
			c1.b = t.r;
			c1.a = t.a;
			*row0 = c0;
			*row1 = c1;
		}
	}

	data.cursor_name = name;
	data.hotspot_x = iHotX;
	data.hotspot_y = iHotY;
	data.size_x = data.size_y = CURSOR_BUF_SIZE;
	data.data = cursor_buffer;
	cursor_buffer = NULL;
	rdrSetCursorFromData(device, &data);
}
