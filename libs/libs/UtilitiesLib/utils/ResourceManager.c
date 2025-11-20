/***************************************************************************



***************************************************************************/

#include "error.h"
#include "estring.h"
#include "message.h"
#include "mutex.h"
#include "ResourceManager.h"
#include "stringcache.h"
#include "ResourceSystem_Internal.h"
#include "objPath.h"
#include "FolderCache.h"
#include "fileutil.h"
#include "TextParserInheritance.h"
#include "SharedMemory.h"
#include "logging.h"
#include "timing.h"
#include "utilitiesLib.h"
#include "UGCProjectUtils.h"
#include "SharedMemory.h"

#include "ReferenceSystem_Internal.h"

#include "ResourceSystem_Internal_h_ast.h"
#include "ResourceManager_h_ast.h"
#include "textparser_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););
AUTO_RUN_ANON(memBudgetAddMapping("ResourceSystem_Internal.h", BUDGET_GameSystems););

ResourceOverlayDefs g_ResourceOverlayDefs;

bool gIsValidatingAllReferences = false;
bool gResourcesFinishedLoading = false;

static S32 sCompactStatusTables = 1;
AUTO_CMD_INT(sCompactStatusTables, CompactStatusTables) ACMD_CMDLINE;

void resDictManageValidation(DictionaryHandleOrName dictHandle, resCallback_Validate pValidateCB)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	assertmsg(pDictionary, "Invalid dictionary specified to request missing data... must be self-defining");
	
	pDictionary->pValidateCB = pValidateCB;
	pDictionary->bShouldSaveResources = true;	
}


void resDictMaintainInfoIndex(DictionaryHandleOrName dictHandle, const char *displayNamePath, const char *scopePath, const char *tagsPath, const char *notesPath, const char *iconPath)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	assert(pDictionary);

	if (pDictionary->bShouldMaintainIndex)
	{
		return;
	}
	pDictionary->bShouldMaintainIndex = true;
	pDictionary->pDisplayNamePath = strdup(displayNamePath);
	pDictionary->pScopePath = strdup(scopePath);
	pDictionary->pTagsPath = strdup(tagsPath);
	pDictionary->pNotesPath = strdup(notesPath);
	pDictionary->pIconPath = strdup(iconPath);
}

void resDictMaintainResourceIDs(DictionaryHandleOrName dictHandle, bool maintainIDs)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	assert(pDictionary);

	pDictionary->bMaintainIDs = maintainIDs;
}

bool resDictHasInfoIndex(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (pDictionary)
	{
		return pDictionary->bShouldMaintainIndex;
	}
	return false;
}

enumResDictFlags resDictGetFlags(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (pDictionary)
	{
		return pDictionary->eFlags;
	}
	return false;
}

// This function only updates the dictionary item in memory. It does not write the data back to the disk.
bool resSaveObjectInMemoryOnly(DictionaryHandle dictHandle, void * pResource, const char * pResourceName)
{
	ResourceCache *pCache = resGetCacheWithEditingLogin();
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary)
	{
		return false;
	}

	// Fix the filename
	resFixFilename(pDictionary->pDictName, pResourceName, pResource);

	if (StructInherit_GetParentName(pDictionary->pDictTable, pResource)) 
	{
		StructInherit_UpdateFromStruct(pDictionary->pDictTable, pResource, false);
		StructInherit_SetFileName(pDictionary->pDictTable, pResource, ParserGetFilename(pDictionary->pDictTable, pResource));
	}

	// Put the copy in as the working copy
	resEditStartDictionaryModification(dictHandle);
	resEditSetWorkingCopy(pDictionary, pResourceName, pResource);

	resApplyInheritanceToDictionary(dictHandle, NULL);

	resEditCommitAllModificationsIncludeNoTextSaveFieldsInComparison(dictHandle, false);

	if (pCache && pResource)
	{		
		resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);
	}

	return true;
}

// Handles a modification from the client on a locked reference
bool SaveResourceObject(DictionaryHandle dictHandle, void * pResource, const char * pResourceName, U32 userID, char **estrOut)
{
	ResourceCache *pCache = resGetCacheFromUserID(userID);
	TextParserResult result;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	void * pOrigReferent;
	const char *fileName;

	if (!pDictionary)
	{
		return false;
	}

	// Fix the filename based on name/scope rather than trusting the client
	resFixFilename(pDictionary->pDictName, pResourceName, pResource);

	if (pResource)
	{
		if ((!pDictionary->bUseAnyName && !pDictionary->bUseExtendedName && !resIsValidName(pResourceName)) ||
			(!pDictionary->bUseAnyName && pDictionary->bUseExtendedName && !resIsValidExtendedName(pResourceName)))
		{
			estrPrintf(estrOut,"User attempted to save resource name (%s) and this is not a legal name!", pResourceName);
			return false;
		}

		fileName = ParserGetFilename(pDictionary->pDictTable, pResource);

		if (!fileName)
		{
			estrPrintf(estrOut,"User attempted to save resource name (%s) with no filename!", pResourceName);
			return false;
		}
		errorLogFileIsBeingReloaded(fileName);
	}

	pOrigReferent = resGetObjectFromDict(pDictionary, pResourceName);

	if (pOrigReferent)
	{
		fileName = ParserGetFilename(pDictionary->pDictTable, pOrigReferent);

		if (fileName)
		{
			errorLogFileIsBeingReloaded(fileName);			
		}
	}


#ifndef NO_EDITORS
	// Apply any DisplayMessage structures
	langApplyEditorCopyServerSide(pDictionary->pDictTable, pResource, pOrigReferent, true);
#endif

	if (StructInherit_GetParentName(pDictionary->pDictTable, pResource)) 
	{
		StructInherit_UpdateFromStruct(pDictionary->pDictTable, pResource, false);
		StructInherit_SetFileName(pDictionary->pDictTable, pResource, ParserGetFilename(pDictionary->pDictTable, pResource));
	}

	// Put the copy in as the working copy
	resEditStartDictionaryModification(dictHandle);
	resEditSetWorkingCopy(pDictionary, pResourceName, pResource);

	resApplyInheritanceToDictionary(dictHandle, NULL);

	resEditRunValidateOnDictionary(RESVALIDATE_POST_TEXT_READING, dictHandle, userID, NULL, NULL);
	resEditRunValidateOnDictionary(RESVALIDATE_POST_BINNING, dictHandle, userID, NULL, NULL);
	resEditRunValidateOnDictionary(RESVALIDATE_FINAL_LOCATION, dictHandle, userID, NULL, NULL);

	// Have dictionary system update both the original and current files
	result = ParserWriteReferentFromDictionary(dictHandle, pResourceName, 0, 0);

	if (result)
	{
		if (resEditCommitAllModifications(dictHandle, false))
		{
			//resValidateCheckAllReferencesForDictionary(dictHandle); // Now Handled by caller
		}
		if (pCache && pResource)
		{		
			resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, RESUPDATE_NEW_RESOURCE);
		}
	}
	else
	{
		estrPrintf(estrOut, "Failed to write out resource modification for %s!", pResourceName);
		resEditRevertAllModifications(dictHandle);
	}

	return result;
}

// formats the filename into an estring
void resFormatFilename(char **estr, const char *pcBase, const char *pcScope, const char *pcName, const char *pcExtension)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	assert(estr && pcExtension);
	if (!pcName)
	{
		pcName = "NoName";
	}
	if (!pcScope)
	{
		pcScope = "";
	}

	// Format of the name is "BASE/SCOPE/NAME.EXT" unless modified by next block

	if (resExtractNameSpace(pcName, nameSpace, baseObjectName))
	// Format of the name is "ns/namespace/BASE/SCOPE/NAME.EXT"
	{
		// Format of the name is "ns/namespace/BASE/SCOPE/NAME.EXT"
		estrPrintf(estr, "ns/%s/", nameSpace);
	}

	if (pcBase)
	{
		estrConcat(estr, pcBase, (int)strlen(pcBase));
		estrConcatChar(estr, '/');
	}
	
	if (strlen(pcScope) > 0) 
	{
		char scopeNameSpace[RESOURCE_NAME_MAX_SIZE];
		char scopeBaseObjectName[RESOURCE_NAME_MAX_SIZE];
		if (resExtractNameSpace(pcScope, scopeNameSpace, scopeBaseObjectName))
		{
			estrConcat(estr, scopeBaseObjectName, (int)strlen(scopeBaseObjectName));
		}
		else
		{
			estrConcat(estr, pcScope, (int)strlen(pcScope));
		}
		estrConcatChar(estr, '/');
	}
	estrConcat(estr, baseObjectName, (int)strlen(baseObjectName));
	estrConcatChar(estr, '.');
	estrConcat(estr, pcExtension, (int)strlen(pcExtension));
	estrFixFilename(estr);
}

bool resIsValidNameEx(const char *pcName, char const ** ppcErrorInfo)
{
	const char *ptr;
	bool bNonPeriod = false;
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseName[RESOURCE_NAME_MAX_SIZE];

	if (!pcName || !pcName[0])
	{
		if (ppcErrorInfo)
		{
			*ppcErrorInfo = "Name is NULL";
		}
		return false;
	}

	if (strlen(pcName) >= RESOURCE_NAME_MAX_SIZE)
	{
		if (ppcErrorInfo)
		{
			*ppcErrorInfo = "Name too long";
		}
		return false;
	}

	if (resExtractNameSpace(pcName, nameSpace, baseName))
	{
		//In development mode we can create resources into namespaces
		//that may not be loaded yet.
		if (!resNameSpaceGetByName(nameSpace) && isProductionMode())
		{
			// Invalid name space
			if (ppcErrorInfo)
			{
				*ppcErrorInfo = "Invalid namespace";
			}
			return false;
		}
	}

	// A name cannot be empty
	// A name can only consist of [A-Za-z0-9_.-]
	// A name must start with an alphanumeric

	ptr = baseName;
	if ((*ptr < 'A' || *ptr > 'Z') &&
		(*ptr < 'a' || *ptr > 'z') &&
		(*ptr < '0' || *ptr > '9'))
	{
		if (ppcErrorInfo)
		{
			*ppcErrorInfo = "First character must be alphanumeric";
		}
		return false;
	}

	while (*ptr) {
		if ((*ptr < 'A' || *ptr > 'Z') &&
			(*ptr < 'a' || *ptr > 'z') &&
			(*ptr < '0' || *ptr > '9') &&
			(*ptr != '_') && 
			(*ptr != '-') && 
			(*ptr != '.'))
		{
			if (ppcErrorInfo)
			{
				if (*ptr == 10)
					*ppcErrorInfo = "Name contains TAB character";
				else
					*ppcErrorInfo = "Name contains invalid character";
			}
			return false;
		}
		++ptr;
	}

	return true;
}


bool resIsValidExtendedName(const char *pcName)
{
	const char *ptr;
	bool bNonPeriod = false;

	// A name cannot be empty
	// A name can only consist of [A-Za-z0-9_.-:'] and space
	// A name must start with an alphanumeric

	ptr = pcName;
	if (!ptr) {
		return false;
	}
	if ((*ptr < 'A' || *ptr > 'Z') &&
		(*ptr < 'a' || *ptr > 'z') &&
		(*ptr < '0' || *ptr > '9')) {
			return false;
	}

	while (*ptr) {
		if ((*ptr < 'A' || *ptr > 'Z') &&
			(*ptr < 'a' || *ptr > 'z') &&
			(*ptr < '0' || *ptr > '9') &&
			(*ptr != '_') && 
			(*ptr != '-') && 
			(*ptr != ':') && 
			(*ptr != '\'') && 
			(*ptr != ' ') && 
			(*ptr != '.')) {
				return false;
		}
		++ptr;
	}

	return true;
}


#define MODE_START 0
#define MODE_MIDDLE 1

bool resIsValidScope(const char *pcScope)
{
	const char *ptr;
	int mode = MODE_START;
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseName[RESOURCE_NAME_MAX_SIZE];

	if (!pcScope) {
		return true;
	}

	if (resExtractNameSpace(pcScope, nameSpace, baseName))
	{
		if (!resNameSpaceGetByName(nameSpace))
			return false;
		return resIsValidScope(baseName);
	}

	// A scope cannot start or end with a slash or space
	// A scope cannot contain consecutive slashes
	// A scope component must start with an alphanumeric or underscore
	// A scope component can only contain [A-Za-z0-9_.-] and space

	// Detect bad scope
	ptr = pcScope;
	while (true) {
		if (*ptr == '\0') {
			return true; // Got to end with no badness
		}
		if ((*ptr < 'A' || *ptr > 'Z') &&
			(*ptr < 'a' || *ptr > 'z') &&
			(*ptr < '0' || *ptr > '9') &&
			(*ptr != '_') && 
			(*ptr != '.') && 
			(*ptr != '-') && 
			(*ptr != ' ') && 
			(*ptr != '/')) {
			return false;
		}
		if (mode == MODE_START) {
			if ((*ptr < 'A' || *ptr > 'Z') &&
				(*ptr < 'a' || *ptr > 'z') &&
				(*ptr < '0' || *ptr > '9') &&
				(*ptr != '_')) { 
				return false;
			}
			mode = MODE_MIDDLE;
		}
		if (*ptr == ' ') {
			if ((*(ptr+1) == '\0') || (*(ptr+1) == '/')) {
				return false; // Trailing spaces are illegal
			}
		}
		if (*ptr == '/') {
			mode = MODE_START;
			if ((*(ptr+1) == '\0') || (*(ptr+1) == '/')) {
				return false; // Trailing slash or consecutive slashes are illegal
			}
		}
		++ptr;
	}
}

