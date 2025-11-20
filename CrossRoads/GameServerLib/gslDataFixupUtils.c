/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// This is a home for miscellaneous utilities related to changing game data.
// (For example, to match a data structure change.)

#include "gslDataFixupUtils.h"

#include "oldencounter_common.h"
#include "mission_common.h"
#include "itemCommon.h"
#include "rewardCommon.h"
#include "GameEvent.h"
#include "GameAction_common.h"

#include "Expression.h"
#include "EArray.h"
#include "EString.h"
#include "textparser.h"
#include "MultiVal.h"
#include "error.h"
#include "file.h"
#include "fileutil.h"
#include "stringcache.h"
#include "utils.h"
#include "gimmeDLLWrapper.h"

#include "AutoGen/oldencounter_common_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"

#include "WorldGrid.h"
#include "../StaticWorld/WorldGridPrivate.h"
#include "../StaticWorld/WorldGridLoadPrivate.h"
#include "../AutoGen/WorldGridLoadPrivate_h_ast.h"


// ----------------------------------------------------------------------
// Gimme Utils
// ----------------------------------------------------------------------


int fixupCheckoutFile(const char *pcFilename, bool bAlertErrors)
{
	char fullfilename[MAX_PATH];
	int ret;

	fileLocateWrite(pcFilename, fullfilename);
	forwardSlashes(fullfilename);

	// If already writable, then no problem
	if (!fileIsReadOnly(fullfilename)) {
		return true;
	}

	ret = gimmeDLLDoOperation(fullfilename, GIMME_CHECKOUT, GIMME_QUIET);

	if (ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB) {
		if (bAlertErrors) {
			const char *pcLockee;
			if (ret == GIMME_ERROR_ALREADY_CHECKEDOUT && (pcLockee = gimmeDLLQueryIsFileLocked(fullfilename))) {
				Alertf("File \"%s\" unable to be checked out, currently checked out by %s", pcFilename, pcLockee);
			} else {
				Alertf("File \"%s\" unable to be checked out (%s)", pcFilename, gimmeDLLGetErrorString(ret));
			}
		}
		return false;
	}

	return true;
}


// ----------------------------------------------------------------------
//   Expression Fixup Utils
// ----------------------------------------------------------------------

