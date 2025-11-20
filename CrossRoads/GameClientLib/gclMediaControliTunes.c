#if !PLATFORM_CONSOLE

#include "gclMediaControl.h"
#include "NotifyCommon.h"
#include "GfxConsole.h"

#include "wininclude.h"
#include "iTunesCOMInterface.h"

#pragma comment(lib,"iTunesCOM.lib")

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static IiTunes *g_itunes = NULL;

// Custom IDispatch implementation to see _IiTunesEvents
HRESULT STDMETHODCALLTYPE Events_QueryInterface(_IiTunesEvents *, REFIID, void **);
HRESULT STDMETHODCALLTYPE Events_AddRef(_IiTunesEvents *);
HRESULT STDMETHODCALLTYPE Events_Release(_IiTunesEvents *);
HRESULT STDMETHODCALLTYPE Events_GetTypeInfoCount(_IiTunesEvents *, UINT *);
HRESULT STDMETHODCALLTYPE Events_GetTypeInfo(_IiTunesEvents *, UINT, LCID, ITypeInfo **);
HRESULT STDMETHODCALLTYPE Events_GetIDsOfNames(_IiTunesEvents *, REFIID, LPOLESTR *, UINT, LCID, DISPID *);
HRESULT STDMETHODCALLTYPE Events_Invoke (_IiTunesEvents *, DISPID, REFIID, LCID, WORD, DISPPARAMS *, VARIANT *, EXCEPINFO *, UINT *);

static void updateTrack(IITTrack *track);

// The VTable for our _IDispatchEx object for events.
_IiTunesEventsVtbl MyIDispatchVtblEvents = {
	Events_QueryInterface,
	Events_AddRef,
	Events_Release,
	Events_GetTypeInfoCount,
	Events_GetTypeInfo,
	Events_GetIDsOfNames,
	Events_Invoke
};

typedef struct _IiTunesEventsEx
{
	_IiTunesEvents events;
	U32 refCount;
} _IiTunesEventsEx;

static _IiTunesEventsEx *g_itunes_events = NULL;

//////////////////////////////////////////////////////////////////////////
// Implementation of IDispatch for event handler

