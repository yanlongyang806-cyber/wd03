#if !PLATFORM_CONSOLE
#include "gclSteam.h"
#include "SteamCommon.h"

#pragma warning( push )
#pragma warning( disable : 4996 )
#include "steam_api.h"
#pragma warning( pop )

#pragma comment(lib, "../../3rdparty/steam_sdk/redistributable_bin/steam_api.lib")

class SteamEventListener 
{
public:
	SteamEventListener();
private:
	STEAM_CALLBACK( SteamEventListener, OnMicroTxnAuthorizationResponse, MicroTxnAuthorizationResponse_t, m_MicroTxnAuthorizationResponse );
};

#pragma warning( push )
#pragma warning( disable : 4355 )
SteamEventListener::SteamEventListener(  ) :
m_MicroTxnAuthorizationResponse( this, &SteamEventListener::OnMicroTxnAuthorizationResponse )
{ }
#pragma warning( pop )

void SteamEventListener::OnMicroTxnAuthorizationResponse( MicroTxnAuthorizationResponse_t *callback )
{
	gclSteamOnMicroTxnAuthorizationResponse(callback->m_bAuthorized!=0, callback->m_ulOrderID);
}

extern "C" {

static bool steam_enabled = false;
static bool steam_owned = false;
static SteamEventListener *steam_event_listener = NULL;

void gclSteamInit(void)
{
	if(!ccSteamAppID())
		return; // Don't even init the API for games with no Steam ID
	steam_enabled = SteamAPI_Init();
	if(steam_enabled)
	{
		steam_event_listener = new SteamEventListener();
		SteamUserStats()->RequestCurrentStats();
		steam_owned = gclSteamIsSubscribedApp(ccSteamAppID());
	}
}

void gclSteamOncePerFrame(void)
{
	if(steam_enabled)
	{
		SteamAPI_RunCallbacks();
	}
}

void gclSteamSetAchievement(const char *pchName)
{
	if(steam_enabled)
	{
		SteamUserStats()->SetAchievement(pchName);
		SteamUserStats()->StoreStats();
	}
}

void gclSteamReset(void)
{
	if(steam_enabled)
	{
		SteamUserStats()->ResetAllStats(true);
		SteamUserStats()->StoreStats();
	}
}

U64 gclSteamID(void)
{
	if(steam_enabled)
	{
		return SteamUser()->GetSteamID().ConvertToUint64();
	}
	return 0;
}

bool gclSteamIsSubscribedApp(U32 uAppID)
{
	if(steam_enabled)
	{
		if(uAppID == STEAM_CURRENT_APP)
			return steam_owned;
		return SteamApps()->BIsSubscribedApp(uAppID);
	}
	return false;
}

} // extern "C"

#endif