bool resIsValidExtendedScope(const char *pcScope)
{
	const char *ptr;
	int mode = MODE_START;

	if (!pcScope) {
		return true;
	}

	// A scope cannot start or end with a slash or space
	// A scope cannot contain consecutive slashes
	// A scope component must start with an alphanumeric or underscore
	// A scope component can only contain [A-Za-z0-9_.-:'] and space

	// Detect bad scope
	ptr = pcScope;
	while (true) {
		if (*ptr == '\0') {
			return true; // Got to end with no badness
		}
		if ((*ptr < 'A' || *ptr > 'Z') &&
			(*ptr < 'a' || *ptr > 'z') &&
			(*ptr < '0' || *ptr > '9') &&
			(*ptr != '_') && 
			(*ptr != '.') && 
			(*ptr != '-') && 
			(*ptr != ':') && 
			(*ptr != '\'') && 
			(*ptr != ' ') && 
			(*ptr != '/')) {
			return false;
		}
		if (mode == MODE_START) {
			if ((*ptr < 'A' || *ptr > 'Z') &&
				(*ptr < 'a' || *ptr > 'z') &&
				(*ptr < '0' || *ptr > '9') &&
				(*ptr != '_')) { 
				return false;
			}
			mode = MODE_MIDDLE;
		}
		if (*ptr == ' ') {
			if ((*(ptr+1) == '\0') || (*(ptr+1) == '/')) {
				return false; // Trailing spaces are illegal
			}
		}
		if (*ptr == '/') {
			mode = MODE_START;
			if ((*(ptr+1) == '\0') || (*(ptr+1) == '/')) {
				return false; // Trailing slash or consecutive slashes are illegal
			}
		}
		++ptr;
	}
}


// Returns false if no fix required.  Returns true if the estring is populated.
bool resFixName(const char *pcName, char **estrNewName)
{
	const char *ptr;
	bool bNonPeriod = false;
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseName[RESOURCE_NAME_MAX_SIZE];

	if (resIsValidName(pcName)) 
	{
		return false;
	}

	if (!pcName || !pcName[0] || strlen(pcName) >= RESOURCE_NAME_MAX_SIZE)
	{
		estrPrintf(estrNewName, "Empty");
		return true;
	}

	if (resExtractNameSpace(pcName, nameSpace, baseName))
	{
		estrCopy2(estrNewName, nameSpace);
		estrConcatChar(estrNewName, ':');
	}

	// A name cannot be empty
	// A name can only consist of [A-Za-z0-9_.-]
	// A name must start with an alphanumeric

	ptr = baseName;
	if ((*ptr < 'A' || *ptr > 'Z') &&
		(*ptr < 'a' || *ptr > 'z') &&
		(*ptr < '0' || *ptr > '9')) {
		estrConcatChar(estrNewName, 'A');
	}

	while (*ptr) {
		if ((*ptr < 'A' || *ptr > 'Z') &&
			(*ptr < 'a' || *ptr > 'z') &&
			(*ptr < '0' || *ptr > '9') &&
			(*ptr != '_') && 
			(*ptr != '-') && 
			(*ptr != '.')) {
			estrConcatChar(estrNewName, '_');
		} else {
			estrConcatChar(estrNewName, *ptr);
		}
		++ptr;
	}

	return true;
}


bool resFixScope(const char *pcScope, char **estrNewScope)
{
	const char *ptr;
	int mode = MODE_START;

	if (resIsValidScope(pcScope)) {
		return false;
	}

	// Get here if the scope has something bad with it so make new estring
	ptr = pcScope;
	mode = MODE_START;
	while(*ptr) {
		if (mode == MODE_START) {
			mode = MODE_MIDDLE;
			if ((*ptr < 'A' || *ptr > 'Z') &&
				(*ptr < 'a' || *ptr > 'z') &&
				(*ptr < '0' || *ptr > '9') &&
				(*ptr != '_')) { 
				estrConcatChar(estrNewScope, '_');
				ptr++;
				continue; // Replace illegal character with underscore
			}
			
		}
		if ((*ptr < 'A' || *ptr > 'Z') &&
			(*ptr < 'a' || *ptr > 'z') &&
			(*ptr < '0' || *ptr > '9') &&
			(*ptr != '_') && 
			(*ptr != '.') && 
			(*ptr != '-') && 
			(*ptr != ' ') && 
			(*ptr != '/')) {
			estrConcatChar(estrNewScope, '_');
			ptr++;
			continue; // Replace illegal character with underscore
		}
		if (*ptr == ' ') {
			if (*(ptr+1) == '\0') {
				return true; // Trailing space gets dropped
			}
			while(*(ptr+1) == ' ') {
				++ptr;
				if (*(ptr+1) == '\0') {
					return true; // Trailing spaces get dropped
				}
			}
		}
		if (*ptr == '/') {
			mode = MODE_START;
			if (*(ptr+1) == '\0') {
				return true; // Trailing slash gets dropped
			}
			while(*(ptr+1) == '/') {
				++ptr;
				if (*(ptr+1) == '\0') {
					return true; // Trailing slashes get dropped
				}
			}
		}
		estrConcatChar(estrNewScope, *ptr);
		++ptr;
	}
	return true;
}

bool resFixPooledFilename(const char **ppcFilename, const char *pcBase, const char *pcScope, const char *pcName, const char *pcExtension)
{
	char *estr = NULL;
	char *estrScope = NULL;
	bool bChanged = false;
	if (resFixScope(pcScope, &estrScope)) {
		pcScope = estrScope;
	}
	resFormatFilename(&estr, pcBase, pcScope, pcName, pcExtension);
	if (!*ppcFilename || (stricmp(estr, *ppcFilename) != 0)) {
		*ppcFilename = allocAddString(estr);
		bChanged = true;
	}
	estrDestroy(&estr);
	estrDestroy(&estrScope);
	return bChanged;
}

static int resCompareStrings(const char** left, const char** right)
{
	return stricmp(*left,*right);
}


void resGetUniqueScopes(DictionaryHandle dictHandle, char ***peaScopes)
{
	ResourceDictionaryInfo *pIndex = resDictGetInfo(dictHandle);
	int i,j;

	// Clean up old data
	eaDestroyEx(peaScopes, NULL);

	for(i=eaSize(&pIndex->ppInfos)-1; i>=0; --i) {
		ResourceInfo *pInfo = pIndex->ppInfos[i];
		if (pInfo->resourceScope) {
			bool bFound = false;
			for(j=eaSize(peaScopes)-1; j>=0; --j) {
				if (stricmp((*peaScopes)[j], pInfo->resourceScope) == 0) {
					bFound = true;
					break;
				}
			}
			if (!bFound) {
				eaPush(peaScopes, strdup(pInfo->resourceScope));
			}
		}
	}

	// Sort values
	eaQSort(*peaScopes, resCompareStrings);
}

void resNotifyRefsExistWithReason(	DictionaryHandleOrName dictHandle,
									const char *pResourceName,
									const char* reason)
{
	void *pObject;
	ResourceStatus *rs;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary)
	{
		devassertmsg(0,"Invalid dictionary handle");
		return;
	}

	if (!pDictionary->bShouldRequestMissingData)
	{
		// Don't request or keep status if dictionary doesn't care
		return;
	}
	if (pDictionary->bDictionaryBeingModified)
	{
		// Don't actually request if dictionary is being modified
		// Put it on a list of resources to check later
		eaPushUnique(&pDictionary->ppCrossReferencedResourcesDuringLoad, (char*)allocAddString(pResourceName));
		return;
	}

	pObject = resGetObjectFromDict(pDictionary, pResourceName);
	rs = resGetOrCreateStatus(pDictionary, pResourceName);
	
	rs->bRefsExist = true;

	if (!rs->bResourceRequested && !rs->bLoadedFromDisk)
	{
		rs->bResourceRequested = true;

		if (resShouldRequestMissingResources(pDictionary))
		{
			if (!rs->bResourcePresent)
			{
				if (pObject) {
					rs->bResourcePresent = true;
				} else {
					pDictionary->pSendRequestCB(pDictionary->pDictName,
												RESREQUEST_GET_RESOURCE,
												pResourceName,
												NULL,
												reason);
				}
			}
			else if (isDevelopmentMode() || resHasNamespace(pResourceName))
			{
				pDictionary->pSendRequestCB(pDictionary->pDictName,
											RESREQUEST_HAS_RESOURCE,
											pResourceName,
											NULL,
											reason);
			}
		}
	}

	if (rs->bInUnreferencedList)
	{
		RemoveResourceFromUnreferencedList(pDictionary, rs);
	}
}

void resNotifyNoRefs(	DictionaryHandleOrName dictHandle,
						const char *pResourceName,
						bool bHadRefs,
						const char* reason)
{
	void *pObject;
	ResourceStatus *rs;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	
	if (!pDictionary)
	{
		devassertmsg(0,"Invalid dictionary handle");
		return;
	}

	if (pDictionary->bDictionaryBeingModified)
	{
		return;
	}

	pObject = resGetObjectFromDict(pDictionary, pResourceName);
	rs = resGetStatus(pDictionary, pResourceName);

	if (pDictionary->bSendNoReferencesEvent && bHadRefs)
	{
		// Notify if had references and no longer does
		resDictRunEventCallbacks(pDictionary, RESEVENT_NO_REFERENCES, pResourceName, pObject);
	}

	if (!rs)
	{		
		return;
	}
	rs->bRefsExist = false;
	
	if (!rs->bResourcePresent && !rs->bLoadedFromDisk)
	{
		if (pDictionary->pSendRequestCB)
		{
			pDictionary->pSendRequestCB(pDictionary->pDictName,
										RESREQUEST_CANCEL_REQUEST,
										pResourceName,
										NULL,
										reason);
		}
		rs->bResourceRequested = 0;
	}

	if (!pObject || (rs->bResourceManaged && rs->bResourceRequested && !rs->bLoadedFromDisk))
	{
		if (pDictionary->iMaxUnreferencedResources == RES_DICT_KEEP_NONE)
		{
			resUpdateObject(pDictionary, rs, NULL);
		}
		else if (pDictionary->iMaxUnreferencedResources > 0)
		{
				AddResourceToUnreferencedList(pDictionary, rs);
		}			
	}
}

void resNotifyObjectCreated(ResourceDictionary *d,
							const char *pResourceName,
							void *pObject,
							const char* reason)
{
	ResourceStatus *rs;
	
	PERFINFO_AUTO_START_FUNC();
	
	if (!d)
	{
		devassertmsg(0,"Invalid dictionary handle");
		PERFINFO_AUTO_STOP();
		return;
	}

	if (d->bDictionaryBeingModified)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (d->pEArrayStruct)
	{
		steaIndexedAdd(&d->pEArrayStruct->ppReferents, pObject, d);
	}

	if (d->bShouldMaintainIndex)
	{
		UpdateResourceInfo(d, pResourceName);
	}

	if (!d->bShouldRequestMissingData && !d->bShouldProvideMissingData)
	{
		resDictRunEventCallbacks(d, RESEVENT_RESOURCE_ADDED, pResourceName, pObject);
		PERFINFO_AUTO_STOP();
		return;
	}

	rs = resGetOrCreateStatus(d, pResourceName);
	rs->bResourcePresent = 1;
	rs->bIsEditCopy = d->bDataEditingMode;

	resDictRunEventCallbacks(d, RESEVENT_RESOURCE_ADDED, pResourceName, pObject);

	if (d->bShouldProvideMissingData)
	{
		resServerUpdateModifiedReferentOnAllClients(d, pResourceName);
	}	

	PERFINFO_AUTO_STOP();
}

void resNotifyObjectDestroyed(	ResourceDictionary *d,
								const char *pResourceName,
								void *pObject,
								const char* reason)
{
	ResourceStatus *rs;

	if (!d)
	{
		devassertmsg(0,"Invalid dictionary handle");
		return;
	}

	if (d->bDictionaryBeingModified)
	{
		return;
	}
	
	PERFINFO_AUTO_START_FUNC();

	if (d->pEArrayStruct)
	{
		char *tempKey = NULL;
		int index;
		estrCreate(&tempKey);
		objGetKeyEString(d->pDictTable, pObject, &tempKey);
		assertmsgf(stricmp(tempKey, pResourceName) == 0, "Resource name does not match reference name. Was %s, now %s", pResourceName, tempKey);
		index = eaIndexedFind(&d->pEArrayStruct->ppReferents, pObject);
		if (index >= 0)
		{
			eaRemove(&d->pEArrayStruct->ppReferents, index);
		}
		estrDestroy(&tempKey);
	}

	resDictRunEventCallbacks(d, RESEVENT_RESOURCE_REMOVED, pResourceName, pObject);

	if (d->bShouldMaintainIndex)
	{
		UpdateResourceInfo(d, pResourceName);
	}

	rs = resGetStatus(d, pResourceName);

	if (!rs)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	
	RemoveResourceFromUnreferencedList(d, rs);

	// Clear any "referent previous" cached for this resource
	RefSystem_ClearReferentPrevious(d->pRefDictHandle, pResourceName);

	if (rs->bResourceManaged)
	{
		StructDestroyVoid(d->pDictTable, pObject);
		rs->bResourceManaged = false;
	}

	if(	rs->bResourcePresent &&
		d->pSendRequestCB &&
		!rs->bLoadedFromDisk &&
		!rs->bRefsExist)
	{
		d->pSendRequestCB(	d->pDictName,
							RESREQUEST_CANCEL_REQUEST,
							pResourceName,
							NULL,
							reason);
		rs->bResourceRequested = false;
	}

	rs->bResourcePresent = false;

	if (d->bShouldProvideMissingData)
	{
		resServerUpdateDestroyedReferentOnAllClients(d, pResourceName);
	}

	if(	!rs->iResourceLocationID &&
		!rs->iLockOwner &&
		!rs->pWorkingCopy &&
		!rs->pBackupCopy &&
		!rs->bRefsExist)
	{
		resDestroyStatus(d, pResourceName);
	}
	
	PERFINFO_AUTO_STOP();
}

static ResourceModifiedFn notificationHookForDemo;
static void* notificationHookDataForDemo;

void resSetNotificationHookForDemo(ResourceModifiedFn pFn, void* pData)
{
	notificationHookForDemo = pFn;
	notificationHookDataForDemo = pData;
}