// Hacky data fixup utility that removes all instances of an expression function from an Expression,
// and runs a callback for each one removed.
// It can also replace the expression with another string, if one is provided by the callback.
// My parsing only works for very simple cases, so check the results carefully!
// returns TRUE if a change was made
bool datafixup_RemoveExprFuncFromEStringWithCallback(char **estrExpr, const char *pchFuncNameToRemove, const char *pchErrorFilename, ExprFixupRemoveFuncCB callback, void *pData)
{
	char *pchNextOccurance;
	bool bExprChanged = false;

	if (estrExpr && *estrExpr)
	{
		pchNextOccurance = *estrExpr;
		while (pchNextOccurance = strstri(pchNextOccurance, pchFuncNameToRemove))
		{
			char **ppParsedArgs = NULL;
			char *pchArgs = pchNextOccurance + strlen(pchFuncNameToRemove);
			char *pchEndArgs;
			char *tmp = NULL;
			char *estrReplacementString = NULL;
			bool bRemove = false;
			bool bInQuotes = false;
			estrStackCreate(&estrReplacementString);

			while (*pchArgs != '(')
				pchArgs++;
			pchArgs++;

			pchEndArgs = strchr(pchArgs, ')');

			// Check for an ending parenthesis
			if (!pchEndArgs)
			{
				Errorf("Error: Non-matching parenthesis: %s, %s", *estrExpr, pchErrorFilename);
				estrDestroy(&estrReplacementString);
				return false;
			}
			
			// This currently can't handle anything with nested parenthesis; abort
			if (strchr(pchArgs, '(') && strchr(pchArgs, '(') < pchEndArgs)
			{
				Errorf("Error: Nested parenthesis: %s, %s", *estrExpr, pchErrorFilename);
				estrDestroy(&estrReplacementString);
				return false;
			}

			// Parse all arguments to pass to the callback
			tmp = pchArgs;
			
			// skip leading spaces and quotes
			while (*tmp == ' ')
				tmp++;
			if (*tmp == '\"')
			{
				tmp++;
				bInQuotes = true;
			}

			while(tmp && (tmp < pchEndArgs))
			{
				char *tmp2 = tmp;
				if (bInQuotes)
				{
					// Find next quote that isn't escaped
					while (tmp2 && (tmp2 == tmp || *(tmp2-1) == '\\'))
						tmp2 = strchr(tmp2, '\"');
				}
				if (tmp2)
					tmp2 = strchr(tmp2, ',');  // Find next comma

				// if this is past the end of the function, back up to the last parenthesis
				if (!tmp2 || tmp2 > pchEndArgs)
					tmp2 = pchEndArgs;
				
				// Ignore trailing spaces and quotes
				tmp2--;
				while (*tmp2 == ' ')
					tmp2--;
				if (*tmp2 == '\"')
					tmp2--;
				tmp2++; // tmp2 == 1 past last char of argument

				if (tmp2 > tmp)
				{
					char *pchArg;
					char endChar = *tmp2;
					*tmp2 = '\0';
					pchArg = strdup(tmp);
					*tmp2 = endChar;
					eaPush(&ppParsedArgs, pchArg);
				}
				
				// Advance to next argument
				if (tmp2 && tmp2 > tmp)
					tmp = strchr(tmp2, ',');
				else
					tmp = strchr(++tmp, ',');
				bInQuotes = false;
				if (tmp)
				{
					tmp++;
					// Ignore leading spaces and quotes
					while (*tmp == ' ')
						tmp++;
					if (*tmp == '\"')
					{
						tmp++;
						bInQuotes = true;
					}
				}
			}

			// Run the callback
			if (callback)
				bRemove = callback(&ppParsedArgs, &estrReplacementString, pData);
			else
				bRemove = true;

			if (bRemove)
			{
				// Remove this Expression from the Expression EString
				pchEndArgs++;

				if (!estrReplacementString || !estrReplacementString[0])
				{
					// Try to eliminate AND or semicolons from the end of the expression
					if (!strnicmp(pchEndArgs, " AND", strlen(" AND")))
						pchEndArgs += strlen(" AND");
					else if (!strnicmp(pchEndArgs, " ;", strlen(" ;")))
						pchEndArgs += strlen(" ;");
					else if (*pchEndArgs == ';')
						pchEndArgs++;
					else if (pchNextOccurance > (*estrExpr))
					{
						while (*pchNextOccurance != ')' && pchNextOccurance > (*estrExpr))
							pchNextOccurance--;
						pchNextOccurance++;
					}
				}
				
				tmp = strdup(pchEndArgs);
				*pchNextOccurance = '\0';
				estrSetSize(estrExpr, (unsigned int)strlen(*estrExpr));
				estrAppend(estrExpr, &estrReplacementString);
				estrAppend2(estrExpr, tmp);
				pchNextOccurance = (*estrExpr) + strlen(*estrExpr) - strlen(tmp);
				free(tmp);

				bExprChanged = true;
			}
			else
			{
				pchNextOccurance++;
			}

			eaDestroyEx(&ppParsedArgs, NULL);
			estrDestroy(&estrReplacementString);
		}

		if (bExprChanged)
		{
			estrTrimLeadingAndTrailingWhitespace(estrExpr);
		}
	}
	
	return bExprChanged;
}

