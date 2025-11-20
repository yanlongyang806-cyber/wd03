#pragma once

void BeginMemLeakTracking(int iPauseBeforeBeginning, int iPauseBetweenChecks, size_t iIncreaseAmountThatIsALeak, size_t iFirstIncreaseAllowance, const char *pAlertKeyPrefix);
/*Wait PauseBeforeBeginning seconds. Then check getProcessPageFileUsage() and remember that value. Then check
every PauseBetweenChecks seconds. If the RAM use has increased by iIncreaseAmountThatIsALeak over the initial
size, there's probably a memory leak. At that point, do two things:
1. Send an alert
2. Dump out mmds

Then keep checking, and if we ever increase by iIncreaseAmountThatIsALeak again, mmds again, and compare the two
and generate a report which should precisely identify the leak*/

/*optionally, you can sort all the leaks into categories and then provide a summary by category. A category consists of a 
category name then a list of member names. The member names can match the mmds output in several different ways:

...\utilitieslib\components\referencesystem.c:402      4.26 MB      324    3463
matches "referencesystem.c"

                         Refdict_Itempowerdef:1        0 bytes   364680 1094048
matches "Refdict_Itempowerdef"

    PlayerVisitedMap (Player.h):100002   6.64 MB    17436   17436
matches "PlayerVisitedMap"
*/


bool MemLeakTracking_Running(void);

AUTO_STRUCT;
typedef struct MemLeakCategory
{
	//underscores are here so that using DEFAULT_FIELD doesn't end up with a name collision where there's some member name named "MemberNames" or something like that

	char *pCategoryName__; AST(STRUCTPARAM) //this is NOT keyed so that we can load from multiple files/sources and then merge them
			//all together without getting in trouble for pushing two things into an array with the same key

	char **ppMemberNames__; AST(FORMATSTRING(DEFAULT_FIELD=1))

	S64 iAmountLeaked; NO_AST
	char *pReportString__;

} MemLeakCategory;

extern ParseTable parse_MemLeakCategory[];
#define TYPE_parse_MemLeakCategory MemLeakCategory

//takes in a string with two MMDS outputs in it, writes out a human-readable mem leak report. If you provide an earray
//of categories, the summary will begin with groups-by-category
//iNumToIncludePerCategory = 0 means all
//ppOutBiggestKey will get filled in with the key (ie, filename/line num) of the biggest single leaker
void FindMemLeaksFromStringWithMultipleMMDS(char *pInString, char **ppOutString, char **ppOutBiggestKey, MemLeakCategory ***pppCategories, int iNumToIncludePerCategory);

//server types or products can set up a special extra reporting function that is called whenever the automatic mem leak tracking
//is going to do its dumping. The filePrefix will be something like "c:\foo\bar_", so you should append to it "someReportType.txt"
//then open that file and write your report into it.
LATELINK;
void MemLeakTracking_ExtraReport(char *pFilePrefix);

//function which gets the size that will be used as the "amount of allocated memory" size. For instance, a gameserver
//subtracts out a certain constant per player logged in
LATELINK;
void GetMemSizeForMemLeakTracking(size_t *pOutSize, size_t *pOutEffectiveSize, char **ppOutPrettySizeString);

//global as it's used by both game servers and the controller
extern bool gbTrackMemLeaksInDevMode;

//I'm some piece of code that is legitimately allocating a large amount of memory that will hang around forever, 
//presumably after mem leak tracking has already started. So I'll report an amount of RAM that should be discounted during all 
//future mem leak reporting
void MemLeakTracking_IgnoreLargeAllocation(char *pComment, size_t iAllocationSize);