
#include "SimpleUIMain.h"
#include "SimpleUI.h"
#include "wininclude.h"
#include "stdtypes.h"
#include "mathutil.h"
#include "assert.h"
#include "MemoryPool.h"
#include "winutil.h"
#include "timing.h"
#include "file.h"
#include "utils.h"
#include "MemoryMonitor.h"
#include "EArray.h"
#include "sysutil.h"
#include <psapi.h>
#include "net/net.h"

#define USE_DIB					1
#define USE_DDB					0

#define PRINT_KEY_MSGS			0
#define PRINT_PAINT_MSGS		0
#define QUERY_ON_END_SESSION	0

#define MAX_WIDTH				1680
#define MAX_HEIGHT				1050

#if PRINT_KEY_MSGS
	#define PRINT_KEY_MSG(msg) printf("%-13s: 0x%8.8x, 0x%8.8x\n", #msg, wParam, lParam)
#else
	#define PRINT_KEY_MSG(msg)
#endif

// Private structures.

typedef struct SUIMainDrawContext {
	SUIMainWindow*		mainWindow;
	HDC					hdc;
	S32					origin[2];
	S32					pos[2];
	S32					size[2];
	HRGN				hClipRegion;
} SUIMainDrawContext;

typedef struct SUIMainWindowEvent {
	void*						hEvent;
	void*						userPointer;
	SUIMainWindowEventCallback	callback;
} SUIMainWindowEvent;

typedef struct SUIMainWindow {
	RECT						dirtyRect;
	RECT						invalidRect;

	HWND						hwnd;

	char*						name;
	char*						titleBar;

	HDC							hdc;

	struct {
		HBITMAP					hBitmap;

		struct {
			S32					x;
			S32					y;
		} size;
	} buffer;
	
	struct {
		SUIMainWindowEvent**	events;
		void**					hEvents;
	} events;

	SUIRootWindow*				suiRootWindow;

	U32							buttonsHeld;

	RECT						rectCur;
	RECT						rectQueued;

	U32 						lastDrawTime;
	U32 						lastMsgTime;
	U32 						nextDrawTime;

	U32							msLastMemUpdate;
	
	U32							timerRefCount;
	
	U32							ncButtonDown;

	struct {
		S32						held;
		S32						x;
		S32						y;
	} anchor;

	struct {
		char					letter;
		U32						rgb;
		HICON					hIcon;
		char*					toolTip;
	} notifyIcon;

	struct {
		UINT					uMsg;
		WPARAM					wParam;
		LPARAM					lParam;
	} lastKeyMsg;

	struct {
		U32						waitForCharMsg				: 1;

		U32 					longSleep					: 1;
		U32 					ignorePaint					: 1;
		U32 					forcedPaint					: 1;

		U32						destroyMe					: 1;
		U32						hasProcessed				: 1;

		U32						hasDirtyRect				: 1;
		U32						hasInvalidRect				: 1;
		
		U32						mouseInWindow				: 1;
		U32						sizeChanged					: 1;
		U32						textDisabled				: 1;

		U32						drawBorderAroundUpdateArea	: 1;

		U32						notifyIconExists			: 1;

		U32						hideCursor					: 1;

		U32						registeredHotKey			: 1;
		
		U32						checkForMsgs				: 1;
		
		U32						minimizeOnShow				: 1;
	} flags;
} SUIMainWindow;

#define GET_BITS(x, lo, hi) (((x) >> lo) & ((1 << (hi - lo + 1)) - 1))
#define GET_BYTE(x, i)		GET_BITS(x, 8 * (i), 8 * (i) + 7)
#define GET_BYTE_0(x)		GET_BYTE(x, 0)
#define GET_BYTE_1(x)		GET_BYTE(x, 1)
#define GET_BYTE_2(x)		GET_BYTE(x, 2)
#define GET_BYTE_3(x)		GET_BYTE(x, 3)

#define WM_FROM_NOTIFY_ICON	(WM_USER)

static U32 taskBarCreateMessage;

static void suiMainNotifyIconSetInternal(SUIMainWindow* mw){
	if(!mw->notifyIcon.letter){
		return;
	}

	FOR_BEGIN(i, 2);
		NOTIFYICONDATA	nid = {0};
		U32				rgb = mw->notifyIcon.rgb;

		if(mw->notifyIcon.hIcon){
			DeleteObject(mw->notifyIcon.hIcon);
			mw->notifyIcon.hIcon = NULL;
		}

		if(!IsWindowVisible(mw->hwnd)){
			rgb = suiColorInterpAllRGB(0xff, rgb, 0x000000, 128);
		}

		mw->notifyIcon.hIcon = getIconColoredLetter(mw->notifyIcon.letter, rgb);

		nid.cbSize = sizeof(nid);
		nid.hWnd = mw->hwnd;
		nid.uID = 0;
		nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
		nid.hIcon = mw->notifyIcon.hIcon;
		nid.uCallbackMessage = WM_FROM_NOTIFY_ICON;

		strcpy(nid.szTip, FIRST_IF_SET(mw->notifyIcon.toolTip, "Someone should set the tooltip."));

		mw->flags.notifyIconExists = Shell_NotifyIcon(	mw->flags.notifyIconExists ?
															NIM_MODIFY :
															NIM_ADD,
														&nid);

		if(mw->flags.notifyIconExists){
			break;
		}
	FOR_END;
}

void suiMainWindowNotifyIconSet(SUIMainWindow* mw,
								char letter,
								U32 rgb,
								const char* toolTip)
{
	mw->notifyIcon.letter = letter;
	mw->notifyIcon.rgb = rgb;
	estrCopy2(&mw->notifyIcon.toolTip, toolTip);

	suiMainNotifyIconSetInternal(mw);
}

static void suiMainShellIconDestroy(SUIMainWindow* mw){
	if(TRUE_THEN_RESET(mw->flags.notifyIconExists)){
		NOTIFYICONDATA nid = {0};

		nid.cbSize = sizeof(nid);
		nid.hWnd = mw->hwnd;
		nid.uID = 0;

		Shell_NotifyIcon(NIM_DELETE, &nid);
	}
}

static void suiMainClearClipRegion(SUIMainDrawContext* dc){
	PERFINFO_AUTO_START("suiMainClearClipRegion", 1);
	{
		if(dc->hClipRegion){
			DeleteObject(dc->hClipRegion);

			dc->hClipRegion = NULL;
		}
	}
	PERFINFO_AUTO_STOP();
}

static void suiMainSetClipRect(SUIMainDrawContext* dc, S32 x, S32 y, S32 sx, S32 sy){
	PERFINFO_AUTO_START("suiMainSetClipRect", 1);
	{
		suiMainClearClipRegion(dc);

		if(sx < 0){
			sx = 0;
		}

		if(sy < 0){
			sy = 0;
		}

		dc->hClipRegion = CreateRectRgn(x, y, x + sx, y + sy);

		SelectClipRgn(dc->hdc, dc->hClipRegion);
	}
	PERFINFO_AUTO_STOP();
}

static void suiMainDrawLine(SUIMainDrawContext* dc, S32 x0, S32 y0, S32 x1, S32 y1, U32 colorARGB){
	PERFINFO_AUTO_START("suiMainDrawLine", 1);
	{
		static HPEN hPen;

		PERFINFO_AUTO_START("CreatePen", 1);
			hPen = hPen ? hPen : CreatePen(PS_SOLID, 1, RGB(	GET_BYTE_2(colorARGB),
																GET_BYTE_1(colorARGB),
																GET_BYTE_0(colorARGB)));
		PERFINFO_AUTO_STOP_START("SelectObject", 1);
			SelectObject(dc->hdc, hPen);
		PERFINFO_AUTO_STOP_START("MoveToEx", 1);
			MoveToEx(dc->hdc, x0, y0, NULL);
		PERFINFO_AUTO_STOP_START("LineTo", 1);
			LineTo(dc->hdc, x1, y1);
		PERFINFO_AUTO_STOP_START("DeleteObject", 1);
			//DeleteObject(hPen);
		PERFINFO_AUTO_STOP();
	}
	PERFINFO_AUTO_STOP();
}

typedef struct CachedBrush {
	U32		argb;
	HBRUSH	hBrush;
	U32		threadID;
} CachedBrush;

static void getBrush(	const CachedBrush** brushOut,
						U32 argb)
{
	static CRITICAL_SECTION cs;
	static CachedBrush**	brushes;
	static CachedBrush*		lastBrushes[10];
	static S32				lastBrushesIndex;

	CachedBrush*			brush = NULL;
	const U32				threadID = GetCurrentThreadId();

	ARRAY_FOREACH_BEGIN(lastBrushes, i);
		const CachedBrush* cb = lastBrushes[i];

		if(	cb &&
			cb->argb == argb &&
			cb->threadID == threadID)
		{
			*brushOut = cb;
			return;
		}
	ARRAY_FOREACH_END;

	PERFINFO_AUTO_START("getBrush", 1);

	ATOMIC_INIT_BEGIN;
		InitializeCriticalSection(&cs);
	ATOMIC_INIT_END;

	EnterCriticalSection(&cs);
	{
		EARRAY_CONST_FOREACH_BEGIN(brushes, i, isize);
			if(	brushes[i]->argb == argb &&
				brushes[i]->threadID == threadID)
			{
				brush = brushes[i];
			}
		EARRAY_FOREACH_END;

		if(!brush){
			brush = callocStruct(CachedBrush);
			brush->argb = argb;
			brush->threadID = threadID;

			eaPush(&brushes, brush);

			PERFINFO_AUTO_START("CreateSolidBrush", 1);
				brush->hBrush = CreateSolidBrush(RGB(	GET_BYTE_2(argb),
														GET_BYTE_1(argb),
														GET_BYTE_0(argb)));
			PERFINFO_AUTO_STOP();
		}
	}
	LeaveCriticalSection(&cs);

	PERFINFO_AUTO_STOP();

	{
		U32 index = lastBrushesIndex + 1;
		index %= ARRAY_SIZE(lastBrushes);
		lastBrushesIndex = index;
		lastBrushes[index] = brush;
	}

	*brushOut = brush;
}

static void suiMainFillRect(HDC hdc,
							const RECT* r,
							HBRUSH hBrush)
{
	PERFINFO_AUTO_START("FillRect", 1);
		FillRect(hdc, r, hBrush);
	PERFINFO_AUTO_STOP();
}

typedef BOOL (*GradientFillFunc)(	HDC hdc,
									PTRIVERTEX pVertex,
									ULONG dwNumVertex,
									PVOID pMesh,
									ULONG dwNumMesh,
									ULONG dwMode);

static void suiMainGradientFill(HDC hdc,
								const RECT* r,
								U32 argb)
{
	static HMODULE			hMSImg32;
	static GradientFillFunc funcGradientFill;

	ATOMIC_INIT_BEGIN;
	{
		hMSImg32 = LoadLibrary("MSImg32.dll");

		if(hMSImg32){
			funcGradientFill = (void*)GetProcAddress(hMSImg32, "GradientFill");
		}
	}
	ATOMIC_INIT_END;

	if(!funcGradientFill){
		return;
	}

	PERFINFO_AUTO_START("GradientFill", 1);
	{
		TRIVERTEX		p[2] = {0};
		GRADIENT_RECT	gr;

		p[0].x = r->left;
		p[0].y = r->top;
		p[0].Alpha = (argb & 0xff000000) >> 16;
		p[0].Red = (argb & 0xff0000) >> 8;
		p[0].Green = (argb & 0xff00) >> 0;
		p[0].Blue = (argb & 0xff) << 8;

		p[1] = p[0];
		p[1].x = r->right;
		p[1].y = r->bottom;
		//p[1].Alpha = p[0].Alpha;
		//p[1].Red = p[0].Red;
		//p[1].Green = p[0].Green;
		//p[1].Blue = p[0].Blue;

		gr.UpperLeft = 0;
		gr.LowerRight = 1;

		funcGradientFill(hdc, p, 2, &gr, 1, GRADIENT_FILL_RECT_V);
	}
	PERFINFO_AUTO_STOP();
}

