//Eventually, we should move all the RPC commands into this file so that we can modify them without having to touch the ObjectDB proper.

#include "timing.h"
#include "objIndex.h"

#include "objContainer.h"
#include "structinternals.h"
#include "ObjectDB.h"

#include "autogen/dbxmlrpc_c_ast.h"

//extern ObjectIndex *gLevel_idx;

AUTO_STRUCT;
typedef struct IntPair
{
	int level; AST(KEY)
	S64 count;
} IntPair;

AUTO_STRUCT;
typedef struct IntHisto
{
	IntPair **values;	AST(FORMATSTRING(XML_UNWRAP_ARRAY = 1))
} IntHisto;

AUTO_COMMAND ACMD_CATEGORY(XMLRPC, ObjectDB) ACMD_ACCESSLEVEL(9);
IntHisto* DbGetLevelHistogram(void)
{
	IntHisto *histo = StructCreate(parse_IntHisto);
	/*
	ObjectIndexKey key = {0};
	S64 count = 0;
	S64 sum = 0;
	int level = 1;

	PERFINFO_AUTO_START_FUNC();

	objIndexObtainReadLock(gLevel_idx);

	count = objIndexCount(gLevel_idx);

	do {
		S64 chunk;
		objIndexInitKey_Int(gLevel_idx, &key, level);
		chunk = objIndexCountKey(gLevel_idx, &key);
		objIndexDeinitKey_Int(gLevel_idx, &key);
		if (chunk)
		{
			IntPair *pair = StructCreate(parse_IntPair);
			pair->level = level;
			pair->count = chunk;
			sum += chunk;
			eaPush(&histo->values, pair);
		}
		level++;
		//just in case
		if (level > 100) break;
	} while (sum < count);

	objIndexReleaseReadLock(gLevel_idx);

	PERFINFO_AUTO_STOP();

	*/
	return histo;
}

AUTO_STRUCT;
typedef struct XMLCharacterBlob
{
	char *character;		AST(ESTRING FORMATSTRING(XML_INLINE_BLOB = 1))
} XMLCharacterBlob;

AUTO_STRUCT;
typedef struct XMLCharactersBlob
{
	U32 nextcreatedtime;
	U32 nextid;
	U32 count;
	U32 processingtime;	//in ms
	bool finished;
	char **ppCharacters;		AST(FORMATSTRING(XML_INLINE_BLOB = 1))
}XMLCharactersBlob;

AUTO_STRUCT;
typedef struct XMLContainersBlob
{
	char **ppContainers;		AST(FORMATSTRING(XML_INLINE_BLOB = 1))
}XMLContainersBlob;

XMLCharacterBlob *DBGetContainerByID(GlobalType type, U32 containerID);
// This command retrieves ALL characterdata for a given character by ID.
// Unless you have a really good reason to need ALL the data, you really should be using DbNamedFetchByCharacterID instead.
AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
XMLCharacterBlob *DBGetCharacterData(U32 containerID)
{
	return DBGetContainerByID(GLOBALTYPE_ENTITYPLAYER, containerID);
}

void dbnamedfetchfields(char **estr, XMLRPCNamedFetch *fetch, Container *con)
{
	static char *buf = NULL;
	static char *error = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(!buf)
		estrCreate(&buf);
	if(!error)
		estrCreate(&error);
	estrPrintf(estr, "<struct>");

	EARRAY_FOREACH_BEGIN(fetch->ppFetchColumns, i);
	{
		ParseTable *tpi = NULL;
		int col = -1;
		int index = -1;
		void *strptr = NULL;
		bool ok = true;
		estrClear(&error);
		ok = ParserResolvePath(fetch->ppFetchColumns[i]->objectpath,con->containerSchema->classParse, con->containerData, &tpi, &col, &strptr, &index, &error, NULL, 0);
		if (!ok)
		{
			estrConcatf(estr, "<member><name>%s</name><value>%s</value></member>", fetch->ppFetchColumns[i]->name, error);
		}
		else
		{
			estrClear(&buf);
			ParserWriteXMLField(&buf, tpi, col, strptr, index, TPXML_FORMAT_XMLRPC|TPXML_NO_PRETTY);
			estrConcatf(estr, "<member><name>%s</name><value>%s</value></member>", fetch->ppFetchColumns[i]->name, buf);
		}
	}
	EARRAY_FOREACH_END;

	estrConcatf(estr,"</struct>");

	estrClear(&buf);
	estrClear(&error);

	PERFINFO_AUTO_STOP();
}