// Hacky data fixup utility that removes all instances of an expression function from an Expression,
// and runs a callback for each one removed.
// It can also replace the expression with another string, if one is provided by the callback.
// My parsing only works for very simple cases, so check the results carefully!
// If the Expression is empty after the fixup, it's destroyed and set to NULL.
// returns TRUE if a change was made
bool datafixup_RemoveExprFuncWithCallback(Expression **ppExpression, const char *pchFuncNameToRemove, const char *pchErrorFilename, ExprFixupRemoveFuncCB callback, void *pData)
{
	char *estrExpr = NULL;
	bool bExprChanged = false;

	if (ppExpression && *ppExpression)
	{
		Expression *pExpression = *ppExpression;
		estrStackCreate(&estrExpr);
		exprGetCompleteStringEstr(pExpression, &estrExpr);

		bExprChanged = datafixup_RemoveExprFuncFromEStringWithCallback(&estrExpr, pchFuncNameToRemove, pchErrorFilename, callback, pData);

		if (bExprChanged)
		{
			if (strlen(estrExpr)){
				exprSetOrigStrNoFilename(pExpression, estrExpr);
			}else{
				// Expression is empty now; delete it
				StructDestroy(parse_Expression, pExpression);
				*ppExpression = NULL;
			}
		}
	}
	
	estrDestroy(&estrExpr);
	return bExprChanged;
}


// ----------------------------------------------------------------------
//   Encounter Layer Fixup Utils
// ----------------------------------------------------------------------

static char **gEncLayerFiles = NULL;

static FileScanAction AssetFindEncLayerFiles(char *dir, struct _finddata32_t* data, void *pUserData)
{
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if(strEndsWith(data->name, ".encounterlayer"))
		{
			char buf[1024];
			sprintf(buf, "%s/%s", dir, data->name);
			eaPush(&gEncLayerFiles, strdup(buf));
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}


void AssetLoadEncLayersEx(EncounterLayer ***peaLayers, const char* dirName)
{
	int i;

	// Clear any previous file list
	eaDestroy(&gEncLayerFiles);

	// Find all the layer files
	fileScanAllDataDirs(dirName, AssetFindEncLayerFiles, NULL);

	for(i=eaSize(&gEncLayerFiles)-1; i>=0; --i) {
		OldEncounterMasterLayer *pMaster = StructCreate(parse_OldEncounterMasterLayer);
		if (gEncLayerFiles[i] && !strstri(gEncLayerFiles[i], "/_Test_Maps")) {
			ParserReadTextFile(gEncLayerFiles[i], parse_OldEncounterMasterLayer, pMaster, 0);
			if (eaSize(&pMaster->encLayers) > 0) {
				eaPush(peaLayers, pMaster->encLayers[0]);
				pMaster->encLayers[0] = NULL;
			}
		}
		StructDestroy(parse_OldEncounterMasterLayer, pMaster);
	}
}

void AssetLoadEncLayers(EncounterLayer ***peaLayers)
{
	AssetLoadEncLayersEx(peaLayers, "maps/");
}

void AssetPutEncLayersInEditMode(EncounterLayer ***peaLayers)
{
	int i;
	resSetDictionaryEditModeServer("Message", true);
	for (i = 0; i < eaSize(peaLayers); i++)
	{
		EncounterLayer *pLayer = (*peaLayers)[i];
		langMakeEditorCopy(parse_EncounterLayer, pLayer, false);
	}
}

void AssetSaveEncLayers(EncounterLayer ***peaLayers, EncounterLayer ***peaOrigLayerCopies, bool bCheckout)
{
	int i;

	// Ensure that peaOrigLayerCopies is valid before saving anything
	if (peaOrigLayerCopies)
	{
		if (eaSize(peaOrigLayerCopies) == eaSize(peaLayers))
		{
			for (i = 0; i < eaSize(peaLayers); i++)
			{
				if (stricmp((*peaLayers)[i]->pchFilename, (*peaOrigLayerCopies)[i]->pchFilename))
				{
					Errorf("Error: Invalid list of OrigLayerCopies passed to AssetSaveLayers");
					return;
				}
			}

			if (bCheckout) {
				// Checkout all layers
				for (i = 0; i < eaSize(peaLayers); i++)
				{
					fixupCheckoutFile((*peaLayers)[i]->pchFilename, true);
				}
			}

			// Save all layers
			for (i = 0; i < eaSize(peaLayers); i++)
			{
				EncounterLayer *pLayer = (*peaLayers)[i];
				EncounterLayer *pOrigLayerCopy = (*peaOrigLayerCopies)[i];

				if (!pLayer->bRemoveOnSave) {
					langApplyEditorCopy(parse_EncounterLayer, pLayer, pOrigLayerCopy, false, false);
					ParserWriteTextFileFromSingleDictionaryStruct(pLayer->pchFilename, g_EncounterLayerDictionary, pLayer, 0, 0);
				} else {
					// Remove the file
					char buf[260];
					int result;
					fileLocatePhysical(pLayer->pchFilename, buf);
					result = remove(buf);
					strcat(buf, ".ms");
					result = remove(buf);
				}
			}
		}
		else
		{
			Errorf("Error: Invalid list of OrigLayerCopies passed to AssetSaveLayers");
			return;
		}
	}
	else
	{
		Errorf("Warning: No list of OrigLayerCopies provided.");
		return;
	}
}

void AssetCleanupEncLayers(EncounterLayer ***peaLayers)
{
	int i;

	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		StructDestroy(parse_EncounterLayer, (*peaLayers)[i]);
	}
	eaDestroy(peaLayers);
}


