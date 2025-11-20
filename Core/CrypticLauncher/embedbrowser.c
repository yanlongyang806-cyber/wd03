/*
* Dll.c -- A dynamic link library to display a web page in your own window.
*
* This is very loosely based upon a C++ example written by Chris Becke. I used
* that to learn the minimum of what I needed to know about hosting the browser
* object. Then I wrote this example from the ground up in C.
*
* The functions in this DLL callable by a program:
*
* EmbedBrowserObject() -- Embeds a browser object in your own window.
* UnEmbedBrowserObject() -- Detaches the browser object from your window.
* DisplayHTMLPage() -- Displays a URL or HTML file on disk.
* DisplayHTMLStr() -- Displays a (in memory) string of HTML code.
* DoPageAction() -- Moves forward/backward a page, brings up Home or Search page, etc.
* WaitOnReadyState() -- Waits for a page to be in a certain state.
* GetWebPtrs() -- Obtains a IWebBrowser2 and/or IHTMLDocument2 pointer.
* GetWebElement() -- Gets the IHTMLElement object of some element on a web page.
* ResizeBrowser() -- Resizes the IE window to be the same width/height as the app's container window.
*
* For the release (ie, not debug) version, then you should set your linker to
* ignore the default libraries. This will reduce code size.
*/

#include "embedbrowser.h"
#include "SimpleWindowManager.h" // For SimpleWindow struct
#include "CrypticLauncher.h"
#include "earray.h"
#include "EString.h"
#include "error.h"
#include "utils.h"
#include "trivia.h"
#include <windows.h>
#include <exdisp.h>		/* Defines of stuff like IWebBrowser2. This is an include file with Visual C 6 and above */
#include <mshtml.h>		/* Defines of stuff like IHTMLDocument2. This is an include file with Visual C 6 and above */
#include <mshtmhst.h>	/* Defines of stuff like IDocHostUIHandler. This is an include file with Visual C 6 and above */
#include <ExDispid.h>   // Dispatch event IDs for IE
#include <tchar.h>
#ifndef NDEBUG
#include <assert.h>
#endif



/*
#define DOCHOSTUIFLAG_DIALOG				0x00000001, 
#define DOCHOSTUIFLAG_DISABLE_HELP_MENU		0x00000002, 
#define DOCHOSTUIFLAG_NO3DBORDER			0x00000004, 
#define DOCHOSTUIFLAG_SCROLL_NO				0x00000008, 
#define DOCHOSTUIFLAG_DISABLE_SCRIPT_INACTIVE	0x00000010, 
#define DOCHOSTUIFLAG_OPENNEWWIN				0x00000020, 
#define DOCHOSTUIFLAG_DISABLE_OFFSCREEN			0x00000040, 
#define DOCHOSTUIFLAG_FLAT_SCROLLBAR			0x00000080, 
#define DOCHOSTUIFLAG_DIV_BLOCKDEFAULT			0x00000100, 
#define DOCHOSTUIFLAG_ACTIVATE_CLIENTHIT_ONLY	0x00000200, 
#define DOCHOSTUIFLAG_OVERRIDEBEHAVIORFACTORY	0x00000400, 
#define DOCHOSTUIFLAG_CODEPAGELINKEDFONTS		0x00000800, 
#define DOCHOSTUIFLAG_URL_ENCODING_DISABLE_UTF8	x00001000, 
#define DOCHOSTUIFLAG_URL_ENCODING_ENABLE_UTF8	0x00002000, 
#define DOCHOSTUIFLAG_ENABLE_FORMS_AUTOCOMPLETE	0x00004000, 
#define DOCHOSTUIFLAG_ENABLE_INPLACE_NAVIGATION	0x00010000, 
#define DOCHOSTUIFLAG_IME_ENABLE_RECONVERSION	0x00020000, 
#define DOCHOSTUIFLAG_THEME						0x00040000, 
#define DOCHOSTUIFLAG_NOTHEME					0x00080000, 
#define DOCHOSTUIFLAG_NOPICS					0x00100000, 
#define DOCHOSTUIFLAG_NO3DOUTERBORDER			0x00200000, 
#define DOCHOSTUIFLAG_DELEGATESIDOFDISPATCH		0x00400000 
*/


// Used by UI_TranslateUrl().  These can be global because we never change them.
static const wchar_t AppUrl[] = {L"app:"};
static const wchar_t Blank[] = {L"about:blank"};
static const wchar_t Anchor[] = {L"#"};

// This is used by displayHTMLStr(). It can be global because we never change it.
static const SAFEARRAYBOUND ArrayBound = {1, 0};

static unsigned char _IID_IHTMLWindow3[] = {0xae, 0xf4, 0x50, 0x30, 0xb5, 0x98, 0xcf, 0x11, 0xbb, 0x82, 0x00, 0xaa, 0x00, 0xbd, 0xce, 0x0b};




// Our IOleInPlaceFrame functions that the browser may call
HRESULT STDMETHODCALLTYPE Frame_QueryInterface(IOleInPlaceFrame *, REFIID, LPVOID *);
HRESULT STDMETHODCALLTYPE Frame_AddRef(IOleInPlaceFrame *);
HRESULT STDMETHODCALLTYPE Frame_Release(IOleInPlaceFrame *);
HRESULT STDMETHODCALLTYPE Frame_GetWindow(IOleInPlaceFrame *, HWND *);
HRESULT STDMETHODCALLTYPE Frame_ContextSensitiveHelp(IOleInPlaceFrame *, BOOL);
HRESULT STDMETHODCALLTYPE Frame_GetBorder(IOleInPlaceFrame *, LPRECT);
HRESULT STDMETHODCALLTYPE Frame_RequestBorderSpace(IOleInPlaceFrame *, LPCBORDERWIDTHS);
HRESULT STDMETHODCALLTYPE Frame_SetBorderSpace(IOleInPlaceFrame *, LPCBORDERWIDTHS);
HRESULT STDMETHODCALLTYPE Frame_SetActiveObject(IOleInPlaceFrame *, IOleInPlaceActiveObject *, LPCOLESTR);
HRESULT STDMETHODCALLTYPE Frame_InsertMenus(IOleInPlaceFrame *, HMENU, LPOLEMENUGROUPWIDTHS);
HRESULT STDMETHODCALLTYPE Frame_SetMenu(IOleInPlaceFrame *, HMENU, HOLEMENU, HWND);
HRESULT STDMETHODCALLTYPE Frame_RemoveMenus(IOleInPlaceFrame *, HMENU);
HRESULT STDMETHODCALLTYPE Frame_SetStatusText(IOleInPlaceFrame *, LPCOLESTR);
HRESULT STDMETHODCALLTYPE Frame_EnableModeless(IOleInPlaceFrame *, BOOL);
HRESULT STDMETHODCALLTYPE Frame_TranslateAccelerator(IOleInPlaceFrame *, LPMSG, WORD);

// Our IOleInPlaceFrame VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to interact with our frame window that contains
// the browser object. We must define a particular set of functions that comprise the
// IOleInPlaceFrame set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IOleInPlaceFrame structure.
static IOleInPlaceFrameVtbl MyIOleInPlaceFrameTable = {Frame_QueryInterface,
Frame_AddRef,
Frame_Release,
Frame_GetWindow,
Frame_ContextSensitiveHelp,
Frame_GetBorder,
Frame_RequestBorderSpace,
Frame_SetBorderSpace,
Frame_SetActiveObject,
Frame_InsertMenus,
Frame_SetMenu,
Frame_RemoveMenus,
Frame_SetStatusText,
Frame_EnableModeless,
Frame_TranslateAccelerator};

// We need to return an IOleInPlaceFrame struct to the browser object. And one of our IOleInPlaceFrame
// functions (Frame_GetWindow) is going to need to access our window handle. So let's create our own
// struct that starts off with an IOleInPlaceFrame struct (and that's important -- the IOleInPlaceFrame
// struct *must* be first), and then has an extra data member where we can store our own window's HWND.
//
// And because we may want to create multiple windows, each hosting its own browser object (to
// display its own web page), then we need to create a IOleInPlaceFrame struct for each window. So,
// we're not going to declare our IOleInPlaceFrame struct globally. We'll allocate it later using
// GlobalAlloc, and then stuff the appropriate HWND in it then, and also stuff a pointer to
// MyIOleInPlaceFrameTable in it. But let's just define it here.
typedef struct {
	IOleInPlaceFrame	frame;		// The IOleInPlaceFrame must be first!

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IOleInPlaceFrame functions.
	// You don't want those functions to access global
	// variables, because then you couldn't use more
	// than one browser object. (ie, You couldn't have
	// multiple windows, each with its own embedded
	// browser object to display a different web page).
	//
	// So here is where I added my extra HWND that my
	// IOleInPlaceFrame function Frame_GetWindow() needs
	// to access.
	///////////////////////////////////////////////////
	HWND				window;
} _IOleInPlaceFrameEx;






// Our IOleClientSite functions that the browser may call
HRESULT STDMETHODCALLTYPE Site_QueryInterface(IOleClientSite *, REFIID, void **);
HRESULT STDMETHODCALLTYPE Site_AddRef(IOleClientSite *);
HRESULT STDMETHODCALLTYPE Site_Release(IOleClientSite *);
HRESULT STDMETHODCALLTYPE Site_SaveObject(IOleClientSite *);
HRESULT STDMETHODCALLTYPE Site_GetMoniker(IOleClientSite *, DWORD, DWORD, IMoniker **);
HRESULT STDMETHODCALLTYPE Site_GetContainer(IOleClientSite *, LPOLECONTAINER *);
HRESULT STDMETHODCALLTYPE Site_ShowObject(IOleClientSite *);
HRESULT STDMETHODCALLTYPE Site_OnShowWindow(IOleClientSite *, BOOL);
HRESULT STDMETHODCALLTYPE Site_RequestNewObjectLayout(IOleClientSite *);

// Our IOleClientSite VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to interact with our frame window that contains
// the browser object. We must define a particular set of functions that comprise the
// IOleClientSite set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IOleClientSite structure.
static IOleClientSiteVtbl MyIOleClientSiteTable = {Site_QueryInterface,
Site_AddRef,
Site_Release,
Site_SaveObject,
Site_GetMoniker,
Site_GetContainer,
Site_ShowObject,
Site_OnShowWindow,
Site_RequestNewObjectLayout};






// Our IDocHostUIHandler functions that the browser may call
HRESULT STDMETHODCALLTYPE UI_QueryInterface(IDocHostUIHandler *, REFIID, void **);
HRESULT STDMETHODCALLTYPE UI_AddRef(IDocHostUIHandler *);
HRESULT STDMETHODCALLTYPE UI_Release(IDocHostUIHandler *);
HRESULT STDMETHODCALLTYPE UI_ShowContextMenu(IDocHostUIHandler *, DWORD, POINT *, IUnknown *, IDispatch *);
HRESULT STDMETHODCALLTYPE UI_GetHostInfo(IDocHostUIHandler *, DOCHOSTUIINFO *);
HRESULT STDMETHODCALLTYPE UI_ShowUI(IDocHostUIHandler *, DWORD, IOleInPlaceActiveObject *, IOleCommandTarget *, IOleInPlaceFrame *, IOleInPlaceUIWindow *);
HRESULT STDMETHODCALLTYPE UI_HideUI(IDocHostUIHandler *);
HRESULT STDMETHODCALLTYPE UI_UpdateUI(IDocHostUIHandler *);
HRESULT STDMETHODCALLTYPE UI_EnableModeless(IDocHostUIHandler *, BOOL);
HRESULT STDMETHODCALLTYPE UI_OnDocWindowActivate(IDocHostUIHandler *, BOOL);
HRESULT STDMETHODCALLTYPE UI_OnFrameWindowActivate(IDocHostUIHandler *, BOOL);
HRESULT STDMETHODCALLTYPE UI_ResizeBorder(IDocHostUIHandler *, LPCRECT, IOleInPlaceUIWindow  *, BOOL);
HRESULT STDMETHODCALLTYPE UI_TranslateAccelerator(IDocHostUIHandler *, LPMSG, const GUID *, DWORD);
HRESULT STDMETHODCALLTYPE UI_GetOptionKeyPath(IDocHostUIHandler *, LPOLESTR *, DWORD);
HRESULT STDMETHODCALLTYPE UI_GetDropTarget(IDocHostUIHandler *, IDropTarget *, IDropTarget **);
HRESULT STDMETHODCALLTYPE UI_GetExternal(IDocHostUIHandler *, IDispatch **);
HRESULT STDMETHODCALLTYPE UI_TranslateUrl(IDocHostUIHandler *, DWORD, SA_PARAM_NN_VALID OLECHAR *, OLECHAR  **);
HRESULT STDMETHODCALLTYPE UI_FilterDataObject(IDocHostUIHandler *, IDataObject *, IDataObject **);

// Our IDocHostUIHandler VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to replace/set certain user interface considerations
// (such as whether to display a pop-up context menu when the user right-clicks on the embedded
// browser object). We must define a particular set of functions that comprise the
// IDocHostUIHandler set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IDocHostUIHandler structure.
static IDocHostUIHandlerVtbl MyIDocHostUIHandlerTable =  {UI_QueryInterface,
UI_AddRef,
UI_Release,
UI_ShowContextMenu,
UI_GetHostInfo,
UI_ShowUI,
UI_HideUI,
UI_UpdateUI,
UI_EnableModeless,
UI_OnDocWindowActivate,
UI_OnFrameWindowActivate,
UI_ResizeBorder,
UI_TranslateAccelerator,
UI_GetOptionKeyPath,
UI_GetDropTarget,
UI_GetExternal,
UI_TranslateUrl,
UI_FilterDataObject};

// We'll allocate our IDocHostUIHandler object dynamically with GlobalAlloc() for reasons outlined later.