HRESULT STDMETHODCALLTYPE Events_QueryInterface(_IiTunesEvents * This, REFIID riid, void **ppvObject)
{
	*ppvObject = 0;

	if (!memcmp(riid, &IID_IUnknown, sizeof(GUID)) || !memcmp(riid, &IID_IDispatch, sizeof(GUID)) || !memcmp(riid, &DIID__IiTunesEvents, sizeof(GUID)))
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

HRESULT STDMETHODCALLTYPE Events_AddRef(_IiTunesEvents *This)
{
	return(InterlockedIncrement(&((_IiTunesEventsEx *)This)->refCount));
}

HRESULT STDMETHODCALLTYPE Events_Release(_IiTunesEvents *This)
{
	if (InterlockedDecrement( &((_IiTunesEventsEx *)This)->refCount ) == 0)
	{
		/* If you uncomment the following line you should get one message
		* when the document unloads for each successful call to
		* CreateEventHandler. If not, check you are setting all events
		* (that you set), to null or detaching them.
		*/
		// OutputDebugString("One event handler destroyed");

		//GlobalFree(((char *)This - ((_DWebBrowserEvents2Ex *)This)->extraSize));
		GlobalFree(This);
		return(0);
	}

	return(((_IiTunesEventsEx *)This)->refCount);
}

HRESULT STDMETHODCALLTYPE Events_GetTypeInfoCount(_IiTunesEvents *This, unsigned int *pctinfo)
{
	return(E_NOTIMPL);
}

HRESULT STDMETHODCALLTYPE Events_GetTypeInfo(_IiTunesEvents *This, unsigned int iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
	return(E_NOTIMPL);
}

HRESULT STDMETHODCALLTYPE Events_GetIDsOfNames(_IiTunesEvents *This, REFIID riid, OLECHAR ** rgszNames, unsigned int cNames, LCID lcid, DISPID * rgDispId)
{
	return(S_OK);
}

HRESULT STDMETHODCALLTYPE Events_Invoke(_IiTunesEvents *This, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags, DISPPARAMS * pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, unsigned int *puArgErr)
{
	ITPlayerState playerstate;
	// This function is called to recevie an event. The event is identified by the
	// dispIdMember argument. It is our responsibility to retrieve the event arguments
	// from the pDispParams->rgvarg array and call the event function.
	// If we do not handle an event we can return DISP_E_MEMBERNOTFOUND.
	// The variant member that we use for each argument is determined by the argument
	// type of the event function. eg. If an event has the argument long x we would
	// use the lVal member of the VARIANT struct.

	// Here is our message map, where we map dispids to function calls.
	//switch (dispIdMember) {
	//case DISPID_BEFORENAVIGATE2:
	//	// call BeforeNavigate
	//	// (parameters are on stack, thus in reverse order)
	//	EventHandler_BeforeNavigate2(window,
	//		pDispParams->rgvarg[6].pdispVal,    // pDisp
	//		pDispParams->rgvarg[5].pvarVal,     // url
	//		pDispParams->rgvarg[4].pvarVal,     // Flags
	//		pDispParams->rgvarg[3].pvarVal,     // TargetFrameName
	//		pDispParams->rgvarg[2].pvarVal,     // PostData
	//		pDispParams->rgvarg[1].pvarVal,     // Headers
	//		pDispParams->rgvarg[0].pboolVal);   // Cancel
	//	break;
	//case DISPID_DOCUMENTCOMPLETE:
	//	EventHandler_DocumentComplete(window, pDispParams->rgvarg[1].pdispVal, pDispParams->rgvarg[0].pvarVal);
	//	break;
	//}
	switch (dispIdMember) {
	case ITEventPlayerPlay:
	{
		IITTrack *track;
		//conPrintf("ITEventPlayerPlay\n");
		g_itunes->lpVtbl->get_PlayerState(g_itunes, &playerstate);
		gclMediaControlUpdate(playerstate==ITPlayerStatePlaying?1:0, NULL, NULL, NULL, -1, -1, -1);
		pDispParams->rgvarg[0].pdispVal->lpVtbl->QueryInterface(pDispParams->rgvarg[0].pdispVal, &IID_IITTrack, &track);
		updateTrack(track);
		if(track)
			track->lpVtbl->Release(track);
		break;
	}
	case ITEventPlayerStop:
		//conPrintf("ITEventPlayerStop\n");
		g_itunes->lpVtbl->get_PlayerState(g_itunes, &playerstate);
		gclMediaControlUpdate(playerstate==ITPlayerStatePlaying?1:0, NULL, NULL, NULL, -1, -1, -1);
		break;
	case ITEventDatabaseChanged:
		{
			IITTrack *track;
			//conPrintf("ITEventDatabaseChanged\n");
			g_itunes->lpVtbl->get_CurrentTrack(g_itunes, &track);
			updateTrack(track);
			if(track)
				track->lpVtbl->Release(track);
			break;
		}
	case ITEventSoundVolumeChanged:
		{
			long volume;
			g_itunes->lpVtbl->get_SoundVolume(g_itunes, &volume);
			gclMediaControlUpdate(-1, NULL, NULL, NULL, volume, -1, -1);
			break;
		}
	case ITEventAboutToPromptUserToQuit:
		//conPrintf("ITEventAboutToPromptUserToQuit\n");
		g_itunes->lpVtbl->Release(g_itunes);
		g_itunes = NULL;
		gclMediaControlUpdate(0, "", "", "", 0, 0, 0);
		gclMediaControlUpdateDisconnected();
		break;
	}

	return(S_OK);
}



static void itunesConnect(void)
{
	if(!g_itunes)
	{
		IConnectionPoint           *pConnectionPoint;
		IConnectionPointContainer  *pConnectionPointContainer;
		DWORD                       dwAdviseCookie = 0;
		HRESULT res;
		IITTrack *track;
		ITPlayerState playerstate;
		long volume;

		assert(SUCCEEDED(res = OleInitialize(NULL)));

		if(!SUCCEEDED(CoCreateInstance(&CLSID_iTunesApp, NULL, CLSCTX_LOCAL_SERVER, &IID_IiTunes, (PVOID *)&g_itunes)))
			return;
		g_itunes_events = GlobalAlloc(GMEM_FIXED, sizeof(_IiTunesEventsEx));
		g_itunes_events->events.lpVtbl = &MyIDispatchVtblEvents;
		g_itunes_events->refCount = 0;

		assert(SUCCEEDED(g_itunes->lpVtbl->QueryInterface(g_itunes, &IID_IConnectionPointContainer, (void**)(&pConnectionPointContainer))));
		// Get the appropriate connection point
		assert(SUCCEEDED(pConnectionPointContainer->lpVtbl->FindConnectionPoint(pConnectionPointContainer, &DIID__IiTunesEvents, &pConnectionPoint)));
		pConnectionPointContainer->lpVtbl->Release(pConnectionPointContainer);

		// Advise the connection point of our event sink
		assert(SUCCEEDED(res = pConnectionPoint->lpVtbl->Advise(pConnectionPoint, (IUnknown *)g_itunes_events, &dwAdviseCookie)));
		pConnectionPoint->lpVtbl->Release(pConnectionPoint);
		assert(dwAdviseCookie);

		// Grab the current playing state
		g_itunes->lpVtbl->get_PlayerState(g_itunes, &playerstate);

		// Grab the current track
		g_itunes->lpVtbl->get_CurrentTrack(g_itunes, &track);
		updateTrack(track);
		if(track)
			track->lpVtbl->Release(track);

		// Grab the current volume
		g_itunes->lpVtbl->get_SoundVolume(g_itunes, &volume);
		gclMediaControlUpdate(playerstate==ITPlayerStatePlaying?1:0, NULL, NULL, NULL, volume, -1, -1);
	}
}

static void itunesDisconnect(void)
{
	if(g_itunes)
	{
		g_itunes->lpVtbl->Release(g_itunes);
		g_itunes = NULL;
	}
}

static void itunesTick(void)
{
	MSG msg;
	
	// Update the current track time since there is no event for seeking within a track
	if(g_itunes)
	{
		long current;
		g_itunes->lpVtbl->get_PlayerPosition(g_itunes, &current);
		gclMediaControlUpdate(-1, NULL, NULL, NULL, -1, current, -1);
	}

	// Check for COM messages waiting to be dispatched. This must run in the same thread as the other COM functions.
	while (PeekMessage(&msg, NULL, WM_USER, WM_USER, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static void updateTrack(IITTrack *track)
{
	BSTR str;
	int size;
	char *name_str, *album_str, *artist_str;
	long current, total;
	HRESULT res;

	if(!track)
	{
		gclMediaControlUpdate(0, "", "", "", -1, 0, 0);
		return;
	}

	// Get the track name
	res = track->lpVtbl->get_Name(track, &str);
	if(res == S_OK)
	{
		size = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)str), -1, 0, 0, 0, 0) + 1;
		name_str = malloc(size);
		WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)str), -1, (char *)name_str, size, 0, 0);
		name_str[size-1] = '\0';
	}
	else
		name_str = strdup("");

	// Get the album
	res = track->lpVtbl->get_Album(track, &str);
	if(res == S_OK)
	{
		size = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)str), -1, 0, 0, 0, 0) + 1;
		album_str = malloc(size);
		WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)str), -1, (char *)album_str, size, 0, 0);
		album_str[size-1] = '\0';
	}
	else
		album_str = strdup("");

	// Get the artist
	res = track->lpVtbl->get_Artist(track, &str);
	if(res == S_OK)
	{
		size = WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)str), -1, 0, 0, 0, 0) + 1;
		artist_str = malloc(size);
		WideCharToMultiByte(CP_UTF8, 0, (WCHAR *)((char *)str), -1, (char *)artist_str, size, 0, 0);
		artist_str[size-1] = '\0';
	}
	else
		artist_str = strdup("");

	// Get the track time
	g_itunes->lpVtbl->get_PlayerPosition(g_itunes, &current);
	track->lpVtbl->get_Duration(track, &total);

	gclMediaControlUpdate(-1, name_str, album_str, artist_str, -1, current, total);

	free(name_str);
	free(album_str);
	free(artist_str);
}

