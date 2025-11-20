#ifndef _EMBEDBROWSER_H
#define _EMBEDBROWSER_H

// For windows types
#include "wininclude.h"

typedef struct SimpleWindow SimpleWindow;
typedef struct IHTMLEventObj IHTMLEventObj;
typedef	struct IHTMLWindow2 IHTMLWindow2;
typedef struct IWebBrowser2 IWebBrowser2;
typedef struct IHTMLDocument2 IHTMLDocument2;
typedef	struct IHTMLElement IHTMLElement;
typedef enum READYSTATE READYSTATE;

// DoPageAction constants
#define WEBPAGE_GOBACK		0
#define WEBPAGE_GOFORWARD	1
#define WEBPAGE_GOHOME		2
#define WEBPAGE_SEARCH		3
#define WEBPAGE_REFRESH		4
#define WEBPAGE_STOP		5

// WaitOnReadyStateChange constants
#define WORS_SUCCESS	0
#define WORS_TIMEOUT	-1
#define WORS_DESTROYED	-2

// Types for InvokeScript
typedef enum
{
	SCRIPT_ARG_NULL=0,
	SCRIPT_ARG_STRING,
	SCRIPT_ARG_INT,
} enumScriptArgType;


// Passed to an app's window procedure (as a WM_NOTIFY message) whenever an
// action has occurred on the web page (and the app has asked to be informed
// of that specific action)
typedef struct {
	NMHDR			nmhdr;
	IHTMLEventObj *	htmlEvent;
	LPCTSTR			eventStr;
} WEBPARAMS; 

// Our _IDispatchEx struct. This is just an IDispatch with some
// extra fields appended to it for our use in storing extra
// info we need for the purpose of reacting to events that happen
// to some element on a web page.
typedef struct {
	IDispatch		dispatchObj;	// The mandatory IDispatch.
	DWORD			refCount;		// Our reference count.
	IHTMLWindow2 *	htmlWindow2;	// Where we store the IHTMLWindow2 so that our IDispatch's Invoke() can get it.
	HWND			hwnd;			// The window hosting the browser page. Our IDispatch's Invoke() sends messages when an event of interest occurs.
	short			id;				// Any numeric value of your choosing that you wish to associate with this IDispatch.
	unsigned short	extraSize;		// Byte size of any extra fields prepended to this struct.
	IUnknown		*object;		// Some object associated with the web page element this IDispatch is for.
	void			*userdata;		// An extra pointer.
} _IDispatchEx;

BSTR TStr2BStr(SimpleWindow *window, const char *string);
void * BStr2TStr(SimpleWindow *window, BSTR strIn);

void UnEmbedBrowserObject(SimpleWindow *window);
void DoPageAction(SimpleWindow *window, DWORD action);
long DisplayHTMLStr(SimpleWindow *window, const char *string);
long DisplayHTMLPage(SimpleWindow *window, const char *webPageName, const char *postData, const char *lang);
void ResizeBrowser(SimpleWindow *window, DWORD width, DWORD height);
long EmbedBrowserObject(SimpleWindow *window);

// Wait for a given ReadyState
HRESULT WaitOnReadyState(SimpleWindow *window, READYSTATE rs, DWORD timeout, IWebBrowser2 *webBrowser2);

// Retrieve the IWebBrowser2 and IHTMLDocument2 objects for a given window.
HRESULT GetWebPtrs(SimpleWindow *window, IWebBrowser2 **webBrowser2Result, IHTMLDocument2 **htmlDoc2Result);

// Setup event dispatchers.
IDispatch * CreateWebEvtHandler(SimpleWindow *window, IHTMLDocument2 * htmlDoc2, DWORD extraData, long id, IUnknown *obj, void *userdata);

// Return an HTMLElement object with a given name or ID
IHTMLElement * GetWebElement(SimpleWindow *window, IHTMLDocument2 *htmlDoc2, const char *name, INT nIndex, char **errorString);

// Invoke a JavaScript function in the loaded page.
bool InvokeScript(SimpleWindow *window, char *name, enumScriptArgType firstType, ...);

// Process a keystroke message in the embedded browser
HRESULT ProcessKeystrokes(SimpleWindow *window, MSG msg);

//Get information about the web browser and put it in trivia.
void RecordIETriviaData(IHTMLDocument2 *htmlDoc2);

#endif