// Our IOleInPlaceSite functions that the browser may call
HRESULT STDMETHODCALLTYPE InPlace_QueryInterface(IOleInPlaceSite *, REFIID, void **);
HRESULT STDMETHODCALLTYPE InPlace_AddRef(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_Release(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_GetWindow(IOleInPlaceSite *, HWND *);
HRESULT STDMETHODCALLTYPE InPlace_ContextSensitiveHelp(IOleInPlaceSite *, BOOL);
HRESULT STDMETHODCALLTYPE InPlace_CanInPlaceActivate(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceActivate(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_OnUIActivate(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_GetWindowContext(IOleInPlaceSite *, LPOLEINPLACEFRAME *, LPOLEINPLACEUIWINDOW *, LPRECT, LPRECT, LPOLEINPLACEFRAMEINFO);
HRESULT STDMETHODCALLTYPE InPlace_Scroll(IOleInPlaceSite *, SIZE);
HRESULT STDMETHODCALLTYPE InPlace_OnUIDeactivate(IOleInPlaceSite *, BOOL);
HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceDeactivate(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_DiscardUndoState(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_DeactivateAndUndo(IOleInPlaceSite *);
HRESULT STDMETHODCALLTYPE InPlace_OnPosRectChange(IOleInPlaceSite *, LPCRECT);

// Our IOleInPlaceSite VTable. This is the array of pointers to the above functions in our C
// program that the browser may call in order to interact with our frame window that contains
// the browser object. We must define a particular set of functions that comprise the
// IOleInPlaceSite set of functions (see above), and then stuff pointers to those functions
// in their respective 'slots' in this table. We want the browser to use this VTable with our
// IOleInPlaceSite structure.
static IOleInPlaceSiteVtbl MyIOleInPlaceSiteTable =  {InPlace_QueryInterface,
InPlace_AddRef,
InPlace_Release,
InPlace_GetWindow,
InPlace_ContextSensitiveHelp,
InPlace_CanInPlaceActivate,
InPlace_OnInPlaceActivate,
InPlace_OnUIActivate,
InPlace_GetWindowContext,
InPlace_Scroll,
InPlace_OnUIDeactivate,
InPlace_OnInPlaceDeactivate,
InPlace_DiscardUndoState,
InPlace_DeactivateAndUndo,
InPlace_OnPosRectChange};

// We need to pass our IOleClientSite structure to the browser object's SetClientSite().
// But the browser is also going to ask our IOleClientSite's QueryInterface() to return a pointer to
// our IOleInPlaceSite and/or IDocHostUIHandler structs. So we'll need to have those pointers handy.
// Plus, some of our IOleClientSite and IOleInPlaceSite functions will need to have the HWND to our
// window, and also a pointer to our IOleInPlaceFrame struct. So let's create a single struct that
// has the IOleClientSite, IOleInPlaceSite, IDocHostUIHandler, and IOleInPlaceFrame structs all inside
// it (so we can easily get a pointer to any one from any of those structs' functions). As long as the
// IOleClientSite struct is the very first thing in this custom struct, it's all ok. We can still pass
// it to the browser's SetClientSite() and pretend that it's an ordinary IOleClientSite. We'll call
// this new struct a _IOleClientSiteEx.
//
// And because we may want to create multiple windows, each hosting its own browser object (to
// display its own web page), then we need to create a unique _IOleClientSiteEx struct for
// each window. So, we're not going to declare this struct globally. We'll allocate it later
// using GlobalAlloc, and then initialize the structs within it.

typedef struct {
	IOleInPlaceSite			inplace;	// My IOleInPlaceSite object. Must be first with in _IOleInPlaceSiteEx.

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IOleInPlaceSite functions.
	//
	// So here is where I added my IOleInPlaceFrame
	// struct. If you need extra variables, add them
	// at the end.
	///////////////////////////////////////////////////
	_IOleInPlaceFrameEx		frame;		// My IOleInPlaceFrame object. Must be first within my _IOleInPlaceFrameEx
} _IOleInPlaceSiteEx;

typedef struct {
	IDocHostUIHandler		ui;			// My IDocHostUIHandler object. Must be first.

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IDocHostUIHandler functions.
	///////////////////////////////////////////////////
} _IDocHostUIHandlerEx;

typedef struct _DWebBrowserEvents2Ex
{
	DWebBrowserEvents2 events;
	U32 refCount;
	SimpleWindow *window;
} _DWebBrowserEvents2Ex;

typedef struct {
	IOleClientSite			client;		// My IOleClientSite object. Must be first.
	_IOleInPlaceSiteEx		inplace;	// My IOleInPlaceSite object. A convenient place to put it.
	_IDocHostUIHandlerEx	ui;			// My IDocHostUIHandler object. Must be first within my _IDocHostUIHandlerEx.
	_DWebBrowserEvents2Ex	events;     // My DWebBrowserEvents2 object.

	///////////////////////////////////////////////////
	// Here you add any extra variables that you need
	// to access in your IOleClientSite functions.
	///////////////////////////////////////////////////
} _IOleClientSiteEx;


unsigned char COM_init;






// We need an IDispatch function in order to receive some "feedback"
// from IE's browser engine as to particular actions (events) that happen.
// For example, we can request that IE inform us when the mouse pointer
// moves over some element on the web page, such as text marked with
// a particular FONT tag. Or we can request that IE inform us when the
// user clicks on the button that submits a FORM's information. Or, we
// can request that we be informed when the user double-clicks anywhere on
// the page (which isn't part of some tag). There are many elements (ie,
// tags) on the typical web page, and each type of element typically
// has many kinds of actions it can report. We can request to be informed
// of only specific actions, with specific elements. But we need an IDispatch
// for that. IE calls our IDispatch's Invoke() function for each action we've
// requested to be informed of.

// Our IDispatch functions that the browser may call
HRESULT STDMETHODCALLTYPE Dispatch_QueryInterface(IDispatch *, REFIID riid, void **);
HRESULT STDMETHODCALLTYPE Dispatch_AddRef(IDispatch *);
HRESULT STDMETHODCALLTYPE Dispatch_Release(IDispatch *);
HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfoCount(IDispatch *, unsigned int *);
HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfo(IDispatch *, unsigned int, LCID, ITypeInfo **);
HRESULT STDMETHODCALLTYPE Dispatch_GetIDsOfNames(IDispatch *, REFIID, OLECHAR **, unsigned int, LCID, DISPID *);
HRESULT STDMETHODCALLTYPE Dispatch_Invoke(IDispatch *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, unsigned int *);

// The VTable for our _IDispatchEx object.
IDispatchVtbl MyIDispatchVtbl = {
	Dispatch_QueryInterface,
	Dispatch_AddRef,
	Dispatch_Release,
	Dispatch_GetTypeInfoCount,
	Dispatch_GetTypeInfo,
	Dispatch_GetIDsOfNames,
	Dispatch_Invoke
};

// Some misc stuff used by our IDispatch
static const BSTR	OnBeforeUnload = L"onbeforeunload";
static const WCHAR	BeforeUnload[] = L"beforeunload";



// Custom IDispatch implementation to see DWebBrowserEvents2
HRESULT STDMETHODCALLTYPE Events_QueryInterface(DWebBrowserEvents2 *, REFIID, void **);
HRESULT STDMETHODCALLTYPE Events_AddRef(DWebBrowserEvents2 *);
HRESULT STDMETHODCALLTYPE Events_Release(DWebBrowserEvents2 *);
HRESULT STDMETHODCALLTYPE Events_GetTypeInfoCount(DWebBrowserEvents2 *, UINT *);
HRESULT STDMETHODCALLTYPE Events_GetTypeInfo(DWebBrowserEvents2 *, UINT, LCID, ITypeInfo **);
HRESULT STDMETHODCALLTYPE Events_GetIDsOfNames(DWebBrowserEvents2 *, REFIID, LPOLESTR *, UINT, LCID, DISPID *);
HRESULT STDMETHODCALLTYPE Events_Invoke (DWebBrowserEvents2 *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);

// The VTable for our _IDispatchEx object for events.
DWebBrowserEvents2Vtbl MyIDispatchVtblEvents = {
	Events_QueryInterface,
	Events_AddRef,
	Events_Release,
	Events_GetTypeInfoCount,
	Events_GetTypeInfo,
	Events_GetIDsOfNames,
	Events_Invoke
};

// This is a simple C example. There are lots more things you can control about the browser object, but
// we don't do it in this example. _Many_ of the functions we provide below for the browser to call, will
// never actually be called by the browser in our example. Why? Because we don't do certain things
// with the browser that would require it to call those functions (even though we need to provide
// at least some stub for all of the functions).
//
// So, for these "dummy functions" that we don't expect the browser to call, we'll just stick in some
// assembly code that causes a debugger breakpoint and tells the browser object that we don't support
// the functionality. That way, if you try to do more things with the browser object, and it starts
// calling these "dummy functions", you'll know which ones you should add more meaningful code to.
#ifdef NDEBUG
#define NOTIMPLEMENTED return(E_NOTIMPL)
#else
#define NOTIMPLEMENTED assert(0); return(E_NOTIMPL)
#endif




//////////////////////////////////// My IDocHostUIHandler functions  //////////////////////////////////////
// The browser object asks us for the pointer to our IDocHostUIHandler object by calling our IOleClientSite's
// QueryInterface (ie, Site_QueryInterface) and specifying a REFIID of IID_IDocHostUIHandler.
//
// NOTE: You need at least IE 4.0. Previous versions do not ask for, nor utilize, our IDocHostUIHandler functions.

HRESULT STDMETHODCALLTYPE UI_QueryInterface(IDocHostUIHandler *This, REFIID riid, LPVOID *ppvObj)
{
	// The browser assumes that our IDocHostUIHandler object is associated with our IOleClientSite
	// object. So it is possible that the browser may call our IDocHostUIHandler's QueryInterface()
	// to ask us to return a pointer to our IOleClientSite, in the same way that the browser calls
	// our IOleClientSite's QueryInterface() to ask for a pointer to our IDocHostUIHandler.
	//
	// Rather than duplicate much of the code in IOleClientSite's QueryInterface, let's just get
	// a pointer to our _IOleClientSiteEx object, substitute it as the 'This' arg, and call our
	// our IOleClientSite's QueryInterface. Note that since our _IDocHostUIHandlerEx is embedded right
	// inside our _IOleClientSiteEx, and comes immediately after the _IOleInPlaceSiteEx, we can employ
	// the following trickery to get the pointer to our _IOleClientSiteEx.
	return(Site_QueryInterface((IOleClientSite *)((char *)This - sizeof(IOleClientSite) - sizeof(_IOleInPlaceSiteEx)), riid, ppvObj));
}

HRESULT STDMETHODCALLTYPE UI_AddRef(IDocHostUIHandler *This)
{
	return(1);
}

HRESULT STDMETHODCALLTYPE UI_Release(IDocHostUIHandler *This)
{
	return(1);
}

// Called when the browser object is about to display its context menu.
HRESULT STDMETHODCALLTYPE UI_ShowContextMenu(IDocHostUIHandler *This, DWORD dwID, POINT *ppt, IUnknown *pcmdtReserved, IDispatch *pdispReserved)
{
	POINT			pt;

	GetCursorPos(&pt);

	// If desired, we can pop up our own custom context menu here. But instead, let's
	// just post a WM_CONTENTMENU message to the window hosting the web browser (stored in our
	// _IOleInPlaceFrameEx). Then, we'll tell the browser not to bring up its own context menu,
	// by returning S_OK.
	if(IsDebuggerPresent())
	{
		return(S_FALSE);
	}
	else
	{
		PostMessage(((_IOleInPlaceSiteEx *)((char *)This - sizeof(_IOleInPlaceSiteEx)))->frame.window, WM_CONTEXTMENU, (WPARAM)pt.x, pt.y);
		return(S_OK);
	}

}

// Called at initialization of the browser object UI. We can set various features of the browser object here.
HRESULT STDMETHODCALLTYPE UI_GetHostInfo(IDocHostUIHandler *This, DOCHOSTUIINFO *pInfo)
{
	pInfo->cbSize = sizeof(DOCHOSTUIINFO);

	// Set some flags. We don't want any 3D border. You can do other things like hide
	// the scroll bar (DOCHOSTUIFLAG_SCROLL_NO), disable picture display (DOCHOSTUIFLAG_NOPICS),
	// disable any script running when the page is loaded (DOCHOSTUIFLAG_DISABLE_SCRIPT_INACTIVE),
	// open a site in a new browser window when the user clicks on some link (DOCHOSTUIFLAG_OPENNEWWIN),
	// and lots of other things. See the MSDN docs on the DOCHOSTUIINFO struct passed to us.
	pInfo->dwFlags = DOCHOSTUIFLAG_NO3DBORDER;

	// Set what happens when the user double-clicks on the object. Here we use the default.
	pInfo->dwDoubleClick = DOCHOSTUIDBLCLK_DEFAULT;

	return(S_OK);
}

// Called when the browser object shows its UI. This allows us to replace its menus and toolbars by creating our
// own and displaying them here.
HRESULT STDMETHODCALLTYPE UI_ShowUI(IDocHostUIHandler *This, DWORD dwID, IOleInPlaceActiveObject *pActiveObject, IOleCommandTarget __RPC_FAR *pCommandTarget, IOleInPlaceFrame __RPC_FAR *pFrame, IOleInPlaceUIWindow *pDoc)
{
	// We've already got our own UI in place so just return S_OK to tell the browser
	// not to display its menus/toolbars. Otherwise we'd return S_FALSE to let it do
	// that.
	return(S_OK);
}

// Called when browser object hides its UI. This allows us to hide any menus/toolbars we created in ShowUI.
HRESULT STDMETHODCALLTYPE UI_HideUI(IDocHostUIHandler *This)
{
	return(S_OK);
}

// Called when the browser object wants to notify us that the command state has changed. We should update any
// controls we have that are dependent upon our embedded object, such as "Back", "Forward", "Stop", or "Home"
// buttons.
HRESULT STDMETHODCALLTYPE UI_UpdateUI(IDocHostUIHandler *This)
{
	// We update our UI in our window message loop so we don't do anything here.
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's EnableModeless() function. Also
// called when the browser displays a modal dialog box.
HRESULT STDMETHODCALLTYPE UI_EnableModeless(IDocHostUIHandler *This, BOOL fEnable)
{
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's OnDocWindowActivate() function.
// This informs off of when the object is getting/losing the focus.
HRESULT STDMETHODCALLTYPE UI_OnDocWindowActivate(IDocHostUIHandler *This, BOOL fActivate)
{
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's OnFrameWindowActivate() function.
HRESULT STDMETHODCALLTYPE UI_OnFrameWindowActivate(IDocHostUIHandler *This, BOOL fActivate)
{
	return(S_OK);
}

// Called from the browser object's IOleInPlaceActiveObject object's ResizeBorder() function.
HRESULT STDMETHODCALLTYPE UI_ResizeBorder(IDocHostUIHandler *This, LPCRECT prcBorder, IOleInPlaceUIWindow *pUIWindow, BOOL fRameWindow)
{
	return(S_OK);
}

// Called from the browser object's TranslateAccelerator routines to translate key strokes to commands.
HRESULT STDMETHODCALLTYPE UI_TranslateAccelerator(IDocHostUIHandler *This, LPMSG lpMsg, const GUID *pguidCmdGroup, DWORD nCmdID)
{
	// We don't intercept any keystrokes, so we do nothing here. But for example, if we wanted to
	// override the TAB key, perhaps do something with it ourselves, and then tell the browser
	// not to do anything with this keystroke, we'd do:
	//
	//	if (pMsg && pMsg->message == WM_KEYDOWN && pMsg->wParam == VK_TAB)
	//	{
	//		// Here we do something as a result of a TAB key press.
	//
	//		// Tell the browser not to do anything with it.
	//		return(S_FALSE);
	//	}
	//
	//	// Otherwise, let the browser do something with this message.
	//	return(S_OK);
	
	//if (lpMsg && lpMsg->message == WM_KEYDOWN && lpMsg->wParam == VK_TAB)
	//{
	//	return(S_OK);
	//}

	// For our example, we want to make sure that the user can't invoke some key to popup the context
	// menu, so we'll tell it to ignore all msgs.
	return(S_FALSE);
}

// Called by the browser object to find where the host wishes the browser to get its options in the registry.
// We can use this to prevent the browser from using its default settings in the registry, by telling it to use
// some other registry key we've setup with the options we want.
HRESULT STDMETHODCALLTYPE UI_GetOptionKeyPath(IDocHostUIHandler * This, LPOLESTR __RPC_FAR *pchKey, DWORD dw)
{
	// Let the browser use its default registry settings.
	return(S_FALSE);
}

// Called by the browser object when it is used as a drop target. We can supply our own IDropTarget object,
// IDropTarget functions, and IDropTarget VTable if we want to determine what happens when someone drags and
// drops some object on our embedded browser object.
HRESULT STDMETHODCALLTYPE UI_GetDropTarget(IDocHostUIHandler * This, IDropTarget __RPC_FAR *pDropTarget, IDropTarget __RPC_FAR *__RPC_FAR *ppDropTarget)
{
	// Return our IDropTarget object associated with this IDocHostUIHandler object. I don't
	// know why we don't do this via UI_QueryInterface(), but we don't.

	// NOTE: If we want/need an IDropTarget interface, then we would have had to setup our own
	// IDropTarget functions, IDropTarget VTable, and create an IDropTarget object. We'd want to put
	// a pointer to the IDropTarget object in our own custom IDocHostUIHandlerEx object (like how
	// we may add an HWND member for the use of UI_ShowContextMenu). So when we created our
	// IDocHostUIHandlerEx object, maybe we'd add a 'idrop' member to the end of it, and
	// store a pointer to our IDropTarget object there. Then we could return this pointer as so:
	//
	// *pDropTarget = ((IDocHostUIHandlerEx FAR *)This)->idrop;
	// return(S_OK);

	// But for our purposes, we don't need an IDropTarget object, so we'll tell whomever is calling
	// us that we don't have one.
	return(S_FALSE);
}

// Called by the browser when it wants a pointer to our IDispatch object. This object allows us to expose
// our own automation interface (ie, our own COM objects) to other entities that are running within the
// context of the browser so they can call our functions if they want. An example could be a javascript
// running in the URL we display could call our IDispatch functions. We'd write them so that any args passed
// to them would use the generic datatypes like a BSTR for utmost flexibility.
HRESULT STDMETHODCALLTYPE UI_GetExternal(IDocHostUIHandler *This, IDispatch **ppDispatch)
{
	// Return our IDispatch object associated with this IDocHostUIHandler object. I don't
	// know why we don't do this via UI_QueryInterface(), but we don't.

	// NOTE: If we want/need an IDispatch interface, then we would have had to setup our own
	// IDispatch functions, IDispatch VTable, and create an IDispatch object. We'd want to put
	// a pointer to the IDispatch object in our custom _IDocHostUIHandlerEx object (like how
	// we may add an HWND member for the use of UI_ShowContextMenu). So when we defined our
	// _IDocHostUIHandlerEx object, maybe we'd add a 'idispatch' member to the end of it, and
	// store a pointer to our IDispatch object there. Then we could return this pointer as so:
	//
	// *ppDispatch = ((_IDocHostUIHandlerEx FAR *)This)->idispatch;
	// return(S_OK);

	// But for our purposes, we don't need an IDispatch object, so we'll tell whomever is calling
	// us that we don't have one. Note: We must set ppDispatch to 0 if we don't return our own
	// IDispatch object.
	*ppDispatch = 0;
	return(S_FALSE);
}

/* ************************* asciiToNumW() **************************
* Converts the OLECHAR string of digits (expressed in base 10) to a
* 32-bit DWORD.
*
* val =	Pointer to the nul-terminated string of digits to convert.
*
* RETURNS: The integer value as a DWORD.
*
* NOTE: Skips leading spaces before the first digit.
*/

DWORD asciiToNumW(OLECHAR *val)
{
	OLECHAR			chr;
	DWORD			len;

	// Result is initially 0
	len = 0;

	// Skip leading spaces
	while (*val == ' ' || *val == 0x09) val++;

	// Convert next digit
	while (*val)
	{
		chr = *(val)++ - '0';
		if ((DWORD)chr > 9) break;
		len += (len + (len << 3) + chr);
	}

	return(len);
}

// Called by the browser object to give us an opportunity to modify the URL to be loaded.
HRESULT STDMETHODCALLTYPE UI_TranslateUrl(IDocHostUIHandler *This, DWORD dwTranslate, OLECHAR *pchURLIn, OLECHAR **ppchURLOut)
{
	unsigned short	*src;
	unsigned short	*dest;
	DWORD			len;
	HINSTANCE		ret;
	HWND	hwnd;
	OLECHAR *pchURLInAfterHTTP;

	// Get length of URL
	src = pchURLIn;
	while ((*(src)++));
	--src;
	len = src - pchURLIn; 

	// See if the URL starts with 'app:'. We will use this as a "special link" that can be
	// placed on a web page. The URL will be in the format "app:XXX" where XXX is some
	// number. For example, maybe the following will be placed on the web page:
	//
	// <A HREF="app:1">Some special link</A>
	//
	// When the user clicks on it, we will substitute a blank page, and then send the
	// application a WM_APP message with wParam as the number after the app:. The
	// application can then do anything it wants as a result of this, for example,
	// call DisplayHTMLStr to load some other string in memory, or whatever.
	if (len >= 4 && !_wcsnicmp(pchURLIn, (WCHAR *)&AppUrl[0], 4))
	{
		// Allocate a new buffer to return an "about:blank" URL
		dest = (OLECHAR *)CoTaskMemAlloc(12<<1);
		assertmsg(dest, "Unable to allocate memory for new URL");
		*ppchURLOut = dest;

		// Convert the number after the "app:"
		len = asciiToNumW(pchURLIn + 4);

		// Return "about:blank" or "#"
		// XHRs need to become "about:blank" while normal page links should become "#"
		switch(len)
		{
		case 1: // Page load XHR
			CopyMemory(dest, &Blank[0], 12<<1);
			break;
		case 2: // Action button link
			CopyMemory(dest, &Anchor[0], 12<<1);
			break;
		case 3: // Options link
			CopyMemory(dest, &Anchor[0], 12<<1);
			break;
		case 4: // Login form
			CopyMemory(dest, &Anchor[0], 12<<1);
			break;
		default:
			FatalErrorf("Unknown application URL %u", len);
		}

		// Get our host window. That was stored in our _IOleInPlaceFrameEx
		hwnd = ((_IOleInPlaceSiteEx *)((char *)This - sizeof(_IOleInPlaceSiteEx)))->frame.window;

		// Post a message to this window using WM_APP, and pass the number converted above.
		// Do not SendMessage()!. Post instead, since the browser does not like us changing
		// the URL within this here callback.
		PostMessage(hwnd, WM_APP, (WPARAM)len, 0);

		// Tell browser that we returned a URL
		return(S_OK);
	}

	// We don't need to modify the URL. Note: We need to set ppchURLOut to 0 if we don't
	// return an OLECHAR (buffer) containing a modified version of pchURLIn.
	pchURLInAfterHTTP = wcsstr(pchURLIn, L"://");
	if(pchURLInAfterHTTP)
		pchURLInAfterHTTP += 3;
	if (pchURLInAfterHTTP)
	{
		ANALYSIS_ASSUME(pchURLInAfterHTTP);
		if (wcsstr(pchURLInAfterHTTP, L"/launcher"))
		{
			*ppchURLOut = 0; 
			return(S_FALSE);
		}
	}
	else if (wcsicmp(pchURLIn, L"about:blank") == 0 || wcsstr(pchURLIn, L"res:") != NULL)
	{
		*ppchURLOut = 0; 
		return(S_FALSE);
	}

	// For any other URL, send it to the default browser.
	ret = ShellExecuteW(NULL, L"open", pchURLIn, NULL, NULL, SW_SHOWNORMAL);

	// Send back a blank anchor
	dest = (OLECHAR *)CoTaskMemAlloc(2<<1);
	assertmsg(dest, "Unable to allocate memory for new URL");
	*ppchURLOut = dest;
	CopyMemory(dest, &Anchor[0], 2<<1);
	return(S_OK);
}

// Called by the browser when it does cut/paste to the clipboard. This allows us to block certain clipboard
// formats or support additional clipboard formats.
HRESULT STDMETHODCALLTYPE UI_FilterDataObject(IDocHostUIHandler * This, IDataObject *pDO, IDataObject **ppDORet)
{
	// Return our IDataObject object associated with this IDocHostUIHandler object. I don't
	// know why we don't do this via UI_QueryInterface(), but we don't.

	// NOTE: If we want/need an IDataObject interface, then we would have had to setup our own
	// IDataObject functions, IDataObject VTable, and create an IDataObject object. We'd want to put
	// a pointer to the IDataObject object in our custom _IDocHostUIHandlerEx object (like how
	// we may add an HWND member for the use of UI_ShowContextMenu). So when we defined our
	// _IDocHostUIHandlerEx object, maybe we'd add a 'idata' member to the end of it, and
	// store a pointer to our IDataObject object there. Then we could return this pointer as so:
	//
	// *ppDORet = ((_IDocHostUIHandlerEx FAR *)This)->idata;
	// return(S_OK);

	// But for our purposes, we don't need an IDataObject object, so we'll tell whomever is calling
	// us that we don't have one. Note: We must set ppDORet to 0 if we don't return our own
	// IDataObject object.
	*ppDORet = 0;
	return(S_FALSE);
}






////////////////////////////////////// My IOleClientSite functions  /////////////////////////////////////
// We give the browser object a pointer to our IOleClientSite object when we call OleCreate() or DoVerb().

/************************* Site_QueryInterface() *************************
* The browser object calls this when it wants a pointer to one of our
* IOleClientSite, IDocHostUIHandler, or IOleInPlaceSite structures. They
* are all accessible via the _IOleClientSiteEx struct we allocated in
* EmbedBrowserObject() and passed to DoVerb() and OleCreate().
*
* This =		A pointer to whatever _IOleClientSiteEx struct we passed to
*				OleCreate() or DoVerb().
* riid =		A GUID struct that the browser passes us to clue us as to
*				which type of struct (object) it would like a pointer
*				returned for.
* ppvObject =	Where the browser wants us to return a pointer to the
*				appropriate struct. (ie, It passes us a handle to fill in).
*
* RETURNS: S_OK if we return the struct, or E_NOINTERFACE if we don't have
* the requested struct.
*/

HRESULT STDMETHODCALLTYPE Site_QueryInterface(IOleClientSite *This, REFIID riid, void **ppvObject)
{
	// It just so happens that the first arg passed to us is our _IOleClientSiteEx struct we allocated
	// and passed to DoVerb() and OleCreate(). Nevermind that 'This' is declared is an IOleClientSite *.
	// Remember that in EmbedBrowserObject(), we allocated our own _IOleClientSiteEx struct, and lied
	// to OleCreate() and DoVerb() -- passing our _IOleClientSiteEx struct and saying it was an
	// IOleClientSite struct. It's ok. An _IOleClientSiteEx starts with an embedded IOleClientSite, so
	// the browser didn't mind. So that's what the browser object is passing us now. The browser doesn't
	// know that it's really an _IOleClientSiteEx struct. But we do. So we can recast it and use it as
	// so here.

	// If the browser is asking us to match IID_IOleClientSite, then it wants us to return a pointer to
	// our IOleClientSite struct. Then the browser will use the VTable in that struct to call our
	// IOleClientSite functions. It will also pass this same pointer to all of our IOleClientSite
	// functions.
	//
	// Actually, we're going to lie to the browser again. We're going to return our own _IOleClientSiteEx
	// struct, and tell the browser that it's a IOleClientSite struct. It's ok. The first thing in our
	// _IOleClientSiteEx is an embedded IOleClientSite, so the browser doesn't mind. We want the browser
	// to continue passing our _IOleClientSiteEx pointer wherever it would normally pass a IOleClientSite
	// pointer.
	// 
	// The IUnknown interface uses the same VTable as the first object in our _IOleClientSiteEx
	// struct (which happens to be an IOleClientSite). So if the browser is asking us to match
	// IID_IUnknown, then we'll also return a pointer to our _IOleClientSiteEx.

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IOleClientSite, sizeof(GUID)))
		*ppvObject = &((_IOleClientSiteEx *)This)->client;

	// If the browser is asking us to match IID_IOleInPlaceSite, then it wants us to return a pointer to
	// our IOleInPlaceSite struct. Then the browser will use the VTable in that struct to call our
	// IOleInPlaceSite functions.  It will also pass this same pointer to all of our IOleInPlaceSite
	// functions (except for Site_QueryInterface, Site_AddRef, and Site_Release. Those will always get
	// the pointer to our _IOleClientSiteEx.
	//
	// Actually, we're going to lie to the browser. We're going to return our own _IOleInPlaceSiteEx
	// struct, and tell the browser that it's a IOleInPlaceSite struct. It's ok. The first thing in
	// our _IOleInPlaceSiteEx is an embedded IOleInPlaceSite, so the browser doesn't mind. We want the
	// browser to continue passing our _IOleInPlaceSiteEx pointer wherever it would normally pass a
	// IOleInPlaceSite pointer.
	else if (!memcmp(riid, &IID_IOleInPlaceSite, sizeof(GUID)))
		*ppvObject = &((_IOleClientSiteEx *)This)->inplace;

	// If the browser is asking us to match IID_IDocHostUIHandler, then it wants us to return a pointer to
	// our IDocHostUIHandler struct. Then the browser will use the VTable in that struct to call our
	// IDocHostUIHandler functions.  It will also pass this same pointer to all of our IDocHostUIHandler
	// functions (except for Site_QueryInterface, Site_AddRef, and Site_Release. Those will always get
	// the pointer to our _IOleClientSiteEx.
	//
	// Actually, we're going to lie to the browser. We're going to return our own _IDocHostUIHandlerEx
	// struct, and tell the browser that it's a IDocHostUIHandler struct. It's ok. The first thing in
	// our _IDocHostUIHandlerEx is an embedded IDocHostUIHandler, so the browser doesn't mind. We want the
	// browser to continue passing our _IDocHostUIHandlerEx pointer wherever it would normally pass a
	// IDocHostUIHandler pointer. My, we're really playing dirty tricks on the browser here. heheh.
	else if (!memcmp(riid, &IID_IDocHostUIHandler, sizeof(GUID)))
		*ppvObject = &((_IOleClientSiteEx *)This)->ui;

	// For other types of objects the browser wants, just report that we don't have any such objects.
	// NOTE: If you want to add additional functionality to your browser hosting, you may need to
	// provide some more objects here. You'll have to investigate what the browser is asking for
	// (ie, what REFIID it is passing).
	else
	{
		*ppvObject = 0;
		return(E_NOINTERFACE);
	}

	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Site_AddRef(IOleClientSite *This)
{
	return(1);
}

HRESULT STDMETHODCALLTYPE Site_Release(IOleClientSite *This)
{
	return(1);
}

HRESULT STDMETHODCALLTYPE Site_SaveObject(IOleClientSite *This)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Site_GetMoniker(IOleClientSite *This, DWORD dwAssign, DWORD dwWhichMoniker, IMoniker **ppmk)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Site_GetContainer(IOleClientSite *This, LPOLECONTAINER *ppContainer)
{
	// Tell the browser that we are a simple object and don't support a container
	*ppContainer = 0;

	return(E_NOINTERFACE);
}

HRESULT STDMETHODCALLTYPE Site_ShowObject(IOleClientSite *This)
{
	return(NOERROR);
}

HRESULT STDMETHODCALLTYPE Site_OnShowWindow(IOleClientSite *This, BOOL fShow)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Site_RequestNewObjectLayout(IOleClientSite *This)
{
	NOTIMPLEMENTED;
}











////////////////////////////////////// My IOleInPlaceSite functions  /////////////////////////////////////
// The browser object asks us for the pointer to our IOleInPlaceSite object by calling our IOleClientSite's
// QueryInterface (ie, Site_QueryInterface) and specifying a REFIID of IID_IOleInPlaceSite.

HRESULT STDMETHODCALLTYPE InPlace_QueryInterface(IOleInPlaceSite *This, REFIID riid, LPVOID * ppvObj)
{
	// The browser assumes that our IOleInPlaceSite object is associated with our IOleClientSite
	// object. So it is possible that the browser may call our IOleInPlaceSite's QueryInterface()
	// to ask us to return a pointer to our IOleClientSite, in the same way that the browser calls
	// our IOleClientSite's QueryInterface() to ask for a pointer to our IOleInPlaceSite.
	//
	// Rather than duplicate much of the code in IOleClientSite's QueryInterface, let's just get
	// a pointer to our _IOleClientSiteEx object, substitute it as the 'This' arg, and call our
	// our IOleClientSite's QueryInterface. Note that since our IOleInPlaceSite is embedded right
	// inside our _IOleClientSiteEx, and comes immediately after the IOleClientSite, we can employ
	// the following trickery to get the pointer to our _IOleClientSiteEx.
	return(Site_QueryInterface((IOleClientSite *)((char *)This - sizeof(IOleClientSite)), riid, ppvObj));
}

HRESULT STDMETHODCALLTYPE InPlace_AddRef(IOleInPlaceSite *This)
{
	return(1);
}

HRESULT STDMETHODCALLTYPE InPlace_Release(IOleInPlaceSite *This)
{
	return(1);
}

HRESULT STDMETHODCALLTYPE InPlace_GetWindow(IOleInPlaceSite *This, HWND * lphwnd)
{
	// Return the HWND of the window that contains this browser object. We stored that
	// HWND in our _IOleInPlaceSiteEx struct. Nevermind that the function declaration for
	// Site_GetWindow says that 'This' is an IOleInPlaceSite *. Remember that in
	// EmbedBrowserObject(), we allocated our own _IOleInPlaceSiteEx struct which
	// contained an embedded IOleInPlaceSite struct within it. And when the browser
	// called Site_QueryInterface() to get a pointer to our IOleInPlaceSite object, we
	// returned a pointer to our _IOleClientSiteEx. The browser doesn't know this. But
	// we do. That's what we're really being passed, so we can recast it and use it as
	// so here.
	*lphwnd = ((_IOleInPlaceSiteEx *)This)->frame.window;

	return(S_OK);
}

HRESULT STDMETHODCALLTYPE InPlace_ContextSensitiveHelp(IOleInPlaceSite *This, BOOL fEnterMode)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE InPlace_CanInPlaceActivate(IOleInPlaceSite *This)
{
	// Tell the browser we can in place activate
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceActivate(IOleInPlaceSite *This)
{
	// Tell the browser we did it ok
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE InPlace_OnUIActivate(IOleInPlaceSite *This)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE InPlace_GetWindowContext(IOleInPlaceSite *This, LPOLEINPLACEFRAME *lplpFrame, LPOLEINPLACEUIWINDOW *lplpDoc, LPRECT lprcPosRect, LPRECT lprcClipRect, LPOLEINPLACEFRAMEINFO lpFrameInfo)
{
	// Give the browser the pointer to our IOleInPlaceFrame struct. We stored that pointer
	// in our _IOleInPlaceSiteEx struct. Nevermind that the function declaration for
	// Site_GetWindowContext says that 'This' is an IOleInPlaceSite *. Remember that in
	// EmbedBrowserObject(), we allocated our own _IOleInPlaceSiteEx struct which
	// contained an embedded IOleInPlaceSite struct within it. And when the browser
	// called Site_QueryInterface() to get a pointer to our IOleInPlaceSite object, we
	// returned a pointer to our _IOleClientSiteEx. The browser doesn't know this. But
	// we do. That's what we're really being passed, so we can recast it and use it as
	// so here.
	//
	// Actually, we're giving the browser a pointer to our own _IOleInPlaceSiteEx struct,
	// but telling the browser that it's a IOleInPlaceSite struct. No problem. Our
	// _IOleInPlaceSiteEx starts with an embedded IOleInPlaceSite, so the browser is
	// cool with it. And we want the browser to pass a pointer to this _IOleInPlaceSiteEx
	// wherever it would pass a IOleInPlaceSite struct to our IOleInPlaceSite functions.
	*lplpFrame = (LPOLEINPLACEFRAME)&((_IOleInPlaceSiteEx *)This)->frame;

	// We have no OLEINPLACEUIWINDOW
	*lplpDoc = 0;

	// Fill in some other info for the browser
	lpFrameInfo->fMDIApp = FALSE;
	lpFrameInfo->hwndFrame = ((_IOleInPlaceFrameEx *)*lplpFrame)->window;
	lpFrameInfo->haccel = 0;
	lpFrameInfo->cAccelEntries = 0;

	// Give the browser the dimensions of where it can draw. We give it our entire window to fill.
	// We do this in InPlace_OnPosRectChange() which is called right when a window is first
	// created anyway, so no need to duplicate it here.
	//	GetClientRect(lpFrameInfo->hwndFrame, lprcPosRect);
	//	GetClientRect(lpFrameInfo->hwndFrame, lprcClipRect);

	return(S_OK);
}

HRESULT STDMETHODCALLTYPE InPlace_Scroll(IOleInPlaceSite *This, SIZE scrollExtent)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE InPlace_OnUIDeactivate(IOleInPlaceSite *This, BOOL fUndoable)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE InPlace_OnInPlaceDeactivate(IOleInPlaceSite *This)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE InPlace_DiscardUndoState(IOleInPlaceSite *This)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE InPlace_DeactivateAndUndo(IOleInPlaceSite *This)
{
	NOTIMPLEMENTED;
}

// Called when the position of the browser object is changed, such as when we call the IWebBrowser2's put_Width(),
// put_Height(), put_Left(), or put_Right().
HRESULT STDMETHODCALLTYPE InPlace_OnPosRectChange(IOleInPlaceSite *This, LPCRECT lprcPosRect)
{
	IOleObject			*browserObject;
	IOleInPlaceObject	*inplace;

	// We need to get the browser's IOleInPlaceObject object so we can call its SetObjectRects
	// function.
	browserObject = *((IOleObject **)((char *)This - sizeof(IOleObject *) - sizeof(IOleClientSite)));
	if (SUCCEEDED(browserObject->lpVtbl->QueryInterface(browserObject, &IID_IOleInPlaceObject, (void **)&inplace)))
	{
		// Give the browser the dimensions of where it can draw.
		inplace->lpVtbl->SetObjectRects(inplace, lprcPosRect, lprcPosRect);
	}

	return(S_OK);
}







////////////////////////////////////// My IOleInPlaceFrame functions  /////////////////////////////////////////

HRESULT STDMETHODCALLTYPE Frame_QueryInterface(IOleInPlaceFrame *This, REFIID riid, LPVOID *ppvObject)
{
	*ppvObject = 0;

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IOleInPlaceFrame, sizeof(GUID)))
	{
		*ppvObject = (void *)This;

		// Increment its usage count. The caller will be expected to call our
		// IOleInPlaceFrame's Release() (ie, Frame_Release) when it's done with
		// our IOleInPlaceFrame.
		Frame_AddRef(This);

		return(S_OK);
	}

	*ppvObject = 0;
	return(E_NOINTERFACE);
}

HRESULT STDMETHODCALLTYPE Frame_AddRef(IOleInPlaceFrame *This)
{
	return(1);
}

HRESULT STDMETHODCALLTYPE Frame_Release(IOleInPlaceFrame *This)
{
	return(1);
}

HRESULT STDMETHODCALLTYPE Frame_GetWindow(IOleInPlaceFrame *This, HWND *lphwnd)
{
	// Give the browser the HWND to our window that contains the browser object. We
	// stored that HWND in our IOleInPlaceFrame struct. Nevermind that the function
	// declaration for Frame_GetWindow says that 'This' is an IOleInPlaceFrame *. Remember
	// that in EmbedBrowserObject(), we allocated our own IOleInPlaceFrameEx struct which
	// contained an embedded IOleInPlaceFrame struct within it. And then we lied when
	// Site_GetWindowContext() returned that IOleInPlaceFrameEx. So that's what the
	// browser is passing us. It doesn't know that. But we do. So we can recast it and
	// use it as so here.
	*lphwnd = ((_IOleInPlaceFrameEx *)This)->window;
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Frame_ContextSensitiveHelp(IOleInPlaceFrame *This, BOOL fEnterMode)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Frame_GetBorder(IOleInPlaceFrame *This, LPRECT lprectBorder)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Frame_RequestBorderSpace(IOleInPlaceFrame * This, LPCBORDERWIDTHS pborderwidths)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Frame_SetBorderSpace(IOleInPlaceFrame * This, LPCBORDERWIDTHS pborderwidths)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Frame_SetActiveObject(IOleInPlaceFrame * This, IOleInPlaceActiveObject *pActiveObject, LPCOLESTR pszObjName)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Frame_InsertMenus(IOleInPlaceFrame * This, HMENU hmenuShared, LPOLEMENUGROUPWIDTHS lpMenuWidths)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Frame_SetMenu(IOleInPlaceFrame * This, HMENU hmenuShared, HOLEMENU holemenu, HWND hwndActiveObject)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Frame_RemoveMenus(IOleInPlaceFrame * This, HMENU hmenuShared)
{
	NOTIMPLEMENTED;
}

HRESULT STDMETHODCALLTYPE Frame_SetStatusText(IOleInPlaceFrame * This, LPCOLESTR pszStatusText)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Frame_EnableModeless(IOleInPlaceFrame * This, BOOL fEnable)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Frame_TranslateAccelerator(IOleInPlaceFrame * This, LPMSG lpmsg, WORD wID)
{
	NOTIMPLEMENTED;
}






//////////////////////////////////// My IDispatch functions  //////////////////////////////////////
// The browser uses our IDispatch to give feedback when certain actions occur on the web page.

HRESULT STDMETHODCALLTYPE Dispatch_QueryInterface(IDispatch * This, REFIID riid, void **ppvObject)
{
	*ppvObject = 0;

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IDispatch, sizeof(GUID)))
	{
		*ppvObject = (void *)This;

		// Increment its usage count. The caller will be expected to call our
		// IDispatch's Release() (ie, Dispatch_Release) when it's done with
		// our IDispatch.
		Dispatch_AddRef(This);

		return(S_OK);
	}

	*ppvObject = 0;
	return(E_NOINTERFACE);
}

HRESULT STDMETHODCALLTYPE Dispatch_AddRef(IDispatch *This)
{
	return(InterlockedIncrement(&((_IDispatchEx *)This)->refCount));
}

HRESULT STDMETHODCALLTYPE Dispatch_Release(IDispatch *This)
{
	if (InterlockedDecrement( &((_IDispatchEx *)This)->refCount ) == 0)
	{
		/* If you uncomment the following line you should get one message
		* when the document unloads for each successful call to
		* CreateEventHandler. If not, check you are setting all events
		* (that you set), to null or detaching them.
		*/
		// OutputDebugString("One event handler destroyed");

		GlobalFree(((char *)This - ((_IDispatchEx *)This)->extraSize));
		return(0);
	}

	return(((_IDispatchEx *)This)->refCount);
}

HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfoCount(IDispatch *This, unsigned int *pctinfo)
{
	return(E_NOTIMPL);
}

HRESULT STDMETHODCALLTYPE Dispatch_GetTypeInfo(IDispatch *This, unsigned int iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
	return(E_NOTIMPL);
}

HRESULT STDMETHODCALLTYPE Dispatch_GetIDsOfNames(IDispatch *This, REFIID riid, OLECHAR ** rgszNames, unsigned int cNames, LCID lcid, DISPID * rgDispId)
{
	return(S_OK);
}

static void webDetach(_IDispatchEx *lpDispatchEx)
{
	IHTMLWindow3	*htmlWindow3;

	// !!!: This is crashing for some reason. Just band-aiding for now <NPK 2008-09-19>
	return;

	// Get the IHTMLWindow3 and call its detachEvent() to disconnect our _IDispatchEx
	// from the element on the web page.
	if (SUCCEEDED(lpDispatchEx->htmlWindow2->lpVtbl->QueryInterface(lpDispatchEx->htmlWindow2, (GUID *)&_IID_IHTMLWindow3[0], (void **)&htmlWindow3)) && htmlWindow3)
	{
		htmlWindow3->lpVtbl->detachEvent(htmlWindow3, OnBeforeUnload, (LPDISPATCH)lpDispatchEx);
		htmlWindow3->lpVtbl->Release(htmlWindow3);
	}

	// Release any object that was originally passed to CreateEventHandler().
	if (lpDispatchEx->object) lpDispatchEx->object->lpVtbl->Release(lpDispatchEx->object);

	// We don't need the IHTMLWindow2 any more (that we got in CreateEventHandler).
	lpDispatchEx->htmlWindow2->lpVtbl->Release(lpDispatchEx->htmlWindow2);
}

HRESULT STDMETHODCALLTYPE Dispatch_Invoke(IDispatch *This, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS * pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, unsigned int *puArgErr)
{
	WEBPARAMS		webParams;
	BSTR			strType;

	// Get the IHTMLEventObj from the associated window.
	if (SUCCEEDED(((_IDispatchEx *)This)->htmlWindow2->lpVtbl->get_event(((_IDispatchEx *)This)->htmlWindow2, &webParams.htmlEvent)) && webParams.htmlEvent)
	{
		// Get the event's type (ie, a BSTR) by calling the IHTMLEventObj's get_type().
		webParams.htmlEvent->lpVtbl->get_type(webParams.htmlEvent, &strType);
		if (strType)
		{
			// Set the WEBPARAMS.NMHDR struct's hwndFrom to the window hosting the browser,
			// and set idFrom to 0 to let him know that this is a message that was sent
			// as a result of an action occuring on the web page (that the app asked to be
			// informed of).
			webParams.nmhdr.hwndFrom = ((_IDispatchEx *)This)->hwnd;

			// Is the type "beforeunload"? If so, set WEBPARAMS.NMHDR struct's "code" to 0, otherwise 1.
			if (!(webParams.nmhdr.code = lstrcmpW(strType, &BeforeUnload[0])))
			{
				// This is a "beforeunload" event. NOTE: This should be the last event before our
				// _IDispatchEx is destroyed.

				// If the id number is negative, then this is the app's way of telling us that it
				// expects us to stuff the "eventStr" arg (passed to CreateEventHandler) into the
				// WEBPARAMS->eventStr.
				if (((_IDispatchEx *)This)->id < 0)
				{	
user:				webParams.eventStr = (LPCTSTR)((_IDispatchEx *)This)->userdata;
				}

				// We can free the BSTR we got above since we no longer need it.
				goto freestr;
			}

			// It's some other type of event. Set the WEBPARAMS->eventStr so he can do a lstrcmp()
			// to see what event happened.
			else
			{
				// Let app know that this is not a "beforeunload" event.
				webParams.nmhdr.code = 1;

				// If the app wants us to set the event type string into the WEBPARAMS, then get that
				// string as UNICODE or ANSI -- whichever is appropriate for the app window.
				if (((_IDispatchEx *)This)->id < 0) goto user;
				if (!IsWindowUnicode(webParams.nmhdr.hwndFrom))
				{
					// For ANSI, we need to convert the BSTR to an ANSI string, and then we no longer
					// need the BSTR.
					webParams.nmhdr.idFrom = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)strType, -1, 0, 0, 0, 0);
					if (!(webParams.eventStr = GlobalAlloc(GMEM_FIXED, sizeof(char) * webParams.nmhdr.idFrom))) goto bad;
					WideCharToMultiByte(CP_ACP, 0, (WCHAR *)strType, -1, (char *)webParams.eventStr, webParams.nmhdr.idFrom, 0, 0);
freestr:			SysFreeString(strType);
					strType = 0;
				}

				// For UNICODE, we can use the BSTR as is. We can't free the BSTR yet.
				else
					webParams.eventStr = (LPCTSTR)strType;
			}

			// Send a WM_NOTIFY message to the window with the _IDispatchEx as WPARAM, and
			// the WEBPARAMS as LPARAM.
			webParams.nmhdr.idFrom = 0;
			SendMessage(webParams.nmhdr.hwndFrom, WM_NOTIFY, (WPARAM)This, (LPARAM)&webParams);

			// Free anything allocated or gotten above
bad:		if (strType) SysFreeString(strType);
			else if (webParams.eventStr && ((_IDispatchEx *)This)->id >= 0) GlobalFree((void *)webParams.eventStr);

			// If this was the "beforeunload" event, detach this IDispatch from that event for its web page element.
			// This should also cause the IE engine to call this IDispatch's Dispatch_Release().
			if (!webParams.nmhdr.code) webDetach((_IDispatchEx *)This);
		}

		// Release the IHTMLEventObj.
		webParams.htmlEvent->lpVtbl->Release(webParams.htmlEvent);
	}

	return(S_OK);
}




/*
			navigator->lpVtbl->get_appVersion(navigator, &result);
			cstr_size = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, 0, 0, 0, 0);
			if (cstr = (char*)malloc(sizeof(char) * cstr_size)) 
				WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, cstr, cstr_size, 0, 0);
			SysFreeString(result);
			triviaPrintf("WebBrowser:IEVersion", "%s", cstr);
			free(cstr);
			
			navigator->lpVtbl->get_browserLanguage(navigator, &result);
			cstr_size = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, 0, 0, 0, 0);
			if (cstr = (char*)malloc(sizeof(char) * cstr_size)) 
				WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, cstr, cstr_size, 0, 0);
			SysFreeString(result);
			triviaPrintf("WebBrowser:BrowserLanguage", "%s", cstr);
			free(cstr);

			navigator->lpVtbl->get_systemLanguage(navigator, &result);
			cstr_size = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, 0, 0, 0, 0);
			if (cstr = (char*)malloc(sizeof(char) * cstr_size)) 
				WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, cstr, cstr_size, 0, 0);
			SysFreeString(result);
			triviaPrintf("WebBrowser:SystemLanguage", "%s", cstr);
			free(cstr);

			navigator->lpVtbl->get_userLanguage(navigator, &result);
			cstr_size = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, 0, 0, 0, 0);
			if (cstr = (char*)malloc(sizeof(char) * cstr_size)) 
			WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, cstr, cstr_size, 0, 0);
			SysFreeString(result);
			triviaPrintf("WebBrowser:UserLanguage", "%s", cstr);
			free(cstr); */

void extractTriviaData(IOmNavigator *navigator, HRESULT (_stdcall *getter) (IOmNavigator*, BSTR*), char *key)
{
	BSTR result;
	char *cstr;
	int cstr_size;

	if (SUCCEEDED(getter(navigator, &result))) {
		cstr_size = WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, 0, 0, 0, 0);
		cstr = (char*)malloc(sizeof(char) * cstr_size);
		WideCharToMultiByte(CP_ACP, 0, (WCHAR *)result, -1, cstr, cstr_size, 0, 0);
		triviaPrintf(key, "%s", cstr);
		free(cstr);
		SysFreeString(result);
	}
}


void RecordIETriviaData(IHTMLDocument2 * htmlDoc2)
{
	IHTMLWindow2 * htmlWindow2;
	IOmNavigator * navigator;
	VARIANT_BOOL b;

	if (SUCCEEDED(htmlDoc2->lpVtbl->get_parentWindow(htmlDoc2, &htmlWindow2)) && htmlWindow2)
	{
		if (SUCCEEDED(htmlWindow2->lpVtbl->get_navigator(htmlWindow2, &navigator)) && navigator)
		{
			extractTriviaData(navigator, navigator->lpVtbl->get_appVersion, "WebBrowser:IEVersion");
			extractTriviaData(navigator, navigator->lpVtbl->get_browserLanguage, "WebBrowser:BrowserLanguage");
			extractTriviaData(navigator, navigator->lpVtbl->get_systemLanguage, "WebBrowser:SystemLanguage");
			extractTriviaData(navigator, navigator->lpVtbl->get_userLanguage, "WebBrowser:UserLanguage");

			navigator->lpVtbl->get_cookieEnabled(navigator, &b);
			triviaPrintf("WebBrowser:CookiesEnabled", "%d", (b == VARIANT_FALSE) ? 0 : 1);

			navigator->lpVtbl->javaEnabled(navigator, &b);
			triviaPrintf("WebBrowser:JavaEnabled", "%d", (b == VARIANT_FALSE) ? 0 : 1);

			navigator->lpVtbl->Release(navigator);
		}
		htmlWindow2->lpVtbl->Release(htmlWindow2);
	}
}





/************************ CreateWebEvtHandler() *********************
* Creates an event handler, to be used to "attach to" some events that
* happpen with an element on the web page.
*
* hwnd - The window where messages will be sent when the event happens.
*
* htmlDoc2 - Pointer to an IHTMLDocument2 object. Objects that use
*			  the resulting event handler must be associated with this
*			  (ie. either its parent window, itself or a child element).
*
* extraData -	sizeof() any application defined struct you wish
*				prepended to the returned IDispatch. You can use
*				GetWebExtraData() to fetch a pointer to this struct.
*				0 if no extra data needed.
*
* id -		Any numeric value of your choosing to be stored in the
*			_IDispatchEx's "id" member. If a negative value, then the
*			WEBPARAMS->eventStr will be set to the passed USERDATA
*			instead of an event string.
*
* obj -	A pointer to any other object to be stored in the _IDispatchEx's
*			"object" member. 0 if none.
*
* userdata -	If not zero, then this will be stored in the _IDispatchEx's
*				"userdata" member.
*
* attachObj -	If not zero, then "userdata" is considered to be a BSTR of
*				some event type to attach to, and "attachObj" is the
*				
*
* RETURNS: Pointer to the IDispatch if success, or 0 if an error.
*
* NOTE: "elem" will automatically be Release()'ed by this DLL when the
* _IDispatchEx is destroyed. It is also Release()'ed if this call fails.
*/

IDispatch * CreateWebEvtHandler(SimpleWindow *window, IHTMLDocument2 * htmlDoc2, DWORD extraData, long id, IUnknown *obj, void *userdata)
{
	HWND            hwnd = window->hWnd;
	_IDispatchEx *	lpDispatchEx;
	IHTMLWindow2 *	htmlWindow2;
	IHTMLWindow3 *	htmlWindow3;
	VARIANT			varDisp;
	//VARIANT_BOOL    varResult;

	// Get the IHTMLWindow2. Our IDispatch's Invoke() will need it to cleanup.
	if (SUCCEEDED(htmlDoc2->lpVtbl->get_parentWindow(htmlDoc2, &htmlWindow2)) && htmlWindow2)
	{
		VariantInit(&varDisp);

		// If he didn't pass any userdata, then we don't need the extra "userdata" member
		// on the IDispatch.
		varDisp.vt = 0;
		if (!userdata && id >= 0) varDisp.vt -= sizeof(void *);

		// Create an IDispatch object (actually we create one of our own _IDispatchEx objects)
		// which we'll use to monitor "events" that occur to an element on a web page.
		// IE's engine will call our IDispatch's Invoke() function when it wants to inform
		// us that a specific event has occurred.
		if ((lpDispatchEx = (_IDispatchEx *)GlobalAlloc(GMEM_FIXED, sizeof(_IDispatchEx) + extraData + varDisp.vt+1000000)))
		{
			// Clear out the extradata area in case the caller wants that.
			ZeroMemory(lpDispatchEx, extraData);
			ZeroMemory(lpDispatchEx, sizeof(_IDispatchEx) + extraData + varDisp.vt+1000000);

			// Skip past the extradata.
			lpDispatchEx = (_IDispatchEx *)((char *)lpDispatchEx + extraData);	

			// Fill in our _IDispatchEx with its VTable, and the args passed to us by the caller
			lpDispatchEx->dispatchObj.lpVtbl = &MyIDispatchVtbl;
			lpDispatchEx->hwnd = hwnd;
			lpDispatchEx->htmlWindow2 = htmlWindow2;
			lpDispatchEx->id = (short)id;
			lpDispatchEx->extraSize = (unsigned short)extraData;
			lpDispatchEx->object = obj;
			if (userdata) lpDispatchEx->userdata = userdata;

			// No one has yet called its Dispatch_AddRef(). That won't happen until we
			// attach some event to it, such as below.
			lpDispatchEx->refCount = 0;

			// Now we attach our IDispatch to the "beforeunload" event so that our IDispatch's
			// Invoke() is called when the browser fires off this event.

			// We need to get the IHTMLWindow3 object (so we can call its attachEvent() and pass it
			// our IDispatch wrapped in a VARIANT).
			if (SUCCEEDED(htmlWindow2->lpVtbl->QueryInterface(htmlWindow2, (GUID *)&_IID_IHTMLWindow3[0], (void **)&htmlWindow3)) && htmlWindow3)
			{
				varDisp.vt = VT_DISPATCH;
				varDisp.pdispVal = (IDispatch *)lpDispatchEx;	
				
				//if (SUCCEEDED(htmlWindow3->lpVtbl->attachEvent(htmlWindow3, OnBeforeUnload, (LPDISPATCH)lpDispatchEx, &varResult)) && varResult == VARIANT_TRUE)
				if(SUCCEEDED(htmlWindow2->lpVtbl->put_onbeforeunload(htmlWindow2, varDisp)))
				{
					// Does the caller want us to consider the "userdata" arg as a BSTR of some other
					// event to attach this IDispatch to?
#if 0
					VARIANT_BOOL	varResult;

					if (userdata && id < 0)
					{
						attachObj->lpVtbl->attachEvent(attachObj, (BSTR)userdata, (LPDISPATCH)lpDispatchEx, &varResult);
						if (!varResult)
						{
							// ERROR: Detach the "beforeunload" event handler we just attached above.
							htmlWindow3->lpVtbl->detachEvent(htmlWindow3, OnBeforeOnLoad, (LPDISPATCH)lpDispatchEx);

							// Fail.
							goto bad;
						}
					}
#endif
					// Release() the IHTMLWindow3 object now that we called its attachEvent().
					htmlWindow3->lpVtbl->Release(htmlWindow3);

					// Return the IDispatch, so the app can use it to attach some other events to the
					// same element on the web page.
					//
					// NOTE: We don't Release() the IHTMLWindow2 object. We stored that pointer
					// in our _IDispatchEx and its Invoke() needs it. webDetach() will
					// Release() it.
					return((IDispatch *)lpDispatchEx);
				}

				// An error. Free all stuff above.
    			htmlWindow3->lpVtbl->Release(htmlWindow3);
			}

			GlobalFree(((char *)lpDispatchEx - lpDispatchEx->extraSize));
		}

		htmlWindow2->lpVtbl->Release(htmlWindow2);
	}

	// Release whatever the app passed, so it doesn't need to do that in case of an error.
	if (obj) obj->lpVtbl->Release(obj);

	// FAILURE.
	return(0);
}





/********************** FreeWebEvtHandler() *********************
* Called to detach our _IDispatchEx from the "onbeforeunload" event
* (ie, it was attached in CreateWebEvtHandler(). Also frees the
* IHTMLWindow2 that CreateWebEvtHandler() got.
*
* NOTE: FreeWebEvtHandler() must be called only if
* CreateWebEvtHandler() succeeds, and you wish to later manually free
* that event handler before loading a web page. Typically, this needs
* to be done only if you have a failure while setting up for an event
* handler.
*
* If the caller has other events still attached to this same
* _IDispatchEx, then it won't really be expunged from memory. So,
* the caller should ensure that it detaches all events it attached.
*/

void FreeWebEvtHandler(IDispatch *lpDispatch)
{
	webDetach((_IDispatchEx *)lpDispatch);
}


//////////////////////////////////////////////////////////////////////////
//

void EventHandler_BeforeNavigate2(SimpleWindow *window, IDispatch *pDisp, VARIANT *url, VARIANT *Flags, VARIANT *TargetFrameName, VARIANT *PostData, VARIANT *Headers, VARIANT_BOOL *Cancel) 
{
	//CrypticLauncherWindow *launcher = (CrypticLauncherWindow*)window->pUserData;
#if 0
	if(Headers && Headers->bstrVal)
	{
		char *h = BStr2TStr(window, Headers->bstrVal), *p;
		printf("%s", h);
		p = strstri(h, "en-us");
		if(p)
		{
			p[0] = 'f';
			p[1] = 'r';
			p[3] = 'f';
			p[4] = 'r';
			SysFreeString(Headers->bstrVal);
			Headers->bstrVal = TStr2BStr(window, h);
		}
		GlobalFree(h);
	}
#endif
}

void EventHandler_DocumentComplete(SimpleWindow *window, IDispatch *pDisp, VARIANT *URL)
{
	char *curl = BStr2TStr(window, URL ? URL->bstrVal : L""), *p;
	// Check the URL starts with "http://" and that the path starts with "launcher"
	if(strStartsWith(curl, "http://") && (p = strchr(curl+7, '/')) && strstri(p+1, "launcher"))
		SendMessage(window->hWnd, WM_APP, CLMSG_PAGE_LOADED, 0);
	else if(strStartsWith(curl, "https://") && (p = strchr(curl+8, '/')) && strstri(p+1, "launcher"))
		SendMessage(window->hWnd, WM_APP, CLMSG_PAGE_LOADED, 0);
	GlobalFree(curl);
}



//////////////////////////////////////////////////////////////////////////
// Implementation of IDispatch for event handler

HRESULT STDMETHODCALLTYPE Events_QueryInterface(DWebBrowserEvents2 * This, REFIID riid, void **ppvObject)
{
	*ppvObject = 0;

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IDispatch, sizeof(GUID)))
	{
		*ppvObject = (void *)This;

		// Increment its usage count. The caller will be expected to call our
		// IDispatch's Release() (ie, Dispatch_Release) when it's done with
		// our IDispatch.
		Events_AddRef(This);

		return(S_OK);
	}

	*ppvObject = 0;
	return(E_NOINTERFACE);
}

HRESULT STDMETHODCALLTYPE Events_AddRef(DWebBrowserEvents2 *This)
{
	return(InterlockedIncrement(&((_DWebBrowserEvents2Ex *)This)->refCount));
}

HRESULT STDMETHODCALLTYPE Events_Release(DWebBrowserEvents2 *This)
{
	if (InterlockedDecrement( &((_DWebBrowserEvents2Ex *)This)->refCount ) == 0)
	{
		/* If you uncomment the following line you should get one message
		* when the document unloads for each successful call to
		* CreateEventHandler. If not, check you are setting all events
		* (that you set), to null or detaching them.
		*/
		// OutputDebugString("One event handler destroyed");

		//GlobalFree(((char *)This - ((_DWebBrowserEvents2Ex *)This)->extraSize));
		return(0);
	}

	return(((_DWebBrowserEvents2Ex *)This)->refCount);
}

HRESULT STDMETHODCALLTYPE Events_GetTypeInfoCount(DWebBrowserEvents2 *This, unsigned int *pctinfo)
{
	return(E_NOTIMPL);
}

HRESULT STDMETHODCALLTYPE Events_GetTypeInfo(DWebBrowserEvents2 *This, unsigned int iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
	return(E_NOTIMPL);
}

HRESULT STDMETHODCALLTYPE Events_GetIDsOfNames(DWebBrowserEvents2 *This, REFIID riid, OLECHAR ** rgszNames, unsigned int cNames, LCID lcid, DISPID * rgDispId)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Events_Invoke(DWebBrowserEvents2 *This, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS * pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, unsigned int *puArgErr)
{
	// This function is called to recevie an event. The event is identified by the
	// dispIdMember argument. It is our responsibility to retrieve the event arguments
	// from the pDispParams->rgvarg array and call the event function.
	// If we do not handle an event we can return DISP_E_MEMBERNOTFOUND.
	// The variant member that we use for each argument is determined by the argument
	// type of the event function. eg. If an event has the argument long x we would
	// use the lVal member of the VARIANT struct.
	SimpleWindow *window = ((_DWebBrowserEvents2Ex*)This)->window;

	// Here is our message map, where we map dispids to function calls.
	switch (dispIdMember) {
	case DISPID_BEFORENAVIGATE2:
		// call BeforeNavigate
		// (parameters are on stack, thus in reverse order)
		EventHandler_BeforeNavigate2(window,
			            pDispParams->rgvarg[6].pdispVal,    // pDisp
			            pDispParams->rgvarg[5].pvarVal,     // url
			            pDispParams->rgvarg[4].pvarVal,     // Flags
			            pDispParams->rgvarg[3].pvarVal,     // TargetFrameName
			            pDispParams->rgvarg[2].pvarVal,     // PostData
			            pDispParams->rgvarg[1].pvarVal,     // Headers
			            pDispParams->rgvarg[0].pboolVal);   // Cancel
		break;
	case DISPID_DOCUMENTCOMPLETE:
		EventHandler_DocumentComplete(window, pDispParams->rgvarg[1].pdispVal, pDispParams->rgvarg[0].pvarVal);
		break;
	}

	return(S_OK);
}


/************************** GetWebSrcElement() **********************
* Retrieves the IHTMLElement of some event object. The returned
* IHTMLElement's QueryInterface() function can be called to
* obtain other objects such as IHTMLInputElement, etc.
* 
* Returns: Pointer to the IHTMLElement object, or 0 if an error.
*/

IHTMLElement * GetWebSrcElement(IHTMLEventObj *htmlEvent)
{
	IHTMLElement *htmlElement;

	htmlElement = 0;
	htmlEvent->lpVtbl->get_srcElement(htmlEvent, &htmlElement);
	return(htmlElement);
}





/************************** SetWebReturnValue() **********************
* Sets the return value (to some IE Object function) to either
* true or false. Some events (such as onsubmit) can return false
* to be cancelled.
*/

HRESULT SetWebReturnValue(IHTMLEventObj *htmlEvent, BOOL returnVal)
{
	VARIANT	varResult;

	varResult.vt = VT_BOOL;

	if (returnVal) varResult.boolVal = (VARIANT_BOOL)-1;
	else varResult.boolVal = (VARIANT_BOOL)0;

	return(htmlEvent->lpVtbl->put_returnValue(htmlEvent, varResult));
}





/************************** GetWebPtrs() **********************
* Fetches pointers to the IWebBrowser2 and/or the IHTMLDocument2
* objects for the browser object associated with the passed window.
*
* hwnd = Handle of the window containing the browser control.
*
* webBrowser2Result =	Where to store a pointer to the IWebBrowser2
*						object, or null if not desired.
*
* htmlDoc2Result =		Where to store a pointer to the IHTMLDocument2
*						object, or null if not desired.
*
* RETURNS: S_OK (0) for success and E_FAIL for failure.
*
* Note: It is the responsibility of the caller to Release() the
* objects.
*
* Note: This will fail to return a IHTMLDocument2 interface in the
* circumstance when a document is being loaded in the browser and its
* readystate is below READYSTATE_LOADED(2). To get around this, always
* use WaitOnReadyState before requesting the document interface.
*/

HRESULT GetWebPtrs(SimpleWindow *window, IWebBrowser2 **webBrowser2Result, IHTMLDocument2 **htmlDoc2Result)
{
	HWND            hwnd = window->hWnd;
	IOleObject		*browserObject;
	IWebBrowser2	*webBrowser2;

	// Make sure he supplied at least one of the return handles. If not, we
	// have nothing to do here
	if (webBrowser2Result || htmlDoc2Result)
	{
		// Make sure he supplied a window
		if (!IsWindow(hwnd) ||

			// Get the browser object stored in the window's USERDATA member
			!(browserObject = *(((CrypticLauncherWindow*)window->pUserData)->browserPtr)) ||

			// Get the IWebBrowser2 object embedded within the browser object
			FAILED(browserObject->lpVtbl->QueryInterface(browserObject, &IID_IWebBrowser2, (void **)&webBrowser2)))
		{
			goto fail;
		}

		// Now the pointer to our IWebBrowser2 object is in 'webBrowser2', and so its VTable is
		// webBrowser2->lpVtbl.

		// Does the caller want the IHTMLDocument2 object for the browser?
		if (htmlDoc2Result)
		{
			LPDISPATCH		lpDispatch = 0;

			// Set his handle to 0 initially
			*htmlDoc2Result = (struct IHTMLDocument2 *)lpDispatch;

			// First, we need the IDispatch object
			webBrowser2->lpVtbl->get_Document(webBrowser2, &lpDispatch);
			if (lpDispatch)
			{
				// Get the IHTMLDocument2 object embedded within the IDispatch object
				lpDispatch->lpVtbl->QueryInterface(lpDispatch, &IID_IHTMLDocument2, (void **)htmlDoc2Result);

				// Release the IDispatch object now that we have the IHTMLDocument2
				lpDispatch->lpVtbl->Release(lpDispatch);
			}

			// If we failed to get IHTMLDocument2, then free the IWebBrowser2 and
			// return an error to the caller
			if (!(*htmlDoc2Result))
			{
				webBrowser2->lpVtbl->Release(webBrowser2);
fail:			return(E_FAIL);
			}
		}

		// If the caller wants the IWebBrowser2 returned, store it in his supplied handle.
		// Note: We do not Release it here. The caller must do that when he is done with it
		if (webBrowser2Result)
			*webBrowser2Result = webBrowser2;

		// If he doesn't want it returned, we need to release it here
		else
			webBrowser2->lpVtbl->Release(webBrowser2);
	}

	return(S_OK);
}





/************************** TStr2BStr() *************************
* Creates a BSTR from the passed nul-terminated string (ANSI or
* UNICODE).
*
* NOTE: The caller must SysFreeString() the returned BSTR when
* done with it.
*
* RETURNS: Pointer to the BSTR, or 0 if failure.
*/


BSTR TStr2BStr(SimpleWindow *window, const char *string)
{
	BSTR	bstr;

	if (!IsWindowUnicode(window->hWnd))
	{
		WCHAR		*buffer;
		DWORD		size;

		size = MultiByteToWideChar(CP_UTF8, 0, (char *)string, -1, 0, 0);
		if (!(buffer = (WCHAR *)GlobalAlloc(GMEM_FIXED, sizeof(WCHAR) * size))) return(0);
		MultiByteToWideChar(CP_UTF8, 0, (char *)string, -1, buffer, size);
		bstr = SysAllocString(buffer);
		GlobalFree(buffer);
	}
	else
		bstr = SysAllocString((WCHAR *)string);

	return(bstr);
}





/************************** BStr2TStr() *************************
* Creates a nul-terminated string (ANSI or UNICODE) from the
* passed BSTR.
*
* NOTE: The caller must GlobalFree() the returned string when
* done with it.
*
* RETURNS: Pointer to the string, or 0 if an error.
*/

void * BStr2TStr(SimpleWindow *window, BSTR strIn)
{
	DWORD	size;
	void	*strOut;

	if (!IsWindowUnicode(window->hWnd))
	{
		size = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)strIn), -1, 0, 0, 0, 0);
		if ((strOut = GlobalAlloc(GMEM_FIXED, size)))
			WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)strIn), -1, (char *)strOut, size, 0, 0);
	}
	else
	{
		size = (*((short *)strIn) + 1) * sizeof(wchar_t);
		if ((strOut = GlobalAlloc(GMEM_FIXED, size)))
			CopyMemory(strOut, (char *)strIn + 2, size);
	}

	return(strOut);
}