// rs MUST be an actual resource status from pDictionary, or memory will corrupt in confusing ways
void resUpdateObject(ResourceDictionary *pDictionary, ResourceStatus *rs, void *pObject)
{
	void *pPreExistingObject;
	if (!pDictionary)
	{
		devassertmsg(0,"Invalid dictionary handle");
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	if (notificationHookForDemo)
		notificationHookForDemo(notificationHookDataForDemo, pDictionary->pDictName, rs->pResourceName,
								pObject, pDictionary->pDictTable);

	pPreExistingObject = resGetObjectFromDict(pDictionary, rs->pResourceName);
	if (pPreExistingObject)
	{
		if (pObject)
		{
			if (gLogResourceRequests && gResourcesFinishedLoading)
				filelog_printf("resourceSend.log","Update is Modifying %s %s",  pDictionary->pDictName, rs->pResourceName);

			rs->bResourceManaged = true;

			// Add will correctly delete the old one
			if (pDictionary->pAddObjectCB)
			{
				if (!pDictionary->pAddObjectCB(pDictionary, rs->pResourceName, pObject, pPreExistingObject, pDictionary->pUserData))
				{
					StructDestroyVoid(pDictionary->pDictTable, pObject);
				}
			}
			else
			{
				StructDestroyVoid(pDictionary->pDictTable, pObject);
			}
		}
		else
		{	
			if (gLogResourceRequests && gResourcesFinishedLoading)
				filelog_printf("resourceSend.log","Update is Destroying %s %s",  pDictionary->pDictName, rs->pResourceName);

			// This will end up calling resNotifyObjectDestroyed which will destroy the object if it's managed

			if (pDictionary->pRemoveObjectCB)
			{
				pDictionary->pRemoveObjectCB(pDictionary, rs->pResourceName, pPreExistingObject, pDictionary->pUserData);
			}
			else if (rs->bResourceManaged)
			{
				StructDestroyVoid(pDictionary->pDictTable, pPreExistingObject);
			}
		}
	}
	else
	{
		if (pObject)
		{

			if (gLogResourceRequests && gResourcesFinishedLoading)
				filelog_printf("resourceSend.log","Update is Creating %s %s",  pDictionary->pDictName, rs->pResourceName);

			// This will end up calling resNotifyObjectCreated
			rs->bResourceManaged = true;

			if (pDictionary->pAddObjectCB)
			{
				if (!pDictionary->pAddObjectCB(pDictionary, rs->pResourceName, pObject, NULL, pDictionary->pUserData)) // FASTLOAD
				{
					StructDestroyVoid(pDictionary->pDictTable, pObject);
				}
			}
			else
			{
				StructDestroyVoid(pDictionary->pDictTable, pObject);
			}
		}
		else if (!rs->iResourceLocationID && !rs->bResourceRequested
			&& !rs->pWorkingCopy && !rs->pBackupCopy)
		{
			if (gLogResourceRequests && gResourcesFinishedLoading)
				filelog_printf("resourceSend.log","Update is Status Destroying %s %s",  pDictionary->pDictName, rs->pResourceName);

			resDestroyStatus(pDictionary, rs->pResourceName);
		}

	}

	PERFINFO_AUTO_STOP();
}

void resUpdateObjectForDemo(DictionaryHandleOrName dictHandle, const char *pResourceName, void *pObject)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	ResourceStatus *pStatus = pObject?resGetOrCreateStatus(pDict, pResourceName):resGetStatus(pDict, pResourceName);

	if (!pDict || !pStatus)
		return;
		
	resUpdateObject(pDict, pStatus, pObject);
}

void resNotifyObjectPreModified(DictionaryHandleOrName dictHandle, const char *pResourceName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *rs;
	void *pResource;
	ReferentPrevious* rp;

	if (!pDictionary)
	{
		devassertmsg(0,"Invalid dictionary handle");
		return;
	}

	if (pDictionary->bDictionaryBeingModified)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	rs = resGetStatus(pDictionary, pResourceName);

	if (SAFE_MEMBER(rs, bIsBeingLoaded))
	{
		// This is really an Add
		PERFINFO_AUTO_STOP();
		return;
	}

	pResource = resGetObjectFromDict(pDictionary, pResourceName);

	if (pDictionary->pEArrayStruct)
	{
		int index = eaIndexedFind(&pDictionary->pEArrayStruct->ppReferents, pResource);
		if (index >= 0)
		{
			eaRemove(&pDictionary->pEArrayStruct->ppReferents, index);
		}
	}

	rp = resGetObjectPreviousFromDict(pDictionary, pResourceName);

	if(	rp &&
		!rp->referentPrevious)
	{
		rp->referentPrevious = StructCreateWithComment(	pDictionary->pDictTable,
														"Reference LastFrame copy");

		StructCopyFieldsVoid(	pDictionary->pDictTable,
								rp->referent,
								rp->referentPrevious,
								0, 0);
	}
	
	resDictRunEventCallbacks(pDictionary, RESEVENT_RESOURCE_PRE_MODIFIED, pResourceName, pResource);

	PERFINFO_AUTO_STOP();
}

void resNotifyObjectModifiedWithReason(	DictionaryHandleOrName dictHandle,
										const char *pResourceName,
										const char* reason)
{
	ResourceStatus *rs;
	ResourceDictionary *pDictionary;
	void *pResource;
	
	PERFINFO_AUTO_START_FUNC();

	pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary)
	{
		devassertmsg(0,"Invalid dictionary handle");
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pDictionary->bDictionaryBeingModified)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("get", 1);
		pResource = resGetObjectFromDict(pDictionary, pResourceName);
		rs = resGetStatus(pDictionary, pResourceName);
	PERFINFO_AUTO_STOP();
	
	if (rs && rs->bIsBeingLoaded)
	{
		// This is really an Add
		resNotifyObjectCreated(pDictionary, pResourceName, pResource, reason);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pDictionary->pEArrayStruct)
	{
		PERFINFO_AUTO_START("indexed add", 1);
		steaIndexedAdd(&pDictionary->pEArrayStruct->ppReferents, pResource, pDictionary);
		PERFINFO_AUTO_STOP();
	}

	if (pDictionary->bShouldMaintainIndex)
	{
		PERFINFO_AUTO_START("update info", 1);
		UpdateResourceInfo(pDictionary, pResourceName);		
		PERFINFO_AUTO_STOP();
	}

	if (pDictionary->bShouldProvideMissingData)
	{
		PERFINFO_AUTO_START("notify clients", 1);
		resServerUpdateModifiedReferentOnAllClients(pDictionary, pResourceName);
		PERFINFO_AUTO_STOP();
	}

	resDictRunEventCallbacks(pDictionary, RESEVENT_RESOURCE_MODIFIED, pResourceName, pResource);

	PERFINFO_AUTO_STOP();// FUNC
}


void UpdateResourceInfo(ResourceDictionary *pDictionary, const char *pResourceName)
{	
	void *pObject;
	
	if (!pDictionary->bShouldMaintainIndex)
	{
		return;
	}

	pObject = resGetObjectFromDict(pDictionary, pResourceName);

	resUpdateInfo(pDictionary->pDictName, pResourceName, pDictionary->pDictTable, pObject,
		pDictionary->pDisplayNamePath, pDictionary->pScopePath, pDictionary->pTagsPath, pDictionary->pNotesPath, pDictionary->pIconPath, true, pDictionary->bMaintainIDs);
}

void UpdatePendingResourceInfo(ResourceDictionary *pDictionary, const char *pResourceName, void *pObject)
{	
	if (!pDictionary->bShouldMaintainIndex || !pDictionary->bDictionaryBeingModified)
	{
		return;
	}

	resUpdateInfo(pDictionary->pDictName, pResourceName, pDictionary->pDictTable, pObject,
		pDictionary->pDisplayNamePath, pDictionary->pScopePath, pDictionary->pTagsPath, pDictionary->pNotesPath, pDictionary->pIconPath, true, pDictionary->bMaintainIDs);
}


void PruneUnreferencedList(ResourceDictionary *pDictionary)
{
	int numElements;

	if (pDictionary->iMaxUnreferencedResources <= 0)
		return;

	while ( (numElements = eaSize(&pDictionary->ppUnreferencedResources)) && numElements >=
		pDictionary->iMaxUnreferencedResources)
	{
		ResourceStatus *pRefInfoToRemove = pDictionary->ppUnreferencedResources[0];
		
		assert(pRefInfoToRemove->bInUnreferencedList);

		// This will remove it from unreferenced
		resUpdateObject(pDictionary, pRefInfoToRemove, NULL);

		assert(!eaSize(&pDictionary->ppUnreferencedResources) || pRefInfoToRemove != pDictionary->ppUnreferencedResources[0]); 
		// Put in to avoid infinite loops
	}
}

void AddResourceToUnreferencedList(ResourceDictionary *pDictionary, ResourceStatus *rs)
{
	int numElements;
	if (pDictionary->iMaxUnreferencedResources <= 0)
		return;

	if (gLogResourceRequests)
		filelog_printf("resourceSend.log","Added to unreferenced list %s %s",  pDictionary->pDictName, rs->pResourceName);

	while ( (numElements = eaSize(&pDictionary->ppUnreferencedResources)) && numElements >=
		pDictionary->iMaxUnreferencedResources
		&& !pDictionary->bDataEditingMode)
	{
		ResourceStatus *pRefInfoToRemove = pDictionary->ppUnreferencedResources[0];
		
		assert(pRefInfoToRemove->bInUnreferencedList);

		// This will remove it from unreferenced
		resUpdateObject(pDictionary, pRefInfoToRemove, NULL);

		assert(!eaSize(&pDictionary->ppUnreferencedResources) || pRefInfoToRemove != pDictionary->ppUnreferencedResources[0]); 
		// Put in to avoid infinite loops
	}

	ASSERT_FALSE_AND_SET(rs->bInUnreferencedList);

	eaPush(&pDictionary->ppUnreferencedResources,rs);
}

void RemoveResourceFromUnreferencedList(ResourceDictionary *pDictionary, ResourceStatus *rs)
{
	if(TRUE_THEN_RESET(rs->bInUnreferencedList)){
		if (gLogResourceRequests)
			filelog_printf("resourceSend.log","Removed from unreferenced list %s %s",  pDictionary->pDictName, rs->pResourceName);

		if(eaFindAndRemove(&pDictionary->ppUnreferencedResources,rs) < 0){
			assertmsg(0, "Resource should have been in unreferenced list");
		}
	}
	//else if(eaFind(&pDictionary->ppUnreferencedResources,rs) >= 0){
	//	assertmsg(0, "Resource should not have been in unreferenced list");
	//}
}


void resDictRegisterEventCallback(DictionaryHandleOrName dictHandle, resCallback_HandleEvent *pCB, void *pUserData)
{
	ResourceEventCallbackStruct *pCallbackStruct;
	ResourceDictionary *pDictionary;
	int i;

	pDictionary = resGetDictionary(dictHandle);
	assert(pDictionary);

	for (i = eaSize(&pDictionary->ppEventCallbacks)-1; i>= 0; i--)
	{
		pCallbackStruct = pDictionary->ppEventCallbacks[i];
		if (pCallbackStruct->pUserCallBack == pCB)
		{
			pCallbackStruct->pUserCallBackUserData = pUserData;
			return;
		}
	}

	pCallbackStruct = calloc(sizeof(ResourceEventCallbackStruct),1);
	pCallbackStruct->pUserCallBack = pCB;
	pCallbackStruct->pUserCallBackUserData = pUserData;
	eaPush(&pDictionary->ppEventCallbacks,pCallbackStruct);
}

void resDictRemoveEventCallback(DictionaryHandleOrName dictHandle, resCallback_HandleEvent *pCB)
{
	ResourceEventCallbackStruct *pCallbackStruct;
	ResourceDictionary *pDictionary;
	int i;
	
	pDictionary = resGetDictionary(dictHandle);
	assert(pDictionary);

	for (i = eaSize(&pDictionary->ppEventCallbacks)-1; i>= 0; i--)
	{
		pCallbackStruct = pDictionary->ppEventCallbacks[i];
		if (pCallbackStruct->pUserCallBack == pCB)
		{
			eaRemove(&pDictionary->ppEventCallbacks,i);
		}
	}
}


void resDictRunEventCallbacks(ResourceDictionary *pDictionary, enumResourceEventType eType,
								const char *pResourceName, void * pResource)
{
	int i, iSize;
	PERFINFO_AUTO_START_FUNC();
	iSize = eaSize(&pDictionary->ppEventCallbacks);
	for (i = 0; i < iSize; i++)
	{	
		pDictionary->ppEventCallbacks[i]->pUserCallBack(eType, pDictionary->pDictName, pResourceName, pResource, pDictionary->ppEventCallbacks[i]->pUserCallBackUserData);
	}
	PERFINFO_AUTO_STOP();
}

// Loading

static void resReloadResourcesFromDisk(FolderCache* fc, FolderNode* node, int virtual_location, const char *pchRelPath, int when, char *pDictName)
{
	ResourceDictionary *pDictionary = resGetDictionary(pDictName);
	loadstart_printf("Reloading %s...", pDictName);
	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);
	ParserReloadFileToDictionaryWithFlags(pchRelPath, pDictName, pDictionary->iParserLoadFlags);

	loadend_printf("Done.");
}

static char *resCreateOverlayPath(char **ppchDest, ResourceOverlayDef *pDef, const char *pchOverlay)
{
	char achOverlayFolderName[MAX_PATH];
	char achTempDirectory[MAX_PATH];
	char *pchFolder;

	achOverlayFolderName[0] = 0;

	estrCopy2(ppchDest, pDef->pchFolders);
	strcpy(achTempDirectory, pDef->pchFolders);
	pchFolder = strtok_quoted(achTempDirectory, ";", ";");

	do
	{
		size_t sz = strlen(pchFolder);
		// Strip trailing directory markers off the name so we can cram #blah on the end
		while (sz > 0)
		{
			sz--;
			if (pchFolder[sz] == '/' || pchFolder[sz] == '\\')
				pchFolder[sz] = '\0';
			else
				break;
		}

		sprintf(achOverlayFolderName, "%s#%s/", pchFolder, pchOverlay);
		if (ppchDest && *ppchDest && **ppchDest)
			estrConcatChar(ppchDest, ';');
		estrAppend2(ppchDest, achOverlayFolderName);
	} while (pchFolder = strtok_quoted(NULL, ";", ";"));
	return *ppchDest;
}

static char *resCreateOverlayBinFileName(char **ppchDest, ResourceOverlayDef *pDef, const char *pchOverlay)
{
	char achOverlayBinName[MAX_PATH];
	achOverlayBinName[0] = 0;
	// Generate a special filename if this is not the base dictionary.
	if (stricmp(pDef->pchBase, pchOverlay))
	{
		getFileNameNoExt(achOverlayBinName, pDef->pchBinFile);
		strcatf(achOverlayBinName, "#%s.bin", pchOverlay);
	}
	else
	{
		strcpy(achOverlayBinName, pDef->pchBinFile);
	}
	estrCopy2(ppchDest, achOverlayBinName);
	return *ppchDest;
}

