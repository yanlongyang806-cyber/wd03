#include "HTMLViewer.h"

#include "GfxSprite.h"
#include "GlobalTypes.h"
#include "StringUtil.h"
#include "inputData.h"
#include "inputLib.h"
#include "inputText.h"
#include "MemRef.h"
#include "stdtypes.h"
#include "EString.h"
#include "file.h"
#include "sysutil.h"
#include "MemTrack.h"
#include "UTF8.h"
#include "Organization.h"

#define ENABLE_LIBCEF		1

#if ENABLE_LIBCEF
#include "include/cryptic.h"
#include "include/internal/cef_types.h"
#include "include/capi/cef_app_capi.h"
#include "include/capi/cef_browser_capi.h"
#include "include/capi/cef_client_capi.h"
#include "include/capi/cef_frame_capi.h"
#include "include/capi/cef_nplugin_capi.h"
#include "include/capi/cef_life_span_handler_capi.h"
#include "include/capi/cef_render_handler_capi.h"
#include "include/capi/cef_load_handler_capi.h"
#include "include/capi/cef_request_handler_capi.h"
#include "include/capi/cef_request_capi.h"
#include "include/capi/cef_base_capi.h"
#include "include/capi/cef_resource_bundle_handler_capi.h"
#include "include/capi/cef_proxy_handler_capi.h"
#include "include/capi/cef_web_urlrequest_capi.h"
#include "include/internal/cef_export.h"
#include "include/internal/cef_string.h"
#include "include/internal/cef_types_win.h"

#pragma comment(lib, "../../3rdparty/cef/lib/libcef.lib")

typedef struct CEFClient CEFClient;
typedef struct CEFLifespan CEFLifespan;
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef struct HTMLViewer
{
#if ENABLE_LIBCEF
	CEFClient *client;
	cef_browser_t *view;
#endif

	U8* crypt_buffer;
	int view_width;
	int view_height;

	BasicTexture *tex;
} HTMLViewer;

typedef struct HTMLViewState
{
#if ENABLE_LIBCEF
	cef_plugin_info_t *osr_plugin;

	CEFClient **clients;
#else
	int unused;
#endif
} HTMLViewState;

HTMLViewer *debug_viewer;
HTMLViewState g_HTMLState;

S32 hv_SendToScreen(HTMLViewer *viewer);

#if ENABLE_LIBCEF

void hv_CEFPostInit(void)
{

}

typedef struct CEFRender 
{
	cef_render_handler_t _base;

	CEFClient *client;
} CEFRender;

typedef struct CEFLifespan
{
	cef_life_span_handler_t _base;

	CEFClient *client;
};

typedef struct CEFLoad
{
	cef_load_handler_t _base;

	CEFClient *client;
} CEFLoad;

typedef struct CEFRequest
{
	cef_request_handler_t _base;

	CEFClient *client;
} CEFRequest;

typedef struct CEFURLRequestClient
{
	cef_web_urlrequest_client_t _base;

	CEFClient *client;
} CEFURLRequestClient;

typedef struct CEFClient
{
	cef_client_t _base;
	CEFRender _render;
	CEFLifespan _lifespan;
	CEFLoad _load;
	CEFRequest _request;

	HTMLViewer *view;
	char *main_url;
	int loading;
	cef_rect_t popup_rect;

	int ref_count;

	U32 dirty : 1;
} CEFClient;

cef_render_handler_t* CEF_CALLBACK cefGetRenderHandler(cef_client_t *self)
{
	CEFClient *c = (CEFClient*)self;
	return &c->_render._base;
}

// srcOffsetX/Y should be the difference between the U/L coordinates between srcAndDst
// srcCoordX = dstCoordX + srcOffsetX
// srcCoordY = dstCoordY + srcOffsetY
static void pixelBlt(
	CEFClient *c,
	const void *srcBuf,
	int srcStride,
	const cef_rect_t *dstRect,
	int srcOffsetX,
	int srcOffsetY)
{
	int x, y;
	unsigned long *dst = (unsigned long *)c->view->crypt_buffer;
	unsigned long *src = (unsigned long *)srcBuf;
	int startX = dstRect->x;
	int startY = dstRect->y;
	// endX/Y are non-inclusive
	int endX = dstRect->x + dstRect->width;
	int endY = dstRect->y + dstRect->height;

	for (y = startY; y < endY; y++)
	{
		int dstIndex = y * c->view->tex->realWidth;
		int srcIndex = (y + srcOffsetY) * srcStride;
		for (x = startX; x < endX; x++)
		{
			// NOTE: this code sets alpha value to 0xFF
			// There are cases where libcef presents us pixels that don't have alpha at 0xFF
			// This causes problems when we go to render the 'sprite' to the black background.
			// TODO: figure out if we can make the sprite system handle opaque renders, 
			// and convert this entire for (x...) loop into a memcpy.
			dst[dstIndex + x] = src[srcIndex + srcOffsetX + x] | 0xFF000000;
		}
	}
}