static void itunesPlayPause(void)
{
	if(g_itunes)
		g_itunes->lpVtbl->PlayPause(g_itunes);
	//else
	//	notify_NotifySend(NULL, kNotifyType_MediaControlError, "Can't connect to iTunes", NULL, NULL);
	//g_playing = !g_playing;
	//gclMediaControlGetCurrentTrack();
}

static void itunesPrevious(void)
{
	if(g_itunes)
		g_itunes->lpVtbl->PreviousTrack(g_itunes);
	//else
	//	notify_NotifySend(NULL, kNotifyType_MediaControlError, "Can't connect to iTunes", NULL, NULL);
	//gclMediaControlGetCurrentTrack();
}

static void itunesNext(void)
{
	if(g_itunes)
		g_itunes->lpVtbl->NextTrack(g_itunes);
	//else
	//	notify_NotifySend(NULL, kNotifyType_MediaControlError, "Can't connect to iTunes", NULL, NULL);
	//gclMediaControlGetCurrentTrack();
}

static void itunesVolume(U32 volume)
{
	if(g_itunes)
		g_itunes->lpVtbl->put_SoundVolume(g_itunes, volume);
}

static void itunesTime(float time)
{
	if(g_itunes)
		g_itunes->lpVtbl->put_PlayerPosition(g_itunes, time);
}

AUTO_RUN;
void gclMediaControliTunesRegister(void)
{
	gclMediaControlRegister("iTunes", itunesConnect, itunesDisconnect, itunesTick, itunesPlayPause, itunesPrevious, itunesNext, itunesVolume, itunesTime);
}

#endif