static void resLoadResourcesAndOverlaysFromDisk(DictionaryHandleOrName dictHandle, const char *directory, const char *filespec, const char *manualBinFile, int flags)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);	
	ResourceOverlayDef *pDef = eaIndexedGetUsingString(&g_ResourceOverlayDefs.eaOverlayDefs, pDictionary->pDictName);
	ESet CanonicalRefDatas = NULL;
	S32 i;

	if (!pDef)
		return;

	for (i = 0; i < eaSize(&pDef->eaOverlays); i++)
	{
		char *pchFullPath = NULL;
		char *pchBinFile = NULL;
		RefDictIterator iter;
		const char *pchRefData;
		U32 j;
		resCreateOverlayPath(&pchFullPath, pDef, pDef->eaOverlays[i]);
		resCreateOverlayBinFileName(&pchBinFile, pDef, pDef->eaOverlays[i]);

		loadstart_printf("Loading overlay %s...", pDef->eaOverlays[i]);

		resLoadResourcesFromDisk(dictHandle, pchFullPath, filespec, pchBinFile, flags);

		for (j = 0; j < eSetGetMaxSize(&CanonicalRefDatas); j++)
		{
			pchRefData = eSetGetValueAtIndex(&CanonicalRefDatas, j);
			if (pchRefData && !resGetObject(dictHandle, pchRefData))
				ErrorFilenamef(pDef->pchFilename,
					"%s: Overlay %s contains %s, and overlay %s does not.",
					pDictionary->pDictName, pDef->eaOverlays[i - 1], pchRefData,
					pDef->eaOverlays[i]);
		}

		RefSystem_InitRefDictIterator(dictHandle, &iter);
		if (i == 0)
		{
			while (pchRefData = RefSystem_GetNextReferenceDataFromIterator(&iter))
				eSetAdd(&CanonicalRefDatas, pchRefData);
		}
		else
		{
			while (pchRefData = RefSystem_GetNextReferenceDataFromIterator(&iter))
			{
				if (!eSetFind(&CanonicalRefDatas, pchRefData))
					ErrorFilenamef(pDef->pchFilename, 
						"%s: Overlay %s contains %s, and overlay %s does not.",
						pDictionary->pDictName, pDef->eaOverlays[i], pchRefData,
						pDef->eaOverlays[0]);
			}
		}

		RefSystem_ClearDictionary(dictHandle, false);
		estrDestroy(&pchFullPath);
		estrDestroy(&pchBinFile);

		loadend_printf(" done.");
	}

	eSetDestroy(&CanonicalRefDatas);

	// Returning to resLoadResourcesFromDisk will load the original bin file again,
	// and make sure the main dictionary metadata gets set correctly.
}

void resLoadResourcesFromDisk(DictionaryHandleOrName dictHandle, const char *baseDirectory, const char *filespec, const char *manualBinFile, int flags)
{
	char binFile[MAX_PATH];
	const char *directory = NULL;
	char *estrDirectoryOverride = NULL, *estrDirectoryOverride2 = NULL;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);	
	int i;
	int non_namespace_elements, final_count;
	char **ppFileSpecs = NULL;

	assert(pDictionary);

	loadstart_printf("Loading %s...", pDictionary->pDictName);

	if (manualBinFile)
	{
		strcpy(binFile, manualBinFile);
	}
	else
	{	
		sprintf(binFile, "%s.bin", pDictionary->pDictName);
	}

	// Init directory to passed in value
	directory = baseDirectory;

	// If we want to load overlays, and we've got some overlays defined, handle that.
	// After handling overlays, the bin file is guaranteed to be good, so we can
	// just continue with the rest of loading normally.
	if (flags & RESOURCELOAD_USEOVERLAYS)
	{
		ResourceOverlayDef *pDef = eaIndexedGetUsingString(&g_ResourceOverlayDefs.eaOverlayDefs, pDictionary->pDictName);
		if (pDef) 
		{
			PERFINFO_AUTO_START("UseOverlays", 1);
			// Set up the def to capture the info used to load the dictionary
			flags = flags & ~RESOURCELOAD_USEOVERLAYS;
			StructFreeStringSafe(&pDef->pchBinFile);
			StructFreeStringSafe(&pDef->pchFolders);
			StructFreeStringSafe(&pDef->pchFileSpec);
			pDef->pchBinFile = StructAllocString(binFile);
			pDef->pchFolders = StructAllocString(directory);
			pDef->pchFileSpec = StructAllocString(filespec);
			pDef->eParserLoadFlags = flags;

			// When making bins, use this special function to build them
			// Not used in production
			if (isDevelopmentMode() || (gbMakeBinsAndExit && gpcMakeBinsAndExitNamespace == NULL))
			{
				resLoadResourcesAndOverlaysFromDisk(dictHandle, directory, filespec, binFile, flags);
			}

			// It will now load the main dictionary, make sure it starts with proper directory path
			resCreateOverlayPath(&estrDirectoryOverride, pDef, pDef->pchBase);
			directory = estrDirectoryOverride;
			PERFINFO_AUTO_STOP();
		}
	}

	// If ALLNS is enabled, munge the baseDirectory
	if (flags & RESOURCELOAD_ALLNS)
	{
		ResourceNameSpace *pNameSpace;
		ResourceNameSpaceIterator iterator;
		char *dir=NULL, *last=NULL;
		char tempDirectory[MAX_PATH], newDirectory[MAX_PATH], finalDirectory[MAX_PATH];

		PERFINFO_AUTO_START("AllNS", 1);
		assertmsg(!(flags & RESOURCELOAD_USERDATA), "USERDATA and ALLNS are incompatible.");
		estrCopy2(&estrDirectoryOverride2, directory);
		resNameSpaceInitIterator(&iterator);
		while (pNameSpace = resNameSpaceIteratorGetNext(&iterator))
		{
			strcpy(tempDirectory, NULL_TO_EMPTY(directory));
			dir = strtok_quoted_r(tempDirectory, ";", ";", &last);
#if !_PS3
			if (dir && (dir[0] == '/' || dir[0] == '\\')) dir++;
#endif
			
			sprintf(newDirectory, "%s:/%s", pNameSpace->pName, dir);
			if (fileLocateWrite(newDirectory, finalDirectory))
			{
				estrConcatf(&estrDirectoryOverride2, ";%s", finalDirectory);
			}

			while (dir = strtok_quoted_r(NULL, ";", ";", &last))
			{
#if !_PS3
				if (dir[0] == '/' || dir[0] == '\\') dir++;
#endif
				sprintf(newDirectory, "%s:/%s", pNameSpace->pName, dir);
				if (fileLocateWrite(newDirectory, finalDirectory))
				{
					estrConcatf(&estrDirectoryOverride2, ";%s", finalDirectory);
				}

			}
		}
		directory = estrDirectoryOverride2;
		PERFINFO_AUTO_STOP();
	}

	pDictionary->iParserLoadFlags = flags;
	if (flags & RESOURCELOAD_SHAREDMEMORY)
	{
		char* estrSharedName = NULL;

		estrStackCreateSize(&estrSharedName, MAX_PATH);
		MakeSharedMemoryName(binFile, &estrSharedName);

		if (flags & RESOURCELOAD_NO_BINS) {
			ParserLoadFilesSharedToDictionary(estrSharedName, directory, filespec, NULL, flags, dictHandle);
		} else {
			ParserLoadFilesSharedToDictionary(estrSharedName, directory, filespec, binFile, flags, dictHandle);
		}

		estrDestroy(&estrSharedName);
	}
	else
	{
		if (flags & RESOURCELOAD_NO_BINS) {
			ParserLoadFilesToDictionary(directory, filespec, NULL, flags, dictHandle);
		} else {
			ParserLoadFilesToDictionary(directory, filespec, binFile, flags, dictHandle);
		}
	}
	non_namespace_elements = resDictGetNumberOfObjects(dictHandle);
	if (isDevelopmentMode() && !(flags & PARSER_NO_RELOAD))
	{
		PERFINFO_AUTO_START("devOnly", 1);
		MakeFileSpecFromDirFilename(directory, filespec, &ppFileSpecs);
		for (i = 0; i < eaSize(&ppFileSpecs); i++)
		{
			FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, ppFileSpecs[i], resReloadResourcesFromDisk, (void *)pDictionary->pDictName);
		}
		eaDestroyEx(&ppFileSpecs, NULL);
		PERFINFO_AUTO_STOP();
	}
	if (flags & RESOURCELOAD_USERDATA)
	{
		ResourceNameSpace *pNameSpace;
		ResourceNameSpaceIterator iterator;
		PERFINFO_AUTO_START("UserData", 1);
		resNameSpaceInitIterator(&iterator);
		while (pNameSpace = resNameSpaceIteratorGetNext(&iterator))
		{
			char tempDirectory[MAX_PATH];
			char userBinFile[MAX_PATH];
			char newDirectory[MAX_PATH];
			char *fullDirectory = NULL;
			char *dir = NULL, *last=NULL;
			estrStackCreate(&fullDirectory);

			if (directory && directory[0])
			{
				strcpy(tempDirectory, directory);
				dir = strtok_quoted_r(tempDirectory, ";", ";", &last);
#if !_PS3
				if (dir && (dir[0] == '/' || dir[0] == '\\')) dir++;
#endif
				ANALYSIS_ASSUME(dir != NULL);
				fileNameSpacePath(dir, pNameSpace->pName, newDirectory);
				estrCopy2(&fullDirectory, newDirectory);

				while (dir = strtok_quoted_r(NULL, ";", ";", &last))
				{
#if !_PS3
					if (dir[0] == '/' || dir[0] == '\\') dir++;
#endif
					fileNameSpacePath(dir, pNameSpace->pName, newDirectory);
					estrConcatf(&fullDirectory,";%s", newDirectory);
									
				}
			}

			if (fullDirectory[0])
			{
				// Don't load UGC bins if we're in dev or production edit mode, unless we're in make bins and exit.
				bool bLoadBins = (isProductionMode() && !isProductionEditMode()) || gbMakeBinsAndExit;
				getFileNameNoExt(tempDirectory, binFile);

				sprintf(userBinFile, "%s:/%s.bin", pNameSpace->pName, tempDirectory);
				if (bLoadBins && resNamespaceIsUGC(userBinFile))
					bLoadBins = false; // Don't look for bin files at all in UGC namespaces

				resLoadingEnableStatusCache(dictHandle, true);
				ParserLoadFilesToDictionary(fullDirectory, filespec,
											(bLoadBins && !(flags & RESOURCELOAD_NO_BINS) ? userBinFile : NULL),
											flags | PARSER_OPTIONALFLAG, dictHandle);
				resLoadingEnableStatusCache(dictHandle, false);

				if (isDevelopmentMode())
				{
					PERFINFO_AUTO_START("devOnly", 1);
					MakeFileSpecFromDirFilename(fullDirectory, filespec, &ppFileSpecs);
					for (i = 0; i < eaSize(&ppFileSpecs); i++)
					{
						FolderCacheSetCallbackEx(FOLDER_CACHE_CALLBACK_UPDATE|FOLDER_CACHE_CALLBACK_DELETE, ppFileSpecs[i], resReloadResourcesFromDisk, (void *)pDictionary->pDictName);
					}
					eaDestroyEx(&ppFileSpecs, NULL);
					PERFINFO_AUTO_STOP();
				}
			}
			estrDestroy(&fullDirectory);

			
		}
		PERFINFO_AUTO_STOP();
	}

	estrDestroy(&estrDirectoryOverride);
	estrDestroy(&estrDirectoryOverride2);

	final_count = resDictGetNumberOfObjects(dictHandle);
	if (final_count != non_namespace_elements)
		loadend_printf(" done (%d %s, %d from namespaces).", final_count, pDictionary->pDictName, final_count-non_namespace_elements);
	else
		loadend_printf(" done (%d %s).", final_count, pDictionary->pDictName);
}

ResourceValidateState gValidateState;

void resErrorfCallback(ErrorMessage* errMsg, void *userdata)
{
	ErrorMessage *msgClone;
	char *errString;

	assertmsg(gValidateState.bIsValidating, "Can't call resErrorfCallback outside of validation");

	if (!errMsg->filename)
	{
		errMsg->filename = ParserGetFilename(gValidateState.pDictionary->pDictTable, gValidateState.pResource);
		errorFindAuthor(errMsg);
	}

	if (gValidateState.bDuringSave)
	{
		// Display all of them, but don't report any
		errMsg->bRelevant = true;
		errMsg->bReport = false;
	}

	msgClone = StructClone(parse_ErrorMessage, errMsg);
	eaPush(&gValidateState.ppErrorMessages, msgClone);
	
	errString = errorFormatErrorMessage(errMsg);
}


void resDictDependOnOtherDict(ResourceDictionary *pDictionary,  DictionaryHandleOrName targetHandle, char const * pchCause)
{
	ResourceDictionary *pTarget = resGetDictionary(targetHandle);
	int iOriginalSize;
	if (!pDictionary || !pTarget || pDictionary == pTarget)
	{
		return;
	}	

	eaPushUnique(&pDictionary->ppDictsIDependOn, pTarget->pDictName);

	iOriginalSize = eaSize(&pTarget->ppDictsDependingOnMe);
	eaPushUnique(&pTarget->ppDictsDependingOnMe, pDictionary->pDictName);
	
	// If you want to know why one dictionary depends on another, this can help you
	/*if (iOriginalSize != eaSize(&pTarget->ppDictsDependingOnMe))
	{
		printf("*#*#*#*#*#*# Reference %s caused %s to depend on %s\n",pchCause,pDictionary->pDictName,pTarget->pDictName);
	}*/
}