// starting code for this was found in ClientOSRenderer::OnPaint() here:
// https://code.google.com/p/chromiumembedded/source/browse/trunk/cef1/tests/cefclient/osrenderer.cpp?r=745
void CEF_CALLBACK cefOnPaint(
	cef_render_handler_t* self,
	cef_browser_t* browser, 
	enum cef_paint_element_type_t type,
	size_t dirtyRectsCount, 
	cef_rect_t const* dirtyRects,
	const void* buffer)
{
	CEFRender *cefr = (CEFRender*)self;
	CEFClient *c = cefr->client;

	if (type == PET_VIEW)
	{
		int old_width = c->view->view_width;
		int old_height = c->view->view_height;

		// retrieve the current size of the browser view
		browser->get_size(browser, type, &(c->view->view_width), &(c->view->view_height));

		if ((old_width != c->view->view_width) ||
			(old_height != c->view->view_height))
		{
			cef_rect_t dstRect;
			dstRect.x = 0;
			dstRect.y = 0;
			dstRect.width = c->view->view_width;
			dstRect.height = c->view->view_height;

			// update the whole texture
			pixelBlt(c, buffer, c->view->tex->realWidth, &dstRect, 0, 0);
		}
		else
		{
			int i;

			// update just the dirtyRects
			for (i = 0; i < (int)dirtyRectsCount; i++)
			{
				cef_rect_t dstRect;
				const cef_rect_t *r = &dirtyRects[i];

				dstRect.x = r->x;
				dstRect.y = r->y;
				dstRect.width = MIN(c->view->view_width, r->width);
				dstRect.height = MIN(c->view->view_height, r->height);

				pixelBlt(c, buffer, c->view->tex->realWidth, &dstRect, 0, 0);
			}
		}
	}
	else if (type == PET_POPUP && c->popup_rect.width > 0 && c->popup_rect.height > 0)
	{
		// we ignore the dirtyRects passed in, and instead use the popup_rect set in 
		// cefOnPopupShow() and cefOnPopupSize()

		cef_rect_t dstRect = c->popup_rect;
		int srcOffsetX = -dstRect.x;
		int srcOffsetY = -dstRect.y;
		cef_rect_t srcRect = {0};
		srcRect.width = dstRect.width;
		srcRect.height = dstRect.height;

		// clip rect to within screen bounds
		if (dstRect.x < 0)
		{
			dstRect.width += dstRect.x; // this subtracts
			dstRect.x = 0;
		}

		if (dstRect.y < 0)
		{
			dstRect.height += dstRect.y; // this subtracts
			dstRect.y = 0;
		}

		if (dstRect.x + dstRect.width > c->view->view_width)
		{
			dstRect.width -= dstRect.x + dstRect.width - c->view->view_width;
		}

		if (dstRect.y + dstRect.height > c->view->view_height)
		{
			dstRect.height -= dstRect.y + dstRect.height - c->view->view_height;
		}

		pixelBlt(c, buffer, c->popup_rect.width, &dstRect, srcOffsetX, srcOffsetY);
	}

	c->dirty = true;

	browser->base.release(&browser->base);
}

// Called to retrieve the view rectangle which is relative to screen
// coordinates. Return true (1) if the rectangle was provided.
int CEF_CALLBACK cefGetViewRect(cef_render_handler_t* self,
	cef_browser_t* browser, 
	cef_rect_t* rect)
{
	CEFRender *r = (CEFRender*)self;
	CEFClient *c = r->client;

	rect->x = 0;
	rect->y = 0;
	rect->width = c->view->view_width;
	rect->height = c->view->view_height;

	browser->base.release(&browser->base);

	return true;
}

