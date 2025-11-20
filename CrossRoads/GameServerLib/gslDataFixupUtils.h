/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// This is a home for miscellaneous utilities related to changing game data.
// (For example, to match a data structure change.)

#ifndef GSLDATAFIXUPUTILS_H
#define GSLDATAFIXUPUTILS_H

typedef struct Expression Expression;
typedef struct EncounterLayer EncounterLayer;
typedef struct MissionDef MissionDef;
typedef struct ItemDef ItemDef;
typedef struct LibFileLoad LibFileLoad;
typedef struct ZoneMap ZoneMap;


// Return TRUE if this expression should be removed.
typedef bool (*ExprFixupRemoveFuncCB) (char*** parsedArgs, char **estrReplacementString, void *pData);

// ----------------------------------------------------------------------
//   Gimme Utils
// ----------------------------------------------------------------------

int fixupCheckoutFile(const char *pcFilename, bool bAlertErrors);


// ----------------------------------------------------------------------
//   Expression Fixup Utils
// ----------------------------------------------------------------------

// Hacky data fixup utility that removes all instances of an expression function from an Expression.
// It can also replace the expression with another string, if one is provided by the callback.
// My parsing only works for very simple cases, so check the results carefully!
// If the Expression is empty after the fixup, it's destroyed and set to NULL.
// returns TRUE if a change was made
bool datafixup_RemoveExprFuncWithCallback(Expression **ppExpression, const char *pchFuncNameToRemove, const char *pchErrorFilename, ExprFixupRemoveFuncCB callback, void *pData);
bool datafixup_RemoveExprFuncFromEStringWithCallback(char **estrExpr, const char *pchFuncNameToRemove, const char *pchErrorFilename, ExprFixupRemoveFuncCB callback, void *pData);

// ----------------------------------------------------------------------
//   Encounter Layer Fixup Utils
// ----------------------------------------------------------------------

// Loads *all* Encounter Layers for all maps to a list
void AssetLoadEncLayers(EncounterLayer ***peaLayers);

// Loads all Encounter Layers underneath a given directory to a list
void AssetLoadEncLayersEx(EncounterLayer ***peaLayers, const char* dirName);

// This prepares all the layers for editing
void AssetPutEncLayersInEditMode(EncounterLayer ***peaLayers);

// Saves all layers (option to perform checkout for you)
void AssetSaveEncLayers(EncounterLayer ***peaLayers, EncounterLayer ***peaOrigLayerCopies, bool bCheckout);

// Frees all layers
void AssetCleanupEncLayers(EncounterLayer ***peaLayers);


// ----------------------------------------------------------------------
//   Object Library Fixup Utils
// ----------------------------------------------------------------------

// Loads *all* object library files to a list
void AssetLoadObjectLibraries(LibFileLoad ***peaLibraries);

// Loads all object libraries underneath a given directory to a list
void AssetLoadObjectLibrariesEx(LibFileLoad ***peaLibraries, const char* dirName);

// Saves all object libraries (They must be checked out already)
void AssetSaveObjectLibraries(LibFileLoad ***peaLibraries, bool bCheckout);

// Frees all object libraries
void AssetCleanupObjectLibraries(LibFileLoad ***peaLibraries);


// ----------------------------------------------------------------------
//   Geometry Layer Fixup Utils
// ----------------------------------------------------------------------

// Loads *all* Geometry Layers for all maps to a list
void AssetLoadGeoLayers(LibFileLoad ***peaLayers);

// Loads all Geometry Layers underneath a given directory to a list
void AssetLoadGeoLayersEx(LibFileLoad ***peaLayers, const char* dirName);

// This prepares all the layers for editing
void AssetPutGeoLayersInEditMode(LibFileLoad ***peaLayers);

// Saves all layers (They must be checked out already)
void AssetSaveGeoLayers(LibFileLoad ***peaLayers, bool bCheckout);

// Frees all layers
void AssetCleanupGeoLayers(LibFileLoad ***peaLayers);


// ----------------------------------------------------------------------
//   Zone Fixup Utils
// ----------------------------------------------------------------------

// Loads *all* Zones for all maps to a list
void AssetLoadZones(ZoneMap ***peaZones);

// Loads all Zones underneath a given directory to a list
void AssetLoadZonesEx(ZoneMap ***peaZones, const char* dirName);

// Saves all zones (They must be checked out already)
void AssetSaveZones(ZoneMap ***peaZones, bool bCheckout);

// Frees all zones
void AssetCleanupZones(ZoneMap ***peaZones);


// ----------------------------------------------------------------------
// MissionDef Fixup Utils
// ----------------------------------------------------------------------

// Finds all ItemDefs used by the given MissionDef
void missiondef_fixup_FindItemsForMissionRecursive(MissionDef *pDef, ItemDef ***pppItemDefList);

void AssetLoadMissionDefs(MissionDef ***peaMissionDefs);
void AssetLoadMissionDefsEx(MissionDef ***peaMissionDefs, const char* dirName);
//void AssetPutMissionDefsInEditMode(MissionDef ***peaMissionDefs);
int AssetSaveMissionDefs(MissionDef ***peaMissionDefs, MissionDef ***peaOrigDefCopies, bool bCheckout);

#endif