/*************************** GetWebElement() **************************
* Retrieves a pointer to the IHTMLElement object for the specified
* element on a web page (such as a FORM).
*
* hwnd =	Handle of the window hosting the browser.
*
* htmlDoc2  = A pointer to the IHTMLDocument2 object for the web page.
*			Could be 0 if this ptr has not yet been gotten.
*
* name =	The name of an element specified by the HTML attributes name
*			or id.
*
* nIndex = A zero based index that specifies which element to return
*			if there is more than element with the name specified by
*			name. If not, pass 0.
*
* RETURNS: A pointer to the IHTMLElement object or 0 if a failure.
*
* NOTE: The QueryInterface() of the returned IHTMLElement can be used
* to get other objects associated with that element on the web page,
* such as IHTMLInputElement.
*/

IHTMLElement * GetWebElement(SimpleWindow *window, IHTMLDocument2 *htmlDoc2, const char *name, INT nIndex, char **errorString)
{
	HWND                        hwnd = window->hWnd;
	IHTMLElementCollection *	htmlCollection;
	IHTMLElement *				htmlElem = 0;
	LPDISPATCH					lpDispatch;

	lpDispatch = (LPDISPATCH)htmlElem;

	// Get the browser's IHTMLDocument2 object if it wasn't passed
	if (htmlDoc2 || SUCCEEDED(GetWebPtrs(window, 0, &htmlDoc2)))
	{
		// Get the IHTMLElementCollection object. We need this to get the IDispatch
		// object for the element the caller wants on the web page. And from that
		// IDispatch, we get the IHTMLElement object. Really roundabout, ain't it?
		// That's COM
		if (SUCCEEDED(htmlDoc2->lpVtbl->get_all(htmlDoc2, &htmlCollection)) && htmlCollection)
		{
			VARIANT			varName;
			VARIANT			varIndex;

			// Get the IDispatch for that element. We need to call the IHTMLElementCollection
			// object's item() function, passing it the name of the element. Note that we
			// have to pass the element name as a BSTR stuffed into a VARIENT struct. And
			// we also need to stuff the index into a VARIENT struct too.
			VariantInit(&varName);
			varName.vt = VT_BSTR;
			if ((varName.bstrVal = TStr2BStr(window, name)))
			{
				HRESULT rv;
				VariantInit(&varIndex);
				varIndex.vt = VT_I4;
				varIndex.lVal = nIndex;

				rv = htmlCollection->lpVtbl->item(htmlCollection, varName, varIndex, &lpDispatch);
#if 1
				if(errorString && (FAILED(rv) || !lpDispatch))
				{
					long i, items;
					estrPrintf(errorString, "Cannot find element %s in collection:\n", name);
					htmlCollection->lpVtbl->get_length(htmlCollection, &items);
					for(i=0; i<items; ++i)
					{
						varIndex.lVal = i;
						if(SUCCEEDED(htmlCollection->lpVtbl->item(htmlCollection, varIndex, varIndex, &lpDispatch)) && lpDispatch)
						{
							char *cstr;
							BSTR bstr = NULL;
							lpDispatch->lpVtbl->QueryInterface(lpDispatch, &IID_IHTMLElement, (void **)&htmlElem);
							lpDispatch->lpVtbl->Release(lpDispatch);
							htmlElem->lpVtbl->get_outerHTML(htmlElem, &bstr);
							if(bstr)
							{
								cstr = BStr2TStr(window, bstr);
								estrConcatf(errorString, "%s,\n", cstr);
								GlobalFree(cstr);
							}
							else
							{
								estrConcatf(errorString, "err no outerhtml,\n");
							}
						}
					}
				}
#endif

				// We don't need the VARIENTs anymore. This frees the string that SysAllocString() gave us
				VariantClear(&varName);
				VariantClear(&varIndex);
			}

			// Release the IHTMLElementCollection now that we're done with it.
			htmlCollection->lpVtbl->Release(htmlCollection);

			// Did we get the IDispatch for that element?
			if (lpDispatch)
			{
				// We can finally get the IHTMLElement object for the desired object.
				lpDispatch->lpVtbl->QueryInterface(lpDispatch, &IID_IHTMLElement, (void **)&htmlElem);

				// Release the IDispatch now that we got the IHTMLElement.
				lpDispatch->lpVtbl->Release(lpDispatch);
			}
		}
	}

	// IE seems to hand back the last element on the page if none match the requested ID. Check we got what we asked for.
	if(htmlElem)
	{
		BSTR id;
		char *id_tstr = NULL;
		htmlElem->lpVtbl->get_id(htmlElem, &id);
		if(id)
			id_tstr = BStr2TStr(window, id);
		if(!id_tstr || stricmp(id_tstr, name) != 0)
		{
			// Mismatch, I hate IE for thinking it should do this.
			htmlElem->lpVtbl->Release(htmlElem);
			htmlElem = NULL;
		}
		if(id_tstr)
			GlobalFree(id_tstr);
	}

	// Return the IHTMLElement ptr.
	return(htmlElem);
}