// Called to retrieve the simulated screen rectangle. Return true (1) if the
// rectangle was provided.
int CEF_CALLBACK cefGetScreenRect(cef_render_handler_t* self,
	cef_browser_t* browser, 
	cef_rect_t* rect)
{
	CEFRender *r = (CEFRender*)self;
	CEFClient *c = r->client;

	rect->x = 0;
	rect->y = 0;
	rect->width = c->view->view_width;
	rect->height = c->view->view_height;

	browser->base.release(&browser->base);

	return true;
}

// Called when the browser wants to show or hide the popup widget. The popup
// should be shown if |show| is true (1) and hidden if |show| is false (0).
void CEF_CALLBACK cefOnPopupShow(cef_render_handler_t* self, cef_browser_t* browser, int show)
{
	if (!show)
	{
		CEFRender *r = (CEFRender*)self;
		CEFClient *c = r->client;

		browser->invalidate(browser, &(c->popup_rect));

		c->popup_rect.x =
		c->popup_rect.y =
		c->popup_rect.width =
		c->popup_rect.height = 0;
	}

	browser->base.release(&browser->base);
}

// Called when the browser wants to move or resize the popup widget. |rect|
// contains the new location and size.
void CEF_CALLBACK cefOnPopupSize(cef_render_handler_t* self, cef_browser_t* browser, const cef_rect_t* rect)
{
	if (rect->width > 0 && rect->height > 0)
	{
		CEFRender *r = (CEFRender*)self;
		CEFClient *c = r->client;
		c->popup_rect.x = rect->x;
		c->popup_rect.y = rect->y;
		c->popup_rect.width = rect->width;
		c->popup_rect.height = rect->height;
	}

	browser->base.release(&browser->base);
}

cef_life_span_handler_t* CEF_CALLBACK cefGetLifespanHandler(cef_client_t *self)
{
	CEFClient *c = (CEFClient*)self;
	return &c->_lifespan._base;
}

void CEF_CALLBACK cefOnAfterCreated(
	cef_life_span_handler_t* self,
	cef_browser_t* browser)
{
	CEFLifespan *l = (CEFLifespan*)self;
	CEFClient *c = l->client;

	browser->set_size(browser, PET_VIEW, c->view->view_width, c->view->view_height);
	c->view->view = browser;
}

int CEF_CALLBACK cefLifespanDoClose(cef_life_span_handler_t* self, cef_browser_t* browser)
{
	browser->base.release(&browser->base);

	return false;
}

void CEF_CALLBACK cefLifespanOnBeforeClose(cef_life_span_handler_t* self, cef_browser_t* browser)
{
	CEFLifespan *l = (CEFLifespan *)self;
	CEFClient *c = l->client;

	// IMPORTANT
	//
	// From https://code.google.com/p/chromiumembedded/wiki/UsingTheCAPI
	//
	// "Reverse any additional references that your code adds to a struct (for instance, if you keep a reference to
	// the cef_browser_t pointer in your handler implementation). The last opportunity to release references is in
	// cef_handler_t::handle_before_window_closed()."
	c->view->view->base.release(&c->view->view->base);

	browser->base.release(&browser->base);
}

cef_load_handler_t* CEF_CALLBACK cefGetLoadHandler(cef_client_t* self)
{
	CEFClient *c = (CEFClient*)self;
	return &c->_load._base;
}

cef_request_handler_t* CEF_CALLBACK cefGetRequestHandler(cef_client_t* self)
{
	CEFClient *c = (CEFClient*)self;
	return &c->_request._base;
}

void CEF_CALLBACK cefLoadOnLoadStart(cef_load_handler_t* self, cef_browser_t* browser, cef_frame_t* frame)
{
	CEFLoad *l = (CEFLoad*)self;
	CEFClient *c = l->client;

	if (frame->is_main(frame))
	{
		cef_string_utf8_t url_utf8 = {0};
		cef_string_userfree_t url_str = frame->get_url(frame);

		cef_string_to_utf8(url_str->str, url_str->length, &url_utf8);
		cef_string_userfree_free(url_str);

		SAFE_FREE(c->main_url);
		c->main_url = malloc(url_utf8.length + 1);
		strcpy_s(c->main_url, url_utf8.length + 1, url_utf8.str);

		cef_string_utf8_clear(&url_utf8);
	}

	++c->loading;

	frame->base.release(&frame->base);
	browser->base.release(&browser->base);
}

