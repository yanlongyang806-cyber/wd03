#include "gslHttpAsync.h"
#include "HttpAsync.h"
#include "Player.h"
#include "gslSendToClient.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

// Debugging callback for gslhaRequest to just print the response
void gslhaPrintCB(Entity *ent, const char *response, int len, int response_code, void *userdata)
{
	if(ent && ent->pPlayer && ent->pPlayer->accessLevel >= ACCESS_GM)
		gslSendPrintf(ent, "HTTP %i\n%s\n\n", response_code, response);
}

// HTTP request utilities
typedef struct gslhaCallbackData
{
	EntityRef entref;
	gslhaCallback cb;
	gslhaTimeout timeout_cb;
	void *userdata;
} gslhaCallbackData;

static void gslhaRequestRunCallback(const char *response, int len, int response_code, gslhaCallbackData *pending)
{
	Entity *ent = entFromEntityRefAnyPartition(pending->entref);
	if(ent)
	{
		if(pending->cb)
			pending->cb(ent, response, len, response_code, pending->userdata);
	}
	else
	{
		if(pending->timeout_cb)
			pending->timeout_cb(ent, pending->userdata);
	}
	free(pending);
}

static void gslhaRequestTimeoutCallback(gslhaCallbackData *pending)
{
	if(pending->timeout_cb)
		pending->timeout_cb(entFromEntityRefAnyPartition(pending->entref), pending->userdata);
	free(pending);
}

void gslhaRequest(Entity *ent, UrlArgumentList *args, gslhaCallback cb, gslhaTimeout timeout_cb, int timeout, void *userdata)
{
	gslhaCallbackData *pending;

	// Create the callback data
	pending = calloc(1, sizeof(gslhaCallbackData));
	pending->cb = cb;
	pending->timeout_cb = timeout_cb;
	pending->entref = entGetRef(ent);
	pending->userdata = userdata;

	// Run the request
	haRequest(NULL, &args, gslhaRequestRunCallback, gslhaRequestTimeoutCallback, timeout, pending);
}