//This command gets arbitrary namedfetch data from a single character.
// Create namedfetches with DbSetNamedFetch/DbGetNamedFetch
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLCharacterBlob *DbNamedFetchByCharacterID(char *namedfetch, U32 containerID)
{
	XMLRPCNamedFetch *fetch = NULL;
	Container *con = NULL;
	if (!namedfetch)
	{
		Errorf("Named Fetches have not been initialized.");
		return NULL;
	}

	if (!(fetch = dbGetNamedFetchInternal(namedfetch, GLOBALTYPE_ENTITYPLAYER)))
	{
		// The website looks for this string. Do not change this without web team sign off.
		Errorf("Named Fetch %s was not set.", namedfetch);
		return NULL;
	}

	con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, containerID, true, false, true);

	if (!con || !con->containerData)
	{
		// The website looks for this string. Do not change this without web team sign off.
		Errorf("EntityPlayer[%u] does not exist.", containerID);
		if(con)
			objUnlockContainer(&con);
		return NULL;
	}
	else
	{
		XMLCharacterBlob *blob = StructCreate(parse_XMLCharacterBlob);
		dbnamedfetchfields(&blob->character, fetch, con);
		objUnlockContainer(&con);
		return blob;
	}
}

extern ObjectIndex *gAccountID_idx;
extern ObjectIndex *gAccountIDDeleted_idx;

XMLCharactersBlob *DbNamedFetchByAccountID_internal(char *namedfetch, U32 accountID, bool deleted)
{
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_ENTITYPLAYER);
	ObjectIndexKey key = {0};
	XMLCharactersBlob *chars = NULL;
	XMLRPCNamedFetch *fetch = NULL;
	static char *buf = NULL;
	S64 starttime = timerCpuTicks64();
	ContainerID *containerIDArray = NULL;
	int i;

	if (!namedfetch)
	{
		Errorf("Named Fetches have not been initialized.");
		return NULL;
	}
	if (!(fetch = dbGetNamedFetchInternal(namedfetch, GLOBALTYPE_ENTITYPLAYER)))
	{
		// The website looks for this string. Do not change this without web team sign off.
		Errorf("Named Fetch %s was not set.", namedfetch);
		return NULL;
	}

	chars = StructCreate(parse_XMLCharactersBlob);
	if (!store) 
		return chars;

	if(!buf)
		estrCreate(&buf);

	containerIDArray = GetContainerIDsFromAccountID(accountID);

	for(i = 0; i < ea32Size(&containerIDArray); ++i)
	{
		Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, containerIDArray[i], true, false, true);
		estrClear(&buf);
		dbnamedfetchfields(&buf, fetch, con);
		if (estrLength(&buf))
		{
			eaPush(&chars->ppCharacters, strdup(buf));
			chars->count++;
		}
		objUnlockContainer(&con);
	}

	ea32Destroy(&containerIDArray);

	chars->finished = true;
	chars->processingtime = ((timerCpuTicks64() - starttime) * 1000)/timerCpuSpeed64();

	return chars;
}
//This command gets arbitrary namedfetch data from all characters associated with a given account.
// Create namedfetches with DbSetNamedFetch/DbGetNamedFetch
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLCharactersBlob *DbNamedFetchByAccountID(char *namedfetch, U32 accountID)
{
	return DbNamedFetchByAccountID_internal(namedfetch, accountID, false);
}

//This command gets arbitrary namedfetch data from all characters associated with a given account.
// Create namedfetches with DbSetNamedFetch/DbGetNamedFetch
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLCharactersBlob *DbNamedFetchByAccountIDDeleted(char *namedfetch, U32 accountID)
{
	return DbNamedFetchByAccountID_internal(namedfetch, accountID, true);
}

extern U32 gNamedFetchMaxTimeMS;
extern ObjectIndex *gCreatedTime_idx;