void CEF_CALLBACK cefLoadOnLoadEnd(cef_load_handler_t* self, cef_browser_t* browser, cef_frame_t* frame, int httpStatusCode)
{
	CEFLoad *l = (CEFLoad*)self;
	CEFClient *c = l->client;

	--c->loading;

	if (c->loading < 0)
	{
		devassert(0);
		c->loading = 0;
	}

	frame->base.release(&frame->base);
	browser->base.release(&browser->base);
}

// This function can be used to replace load error messages (default is "error -###.  blah blah")
int CEF_CALLBACK cefLoadOnLoadError(cef_load_handler_t* self, cef_browser_t* browser, cef_frame_t* frame, enum cef_handler_errorcode_t errorCode, const cef_string_t* failedUrl, cef_string_t* errorText)
{
	switch(errorCode)
	{
		xcase ERR_CERT_COMMON_NAME_INVALID: {
			break;
		}
		xcase ERR_CERT_DATE_INVALID: {
			break;
		}
		xcase ERR_CERT_AUTHORITY_INVALID: {
			break;
		}
		xcase ERR_CERT_CONTAINS_ERRORS: {
			break;
		}
		xcase ERR_CERT_NO_REVOCATION_MECHANISM: {
			break;
		}
		xcase ERR_CERT_UNABLE_TO_CHECK_REVOCATION: {
			break;
		}
		xcase ERR_CERT_REVOKED: {
			break;
		}
		xcase ERR_CERT_INVALID: {
			break;
		}
		xcase ERR_CERT_END: {
			break;
		}
		xcase ERR_SSL_CLIENT_AUTH_CERT_NEEDED: {
			break;
		}
		xcase ERR_SSL_PROTOCOL_ERROR: {
			break;
		}
		xcase ERR_NO_SSL_VERSIONS_ENABLED: {
			break;
		}
	}

	frame->base.release(&frame->base);
	browser->base.release(&browser->base);

	return false;
}

// Called on the UI thread before browser navigation. Return true (1) to
// cancel the navigation or false (0) to allow the navigation to proceed.
int CEF_CALLBACK cefRequestOnBeforeBrowse(
	cef_request_handler_t* self,
	cef_browser_t* browser,
	cef_frame_t* frame,
	cef_request_t* request,
	enum cef_handler_navtype_t navType,
	int isRedirect)
{
#define URL_SEPS "/:"

	int retVal = 1;
	cef_string_utf8_t url_utf8 = {0};
	cef_string_userfree_t url_str = request->get_url(request);

	char *domain = NULL;
	char *next_token = NULL;
	char *token = NULL;

	cef_string_to_utf8(url_str->str, url_str->length, &url_utf8);
	cef_string_userfree_free(url_str);

	domain = url_utf8.str;
	next_token = NULL;
	token = strtok_s(url_utf8.str, URL_SEPS, &next_token);
//		if (stricmp_safe(token, "https") == 0) 
//		{
		token = strtok_s(NULL, URL_SEPS, &next_token);
//			if ((stricmp_safe(token, ORGANIZATION_DOMAIN) == 0) ||
//				(stricmp_safe(token, ORGANIZATION_DOMAIN) == 0))
//			{
			domain = token;
			retVal = 0; // allow navigation to proceed
//			}
//			else
//			{
//				printf("HTMLViewer: blocking %s\n", token);
//			}
//		}

/*
	printf("HTMLViewer: ");
	if (retVal == 0)
	{
		printf("allowing");
	}
	else
	{
		printf("blocking");
	}
	printf(" %s navType: ", domain);
	switch (navType)
	{
		case NAVTYPE_LINKCLICKED:
			printf("link click");
			break;
		case NAVTYPE_FORMSUBMITTED:
			printf("form submit");
			break;
		case NAVTYPE_BACKFORWARD:
			printf("back fwd");
			break;
		case NAVTYPE_RELOAD:
			printf("realod");
			break;
		case NAVTYPE_FORMRESUBMITTED:
			printf("form resubmit");
			break;
		case NAVTYPE_OTHER:
			printf("other");
			break;
		case NAVTYPE_LINKDROPPED:
			printf("link dropped");
			break;
	}
	if (isRedirect)
	{
		printf(" [redirect]");
	}
	printf("\n");
*/
	cef_string_utf8_clear(&url_utf8);

	request->base.release(&request->base);
	frame->base.release(&frame->base);
	browser->base.release(&browser->base);

	return retVal;
}