AUTO_COMMAND;
void WriteDictionaryDependencies(void)
{
	char fileName[MAX_PATH];	
	FILE *pOutFile;
	int j;

	sprintf(fileName, "%s/%s_dict_graph.txt", fileTempDir(), GlobalTypeToName(GetAppGlobalType()));
	pOutFile = fopen(fileName, "wt");

	if (pOutFile)
	{
		ResourceDictionary *pDictionary;
		ResourceDictionaryIterator iter;

		fprintf(pOutFile, "DiGraph G\n{\n");

		resDictInitIterator(&iter);

		while (pDictionary = resDictIteratorGetNextDictionary(&iter))
		{			
			for (j=0; j < eaSize(&pDictionary->ppDictsIDependOn); j++)
			{
				fprintf(pOutFile, "\t%s -> %s;\n", pDictionary->pDictName, pDictionary->ppDictsIDependOn[j]);
			}
		}
		fprintf(pOutFile, "}\n");
		fclose(pOutFile);
	}
}

AUTO_COMMAND ACMD_CATEGORY(DEBUG);
char *DumpResourceSystemStashTableStats(void)
{
	static char *spOutString = NULL;

	ResourceDictionary *pDictionary;
	ResourceDictionaryIterator iter;

	estrCopy2(&spOutString, "");
	estrConcatf(&spOutString, "Resource Status Counts\nFor each table, count elements, max size\r\n");

	resDictInitIterator(&iter);

	while (pDictionary = resDictIteratorGetNextDictionary(&iter))
	{
		estrConcatf(&spOutString, "%s: %u %u\r\n",
			pDictionary->pDictName,
			stashGetCount(pDictionary->resourceStatusTable), stashGetMaxSize(pDictionary->resourceStatusTable));
	}

	printf("%s", spOutString);
	return spOutString;
}

void resStartValidate(ResourceDictionary *pDictionary, const char *pResourceName, void *pResource, U32 userID, bool bDuringSave, TextParserState *tps)
{
	assertmsg(!gValidateState.bIsValidating, "Can't recursively call resStartValidate");
	gValidateState.bIsValidating = true;
	gValidateState.pDictionary = pDictionary;
	gValidateState.pResourceName = allocAddString(pResourceName);
	gValidateState.bDuringSave = bDuringSave;
	gValidateState.pResource = pResource;
	gValidateState.userID = userID;
	gValidateState.pTPS = tps;
	assertmsg(gValidateState.pDictionary, "Invalid dictionary for validation");
	ErrorfPushCallback(resErrorfCallback, NULL);
}

void resPushTextParserState(TextParserState *tps)
{
	assertmsg(!gValidateState.bIsValidating, "Can't recursively call resStartValidate");
	gValidateState.bIsValidating = true;
	gValidateState.pTPS = tps;
}

void resPopTextParserState(void)
{
	assertmsg(gValidateState.bIsValidating, "Can't stop validation without starting it");	
	gValidateState.bIsValidating = false;
	gValidateState.pDictionary = NULL;
	gValidateState.pResourceName = NULL;
	gValidateState.pResource = NULL;
	gValidateState.pTPS = NULL;
}

TextParserResult resStopValidate(void)
{
	ResourceCache *pCache = resGetCacheFromUserID(gValidateState.userID);
	TextParserResult eResult = PARSERESULT_SUCCESS;
	int i;
	assertmsg(gValidateState.bIsValidating, "Can't stop validation without starting it");	
	ErrorfPopCallback();

	for (i = 0; i < eaSize(&gValidateState.ppErrorMessages); i++)
	{
		ErrorMessage *errMsg = gValidateState.ppErrorMessages[i];

		if (errMsg->errorType == ERROR_NORMAL )
		{
			SET_ERROR_RESULT(eResult);
		}
		else if (errMsg->errorType == ERROR_DEPRECATED && gValidateState.bDuringSave)
		{
			SET_ERROR_RESULT(eResult);
		}
		else if (errMsg->errorType == ERROR_INVALID)
		{
			SET_INVALID_RESULT(eResult);
		}

		if (pCache)
		{
			resServerRequestSendResourceUpdate(gValidateState.pDictionary, gValidateState.pResourceName, errMsg, pCache, NULL, RESUPDATE_ERROR);
		}
		else
		{
			ErrorfCallCallback(errMsg);
		}

		StructDestroy(parse_ErrorMessage, errMsg);
	}
	eaClear(&gValidateState.ppErrorMessages);

	if (gValidateState.bForceNoLoad)
	{
		if (eResult == PARSERESULT_SUCCESS)
		{
			eResult = PARSERESULT_PRUNE; // This won't invalidate the bin file
		}
		else 
		{
			eResult = PARSERESULT_INVALID;
		}
	}

	gValidateState.bIsValidating = false;
	gValidateState.bForceNoLoad = false;
	gValidateState.pDictionary = NULL;
	gValidateState.pResourceName = NULL;
	gValidateState.pResource = NULL;
	gValidateState.pTPS = NULL;
	return eResult;
}

void resDoNotLoadCurrentResource(void)
{
	if (gValidateState.bIsValidating)
	{
		gValidateState.bForceNoLoad = true;
	}
}

bool resAddResourceDep(DictionaryHandleOrName dictHandle, const char *resourceName, ResourceReferenceType refType, const char *errorString)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	ResourceDictionaryInfo *pPendingInfo;
	ResourceInfo *pInfo;
	const char *locationString = resGetLocation(dictHandle, resourceName);
	if (!gValidateState.bIsValidating)
	{
		return false;
	}
	pPendingInfo = resEditGetPendingInfo(gValidateState.pDictionary->pDictName);
	if (locationString)
	{
		ParserBinAddFileDep(gValidateState.pTPS, locationString);
	}
	if (pPendingInfo)
	{
		pInfo = resGetOrCreateInfo(pPendingInfo, gValidateState.pResourceName);
		if (pInfo)
		{
			ResourceReference *pRef = resInfoGetOrCreateReference(pInfo, pDict->pDictName, resourceName, NULL, refType, errorString);
			return true;
		}
	}
	return false;
}



bool resAddFileDep(const char *filename)
{
	if (filename && gValidateState.bIsValidating)
	{
		ParserBinAddFileDep(gValidateState.pTPS, filename);
		return true;
	}
	return false;
}

bool resAddParseTableDep(ParseTable *pTable)
{
	if (gValidateState.bIsValidating)
	{
		ParserBinAddParseTableDep(gValidateState.pTPS, pTable);
		return true;
	}
	return false;
}

bool resAddParseTableNameDep(const char *pchTable)
{
	if (gValidateState.bIsValidating)
	{
		ParserBinAddParseTableNameDep(gValidateState.pTPS, pchTable);
		return true;
	}
	return false;
}

bool resAddExprFuncDep(const char *pchFuncName)
{
	if (gValidateState.bIsValidating)
	{
		ParserBinAddExprFuncDep(gValidateState.pTPS, pchFuncName);
		return true;
	}
	return false;
}

bool resAddValueDep(const char *pchValueName)
{
	if (gValidateState.bIsValidating)
	{
		ParserBinAddValueDep(gValidateState.pTPS, pchValueName);
		return true;
	}
	return false;
}

bool resValidateClientSave(DictionaryHandleOrName dictHandle, const char *pResourceName, void *pResource)
{
	TextParserResult result = PARSERESULT_SUCCESS;
	SET_MIN_RESULT(result, resRunValidate(RESVALIDATE_POST_TEXT_READING, dictHandle, pResourceName, pResource, -1, NULL));
	SET_MIN_RESULT(result, resRunValidate(RESVALIDATE_POST_BINNING, dictHandle, pResourceName, pResource, -1, NULL));
	resRunValidate(RESVALIDATE_FINAL_LOCATION, dictHandle, pResourceName, pResource, -1, NULL);

	if (result == PARSERESULT_SUCCESS)
		return true;
	return false;
}

TextParserResult resRunValidate(enumResourceValidateType eType, DictionaryHandleOrName dictHandle, const char *pResourceName, void *pResource, U32 userID, TextParserState *tps)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (!pDictionary)
	{
		return PARSERESULT_INVALID;
	}

	if (pDictionary->pValidateCB)
	{
		TextParserResult res;
		resStartValidate(pDictionary, pResourceName, pResource, userID, !!userID, tps);
		pDictionary->pValidateCB(eType, pDictionary->pDictName, pResourceName, pResource, userID);
		res = resStopValidate();
		devassertmsg((res != PARSERESULT_INVALID && res != PARSERESULT_PRUNE) || eType != RESVALIDATE_FINAL_LOCATION, "Can't call InvalidDataErrorF or DoNotLoadCurrentResource from a RESVALIDATE_FINAL_LOCATION call, it won't do the right thing");
		return res;
	}
	else
	{
		return PARSERESULT_SUCCESS;
	}
}

typedef struct ResPerfInfo
{
	PERFINFO_TYPE *perfInfo;
	char *name;
}ResPerfInfo;

ResPerfInfo resValidateTimers[RESVALIDATE_COUNT];

AUTO_RUN;
void initValidateTimerNames(void)
{
	int i;

	for(i = 0; i < RESVALIDATE_COUNT; i++)
	{
		ResPerfInfo *info = &resValidateTimers[i];
		estrPrintf(&info->name, "resValidate - %s", StaticDefineIntRevLookup(enumResourceValidateTypeEnum, i));
	}

	VerifyFlagEnums(enumParserLoadFlagsEnum, enumResourceLoadFlagsEnum);
}

TextParserResult resEditRunValidateOnDictionary(enumResourceValidateType eType, DictionaryHandleOrName dictHandle, U32 userID, void ***pppResourceArray, TextParserState *tps)
{
	TextParserResult totalResult = PARSERESULT_SUCCESS;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	StashTableIterator iterator;
	StashElement element;
	ResourceStatus *pStatus;
	assertmsg(pDictionary, "Can't edit nonexistent dictionary");
	assertmsg(pDictionary->bDictionaryBeingModified, "Can't call resEditRunValidateOnDictionary before resEditStartDictionaryModification");

	PERFINFO_AUTO_START_STATIC(resValidateTimers[eType].name, &resValidateTimers[eType].perfInfo, 1);

	if (pDictionary->bUsingLoadingTable && pDictionary->resourceStatusLoadingTable)
		stashGetIterator(pDictionary->resourceStatusLoadingTable, &iterator);
	else
		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		TextParserResult valResult;
		pStatus = stashElementGetPointer(element);
		if (pStatus->pWorkingCopy)
		{
			void *pWorkingCopy = pStatus->pWorkingCopy;
			valResult = resRunValidate(eType, dictHandle, pStatus->pResourceName, pWorkingCopy, userID, tps);
			if ((valResult == PARSERESULT_INVALID || valResult == PARSERESULT_PRUNE) && eType != RESVALIDATE_FINAL_LOCATION)
			{
				if (pppResourceArray)
				{
					int idx = eaIndexedFindUsingString(pppResourceArray, pStatus->pResourceName);
					if (idx != -1)
					{
						eaRemove(pppResourceArray,idx);
					}
				}
				resEditRevertWorkingCopy(dictHandle, pStatus->pResourceName);
				StructDestroyVoid(pDictionary->pDictTable, pWorkingCopy);
			}
			SET_MIN_RESULT(totalResult, valResult);
		}		
	}

	if (eType == RESVALIDATE_POST_TEXT_READING && pDictionary->bShouldMaintainIndex)
	{
		if (pDictionary->bUsingLoadingTable && pDictionary->resourceStatusLoadingTable)
			stashGetIterator(pDictionary->resourceStatusLoadingTable, &iterator);
		else
			stashGetIterator(pDictionary->resourceStatusTable, &iterator);

		while (stashGetNextElement(&iterator, &element))
		{
			pStatus = stashElementGetPointer(element);
			if (pStatus->pWorkingCopy)
			{
				void *pWorkingCopy = pStatus->pWorkingCopy;
				UpdatePendingResourceInfo(pDictionary, pStatus->pResourceName, pWorkingCopy);		
			}
		}
	}

	if (totalResult != PARSERESULT_SUCCESS && tps)
	{
		ParserBinHadErrors(tps);
	}

	PERFINFO_AUTO_STOP();

	return totalResult;
}

TextParserResult resRunSimpleValidateOnDictionary(enumResourceValidateType eType, DictionaryHandleOrName dictHandle)
{
	void *pResource;
	const char *pResourceName;
	TextParserResult totalResult = PARSERESULT_SUCCESS;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceIterator iterator;

	PERFINFO_AUTO_START_FUNC();
	
	resInitIterator(dictHandle, &iterator);
	while (resIteratorGetNext(&iterator, &pResourceName, &pResource))
	{
		TextParserResult valResult;
		valResult = resRunValidate(eType, dictHandle, pResourceName, pResource, 0, NULL);			
		SET_MIN_RESULT(totalResult, valResult);
	}
	resFreeIterator(&iterator);
	
	PERFINFO_AUTO_STOP();

	return totalResult;
}


void resWaitForFinishLoading(DictionaryHandleOrName dictHandle, SharedMemoryHandle *handle, bool bUnShareOnly)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	assert(pDictionary);
	if (!bUnShareOnly)
	{
		pDictionary->bShouldFinishLoading = true;
	}
	if (handle)
	{
		eaPush(&pDictionary->ppDictSharedMemory, handle);
	}
}

