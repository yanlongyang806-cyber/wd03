#include "gslEditor.h"

#include "Entity.h"
#include "GameServerLib.h"
#include "Player.h"
#include "ResourceSystem_internal.h"
#include "WorldGrid.h"
#include "wlState.h"
#include "StaticWorld/WorldCellStreaming.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

void editorServerZmapReadyForSaveAsCB(Entity *pEnt, void *success)
{
	ClientCmd_wleZmapReadyForSaveAs(pEnt, !!success);
}

static void editorServerRequestSendMessageUpdate(DisplayMessage *pMsg, ResourceCache *pCache)
{
	ResourceDictionary *dict = resGetDictionary( gMessageDict );
	const char *pMsgKey;

	assert( !pMsg->pEditorCopy );

	pMsgKey = REF_STRING_FROM_HANDLE( pMsg->hMessage );
	if (pMsgKey)
		resServerRequestSendResourceUpdate( dict, pMsgKey, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE );
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_PRIVATE;
void editorServerPrepareZmapSaveAs(Entity *pEnt)
{
	int l;
	int layer_cnt = zmapGetLayerCount(NULL);

	layerSetHACKDisableGameCallbacks( true );
	
	// Make sure the client has all the messages in this map
	for ( l=0; l < layer_cnt; l++ )
	{
		int i;
		ZoneMapLayer *layer = zmapGetLayer(NULL, l);
		ClientLink *pClientLink = entGetClientLink(pEnt);
		GroupDefPropertyGroup gdg = { 0 };
		GroupDefLib *def_lib;
		GroupDef **lib_defs;
	
		assert(layer);

		// Load source if not currently loaded
		if (layerGetMode(layer) < LAYER_MODE_TERRAIN)
			layerSetMode(layer, LAYER_MODE_GROUPTREE, false, false, false);

		def_lib = layerGetGroupDefLib(layer);
		if (!pClientLink || !pClientLink->pResourceCache || !def_lib) {
			layerUnload(layer);
			editorServerZmapReadyForSaveAsCB(pEnt, 0);
			return;
		}

		lib_defs = groupLibGetDefEArray(def_lib);
		for (i = 0; i < eaSize(&lib_defs); ++i)
			eaPush(&gdg.props, &lib_defs[i]->property_structs);

		// Messages will get pushed later, when the reply is being sent
		langForEachDisplayMessage(parse_GroupDefPropertyGroup, &gdg, editorServerRequestSendMessageUpdate, pClientLink->pResourceCache);
		eaDestroy(&gdg.props);

		layerUnload(layer);
	}
	
	layerSetHACKDisableGameCallbacks( false );

	if (wl_state.save_map_game_callback)
	{
		wl_state.save_map_game_callback(NULL);
	}
	gslEditorQueueAutoCommandReply(pEnt, editorServerZmapReadyForSaveAsCB, (void*)1);
}