int CEF_CALLBACK cefClientAddRef(cef_base_t* self)
{
	CEFClient *c = (CEFClient *)self;

	InterlockedIncrement(&c->ref_count);

	return c->ref_count;
}

int CEF_CALLBACK cefClientGetRefct(cef_base_t* self)
{
	CEFClient *c = (CEFClient *)self;

	return c->ref_count;
}

int CEF_CALLBACK cefClientRelease(cef_base_t* self)
{
	CEFClient *c = (CEFClient *)self;

	InterlockedDecrement(&c->ref_count);

	if(0 == c->ref_count)
	{
		eaFindAndRemove(&g_HTMLState.clients, c);

		free(c);

		return 0;
	}

	return c->ref_count;
}

cef_client_t* cefCreateClient(HTMLViewer *view)
{
	CEFClient *client = calloc(1, sizeof(CEFClient));

	// client init
	client->view = view;
	client->ref_count = 1;

	// cef_base_t init
	client->_base.base.size = sizeof(CEFClient);
	client->_base.base.add_ref = cefClientAddRef;
	client->_base.base.get_refct = cefClientGetRefct;
	client->_base.base.release = cefClientRelease;

	// cef_client_t init
	client->_base.get_render_handler = cefGetRenderHandler;
	client->_base.get_life_span_handler = cefGetLifespanHandler;
	client->_base.get_load_handler = cefGetLoadHandler;
	client->_base.get_request_handler = cefGetRequestHandler;

	// cef_lifespan_handler_t init
	client->_lifespan._base.base.size = sizeof(CEFLifespan);
	client->_lifespan._base.on_after_created = cefOnAfterCreated;
	client->_lifespan._base.do_close = cefLifespanDoClose;
	client->_lifespan._base.on_before_close = cefLifespanOnBeforeClose;
	client->_lifespan.client = client;

	// cef_render_handler_t init
	client->_render._base.base.size = sizeof(CEFRender);
	client->_render._base.on_paint = cefOnPaint;
	client->_render._base.get_screen_rect = cefGetScreenRect;
	client->_render._base.get_view_rect = cefGetViewRect;
	client->_render._base.on_popup_show = cefOnPopupShow;
	client->_render._base.on_popup_size = cefOnPopupSize;
	client->_render.client = client;

	// cef_load_handler_t init
	client->_load._base.base.size = sizeof(CEFLoad);
	client->_load._base.on_load_start = cefLoadOnLoadStart;
	client->_load._base.on_load_end = cefLoadOnLoadEnd;
	client->_load._base.on_load_error = cefLoadOnLoadError;
	client->_load.client = client;

	// cef_request_handler_t init
	client->_request._base.base.size = sizeof(CEFRequest);
	client->_request._base.on_before_browse = cefRequestOnBeforeBrowse;
	client->_request.client = client;

	view->client = client;

	eaPush(&g_HTMLState.clients, client);

	return (cef_client_t*)client;
}

#define CRYPTIC_FILESYSTEM_OVERRIDE "cryptic_override"
#define CRYPTIC_CEF_OVERRIDE_VERSION 3

static cef_cryptic_override_func_ptr_t cef_cryptic_override_func_ptr;
static char sLocalesLocation[] = CRYPTIC_FILESYSTEM_OVERRIDE;

static void *cef_chrome_pak;  // Chrome pak data
int cef_chrome_pak_length;
static void *cef_locale_pak;  // Locale pak data
int cef_locale_pak_length;

// Load files for CEF.
static unsigned char *cef_file_mapper(const wchar_t *filename, size_t *length)
{
	if (wcsstr(filename, L"\\locales\\en-US.pak"))
	{
		if (length)
			*length = cef_locale_pak_length;
		return cef_locale_pak;
	}
	return 0;
}

static void* htmlViewerMallocCB(size_t size)
{
	return malloc(size);
}

static void htmlViewerFreeCB(void* ptr)
{
	free(ptr);
}

//extern void crashNow( void ); // function defined in C:\srcTip\libs\UtilitiesLib\utils\SuperAssert.c