void resFinishLoading(void)
{
	ResourceDictionary *pDictionary;
	ResourceDictionaryIterator iter;
	int i;

	loadstart_printf("Finishing loading for all resources...");

	resDictInitIterator(&iter);

	while (pDictionary = resDictIteratorGetNextDictionary(&iter))
	{
		if (pDictionary->bShouldFinishLoading)
		{
			if (errorGetVerboseLevel())
				loadstart_printf("Finishing loading %s resources...", pDictionary->pDictName);
			resRunSimpleValidateOnDictionary(RESVALIDATE_FINAL_LOCATION, pDictionary->pDictName);
			if (errorGetVerboseLevel())
				loadend_printf(" done.");

			PERFINFO_AUTO_START("check shared memory", 1);
			pDictionary->bShouldFinishLoading = false;

			if (eaSize(&pDictionary->ppDictSharedMemory))
			{
				if (isDevelopmentMode())
				{
					ResourceIterator iterator;
					void *pResource;
					const char *pResourceName;

					resInitIterator(pDictionary->pDictName, &iterator);
					while (resIteratorGetNext(&iterator, &pResourceName, &pResource))
					{
						if (isSharedMemory(pResource) && sharedMemoryGetMode() == SMM_ENABLED)
						{						
							CheckSharedMemory(pDictionary->pDictTable, pResource);
						}
					}
					resFreeIterator(&iterator);
				}
				
				for (i = 0; i < eaSize(&pDictionary->ppDictSharedMemory); i++)
				{		
					if (sharedMemoryIsLocked(pDictionary->ppDictSharedMemory[i]))
						sharedMemoryUnlock(pDictionary->ppDictSharedMemory[i]);
				}
				eaClear(&pDictionary->ppDictSharedMemory);
			}		
			PERFINFO_AUTO_STOP();
		}
		else if (eaSize(&pDictionary->ppDictSharedMemory))
		{
			for (i = 0; i < eaSize(&pDictionary->ppDictSharedMemory); i++)
			{
				if (sharedMemoryIsLocked(pDictionary->ppDictSharedMemory[i]))
					sharedMemoryUnlock(pDictionary->ppDictSharedMemory[i]);
			}
			eaClear(&pDictionary->ppDictSharedMemory);
		}
	}

	stringCacheFinalizeShared();

	loadend_printf(" done.");

	resValidateCheckAllReferences();

	if (IsClient()) {
		// Client can accumulate requests during loading of resources.
		// Clear any leftover requests here.
		resClientCancelAnyPendingRequests();
	}

	if (isProductionMode() && !isProductionEditMode() && sCompactStatusTables)
	{
		resDeleteUnusedStatuses();
	}

	gResourcesFinishedLoading = true;
}

void resValidateResourceReferenceErrors(ResourceDictionary *pDictionary)
{	 
	ResourceDictionaryInfo *pDictInfo = pDictionary->pDictInfo;
	if (pDictInfo && pDictionary->bHasErrorToValidate)
	{
		int i, j;
		U32 errorCount = 0;
		U32 refCount = 0;
		
		PERFINFO_AUTO_START_FUNC();

		loadstart_printf(	"Validating resource reference errors for dictionary %s...",
							pDictionary->pDictName);

		for (i = 0; i < eaSize(&pDictInfo->ppInfos); i++)
		{
			ResourceInfo *pInfo = pDictInfo->ppInfos[i];
			refCount += eaSize(&pInfo->ppReferences);
			for (j = 0; j < eaSize(&pInfo->ppReferences); j++)
			{
				ResourceDictionary *pOtherDict;
				ResourceReference *pRef = pInfo->ppReferences[j];
				char ns[ RESOURCE_NAME_MAX_SIZE ];

				if (!pRef->errorString)
				{
					continue;
				}
				if (resGetInfo(pRef->resourceDict, pRef->resourceName) || RefSystem_ReferentFromString(pRef->resourceDict, pRef->resourceName))
				{
					continue;
				}
				if (resGetNumberOfInfos(pRef->resourceDict) == 0 && RefSystem_GetDictionaryNumberOfReferents(pRef->resourceDict) == 0)
				{
					// Ignore completely unloaded dictionaries, useful for loginserver
					continue;
				}
				if ((pOtherDict = resGetDictionary(pRef->resourceDict)) && pOtherDict->bShouldRequestMissingData && !pOtherDict->bReceivedAllInfos)
				{
					// Ignore things that need to be downloaded, for now
					continue;
				}
				if (!resExtractNameSpace_s(pRef->resourceName, SAFESTR(ns), NULL, 0)) {
					ns[0] = '\0';
				}
				if (strStartsWith(ns, "dyn_") && !resNameSpaceGetByName(ns))
				{
					// Ignore namespaces done for patching
					continue;
				}

				errorCount++;
				//ErrorFilenamef(pInfo->resourceLocation,"Ref Validation Failed: %s", pRef->errorString);
				ErrorFilenameGroupRetroactivef(pInfo->resourceLocation, "OwnerOnly", 7, 5, 14, 2009, "Ref Validation Failed: %s", pRef->errorString);
			}
		}	
		
		loadend_printf(	" done (%u errors, %u referents, %u references).",
						errorCount,
						eaSize(&pDictInfo->ppInfos),
						refCount);

		PERFINFO_AUTO_STOP();
	}
}


void resValidateCheckAllReferences(void)
{
	ResourceDictionary *pDictionary;
	ResourceDictionaryIterator iter;

	loadstart_printf("Validating all resources...");
	gIsValidatingAllReferences = true;

	resDictInitIterator(&iter);

	while (pDictionary = resDictIteratorGetNextDictionary(&iter))
	{
		PERFINFO_AUTO_START_STATIC(pDictionary->pDictName, &pDictionary->validateTimer, 1);
		if (pDictionary->bShouldSaveResources)
		{
			if (errorGetVerboseLevel())
				loadstart_printf("Validating %s resources...", pDictionary->pDictName);
			resRunSimpleValidateOnDictionary(RESVALIDATE_CHECK_REFERENCES, pDictionary->pDictName);
			if (!pDictionary->bShouldRequestMissingData)
			{
				resValidateResourceReferenceErrors(pDictionary);
			}
			if (errorGetVerboseLevel())
				loadend_printf(" done.");

		}
		PERFINFO_AUTO_STOP();
	}

	gIsValidatingAllReferences = false;
	loadend_printf(" done.");
}

void resValidateCheckAllReferencesForDictionary(DictionaryHandleOrName dictHandle)
{
	int i;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);

	assert(pDictionary);
	resRunSimpleValidateOnDictionary(RESVALIDATE_CHECK_REFERENCES, pDictionary->pDictName);
	if (!pDictionary->bShouldRequestMissingData)
	{
		resValidateResourceReferenceErrors(pDictionary);
	}


	for (i = 0; i < eaSize(&pDictionary->ppDictsDependingOnMe); i++)
	{
		ResourceDictionary *pOtherDict = resGetDictionary(pDictionary->ppDictsDependingOnMe[i]);
		resRunSimpleValidateOnDictionary(RESVALIDATE_CHECK_REFERENCES, pDictionary->ppDictsDependingOnMe[i]);
		if (pOtherDict && !pOtherDict->bShouldRequestMissingData)
		{
			resValidateResourceReferenceErrors(pOtherDict);
		}

	}
}

void resValidateCheckAllReferencesForDictionaries(const char **ppDictNames)
{
	const char **ppCheckNames = NULL;
	int i, j;
	for (i = 0; i < eaSize(&ppDictNames); i++)
	{
		ResourceDictionary *pDictionary = resGetDictionary(ppDictNames[i]);

		assert(pDictionary);

		eaPushUnique(&ppCheckNames, pDictionary->pDictName);

		for (j = 0; j < eaSize(&pDictionary->ppDictsDependingOnMe); j++)
		{
			ResourceDictionary *pOtherDict = resGetDictionary(pDictionary->ppDictsDependingOnMe[j]);
			if (pOtherDict)
			{
				eaPushUnique(&ppCheckNames, pDictionary->pDictName);
			}
		}
	}

	for (i = 0; i < eaSize(&ppCheckNames); i++)
	{
		ResourceDictionary *pDictionary = resGetDictionary(ppCheckNames[i]);

		resRunSimpleValidateOnDictionary(RESVALIDATE_CHECK_REFERENCES, pDictionary->pDictName);
		if (!pDictionary->bShouldRequestMissingData)
		{
			resValidateResourceReferenceErrors(pDictionary);
		}
	}

	eaDestroy(&ppCheckNames);

}


void resEditStartDictionaryModification(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	assertmsg(pDictionary, "Can't edit nonexistent dictionary");	
	pDictionary->bDictionaryBeingModified = true;
	if (!pDictionary->pPendingInfo && pDictionary->bShouldMaintainIndex)
	{
		pDictionary->pPendingInfo = StructCreate(parse_ResourceDictionaryInfo);
		pDictionary->pPendingInfo->pDictName = pDictionary->pDictName;
	}
}

static void InfoMergeCB(EArrayHandle *handle, int index, ResourceDictionary *pDictionary)
{
	ResourceInfo ***pppInfos = (ResourceInfo ***)handle;
	ResourceInfo *pNewInfo = (*pppInfos)[index];
	
	resHandleChangedInfo(pDictionary->pRefDictHandle, pNewInfo->resourceName, false);
}

bool resEditCommitAllModificationsWithReason(	DictionaryHandleOrName dictHandle,
												bool bLoadsOnly,
												bool bExcludeNoTextSaveFieldsInComparison,
												const char* reason)
{
	bool bAnyCommited = false;
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	StashTableIterator iterator;
	StashElement element;
	ResourceStatus *pStatus;
	const char **ppDeletedNames = NULL;
	int i;
	StructTypeField compareExcludeFlags = bExcludeNoTextSaveFieldsInComparison ? TOK_NO_TEXT_SAVE | TOK_NO_WRITE : TOK_NO_WRITE;
	assertmsg(pDictionary, "Can't edit nonexistent dictionary");
	assertmsg(pDictionary->bDictionaryBeingModified, "Can't call resEditCommitModifications before resEditStartDictionaryModification");

	if (pDictionary->bUsingLoadingTable && pDictionary->resourceStatusLoadingTable)
		stashGetIterator(pDictionary->resourceStatusLoadingTable, &iterator);
	else
		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		pStatus = stashElementGetPointer(element);
		if (!pStatus->pWorkingCopy && !pStatus->pBackupCopy)
		{
			continue;
		}

		if (pStatus->pWorkingCopy && pStatus->pBackupCopy &&
			StructCompare(pDictionary->pDictTable, pStatus->pWorkingCopy, pStatus->pBackupCopy, 0, 0, compareExcludeFlags) == 0)
		{
			void *dupItem = resEditRevertWorkingCopy(dictHandle, pStatus->pResourceName);
			// Same as before, ignore the new struct
			StructDestroyVoid(pDictionary->pDictTable, dupItem);
		}
		else
		{
			if (bLoadsOnly && !pStatus->pBackupCopy)
			{
				pStatus->bIsBeingLoaded = 1;
			}
			else
			{
				// Temporarily Restore pointers to old values
				resUpdateObject(pDictionary, pStatus, pStatus->pBackupCopy);
			}
		}
	}

	pDictionary->bDictionaryBeingModified = false;
	// Callbacks will happen with this next step

	if (pDictionary->bShouldMaintainIndex)
	{
		pDictionary->bTempDisableResourceInfo = 1;
		pDictionary->bShouldMaintainIndex = 0;
	}

	// Cross-references within the same dictionary get ref notifications here
	for(i=eaSize(&pDictionary->ppCrossReferencedResourcesDuringLoad)-1; i>=0; --i) {
		resNotifyRefsExistWithReason(	pDictionary->pDictName,
										pDictionary->ppCrossReferencedResourcesDuringLoad[i],
										reason);
	}
	eaDestroy(&pDictionary->ppCrossReferencedResourcesDuringLoad);

	if (pDictionary->bUsingLoadingTable && pDictionary->resourceStatusLoadingTable)
		stashGetIterator(pDictionary->resourceStatusLoadingTable, &iterator);
	else
		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		void *oldReferent, *newReferent;
		pStatus = stashElementGetPointer(element);
		if (!pStatus->pWorkingCopy && !pStatus->pBackupCopy)
		{
			continue;
		}

		oldReferent = pStatus->pBackupCopy;
		newReferent = pStatus->pWorkingCopy;
		pStatus->bSaveWorkingCopy = false;
		pStatus->bResourceManaged = true;
		pStatus->bLoadedFromDisk = true;
		pStatus->pBackupCopy = NULL;
		pStatus->pWorkingCopy = NULL;

		// This was a delete, remove the info
		if (oldReferent && !newReferent && pDictionary->bTempDisableResourceInfo)
		{
			eaPush(&ppDeletedNames, pStatus->pResourceName);			
		}

		// Make the change for real
		resUpdateObject(pDictionary, pStatus, newReferent);
		bAnyCommited = true;
		pStatus->bIsBeingLoaded = false;
	}

	if (pDictionary->bTempDisableResourceInfo)
	{
		pDictionary->bTempDisableResourceInfo = 0;
		pDictionary->bShouldMaintainIndex = 1;
		
		for (i = 0; i < eaSize(&ppDeletedNames); i++)
		{
			resUpdateInfo(pDictionary->pDictName, ppDeletedNames[i], NULL, NULL, NULL, NULL, NULL, NULL, NULL, false, false);
		}

		eaDestroy(&ppDeletedNames);

		// just overwrite for now, may be a merge eventually
		eaIndexedMerge(&pDictionary->pDictInfo->ppInfos, &pDictionary->pPendingInfo->ppInfos, 
			InfoMergeCB, pDictionary);
		
	}

	return bAnyCommited;
}

void resEditRevertAllModificationsWithReason(	DictionaryHandleOrName dictHandle,
												const char* reason)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	StashTableIterator iterator;
	StashElement element;
	ResourceStatus *rs;

	if (pDictionary->bUsingLoadingTable && pDictionary->resourceStatusLoadingTable)
		stashGetIterator(pDictionary->resourceStatusLoadingTable, &iterator);
	else
		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		rs = stashElementGetPointer(element);

		resEditRevertWorkingCopy(dictHandle, rs->pResourceName);
	}

	resEditCommitAllModificationsWithReason(dictHandle, false, true, reason);
}

bool resEditDoChangesExist(DictionaryHandleOrName dictHandle, const char *pString)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pStatus;
	if (!pDictionary || !pDictionary->bDictionaryBeingModified)
	{
		return false;
	}
	pStatus = resGetStatus(pDictionary, pString);

	if (pStatus)
	{
		return (pStatus->pWorkingCopy || pStatus->pBackupCopy);
	}
	return false;
}

void *resEditGetSaveCopy(DictionaryHandleOrName dictHandle, const char *pString)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pStatus;
	if (!pDictionary)
	{
		return NULL;
	}
	pStatus = resGetStatus(pDictionary, pString);

	if (pStatus && pDictionary->bDictionaryBeingModified)
	{
		if (!pStatus->pBackupCopy && !pStatus->pWorkingCopy)
		{
			return resGetObjectFromDict(pDictionary, pString);
		}
		if (pStatus->bSaveWorkingCopy)
		{
			return pStatus->pWorkingCopy;
		}		
		return pStatus->pBackupCopy;		
	}
	return resGetObjectFromDict(pDictionary, pString);
}

