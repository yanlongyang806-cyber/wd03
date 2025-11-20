#include <string.h>

#include "FolderCache.h"
#include "fileutil.h"
#include "timing.h"
#include "StringCache.h"

#include "WorldLib.h"
#include "wlPhysicalProperties.h"
#include "WorldColl.h"

#include "AutoGen/wlPhysicalProperties_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

PhysicalPropertiesList physical_properties;

REF_TO(PhysicalProperties) default_profile; // default.txt

// Name: findDefaultProfile
// Desc: Looks through the array for the one to be the default
// Args: No magic here
static void findDefaultProfile(void)
{
	bool ret;
	ret = RefSystem_SetHandleFromString("PhysicalProperties", "Default", &default_profile.__handle_INTERNAL);
	assert(ret);
}

// Name: initProfile
// Desc: Initializes a profile with default values
// Args: physical_properties: the structure to be initialized!
static void initProfile(PhysicalProperties *local_physical_properties)
{
	/*
	// Gets defaults from default_scene.txt
	if (physical_properties-> != default_profile) {
		StructApplyDefaults(parse_PhysicalProperties, physical_properties, default_profile, 1, 1);
	}
	*/
}

//#ifed out because it's empty. Use FIXUPFUNCs in the future
#if 0
// Name: physicalPropertiesLoadPreProcess
// Desc: Called by Parser magic to preprocess sound profile array, i.e. fill them with defaults
// Args:	pti: Magic
//			physical_properties: EArray of sound profiles
static int physicalPropertiesLoadPreProcess(ParseTable pti[], PhysicalProperties ***physical_properties_array)
{
	/*
	int i;
	findDefaultProfile();
	for (i=eaSize(physical_properties_array)-1; i>=0; i--) {
		initProfile((*physical_properties_array)[i]);
	}
	*/
	return 1;
}
#endif

// Name: buildProfileDict
// Desc: Called by physicalPropertiesLoad to build the earray into a reference dictionary for future access.
// Args: None!
void buildProfileDict(void)
{
	char buf[MAX_PATH];
	int i;
	DictionaryHandle dict = RefSystem_RegisterSelfDefiningDictionary("PhysicalProperties", 0, parse_PhysicalProperties, true, true, NULL);
	for (i=eaSize(&physical_properties.profiles)-1; i>=0; i--) {
		const char *profile_name = allocAddString(getFileNameNoExt(buf, physical_properties.profiles[i]->filename));
		physical_properties.profiles[i]->name_key = profile_name;
		RefSystem_AddReferent(dict, profile_name, physical_properties.profiles[i]);
	}
	findDefaultProfile();
}

// Name: reloadProfileSubStructCallback
// Desc: Used to initialize a physicalproperties or let it be deleted.
// Args:	substruct: the structure to place the data in when reloaded
//			oldsubstruct: the old structure being replaced
//			...: arguments used by tokenizer (i.e. Magic!)
static int reloadProfileSubStructCallback(void *substruct, void *oldsubstruct, ParseTable *tpi, eParseReloadCallbackType callback_type)
{
	if (callback_type == eParseReloadCallbackType_Delete) 
	{
		RefSystem_RemoveReferent(substruct, 0);
		return 1;
	}
	else if (callback_type == eParseReloadCallbackType_Add)
	{
		char profile_name[MAX_PATH];
		getFileNameNoExt(profile_name, ((PhysicalProperties*)substruct)->filename);
		RefSystem_AddReferent("PhysicalProperties", allocAddString(profile_name), substruct);
	}
	else if (callback_type == eParseReloadCallbackType_Update)
	{
		if (tpi == parse_PhysicalProperties) {
			initProfile(substruct);
			//JE: I don't think you want to call this here.  a) the parameters are backwards, b) It's not moved
			//RefSystem_MoveReferent(oldsubstruct, substruct);
		} else {
			assertmsg(0, "Got unknown struct type passed to reloadProfile");
		}
	}
	return 1;
}


// Name: reloadProfileCallback
// Desc: Called when a file is changed and needs to be reloaded.  (Set by FolderCacheSetCallback in physicalPropertiesLoad)
//			Reparses a file and updates the struct.
// Args:	relpath: relative path of the file (probably /*.txt)
//			when: I have no idea.  Magic.
static void reloadProfileCallback(const char *relpath, int when)
{
	fileWaitForExclusiveAccess(relpath);
	errorLogFileIsBeingReloaded(relpath);
	if (!ParserReloadFile(relpath, parse_PhysicalPropertiesList, &physical_properties.profiles, reloadProfileSubStructCallback, 0)) {
		wlStatusPrintf("Error reloading PhysicalProperties: %s", relpath);
	} else {
		wlStatusPrintf("PhysicalProperties reloaded: %s", relpath);
		wcMaterialUpdatePhysicalProperties();
	}
}

// Name: PhysicalPropertiesLoad
// Desc: Called by worldLibStartup (after utils and before materials) to load sound profiles.
//			Reads data/world/PhysicalProperties/*.txt for all sound profiles.  Loads them into
//			physical_properties.profiles.
// Args: None
void physicalPropertiesLoad(void)
{
	physical_properties.profiles = NULL;
	loadstart_printf("Loading physical profiles...");
	ParserLoadFiles("world/PhysicalProperties", ".txt", "PhysicalProperties.bin", PARSER_BINS_ARE_SHARED, 
		parse_PhysicalPropertiesList, &physical_properties.profiles);
	buildProfileDict();

	// Add callback for re-loading PhysicalProperties
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "world/PhysicalProperties/*.txt", reloadProfileCallback);
	loadend_printf(" done (%d Profiles).", eaSize(&physical_properties.profiles));
}

// Name: physicalPropertiesFindByName
// Desc: Finds a profile by name, e.g. "Metal" or "Default", etc. (see data/PhysicalProperties/)
bool physicalPropertiesFindByName(const char *profile_name, ReferenceHandle *profile_handle)
{
	RefSystem_RemoveHandle(profile_handle);
	return !profile_name || RefSystem_SetHandleFromString("PhysicalProperties", profile_name, profile_handle);
}

// Name: physicalPropertiesGetDefault
// Desc: Gets the default profile
PhysicalProperties *physicalPropertiesGetDefault(void)
{
	return GET_REF(default_profile);
}

const char* physicalPropertiesGetName(PhysicalProperties *properties)
{
	if(properties)
		return properties->countAs ? properties->countAs : properties->name_key;

	return NULL;
}