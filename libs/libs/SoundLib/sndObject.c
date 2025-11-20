/***************************************************************************



***************************************************************************/

#ifndef STUB_SOUNDLIB

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "earray.h"
#include "timing.h"
#include "StringCache.h"

#include "event_sys.h"

#include "sndDebug2.h"
#include "sndObject.h"
#include "sndFade.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Audio););

#define ORIG_CONN_FADE_RATE 1.0/15

StashTable sndObjStash;
SoundObjectClass **sndObjClasses;

void sndObjectRegisterClass(const char *name, SoundObjectMsgHandler handler)
{
	int id = 0;
	SoundObjectClass *soc = NULL;
	if(!sndObjStash)
	{
		sndObjStash = stashTableCreateWithStringKeys(10, StashDefault);
		eaClear(&sndObjClasses);
	}

	if(stashFindInt(sndObjStash, name, &id))
	{
		devassertmsg(sndObjClasses[id]->msgHandler==handler, 
					"Trying to register two SoundObject classes with different handlers");
		return;
	}

	soc = callocStruct(SoundObjectClass);
	eaPush(&sndObjClasses, soc);

	soc->id = eaSize(&sndObjClasses)-1;
	soc->name = name;
	soc->msgHandler = handler;

	stashAddInt(sndObjStash, name, soc->id, 1);
}

void sndObjectSendMessage(SoundObject *obj, SoundObjectMsg *msg)
{
	if(obj->soc)
	{
		obj->soc->msgHandler(obj, msg);
	}
}

void sndObjectGetName(SoundObject *obj, char *str, int len)
{
	strcpy_s(str, len, obj->desc_name);
}

static SoundObjectClass* sndObjectGetClassByName(const char *name)
{
	int id;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(stashFindInt(sndObjStash, name, &id))
	{
		PERFINFO_AUTO_STOP();
		return sndObjClasses[id];
	}

	devassertmsg(0, "Trying to get unregistered object class");

	PERFINFO_AUTO_STOP();

	return NULL;
}

void sndObjectCreateByName(SoundObject *obj, const char *class_name, const char *file_name, const char *desc_name, const char *orig_name)
{
	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(!obj)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	ZeroStruct(obj);
	obj->soc = sndObjectGetClassByName(class_name);
	obj->soc->count++;
	
	obj->file_name = allocAddFilename(file_name);
	obj->desc_name = allocAddString(desc_name);
	obj->orig_name = allocAddString(orig_name);

	eaPush(&space_state.objects, obj);

	PERFINFO_AUTO_STOP();
}

void sndObjectSendCleanMsg(SoundObject *obj, int reload)
{
	SoundObjectMsg msg;

	msg.type = SNDOBJMSG_CLEAN;
	msg.in.clean.reload = !!reload;
	sndObjectSendMessage(obj, &msg);
}

void sndObjectSendDestroyMsg(SoundObject *obj)
{
	SoundObjectMsg msg;

	msg.type = SNDOBJMSG_DESTROY;
	sndObjectSendMessage(obj, &msg);
}

void* sndObjectGetMemTracker(SoundObject *obj)
{
	SoundObjectMsg msg = {0};
	msg.type = SNDOBJMSG_MEMTRACKER;
	sndObjectSendMessage(obj, &msg);

	return msg.out.memtracker.tracker;
}

void sndObjectDestroy(SoundObject *obj)
{
	int result;

	PERFINFO_AUTO_START(__FUNCTION__, 1);

	if(!obj->soc)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	
	sndObjectSendDestroyMsg(obj);

	obj->soc->count--;

	result = eaFindAndRemoveFast(&space_state.objects, obj);

	PERFINFO_AUTO_STOP();
}

#endif