//This command gets arbitrary namedfetch data for characters created after a created time and ID.
// Processing time is limited, so you may need to call this multiple times.
// Create namedfetches with DbSetNamedFetch/DbGetNamedFetch
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLCharactersBlob *DbNamedFetchByTime(char *namedfetch, U32 createdtime, U32 conid)
{
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_ENTITYPLAYER);
	ObjectIndexKey key = {0};
	ObjectIndexIterator iter;
	XMLCharactersBlob *chars = NULL;
	XMLRPCNamedFetch *fetch = NULL;
	static char *buf = NULL;
	S64 starttime = timerCpuTicks64();
	S64 endtime = starttime + (timerCpuSpeed64() * gNamedFetchMaxTimeMS)/1000; //limit to 20ms
	ContainerID *containerIDArray = NULL;
	int i = 0;

	if (!namedfetch)
	{
		Errorf("Named Fetches have not been initialized.");
		return NULL;
	}
	if (!(fetch = dbGetNamedFetchInternal(namedfetch, GLOBALTYPE_ENTITYPLAYER)))
	{
		// The website looks for this string. Do not change this without web team sign off.
		Errorf("Named Fetch %s was not set.", namedfetch);
		return NULL;
	}

	chars = StructCreate(parse_XMLCharactersBlob);
	// Finished is true if there are no characters to return.
	chars->finished = true;
	if (!store) 
		return chars;

	objIndexInitKey_Template(gCreatedTime_idx, &key, createdtime, conid);

	if(!buf)
		estrCreate(&buf);

	objIndexObtainReadLock(gCreatedTime_idx);
	if (objIndexGetIteratorFrom(gCreatedTime_idx, &iter, ITERATE_FORWARD, &key, 0))
	{
		void *containerData;
		while (containerData = objIndexGetNextContainerData(&iter, store))
		{
			ContainerID containerID = 0;
			objGetKeyInt(store->containerSchema->classParse, containerData, &containerID);
			if(containerID)
				ea32Push(&containerIDArray, containerID);
		}
	}
	objIndexReleaseReadLock(gCreatedTime_idx);
	objIndexDeinitKey_Template(gCreatedTime_idx, &key);

	for(i = 0; i < ea32Size(&containerIDArray); ++i)
	{
		Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, containerIDArray[i], true, false, true);
		if(con)
		{
			estrClear(&buf);
			dbnamedfetchfields(&buf, fetch, con);
			if (estrLength(&buf))
			{
				eaPush(&chars->ppCharacters, strdup(buf));
				chars->count++;
			}
			objUnlockContainer(&con);
		}
		if (timerCpuTicks64() > endtime)
			break;
	}

	//check for more
	if (++i < ea32Size(&containerIDArray))
	{
		Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, containerIDArray[i], true, false, true);
		if (store->requiresHeaders)
		{
			chars->nextid = con->header->containerId;
			chars->nextcreatedtime = con->header->createdTime;
		}
		else
		{
			char ctbuf[16];
			objGetKeyInt(store->containerSchema->classParse, con->containerData, &chars->nextid);

			if (objPathGetString(".pplayer.icreatedtime", con->containerSchema->classParse,
				con->containerData, SAFESTR(ctbuf)))
			{
				chars->nextcreatedtime = atoi(ctbuf);
			}
		}
		objUnlockContainer(&con);
		// Only set finished to false if, after processing, there are characters left.
		chars->finished = false;
	}

	estrClear(&buf);

	chars->processingtime = ((timerCpuTicks64() - starttime) * 1000)/timerCpuSpeed64();

	return chars;
}


extern ObjectIndex *gGuildCreatedTime_idx;

//This command gets arbitrary namedfetch data for guilds created after a created time and ID.
// Processing time is limited, so you may need to call this multiple times.
// Create namedfetches with DbSetNamedFetch/DbGetNamedFetch
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLCharactersBlob *DbGuildNamedFetchByTime(char *namedfetch, U32 createdtime, U32 conid)
{
	ContainerStore *store = objFindContainerStoreFromType(GLOBALTYPE_GUILD);
	ObjectIndexKey key = {0};
	ObjectIndexIterator iter;
	XMLCharactersBlob *guilds = NULL;
	XMLRPCNamedFetch *fetch = NULL;
	static char *buf = NULL;
	S64 starttime = timerCpuTicks64();
	S64 endtime = starttime + (timerCpuSpeed64() * gNamedFetchMaxTimeMS)/1000; //limit to 20ms
	ContainerID *containerIDArray = NULL;
	int i;

	if (!namedfetch)
	{
		Errorf("Named Fetches have not been initialized.");
		return NULL;
	}
	if (!(fetch = dbGetNamedFetchInternal(namedfetch, GLOBALTYPE_GUILD)))
	{
		// The website looks for this string. Do not change this without web team sign off.
		Errorf("Named Fetch %s was not set.", namedfetch);
		return NULL;
	}

	guilds = StructCreate(parse_XMLCharactersBlob);
	if (!store) 
		return guilds;

	objIndexInitKey_Template(gGuildCreatedTime_idx, &key, createdtime, conid);

	if(!buf)
		estrCreate(&buf);

	objIndexObtainReadLock(gGuildCreatedTime_idx);
	if (objIndexGetIteratorFrom(gGuildCreatedTime_idx, &iter, ITERATE_FORWARD, &key, 0))
	{
		void *containerData;
		while (containerData = objIndexGetNextContainerData(&iter, store))
		{
			ContainerID containerID;
			if(objGetKeyInt(store->containerSchema->classParse, containerData, &containerID))
				ea32Push(&containerIDArray, containerID);
		}
	}
	objIndexReleaseReadLock(gGuildCreatedTime_idx);
	objIndexDeinitKey_Template(gGuildCreatedTime_idx, &key);

	for(i = 0; i < ea32Size(&containerIDArray); ++i)
	{
		Container *con = objGetContainerEx(GLOBALTYPE_GUILD, containerIDArray[i], true, false, true);
		if(con)
		{
			estrClear(&buf);
			dbnamedfetchfields(&buf, fetch, con);
			if (estrLength(&buf))
			{
				eaPush(&guilds->ppCharacters, strdup(buf));
				guilds->count++;
			}
			objUnlockContainer(&con);
		}
		if (timerCpuTicks64() > endtime)
			break;
	}

	//check for more
	if (++i < ea32Size(&containerIDArray))
	{
		Container *con = objGetContainerEx(GLOBALTYPE_GUILD, containerIDArray[i], true, false, true);
		char ctbuf[16];
		if(con)
		{
			objGetKeyInt(store->containerSchema->classParse, con->containerData, &guilds->nextid);

			if (objPathGetString(".iCreatedOn", con->containerSchema->classParse,
				con->containerData, SAFESTR(ctbuf)))
			{
				guilds->nextcreatedtime = atoi(ctbuf);
			}

			guilds->finished = false;
		}
	}
	else
	{
		guilds->finished = true;
	}

	estrClear(&buf);

	guilds->processingtime = ((timerCpuTicks64() - starttime) * 1000)/timerCpuSpeed64();

	return guilds;
}

