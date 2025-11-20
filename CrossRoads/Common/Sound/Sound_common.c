#include "Sound_common.h"
#include "GameEvent.h"
#include "StringCache.h"
#include "FolderCache.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "error.h"
#include "AutoGen\Sound_common_h_ast.h"
#include "../../../libs/WorldLib/StaticWorld/WorldCellStreaming.h"
#include "fileutil.h"

#include "genericlist.h"

#ifdef GAMESERVER
#include "gslSound.h"
#endif

DictionaryHandle *g_GAEMapDict = NULL;

static SndCommonChangedCallbackInfo *gSndCommonChangedCallbacks = NULL;

//SndCommonChangedCallback gSndCommonChangedCallback = { NULL, NULL };

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););
AUTO_RUN_ANON(memBudgetAddStructMapping("GameAudioEventMap", __FILE__););

void sndCommonAddChangedCallback(SndCommonGAELayersChanged func, void *userData)
{
	SndCommonChangedCallbackInfo *newItem;

	newItem = listAddNewMember(&gSndCommonChangedCallbacks, sizeof(SndCommonChangedCallbackInfo));

	newItem->func = func;
	newItem->userData = userData;
}

static void reloadGAECallback(const char *relpath, int when)
{
	if(when & FOLDER_CACHE_CALLBACK_UPDATE)
	{
		ParserReloadFileToDictionary(relpath, g_GAEMapDict);

		{
			SndCommonChangedCallbackInfo *cbInfo;

			for(cbInfo = gSndCommonChangedCallbacks; cbInfo; cbInfo = cbInfo->next) {
				// make the call if there is one to make
				if(cbInfo->func)
				{
					cbInfo->func(relpath, when, cbInfo->userData);
				}
			}
		}

		wlStatusPrintf("Reloaded %s", relpath);
	}
}

void sndCommonCreateGAEDict(void)
{
	if(!g_GAEMapDict)
	{
		DictionaryEArrayStruct *dictea;
		int i;

		g_GAEMapDict = RefSystem_RegisterSelfDefiningDictionary("AudioMapLayers", 0, parse_GameAudioEventMap, 
																true, 1, NULL);

		if(IsClient() && !isDevelopmentMode())
			return;

		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_ALL, "maps/*.gaelayer", reloadGAECallback);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_ALL, "sound/*.gaelayer", reloadGAECallback);

		ParserLoadFilesToDictionary("sound/gaelayers", ".gaelayer", "gaelayers.gaevents",
									PARSER_SERVERSIDE|PARSER_OPTIONALFLAG, g_GAEMapDict);
		
		dictea = resDictGetEArrayStruct(g_GAEMapDict);
		for(i=0; i<eaSize(&dictea->ppReferents); i++)
		{
			GameAudioEventMap *map = (GameAudioEventMap*)dictea->ppReferents[i];
			map->is_global = 1;
		}
	}
}

void sndCommonLoadToGAEDict(const char *path)
{
	char binName[MAX_PATH] = {0};
	DictionaryEArrayStruct *eaStruct = NULL;
	BinFileListWithCRCs *deps_list = zmapGetExternalDepsList(NULL);
	int i;
	bool found = false;

	if(IsClient() && !isDevelopmentMode())
		return;

	strcpy(binName, path);
	strcat(binName, ".gaevents");

	eaStruct = resDictGetEArrayStruct(g_GAEMapDict);

	for(i=0; i<eaSize(&eaStruct->ppReferents); i++)
	{
		GameAudioEventMap *map = eaStruct->ppReferents[i];

		if(strstri(map->filename, path))
		{
			// Already loaded
			return;
		}
	}

	ParserLoadFilesToDictionary(path, ".gaelayer", worldMakeBinName(binName), 
								PARSER_SERVERSIDE|PARSER_OPTIONALFLAG, g_GAEMapDict);

	worldSetGAELayerCRC(ParseTableCRC(parse_GameAudioEventMap, NULL, 0)); // Called at least once (for EmptyMap) before we do fast bin dependency checking

	FOR_EACH_IN_REFDICT(g_GAEMapDict, GameAudioEventMap, map) {
		if (!map->is_global)
		{
			bflAddDepsSourceFile(deps_list, map->filename);
			found = true;
		}
	} FOR_EACH_END;
	bflSetDepsGAELayerCRC(deps_list, found ? ParseTableCRC(parse_GameAudioEventMap, NULL, 0) : 0);
}

AUTO_FIXUPFUNC;
TextParserResult fixupGAEMap(GameAudioEventMap *map, enumTextParserFixupType type, void *pExtraData)
{
	switch(type)
	{
		xcase FIXUPTYPE_POST_TEXT_READ: {
			char name[256];
			getFileNameNoExt(name, map->filename);
			map->name = allocAddString(name);
		}

		xcase FIXUPTYPE_POST_LOAD_DURING_LOADFILES_FINAL_LOCATION: {
			int i;
			for(i=0; i<eaSize(&map->pairs); i++)
			{
				map->pairs[i]->map = map;
			}
		}

		xcase FIXUPTYPE_DESTRUCTOR: {
#ifdef GAMESERVER
			sndGAEMapStopTracking(map);
#endif
		}

		xcase FIXUPTYPE_POST_RELOAD: {
			int i;
			for(i=0; i<eaSize(&map->pairs); i++)
			{
				map->pairs[i]->map = map;
			}

#ifdef GAMESERVER			
			sndGAEMapStartTracking(map);

			snd_server_state.needsInitEnc = 1;
#endif
		}
	}

	return PARSERESULT_SUCCESS;
}