static void* htmlViewerReallocCB(void* ptr, size_t size)
{
//	crashNow();
	return realloc(ptr, size);
}

static size_t htmlViewerMsizeCB(void* ptr)
{
	return GetAllocSize(ptr);
}

#endif

AUTO_STARTUP(HTMLViewer);
void hv_Init(void)
{
	if(!gConf.bHTMLViewerEnabled)
		return;

#if !ENABLE_LIBCEF
	gConf.bHTMLViewerEnabled = false;
#endif

#if ENABLE_LIBCEF
	{
		cef_settings_t settings = {0};
//		static cef_cryptic_override_t override;
		HANDLE dll = NULL;
		char libcefDLL[MAX_PATH];

		loadstart_printf("Loading ChromiumEmbeddedFramework...");

		// Load CEF DLL
		getExecutableDir(libcefDLL);
		strcat(libcefDLL, "/libcef.dll");
		dll = LoadLibrary_UTF8(libcefDLL);
		if(!dll)
		{
			gConf.bHTMLViewerEnabled = false;
			loadend_printf("DLL not found");
			return;
		}

		// Attempt to load required CEF data files.
		// The Cryptic absolute paths are for backward-compatibility.
		if (!(cef_locale_pak = fileAlloc("browser/locales/en-US.pak", &cef_locale_pak_length)))
		{
			FreeLibrary(dll);
			gConf.bHTMLViewerEnabled = false;
			loadend_printf("Locale file not found");
			return;
		}

		// Set data file paths, which will be recognized by cef_file_mapper().
		cef_string_from_utf8(sLocalesLocation, sizeof(sLocalesLocation) - 1, &settings.locales_dir_path);
		cef_initialize(&settings, NULL);

		hv_CEFPostInit();

		loadend_printf("done.");
	}
#endif
}

__declspec(dllexport) void cef_cryptic_override(cef_cryptic_override_t *override)
{
	// Set up mapper.
	override->cef_cryptic_version = CRYPTIC_CEF_OVERRIDE_VERSION;
	override->cef_cryptic_file_mapper = cef_file_mapper;
	override->cef_cryptic_malloc_override = htmlViewerMallocCB;
	override->cef_cryptic_realloc_override = htmlViewerReallocCB;
	override->cef_cryptic_free_override = htmlViewerFreeCB;
	override->cef_cryptic_msize_override = htmlViewerMsizeCB;
}

void hv_Shutdown(void)
{
#if ENABLE_LIBCEF
	if(!gConf.bHTMLViewerEnabled)
		return;
	cef_shutdown();
#endif
}

void hv_Update(void)
{
	if(!gConf.bHTMLViewerEnabled)
		return;

#if ENABLE_LIBCEF
	FP_NO_EXCEPTIONS_BEGIN;
	cef_do_message_loop_work();
	FP_NO_EXCEPTIONS_END;
#endif
}

S32 hv_CreateViewer(HTMLViewer **viewerOut, F32 w, F32 h, const char* url)
{
	HTMLViewer *viewer = NULL;
	if(!gConf.bHTMLViewerEnabled)
		return false;
	if(!viewerOut || *viewerOut)
		return false;

#if ENABLE_LIBCEF
	viewer = callocStruct(HTMLViewer);
	viewer->view_width = w;
	viewer->view_height = h;
	// Create the off-screen rendering window.
	{
		cef_window_info_t info = {0};
		cef_browser_settings_t settings = {0};
		cef_client_t *client = cefCreateClient(viewer);
		cef_string_t url_str = {0};
		info.m_bWindowRenderingDisabled = true;
		info.m_bTransparentPainting = true;

		if(!url)
			url = "https://www.google.com";

		cef_string_from_utf8(url, strlen(url), &url_str);

		cef_browser_create(&info, client, &url_str, &settings);

		cef_string_clear(&url_str);
	}

	viewer->tex = texGenNew(viewer->view_width, viewer->view_height, "HTML View", TEXGEN_NORMAL, WL_FOR_UI);

	viewer->crypt_buffer = memrefAlloc(viewer->tex->realHeight * viewer->tex->realWidth * 4);

	//viewer->tex->bt_texopt_flags |= TEXOPT_CLAMPS | TEXOPT_CLAMPT | TEXOPT_MAGFILTER_POINT;

	*viewerOut = viewer;
	return true;
#else
	return false;
#endif
}

