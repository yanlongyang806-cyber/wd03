#include "gslEditor.h"
#include "earray.h"
#include "GameServerLib.h"
#include "ResourceManager.h"
#include "Entity.h"
#include "Player.h"

#include "ResourceSystem_Internal.h"

typedef struct EditorServerQueuedAutoCommandReply
{
	EntityRef entRef;
	gslQueuedReplyFunc func;
	bool fence_data;
	U32 fence_id;
	void *userdata;
} EditorServerQueuedAutoCommandReply;

static EditorServerQueuedAutoCommandReply** editorServerQueuedACReplies = NULL;

void gslEditorOncePerFrame()
{
	int it;
	for (it = eaSize(&editorServerQueuedACReplies)-1; it >= 0; --it) {
		EditorServerQueuedAutoCommandReply* esqr = editorServerQueuedACReplies[it];
		ClientLink *pClientLink;
		U32 last_update_time = 0;
		Entity *pEnt = entFromEntityRefAnyPartition(esqr->entRef);

		pClientLink = entGetClientLink(pEnt);
		if (!pClientLink || !pClientLink->pResourceCache)
		{
			// Client is no longer connected -- no need to send a
			// reply.
			free( esqr );
			eaRemove(&editorServerQueuedACReplies, it);
			continue;
		}
		if (!esqr->fence_data)
		{
			// Not yet safe to send a reply -- keep the reply around
			// to send in the future.
			continue;
		}

		esqr->func(pEnt, esqr->userdata);

		// Reply has been sent.
		free(esqr);
		eaRemove(&editorServerQueuedACReplies, it);
	}
}

void gslEditorFenceCB(U32 fence_id, UserData *pUnused)
{
	int it;
	for (it = eaSize(&editorServerQueuedACReplies)-1; it >= 0; --it) {
		EditorServerQueuedAutoCommandReply* esqr = editorServerQueuedACReplies[it];
		if (esqr->fence_id == fence_id)
		{
			esqr->fence_data = true;
			break;
		}
	}
}

void gslEditorQueueAutoCommandReply(Entity *pEnt, gslQueuedReplyFunc func, void *userdata)
{
	ClientLink *pClientLink = entGetClientLink(pEnt);
	if (pClientLink && pClientLink->pResourceCache)
	{
		EditorServerQueuedAutoCommandReply* esqr = calloc( 1, sizeof( *esqr ));
		esqr->entRef = entGetRef(pEnt);
		esqr->func = func;
		esqr->userdata = userdata;
		esqr->fence_data = false;
		eaPush( &editorServerQueuedACReplies, esqr );

		esqr->fence_id = resServerRequestFenceInstruction(pClientLink->pResourceCache, gslEditorFenceCB, NULL);
	}
}
