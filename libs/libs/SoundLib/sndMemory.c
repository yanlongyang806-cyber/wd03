#ifdef STUB_SOUNDLIB

#include "sndMemory.h"

AudioMemData sndMemData;
void sndMemGetUsage(AudioMemEntry ***entries) { }

#else

#include "sndMemory.h"

#include "event_sys.h"

#include "sndLibPrivate.h"
#include "sndSource.h"

#include "textparser.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

AudioMemData sndMemData;
StashTable sndPtrToEntry;

typedef struct FMODSystemObject {
	SoundObject obj;
} FMODSystemObject;

typedef struct FMODEventSystemObject {
	SoundObject obj;
} FMODEventSystemObject;

typedef struct FMODEventProjectObject {
	SoundObject obj;
	void *project;
} FMODEventProjectObject;

typedef struct FMODWaveBankObject {
	SoundObject obj;
	void *wavebank;
} FMODWaveBankObject;

void sndFMODSystemObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase SNDOBJMSG_MEMTRACKER: {
			//msg->out.memtracker.tracker = fmodSystemGetMemTracker();
		}
	}
}

void sndFMODEventSystemObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase SNDOBJMSG_MEMTRACKER: {
			//msg->out.memtracker.tracker = fmodEventSystemGetMemTracker();
		}
	}
}

void sndFMODEventProjectObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase SNDOBJMSG_MEMTRACKER: {
			//msg->out.memtracker.tracker = fmodEventProjectGetMemTracker();
		}
	}
}

void sndFMODWaveBankObjectMsgHandler(SoundObject *obj, SoundObjectMsg *msg)
{
	switch(msg->type)
	{
		xcase SNDOBJMSG_MEMTRACKER: {
			//msg->out.memtracker.tracker = fmodEventWaveBankGetMemTracker();
		}
	}
}

void sndMemInit(void)
{
	//sndObjectRegisterClass(EVENTSYSTEM_CLASS_NAME, sndEventSystemObjectMsgHandler);
}

const void* sndMemEntryGetObject(const void *ent)
{
	AudioMemEntry *entry = (AudioMemEntry*)ent;
	return (void*)entry->object;
}

void sndMemGetUsage(AudioMemEntry ***entries)
{
	int i;
	static SoundObject **objects = NULL;
	static AudioMemEntry **deletedEntries = NULL;
	static SoundObject **newObjects = NULL;
	if(entries && !*entries)
	{
		eaCreate((const void***)entries);
	}
	sndMemData.totalmem = 0;
	
	eaClear(&objects);
	eaPushEArray(&objects, (SoundObject***)&space_state.sources);
	eaPushEArray(&objects, (SoundObject***)&space_state.global_spaces);
	eaPushEArray(&objects, (SoundObject***)&space_state.global_conns);

	if(entries)
	{
		eaClear(&newObjects);
		eaClear(&deletedEntries);
		eaDiffAddrEx(&objects, NULL, entries, sndMemEntryGetObject, &newObjects);
		eaDiffAddrEx(entries, sndMemEntryGetObject, &objects, NULL, &deletedEntries);

		for(i=0; i<eaSize(&deletedEntries); i++)
		{
			AudioMemEntry *entry = deletedEntries[i];

			eaFindAndRemoveFast(entries, entry);
			StructDestroySafe(parse_AudioMemEntry, &entry);
		}

		for(i=0; i<eaSize(&newObjects); i++)
		{
			SoundObject *object = newObjects[i];
			AudioMemEntry *entry = StructCreate(parse_AudioMemEntry);

			entry->object = object;
			entry->file_name = object->file_name;
			entry->desc_name = object->desc_name;
			entry->orig_name = object->orig_name;

			eaPush(entries, entry);
		}
	}
	
	sndMemData.totalmem = 0;
	for(i=0; i<eaSize(&objects); i++)
	{
		SoundObject *obj = objects[i];
		void *memtracker = sndObjectGetMemTracker(obj);

		obj->mem_total = 0;

		sndMemData.totalmem += obj->mem_total;
	}

	if(entries)
	{
		for(i=0; i<eaSize(entries); i++)
		{
			AudioMemEntry *entry = (*entries)[i];

			entry->total_mem = entry->object->mem_total;
		}
	}
}

void sndMemGetProjectUsage(void)
{

}


#endif

#include "sndMemory_h_ast.c"
