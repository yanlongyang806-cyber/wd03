#include "SimpleCpuUsage.h"
#include "gclSimpleCpuUsage.h"

#include "Entity.h"
#include "Player.h"
#include "gclEntity.h"

#include "GraphicsLib.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"

#include "StringCache.h"

#include "cmdparse.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static S32 s_SimpleCpuGraphEnabled;
AUTO_CMD_INT(s_SimpleCpuGraphEnabled, simpleCpuGraph) ACMD_CALLBACK(gclSimpleCpuGraphEnabledChanged) ACMD_ACCESSLEVEL(3) ACMD_CLIENTONLY ACMD_HIDE;
void gclSimpleCpuGraphEnabledChanged(void)
{
	if(s_SimpleCpuGraphEnabled)
		ServerCmd_SetEntityDesiringCapturedFrames();
	else
		ServerCmd_ClearEntityDesiringCapturedFrames();
}

static S32 s_SimpleCpuGraphPause;
AUTO_CMD_INT(s_SimpleCpuGraphPause, simpleCpuGraphPause) ACMD_CALLBACK(gclSimpleCpuGraphPauseChanged) ACMD_ACCESSLEVEL(3) ACMD_CLIENTONLY ACMD_HIDE;
void gclSimpleCpuGraphPauseChanged(void)
{
	if(s_SimpleCpuGraphEnabled)
	{
		if(s_SimpleCpuGraphPause)
			ServerCmd_PauseCapturing();
		else
			ServerCmd_ResumeCapturing();
	}
}

static S32 s_SimpleCpuGraphDrawLabels = 1;
AUTO_CMD_INT(s_SimpleCpuGraphDrawLabels, simpleCpuGraphDrawLabels) ACMD_ACCESSLEVEL(3) ACMD_CLIENTONLY ACMD_HIDE;

static S32 s_SimpleCpuGraphDrawLegend = 1;
AUTO_CMD_INT(s_SimpleCpuGraphDrawLegend, simpleCpuGraphDrawLegend) ACMD_ACCESSLEVEL(3) ACMD_CLIENTONLY ACMD_HIDE;

static S32 s_SimpleCpuGraphTargetCycles = 20000000 / 300; // 20M cycles total on a 300 player map
AUTO_CMD_INT(s_SimpleCpuGraphTargetCycles, simpleCpuGraphTargetCycles) ACMD_ACCESSLEVEL(3) ACMD_CLIENTONLY ACMD_HIDE;

static S32 s_SimpleCpuGraphAlpha = 0xc0;
AUTO_CMD_INT(s_SimpleCpuGraphAlpha, simpleCpuGraphAlpha) ACMD_ACCESSLEVEL(3) ACMD_CLIENTONLY ACMD_HIDE;

static S32 s_SimpleCpuGraphScale = 2;
AUTO_CMD_INT(s_SimpleCpuGraphScale, simpleCpuGraphScale) ACMD_CALLBACK(gclSimpleCpuGraphScaleChanged) ACMD_ACCESSLEVEL(3) ACMD_CLIENTONLY ACMD_HIDE;
void gclSimpleCpuGraphScaleChanged(void)
{
	s_SimpleCpuGraphScale = MIN(15, MAX(2, s_SimpleCpuGraphScale));
}