S32 hv_Destroy(HTMLViewer **viewerInOut)
{
	return false;
}

S32 hv_Resize(HTMLViewer *viewer, S32 w, S32 h)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

	if(viewer->tex->realWidth < w || viewer->tex->realHeight < h)
	{
		texGenFree(viewer->tex);
		viewer->tex = texGenNew(viewer->tex->realWidth * 2, viewer->tex->realHeight * 2, "HTML View", TEXGEN_NORMAL, WL_FOR_UI);

		memrefDecrement(viewer->crypt_buffer);
		viewer->crypt_buffer = memrefAlloc(viewer->tex->realHeight * viewer->tex->realWidth * 4);
	}

	viewer->tex->width = w;
	viewer->tex->height = h;
	viewer->view_width = w;
	viewer->view_height = h;

#if ENABLE_LIBCEF
	if(!viewer->view)
		return false;

	viewer->view->set_size(viewer->view, PET_VIEW, w, h);
	return true;
#else
	return false;
#endif
}

S32 hv_LoadRemote(HTMLViewer *viewer, const char* page)
{
#if ENABLE_LIBCEF
	if (gConf.bHTMLViewerEnabled && viewer && viewer->view)
	{
		cef_frame_t* frame = viewer->view->get_focused_frame(viewer->view);
		cef_string_t url_str = {0};

		cef_string_from_utf8(page, strlen(page), &url_str);
		frame->load_url(frame, &url_str);
		cef_string_clear(&url_str);

		return true;
	}
#endif
	return false;
}

S32 hv_LoadLocal(HTMLViewer *viewer, const char* page)
{
#if ENABLE_LIBCEF
	if (gConf.bHTMLViewerEnabled && viewer && viewer->view)
	{
		cef_frame_t* frame = viewer->view->get_focused_frame(viewer->view);
		cef_string_t url_str = {0};

		cef_string_from_utf8(page, strlen(page), &url_str);
		frame->load_url(frame, &url_str);
		cef_string_clear(&url_str);

		return true;
	}
#endif
	return false;
}

S32 hv_GetMainURL(HTMLViewer *viewer, const char **page)
{
#if ENABLE_LIBCEF
	if(!gConf.bHTMLViewerEnabled || !viewer || !viewer->client)
		return false;

	*page = viewer->client->main_url;
	return true;
#else
	return false;
#endif
}

S32 hv_IsLoading(HTMLViewer *viewer)
{
#if ENABLE_LIBCEF
	if(!gConf.bHTMLViewerEnabled || !viewer || !viewer->client)
		return false;

	return viewer->client->loading > 0;
#else
	return false;
#endif
}

S32 hv_IsDirty(HTMLViewer *viewer, S32 *dirtyOut)
{
	*dirtyOut = false;
	if(!gConf.bHTMLViewerEnabled)
		return false;
#if ENABLE_LIBCEF
	*dirtyOut = viewer->client->dirty;
	return true;
#else
	return false;
#endif
}

S32 hv_Render(HTMLViewer *viewer, S32 force)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

	if(!viewer)
		return false;

#if ENABLE_LIBCEF
	if(!viewer->view)
		return false;

	if(!force && !viewer->client->dirty)
		return true;

	// crypt_buffer is updated in render_handler's cefOnPaint
	texGenUpdate(viewer->tex, viewer->crypt_buffer, RTEX_2D, RTEX_BGRA_U8, 1, true, false, false, true);

	return true;
#else
	return false;
#endif
}

S32 hv_SendToScreen(HTMLViewer *viewer)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

	display_sprite_tex(viewer->tex, 200, 200, 1, 300.0/1024, 300.0/1024, 0xffffffff);

	return true;
}

S32 hv_GetTexture(HTMLViewer *viewer, BasicTexture **texOut)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

	if(!viewer || !viewer->tex)
		return false;

	*texOut = viewer->tex;
	return true;
}

S32 hv_GetDimensions(HTMLViewer *viewer, Vec2 dimOut)
{
	if (gConf.bHTMLViewerEnabled)
	{
		dimOut[0] = viewer->view_width;
		dimOut[1] = viewer->view_height;

		return true;
	}
	else
	{
		return false;
	}
}

S32 hv_Focus(HTMLViewer *viewer)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