/************************** doEvents() ***********************
* Pumps available messages in the thread's message queue.
*
* Note: Any windows may be destroyed within doEvents, so if a
* caller has a handle to any window, that handle should be
* checked with IsWindow() upon return.
*/

static void doEvents(HWND hwnd)
{
	MSG		msg;

	while (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg); 
		DispatchMessage(&msg);
	}
}





/************************** WaitOnReadyState() **********************
* Waits for a page (that is currently loading into the browser (for
* the specified window) to be in specified state.
*
* hwnd =	Handle to the window hosting the browser object.
*
* rs =		The desired readystate to wait for. A list of ready states can
*			be found at:
* http://msdn.microsoft.com/library/default.asp?url=/workshop/browser/webbrowser/reference/enums/readystate_enum.asp
*
* timeout = How long to wait for the state to match 'rs'. 0 for no wait.
*
* webBrowser2 =	A pointer to the IWebBrowser2 for the browser control,
*					or null if not supplied.
*
* RETURNS: WORS_SUCCESS (0) if the loading page has achieved the specified
* state by the time WaitOnReadyState returns, WORS_TIMEOUT (-1) for timeout
* and WORS_DESTROYED (-2) if the window was destroyed or a webBrowser
* object could not be obtained from the window.
*/

HRESULT WaitOnReadyState(SimpleWindow *window, READYSTATE rs, DWORD timeout, IWebBrowser2 *webBrowser2)
{
	HWND            hwnd = window->hWnd;
	READYSTATE		rsi;
	DWORD			dwStart;
	unsigned char	releaseOnComplete;

	// If the caller didn't supply the IWebBrowser2, then we need to get it (and
	// release it when we're done)
	releaseOnComplete = 0;
	if (!webBrowser2)
	{
		if (FAILED(GetWebPtrs(window, &webBrowser2, 0))) goto destroyed;
		releaseOnComplete = 1;
	}

	// Get the current ready state of the loading page
	webBrowser2->lpVtbl->get_ReadyState(webBrowser2, &rsi);

	// Is the current ready state at least as high as the caller needs?
	if (rsi >= rs)
	{
		// Yes, it is. Tell the caller that the page is in a state where
		// he can proceed with whatever he wants to do.
yes:	rs = WORS_SUCCESS;

		// If we got the IWebBrowser2 ourselves above, release it now.
out:	if (releaseOnComplete) webBrowser2->lpVtbl->Release(webBrowser2);

		return((HRESULT)rs);
	}

	// Ok, the loading page is not yet in the state that the caller
	// requires. We need to timeout. We can't just Sleep() for the
	// specified amount of time. Why? Because a page will not load
	// unless we are emptying out certain messages in our thread's
	// message queue. So we need to at least call doEvents() periodically
	// while we are waiting for the ready state to be achieved.
	dwStart = GetTickCount();
	do
	{
		// Empty out messages in the message queue.
		doEvents(hwnd);

		// Make sure our window with the browser object wasn't closed down in while processing messages.
		if (!IsWindow(hwnd))
		{
			// Oops! It was. Get out of here with WORS_DESTROYED.
destroyed:	rs = WORS_DESTROYED;
			goto out;
		}

		// Is the current ready state at least as high as the caller needs?
		webBrowser2->lpVtbl->get_ReadyState(webBrowser2, &rsi);
		if (rsi >= rs) goto yes;

		// We may need a sleep here on Win9x/Me systems to avoid a system hang.
		Sleep(10);

		// Did we timeout yet?
	} while (!timeout || (GetTickCount() - dwStart) <= timeout);

	// We timed out before the page achieved the desired ready state.
	rs = WORS_TIMEOUT;
	goto out;
}