static void suiMainDrawFilledRect(	SUIMainDrawContext* dc,
									S32 x,
									S32 y,
									S32 sx,
									S32 sy,
									U32 colorARGB)
{
	PERFINFO_AUTO_START("suiMainDrawFilledRect", 1);
	{
		RECT				rect = {x, y, x + sx, y + sy};

		#if 0
			const CachedBrush*	brush;

			getBrush(&brush, colorARGB);

			if(0){
				printf(	"Drawing: (%4d,%4d)-(%4d,%4d) Color: 0x%8.8x\n",
						rect.left,
						rect.top,
						rect.right,
						rect.bottom,
						colorARGB);
			}

			//SelectObject(dc->hdc, brush->hBrush);

			suiMainFillRect(dc->hdc, &rect, brush->hBrush);
		#else
			suiMainGradientFill(dc->hdc, &rect, colorARGB);
		#endif
	}
	PERFINFO_AUTO_STOP();
}

static void suiMainDrawRect(SUIMainDrawContext* dc,
							S32 x,
							S32 y,
							S32 sx,
							S32 sy,
							S32 borderWidth,
							U32 colorARGB)
{
	PERFINFO_AUTO_START("suiMainDrawRect", 1);
	{
		CachedBrush*	brush;
		RECT			rect[4] = {
							{x, y, x + borderWidth, y + sy},
							{x + borderWidth, y, x + sx - borderWidth, y + borderWidth},
							{x + sx - borderWidth, y, x + sx, y + sy},
							{x + borderWidth, y + sy - borderWidth, x + sx - borderWidth, y + sy},
						};

		getBrush(&brush, colorARGB);

		//SelectObject(dc->hdc, brush->hBrush);

		ARRAY_FOREACH_BEGIN(rect, i);
			suiMainFillRect(dc->hdc,
							rect + i,
							brush->hBrush);
		ARRAY_FOREACH_END;

		//DeleteObject(hBrush);
	}
	PERFINFO_AUTO_STOP();
}

static void suiMainDrawFilledTriangle(	SUIMainDrawContext* dc,
										S32 x0,
										S32 y0,
										S32 x1,
										S32 y1,
										S32 x2,
										S32 y2,
										U32 colorARGB)
{
	PERFINFO_AUTO_START("suiMainDrawFilledTriangle", 1);
	{
		CachedBrush*	brush;
		HPEN			hPen = CreatePen(PS_NULL, 0, 0);
		POINT			tri[3] = {{x0, y0}, {x1, y1}, {x2, y2}};

		getBrush(&brush, colorARGB);

		SelectObject(dc->hdc, brush->hBrush);
		SelectObject(dc->hdc, hPen);

		Polygon(dc->hdc, tri, 3);

		//DeleteObject(hBrush);
		DeleteObject(hPen);
	}
	PERFINFO_AUTO_STOP();
}

static HFONT getFont(U32 height){
	static HFONT hFont[201];

	//return;

	if(!height){
		height = 15;
	}
	else if(height >= ARRAY_SIZE(hFont)){
		height = ARRAY_SIZE(hFont) - 1;
	}

	if(!hFont[height]){
		static CRITICAL_SECTION cs;

		ATOMIC_INIT_BEGIN;
		{
			InitializeCriticalSection(&cs);
		}
		ATOMIC_INIT_END;

		EnterCriticalSection(&cs);
		{
			if(!hFont[height]){
				hFont[height] = CreateFont(	height,
											0,
											0,
											0,
											0 ? FW_NORMAL : FW_BOLD,
											FALSE,
											FALSE,
											FALSE,
											ANSI_CHARSET,
											OUT_DEFAULT_PRECIS,
											CLIP_DEFAULT_PRECIS,
											0 ? ANTIALIASED_QUALITY : CLEARTYPE_QUALITY,
											DEFAULT_PITCH | FF_SWISS,
											"SegoeUI");
			}
		}
		LeaveCriticalSection(&cs);
	}

	return hFont[height];
}

void suiMainPrintText(	SUIMainDrawContext* dc,
						S32 x,
						S32 y,
						const char* textData,
						S32 textLen,
						U32 height,
						U32 colorARGB)
{
	SUIMainWindow* mw = dc->mainWindow;

	if(mw->flags.textDisabled){
		return;
	}

	PERFINFO_AUTO_START("suiMainPrintText", 1);
	{
		S32 lineHeight = 10;

		SelectObject(dc->hdc, getFont(height));
		SetBkMode(dc->hdc, TRANSPARENT);

		SetTextColor(	dc->hdc,
						RGB(GET_BYTE_2(colorARGB),
							GET_BYTE_1(colorARGB),
							GET_BYTE_0(colorARGB)));

		TextOut(dc->hdc,
				dc->origin[0] + x,
				dc->origin[1] + y,
				textData,
				textLen >= 0 ?
					textLen :
					(S32)strlen(textData));

		//DeleteObject(hFont);
	}
	PERFINFO_AUTO_STOP();
}

#if PRINT_PAINT_MSGS
static void printClipBox(	SUIMainWindow* mw,
							const char* name,
							HDC hdc)
{
	static U32 count;

	RECT r;

	GetClipBox(hdc, &r);

	printf(	"%2d: %20s      %4d, %4d, %4d, %4d %s(%4d, %4d, %4d, %4d).\n",
			++count % 100,
			name,
			r.left,
			r.top,
			r.right,
			r.bottom,
			mw->flags.hasDirtyRect ? "r" : " ",
			mw->invalidRect.left,
			mw->invalidRect.top,
			mw->invalidRect.right,
			mw->invalidRect.bottom);
}
#endif

static void suiMainWindowDraw(	SUIMainWindow* mw,
								HDC hdcParam,
								S32 canUpdate)
{
	PAINTSTRUCT	ps = {0};
	HDC			hdc;
	S32			width;
	S32			height;

	if(hdcParam){
		mw->hdc = hdcParam;
	}else{
		PERFINFO_AUTO_START("BeginPaint", 1);
			BeginPaint(mw->hwnd, &ps);
		PERFINFO_AUTO_STOP();

		#if PRINT_PAINT_MSGS
			printClipBox(mw, "WM_PAINT", ps.hdc);

			printf(	"rcPaint: %4d, %4d, %4d, %4d\n",
					ps.rcPaint.left,
					ps.rcPaint.top,
					ps.rcPaint.right,
					ps.rcPaint.bottom);
		#endif

		mw->hdc = ps.hdc;
	}

	PERFINFO_AUTO_START("CreateCompatibleDC", 1);
		hdc = CreateCompatibleDC(mw->hdc);
	PERFINFO_AUTO_STOP();

	if(0){
		RECT r;

		GetClipBox(mw->hdc, &r);

		printf(	"Clip box: %d,%d - %d,%d\n",
				r.left,
				r.top,
				r.right,
				r.bottom);
	}

	PERFINFO_AUTO_START("SelectObject", 1);
		SelectObject(hdc, mw->buffer.hBitmap);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("GetClientRect", 1);
	{
		RECT wr;
		GetClientRect(mw->hwnd, &wr);
		width = wr.right - wr.left;
		height = wr.bottom - wr.top;
	}
	PERFINFO_AUTO_STOP();

	if(	canUpdate &&
		TRUE_THEN_RESET(mw->flags.hasDirtyRect))
	{
		SUIRootWindowDrawInfo	di = {0};
		SUIMainDrawContext		dcp = {0};

		PERFINFO_AUTO_START("setup", 1);
			//printf("------\n");

			di.userPointer = &dcp;

			// Set draw functions.

			di.funcs.setClipRect = suiMainSetClipRect;
			di.funcs.drawLine = suiMainDrawLine;
			di.funcs.drawFilledRect = suiMainDrawFilledRect;
			di.funcs.drawRect = suiMainDrawRect;
			di.funcs.drawFilledTriangle = suiMainDrawFilledTriangle;
			di.funcs.printText = suiMainPrintText;

			// Setup the private data.

			dcp.mainWindow = mw;
			dcp.hdc = hdc;

			dcp.origin[0] = 0;
			dcp.origin[1] = 0;

			dcp.pos[0] = mw->dirtyRect.left;
			dcp.pos[1] = mw->dirtyRect.top;

			dcp.size[0] = mw->dirtyRect.right - mw->dirtyRect.left;
			dcp.size[1] = mw->dirtyRect.bottom - mw->dirtyRect.top;

			ZeroStruct(&mw->dirtyRect);
		PERFINFO_AUTO_STOP();

		if(	dcp.size[0] > 0 &&
			dcp.size[1] > 0)
		{
			// Call the top level draw routine.

			PERFINFO_AUTO_START("suiRootWindowDraw", 1);
				suiRootWindowDraw(	mw->suiRootWindow,
									&di,
									0,
									0,
									dcp.pos[0],
									dcp.pos[1],
									dcp.size[0],
									dcp.size[1],
									mw->flags.drawBorderAroundUpdateArea);
			PERFINFO_AUTO_STOP();
		}

		suiMainClearClipRegion(&dcp);

		//Sleep(3);
	}

	//printf("blt!\n");

	PERFINFO_AUTO_START("BitBlt", 1);
		BitBlt(mw->hdc, 0, 0, width, height, hdc, 0, 0, SRCCOPY);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("DeleteDC", 1);
		DeleteDC(hdc);
	PERFINFO_AUTO_STOP();

	if(!hdcParam){
		PERFINFO_AUTO_START("EndPaint", 1);
			EndPaint(mw->hwnd, &ps);
		PERFINFO_AUTO_STOP();
	}

	mw->hdc = NULL;
}

static void suiMainWindowTrackMouseLeave(SUIMainWindow* mw){
	if(FALSE_THEN_SET(mw->flags.mouseInWindow)){
		TRACKMOUSEEVENT tme = {0};

		tme.cbSize = sizeof(tme);
		tme.dwFlags = TME_LEAVE;
		tme.hwndTrack = mw->hwnd;

		TrackMouseEvent(&tme);
	}
}

static void suiMainWindowMouseMove(	SUIMainWindow* mw,
									DWORD wParam,
									DWORD lParam)
{
	S32 x = (S16)LOWORD(lParam) - 0;//suiWindowGetPosX(mw->suiRootWindow);
	S32 y = (S16)HIWORD(lParam) - 0;//suiWindowGetPosY(mw->suiRootWindow);

	suiMainWindowTrackMouseLeave(mw);

	suiRootWindowMouseMove(mw->suiRootWindow, x, y);
}

static void suiMainWindowMouseWheel(SUIMainWindow* mw,
									DWORD wParam,
									DWORD lParam)
{
	S32		delta = GET_WHEEL_DELTA_WPARAM(wParam);
	POINT	p = {(S16)LOWORD(lParam), (S16)HIWORD(lParam)};

	suiMainWindowTrackMouseLeave(mw);

	ScreenToClient(mw->hwnd, &p);

	suiRootWindowMouseWheel(mw->suiRootWindow, p.x, p.y, 1000 * delta / WHEEL_DELTA);
}