#if ENABLE_LIBCEF
	viewer->view->send_focus_event(viewer->view, true);
#endif

	return true;
}

S32 hv_Unfocus(HTMLViewer *viewer)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;
#if ENABLE_LIBCEF
	viewer->view->send_focus_event(viewer->view, false);
#endif

	return true;
}

S32 hv_InjectMousePos(HTMLViewer *viewer, S32 x, S32 y)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;
#if ENABLE_LIBCEF
	if(!viewer->view)
		return false;

	viewer->view->send_focus_event(viewer->view, true);
	viewer->view->send_mouse_move_event(viewer->view, x, y, false);
#endif

	return true;
}

S32 hv_InjectMouseDown(HTMLViewer *viewer, S32 x, S32 y, InpKeyCode code)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

#if ENABLE_LIBCEF
	if(!viewer->view)
		return false;

	viewer->view->send_focus_event(viewer->view, true);

	switch(code)
	{
		xcase INP_LBUTTON:
	viewer->view->send_mouse_click_event(viewer->view, x, y, MBT_LEFT, false, 1);
	xcase INP_MBUTTON:
	viewer->view->send_mouse_click_event(viewer->view, x, y, MBT_MIDDLE, false, 1);
	xcase INP_RBUTTON:
	viewer->view->send_mouse_click_event(viewer->view, x, y, MBT_RIGHT, false, 1);
	xcase INP_MOUSEWHEEL_BACKWARD:
	viewer->view->send_mouse_wheel_event(viewer->view, x, y, 0, -1);
	xcase INP_MOUSEWHEEL_FORWARD:
	viewer->view->send_mouse_wheel_event(viewer->view, x, y, 0, 1);
	}
#endif

	return true;
}

S32 hv_InjectMouseUp(HTMLViewer *viewer, S32 x, S32 y, InpKeyCode code)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

#if ENABLE_LIBCEF
	if(!viewer->view)
		return false;

	viewer->view->set_focus(viewer->view, true);

	switch(code)
	{
		xcase INP_LBUTTON:
	viewer->view->send_mouse_click_event(viewer->view, x, y, MBT_LEFT, true, 1);
	xcase INP_MBUTTON:
	viewer->view->send_mouse_click_event(viewer->view, x, y, MBT_MIDDLE, true, 1);
	xcase INP_RBUTTON:
	viewer->view->send_mouse_click_event(viewer->view, x, y, MBT_RIGHT, true, 1);
	}
#endif

	return true;
}

S32 hv_InjectKey(HTMLViewer *viewer, KeyInput *key)
{
	if(!gConf.bHTMLViewerEnabled)
		return false;

#if ENABLE_LIBCEF
	if(!viewer->view)
		return false;

	{
		if(key->type == KIT_Text && key->character)
		{
			//hv_FillKeyMods(&evt, key);
			cef_key_info_t cefkey;
			cefkey.imeChar = false;
			cefkey.sysChar = false;
			cefkey.key = key->vkey;

			viewer->view->send_key_event(viewer->view, KT_CHAR, &cefkey, 0);
		}
		else if(!key->character)
		{
			//hv_FillKeyMods(&evt, key);
			cef_key_info_t cefkey;
			cefkey.imeChar = false;
			cefkey.sysChar = false;
			cefkey.key = key->vkey;

			viewer->view->send_key_event(viewer->view, KT_KEYDOWN, &cefkey, 0);
			viewer->view->send_key_event(viewer->view, KT_KEYUP, &cefkey, 0);
		}
	}
#endif

	return true;
}

AUTO_COMMAND ACMD_NAME(hvCreate);
void cmd_hv_Create(void)
{
	hv_CreateViewer(&debug_viewer, 500, 500, "https://www.google.com");
}

AUTO_COMMAND ACMD_NAME(hvOpenURL);
void cmd_hv_LoadRemote(const char* page)
{
	hv_LoadRemote(debug_viewer, page);
}

// This is backwards from what I want (hvEnable),
// but at the moment, it's safer to leave the command as 'hvDisable' - don't want to break STO or NW this close to ship
AUTO_COMMAND ACMD_NAME(hvDisable) ACMD_COMMANDLINE;
void cmd_hv_Disable(bool bDisable)
{
	gConf.bHTMLViewerEnabled = !bDisable;
}