/*************************** UnEmbedBrowserObject() ************************
* Called to detach the browser object from our host window, and free its
* resources, right before we destroy our window.
*
* hwnd =		Handle to the window hosting the browser object.
*
* NOTE: The pointer to the browser object must have been stored in the
* window's USERDATA member. In other words, don't call UnEmbedBrowserObject().
* with a HWND that wasn't successfully passed to EmbedBrowserObject().
*/

void UnEmbedBrowserObject(SimpleWindow *window)
{
	IOleObject	**browserHandle;
	IOleObject	*browserObject;

	// Retrieve the browser object's pointer we stored in our window's GWL_USERDATA when we
	// initially attached the browser object to this window. If 0, you may have called this
	// for a window that wasn't successfully passed to EmbedBrowserObject(). Bad boy! Or, we
	// may have failed the EmbedBrowserObject() call in WM_CREATE, in which case, our window
	// may get a WM_DESTROY which could call this a second time (ie, since we may call
	// UnEmbedBrowserObject in EmbedBrowserObject).
	if ((browserHandle = ((CrypticLauncherWindow*)window->pUserData)->browserPtr))
	{
		// Unembed the browser object, and release its resources.
		browserObject = *browserHandle;
		browserObject->lpVtbl->Close(browserObject, OLECLOSE_NOSAVE);
		browserObject->lpVtbl->Release(browserObject);

		// Zero out the pointer just in case UnEmbedBrowserObject is called again for this window.
		((CrypticLauncherWindow*)window->pUserData)->browserPtr = NULL;
	}
}