static void suiMainWindowMouseDown(	SUIMainWindow* mw,
									U32 button,
									DWORD wParam,
									DWORD lParam)
{
	S32 x = (S16)LOWORD(lParam) - 0;//suiWindowGetPosX(mw->suiRootWindow);
	S32 y = (S16)HIWORD(lParam) - 0;//suiWindowGetPosY(mw->suiRootWindow);

	mw->buttonsHeld |= button;

	suiMainWindowTrackMouseLeave(mw);

	SetCapture(mw->hwnd);

	suiRootWindowMouseDown(mw->suiRootWindow, x, y, button);
}

static void suiMainWindowMouseDoubleClick(	SUIMainWindow* mw,
											U32 button,
											DWORD wParam,
											DWORD lParam)
{
	S32 x = (S16)LOWORD(lParam) - 0;//suiWindowGetPosX(mw->suiRootWindow);
	S32 y = (S16)HIWORD(lParam) - 0;//suiWindowGetPosY(mw->suiRootWindow);

	mw->buttonsHeld |= button;

	suiMainWindowTrackMouseLeave(mw);

	SetCapture(mw->hwnd);

	suiRootWindowMouseDoubleClick(mw->suiRootWindow, x, y, button);
}

static void suiMainWindowMouseUp(	SUIMainWindow* mw,
									U32 button,
									DWORD wParam,
									DWORD lParam)
{
	S32 x = (S16)LOWORD(lParam) - 0;//suiWindowGetPosX(mw->suiRootWindow);
	S32 y = (S16)HIWORD(lParam) - 0;//suiWindowGetPosY(mw->suiRootWindow);

	suiMainWindowTrackMouseLeave(mw);

	mw->buttonsHeld &= ~button;

	if(!mw->buttonsHeld){
		ReleaseCapture();
	}

	suiRootWindowMouseUp(mw->suiRootWindow, x, y, button);
}

static void suiMainWindowInvalidateRect(SUIMainWindow* mw,
										const RECT* invalidRect)
{
	RECT tempRect;

	//InvalidateRect(mw->hwnd, invalidRect, FALSE);

	if(!invalidRect){
		GetClientRect(mw->hwnd, &tempRect);

		//printf( "Invalidating: %d, %d, %d, %d (%d x %d)\n",
		//		tempRect.left,
		//		tempRect.top,
		//		tempRect.right,
		//		tempRect.bottom,
		//		tempRect.right - tempRect.left,
		//		tempRect.bottom - tempRect.top);

		tempRect.right -= tempRect.left;
		tempRect.bottom -= tempRect.top;
		tempRect.left = 0;
		tempRect.top = 0;

		invalidRect = &tempRect;
	}
	else if(invalidRect->right <= invalidRect->left ||
			invalidRect->bottom <= invalidRect->top)
	{
		return;
	}

	if(FALSE_THEN_SET(mw->flags.hasDirtyRect)){
		mw->dirtyRect = *invalidRect;
	}else{
		MIN1(mw->dirtyRect.left, invalidRect->left);
		MIN1(mw->dirtyRect.top, invalidRect->top);
		MAX1(mw->dirtyRect.right, invalidRect->right);
		MAX1(mw->dirtyRect.bottom, invalidRect->bottom);
	}

	if(FALSE_THEN_SET(mw->flags.hasInvalidRect)){
		mw->invalidRect = *invalidRect;
	}else{
		MIN1(mw->invalidRect.left, invalidRect->left);
		MIN1(mw->invalidRect.top, invalidRect->top);
		MAX1(mw->invalidRect.right, invalidRect->right);
		MAX1(mw->invalidRect.bottom, invalidRect->bottom);
	}
}

static void suiMainClientRectToScreenRect(	SUIMainWindow* mw,
											RECT* r)
{
	POINT pt0 = {r->left, r->top};
	POINT pt1 = {r->right, r->bottom};

	ClientToScreen(mw->hwnd, &pt0);
	ClientToScreen(mw->hwnd, &pt1);

	r->left = pt0.x;
	r->top = pt0.y;
	r->right = pt1.x;
	r->bottom = pt1.y;
}

static U32 shellHookMsg;

#if 0
	#define USE_DEFERERASE SWP_DEFERERASE
#else
	#define USE_DEFERERASE 0
#endif

static void suiMainWinMsgTimerStart(UINT uMsg);
static void suiMainWinMsgTimerStop(void);

static LRESULT suiMainDefWindowProc(HWND hwnd,
									UINT uMsg,
									WPARAM wParam,
									LPARAM lParam)
{
	LRESULT result;

	PERFINFO_AUTO_START_FUNC();
		suiMainWinMsgTimerStart(uMsg);
		result = DefWindowProc(hwnd, uMsg, wParam, lParam);
		suiMainWinMsgTimerStop();
	PERFINFO_AUTO_STOP();

	return result;
}

static void suiMainWindowSetHidden(SUIMainWindow* mw){
	if(IsWindowVisible(mw->hwnd)){
		ShowWindow(mw->hwnd, SW_HIDE);
		suiMainNotifyIconSetInternal(mw);
	}
}

static void suiMainWindowSetVisible(SUIMainWindow* mw){
	if(!IsWindowVisible(mw->hwnd)){
		ShowWindow(mw->hwnd, SW_SHOW);
		if(IsIconic(mw->hwnd)){
			ShowWindow(mw->hwnd, SW_RESTORE);
		}
		SetForegroundWindow(mw->hwnd);
		SetActiveWindow(mw->hwnd);
		suiMainNotifyIconSetInternal(mw);
	}else{
		if(IsIconic(mw->hwnd)){
			ShowWindow(mw->hwnd, SW_RESTORE);
		}

		if(GetForegroundWindow() != mw->hwnd){
			SetForegroundWindow(mw->hwnd);
			SetActiveWindow(mw->hwnd);
		}
	}
}

static void suiMainTranslateKey(WPARAM wParam,
								LPARAM lParam,
								SUIKey* keyOut)
{
	U32 vk = wParam;

	#define CHECK_RANGE(vk1, vk2, base) \
		if(vk >= vk1 && vk <= vk2){*keyOut = base + vk - vk1;return;}

	CHECK_RANGE(VK_F1, VK_F12, SUI_KEY_F1);
	CHECK_RANGE('a', 'z', SUI_KEY_A);
	CHECK_RANGE('A', 'Z', SUI_KEY_A);

	#undef CHECK_RANGE

	switch(vk){
		#define CASE(x, y) case x:*keyOut=y;return
		CASE(VK_RETURN,			SUI_KEY_ENTER);
		CASE(VK_ESCAPE,			SUI_KEY_ESCAPE);
		CASE(VK_BACK,			SUI_KEY_BACKSPACE);
		CASE(VK_TAB,			SUI_KEY_TAB);
		CASE(VK_SPACE,			SUI_KEY_SPACE);
		CASE(VK_LSHIFT,			SUI_KEY_LSHIFT);
		CASE(VK_RSHIFT,			SUI_KEY_RSHIFT);
		CASE(VK_LCONTROL,		SUI_KEY_LCONTROL);
		CASE(VK_RCONTROL,		SUI_KEY_RCONTROL);
		CASE(VK_LMENU,			SUI_KEY_LALT);
		CASE(VK_RMENU,			SUI_KEY_RALT);
		CASE(VK_OEM_PLUS,		SUI_KEY_PLUS);
		CASE(VK_OEM_MINUS,		SUI_KEY_MINUS);
		CASE(VK_OEM_4,			SUI_KEY_LEFT_BRACKET);
		CASE(VK_OEM_6,			SUI_KEY_RIGHT_BRACKET);
		#undef CASE
	}
}

static BOOL CALLBACK windowStationCallback(	const char* name,
											LPARAM userPointer)
{
	printf("window station: %s\n", name);

	return TRUE;
}

static S32 suiMainWindowKey(SUIMainWindow* mw,
							UINT uMsg,
							WPARAM wParam,
							LPARAM lParam,
							U32 character,
							S32 isDown)
{
	SUIKey	key = 0;
	S32		isFirstPress = !!(lParam & BIT(30));
	U32		modBits = 0;

	if((GetKeyState(VK_LSHIFT) | GetKeyState(VK_RSHIFT)) & BIT(31)){
		modBits |= SUI_KEY_MOD_SHIFT;
	}

	if((GetKeyState(VK_LCONTROL) | GetKeyState(VK_RCONTROL)) & BIT(31)){
		modBits |= SUI_KEY_MOD_CONTROL;
	}

	if((GetKeyState(VK_LMENU) | GetKeyState(VK_RMENU)) & BIT(31)){
		modBits |= SUI_KEY_MOD_ALT;
	}

	if(mw->flags.waitForCharMsg){
		mw->lastKeyMsg.uMsg = uMsg;
		mw->lastKeyMsg.wParam = wParam;
		mw->lastKeyMsg.lParam = lParam;

		return 0;
	}else{
		suiMainTranslateKey(wParam, lParam, &key);
	}

	if(suiRootWindowKey(mw->suiRootWindow, key, character, isDown, modBits)){
		return 1;
	}

	if(isDown){
		switch(key){
			#if 1
			xcase SUI_KEY_Q:{
				if(	!(~modBits & SUI_KEY_MOD_CONTROL_SHIFT) &&
					!(modBits & SUI_KEY_MOD_ALT))
				{
					mw->flags.destroyMe = 1;
				}
			}
			#endif
		}

		switch(wParam){
			xcase VK_F1:{
				memMonitorDisplayStats();
			}

			xcase VK_F2:{
				#if 0
				{
					mw->flags.textDisabled = !mw->flags.textDisabled;

					suiMainWindowInvalidateRect(mw, NULL);
				}
				#endif
			}

			xcase VK_F3:{
				mw->flags.drawBorderAroundUpdateArea = !mw->flags.drawBorderAroundUpdateArea;

				suiMainWindowInvalidateRect(mw, NULL);
			}

			xcase VK_F5:{
				#if 0
				{
					suiMainWindowInvalidateRect(mw, NULL);

					//EnumWindowStationsA(windowStationCallback, 0);
				}
				#endif
			}

			xcase VK_F6:{
				#if 1
				{
					InvalidateRect(mw->hwnd, NULL, FALSE);
				}
				#endif
			}

			xcase VK_F7:{
				#if 0
				{
					mw->flags.ignorePaint = !mw->flags.ignorePaint;
				}
				#endif
			}

			xcase VK_F8:{
				#if 0
				{
					S32 value;

					mw->flags.hideCursor = !mw->flags.hideCursor;

					value = ShowCursor(!mw->flags.hideCursor);

					printf(	"tid %d: ShowCursor(%d) = %d\n",
							GetCurrentThreadId(),
							!mw->flags.hideCursor,
							value);
				}
				#endif
			}

			xcase VK_F9:{
				if(IsConsoleWindowVisible()){
					hideConsoleWindow();
				}else{
					newConsoleWindow();
					showConsoleWindow();
				}
			}

			xcase VK_LEFT:{
				#if 0
				{
					mw->rectQueued.left -= 10;

					//longSleep = 1;

					mw->flags.sizeChanged = 1;

					//printf(	"window1: %4d, %4d, %4d, %4d  (%4d x %4d)\n",
					//		rect.left,
					//		rect.top,
					//		rect.right,
					//		rect.bottom,
					//		rect.right - rect.left,
					//		rect.bottom - rect.top);

					//suiWindowSetSize(mw->suiRootWindow, rect.right - rect.left, rect.bottom - rect.top);

					//mw->invalidRect.right += 10;

					//printf(	"window2: %4d, %4d, %4d, %4d  (%4d x %4d)\n",
					//		rect.left,
					//		rect.top,
					//		rect.right,
					//		rect.bottom,
					//		rect.right - rect.left,
					//		rect.bottom - rect.top);

					//InvalidateRect(hwnd, NULL, FALSE);
					//SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | USE_DEFERERASE);
				}
				#endif
			}

			xcase VK_RIGHT:{
				#if 0
				{
					mw->rectQueued.left += 10;

					//printf("right key\n");

					mw->flags.longSleep = 1;

					//suiWindowSetSize(mw->suiRootWindow, rect.right - rect.left, rect.bottom - rect.top);
					//
					//InvalidateRect(hwnd, NULL, FALSE);
					//
					//SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | USE_DEFERERASE);
				}
				#endif
			}

			xcase VK_DOWN:{
				#if 0
				{
					InvalidateRect(mw->hwnd, NULL, FALSE);

					mw->flags.hasDirtyRect = 1;
				}
				#endif
			}

			xcase VK_UP:{
				#if 0
				{
					InvalidateRect(mw->hwnd, NULL, FALSE);

					mw->flags.hasDirtyRect = 1;
				}
				#endif
			}

			xcase VK_RETURN:{
				#if 0
				{
					S32 sx = mw->rectQueued.right - mw->rectQueued.left + 10;
					S32 sy = mw->rectQueued.bottom - mw->rectQueued.top + 10;

					sx = min(sx, mw->buffer.size.x);
					sy = min(sy, mw->buffer.size.y);

					//sx += 100;
					//sy += 100;

					mw->rectQueued.left = mw->rectQueued.right - sx;
					mw->rectQueued.top = mw->rectQueued.bottom - sy;
				}
				#endif
			}

			xcase VK_ESCAPE:{
				#if 0
				{
					//suiMainWindowSetHidden(mw);
					//mw->flags.destroyMe = 1;
				}
				#endif
			}

			xdefault:{
				//InvalidateRect(mw->hwnd, NULL, FALSE);
				//mw->flags.hasDirtyRect = 1;

				return suiMainDefWindowProc(mw->hwnd, uMsg, wParam, lParam);
			}
		}
		
		return 1;
	}else{
		return suiMainDefWindowProc(mw->hwnd, uMsg, wParam, lParam);
	}
}

