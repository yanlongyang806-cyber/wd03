#include "gclDumpSending.h"
#include "inputlib.h"
#include "GraphicsLib.h"
#include "GfxPrimitive.h"
#include "GfxSpriteText.h"
#include "errornet.h"
#include "errorprogressdlg.h"

#define BAR_MARGIN  (50)
#define BAR_HEIGHT  (50)
#define LINE_HEIGHT (40)

// Updates the screen with a progress bar during an xbox 360 dump send
static void updateDumpSendingProgress(size_t iSentBytes, size_t iTotalBytes)
{
	Color bg_color    = {  64,  64, 192, 255};
	Color bar_color   = {  64, 128,  64, 255};

	Color text_color1 = { 255, 255, 255, 255};
	Color text_color2 = { 255, 255, 255, 255};

	F32 percent_complete = (iTotalBytes) ? ((F32)iSentBytes / (F32)iTotalBytes) : 0.0f;
	int width, height, bar_width, y;

	gfxOncePerFrame(0.01f, 0.01f, false, false); // TODO: pass in a real time value?
	gfxSetActiveDevice(gfxGetActiveOrPrimaryDevice()); 
	inpUpdateEarly(gfxGetActiveInputDevice()); 
	inpUpdateLate(gfxGetActiveInputDevice()); 

	if(inpEdge(INP_AB))
		errorProgressDlgCancel();

	gfxGetActiveSurfaceSize(&width, &height);
	bar_width = (int)(percent_complete * (width - (BAR_MARGIN*2)));
	gfxDrawQuad(BAR_MARGIN, height - (BAR_MARGIN+BAR_HEIGHT), width - BAR_MARGIN,     height - BAR_MARGIN, 0, bg_color);
	gfxDrawQuad(BAR_MARGIN, height - (BAR_MARGIN+BAR_HEIGHT), bar_width + BAR_MARGIN, height - BAR_MARGIN, 0, bar_color);

	gfxfont_SetColor(text_color1, text_color2);

	y = 100;

	gfxfont_Printf(100, y,0, 1.5f, 1.5f, 0, "The game has detected previously recorded");		y += LINE_HEIGHT;
	gfxfont_Printf(100, y,0, 1.5f, 1.5f, 0, "crash data. Sending this information to the");		y += LINE_HEIGHT;
	gfxfont_Printf(100, y,0, 1.5f, 1.5f, 0, "ErrorTracker, please wait ...");					y += LINE_HEIGHT;
	y += LINE_HEIGHT;
	gfxfont_Printf(100, y,0, 1.5f, 1.5f, 0, "To cancel this transfer, press the A button.");	y += LINE_HEIGHT;

	y = height - BAR_MARGIN - (LINE_HEIGHT / 2);
	gfxfont_Printf(width/2, y,0, 1.5f, 1.5f, CENTER_X, "%2.2f / %2.2f MB", 
		(F32)iSentBytes  / (1024.0f * 1024.0f),
		(F32)iTotalBytes / (1024.0f * 1024.0f));

	gfxStartMainFrameAction(false, false, true, false, false);
	gfxFillDrawList(false, NULL);
	gfxDrawFrame();
	gfxOncePerFrameEnd(false);
}

void gclSendDeferredDumps(void)
{
#ifdef _XBOX
	// Send any lingering dump data to the error tracker
	errorProgressDlgSetUpdateCallback(updateDumpSendingProgress);
	errorTrackerCheckDumpCache();
	errorProgressDlgResetCallbacks();
#endif
}

static void updateDumpSendingProgressStub(size_t iSentBytes, size_t iTotalBytes)
{
}

void gclSendDeferredDumps_NoGraphics(void)
{
#ifdef _XBOX
	// Send any lingering dump data to the error tracker
	errorProgressDlgSetUpdateCallback(updateDumpSendingProgressStub);
	errorTrackerCheckDumpCache();
	errorProgressDlgResetCallbacks();
#endif
}