/******************************* DisplayHTMLStr() ****************************
* Takes a string containing some HTML BODY, and displays it in the specified
* window. For example, perhaps you want to display the HTML text of...
*
* <P>This is a picture.<P><IMG src="mypic.jpg">
*
* hwnd =		Handle to the window hosting the browser object.
* string =		Pointer to nul-terminated string containing the HTML BODY.
*				(NOTE: No <BODY></BODY> tags are required in the string).
* flag =		1 if "string" is ANSI, or 0 if UNICODE.
*
* RETURNS: 0 if success, or non-zero if an error.
*
* NOTE: EmbedBrowserObject() must have been successfully called once with the
* specified window, prior to calling this function. You need call
* EmbedBrowserObject() once only, and then you can make multiple calls to
* this function to display numerous pages in the specified window.
*/

long DisplayHTMLStr(SimpleWindow *window, const char *string)
{	
	HWND            hwnd = window->hWnd;
	IHTMLDocument2	*htmlDoc2;
	IWebBrowser2	*webBrowser2;
	SAFEARRAY		*sfArray;
	VARIANT			myURL;
	VARIANT			*pVar;

	VariantInit(&myURL);
	myURL.vt = VT_BSTR;

	// Get the browser's IWebBrowser2 object.
	if (SUCCEEDED(GetWebPtrs(window, &webBrowser2, 0)))
	{
		// Ok, now the pointer to our IWebBrowser2 object is in 'webBrowser2', and so its VTable is
		// webBrowser2->lpVtbl.

		// Before we can get the IHTMLDocument2, we actually need to have some HTML page loaded in
		// the browser. So, let's load an empty HTML page. Then, once we have that empty page, we
		// can get that IHTMLDocument2 and call its write() to stuff our HTML string into it.

		// Call the IWebBrowser2 object's get_Document so we can get its DISPATCH object. I don't know why you
		// don't get the DISPATCH object via the browser object's QueryInterface(), but you don't.

		// Give a URL that causes the browser to display an empty page.
		myURL.bstrVal = SysAllocString(&Blank[0]);

		// Call the Navigate2() function to actually display the page.
		webBrowser2->lpVtbl->Navigate2(webBrowser2, &myURL, 0, 0, 0, 0);

		// Wait for blank page to finish loading
		if ((int)WaitOnReadyState(window, READYSTATE_COMPLETE, 1000, webBrowser2) != WORS_DESTROYED)
		{
			SysFreeString(myURL.bstrVal);

			// Get the browser's IHTMLDocument2 object.
			if (SUCCEEDED(GetWebPtrs(window, 0, &htmlDoc2)))
			{
				// Ok, now the pointer to our IHTMLDocument2 object is in 'htmlDoc2', and so its VTable is
				// htmlDoc2->lpVtbl.

				// Our HTML must be in the form of a BSTR. And it must be passed to write() in an
				// array of "VARIENT" structs. So let's create all that.
				if ((sfArray = SafeArrayCreate(VT_VARIANT, 1, (SAFEARRAYBOUND *)&ArrayBound)))
				{
					if (SUCCEEDED(SafeArrayAccessData(sfArray, (void **)&pVar)))
					{
						pVar->vt = VT_BSTR;

						// Store our BSTR pointer in the VARIENT.
						if ((pVar->bstrVal = TStr2BStr(window, string)))
						{
							// Pass the VARIENT with its BSTR to write() in order to shove our desired HTML string
							// into the body of that empty page we created above.
							htmlDoc2->lpVtbl->write(htmlDoc2, sfArray);

							// Close the document. If we don't do this, subsequent calls to DisplayHTMLStr
							// would append to the current contents of the page
							htmlDoc2->lpVtbl->close(htmlDoc2);

							// Success. Just set this to something other than VT_BSTR to flag success
							++myURL.vt;

							// Normally, we'd need to free our BSTR, but SafeArrayDestroy() does it for us
							//						SysFreeString(pVar->bstrVal);
						}

						// Free the array. This also frees the VARIENT that SafeArrayAccessData created for us,
						// and even frees the BSTR we allocated with SysAllocString
						SafeArrayDestroy(sfArray);
					}
				}

				// Release the IHTMLDocument2 object.
				htmlDoc2->lpVtbl->Release(htmlDoc2);
			}
		}
		else
			SysFreeString(myURL.bstrVal);

		// Release the IWebBrowser2 object.
		webBrowser2->lpVtbl->Release(webBrowser2);
	}

	// No error?
	if (myURL.vt != VT_BSTR) return(0);

	// An error
	return(-1);
}