static void suiMainSetSizeToQueuedRect(SUIMainWindow* mw){
	RECT	rectWindow;
	RECT	rectClient;
	POINT	ptClient[2];
	S32		noSize;

	GetWindowRect(	mw->hwnd,
					&rectWindow);

	GetClientRect(	mw->hwnd,
					&rectClient);

	ptClient[0].x = rectClient.left;
	ptClient[0].y = rectClient.top;
	ptClient[1].x = rectClient.right;
	ptClient[1].y = rectClient.bottom;

	ClientToScreen(	mw->hwnd,
					ptClient);
	ClientToScreen(	mw->hwnd,
					ptClient + 1);

	noSize =	rectClient.right - rectClient.left == mw->rectQueued.right - mw->rectQueued.left &&
				rectClient.bottom - rectClient.top == mw->rectQueued.bottom - mw->rectQueued.top;

	if(noSize){
		PERFINFO_AUTO_START("SetWindowPos(noSize)", 1);
	}else{
		PERFINFO_AUTO_START("SetWindowPos(size)", 1);
	}
	{
		SetWindowPos(	mw->hwnd,
						0,
						mw->rectQueued.left - (ptClient[0].x - rectWindow.left),
						mw->rectQueued.top - (ptClient[0].y - rectWindow.top),
						mw->rectQueued.right - mw->rectQueued.left +
							(rectWindow.right - rectWindow.left) -
							(ptClient[1].x - ptClient[0].x),
						mw->rectQueued.bottom - mw->rectQueued.top +
							(rectWindow.bottom - rectWindow.top) -
							(ptClient[1].y - ptClient[0].y),
						SWP_NOZORDER
							//| SWP_DEFERERASE
							//| SWP_NOCOPYBITS
							| (noSize ? SWP_NOSIZE : 0));
	}
	PERFINFO_AUTO_STOP();
}

static S32 suiMainCheckQueuedRectChange(SUIMainWindow* mw){
	if(!memcmp(&mw->rectCur, &mw->rectQueued, sizeof(mw->rectCur))){
		return 0;
	}else{
		S32 sizeChanged =	mw->rectCur.right - mw->rectCur.left !=
								mw->rectQueued.right - mw->rectQueued.left
							||
							mw->rectCur.bottom - mw->rectCur.top !=
								mw->rectQueued.bottom - mw->rectQueued.top;

		if(sizeChanged){
			PERFINFO_AUTO_START("resize", 1);
		}else{
			PERFINFO_AUTO_START("move", 1);
		}
		{
			if(sizeChanged){
				mw->flags.longSleep = 1;

				PERFINFO_AUTO_START("suiRootWindowSetSize", 1);
					suiRootWindowSetSize(	mw->suiRootWindow,
											mw->rectQueued.right - mw->rectQueued.left,
											mw->rectQueued.bottom - mw->rectQueued.top);
				PERFINFO_AUTO_STOP();

				mw->flags.hasInvalidRect = 1;
				mw->invalidRect.left = 0;
				mw->invalidRect.top = 0;
				mw->invalidRect.right = mw->rectQueued.right - mw->rectQueued.left;
				mw->invalidRect.bottom = mw->rectQueued.bottom - mw->rectQueued.top;
				
				mw->flags.hasDirtyRect = 1;
				mw->dirtyRect = mw->invalidRect;
			}

			mw->rectCur = mw->rectQueued;

			suiMainSetSizeToQueuedRect(mw);
		}
		PERFINFO_AUTO_STOP();

		return 1;
	}
}

static void suiMainCheckProcessAndDrawTime(SUIMainWindow* mw){
	PERFINFO_AUTO_START_FUNC();
	{
		const U32	redrawPeriod = mw->flags.waitForCharMsg ? 0 : 1;
		S32			windowMoved = 0;
		S32			timeToProcess = mw->lastMsgTime - mw->lastDrawTime >= redrawPeriod;

		if(!timeToProcess){
			if(	mw->rectCur.left != mw->rectQueued.left ||
				mw->rectCur.top != mw->rectQueued.top)
			{
				S32 sizeChanged =	mw->rectCur.right - mw->rectCur.left !=
										mw->rectQueued.right - mw->rectQueued.left
									||
									mw->rectCur.bottom - mw->rectCur.top !=
										mw->rectQueued.bottom - mw->rectQueued.top;

				if(!sizeChanged){
					windowMoved = 1;
				}
			}
		}

		if(	timeToProcess ||
			windowMoved)
		{
			if(timeToProcess){
				if(timeGetTime() - mw->msLastMemUpdate > 1000){
					if(IsWindowVisible(mw->hwnd)){
						PERFINFO_AUTO_START("update title bar", 1);
						{
							PROCESS_MEMORY_COUNTERS_EX	pmc;
							char						name[500];

							GetProcessMemoryInfo(	GetCurrentProcess(),
													(PROCESS_MEMORY_COUNTERS*)&pmc,
													sizeof(pmc));

							sprintf(name,
									"%s%s%sKB private, %d links",
									mw->name,
									mw->name[0] ? " : " : "",
									getCommaSeparatedInt(pmc.PrivateUsage / 1024),
									linkGetTotalCount());

							if(	!mw->titleBar ||
								stricmp(name, mw->titleBar))
							{
								estrCopy2(&mw->titleBar, name);
								SetWindowText(mw->hwnd, name);
							}
						}
						PERFINFO_AUTO_STOP();
					}

					mw->msLastMemUpdate = timeGetTime();
				}
				
				suiRootWindowProcess(mw->suiRootWindow);
			}

			windowMoved = suiMainCheckQueuedRectChange(mw);

			if(	(	windowMoved ||
					mw->flags.hasDirtyRect)
				&&
				!IsIconic(mw->hwnd)
				&&
				IsWindowVisible(mw->hwnd))
			{
				PERFINFO_AUTO_START("hasDirtyRect", 1);
				{
					if(TRUE_THEN_RESET(mw->flags.hasInvalidRect)){
						PERFINFO_AUTO_START("InvalidateRect", 1);
							InvalidateRect(mw->hwnd, &mw->invalidRect, FALSE);
						PERFINFO_AUTO_STOP();
					}

					mw->flags.forcedPaint = 1;

					PERFINFO_AUTO_START("UpdateWindow", 1);
						UpdateWindow(mw->hwnd);
					PERFINFO_AUTO_STOP();

					mw->flags.forcedPaint = 0;

					mw->lastDrawTime = max(mw->lastDrawTime, timeGetTime());
				}
				PERFINFO_AUTO_STOP();
			}
		}
	}
	PERFINFO_AUTO_STOP();
}

static void suiMainWaitForEventsAndMsgs(SUIMainWindow* mw,
										U32 msTimeout,
										U32 wakeMask)
{
	U32	waitResult;
	U32 eventCount = eaSize(&mw->events.hEvents);

	if(msTimeout){
		PERFINFO_AUTO_START("MsgWaitForMultipleObjects(>0)", 1);
	}else{
		PERFINFO_AUTO_START("MsgWaitForMultipleObjects(0)", 1);
	}

	waitResult = MsgWaitForMultipleObjects(	eventCount,
											mw->events.hEvents,
											FALSE,
											msTimeout,
											wakeMask);
	
	PERFINFO_AUTO_STOP();

	if(waitResult >= WAIT_OBJECT_0){
		if(waitResult == WAIT_OBJECT_0 + eventCount){
			// Has a msg.
			
			PERFINFO_AUTO_START("has a msg", 1);

			mw->flags.checkForMsgs = 1;

			waitResult = WaitForMultipleObjects(eventCount,
												mw->events.hEvents,
												FALSE,
												0);
			
			PERFINFO_AUTO_STOP();
		}

		if(waitResult < WAIT_OBJECT_0 + eventCount){
			// One of the waited-on handles was set.

			SUIMainWindowEvent* e = mw->events.events[waitResult];

			PERFINFO_AUTO_START("eventCallback", 1);
			e->callback(mw, e->userPointer, e->hEvent);
			PERFINFO_AUTO_STOP();
		}
	}
}

static void suiMainTimerMsgInc(SUIMainWindow* mw){
	if(!mw->timerRefCount++){
		SetTimer(mw->hwnd, 0, 10, NULL);
	}
}

static void suiMainTimerMsgDec(SUIMainWindow* mw){
	assert(mw->timerRefCount);

	if(!--mw->timerRefCount){
		KillTimer(mw->hwnd, 0);
	}
}