void resEditSetSaveWorkingCopy(DictionaryHandleOrName dictHandle, const char *pString, bool bSaveWorkingCopy)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pStatus;
	assertmsg(pDictionary && pDictionary->bDictionaryBeingModified, "Can't set working copies on dictionary not being modified");
	pStatus = resGetStatus(pDictionary, pString);

	if (pStatus)
	{
		pStatus->bSaveWorkingCopy = bSaveWorkingCopy;
	}
}

void * resEditGetWorkingCopy(DictionaryHandleOrName dictHandle, const char *pString)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pStatus;
	if (!pDictionary)
	{
		return NULL;
	}
	pStatus = resGetStatus(pDictionary, pString);

	if (pStatus)
	{
		return pStatus->pWorkingCopy;	
	}
	return NULL;
}


void * resEditGetBackupCopy(DictionaryHandleOrName dictHandle, const char *pString)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pStatus;
	if (!pDictionary)
	{
		return NULL;
	}
	pStatus = resGetStatus(pDictionary, pString);

	if (pStatus)
	{
		if (pStatus->pBackupCopy || pStatus->pWorkingCopy)
			return pStatus->pBackupCopy;
		return resGetObjectFromDict(pDictionary, pString);
	}
	return resGetObjectFromDict(pDictionary, pString);
}


void * resEditSetWorkingCopy(ResourceDictionary* pDictionary, const char *pString, void * pWorkingCopy)
{
	ResourceStatus *pStatus;

	PERFINFO_AUTO_START_FUNC();

	assertmsg(pDictionary && pDictionary->bDictionaryBeingModified, "Can't set working copies on dictionary not being modified");
	pStatus = resGetOrCreateStatus(pDictionary, pString);

	if (pStatus->pWorkingCopy || pStatus->pBackupCopy)
	{
		if (pStatus->pWorkingCopy != pWorkingCopy)
		{				
			resUpdateObject(pDictionary, pStatus, pWorkingCopy);
			pStatus->pWorkingCopy = pWorkingCopy;
		}		
	}
	else
	{		
		pStatus->pBackupCopy = resGetObjectFromDict(pDictionary, pString);
		resUpdateObject(pDictionary, pStatus, pWorkingCopy);
		pStatus->pWorkingCopy = pWorkingCopy;
	}

	PERFINFO_AUTO_STOP();

	return pStatus->pWorkingCopy;
}

void * resEditRevertWorkingCopy(DictionaryHandleOrName dictHandle, const char *pString)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	ResourceStatus *pStatus;
	assertmsg(pDictionary && pDictionary->bDictionaryBeingModified, "Can't set working copies on dictionary not being modified");
	pStatus = resGetStatus(pDictionary, pString);

	if (pStatus)
	{
		void * editCopy;
		if (!pStatus->pWorkingCopy && !pStatus->pBackupCopy)
		{
			return NULL;
		}

		resUpdateObject(pDictionary, pStatus, pStatus->pBackupCopy);

		editCopy = pStatus->pWorkingCopy;
		pStatus->pBackupCopy = NULL;
		pStatus->pWorkingCopy = NULL;
		pStatus->bSaveWorkingCopy = false;

		return editCopy;
	}
	return NULL;
}

ResourceDictionaryInfo *resEditGetPendingInfo(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (pDictionary && pDictionary->bDictionaryBeingModified)
	{
		return pDictionary->pPendingInfo;
	}
	return NULL;
}


// Returns if this (or parent) is an override
static bool resApplyInheritanceToResource(ResourceDictionary *pDictionary, const char *pResourceName, 
										  StashTable alreadyProcessedTable, int iRecurseDepth,
										  void ***pppResourceArray)
{	
	void *pResource;
	char *pParentName;
	void *pParent;
	int bParentOverride = false;
	int bSelfOverride = false;
	
	assertmsgf(iRecurseDepth < 16, "Probable circular inheritance in Resource Dictionary %s (object %s)", pDictionary->pDictName, 
		pResourceName);

	if (stashFindInt(alreadyProcessedTable, pResourceName, &bSelfOverride))
	{
		return bSelfOverride;
	}	

	if (!resEditDoChangesExist(pDictionary->pDictName, pResourceName))
	{
		pResource = resEditGetBackupCopy(pDictionary->pDictName, pResourceName);;		
	}
	else
	{
		bSelfOverride = true;
		pResource = resEditGetWorkingCopy(pDictionary->pDictName, pResourceName);		
	}

	if (!pResource)
	{
		return false;
	}

	pParentName = StructInherit_GetParentName(pDictionary->pDictTable, pResource);

	if (!pParentName)
	{
		stashAddInt(alreadyProcessedTable, pResourceName, bSelfOverride, false);
		return false;
	}

	resApplyInheritanceToResource(pDictionary, pParentName, alreadyProcessedTable, iRecurseDepth + 1, pppResourceArray);

	if (!resEditDoChangesExist(pDictionary->pDictName, pParentName))
	{
		pParent = resEditGetBackupCopy(pDictionary->pDictName, pParentName);;		
	}
	else
	{
		bParentOverride = true;
		pParent = resEditGetWorkingCopy(pDictionary->pDictName, pParentName);		
	}

	if (!pParent)
	{
		ErrorFilenamef(ParserGetFilename(pDictionary->pDictTable, pResource), "Couldn't find parent struct %s for struct %s in dictionary %s",
			pParentName, pResourceName,
			pDictionary->pDictName);

		stashAddInt(alreadyProcessedTable, pResourceName, false, false);

		return false;
	}

	stashAddInt(alreadyProcessedTable, pResourceName, bSelfOverride || bParentOverride, false);

	if (bSelfOverride || bParentOverride)
	{	
		TextParserResult valResult;
		if (!bSelfOverride)
		{
			pResource = StructCloneVoid(pDictionary->pDictTable, pResource);
			// Need to make a working copy
			resEditSetWorkingCopy(pDictionary, pResourceName, pResource);	
		}

		resStartValidate(pDictionary, pResourceName, pResource, 0, 0, NULL);
		StructInherit_ApplyToStruct(pDictionary->pDictTable, pResource, pParent);

		// Due to issues with redundant-name fields in inheritance data, after applying inheritance we
		//  perform an update from the struct.  This rebuilds the inheritance data, which implicitly
		//  normalizes the inheritance data field names.
		StructInherit_UpdateFromStruct(pDictionary->pDictTable, pResource, false);

		valResult = resStopValidate();
		if (valResult == PARSERESULT_INVALID || valResult == PARSERESULT_PRUNE)
		{
			if (pppResourceArray)
			{
				int idx = eaIndexedFindUsingString(pppResourceArray, pResourceName);
				if(idx >= 0)
				{
					eaRemove(pppResourceArray,idx);				
				}
			}
			resEditRevertWorkingCopy(pDictionary->pDictName, pResourceName);
			StructDestroyVoid(pDictionary->pDictTable, pResource);
		}
	}
	return bSelfOverride || bParentOverride;
}


void resApplyInheritanceToDictionary(DictionaryHandleOrName dictHandle, void ***pppResourceArray)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	StashTable alreadyProcessedTable;
	StashTableIterator iterator;
	StashElement element;

	assertmsgf(pDictionary && pDictionary->pDictTable, "Calling ApplyInheritanceToDictionary on invalid dictionary");

	if (!StructInherit_IsSupported(pDictionary->pDictTable))
	{
		return;
	}

	alreadyProcessedTable = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);

	if (pDictionary->bUsingLoadingTable && pDictionary->resourceStatusLoadingTable)
		stashGetIterator(pDictionary->resourceStatusLoadingTable, &iterator);
	else
		stashGetIterator(pDictionary->resourceStatusTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		ResourceStatus *rs = stashElementGetPointer(element);
		if (rs->bResourcePresent || rs->pWorkingCopy)
		{
			resApplyInheritanceToResource(pDictionary, rs->pResourceName, alreadyProcessedTable, 0, pppResourceArray);
		}
	}

	stashTableDestroy(alreadyProcessedTable);
}

void resLoadingEnableStatusCache(DictionaryHandleOrName dictHandle, bool bEnabled)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (!pDictionary)
		return;
	if (bEnabled)
	{
		if (!pDictionary->resourceStatusLoadingTable)
		{
			pDictionary->resourceStatusLoadingTable = stashTableCreateAddress(8);
		}
		pDictionary->bUsingLoadingTable = true;
	}
	else
	{
		if (pDictionary->resourceStatusLoadingTable)
		{
			stashTableDestroy(pDictionary->resourceStatusLoadingTable);
			pDictionary->resourceStatusLoadingTable = NULL;
		}
		pDictionary->bUsingLoadingTable = false;
	}
}

ResourceActionList *resGetResourceStatusList(void)
{
//	ResourceDictionary *pDict;
//	ResourceDictionaryIterator iter;
	ResourceActionList *pHolder = StructCreate(parse_ResourceActionList);

/*	if (gResourceBackend.CheckStatusCB)
	{	
		resDictInitIterator(&iter);

		while (pDict = resDictIteratorGetNextDictionary(&iter))
		{
			int i;
			if (stricmp(pDict->pDictName, "Message") == 0)
			{
				continue; // Skip message dictionary
			}
			for (i = 0; i < eaSize(&pDict->pDictInfo->ppInfos); i++)
			{
				ResourceInfo *pInfo = pDict->pDictInfo->ppInfos[i];
				if (pInfo->bWritable)
				{
					ResourceAction *pAction = StructCreate(parse_ResourceAction);
					StructCopy(parse_ResourceInfo, pInfo, &pAction->resInfo, 0, 0, 0);
					gResourceBackend.CheckStatusCB(pDict, &pAction->resInfo);
					if (pAction->resInfo.bNew)
					{
						pAction->resStatus = kResStatus_New;
					}
					else if (pAction->resInfo.bCheckedOut)
					{
						pAction->resStatus = kResStatus_Modified;
					}
					else
					{
						StructDestroy(parse_ResourceAction, pAction);
						continue;
					}
					eaPush(&pHolder->ppActions, pAction);
				}
			}
		}
	}*/

	return pHolder;
}

resCallback_HandleActionList *gDisplayStatusCB;
resCallback_HandleActionList *gHandleActionListCB;

void resRegisterResourceStatusDisplayCB(resCallback_HandleActionList cb)
{
	gDisplayStatusCB = cb;
}

void resRegisterHandleActionListCB(resCallback_HandleActionList cb)
{
	gHandleActionListCB = cb;
}

void resRequestResourceStatusList(const char* reason)
{
	assertmsg(areEditorsAllowed(), "Can't run dev commands in production mode");

	resClientRequestSendReferentCommand(GLOBALCOMMANDS_DICTIONARY, RESREQUEST_REQUEST_RESOURCE_STATUS, "IGNORE", NULL, reason);
}


ResourceDataBackend gResourceBackend;

HANDLE resGetResourceMutex(void)
{
	ThreadAgnosticMutex loadMutex = 0;
	char mutexNameLegal[MAX_PATH];

	makeLegalMutexName(mutexNameLegal, "resourceLoadMutex");
	loadMutex = acquireThreadAgnosticMutex(mutexNameLegal);

	return loadMutex;
}

void resReleaseResourceMutex(HANDLE loadMutex)
{
	if (loadMutex)
		releaseThreadAgnosticMutex(loadMutex);
}