/***************************** DisplayHTMLPage() **************************
* Displays a URL, or HTML file on disk.
*
* hwnd =			Handle to the window hosting the browser object.
* webPageName =	Pointer to nul-terminated name of the URL/file.
*
* RETURNS: 0 if success, or non-zero if an error.
*
* NOTE: EmbedBrowserObject() must have been successfully called once with the
* specified window, prior to calling this function. You need call
* EmbedBrowserObject() once only, and then you can make multiple calls to
* this function to display numerous pages in the specified window.
*/

long DisplayHTMLPage(SimpleWindow *window, const char *webPageName, const char *postData, const char *lang)
{
	HWND            hwnd = window->hWnd;
	IWebBrowser2	*webBrowser2;
	VARIANT			myURL, varFlags, varPostData, varHeaders;
	SAFEARRAY       *saPostData;
	SAFEARRAYBOUND  sabPostData[1];
	char HUGEP *arrPostData;
	char *headers = NULL;

	// Get the browser's IWebBrowser2 object.
	if (SUCCEEDED(GetWebPtrs(window, &webBrowser2, 0)))
	{
		// Ok, now the pointer to our IWebBrowser2 object is in 'webBrowser2', and so its VTable is
		// webBrowser2->lpVtbl.

		// Our URL (ie, web address, such as "http://www.microsoft.com" or an HTM filename on disk
		// such as "c:\myfile.htm") must be passed to the IWebBrowser2's Navigate2() function as a BSTR.
		// A BSTR is like a pascal version of a double-byte character string. In other words, the
		// first unsigned short is a count of how many characters are in the string, and then this
		// is followed by those characters, each expressed as an unsigned short (rather than a
		// char). The string is not nul-terminated. The OS function SysAllocString can allocate and
		// copy a UNICODE C string to a BSTR. Of course, we'll need to free that BSTR after we're done
		// with it. If we're not using UNICODE, we first have to convert to a UNICODE string.
		//
		// What's more, our BSTR needs to be stuffed into a VARIENT struct, and that VARIENT struct is
		// then passed to Navigate2(). Why? The VARIENT struct makes it possible to define generic
		// 'datatypes' that can be used with all languages. Not all languages support things like
		// nul-terminated C strings. So, by using a VARIENT, whose first member tells what sort of
		// data (ie, string, float, etc) is in the VARIENT, COM interfaces can be used by just about
		// any language.
		VariantInit(&varFlags);
		varFlags.vt = VT_I4;
		varFlags.lVal = navNoHistory | navNoReadFromCache | navNoWriteToCache;

		VariantInit(&myURL);
		myURL.vt = VT_BSTR;

		if (!(myURL.bstrVal = TStr2BStr(window, webPageName)))
		{
			webBrowser2->lpVtbl->Release(webBrowser2);
			return(-6);
		}
	
		if(postData)
		{
			sabPostData[0].lLbound = 0;
			sabPostData[0].cElements = (ULONG)strlen(postData);
			saPostData = SafeArrayCreate(VT_UI1, 1, sabPostData);
			assert(saPostData);
			assert(SUCCEEDED(SafeArrayAccessData(saPostData, (void HUGEP **)&arrPostData)));
			memcpy(arrPostData, postData, strlen(postData));
			SafeArrayUnaccessData(saPostData);

			VariantInit(&varPostData);
			varPostData.vt = VT_ARRAY|VT_UI1;
			varPostData.parray = saPostData;

			estrConcatf(&headers, "Content-Type: application/x-www-form-urlencoded\r\n");

		}

		if(lang)
			estrConcatf(&headers, "X-Accept-Language-Cryptic: %s\r\n", lang);

		estrConcatf(&headers, "User-Agent: Mozilla/4.0 (compatible; CrypticLauncher)\r\n");

		if(headers)
		{
			VariantInit(&varHeaders);
			varHeaders.vt = VT_BSTR;

			if (!(varHeaders.bstrVal = TStr2BStr(window, headers)))
			{
				webBrowser2->lpVtbl->Release(webBrowser2);
				return(-6);
			}
		}

		// Call the Navigate2() function to actually display the page.
		webBrowser2->lpVtbl->Navigate2(webBrowser2, &myURL, &varFlags, NULL, postData ? &varPostData : NULL, headers ? &varHeaders : NULL);

		// Free any resources (including the BSTR we allocated above).
		VariantClear(&myURL);
		VariantClear(&varPostData);
		VariantClear(&varHeaders);
		VariantClear(&varFlags);
		estrDestroy(&headers);

		// We no longer need the IWebBrowser2 object (ie, we don't plan to call any more functions in it,
		// so we can release our hold on it). Note that we'll still maintain our hold on the browser
		// object.
		webBrowser2->lpVtbl->Release(webBrowser2);

		// Success
		return(0);
	}

	return(-5);
}





/******************************* DoPageAction() **************************
* Implements the functionality of a "Back". "Forward", "Home", "Search",
* "Refresh", or "Stop" button.
*
* hwnd =		Handle to the window hosting the browser object.
* action =		One of the following:
*				0 = Move back to the previously viewed web page.
*				1 = Move forward to the previously viewed web page.
*				2 = Move to the home page.
*				3 = Search.
*				4 = Refresh the page.
*				5 = Stop the currently loading page.
*
* NOTE: EmbedBrowserObject() must have been successfully called once with the
* specified window, prior to calling this function. You need call
* EmbedBrowserObject() once only, and then you can make multiple calls to
* this function to display numerous pages in the specified window.
*/

void DoPageAction(SimpleWindow *window, DWORD action)
{	
	IWebBrowser2	*webBrowser2;

	// Get the browser's IWebBrowser2 object.
	if (SUCCEEDED(GetWebPtrs(window, &webBrowser2, 0)))
	{
		// Ok, now the pointer to our IWebBrowser2 object is in 'webBrowser2', and so its VTable is
		// webBrowser2->lpVtbl.

		// Call the desired function
		switch (action)
		{
		case WEBPAGE_GOBACK:
			{
				// Call the IWebBrowser2 object's GoBack function.
				webBrowser2->lpVtbl->GoBack(webBrowser2);
				break;
			}

		case WEBPAGE_GOFORWARD:
			{
				// Call the IWebBrowser2 object's GoForward function.
				webBrowser2->lpVtbl->GoForward(webBrowser2);
				break;
			}

		case WEBPAGE_GOHOME:
			{
				// Call the IWebBrowser2 object's GoHome function.
				webBrowser2->lpVtbl->GoHome(webBrowser2);
				break;
			}

		case WEBPAGE_SEARCH:
			{
				// Call the IWebBrowser2 object's GoSearch function.
				webBrowser2->lpVtbl->GoSearch(webBrowser2);
				break;
			}

		case WEBPAGE_REFRESH:
			{
				// Call the IWebBrowser2 object's Refresh function.
				webBrowser2->lpVtbl->Refresh(webBrowser2);
			}

		case WEBPAGE_STOP:
			{
				// Call the IWebBrowser2 object's Stop function.
				webBrowser2->lpVtbl->Stop(webBrowser2);
			}
		}

		// Release the IWebBrowser2 object.
		webBrowser2->lpVtbl->Release(webBrowser2);
	}
}





/******************************* ResizeBrowser() ****************************
* Resizes the browser object for the specified window to the specified
* width and height.
*
* hwnd =			Handle to the window hosting the browser object.
* width =			Width.
* height =			Height.
*
* NOTE: EmbedBrowserObject() must have been successfully called once with the
* specified window, prior to calling this function. You need call
* EmbedBrowserObject() once only, and then you can make multiple calls to
* this function to resize the browser object.
*/

void ResizeBrowser(SimpleWindow *window, DWORD width, DWORD height)
{
	IWebBrowser2	*webBrowser2;

	// Get the browser's IWebBrowser2 object.
	if (SUCCEEDED(GetWebPtrs(window, &webBrowser2, 0)))
	{
		// Ok, now the pointer to our IWebBrowser2 object is in 'webBrowser2', and so its VTable is
		// webBrowser2->lpVtbl.

		// Call are put_Width() and put_Height() to set the new width/height.
		webBrowser2->lpVtbl->put_Width(webBrowser2, width);
		webBrowser2->lpVtbl->put_Height(webBrowser2, height);

		// We no longer need the IWebBrowser2 object (ie, we don't plan to call any more functions in it,
		// so we can release our hold on it). Note that we'll still maintain our hold on the browser
		// object.
		webBrowser2->lpVtbl->Release(webBrowser2);
	}
}