// ----------------------------------------------------------------------
//   Object Library Fixup Utils
// ----------------------------------------------------------------------

static char **gObjectLibraryFiles = NULL;

static FileScanAction AssetFindObjectLibraryFiles(char *dir, struct _finddata32_t* data, void *pUserData)
{
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if(strEndsWith(data->name, ".objlib") || strEndsWith(data->name, ".rootmods"))
		{
			char buf[1024];
			sprintf(buf, "%s/%s", dir, data->name);
			eaPush(&gObjectLibraryFiles, strdup(buf));
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}


void AssetLoadObjectLibrariesEx(LibFileLoad ***peaLibraries, const char* dirName)
{
	int i;

	// Clear any previous file list
	eaDestroy(&gObjectLibraryFiles);

	// Find all the layer files
	fileScanAllDataDirs(dirName, AssetFindObjectLibraryFiles, NULL);

	for(i=eaSize(&gObjectLibraryFiles)-1; i>=0; --i) {
		LibFileLoad *pLibrary = StructCreate(parse_LibFileLoad);
		ParserReadTextFile(gObjectLibraryFiles[i], parse_LibFileLoad, pLibrary, 0);
		eaPush(peaLibraries, pLibrary);
	}
}

void AssetLoadObjectLibraries(LibFileLoad ***peaLibraries)
{
	AssetLoadObjectLibrariesEx(peaLibraries, "object_library/");
}

void AssetSaveObjectLibraries(LibFileLoad ***peaLibraries, bool bCheckout)
{
	int i;

	if (bCheckout) {
		// Checkout all layers
		for (i = 0; i < eaSize(peaLibraries); i++)
		{
			fixupCheckoutFile((*peaLibraries)[i]->defs[0]->filename, true);
		}
	}

	// Save all layers
	for (i = 0; i < eaSize(peaLibraries); i++)
	{
		LibFileLoad *pLibrary = (*peaLibraries)[i];
		ParserWriteTextFile(pLibrary->defs[0]->filename, parse_LibFileLoad, pLibrary, 0, 0);
	}
}

void AssetCleanupObjectLibraries(LibFileLoad ***peaLibraries)
{
	int i;

	for(i=eaSize(peaLibraries)-1; i>=0; --i) {
		StructDestroy(parse_LibFileLoad, (*peaLibraries)[i]);
	}
	eaDestroy(peaLibraries);
}


// ----------------------------------------------------------------------
//   Geometry Layer Fixup Utils
// ----------------------------------------------------------------------

static char **gGeoLayerFiles = NULL;

static FileScanAction AssetFindGeoLayerFiles(char *dir, struct _finddata32_t* data, void *pUserData)
{
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if(strEndsWith(data->name, ".layer"))
		{
			char buf[1024];
			sprintf(buf, "%s/%s", dir, data->name);
			eaPush(&gGeoLayerFiles, strdup(buf));
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}


void AssetLoadGeoLayersEx(LibFileLoad ***peaLayers, const char* dirName)
{
	int i;

	// Clear any previous file list
	eaDestroy(&gGeoLayerFiles);

	// Find all the layer files
	fileScanAllDataDirs(dirName, AssetFindGeoLayerFiles, NULL);

	for(i=eaSize(&gGeoLayerFiles)-1; i>=0; --i) {
		if (gGeoLayerFiles[i] && !strstri(gGeoLayerFiles[i], "/_Test_Maps")) {
			LibFileLoad *pLayer = StructCreate(parse_LibFileLoad);
			ParserReadTextFile(gGeoLayerFiles[i], parse_LibFileLoad, pLayer, 0);
			eaPush(peaLayers, pLayer);
		}
	}
}

void AssetLoadGeoLayers(LibFileLoad ***peaLayers)
{
	AssetLoadGeoLayersEx(peaLayers, "maps/");
}

void AssetPutGeoLayersInEditMode(LibFileLoad ***peaLayers)
{
	int i;
	resSetDictionaryEditModeServer("Message", true);
	for (i = 0; i < eaSize(peaLayers); i++)
	{
		LibFileLoad *pLayer = (*peaLayers)[i];
		langMakeEditorCopy(parse_LibFileLoad, pLayer, false);
	}
}

void AssetSaveGeoLayers(LibFileLoad ***peaLayers, bool bCheckout)
{
	int i;

	if (bCheckout) {
		// Checkout all layers
		for (i = 0; i < eaSize(peaLayers); i++)
		{
			fixupCheckoutFile((*peaLayers)[i]->defs[0]->filename, true);
		}
	}

	// Save all layers
	for (i = 0; i < eaSize(peaLayers); i++)
	{
		LibFileLoad *pLayer = (*peaLayers)[i];
		char buf[260];
		pLayer->filename = pLayer->defs[0]->filename;
		langApplyEditorCopySingleFile(parse_LibFileLoad, pLayer, false, false);
		ParserWriteTextFile(pLayer->defs[0]->filename, parse_LibFileLoad, pLayer, 0, 0);
		sprintf(buf, "%s.ms", pLayer->defs[0]->filename);
		ParserWriteTextFileFromDictionary(buf, "Message", 0, 0);
	}
}

void AssetCleanupGeoLayers(LibFileLoad ***peaLayers)
{
	int i;

	for(i=eaSize(peaLayers)-1; i>=0; --i) {
		StructDestroy(parse_LibFileLoad, (*peaLayers)[i]);
	}
	eaDestroy(peaLayers);
}


// ----------------------------------------------------------------------
//   Zone Fixup Utils
// ----------------------------------------------------------------------

// Defined in WorldGrid.c
//extern ParseTable parse_ZoneMap[];
//extern ParseTable zone_map_array_parseinfo[];

static char **gZoneFiles = NULL;

static FileScanAction AssetFindZoneFiles(char *dir, struct _finddata32_t* data, void *pUserData)
{
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if(strEndsWith(data->name, ".zone"))
		{
			char buf[1024];
			sprintf(buf, "%s/%s", dir, data->name);
			eaPush(&gZoneFiles, strdup(buf));
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}


void AssetLoadZonesEx(ZoneMap ***peaZones, const char* dirName)
{
	// The way this works has changed. Stephen says it's OK to leave commented out. -TomY
	// DJR: Note, ZoneMaps were split in to ZoneMapInfo and ZoneMap structs. There is a 
	// dictionary of ZoneMapInfo objects that is always loaded, though not all ZoneMapInfo
	// structs are available, unless -loadallusernamespaces is enabled. Then you can
	// traverse all the map info by iterating the ZoneMapInfo dictionary. See zmapInfoReport.
	/*
	static ZoneMap **eaZones = NULL;
	int i;

	// Clear any previous file list
	eaDestroy(&gZoneFiles);

	// Find all the zone files
	fileScanAllDataDirs(dirName, AssetFindZoneFiles, NULL);

	for(i=eaSize(&gZoneFiles)-1; i>=0; --i) {
		eaSetSize(&eaZones, 0);
		ParserReadTextFile(gZoneFiles[i], zone_map_array_parseinfo, &eaZones, 0);
		if (eaSize(&eaZones) > 0)
			eaPush(peaZones, eaZones[0]);
	}
	*/
}

void AssetLoadZones(ZoneMap ***peaZones)
{
	AssetLoadZonesEx(peaZones, "maps/");
}

void AssetSaveZones(ZoneMap ***peaZones, bool bCheckout)
{
	// The way this works has changed. Stephen says it's OK to leave commented out. -TomY
	// See comment in AssetLoadZonesEx for more info.
	/*
	static ZoneMap **eaZones = NULL;
	int i;

	if (bCheckout) {
		// Checkout all zones
		for (i = 0; i < eaSize(peaZones); i++)
		{
			ZoneMap *pZone = (*peaZones)[i];
			fixupCheckoutFile(zmapGetFilename(pZone), true);
		}
	}

	// Save all zones
	for (i = 0; i < eaSize(peaZones); i++)
	{
		ZoneMap *pZone = (*peaZones)[i];
		eaSetSize(&eaZones, 1);
		eaZones[0] = pZone;
		ParserWriteTextFile(zmapGetFilename(pZone), zone_map_array_parseinfo, &eaZones, 0, 0);
	}*/
}

void AssetCleanupZones(ZoneMap ***peaZones)
{
	// The way this works has changed. Stephen says it's OK to leave commented out. -TomY
	// See comment in AssetLoadZonesEx for more info.
	/*
	int i;

	for(i=eaSize(peaZones)-1; i>=0; --i) {
		StructDestroy(parse_ZoneMap, (*peaZones)[i]);
	}
	eaDestroy(peaZones);
	*/
}


// ----------------------------------------------------------------------
// MissionDef Fixup
// ----------------------------------------------------------------------

// This is useful for various fixup/reporting 
void missiondef_fixup_FindItemsForMissionRecursive(MissionDef *pDef, ItemDef ***pppItemDefList)
{
	int i, n;

	// Find items from Actions
	n = eaSize(&pDef->ppOnReturnActions);
	for (i = 0; i < n; i++)
	{
		WorldGameActionProperties *action = pDef->ppOnReturnActions[i];
		if (action->eActionType == WorldGameActionType_TakeItem)
		{
			ItemDef *pItemDef = GET_REF(action->pTakeItemProperties->hItemDef);
			if (pItemDef)
				eaPushUnique(pppItemDefList, pItemDef);
		}
	}

	// Find items referenced in Events
	n = eaSize(&pDef->eaTrackedEventsNoSave);
	for (i = 0; i < n; i++)
	{
		GameEvent *pEvent = pDef->eaTrackedEventsNoSave[i];
		if (pEvent->type == EventType_ItemGained)
		{
			ItemDef *pItemDef = RefSystem_ReferentFromString(g_hItemDict, pEvent->pchItemName);
			if (pItemDef)
				eaPushUnique(pppItemDefList, pItemDef);
		}
	}

	// Find items referenced in OnStart rewards
	// (doesn't scan very thoroughly but should catch the basic case)
	if (pDef->params && pDef->params->OnstartRewardTableName)
	{
		RewardTable *pRewardTable = RefSystem_ReferentFromString(g_hRewardTableDict, pDef->params->OnstartRewardTableName);
		if (pRewardTable)
		{
			n = eaSize(&pRewardTable->ppRewardEntry);
			for (i = 0; i < n; i++)
			{
				RewardEntry *pEntry = pRewardTable->ppRewardEntry[i];
				if (GET_REF(pEntry->hItemDef))
					eaPushUnique(pppItemDefList, GET_REF(pEntry->hItemDef));
			}
		}
	}
	
	// Find items referenced by any children
	n = eaSize(&pDef->subMissions);
	for (i = 0; i < n; i++)
		missiondef_fixup_FindItemsForMissionRecursive(pDef->subMissions[i], pppItemDefList);
}

static char **gMissionDefFiles = NULL;

static FileScanAction AssetFindMissionDefFiles(char *dir, struct _finddata32_t* data, void *pUserData)
{
	if(data->attrib & _A_SUBDIR)
	{
		return FSA_EXPLORE_DIRECTORY;
	}
	else
	{
		if(strEndsWith(data->name, ".mission"))
		{
			char buf[1024];
			sprintf(buf, "%s/%s", dir, data->name);
			eaPush(&gMissionDefFiles, strdup(buf));
		}
	}

	return FSA_EXPLORE_DIRECTORY;
}


void AssetLoadMissionDefsEx(MissionDef ***peaMissionDefs, const char* dirName)
{
	int i;

	// Clear any previous file list
	eaDestroyEx(&gMissionDefFiles, NULL);

	// Find all the layer files
	fileScanAllDataDirs(dirName, AssetFindMissionDefFiles, NULL);

	for(i=eaSize(&gMissionDefFiles)-1; i>=0; --i) {
		MissionDef *pDef = StructCreate(parse_MissionDef);
		ParserLoadSingleDictionaryStruct(gMissionDefFiles[i], g_MissionDictionary, pDef, 0);
		eaPush(peaMissionDefs, pDef);
	}
}

void AssetLoadMissionDefs(MissionDef ***peaMissionDefs)
{
	AssetLoadMissionDefsEx(peaMissionDefs, "defs/missions/");
}

// Disabling this for now because it doesn't work for data in underscore dirs
/*
void AssetPutMissionDefsInEditMode(MissionDef ***peaMissionDefs)
{

	int i;
	for (i = 0; i < eaSize(peaMissionDefs); i++)
	{
		MissionDef *pDef = (*peaMissionDefs)[i];
		langMakeEditorCopy(parse_MissionDef, pDef, false);
	}
}
*/

int AssetSaveMissionDefs(MissionDef ***peaMissionDefs, MissionDef ***peaOrigDefCopies, bool bCheckout)
{
	int i;
	int iNumSaved = 0;

	// Ensure that peaOrigLayerCopies is valid before saving anything
	if (peaOrigDefCopies)
	{
		if (eaSize(peaOrigDefCopies) == eaSize(peaMissionDefs))
		{
			for (i = 0; i < eaSize(peaMissionDefs); i++)
			{
				if (stricmp((*peaOrigDefCopies)[i]->filename, (*peaMissionDefs)[i]->filename))
				{
					Errorf("Error: Invalid list of peaOrigDefCopies passed to AssetSaveMissionDefs");
					return 0;
				}
			}

			if (bCheckout) {
				// Checkout all layers
				for (i = 0; i < eaSize(peaMissionDefs); i++)
				{
					fixupCheckoutFile((*peaMissionDefs)[i]->filename, true);
				}
			}

			// Save all layers
			for (i = 0; i < eaSize(peaMissionDefs); i++)
			{
				MissionDef *pDef = (*peaMissionDefs)[i];
				MissionDef *pOrigDef = (*peaOrigDefCopies)[i];

				// Disabling this for now because it doesn't work for data in underscore dirs
				//langApplyEditorCopy(parse_MissionDef, pDef, pOrigDef, false, false);
				if (ParserWriteTextFileFromSingleDictionaryStruct(pDef->filename, g_MissionDictionary, pDef, 0, 0))
					iNumSaved++;
			}
		}
		else
		{
			Errorf("Error: Invalid list of peaOrigDefCopies passed to AssetSaveMissionDefs");
			return 0;
		}
	}
	else
	{
		Errorf("Warning: No list of peaOrigDefCopies provided.");
		return 0;
	}
	return iNumSaved;
}
