#include "InfoTracker.h"

#include "earray.h"
#include "EString.h"
#include "StashTable.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

typedef struct InfoTrackerTag
{
	const char*			name;
	int					count;
}InfoTrackerTag;

typedef struct InfoTracker	InfoTracker;

struct InfoTracker
{
	StashTable			tagTable;
	InfoTrackerTag**	tags;
	StashTable			subTrackerTable;
	InfoTracker**		subTrackers;
	InfoTracker*		curSubTracker;
	int					frameCount;
	const char*			name;
};

typedef struct InfoTrackerStorage
{
	StashTable			trackerTable;
	InfoTracker**		trackers;
}InfoTrackerStorage;

static InfoTrackerStorage* storage = NULL;

InfoTracker* findTracker(SA_PARAM_NN_STR const char* infotag)
{
	InfoTracker* tracker;
	const char* allocTag = allocAddString(infotag);

	if(!storage)
	{
		storage = callocStruct(InfoTrackerStorage);
		storage->trackerTable = stashTableCreateWithStringKeys(1, StashDefault);
	}

	if(!stashFindPointer(storage->trackerTable, allocTag, &tracker))
	{
		tracker = callocStruct(InfoTracker);
		tracker->subTrackerTable = stashTableCreateWithStringKeys(2, StashDefault);
		tracker->name = allocTag;
		stashAddPointer(storage->trackerTable, allocTag, tracker, false);
	}

	return tracker;
}

AUTO_COMMAND;
void infotrackStartFrameInternal(const char* infotag, const char* subtype)
{
	InfoTracker* tracker = findTracker(infotag);
	InfoTracker* subTracker;
	const char* substr = subtype ? allocAddString(subtype) : allocAddString("");

	tracker->frameCount++;

	if(!stashFindPointer(tracker->subTrackerTable, substr, &subTracker))
	{
		subTracker = callocStruct(InfoTracker);
		subTracker->tagTable = stashTableCreateWithStringKeys(32, StashDefault);
		subTracker->name = allocAddString(substr);
		eaPush(&tracker->subTrackers, subTracker);
		stashAddPointer(tracker->subTrackerTable, subTracker->name, subTracker, false);
	}

	subTracker->frameCount++;
	tracker->curSubTracker = subTracker;
}

AUTO_COMMAND;
void infotrackEndFrameInternal(const char* infotag)
{
	InfoTracker* tracker = findTracker(infotag);
	tracker->curSubTracker = NULL;
}

void infotrackIncrementCountfInternal(const char* infotag, const char* formatStr, ...)
{
	InfoTracker* tracker = findTracker(infotag);
	InfoTracker* subTracker = tracker->curSubTracker;
	InfoTrackerTag* tag;
	static char* tagStr = NULL;
	const char* allocTag;

	if(!subTracker)
		return;

	VA_START(args, formatStr);
	estrClear(&tagStr);
	estrConcatfv(&tagStr, formatStr, args);
	VA_END();

	allocTag = allocAddString(tagStr);

	if(!stashFindPointer(subTracker->tagTable, allocTag, &tag))
	{
		tag = callocStruct(InfoTrackerTag);
		tag->name = allocAddString(allocTag);
		eaPush(&subTracker->tags, tag);
		stashAddPointer(subTracker->tagTable, allocTag, tag, false);
	}

	tag->count++;
}

int infotrackPrintHelper(int* totalCount, StashElement elem)
{
	InfoTrackerTag* tag = stashElementGetPointer(elem);
	printf("Tag: %s, count: %d (%.2f%%)\n", tag->name, tag->count, (F32)tag->count / *totalCount);
	return 1;
}

int infotrackCmpTagByCount(const InfoTrackerTag** lhs, const InfoTrackerTag** rhs)
{
	return (*lhs)->count - (*rhs)->count;
}

AUTO_COMMAND;
void infotrackPrint(const char* infotag)
{
	int i;
	InfoTracker* tracker = findTracker(infotag);

	printf("Total frame count: %d\n", tracker->frameCount);
	for(i = eaSize(&tracker->subTrackers)-1; i >= 0; i--)
	{
		int j, n;
		InfoTracker* subTracker = tracker->subTrackers[i];
		printf("Subtracker %s, frame count: %d\n", subTracker->name, subTracker->frameCount);
		//stashForEachElementEx(subTracker->tagTable, infotrackPrintHelper, &subTracker->frameCount);
		eaQSort(subTracker->tags, infotrackCmpTagByCount);
		for(j = 0, n = eaSize(&subTracker->tags); j < n; j++)
		{
			InfoTrackerTag* tag = subTracker->tags[j];
			printf("\nTag: %s, count: %d (%.2f%%)\n", tag->name, tag->count, (F32)tag->count / subTracker->frameCount);
		}
	}
}