/***************************** EmbedBrowserObject() **************************
* Puts the browser object inside our host window, and save a pointer to this
* window's browser object in the window's GWL_USERDATA member.
*
* hwnd =		Handle of our window into which we embed the browser object.
*
* RETURNS: 0 if success, or non-zero if an error.
*
* NOTE: We tell the browser object to occupy the entire client area of the
* window.
*
* NOTE: No HTML page will be displayed here. We can do that with a subsequent
* call to either DisplayHTMLPage() or DisplayHTMLStr(). This is merely once-only
* initialization for using the browser object. In a nutshell, what we do
* here is get a pointer to the browser object in our window's GWL_USERDATA
* so we can access that object's functions whenever we want, and we also pass
* the browser a pointer to our IOleClientSite struct so that the browser can
* call our functions in our struct's VTable.
*/

long EmbedBrowserObject(SimpleWindow *window)
{
	HRESULT						hr;
	BOOL						success;
	HWND                        hwnd = window->hWnd;
	IOleObject					*browserObject;
	IWebBrowser2				*webBrowser2;
	RECT						rect;
	register char				*ptr;
	register _IOleClientSiteEx	*_iOleClientSiteEx;
	IConnectionPoint           *pConnectionPoint;
	IConnectionPointContainer  *pConnectionPointContainer;
	DWORD                       dwAdviseCookie = 0;


	// Our IOleClientSite, IOleInPlaceSite, and IOleInPlaceFrame functions need to get our window handle. We
	// could store that in some global. But then, that would mean that our functions would work with only that
	// one window. If we want to create multiple windows, each hosting its own browser object (to display its
	// own web page), then we need to create unique IOleClientSite, IOleInPlaceSite, and IOleInPlaceFrame
	// structs for each window. And we'll put an extra member at the end of those structs to store our extra
	// data such as a window handle. So, our functions won't have to touch global data, and can therefore be
	// re-entrant and work with multiple objects/windows.
	//
	// Remember that a pointer to our IOleClientSite we create here will be passed as the first arg to every
	// one of our IOleClientSite functions. Ditto with the IOleInPlaceFrame object we create here, and the
	// IOleInPlaceFrame functions. So, our functions are able to retrieve the window handle we'll store here,
	// and then, they'll work with all such windows containing a browser control.
	//
	// Furthermore, since the browser will be calling our IOleClientSite's QueryInterface to get a pointer to
	// our IOleInPlaceSite and IDocHostUIHandler objects, that means that our IOleClientSite QueryInterface
	// must have an easy way to grab those pointers. Probably the easiest thing to do is just embed our
	// IOleInPlaceSite and IDocHostUIHandler objects inside of an extended IOleClientSite which we'll call
	// a _IOleClientSiteEx. As long as they come after the pointer to the IOleClientSite VTable, then we're
	// ok.
	//
	// Of course, we need to GlobalAlloc the above structs now. We'll just get all 3 with a single call to
	// GlobalAlloc, especially since they're are contained inside of our _IOleClientSiteEx anyway.
	//
	// So, we're not actually allocating separate IOleClientSite, IOleInPlaceSite, IOleInPlaceFrame and
	// IDocHostUIHandler structs.
	//
	// One final thing. We're going to allocate extra room to store the pointer to the browser object.
	if (!(ptr = (char *)GlobalAlloc(GMEM_FIXED, sizeof(_IOleClientSiteEx) + sizeof(IOleObject *))))
	{
		FatalErrorf("Unable to embed browser frame: GlobalAlloc() failed: %s", getWinErrString(NULL, GetLastError()));
		return(-1);
	}

	// Initialize our IOleClientSite object with a pointer to our IOleClientSite VTable.
	_iOleClientSiteEx = (_IOleClientSiteEx *)(ptr + sizeof(IOleObject *));
	_iOleClientSiteEx->client.lpVtbl = &MyIOleClientSiteTable;

	// Initialize our IOleInPlaceSite object with a pointer to our IOleInPlaceSite VTable.
	_iOleClientSiteEx->inplace.inplace.lpVtbl = &MyIOleInPlaceSiteTable;

	// Initialize our IOleInPlaceFrame object with a pointer to our IOleInPlaceFrame VTable.
	_iOleClientSiteEx->inplace.frame.frame.lpVtbl = &MyIOleInPlaceFrameTable;

	// Save our HWND (in the IOleInPlaceFrame object) so our IOleInPlaceFrame functions can retrieve it.
	_iOleClientSiteEx->inplace.frame.window = hwnd;

	// Initialize our IDocHostUIHandler object with a pointer to our IDocHostUIHandler VTable.
	_iOleClientSiteEx->ui.ui.lpVtbl = &MyIDocHostUIHandlerTable;

	// Initialize out IDispatch object.
	_iOleClientSiteEx->events.events.lpVtbl = &MyIDispatchVtblEvents;
	_iOleClientSiteEx->events.refCount = 0;
	_iOleClientSiteEx->events.window = window;

	// Get a pointer to the browser object and lock it down (so it doesn't "disappear" while we're using
	// it in this program). We do this by calling the OS function CoCreateInstance().
	//
	// NOTE: We need this pointer to interact with and control the browser. With normal WIN32 controls such as a
	// Static, Edit, Combobox, etc, you obtain an HWND and send messages to it with SendMessage(). Not so with
	// the browser object. You need to get a pointer to it. This object contains an array of pointers to functions           // you can call within the browser object. Actually, it contains a 'lpVtbl' member that is a pointer to that
	// array. We call the array a 'VTable'.
	//
	// For example, the browser object happens to have a SetClientSite() function we want to call. So, after we
	// retrieve the pointer to the browser object (in a local we'll name 'browserObject'), then we can call that
	// function, and pass it args, as so:
	//
	// browserObject->lpVtbl->SetClientSite(browserObject, SomeIOleClientObject);
	//
	// There's our pointer to the browser object in 'browserObject'. And there's the pointer to the browser object's
	// VTable in 'browserObject->lpVtbl'. And the pointer to the SetClientSite function happens to be stored in a
	// member named 'SetClientSite' within the VTable. So we are actually indirectly calling SetClientSite by using
	// a pointer to it. That's how you use a VTable.

	// Get Internet Explorer's IWebBrowser2 object (ie, IE's main object)
	if (!SUCCEEDED(hr = CoCreateInstance(&CLSID_WebBrowser, 0, CLSCTX_INPROC, &IID_IWebBrowser2, (void **)&webBrowser2)))
	{
		// Can't get the web browser's IWebBrowser2!
		FatalErrorf("Unable to embed browser frame: CoCreateInstance() IID_IWebBrowser2 failed: %s", getWinErrString(NULL, hr));
		GlobalFree(ptr);
		return(-2);
	}

	browserObject = 0;

	// We need to get a pointer to IWebBrowser2's IOleObject child object
	hr = webBrowser2->lpVtbl->QueryInterface(webBrowser2, &IID_IOleObject, (void**)&browserObject);
	if (!browserObject)
	{
		// Can't create an instance of the browser!
		FatalErrorf("Unable to embed browser frame: IWebBrowser2::QueryInterface() IID_IOleObject failed: %s", getWinErrString(NULL, hr));
		webBrowser2->lpVtbl->Release(webBrowser2);
		GlobalFree(ptr);
		return(-3);
	}

	// Ok, we now have the pointer to the IOleObject child object in 'browserObject'. Let's save this in the
	// memory block we allocated above, and then save the pointer to that whole thing in our window's
	// USERDATA member. That way, if we need multiple windows each hosting its own browser object, we can
	// call EmbedBrowserObject() for each one, and easily associate the appropriate browser object with
	// its matching window and its own objects containing per-window data.
	*((IOleObject **)ptr) = browserObject;
	((CrypticLauncherWindow *)window->pUserData)->browserPtr = (IOleObject **)ptr;
	// Don't use SetWindowLong, as the SimpleWindowManager system uses it.
	//SetWindowLong(hwnd, GWL_USERDATA, (LONG)ptr);

	// Give the browser a pointer to my IOleClientSite object.
	//
	// NOTE: We pass our _IOleClientSiteEx struct and lie -- saying that it's a IOleClientSite. It's ok. A
	// _IOleClientSiteEx struct starts with an embedded IOleClientSite. So the browser won't care, and we want
	// this extended struct passed to our IOleClientSite functions.
	if (!SUCCEEDED(hr = browserObject->lpVtbl->SetClientSite(browserObject, (IOleClientSite *)_iOleClientSiteEx)))
	{
		// Something went wrong setting up the browser!
		FatalErrorf("Unable to embed browser frame: IOleObject::SetClientSite() failed: %s", getWinErrString(NULL, hr));
		webBrowser2->lpVtbl->Release(webBrowser2);
		UnEmbedBrowserObject(window);
		return(-4);
	}

	// Set the display area of our browser control the same as our window's size
	success = GetClientRect(hwnd, &rect);
	if (!success)
	{
		FatalErrorf("Unable to embed browser frame: GetClientRect() failed: %s", getWinErrString(NULL, GetLastError()));
		webBrowser2->lpVtbl->Release(webBrowser2);
		UnEmbedBrowserObject(window);
		return(-5);
	}

	// and actually put the browser object into our window.
	if (!SUCCEEDED(hr = browserObject->lpVtbl->DoVerb(browserObject, OLEIVERB_INPLACEACTIVATE, 0, (IOleClientSite *)_iOleClientSiteEx, 0, hwnd, &rect)))
	{
		FatalErrorf("Unable to embed browser frame: IOleObject::DoVerb() failed: %s", getWinErrString(NULL, hr));
		webBrowser2->lpVtbl->Release(webBrowser2);
		UnEmbedBrowserObject(window);
		return(-5);
	}

	// Let's call several functions in the IWebBrowser2 object to position the browser display area
	// in our window. The functions we call are put_Left(), put_Top(), put_Width(), and put_Height().
	// Note that we reference the IWebBrowser2 object's VTable to get pointers to those functions. And
	// also note that the first arg we pass to each is the pointer to the IWebBrowser2 object.
	webBrowser2->lpVtbl->put_Left(webBrowser2, 0);
	webBrowser2->lpVtbl->put_Top(webBrowser2, 0);
	webBrowser2->lpVtbl->put_Width(webBrowser2, rect.right);
	webBrowser2->lpVtbl->put_Height(webBrowser2, rect.bottom);

	// Get the connection point container
	assert(SUCCEEDED(browserObject->lpVtbl->QueryInterface(browserObject, &IID_IConnectionPointContainer, (void**)(&pConnectionPointContainer))));
	// Get the appropriate connection point
	assert(SUCCEEDED(pConnectionPointContainer->lpVtbl->FindConnectionPoint(pConnectionPointContainer,&DIID_DWebBrowserEvents2, &pConnectionPoint)));
	// Advise the connection point of our event sink
	assert(SUCCEEDED(pConnectionPoint->lpVtbl->Advise(pConnectionPoint, (IUnknown *)&_iOleClientSiteEx->events, &dwAdviseCookie)));
	pConnectionPoint->lpVtbl->Release(pConnectionPoint);
	pConnectionPointContainer->lpVtbl->Release(pConnectionPointContainer);
	assert(dwAdviseCookie);

	// We no longer need the IWebBrowser2 object (ie, we don't plan to call any more functions in it
	// right now, so we can release our hold on it). Note that we'll still maintain our hold on the
	// browser IOleObject until we're done with that object.
	webBrowser2->lpVtbl->Release(webBrowser2);

	// Success
	return(0);
}

// !!!: THIS DOESN'T ACTUALLY WORK WITH MORE THAN ONE ARGUMENT. <NPK 2008-06-09>
bool InvokeScript(SimpleWindow *window, char *name, enumScriptArgType firstType, ...)
{
	CrypticLauncherWindow *launcher = (CrypticLauncherWindow *)window->pUserData;
	IDispatch *pScriptDisp = 0;
	DISPID IdDisp = 0;
	BSTR bstrName;
	VARIANT **vArgs = NULL;
	DISPPARAMS dpArgs = {NULL, NULL, 0, 0};
	enumScriptArgType currentType = firstType;
	va_list args;
	bool returnVal = true;

	if(!launcher->htmlDoc)
		return false;

	// Convert name to BSTR
	bstrName = TStr2BStr(window, name);

	// Convert args to VARIANT
	eaCreate(&vArgs);
	va_start(args, firstType);
	while(currentType != SCRIPT_ARG_NULL)
	{
		VARIANT *vArg = GlobalAlloc(GMEM_FIXED, sizeof(VARIANT));
		assert(vArg);
#pragma warning(suppress:6386) // Buffer overrun: accessing 'argument 1', the writable size is '1*0' bytes, but '16' bytes might be written
		VariantInit(vArg);

		switch(currentType)
		{
		case SCRIPT_ARG_STRING:
			{
				V_VT(vArg) = VT_BSTR;
				V_BSTR(vArg) = TStr2BStr(window, va_arg(args, char*));
			}
			break;
		case SCRIPT_ARG_INT:
			{
#if 0
				char *buffer = NULL;
				estrStackCreate(&buffer);
				estrPrintf(&buffer, "%i", va_arg(args, S32));
				V_VT(vArg) = VT_BSTR;
				V_BSTR(vArg) = TStr2BStr(window, buffer);
#endif
				V_VT(vArg) = VT_I4;
				V_I4(vArg) = va_arg(args, S32);
			}
		}

		eaPush(&vArgs, vArg);
		currentType = va_arg(args, enumScriptArgType);
	}
	va_end(args);

	// Initialize the DISPPARAMS structure
	dpArgs.cArgs = eaSize(&vArgs);
	if(dpArgs.cArgs > 0)
		dpArgs.rgvarg = *vArgs;

	// NOTE: Some WTF sanity checking because of CO-40611. Pretty sure the debugger is lying. <NPK 2009-04-20>
	assert(launcher);
	assert(launcher->htmlDoc);
	assert(launcher->htmlDoc->lpVtbl);
	assert(launcher->htmlDoc->lpVtbl->get_Script);

	// Get the script and execute it
	if(SUCCEEDED(launcher->htmlDoc->lpVtbl->get_Script(launcher->htmlDoc, &pScriptDisp)))
	{
		if (SUCCEEDED(pScriptDisp->lpVtbl->GetIDsOfNames(pScriptDisp,
														 &IID_NULL,
														 &bstrName,
														 1,
														 LOCALE_SYSTEM_DEFAULT,
														 &IdDisp)))
		{
			HRESULT hr = pScriptDisp->lpVtbl->Invoke(pScriptDisp,
				IdDisp,
				&IID_NULL,
				LOCALE_SYSTEM_DEFAULT,
				DISPATCH_METHOD,
				&dpArgs,
				NULL, NULL, NULL);
		}
		pScriptDisp->lpVtbl->Release(pScriptDisp);
	}

	// Clean up

	SysFreeString(bstrName);
	FOR_EACH_IN_EARRAY(vArgs, VARIANT, var)
		//switch(V_VT(var))
		//{
		//case VT_BSTR:
		//	SysFreeString(V_BSTR(var));
		//}
		VariantClear(var);
		GlobalFree(var);
	FOR_EACH_END
	eaDestroy(&vArgs);
	return true;
}

HRESULT ProcessKeystrokes(SimpleWindow *window, MSG msg)
{
	IWebBrowser2 *webBrowser2;

	if (SUCCEEDED(GetWebPtrs(window, &webBrowser2, 0)))
	{
		HRESULT hr;
		IOleInPlaceActiveObject* pIOIPAO;

		hr = webBrowser2->lpVtbl->QueryInterface(webBrowser2, &IID_IOleInPlaceActiveObject, (LPVOID *)&pIOIPAO);

		if (SUCCEEDED(hr)) return pIOIPAO->lpVtbl->TranslateAccelerator(pIOIPAO, &msg);

		webBrowser2->lpVtbl->Release(webBrowser2);
	}

	return S_FALSE;
}