void resHandleResourceActions(ResourceActionList *pHolder, U32 userID, const char *pLoginName)
{
	static const char **ppValidateDictionaries;
	int i;
	HANDLE loadMutex;

	loadMutex = resGetResourceMutex();

	eaClear(&ppValidateDictionaries);

	for (i = 0; i < eaSize(&pHolder->ppActions); i++)
	{		
		ResourceAction *pAction = pHolder->ppActions[i];
		ResourceDictionary *pDictionary = resGetDictionary(pAction->pDictName);
		ResourceInfo *pInfo = resGetInfo(pAction->pDictName, pAction->pResourceName);
		ResourceStatus *rs;
		const char *pResourceName = pAction->pResourceName;
		void *pResource = NULL;

		if (!pDictionary)
		{
			estrPrintf(&pAction->estrResultString, "Invalid Dictionary %s for action", pAction->pDictName);
			pHolder->eResult = pAction->eResult = kResResult_Failure;
			resReleaseResourceMutex(loadMutex);
			return;
		}
	
		rs = resGetStatus(pDictionary, pAction->pResourceName);

		switch (pAction->eActionType)
		{
		xcase kResAction_Open:
			{
				ResourceCache *pCache = resGetCacheFromUserID(userID);
				pResource = resGetObjectFromDict(pDictionary, pResourceName);
				if (!pResource)
				{
					estrPrintf(&pAction->estrResultString, "Resource %s %s doesn't exist", pAction->pDictName, pAction->pResourceName);
					pHolder->eResult = pAction->eResult = kResResult_Failure;
					resReleaseResourceMutex(loadMutex);
					return;
				}
				if (pCache && pDictionary->bShouldProvideMissingData)
				{
					resServerRequestSendResourceUpdate(pDictionary, pResourceName, NULL, pCache, NULL, RESUPDATE_FORCE_UPDATE);
				}
				pAction->eResult = kResResult_Success;
			}
		xcase kResAction_Check_Out:
			{
				bool bGlobalReferent = false;
				if (!estrLength(&pAction->estrActionDetails))
				{
					bGlobalReferent = true;
					pResource = resGetObjectFromDict(pDictionary, pResourceName);
				}
				else
				{				
					pResource = StructCreateVoid(pDictionary->pDictTable);

					if (!ParserReadText(pAction->estrActionDetails, pDictionary->pDictTable, pResource, PARSER_PARSECURRENTFILE))
					{
						estrPrintf(&pAction->estrResultString, "Attempted to check out invalid resource %s", pAction->pResourceName);
						pHolder->eResult = pAction->eResult = kResResult_Failure;
						if (!bGlobalReferent)
							StructDestroyVoid(pDictionary->pDictTable,pResource);
						resReleaseResourceMutex(loadMutex);
						return;
					}
				}

				if (rs)
				{
					if (rs->iLockOwner && rs->iLockOwner != userID)
					{
						estrPrintf(&pAction->estrResultString, "Attempted to check out item %s already checked out by %s", pAction->pResourceName, rs->pResourceOwner);
						pHolder->eResult = pAction->eResult = kResResult_Failure;
						if (!bGlobalReferent)
							StructDestroyVoid(pDictionary->pDictTable,pResource);
						resReleaseResourceMutex(loadMutex);
						return;
					}
				}

				// Fix the filename based on name/scope rather than trusting the client
				resFixFilename(pDictionary->pDictName, pResourceName, pResource);

				if (gResourceBackend.CheckoutCB)
				{
					// This is a bit of a cludge since userID is a nebulous concept to allow GameServers to save resources without a client logged in.
					// Made cludge better by allowing checkout to occur on the GameClient
					if ((userID != 1 || IsClient()) &&
						!gResourceBackend.CheckoutCB(pDictionary, pResource, pResourceName, &pAction->estrResultString))
					{
						pHolder->eResult = pAction->eResult = kResResult_Failure;
						if (!bGlobalReferent)
							StructDestroyVoid(pDictionary->pDictTable,pResource);
						resReleaseResourceMutex(loadMutex);
						return;
					}
				}

				// Update the info
				if (!rs)
				{
					rs = resGetOrCreateStatus(pDictionary, pResourceName);
				}
				assert(rs);
				
				resUpdateResourceLockOwner(pDictionary, pResourceName, userID);
				pAction->eResult = kResResult_Success;
				if (!bGlobalReferent)
					StructDestroyVoid(pDictionary->pDictTable,pResource);
			}
		xcase kResAction_Revert:
			{
				bool bGlobalReferent = false;

				if (!estrLength(&pAction->estrActionDetails))
				{
					bGlobalReferent = true;
					pResource = resGetObjectFromDict(pDictionary, pResourceName);
				}
				else
				{				
					pResource = StructCreateVoid(pDictionary->pDictTable);

					if (!ParserReadText(pAction->estrActionDetails, pDictionary->pDictTable, pResource, PARSER_PARSECURRENTFILE))
					{
						estrPrintf(&pAction->estrResultString, "Attempted to revert invalid resource %s", pAction->pResourceName);
						pHolder->eResult = pAction->eResult = kResResult_Failure;
						if (!bGlobalReferent)
							StructDestroyVoid(pDictionary->pDictTable,pResource);
						resReleaseResourceMutex(loadMutex);
						return;
					}
				}

				if (!pResource)
				{
					// Silently succeed
					pAction->eResult = kResResult_Success;
					resReleaseResourceMutex(loadMutex);
					break;
				}

				resFixFilename(pDictionary->pDictName, pResourceName, pResource);

				if (gResourceBackend.UndoCheckoutCB)
				{
					if (!gResourceBackend.UndoCheckoutCB(pDictionary, pResource, pResourceName, &pAction->estrResultString))
					{
						pHolder->eResult = pAction->eResult = kResResult_Failure;
						if (!bGlobalReferent)
							StructDestroyVoid(pDictionary->pDictTable,pResource);
						resReleaseResourceMutex(loadMutex);
						return;
					}
				}

				resUpdateResourceLockOwner(pDictionary, pResourceName, 0);

				pAction->eResult = kResResult_Success;
				if (!bGlobalReferent)
					StructDestroyVoid(pDictionary->pDictTable,pResource);				
			}		
		xcase kResAction_Modify:
			{
				if (!estrLength(&pAction->estrActionDetails))
				{				
					pResource = NULL;
				}
				else
				{				
					pResource = StructCreateVoid(pDictionary->pDictTable);

					if (!ParserReadText(pAction->estrActionDetails, pDictionary->pDictTable, pResource, PARSER_PARSECURRENTFILE))
					{
						estrPrintf(&pAction->estrResultString, "Attempted to modify invalid resource %s", pAction->pResourceName);
						pHolder->eResult = pAction->eResult = kResResult_Failure;
						StructDestroyVoid(pDictionary->pDictTable,pResource);
						resReleaseResourceMutex(loadMutex);
						return;
					}
				}

				if (!rs || rs->iLockOwner != userID)
				{
					estrPrintf(&pAction->estrResultString,"User attempted to modify data (%s) in dictionary %s they don't have locked!", pResourceName, pAction->pDictName);
					pHolder->eResult = pAction->eResult = kResResult_Failure;
					if (pResource)				
						StructDestroyVoid(pDictionary->pDictTable,pResource);
					resReleaseResourceMutex(loadMutex);
					return;
				}	
				if (SaveResourceObject(pDictionary->pDictName, pResource, pResourceName, userID, &pAction->estrResultString))
				{		
					eaPushUnique(&ppValidateDictionaries,pDictionary->pDictName);
					pAction->eResult = kResResult_Success;
				}
				else
				{
					// Could possibly leak in certain cases
					pHolder->eResult = pAction->eResult = kResResult_Failure;
					resReleaseResourceMutex(loadMutex);
					return;
				}
			}
		}
	}
	// If we got this far, all succeeded

	if (eaSize(&ppValidateDictionaries) && !pHolder->bDisableValidation)
	{
		resValidateCheckAllReferencesForDictionaries(ppValidateDictionaries);
	}
	pHolder->eResult = kResResult_Success;

	resReleaseResourceMutex(loadMutex);
}

void resRequestResourceActions(ResourceActionList *pHolder)
{
	int i;
	bool bFoundRemote = false;
	bool bFoundLocal = false;

	pHolder->eResult = kResResult_None;

	for (i = 0; i < eaSize(&pHolder->ppActions); i++)
	{
		ResourceAction *pAction = pHolder->ppActions[i];
		ResourceDictionary *pDictionary = resGetDictionary(pAction->pDictName);
		pAction->eResult = kResResult_None;
		estrClear(&pAction->estrResultString);

		if (!pDictionary || !pDictionary->bDataEditingMode)
		{
			Errorf("Can't edit data from dictionary %s without setting it to editing mode!",pAction->pDictName);
			return;
		}

		if (pDictionary->bLocalEditingOverride)
		{
			bFoundLocal = true;
		}
		else if (pDictionary && pDictionary->bShouldRequestMissingData)
		{
			bFoundRemote = true;
		}
		else if (pDictionary && !pDictionary->bShouldRequestMissingData)
		{
			bFoundLocal = true;
		}
	}

	if (bFoundLocal)
	{
		ResourceCache *pCache;
		if (bFoundRemote)
		{
			Errorf("Combining resource actions on local and remote dictionaries is not currently allowed!");
			return;
		}
		pCache = resGetCacheWithEditingLogin();
		resHandleResourceActions(pHolder, pCache?pCache->userID:1, NULL);

		if (gHandleActionListCB)
		{
			gHandleActionListCB(pHolder);
		}
	}
	else if (bFoundRemote)
	{
		resClientRequestSendReferentCommand(GLOBALCOMMANDS_DICTIONARY,
											RESREQUEST_APPLY_RESOURCE_ACTIONS,
											"IGNORE",
											pHolder,
											__FUNCTION__);
	}
	else
	{
		//Empty. Should it error?
	}	

}

void resRegisterPreContainerSendCB(DictionaryHandleOrName dictHandle, resCallback_PreContainerSend *cb)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	assert(pDict);
	pDict->pPreContainerSendCB = cb;
}

void resRegisterSimpleEditCB(DictionaryHandleOrName dictHandle, U32 requestTypeBits, resCallback_HandleSimpleEdit* cb)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	assert(pDict);
	if (!cb)
	{
		cb = NULL; //replace with default callback eventually
	}
	if ( requestTypeBits & kResEditType_CheckOut)
	{
		pDict->pSimpleCheckOutCB = cb;
	}
	if ( requestTypeBits & kResEditType_Revert)
	{
		pDict->pSimpleRevertCB = cb;
	}
	if ( requestTypeBits & kResEditType_EditTags)
	{
		pDict->pSimpleTagEditCB = cb;
	}
}

// Returns the callback for a specified request type and 
resCallback_HandleSimpleEdit *resGetSimpleEditCB(DictionaryHandleOrName dictHandle, ResourceSimpleEditRequest eRequestType)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return NULL;
	}
	if ( eRequestType == kResEditType_CheckOut)
	{
		return pDict->pSimpleCheckOutCB;
	}
	if ( eRequestType == kResEditType_Revert)
	{
		return pDict->pSimpleRevertCB;
	}
	if ( eRequestType == kResEditType_EditTags)
	{
		return pDict->pSimpleTagEditCB;
	}
	return NULL;
}

AUTO_STARTUP(ResourceOverlayDef);
void ResourceOverlayDefLoad(void)
{
	loadstart_printf("Loading ResourceOverlayDefs...");
	ParserLoadFiles("defs;ui", ".resoverlay", "ResourceOverlay.bin", PARSER_OPTIONALFLAG, parse_ResourceOverlayDefs, &g_ResourceOverlayDefs);
	loadend_printf("done. (%d overlays)", eaSize(&g_ResourceOverlayDefs.eaOverlayDefs));
}

bool ResourceOverlayExists(const char *pchDictName, const char *pchOverlay)
{
	ResourceDictionary *pDictionary = resGetDictionary(pchDictName);	
	ResourceOverlayDef *pDef = pDictionary ? eaIndexedGetUsingString(&g_ResourceOverlayDefs.eaOverlayDefs, pDictionary->pDictName) : NULL;

	return (pDef != NULL);
}

void ResourceOverlayNames(const char *pchDictName, const char ***ppchOverlayNames)
{
	ResourceDictionary *pDictionary = resGetDictionary(pchDictName);	
	ResourceOverlayDef *pDef = pDictionary ? eaIndexedGetUsingString(&g_ResourceOverlayDefs.eaOverlayDefs, pDictionary->pDictName) : NULL;

	eaClear(ppchOverlayNames);

	if (pDef != NULL)
	{
		eaPushEArray(ppchOverlayNames, &pDef->eaOverlays);
	}
}

const char *ResourceOverlayBaseName(const char *pchDictName)
{
	ResourceDictionary *pDictionary = resGetDictionary(pchDictName);	
	ResourceOverlayDef *pDef = pDictionary ? eaIndexedGetUsingString(&g_ResourceOverlayDefs.eaOverlayDefs, pDictionary->pDictName) : NULL;

	if (pDef != NULL)
	{
		return pDef->pchBase;
	}

	return NULL;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(ResourceOverlayLoad);
void ResourceOverlayLoad(const char *pchDictName, const char *pchOverlay)
{
	ResourceDictionary *pDictionary = resGetDictionary(pchDictName);	
	ResourceOverlayDef *pDef = pDictionary ? eaIndexedGetUsingString(&g_ResourceOverlayDefs.eaOverlayDefs, pDictionary->pDictName) : NULL;
	if (pDef)
	{
		const char *pchPoolOverlay = allocFindString(pchOverlay);
		bool bFound = eaFind(&pDef->eaOverlays, pchPoolOverlay) >= 0;
		if (bFound)
		{
			char *pchFullPath = NULL;
			char *pchBinFile = NULL;
			resCreateOverlayPath(&pchFullPath, pDef, pchOverlay);
			resCreateOverlayBinFileName(&pchBinFile, pDef, pchOverlay);
			RefSystem_ClearDictionary(pchDictName, false);
			resLoadResourcesFromDisk(pchDictName, pchFullPath, pDef->pchFileSpec, pchBinFile, pDef->eParserLoadFlags);
			estrDestroy(&pchFullPath);
			estrDestroy(&pchBinFile);
		}
		else
			ErrorFilenamef(pDef->pchFilename, "%s: No dictionary overlay %s found.", pDictionary->pDictName, pchOverlay);

	}
	else if (!pDictionary)
	{
		Errorf("%s: Dictionary not found.", pchDictName);
	}
	else if (!pDef)
	{
		Errorf("%s: Dictionary has no overlays.", pDictionary->pDictName);
	}
}

void DEFAULT_LATELINK_resDictGetMissingResourceFromResourceDBIfPossible(void *dictNameOrHandle)
{

}

void resClearAllDictionaryEditModes(void)
{
	ResourceDictionaryIterator resDictIterator = {0};
	ResourceDictionary *pDict;

	resDictInitIterator(&resDictIterator);

	while ((pDict = resDictIteratorGetNextDictionary(&resDictIterator)))
	{
		if (resIsDictionaryEditMode(pDict->pDictName))
		{
			resSetDictionaryEditMode(pDict->pDictName, false);
		}
	}
}


void resDictSetHTMLCommentCallback(DictionaryHandleOrName dictHandle, resCallback_GetHTMLCommentString *pGetHTMLCommentCB)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return;
	}

	pDict->pGetHTMLCommentCB = pGetHTMLCommentCB;
}

void resDictSetHTMLExtraCommand(DictionaryHandleOrName dictHandle, char *pCommandName, char *pCommandString)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return;
	}

	SAFE_FREE(pDict->pExtraServerMonCommandName);
	SAFE_FREE(pDict->pExtraServerMonCommandString);

	pDict->pExtraServerMonCommandName = strdup(pCommandName);
	pDict->pExtraServerMonCommandString = strdup(pCommandString);
}
	

void resDictSetSendNoReferencesCallback(DictionaryHandleOrName dictHandle, bool bFlag)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return;
	}

	pDict->bSendNoReferencesEvent = bFlag;
}


char *resDictGetHTMLCommentString(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return NULL;
	}

	if (!pDict->pGetHTMLCommentCB)
	{
		return NULL;
	}

	return pDict->pGetHTMLCommentCB(pDict);
}

void resDictGetHTMLExtraCommand(DictionaryHandleOrName dictHandle, char **ppOutCombinedCmdString)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return;
	}

	if (pDict->pExtraServerMonCommandName)
	{
		estrPrintf(ppOutCombinedCmdString, "%s $COMMANDNAME(%s)", 
			pDict->pExtraServerMonCommandString, pDict->pExtraServerMonCommandName);
	}
}


char *resDictGetHTMLExtraLink(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return NULL;
	}

	return pDict->pHTMLExtraLink;
}

void resDictSetHTMLExtraLink(DictionaryHandleOrName dictHandle, FORMAT_STR const char *pFmt, ...)
{
	ResourceDictionary *pDict = resGetDictionary(dictHandle);
	if (!pDict)
	{
		return;
	}

	estrClear(&pDict->pHTMLExtraLink);
	estrGetVarArgs(&pDict->pHTMLExtraLink, pFmt);
}


#include "ResourceManager_h_ast.c"