//This command gets arbitrary namedfetch data from a single container.
// Create namedfetches with DbSetNamedFetch/DbGetNamedFetch2
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLCharacterBlob *DbNamedFetchByID(char *namedfetch, GlobalType type, U32 containerID)
{
	XMLRPCNamedFetch *fetch = NULL;
	Container *con = NULL;
	if (!namedfetch)
	{
		Errorf("Named Fetches have not been initialized.");
		return NULL;
	}

	if (!(fetch = dbGetNamedFetchInternal(namedfetch, type)))
	{
		// The website looks for this string. Do not change this without web team sign off.
		Errorf("Named Fetch %s was not set.", namedfetch);
		return NULL;
	}

	con = objGetContainerEx(type, containerID, true, false, true);

	if (!con || !con->containerData)
	{
		Errorf("%s[%u] does not exist.", GlobalTypeToName(type), containerID);
		if(con)
			objUnlockContainer(&con);
		return NULL;
	}
	else
	{
		XMLCharacterBlob *blob = StructCreate(parse_XMLCharacterBlob);
		dbnamedfetchfields(&blob->character, fetch, con);
		objUnlockContainer(&con);
		return blob;
	}
}

//This command gets arbitrary namedfetch data from multiple containers.
// Create namedfetches with DbSetNamedFetch/DbGetNamedFetch2
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLContainersBlob *DbNamedFetchByIDs(XMLRPCNamedFetchInput *input)
{
	int i;
	XMLContainersBlob *blob;
	static char *pContainer = NULL;
	XMLRPCNamedFetch *fetch = NULL;

	if(!input->Ids)
	{
		Errorf("You must pass in at least one ContainerID.");
		return NULL;
	}

	if (!input->namedfetch)
	{
		Errorf("Named Fetches have not been initialized.");
		return NULL;
	}

	if (!(fetch = dbGetNamedFetchInternal(input->namedfetch, input->type)))
	{
		// The website looks for this string. Do not change this without web team sign off.
		Errorf("Named Fetch %s was not set.", input->namedfetch);
		return NULL;
	}

	blob = StructCreate(parse_XMLContainersBlob);
	if(!pContainer)
		estrCreate(&pContainer);

	for(i = 0; i < ea32Size(&input->Ids); ++i)
	{
		Container *con = objGetContainerEx(input->type, input->Ids[i], true, false, true);

		if (!con || !con->containerData)
		{
			Errorf("%s[%u] does not exist.", GlobalTypeToName(input->type), input->Ids[i]);
			if(con)
				objUnlockContainer(&con);
			continue;
		}
		else
		{
			estrClear(&pContainer);
			dbnamedfetchfields(&pContainer, fetch, con);
			eaPush(&blob->ppContainers, strdup(pContainer));
			objUnlockContainer(&con);
		}
	}
	estrClear(&pContainer);
	return blob;
}

// This command retrieves ALL data for an arbitrary container.
// Unless you have a really good reason to need ALL the data, you really should be using DbNamedFetchByID instead.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(9);
XMLCharacterBlob *DBGetContainerByID(GlobalType type, U32 containerID)
{
	Container *con = objGetContainerEx(type, containerID, true, false, true);
	XMLCharacterBlob *blob = NULL;
	if (!con || !con->containerData)
	{
		Errorf("%s[%u] does not exist.", GlobalTypeToName(type), containerID);
		if(con)
			objUnlockContainer(&con);
		return NULL;
	}
	blob = StructCreate(parse_XMLCharacterBlob);
	blob->character = NULL;
	ParserWriteXMLEx(&blob->character, con->containerSchema->classParse, con->containerData, TPXML_FORMAT_XMLRPC|TPXML_NO_PRETTY);
	objUnlockContainer(&con);
	return blob;
}

#include "autogen/dbxmlrpc_c_ast.c"