static LRESULT suiMainWindowProcTimed(	HWND hwnd,
										UINT uMsg,
										WPARAM wParam,
										LPARAM lParam)
{
	SUIMainWindow*	mw = (SUIMainWindow*)(intptr_t)GetWindowLongPtr(hwnd, GWLP_USERDATA);

	if(!mw){
		return suiMainDefWindowProc(hwnd, uMsg, wParam, lParam);
	}

	hwnd = mw->hwnd;

	switch(uMsg){
		xcase WM_HOTKEY:{
			if(	IsWindowVisible(hwnd) &&
				GetForegroundWindow() == hwnd)
			{
				suiMainWindowSetHidden(mw);
			}else{
				suiMainWindowSetVisible(mw);
			}
		}

		xcase WM_FROM_NOTIFY_ICON:{
			// The notification icon is being interacted with.

			if(	lParam == WM_LBUTTONDOWN ||
				lParam == WM_LBUTTONDBLCLK ||
				lParam == WM_MBUTTONDOWN ||
				lParam == WM_MBUTTONDBLCLK ||
				lParam == WM_RBUTTONDOWN ||
				lParam == WM_RBUTTONDBLCLK)
			{
				if(	IsWindowVisible(hwnd) &&
					!IsIconic(hwnd))
				{
					suiMainWindowSetHidden(mw);
				}else{
					suiMainWindowSetVisible(mw);
				}
			}
		}

		xcase WM_SHOWWINDOW:{
			// TODO
		}

		//xcase WM_INPUT:{
		//	printf("wm_input: %d, %d, %d\n", uMsg, wParam, lParam);
		//}

		xcase WM_CLOSE:{
			//suiMainWindowSetHidden(mw);

			mw->flags.destroyMe = 1;
		}
		
		xcase WM_TIMER:{
			if(!wParam){
				suiMainWaitForEventsAndMsgs(mw, 0, 0);
				suiMainCheckProcessAndDrawTime(mw);
			}
		}
		
		xcase WM_NCLBUTTONDOWN:{
			switch(wParam){
				xcase HTCLOSE:
				acase HTMINBUTTON:
				acase HTMAXBUTTON:{
					mw->ncButtonDown = wParam;
					return 1;
				}
			}
		}
		
		xcase WM_NCLBUTTONUP:{
			S32 handled = 1;
			
			if(wParam == mw->ncButtonDown){
				switch(wParam){
					xcase HTCLOSE:{
						SendMessage(mw->hwnd, WM_CLOSE, 0, 0);
					}

					xcase HTMINBUTTON:{
						ShowWindow(mw->hwnd, SW_MINIMIZE);
					}

					xcase HTMAXBUTTON:{
						WINDOWPLACEMENT wp;
						
						wp.length = sizeof(wp);
						GetWindowPlacement(mw->hwnd, &wp);
						
						if(wp.showCmd == SW_MAXIMIZE){
							ShowWindow(mw->hwnd, SW_RESTORE);
						}else{
							ShowWindow(mw->hwnd, SW_MAXIMIZE);
						}
					}
					
					xdefault:{
						handled = 0;
					}
				}
			}

			mw->ncButtonDown = 0;
			
			if(handled){
				return 1;
			}
		}

		xcase WM_CONTEXTMENU:{
		}
		
		xcase WM_ENTERMENULOOP:{
			suiMainTimerMsgInc(mw);
		}

		xcase WM_EXITMENULOOP:{
			suiMainTimerMsgDec(mw);
		}

		xcase WM_ENTERSIZEMOVE:{
			suiMainTimerMsgInc(mw);
		}
		
		xcase WM_EXITSIZEMOVE:{
			suiMainTimerMsgDec(mw);
		}
		
		xcase WM_SETFOCUS:{
			return 1;
		}

		xcase WM_KILLFOCUS:{
			suiRootWindowLoseFocus(mw->suiRootWindow);
			return 1;
		}

		#if 0
		xcase WM_WINDOWPOSCHANGED:{
			return 1;
		}

		xcase WM_WINDOWPOSCHANGING:{
			WINDOWPOS* p = (WINDOWPOS*)lParam;

			//p->flags |= SWP_NOREDRAW | SWP_FRAMECHANGED;

			//printf("");//SWP_NOREDRAW

			return 0;
		}

		xcase WM_NCCALCSIZE:{
			if(wParam){
				NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lParam;

				printf("------------------------\n");
				printf(	"before[0]: (%d, %d) - (%d, %d)\n",
						p->rgrc[0].left,
						p->rgrc[0].top,
						p->rgrc[0].right,
						p->rgrc[0].bottom);

				printf(	"before[1]: (%d, %d) - (%d, %d)\n",
						p->rgrc[1].left,
						p->rgrc[1].top,
						p->rgrc[1].right,
						p->rgrc[1].bottom);

				printf(	"before[2]: (%d, %d) - (%d, %d)\n",
						p->rgrc[2].left,
						p->rgrc[2].top,
						p->rgrc[2].right,
						p->rgrc[2].bottom);

				p->rgrc[1].left = p->rgrc[0].left;
				p->rgrc[1].top = p->rgrc[0].top;

				if(0){
					//p->rgrc[0].left = p->rgrc[1].left;
					//p->rgrc[0].top = p->rgrc[1].top;
					p->rgrc[1].left = p->rgrc[0].left;
					p->rgrc[1].top = p->rgrc[0].top;
					p->rgrc[1].right = p->rgrc[0].right;
					p->rgrc[1].bottom = p->rgrc[0].bottom;

					p->rgrc[2].left = p->rgrc[0].left;
					p->rgrc[2].top = p->rgrc[0].top;
					p->rgrc[2].right = p->rgrc[0].right;
					p->rgrc[2].bottom = p->rgrc[0].bottom;
					//p->rgrc[2].right = p->rgrc[2].left + 5;
					//p->rgrc[2].bottom = p->rgrc[2].top + 5;

					printf(	"after[1]: (%d, %d) - (%d, %d)\n",
							p->rgrc[1].left,
							p->rgrc[1].top,
							p->rgrc[1].right,
							p->rgrc[1].bottom);

					printf(	"after[2]: (%d, %d) - (%d, %d)\n",
							p->rgrc[2].left,
							p->rgrc[2].top,
							p->rgrc[2].right,
							p->rgrc[2].bottom);
				}

				printf("Sleeping: ");
				//Sleep(1000);
				printf("Done!\n");

				return WVR_REDRAW;
			}else{
				printf("What!\n");
			}
			return 1;
		}
		#endif

		#if QUERY_ON_END_SESSION
		xcase WM_QUERYENDSESSION:{
			S32 result = MessageBox(hwnd,
									"Are you sure you want to quit windows?",
									"Quit windows?  Really?",
									MB_YESNO);

			return result == IDYES;
		}
		#endif

		xcase WM_SETCURSOR:{
			//printf("WM_SETCURSOR: %d\n", wParam);

			return suiMainDefWindowProc(hwnd, uMsg, wParam, lParam);
		}

		xcase WM_MOUSELEAVE:{
			//printf("WM_MOUSELEAVE!\n");

			suiRootWindowMouseLeave(mw->suiRootWindow);

			mw->flags.mouseInWindow = 0;
			return 1;
		}

		xcase WM_CAPTURECHANGED:{
			//printf("WM_CAPTURECHANGED\n");
			return 1;
		}

		xcase WM_INITMENU:{
			//printf("init menu!\n");
			return 1;
		}

		#define LONGSLEEP_TIME 10

		xcase WM_PAINT:{
			if(!mw->flags.ignorePaint){
				suiMainWindowDraw(mw, NULL, 1);

				if(TRUE_THEN_RESET(mw->flags.longSleep)){
					Sleep(LONGSLEEP_TIME);
				}else{
					//Sleep(1);
				}
			}
			return 1;
		}

		xcase WM_ERASEBKGND:{
			if(!mw->flags.ignorePaint){
				HDC hdc = (HDC)wParam;

				#if PRINT_PAINT_MSGS
					printClipBox(mw, "WM_ERASEBKGND", hdc);
				#endif

				suiMainWindowDraw(mw, hdc, 0);

				if(TRUE_THEN_RESET(mw->flags.longSleep)){
					Sleep(LONGSLEEP_TIME);
				}else{
					//Sleep(1);
				}
			}
			return 1;
		}

		#undef LONGSLEEP_TIME

		xcase WM_SIZE:{
			POINTS	pt = MAKEPOINTS(lParam);

			GetClientRect(mw->hwnd, &mw->rectCur);
			suiMainClientRectToScreenRect(mw, &mw->rectCur);
			mw->rectQueued = mw->rectCur;

			//printf(	"WM_SIZE: %d, %d\n",
			//		pt.x,
			//		pt.y);

			suiRootWindowSetSize(mw->suiRootWindow, pt.x, pt.y);

			//suiMainWindowInvalidateRect(mw, NULL);
			return 1;
		}

		xcase WM_MOVE:{
			POINTS pt = MAKEPOINTS(lParam);

			//printf(	"WM_MOVE: %d, %d\n",
			//		pt.x,
			//		pt.y);

			mw->rectCur.right = pt.x + mw->rectCur.right - mw->rectCur.left;
			mw->rectCur.left = pt.x;
			mw->rectCur.bottom = pt.y + mw->rectCur.bottom - mw->rectCur.top;
			mw->rectCur.top = pt.y;

			mw->rectQueued = mw->rectCur;
			return 1;
		}

		xcase WM_MOUSEMOVE:{
			suiMainWindowMouseMove(mw, wParam, lParam);

			//if(mw->buttonsHeld & SUI_MBUTTON_LEFT){
			//	RECT rect;
			//	int dx = (S16)(lParam & 0xffff) - x;
			//	int dy = (S16)((lParam >> 16) & 0xffff) - y;
			//	GetWindowRect(hwnd, &rect);
			//	if(0){
			//		rect.left += dx;
			//		rect.top += dy;
			//	}else{
			//		rect.right += dx;
			//		rect.bottom += dy;
			//		x += dx;
			//		y += dy;
			//	}
			//	SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER);
			//	InvalidateRect(hwnd, NULL, FALSE);
			//	//mw->flags.hasDirtyRect = 1;
			//	Sleep(0);
			//}
			//else if(mw->buttonsHeld & SUI_MBUTTON_RIGHT){
			//	RECT rect;
			//	int dx = (S16)(lParam & 0xffff) - x;
			//	int dy = (S16)((lParam >> 16) & 0xffff) - y;
			//	GetWindowRect(hwnd, &rect);
			//	rect.right += dx;
			//	rect.bottom += dy;
			//	rect.left += dx;
			//	rect.top += dy;
			//	//x += dx;
			//	//y += dy;
			//	SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER);
			//	//InvalidateRect(hwnd, NULL, FALSE);
			//	//mw->flags.hasDirtyRect = 1;
			//	Sleep(0);
			//}
			return 1;
		}

		xcase WM_MOUSEWHEEL:{
			suiMainWindowMouseWheel(mw, wParam, lParam);
			return 1;
		}

		xcase WM_LBUTTONDOWN:{
			suiMainWindowMouseDown(mw, SUI_MBUTTON_LEFT, wParam, lParam);
			return 1;
		}

		xcase WM_LBUTTONDBLCLK:{
			suiMainWindowMouseDoubleClick(mw, SUI_MBUTTON_LEFT, wParam, lParam);
			return 1;
		}

		xcase WM_LBUTTONUP:{
			mw->ncButtonDown = 0;
			suiMainWindowMouseUp(mw, SUI_MBUTTON_LEFT, wParam, lParam);
			return 1;
		}

		xcase WM_RBUTTONDOWN:{
			suiMainWindowMouseDown(mw, SUI_MBUTTON_RIGHT, wParam, lParam);
			return 1;
		}

		xcase WM_RBUTTONDBLCLK:{
			suiMainWindowMouseDoubleClick(mw, SUI_MBUTTON_RIGHT, wParam, lParam);
			return 1;
		}

		xcase WM_RBUTTONUP:{
			suiMainWindowMouseUp(mw, SUI_MBUTTON_RIGHT, wParam, lParam);
			return 1;
		}

		xcase WM_MBUTTONDOWN:{
			suiMainWindowMouseDown(mw, SUI_MBUTTON_MIDDLE, wParam, lParam);
			return 1;
		}

		xcase WM_MBUTTONDBLCLK:{
			suiMainWindowMouseDoubleClick(mw, SUI_MBUTTON_MIDDLE, wParam, lParam);
			return 1;
		}

		xcase WM_MBUTTONUP:{
			suiMainWindowMouseUp(mw, SUI_MBUTTON_MIDDLE, wParam, lParam);
			return 1;
		}

		xcase WM_SYSKEYDOWN:{
			PRINT_KEY_MSG(WM_SYSKEYDOWN);

			return suiMainWindowKey(mw, uMsg, wParam, lParam, 0, 1);
		}

		xcase WM_KEYDOWN:{
			PRINT_KEY_MSG(WM_KEYDOWN);

			return suiMainWindowKey(mw, uMsg, wParam, lParam, 0, 1);
		}

		xcase WM_SYSKEYUP:{
			PRINT_KEY_MSG(WM_SYSKEYUP);

			return suiMainWindowKey(mw, uMsg, wParam, lParam, 0, 0);
		}

		xcase WM_KEYUP:{
			PRINT_KEY_MSG(WM_KEYUP);

			return suiMainWindowKey(mw, uMsg, wParam, lParam, 0, 0);
		}

		xcase WM_SYSCHAR:
		acase WM_CHAR:{
			if(uMsg == WM_SYSCHAR){
				PRINT_KEY_MSG(WM_SYSCHAR);
			}else{
				PRINT_KEY_MSG(WM_CHAR);
			}

			if(TRUE_THEN_RESET(mw->flags.waitForCharMsg)){
				return suiMainWindowKey(mw,
										mw->lastKeyMsg.uMsg,
										mw->lastKeyMsg.wParam,
										mw->lastKeyMsg.lParam,
										wParam,
										1);
			}else{
				return suiMainWindowKey(mw,
										0,
										0,
										0,
										wParam,
										1);
			}
		}

		xcase WM_SYSCOMMAND:{
			//printf("syscommand: %d, %d\n", wParam, lParam);

			return suiMainDefWindowProc(hwnd, uMsg, wParam, lParam);
		}

		xdefault:{
			if(uMsg == taskBarCreateMessage){
				suiMainNotifyIconSetInternal(mw);
				return 1;
			}
			else if(uMsg == shellHookMsg){
				//printf("shell: %d, %d, %d\n", uMsg, wParam, lParam);
				return 1;
			}
		}
	}

	//printf("unhandled: 0x%4.4x, w=0x%8.8x, l=%8.8x\n", uMsg, wParam, lParam);

	return suiMainDefWindowProc(hwnd, uMsg, wParam, lParam);
}