void gclSimpleCpu_DrawFrames(void)
{
	static S32 s_MaxCycles = 0;
	S32 s_ThisMaxCycles = 0;

	Entity *pEntity = entActivePlayerPtr();
	F32		dividend; // cycles divided by this value gives actual pixel height
	S32		scaleWidth = 2;
	S32		w;
	S32		h;
	S32		wBox;
	S32		hBox = 50 * s_SimpleCpuGraphScale;
	F32		z = 2000.f;
	U32		alpha;
	S32		yBoxTop;
	S32		yBoxBottom;
	S32		yBoxMiddle;
	U32		maxCount;
	U32		shownCount;

	if(!pEntity || !pEntity->pPlayer || !pEntity->pPlayer->pSimpleCpuData) return;

	gfxGetActiveDeviceSize(&w, &h);

	maxCount = (w - 20) / scaleWidth;
	shownCount = MIN(maxCount, SIMPLE_CPU_DATA_MAX_FRAME_COUNT);

	wBox = shownCount * scaleWidth;

	s_SimpleCpuGraphAlpha = MINMAX(s_SimpleCpuGraphAlpha, 64, 0xff);
	alpha = s_SimpleCpuGraphAlpha << 24;

	yBoxBottom = h - 110; // 110 is to give room for the netTimingGraph at the bottom of the screen, which is 100 pixels tall
	yBoxTop = yBoxBottom - hBox;
	yBoxMiddle = (yBoxBottom - yBoxTop) / 2;

	gfxDrawQuadARGB(w / 2 - wBox / 2 - 10,
		yBoxTop,
		w / 2 + wBox / 2 + 10,
		yBoxBottom,
		z,
		alpha);

	//dividend = pEntity->pPlayer->pSimpleCpuData->fMaxCyclesFor30Fps / (yBoxBottom - yBoxTop);
	//dividend = s_SimpleCpuGraphTargetCycles / (yBoxBottom - yBoxTop);
	dividend = MAX(s_MaxCycles, s_SimpleCpuGraphTargetCycles) / (yBoxBottom - yBoxTop - 20);

	FOR_BEGIN(i, (S32)shownCount);
	{
		int index = (pEntity->pPlayer->pSimpleCpuData->iNextFrameIndex + i + (SIMPLE_CPU_DATA_MAX_FRAME_COUNT - shownCount)) % SIMPLE_CPU_DATA_MAX_FRAME_COUNT;
		SimpleCpuFrameData *f = pEntity->pPlayer->pSimpleCpuData->eaSimpleCpuFrameData[index];

		S32 x = w / 2 - wBox / 2 + i * scaleWidth;

		S64 cycles = 0; // this is incremented by each cycle count to give an actual, fractional base y position to draw the next thread frame bar
		S32 y1 = 0;
		
		FOR_EACH_IN_EARRAY_FORWARDS(f->eaSimpleCpuThreadData, SimpleCpuThreadData, pSimpleCpuThreadData)
		{
			if(pSimpleCpuThreadData->s64TimerCpuTicks)
			{
				S32 rgb = 0xffffff;

				S32 y2 = pSimpleCpuThreadData->s64TimerCpuTicks / dividend + y1;

				switch(pSimpleCpuThreadData->thread)
				{
					case SIMPLE_CPU_USAGE_THREAD_GAMESERVER_MAIN: rgb = 0x00ff00; break;
					case SIMPLE_CPU_USAGE_THREAD_GAMESERVER_SENDTOCLIENT: rgb = 0xff00ff; break;
					case SIMPLE_CPU_USAGE_THREAD_MMBG_MAIN: rgb = 0xff0000; break;
					case SIMPLE_CPU_USAGE_THREAD_WORLDCOLL_MAIN: rgb = 0x00ffff; break;
					case SIMPLE_CPU_USAGE_THREAD_PKTSEND: rgb = 0xffff00; break;
				}

				gfxDrawQuadARGB(x,
					MAX(yBoxBottom - y1, 0),
					x + scaleWidth,
					MAX(yBoxBottom - y2, 0),
					z,
					alpha | rgb);

				cycles += pSimpleCpuThreadData->s64TimerCpuTicks;
				y1 = y2;
			}
		}
		FOR_EACH_END;

		s_ThisMaxCycles = MAX(s_ThisMaxCycles, cycles);
		s_MaxCycles = MAX(s_MaxCycles, cycles);
	}
	FOR_END;

	FOR_BEGIN(i, (S32)shownCount + 1);
	{
		S32 x = w / 2 - wBox / 2 + i * scaleWidth;

		if(!(i % 5)){
			gfxDrawQuadARGB(x,
				yBoxTop + 10,
				x + 1,
				yBoxBottom - 10,
				z,
				0x08ffffff);
		}
	}
	FOR_END;

	FOR_BEGIN(i, hBox / (2 * s_SimpleCpuGraphScale * 5));
	{
		S32 yOffset = i * s_SimpleCpuGraphScale * 5;

		if(yOffset > hBox / 2 - 10){
			break;
		}

		gfxDrawQuadARGB(w / 2 - wBox / 2,
			yBoxMiddle - yOffset,
			w / 2 + wBox / 2,
			yBoxMiddle - yOffset + 1,
			z,
			0x08ffffff);

		if(i){
			gfxDrawQuadARGB(w / 2 - wBox / 2,
				yBoxMiddle + yOffset,
				w / 2 + wBox / 2,
				yBoxMiddle + yOffset + 1,
				z,
				0x08ffffff);
		}
	}
	FOR_END;

	// Max Cycles
	if(s_MaxCycles > s_SimpleCpuGraphTargetCycles + 1000)
	{
		S32 y = s_MaxCycles / dividend;
		gfxDrawQuadARGB(w / 2 - wBox / 2, MAX(yBoxBottom - y, 0), w / 2 + wBox / 2, MAX(yBoxBottom - y, 0)-1, z, alpha | 0xff0000);

		if(s_SimpleCpuGraphDrawLabels)
		{
			F32 font_height;
			gfxfont_SetFontEx(&g_font_Sans, 0, 0, 0, 0, s_SimpleCpuGraphAlpha | 0xff000000, s_SimpleCpuGraphAlpha | 0xff000000);
			font_height = gfxfont_FontHeight(g_font_Active, 1);

			if(s_MaxCycles > 1000000000)
				gfxfont_Printf(w / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Max Cycles = %dB", (S32)s_MaxCycles / 1000000000);
			else if(s_MaxCycles > 1000000)
				gfxfont_Printf(w / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Max Cycles = %dM", (S32)s_MaxCycles / 1000000);
			else if(s_MaxCycles > 1000)
				gfxfont_Printf(w / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Max Cycles = %dK", (S32)s_MaxCycles / 1000);
			else
				gfxfont_Printf(w / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Max Cycles = %d", (S32)s_MaxCycles);
		}
	}

	// Target Cycles
	{
		S32 y = s_SimpleCpuGraphTargetCycles / dividend;
		gfxDrawQuadARGB(w / 2 - wBox / 2, MAX(yBoxBottom - y, 0), w / 2 + wBox / 2, MAX(yBoxBottom - y, 0)-1, z, alpha | 0x00ffff);

		if(s_SimpleCpuGraphDrawLabels)
		{
			F32 font_height;
			gfxfont_SetFontEx(&g_font_Sans, 0, 0, 0, 0, s_SimpleCpuGraphAlpha | 0x00ffff00, s_SimpleCpuGraphAlpha | 0x00ffff00);
			font_height = gfxfont_FontHeight(g_font_Active, 1);

			if(s_SimpleCpuGraphTargetCycles > 1000000000)
				gfxfont_Printf(w / 2 - wBox / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Target Cycles = %dB", (S32)s_SimpleCpuGraphTargetCycles / 1000000000);
			else if(s_SimpleCpuGraphTargetCycles > 1000000)
				gfxfont_Printf(w / 2 - wBox / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Target Cycles = %dM", (S32)s_SimpleCpuGraphTargetCycles / 1000000);
			else if(s_SimpleCpuGraphTargetCycles > 1000)
				gfxfont_Printf(w / 2 - wBox / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Target Cycles = %dK", (S32)s_SimpleCpuGraphTargetCycles / 1000);
			else
				gfxfont_Printf(w / 2 - wBox / 2, MAX(yBoxBottom - y, 0) + font_height, z, 1, 1, 0, "Target Cycles = %d", (S32)s_SimpleCpuGraphTargetCycles);
		}
	}

	if(s_SimpleCpuGraphDrawLegend)
	{
		F32 font_height;
		gfxfont_SetFontEx(&g_font_Sans, 0, 0, 0, 0, s_SimpleCpuGraphAlpha | 0xffffff00, s_SimpleCpuGraphAlpha | 0xffffff00);
		font_height = gfxfont_FontHeight(g_font_Active, 1);

		gfxfont_SetColorRGBA(s_SimpleCpuGraphAlpha | 0x00ff0000, s_SimpleCpuGraphAlpha | 0x00ff0000);
		gfxfont_Print(w / 2 + wBox / 2 - 120, yBoxTop + font_height + 20, z, 1, 1, 0, "Main");

		gfxfont_SetColorRGBA(s_SimpleCpuGraphAlpha | 0xff00ff00, s_SimpleCpuGraphAlpha | 0xff00ff00);
		gfxfont_Print(w / 2 + wBox / 2 - 120, yBoxTop + font_height * 2 + 20, z, 1, 1, 0, "Send To Client");

		gfxfont_SetColorRGBA(s_SimpleCpuGraphAlpha | 0xff000000, s_SimpleCpuGraphAlpha | 0xff000000);
		gfxfont_Print(w / 2 + wBox / 2 - 120, yBoxTop + font_height * 3 + 20, z, 1, 1, 0, "Movement BG Main");

		gfxfont_SetColorRGBA(s_SimpleCpuGraphAlpha | 0x00ffff00, s_SimpleCpuGraphAlpha | 0x00ffff00);
		gfxfont_Print(w / 2 + wBox / 2 - 120, yBoxTop + font_height * 4 + 20, z, 1, 1, 0, "Collision Main");

		gfxfont_SetColorRGBA(s_SimpleCpuGraphAlpha | 0xffff0000, s_SimpleCpuGraphAlpha | 0xffff0000);
		gfxfont_Print(w / 2 + wBox / 2 - 120, yBoxTop + font_height * 5 + 20, z, 1, 1, 0, "Packet Send");
	}

	// Decay the max cycles
	if(s_MaxCycles > s_ThisMaxCycles && s_MaxCycles > s_SimpleCpuGraphTargetCycles)
	{
		S32 prevMaxCycles = s_MaxCycles;
		s_MaxCycles = 0.99 * s_MaxCycles + 0.01 * s_SimpleCpuGraphTargetCycles;
		if(abs(prevMaxCycles - s_MaxCycles) < 10)
			s_MaxCycles = s_SimpleCpuGraphTargetCycles;
	}
}