StatusWatchType sndCommonGetStatusWatchType(GameEvent *ge)
{
	if(!ge)
		return StatusWatch_Imm_Source;
	switch(ge->type)
	{
		
		case EventType_HealthState: {
			return StatusWatch_Entity;
		}
		case EventType_InteractInterrupted: {
			return StatusWatch_Pos;
		}
		case EventType_InteractBegin:
		case EventType_InteractSuccess:
		case EventType_InteractFailure:
		case EventType_ClickableActive:
		case EventType_InteractEndActive: {
			return StatusWatch_Pos;
		}
		case EventType_PlayerSpawnIn:
		case EventType_VolumeEntered:
		case EventType_VolumeExited:
		case EventType_MissionState:
		case EventType_CutsceneEnd:
		case EventType_CutsceneStart:
		case EventType_ContactDialogComplete: {
			return StatusWatch_Imm_Source;
		}
		case EventType_Kills:
		case EventType_Assists: {
			return StatusWatch_Imm_All;
		}		
	}
	return StatusWatch_Imm_Source;
}

SndCommonEventExistsFunc sndCommonEventExists = NULL;
void sndGAEMapValidate(GameAudioEventMap *map, int noError)
{
	int i;

	map->invalid = 0;
	for(i=0; i<eaSize(&map->pairs); i++)
	{
		GameAudioEventPair *pair = map->pairs[i];

		if(!pair->game_event || !pair->audio_event || !pair->audio_event[0])
			pair->invalid = 1;
		else if (sndCommonEventExists && !sndCommonEventExists(pair->audio_event))
		{
			pair->invalid = 1;
			if(!noError)
				ErrorFilenameGroupRetroactivef(map->filename, "Audio", 20, 3, 16, 2009, 
						"Event no longer exists: %s - run refresh audio", pair->audio_event);
		}
		//else if(!pair->one_shot && sndCommonGetStatusWatchType(pair->game_event)!=StatusWatch_Entity)
		//{
		//	if(!noError)
		//		ErrorFilenamef(map->filename, "Non-entity-based event used with looping event: %s.", pair->audio_event);
		//	pair->invalid = 1;
		//	map->invalid = 1;
		//}
		else
			pair->invalid = 0;
	}	
}

void sndCommonSetEventExistsFunc(SndCommonEventExistsFunc func)
{
	sndCommonEventExists = func;
}

static bool sndCommon_GetAudioAssets_HandleString(const char *pcAddString, const char ***peaStrings)
{
	if (pcAddString)
	{
		bool bDup = false;
		FOR_EACH_IN_EARRAY(*peaStrings, const char, pcHasString) {
			if (strcmpi(pcHasString, pcAddString) == 0) {
				bDup = true;
			}
		} FOR_EACH_END;
		if (!bDup) {
			eaPush(peaStrings, strdup(pcAddString));
		}
		return true;
	}
	return false;
}

static FileScanAction sndCommon_FindGameAudioEventMaps(char *dir, struct _finddata32_t* data, char*** filenames)
{
	char fullPath[ MAX_PATH ];
	sprintf(fullPath, "%s/%s", dir, data->name);

	if(!(data->attrib & _A_SUBDIR) && strEndsWith(data->name, ".gaelayer")) {
		eaPush(filenames, strdup(fullPath));
	}

	return FSA_EXPLORE_DIRECTORY;
}

void sndCommon_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio)
{
	char** objectFiles = NULL;

	*ppcType = strdup("GameAudioEventMaps");

	fileScanAllDataDirs("maps/",  sndCommon_FindGameAudioEventMaps, &objectFiles);
	fileScanAllDataDirs("sound/", sndCommon_FindGameAudioEventMaps, &objectFiles);

	FOR_EACH_IN_EARRAY(objectFiles, char, filename)
	{
		GameAudioEventMap *pGAEMap;
		bool bResourceHasAudio = false;

		pGAEMap = StructCreate(parse_GameAudioEventMap);
		ParserReadTextFile(filename, parse_GameAudioEventMap, pGAEMap, 0);
		FOR_EACH_IN_EARRAY(pGAEMap->pairs, GameAudioEventPair, pGAEPair)
		{
			bResourceHasAudio |= sndCommon_GetAudioAssets_HandleString(pGAEPair->audio_event, peaStrings);
		}
		FOR_EACH_END;
		StructDestroy(parse_GameAudioEventMap, pGAEMap);

		*puiNumData = *puiNumData + 1;
		if (bResourceHasAudio) {
			*puiNumDataWithAudio = *puiNumDataWithAudio + 1;
		}
	}
	FOR_EACH_END;

	eaDestroyEx(&objectFiles, NULL);
}

#include "AutoGen\Sound_common_h_ast.c"