typedef struct MsgTimer {
	U32				msg;
	char*			name;
	PERFINFO_TYPE*	pi;
} MsgTimer;

static void suiMainWinMsgTimerStart(UINT uMsg){
	if(PERFINFO_RUN_CONDITIONS){
		switch(uMsg){
			#if 0
				#define WM_NOTIFY                       0x004E
				#define WM_INPUTLANGCHANGEREQUEST       0x0050
				#define WM_INPUTLANGCHANGE              0x0051
				#define WM_TCARD                        0x0052
				#define WM_HELP                         0x0053
				#define WM_USERCHANGED                  0x0054
				#define WM_NOTIFYFORMAT                 0x0055

				#define NFR_ANSI                             1
				#define NFR_UNICODE                          2
				#define NF_QUERY                             3
				#define NF_REQUERY                           4

				#define WM_CONTEXTMENU                  0x007B
				#define WM_STYLECHANGING                0x007C
				#define WM_STYLECHANGED                 0x007D
				#define WM_DISPLAYCHANGE                0x007E

				#define WM_NCCREATE                     0x0081
				#define WM_NCDESTROY                    0x0082
				#define WM_NCCALCSIZE                   0x0083
				#define WM_NCHITTEST                    0x0084
				#define WM_NCPAINT                      0x0085
				#define WM_NCACTIVATE                   0x0086
				#define WM_GETDLGCODE                   0x0087
			#endif
		
			#define CASE(x) xcase x:PERFINFO_AUTO_START("msg:"#x, 1);
				CASE(WM_NULL);
				CASE(WM_CREATE);
				CASE(WM_DESTROY);
				CASE(WM_MOVE);
				CASE(WM_SIZE);
				CASE(WM_ACTIVATE);
				CASE(WM_SETFOCUS);
				CASE(WM_KILLFOCUS);
				CASE(WM_ENABLE);
				CASE(WM_SETREDRAW);
				CASE(WM_SETTEXT);
				CASE(WM_GETTEXT);
				CASE(WM_GETTEXTLENGTH);
				CASE(WM_PAINT);
				CASE(WM_CLOSE);
				CASE(WM_QUERYENDSESSION);
				CASE(WM_QUERYOPEN);
				CASE(WM_ENDSESSION);
				CASE(WM_QUIT);
				CASE(WM_ERASEBKGND);
				CASE(WM_SYSCOLORCHANGE);
				CASE(WM_SHOWWINDOW);
				CASE(WM_WININICHANGE);
				
				CASE(WM_WINDOWPOSCHANGED);
				CASE(WM_WINDOWPOSCHANGING);
				CASE(WM_SETCURSOR);
				CASE(WM_MOUSEACTIVATE);
				CASE(WM_MOUSELEAVE);
				CASE(WM_MOUSEHOVER);
				CASE(WM_INITMENU);
				CASE(WM_GETMINMAXINFO);

				CASE(WM_NEXTMENU);
				CASE(WM_SIZING);
				CASE(WM_CAPTURECHANGED);
				CASE(WM_MOVING);

				CASE(WM_MOUSEMOVE);
				CASE(WM_LBUTTONDOWN);
				CASE(WM_LBUTTONDBLCLK);
				CASE(WM_LBUTTONUP);
				CASE(WM_RBUTTONDOWN);
				CASE(WM_RBUTTONDBLCLK);
				CASE(WM_RBUTTONUP);
				CASE(WM_MBUTTONDOWN);
				CASE(WM_MBUTTONDBLCLK);
				CASE(WM_MBUTTONUP);

				CASE(WM_NCCREATE);
				CASE(WM_NCDESTROY);
				CASE(WM_NCCALCSIZE);
				CASE(WM_NCHITTEST);
				CASE(WM_NCPAINT);
				CASE(WM_NCACTIVATE);
				CASE(WM_GETDLGCODE);

				CASE(WM_NCMOUSEMOVE);
				CASE(WM_NCLBUTTONDOWN);
				CASE(WM_NCLBUTTONUP);
				CASE(WM_NCLBUTTONDBLCLK);
				CASE(WM_NCRBUTTONDOWN);
				CASE(WM_NCRBUTTONUP);
				CASE(WM_NCRBUTTONDBLCLK);
				CASE(WM_NCMBUTTONDOWN);
				CASE(WM_NCMBUTTONUP);
				CASE(WM_NCMBUTTONDBLCLK);

				CASE(WM_NCMOUSEHOVER);
				CASE(WM_NCMOUSELEAVE);

				CASE(WM_KEYDOWN);
				CASE(WM_KEYUP);

				CASE(WM_CHAR);
				CASE(WM_DEADCHAR);

				CASE(WM_SYSCOMMAND);
				CASE(WM_SYSKEYDOWN);
				CASE(WM_SYSKEYUP);
				CASE(WM_SYSCHAR);
				CASE(WM_SYSDEADCHAR);
				
				CASE(WM_GETICON);
				CASE(WM_SETICON);
				
				CASE(WM_ENTERSIZEMOVE);
				CASE(WM_EXITSIZEMOVE);
				CASE(WM_DROPFILES);
				
				CASE(WM_CONTEXTMENU);
				CASE(WM_ENTERMENULOOP);
				CASE(WM_EXITMENULOOP);

				CASE(WM_TIMER);
				
				CASE(WM_FROM_NOTIFY_ICON);
				
				CASE(WM_ACTIVATEAPP);
				
				CASE(WM_IME_SETCONTEXT);
				CASE(WM_IME_STARTCOMPOSITION);
				CASE(WM_IME_ENDCOMPOSITION);
				CASE(WM_IME_COMPOSITION);
				CASE(WM_IME_NOTIFY);
				CASE(WM_IME_CONTROL);
				CASE(WM_IME_COMPOSITIONFULL);
				CASE(WM_IME_SELECT);
				CASE(WM_IME_CHAR);
				CASE(WM_IME_REQUEST);
				CASE(WM_IME_KEYDOWN);
				CASE(WM_IME_KEYUP);
				
				CASE(WM_INITDIALOG);
				CASE(WM_COMMAND);
				CASE(WM_HSCROLL);
				CASE(WM_VSCROLL);
				CASE(WM_INITMENUPOPUP);
				CASE(WM_MENUSELECT);
				CASE(WM_MENUCHAR);
				CASE(WM_ENTERIDLE);
				CASE(WM_MENURBUTTONUP);
				CASE(WM_MENUDRAG);
				CASE(WM_MENUGETOBJECT);
				CASE(WM_UNINITMENUPOPUP);
				CASE(WM_MENUCOMMAND);
			#undef CASE

			xdefault:{
				static MsgTimer**			timers;
				static CRITICAL_SECTION		csCreateUnknownTimer;
				MsgTimer*					t = NULL;

				ATOMIC_INIT_BEGIN;
				{
					InitializeCriticalSection(&csCreateUnknownTimer);
				}
				ATOMIC_INIT_END;

				EnterCriticalSection(&csCreateUnknownTimer);
				{
					EARRAY_CONST_FOREACH_BEGIN(timers, i, isize);
						if(timers[i]->msg == uMsg){
							t = timers[i];
							break;
						}
					EARRAY_FOREACH_END;

					if(!t){
						t = callocStruct(MsgTimer);
						t->msg = uMsg;
						t->name = strdupf("msg:0x%4.4x", uMsg);
						eaPush(&timers, t);
					}
				}
				LeaveCriticalSection(&csCreateUnknownTimer);

				PERFINFO_AUTO_START_STATIC(t->name, &t->pi, 1);
			}
		}
	}
}

static void suiMainWinMsgTimerStop(void){
	PERFINFO_AUTO_STOP();
}

static LRESULT CALLBACK suiMainWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
	LRESULT retVal;
	
	PERFINFO_AUTO_START("suiMainWindowProcTimed", 1);
		suiMainWinMsgTimerStart(uMsg);
		retVal = suiMainWindowProcTimed(hwnd, uMsg, wParam, lParam);
		suiMainWinMsgTimerStop();
	PERFINFO_AUTO_STOP();

	return retVal;
}

static const char* suiWindowClassName = "SimpleUIWindowClass";

static S32 suiInitOSWindowClass(void){
	static S32 success;

	ATOMIC_INIT_BEGIN;
	{
		WNDCLASSEX winClass = {0};

		winClass.cbSize			= sizeof(winClass);
		winClass.style			= CS_OWNDC | CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
		winClass.lpfnWndProc	= (WNDPROC)suiMainWindowProc;
		winClass.cbClsExtra		= 0;
		winClass.cbWndExtra		= 0;
		winClass.hInstance		= GetModuleHandle(NULL);
		winClass.hCursor		= LoadCursor( NULL, IDC_ARROW );
		winClass.hbrBackground	= 1 ? NULL : CreateSolidBrush(RGB(255,255,0));
		winClass.lpszMenuName	= NULL;
		winClass.lpszClassName	= suiWindowClassName;

		if(RegisterClassEx(&winClass)){
			success = 1;
		}
	}
	ATOMIC_INIT_END;

	return success;
}

void suiMainWindowDestroy(SUIMainWindow** mwInOut){
	SUIMainWindow* mw = SAFE_DEREF(mwInOut);

	if(!mw){
		return;
	}

	suiMainShellIconDestroy(mw);

	if(mw->buffer.hBitmap){
		DeleteObject(mw->buffer.hBitmap);
		mw->buffer.hBitmap = NULL;
	}

	suiRootWindowDestroy(&mw->suiRootWindow);

	DestroyWindow(mw->hwnd);

	SAFE_FREE(mw->name);
	estrDestroy(&mw->titleBar);

	SAFE_FREE(*mwInOut);
}

typedef struct DesktopDimensions {
	RECT			rect;
	S32				width;
	S32				height;
} DesktopDimensions;

// Max: 1745x1202

static void suiGetDesktopDimensions(DesktopDimensions* dd){
	GetWindowRect(GetDesktopWindow(), &dd->rect);

	MIN1(dd->rect.right, MAX_WIDTH);
	MIN1(dd->rect.bottom, MAX_HEIGHT);

	dd->width = dd->rect.right - dd->rect.left;
	dd->height = dd->rect.bottom - dd->rect.top;
}

static S32 suiMainWindowCreateOSWindow(	SUIMainWindow* mw,
										const DesktopDimensions* desktop,
										U32 style)
{
	#define CHOOSE_STYLE(x, y, z)	(style & (x) ? (y) : (z))
	#define CONVERT_STYLE(x, y)		CHOOSE_STYLE(x, y, 0)

	DWORD windowStyle = WS_POPUP
						|
						CONVERT_STYLE(SUI_MWSTYLE_TASKBAR_BUTTON, WS_SYSMENU)
						|
						CONVERT_STYLE(	SUI_MWSTYLE_BORDER,
										WS_CAPTION |
										WS_SIZEBOX |
										WS_OVERLAPPED |
										WS_MAXIMIZEBOX |
										WS_MINIMIZEBOX |
										WS_SYSMENU)
						| 0;

	srand(timerCpuTicks());

	mw->hwnd = CreateWindowEx(	CHOOSE_STYLE(SUI_MWSTYLE_TASKBAR_BUTTON, 0, WS_EX_TOOLWINDOW),
								suiWindowClassName,
								mw->name,
								windowStyle,
								desktop->rect.left + 100 + (rand() % 101 - 50),
								desktop->rect.top + 100 + (rand() % 101 - 50),
								desktop->width - 200,
								desktop->height - 200,
								NULL,
								NULL,
								GetModuleHandle(0),
								NULL);

	#undef CONVERT_STYLE
	#undef CHOOSE_STYLE

	if(!mw->hwnd){
		return 0;
	}

	GetClientRect(mw->hwnd, &mw->rectCur);
	suiMainClientRectToScreenRect(mw, &mw->rectCur);
	mw->rectQueued = mw->rectCur;

	suiMainSetSizeToQueuedRect(mw);
	//GetWindowRect(mw->hwnd, &mw->rectCur);
	//mw->rectQueued = mw->rectCur;

	//printf("menu: %p, %d\n", GetSystemMenu(mw->hwnd, FALSE), GetLastError());

	setWindowIconColoredLetter(mw->hwnd, 'P', 0xffcc88);

	if(0){
		HMODULE hModule = LoadLibrary("user32.dll");

		if(hModule){
			typedef BOOL (__stdcall *SetShellWindowFuncType)(HWND hwnd);

			SetShellWindowFuncType SetShellWindowFunc;

			SetShellWindowFunc = (SetShellWindowFuncType)GetProcAddress(hModule, "SetShellWindow");

			if(SetShellWindowFunc){
				if(SetShellWindowFunc(mw->hwnd)){
					printf("Set shell window!\n");
				}else{
					printf("Failed to set shell window!\n");
				}
			}else{
				printf("Failed to find SetShellWindow!\n");
			}

			FreeLibrary(hModule);
		}else{
			printf("Can't load module!\n");
		}
	}

	//RegisterShellHookWindow(mw->hwnd);

	shellHookMsg = RegisterWindowMessage("SHELLHOOK");

	if(0){
		RAWINPUTDEVICE Rid[2];

		Rid[0].usUsagePage = 01;
		Rid[0].usUsage = 02;
		Rid[0].dwFlags = RIDEV_NOLEGACY;   // adds HID mouse and also ignores legacy mouse messages
		Rid[0].hwndTarget = mw->hwnd;

		Rid[1].usUsagePage = 01;
		Rid[1].usUsage = 06;
		Rid[1].dwFlags = RIDEV_NOLEGACY;   // adds HID keyboard and also ignores legacy keyboard messages
		Rid[1].hwndTarget = mw->hwnd;

		if (RegisterRawInputDevices(Rid, 2, sizeof (Rid[0])) == FALSE) {
			//registration failed. Call GetLastError for the cause of the error
		}
	}

	return 1;
}

typedef struct SUIMainWindowTopLevel {
	SUIMainWindow*	mw;
	U32				bgColor;
} SUIMainWindowTopLevel;

typedef struct SUIMainRootWindowCreateParams {
	SUIMainWindow* mw;
} SUIMainRootWindowCreateParams;

static S32 suiMainRootWindowMsgHandler(	SUIWindow* w,
										SUIMainWindowTopLevel* wd,
										const SUIWindowMsg* msg)
{
	SUIMainWindow* mw = SAFE_MEMBER(wd, mw);

	ANALYSIS_ASSUME(mw);

	SUI_WM_DEFAULT_HANDLERS_BEGIN(w, msg, SUIMainWindowTopLevel, wd);
		SUI_WM_CASE(SUI_WM_CREATE){
			const SUIMainRootWindowCreateParams* cp = msg->msgData;

			assert(!wd);

			wd = callocStruct(SUIMainWindowTopLevel);

			wd->mw = cp->mw;

			srand(time(NULL) ^ PTR_TO_U32(wd));

			#if 0
			{
				wd->bgColor =	0xff000000 |
								((rand() % 40) << 16) |
								((rand() % 20) << 8) |
								((rand() % 20) << 0);
			}
			#else
			{
				wd->bgColor = 0xff00020e;
			}
			#endif

			suiWindowSetUserPointer(w, suiMainRootWindowMsgHandler, wd);
		}

		SUI_WM_CASE(SUI_WM_INVALIDATE_RECT){
			suiMainWindowInvalidateRect(mw, msg->msgData);
		}

		SUI_WM_CASE(SUI_WM_GET_TEXT_SIZE){
			PERFINFO_AUTO_START("suiMainGetTextSize", 1);
			{
				const SUIWindowMsgGetTextSize*	md = msg->msgData;
				S32								gotDC = 0;
				SIZE							size;

				if(!mw->hdc){
					mw->hdc = GetDC(mw->hwnd);
					gotDC = 1;
				}

				SelectObject(mw->hdc, getFont(md->height));

				if(GetTextExtentPoint32(mw->hdc,
										md->text,
										(int)strlen(md->text),
										&size))
				{
					md->out->flags.gotSize = 1;
					md->out->size[0] = size.cx;
					md->out->size[1] = size.cy;
				}

				if(gotDC){
					ReleaseDC(mw->hwnd, mw->hdc);
					mw->hdc = NULL;
				}
			}
			PERFINFO_AUTO_STOP();
		}

		SUI_WM_CASE(SUI_WM_MOUSE_DOWN){
			const SUIWindowMsgMouseButton* mm = msg->msgData;

			if(	!IsZoomed(mw->hwnd) &&
				!mw->anchor.held)
			{
				mw->anchor.held = mm->button;
				mw->anchor.x = mm->x;
				mw->anchor.y = mm->y;
			}

			return 1;
		}

		SUI_WM_CASE(SUI_WM_MOUSE_UP){
			const SUIWindowMsgMouseButton* mm = msg->msgData;

			if(mm->button == mw->anchor.held){
				mw->anchor.held = 0;
			}

			return 1;
		}

		SUI_WM_CASE(SUI_WM_DRAW){
			PERFINFO_AUTO_START("suiMainDraw", 1);
			{
				const SUIDrawContext*	dc = msg->msgData;
				S32						sx = suiWindowGetSizeX(w);
				S32						sy = suiWindowGetSizeY(w);

				suiDrawFilledRect(	dc,
									0,
									0,
									sx,
									sy,
									wd->bgColor);

				suiDrawRect(dc, 3, 3, sx - 6, sy - 6, 3, 0xff332233);
				suiDrawRect(dc, 0, 0, sx, sy, 3, 0xff000000);

				#if 0
				{
					char	buffer[100];
					S32		tsx;

					sprintf(buffer,
							"Cur: [%d x %d] (%d, %d) - (%d, %d)\n",
							mw->rectCur.right - mw->rectCur.left,
							mw->rectCur.bottom - mw->rectCur.top,
							mw->rectCur.left,
							mw->rectCur.top,
							mw->rectCur.right,
							mw->rectCur.bottom);

					suiWindowGetTextSize(w, buffer, &tsx, NULL);
					suiPrintText(dc, sx - tsx - 20, 10, buffer, -1, 0xff606060);
				}
				#endif
			}
			PERFINFO_AUTO_STOP();
		}

		SUI_WM_CASE(SUI_WM_MOUSE_MOVE){
			const SUIWindowMsgMouseMove*	mm = msg->msgData;
			S32 							dx = mm->x - mw->anchor.x;
			S32 							dy = mm->y - mw->anchor.y;

			if(	mw->anchor.held &&
				IsZoomed(mw->hwnd))
			{
				mw->anchor.held = 0;
			}

			switch(mw->anchor.held){
				xcase SUI_MBUTTON_LEFT:{
					mw->rectQueued.left = mw->rectCur.left + dx;
					mw->rectQueued.top = mw->rectCur.top + dy;
					mw->rectQueued.right = mw->rectCur.right + dx;
					mw->rectQueued.bottom = mw->rectCur.bottom + dy;

					#if 0
						printf(	"moved:                 %4d, %4d, %4d, %4d\n",
								mw->rectQueued.left,
								mw->rectQueued.top,
								mw->rectQueued.right,
								mw->rectQueued.bottom);
					#endif

					//mw->flags.longSleep = 1;
				}

				xcase SUI_MBUTTON_MIDDLE:{
					#if 0
						S32 sx = mw->rectCur.right - mw->rectCur.left + dx * 2;
						S32 sy = mw->rectCur.bottom - mw->rectCur.top + dy * 2;

						sx = min(sx, mw->buffer.size.x);
						sy = min(sy, mw->buffer.size.y);

						longSleep = 1;
					#else
						S32 sx = mw->rectQueued.right - mw->rectQueued.left - dx;
						S32 sy = mw->rectQueued.bottom - mw->rectQueued.top - dy;

						MINMAX1(sx, 100, mw->buffer.size.x);
						MINMAX1(sy, 100, mw->buffer.size.y);

						//sx += 100;
						//sy += 100;

						mw->rectQueued.left = mw->rectQueued.right - sx;
						mw->rectQueued.top = mw->rectQueued.bottom - sy;

						//mw->anchor.x += dx;
						//mw->anchor.y += dy;
					#endif
				}

				xcase SUI_MBUTTON_RIGHT:{
					S32 adx = mw->anchor.x - mw->rectQueued.right;
					S32 ady = mw->anchor.y - mw->rectQueued.bottom;
					S32 sx = mw->rectQueued.right - mw->rectQueued.left + dx;
					S32 sy = mw->rectQueued.bottom - mw->rectQueued.top + dy;

					MINMAX1(sx, 100, mw->buffer.size.x);
					MINMAX1(sy, 100, mw->buffer.size.y);

					mw->rectQueued.right = mw->rectQueued.left + sx;
					mw->rectQueued.bottom = mw->rectQueued.top + sy;

					mw->anchor.x = mw->rectQueued.right + adx;
					mw->anchor.y = mw->rectQueued.bottom + ady;
				}
			}

			return 1;
		}
	SUI_WM_HANDLERS_END;

	return 0;
}

static S32 suiMainWindowCreateRootWindow(	SUIMainWindow* mw,
											const DesktopDimensions* desktop)
{
	SUIMainRootWindowCreateParams	cp = {0};
	RECT							rect;

	cp.mw = mw;

	if(!suiRootWindowCreate(&mw->suiRootWindow,
							suiMainRootWindowMsgHandler,
							&cp))
	{
		return 0;
	}

	//suiWindowSetPos(mw->suiRootWindow, 0, 0);

	GetClientRect(mw->hwnd, &rect);
	suiMainClientRectToScreenRect(mw, &rect);

	suiRootWindowSetSize(	mw->suiRootWindow,
							rect.right - rect.left,
							rect.bottom - rect.top);

	return 1;
}

S32 suiMainWindowCreateBackBuffer(	SUIMainWindow* mw,
									const DesktopDimensions* desktop)
{
	HDC hdc = GetDC(mw->hwnd);
	S32 width = MAX(MAX_WIDTH, desktop->width);
	S32 height = MAX(MAX_HEIGHT, desktop->height);

	if(!hdc){
		printf(	"%d: Failed to get DC.\n",
				GetCurrentThreadId());
	}

	#if USE_DIB
	{
		BITMAPINFO	bi = {0};
		void*		buffer;

		mw->buffer.size.x = 2000;
		mw->buffer.size.y = 2000;

		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = mw->buffer.size.x;
		bi.bmiHeader.biHeight = mw->buffer.size.y;
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;

		mw->buffer.hBitmap = CreateDIBSection(	NULL,
												&bi,
												DIB_RGB_COLORS,
												&buffer,
												NULL,
												0);
	}
	#elif USE_DDB
	{
		BITMAPINFOHEADER bih = {0};

		mw->buffer.size.x = 2000;
		mw->buffer.size.y = 2000;

		bih.biSize = sizeof(bih);
		bih.biWidth = mw->buffer.size.x;
		bih.biHeight = mw->buffer.size.y;
		bih.biPlanes = 1;
		bih.biBitCount = 32;
		bih.biCompression = BI_RGB;

		mw->buffer.hBitmap = CreateDIBitmap(hdc,
											&bih,
											0,
											NULL,
											NULL,
											0);
	}
	#else
	{
		mw->buffer.size.x = width;
		mw->buffer.size.y = height;
		mw->buffer.hBitmap = CreateCompatibleBitmap(hdc, width, height);
	}
	#endif

	if(!mw->buffer.hBitmap){
		U32 error = GetLastError();

		printf(	"%d: Failed to create bitmap (%d).\n",
				GetCurrentThreadId(),
				error);
		return 0;
	}

	mw->invalidRect.left = 0;
	mw->invalidRect.top = 0;
	mw->invalidRect.right = width;
	mw->invalidRect.bottom = height;
	mw->flags.hasInvalidRect = 1;
	
	mw->dirtyRect = mw->invalidRect;
	mw->flags.hasDirtyRect = 1;

	if (hdc)
		ReleaseDC(mw->hwnd, hdc);

	return 1;
}

static void suiMainRegisterHotKey(SUIMainWindow* mw){
	//if(!mw->flags.registeredHotKey){
	//	PERFINFO_AUTO_START_FUNC();
	//		mw->flags.registeredHotKey = RegisterHotKey(mw->hwnd, 0, MOD_WIN, VK_SPACE);
	//	PERFINFO_AUTO_STOP();
	//}
}

S32 suiMainWindowCreate(SUIMainWindow** mwOut,
						const SUIMainWindowCreateParams* params)
{
	SUIMainWindow*		mw;
	DesktopDimensions	desktop;

	PERFINFO_AUTO_START_FUNC();

	ATOMIC_INIT_BEGIN;
	{
		taskBarCreateMessage = RegisterWindowMessage("TaskbarCreated");
	}
	ATOMIC_INIT_END;

	// Create the window class.

	if(	!mwOut ||
		!suiInitOSWindowClass())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}

	mw = callocStruct(SUIMainWindow);

	// Get the desktop dimensions.

	suiGetDesktopDimensions(&desktop);

	mw->name = strdup(NULL_TO_EMPTY(SAFE_MEMBER(params, name)));

	if(suiMainWindowCreateOSWindow(	mw,
									&desktop,
									SAFE_MEMBER(params, style)))
	{
		//printf(	"%d: Created OS window.\n",
		//		GetCurrentThreadId());

		// Create the backbuffer.

		if(suiMainWindowCreateBackBuffer(mw, &desktop)){
			//printf(	"%d: Created back buffer.\n",
			//		GetCurrentThreadId());

			// Create the top-level window.

			if(suiMainWindowCreateRootWindow(mw, &desktop)){
				//printf(	"%d: Created root window.\n",
				//		GetCurrentThreadId());

				// Done.

				SetWindowLongPtr(mw->hwnd, GWLP_USERDATA, (LONG_PTR)mw);

				suiMainRegisterHotKey(mw);

				*mwOut = mw;

				return 1;
			}
		}
	}

	return 0;
}

S32 suiMainWindowCreateBasic(	SUIMainWindow** mwOut,
								const char* name,
								U32 style)
{
	SUIMainWindowCreateParams params = {0};

	params.name = name;
	params.style = style;

	return suiMainWindowCreate(mwOut, &params);
}

static void suiMainWindowDoFirstProcess(SUIMainWindow* mw){
	if(FALSE_THEN_SET(mw->flags.hasProcessed)){
		PERFINFO_AUTO_START_FUNC();

		suiMainWindowInvalidateRect(mw, NULL);

		ShowWindow(mw->hwnd, mw->flags.minimizeOnShow ? SW_MINIMIZE : SW_SHOW);

		suiMainNotifyIconSetInternal(mw);

		PERFINFO_AUTO_STOP();
	}
}

static void suiMainWindowProcessMsgs(SUIMainWindow* mw){
	U32	redrawPeriod;
	MSG	msg;
	U32	msStartTime = timeGetTime();
	U32 msTimeOut;

	PERFINFO_AUTO_START("top", 1);
	{
		suiMainRegisterHotKey(mw);

		if(!mw->lastDrawTime){
			mw->lastDrawTime = mw->nextDrawTime = msStartTime;
		}
		
		if(mw->flags.waitForCharMsg){
			redrawPeriod = 0;
		}
		else if(mw->flags.hasDirtyRect &&
				IsWindowVisible(mw->hwnd)
				||
				suiRootWindowNeedsProcess(mw->suiRootWindow))
		{
			redrawPeriod = 1;
		}else{
			redrawPeriod = 1000;
		}
	}
	PERFINFO_AUTO_STOP();

	msTimeOut = redrawPeriod ?
					MINMAX(	mw->lastDrawTime + redrawPeriod - msStartTime,
						1,
						redrawPeriod) :
					0;

	while(1){
		S32 msgFound = 0;
		
		suiMainWaitForEventsAndMsgs(mw,
									msTimeOut,
									QS_ALLEVENTS |
										QS_ALLINPUT |
										QS_ALLPOSTMESSAGE);
														
		if(	mw->flags.checkForMsgs ||
			mw->flags.waitForCharMsg)
		{
			PERFINFO_AUTO_START("PeekMessage", 1);
				msgFound = PeekMessage(&msg, mw->hwnd, 0, 0, PM_REMOVE);
			PERFINFO_AUTO_STOP();

			if(!msgFound){
				mw->flags.checkForMsgs = 0;
			}else{
				PERFINFO_AUTO_START("msgFound", 1);
					mw->lastMsgTime = msg.time;
					mw->flags.checkForMsgs = 1;

					PERFINFO_AUTO_START("TranslateMessage", 1);
						if(TranslateMessage(&msg)){
							MSG msgChar;

							if(!PeekMessage(&msgChar, mw->hwnd, 0, 0, PM_NOREMOVE)){
								mw->flags.checkForMsgs = 0;
							}else{
								mw->flags.checkForMsgs = 1;

								if(	msgChar.message == WM_CHAR ||
									msgChar.message == WM_SYSCHAR)
								{
									mw->flags.waitForCharMsg = 1;
								}
							}
						}
					PERFINFO_AUTO_STOP_START("DispatchMessage", 1);
						DispatchMessage(&msg);
					PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
			}
		}
		
		if(!msgFound){
			mw->lastMsgTime = max(mw->lastMsgTime, timeGetTime());
			assert(!mw->flags.waitForCharMsg);
			break;
		}
		
		if(	timeGetTime() - msStartTime >= 10 &&
			!mw->flags.waitForCharMsg)
		{
			break;
		}
		
		msTimeOut = 0;
	}

	suiMainCheckProcessAndDrawTime(mw);
}

S32 suiMainWindowProcess(SUIMainWindow* mw){
	if(!mw){
		return 0;
	}

	if(	mw->hwnd &&
		!mw->flags.destroyMe)
	{
		if(!mw->flags.hasProcessed){
			suiMainWindowDoFirstProcess(mw);
		}

		PERFINFO_AUTO_START("suiMainWindowProcessMsgs", 1);

			suiMainWindowProcessMsgs(mw);

		PERFINFO_AUTO_STOP();
	}

	if(mw->flags.destroyMe){
		suiMainWindowDestroy(&mw);
		return 0;
	}

	return 1;
}

void suiMainWindowProcessUntilDone(SUIMainWindow* mw){
	while(1){
		S32 done;

		PERFINFO_AUTO_START("suiMainWindowProcess", 1);

			done = !suiMainWindowProcess(mw);

		PERFINFO_AUTO_STOP();

		if(done){
			break;
		}
	}
}

S32 suiMainWindowAddChild(	SUIMainWindow* mw,
							SUIWindow* wChild)
{
	if(!mw){
		return 0;
	}

	return suiRootWindowAddChild(	mw->suiRootWindow,
									wChild);
}

S32 suiMainWindowAddEvent(	SUIMainWindow* mw,
							void* hEvent,
							void* userPointer,
							SUIMainWindowEventCallback callback)
{
	SUIMainWindowEvent* e;
	
	if(	!mw ||
		!hEvent ||
		!userPointer ||
		!callback ||
		eaFind(&mw->events.hEvents, hEvent) >= 0)
	{
		return 0;
	}
	
	e = callocStruct(SUIMainWindowEvent);
	e->hEvent = hEvent;
	e->userPointer = userPointer;
	e->callback = callback;
	
	eaPush(&mw->events.events, e);
	eaPush(&mw->events.hEvents, hEvent);
	
	return 1;
}

S32 suiMainWindowMinimize(SUIMainWindow* mw){
	if(!mw){
		return 0;
	}
	
	if(!mw->flags.hasProcessed){
		mw->flags.minimizeOnShow = 1;
	}
	else if(IsWindowVisible(mw->hwnd) &&
			!IsIconic(mw->hwnd))
	{
		ShowWindow(mw->hwnd, SW_MINIMIZE);
	}
	
	return 1;
}

S32 suiMainWindowSetName(	SUIMainWindow* mw,
							const char* name)
{
	if(!mw){
		return 0;
	}
	
	SAFE_FREE(mw->name);
	mw->name = strdup(name);
	
	return 1;
}
