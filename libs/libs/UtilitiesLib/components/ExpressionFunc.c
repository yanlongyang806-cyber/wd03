#include "ExpressionFunc.h"

#include "Expression.h"
#include "ExpressionPrivate.h"

#include "cmdparse.h"
#include "Crypt.h"
#include "file.h"
#include "rand.h"
#include "HashFunctions.h"
#include "referencesystem.h"
#include "structDefines.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "textparser.h"
#include "textparserUtils.h"
#include "timing.h"
#include "rgb_hsv.h"
#include "Color.h"

#include "ExpressionMinimal_h_ast.h"
#include "ExpressionFunc_h_ast.h"

ExprFuncTag tagList[] = {
	// General Use			Requires SelfPtr,	Requires Partition
	{ "util",				false,				false},
	{ "gameutil",			false,				true},

	{ "entity",				true,				true},
	{ "entityutil",			false,				false},
	{ "player",				true,				true},

	// AI
	{ "ai",					true, 				true},
	{ "ai_movement",		true, 				false},
	{ "ai_powers",			true, 				false},

	// Game Systems
	{ "clickable",			false, 				true},
	{ "Contact",			false, 				false},
	{ "encounter",			false, 				true},
	{ "encounter_action",	false, 				true},
	{ "event_count",		false, 				true},
	{ "ItemEval",			false, 				false},
	{ "ItemAssignments",	false, 				false},
	{ "layerfsm",			false,				true},
	{ "Mission",			true, 				true},
	{ "mission_template",	false,				true},
	{ "OpenMission",		false, 				true},
	{ "reward",				false, 				true},

	// Combat & Powers
	{ "CEFuncsActivation",	false,				false},
	{ "CEFuncsAffects",		false,				false},
	{ "CEFuncsApplication", false,				false},
	{ "CEFuncsApplicationSimple", false, 		false},
	{ "CEFuncsAttribMod",	false,				false},
	{ "CEFuncsCharacter",	false, 				true},
	{ "CEFuncsGeneric",		false, 				false},
	{ "CEFuncsPowerDef",	false, 				false},
	{ "CEFuncsRegionRules", false, 				false},
	{ "CEFuncsSelf",		false, 				true},
	{ "CEFuncsTrigger",		false, 				false},
	{ "critter",			false,				false},
	{ "exprFuncListPowerStats", false, 			false},
	{ "powerart",			false, 				false},
	{ "PTECharacter",		false, 				true},
	{ "PTENode",			false, 				false},
	{ "PTERespec",			false, 				false},
	{ "s_CritterExprFuncList", false,			false},
	{ "projectile",			true,				false},
	{ "CEFuncsTeleport",	false,				false},

	// Other
	{ "alerts",				false, 				false},
	{ "AlgoPetFuncs",		false, 				false},
	{ "AutoPlaceCommand",	false, 				false},
	{ "buildscripting",		true, 				true},
	{ "controllerScripting", false, 			false},
	{ "Emote",				true, 				false},
	{ "lpco",				true, 				false},
	{ "LpcoTrackedValue",	true, 				false},
	{ "PetContact",			false, 				false},
	{ "test",				false, 				true},
	{ "UIGen",				false, 				false},
	{ "SentryServer",		false,				false},
	{ "SmartAds",			false,				false},
    { "GroupProject",		false,				true},
	{ "Account",			false,				false},
	{ "Error",				false,				false}, 

};

STATIC_ASSERT(ARRAY_SIZE(tagList) < TAG_LIST_BYTES * 8);

extern StaticDefineInt enumExprCodeEnum_Autogen[];

S64IntFunc g_PartitionTimeFunc = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

void exprAddFuncToTable(StashTable funcTable, ExprFuncDesc* desc, int checkDuplicates, ExprFuncTag* tag);

StashTable tagTable;
StashTable globalFuncTable = NULL;

StashTable sFuncTablesByName = NULL;

ExprFuncTable* exprContextCreateFunctionTableEx(char *pName, MEM_DBG_PARMS_VOID)
{
	ExprFuncTable* funcTable;
	funcTable = scallocStruct(ExprFuncTable);
	funcTable->allowedTags = scalloc(TAG_LIST_BYTES, sizeof(U8));
	funcTable->pName = pName;

	if (!sFuncTablesByName)
	{
		sFuncTablesByName =  stashTableCreateWithStringKeys(64, StashDefault);
	}

	assertmsgf(!stashFindPointer(sFuncTablesByName, pName, NULL),
		"Two Function tables named %s", pName);

	stashAddPointer(sFuncTablesByName, pName, funcTable, false);

	return funcTable;
}

typedef struct exprAddFuncData
{
	ExprFuncTable* funcTable;
	ExprFuncTag* tag;
}exprAddFuncData;

bool exprTagUsingSelfPtr(const char *tagStr)
{
	ExprFuncTag *tag = NULL;

	stashFindPointer(tagTable, tagStr, &tag);

	return tag && tag->usingSelfPtr;
}

bool exprTagUsingPartition(const char *tagStr)
{
	ExprFuncTag *tag = NULL;

	stashFindPointer(tagTable, tagStr, &tag);

	return tag && tag->usingPartition;
}

/*
int addFuncByTag(exprAddFuncData* addFuncData, StashElement element)
{
	ExprFuncDesc* funcDesc = stashElementGetPointer(element);
	int i = 0;

	do{
		if(!stricmp(addFuncData->tag->name, funcDesc->tags[i].str))
		{
			exprAddFuncToTable(addFuncData->funcTable, funcDesc, true, addFuncData->tag);
			break;
		}
	} while(funcDesc->tags[++i].str != NULL);

	return 1;
}
*/

void exprContextAddFuncsToTableByTag(ExprFuncTable* funcTable, const char* tagStr)
{
	ExprFuncTag* tag;
	//exprAddFuncData addFuncData;

	if(!stashFindPointer(tagTable, tagStr, &tag))
	{
		ErrorFilenameDeferredf(NULL, "Trying to add functions of tag %s, but tag is not registered", tagStr);
		return;
	}

	funcTable->allowedTags[tag->tagNum / 8] |= 1 << (tag->tagNum % 8);

	if(tag->usingSelfPtr && !funcTable->requireSelfPtr)
	{
		funcTable->requireSelfPtr = true;
		funcTable->selfPtrTag = tag;
	}
	if(tag->usingPartition && !funcTable->requirePartition)
	{
		funcTable->requirePartition = true;
		funcTable->partitionTag = tag;
	}
	/*
	if(!stashFindPointer(tagTable, tag, &addFuncData.tag))
	{
#ifdef EXPRESSION_STRICT
		devassertmsgf(0, "Trying to add invalid tag %s to a function table", tag);
#endif
		//ErrorFilenameDeferredf(NULL, "Trying to add invalid tag %s to a function table", tag);
		return;
	}

	addFuncData.funcTable = funcTable;

	stashForEachElementEx(globalFuncTable, addFuncByTag, &addFuncData);
	*/
}

void exprAddFuncToTable(StashTable funcTable, ExprFuncDesc* desc, int checkDuplicates, ExprFuncTag* tag)
{
	ExprFuncDesc* oldDesc;
	const char* allocName = allocAddString(desc->funcName);

	if(stashAddressFindPointer(funcTable, allocName, &oldDesc))
	{
		if(checkDuplicates && oldDesc != desc)
			assertmsgf(0,"Duplicate function entry found for function %s", desc->funcName);
	}
	else
		stashAddressAddPointer(funcTable, allocName, desc, 0);

	/*
	if(desc->funcFlags & EXPR_FUNC_RQ_SELFPTR && !(funcTable->funcFlags & EXPR_FUNC_RQ_SELFPTR))
		funcTable->selfPtrTag = tag;
	if(desc->funcFlags & EXPR_FUNC_RQ_PARTITION && !(funcTable->funcFlags & EXPR_FUNC_RQ_PARTITION))
		funcTable->partitionTag = tag;
	funcTable->funcFlags |= desc->funcFlags;
	*/
}

void exprRegisterFunction(ExprFuncDesc* desc, int checkDuplicates, bool bFunctionWontBeExecuted)
{
	ExprFuncTag* tag;
	int i;

	if(!globalFuncTable)
	{
		globalFuncTable = stashTableCreateAddress(128);
		tagTable = stashTableCreateWithStringKeys(32, StashDefault);
		for(i = 0; i < ARRAY_SIZE(tagList); i++)
		{
			ExprFuncTag* tableTag = &tagList[i];
			if(!stashAddPointer(tagTable, tableTag->name, tableTag, false))
				Errorf("Duplicate tag found %s", tableTag->name);
			tableTag->tagNum = i;
		}
	}

	devassert(!desc->parsedTags);
	desc->parsedTags = calloc(TAG_LIST_BYTES, sizeof(U8));

	for(i = 0; i < ARRAY_SIZE(desc->tags); i++)
	{
		const char* tagStr = desc->tags[i].str;

		if(!tagStr)
			break;

		if(!stashFindPointer(tagTable, tagStr, &tag))
			ErrorFilenameDeferredf(NULL, "Expression function %s using unknown tag %s", desc->funcName, tagStr);
		else
		{
			/*
			devassertmsgf(!(desc->funcFlags & EXPR_FUNC_RQ_SELFPTR) || tag->allowSelfPtr,
				"Function %s requires a self pointer, but that's not allowed when using tag %s", desc->funcName, tag->name);
			devassertmsgf(!(desc->funcFlags & EXPR_FUNC_RQ_PARTITION) || tag->allowPartition,
				"Function %s requires a partition, but that's not allowed when using tag %s", desc->funcName, tag->name);
				*/
			desc->parsedTags[tag->tagNum / 8] |= 1 << (tag->tagNum % 8);
			eaPush(&tag->functions, desc);

			tag->usingSelfPtr |= !!(desc->funcFlags & EXPR_FUNC_RQ_SELFPTR);
			tag->usingPartition |= !!(desc->funcFlags & EXPR_FUNC_RQ_PARTITION);

			if(desc->funcFlags & EXPR_FUNC_RQ_SELFPTR && !tag->allowSelfPtr)
			{
				//changed this to an assert because hitting this error means that a tag had SelfPointerRequired turned on, which
				//is very likely to cause bazillions of errors, particularly for a tag like "util" that is used all over the place.
				assertmsgf(0, "Function %s requires a self pointer, but that's not allowed when using tag %s. This is super-bad because it will twiddle the self-pointer bit on this Expr tag, potentially causing a deluge of misleading errors", desc->funcName, tag->name);

//				ErrorFilenameDeferredf(NULL, "Function %s requires a self pointer, but that's not allowed when using tag %s", desc->funcName, tag->name);
			}
			if(desc->funcFlags & EXPR_FUNC_RQ_PARTITION && !tag->allowPartition)
				ErrorFilenameDeferredf(NULL, "Function %s requires a partition, but that's not allowed when using tag %s", desc->funcName, tag->name);
		}
	}

	if (desc->returnType.ptrType && !desc->returnType.ptrTypeName)
	{
		desc->returnType.ptrTypeName = ParserGetTableName(desc->returnType.ptrType);
	}
	if (desc->returnType.ptrTypeName && !desc->returnType.ptrType)
	{
		desc->returnType.ptrType = ParserGetTableFromStructName(desc->returnType.ptrTypeName);
	}
	if (desc->returnType.staticCheckType)
	{
		exprCheckStaticCheckType(desc->returnType.staticCheckType, desc->returnType.scTypeCategory);
	}

	for(desc->argc = 0;
		desc->args[desc->argc].type && desc->argc < ARRAY_SIZE(desc->args);
		desc->argc++)
	{
		ExprFuncArg* curArg = &desc->args[desc->argc];
		if(curArg->staticCheckType)
			exprCheckStaticCheckType(curArg->staticCheckType, curArg->scTypeCategory);

		if (curArg->ptrType && !curArg->ptrTypeName)
		{
			curArg->ptrTypeName = ParserGetTableName(curArg->ptrType);
		}
		if (curArg->ptrTypeName && !curArg->ptrType)
		{
			curArg->ptrType = ParserGetTableFromStructName(curArg->ptrTypeName);
		}
	}

	exprAddFuncToTable(globalFuncTable, desc, checkDuplicates, NULL);

	if (!bFunctionWontBeExecuted)
	{
		desc->eExprCodeEnum = StaticDefineIntGetInt(enumExprCodeEnum_Autogen, desc->pExprCodeName);
		assertmsgf(desc->eExprCodeEnum != -1, "Invalid exprCode %s. You probably need to recompile because you used structparser 'fast' mode.", desc->pExprCodeName);
	}
	else
	{
		//don't overwrite a "real" value that has already been set
		if (!desc->eExprCodeEnum)
		{
			desc->eExprCodeEnum = -1;
		}
	}
}

AUTO_COMMAND;
void printExprTagStats(void)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(tagList); i++)
	{
		ExprFuncTag* tag = &tagList[i];
		int j;

		printf("Tag: %s\n", tag->name);

		printf("SelfPtr: %s, %s\n",
			tag->allowSelfPtr ? "allowed" : "not allowed",
			tag->usingSelfPtr ? "using" : "not using");
		printf("Partition: %s, %s\n",
			tag->allowPartition ? "allowed" : "not allowed",
			tag->usingPartition ? "using" : "not using");

		for(j = eaSize(&tag->functions) - 1; j >= 0; j--)
		{
			ExprFuncDesc* func = tag->functions[j];
			if(func->funcFlags & EXPR_FUNC_RQ_SELFPTR)
				printf("S ");
			else
				printf("  ");
			if(func->funcFlags & EXPR_FUNC_RQ_PARTITION)
				printf("P ");
			else
				printf("  ");
			printf("  %s\n", tag->functions[j]->funcName);
		}
	}
}

void exprRegisterFunctionTable(ExprFuncDesc* exprTable, size_t numEntries, bool bFunctionWontBeExecuted)
{
	size_t i;

	for(i = 0; i < numEntries; i++)
	{
		ExprFuncDesc* curDesc = &exprTable[i];

		exprRegisterFunction(curDesc, true, bFunctionWontBeExecuted);
	}
}

ExprFuncDescContainer *exprGetAllFuncs()
{
	ExprFuncDescContainer *container = StructCreate(parse_ExprFuncDescContainer);
	StashTableIterator iter;
	StashElement el;

	stashGetIterator(globalFuncTable, &iter);

	while (stashGetNextElement(&iter, &el))
		eaPush(&container->funcs, (ExprFuncDesc*) stashElementGetPointer(el));

	return container;
}

// Returns <divident> modulus <divisor>
AUTO_EXPR_FUNC(util) ACMD_NAME(mod);
S64 exprFuncMod(int divident, int divisor)
{
	return divident % divisor;
}

// Subtracts <piece> from <total> until <total> is less than <piece>.
AUTO_EXPR_FUNC(util) ACMD_NAME(modfloat);
F32 exprFuncModFloat(F32 total, F32 piece)
{
	return total - (U32)(total / piece) * piece;
}

// Returns the sine of <angle> in degrees
AUTO_EXPR_FUNC(util) ACMD_NAME(Sine);
F32 exprFuncSine(F32 degAngle)
{
	return sin(RAD(degAngle));
}

// Returns the cosine of <angle> degrees
AUTO_EXPR_FUNC(util) ACMD_NAME(Cosine);
F32 exprFuncCosine(F32 degAngle)
{
	return cos(RAD(degAngle));
}

// Returns the square root of <value> or -1 if <value> is less than 0
AUTO_EXPR_FUNC(util) ACMD_NAME(SquareRoot);
F32 exprFuncSquareRoot(F32 value)
{
	if (value < 0)
		return -1;
	return sqrtf(value);
}

// Returns the angle of <slope> in degrees
AUTO_EXPR_FUNC(util) ACMD_NAME(ArcTan);
F32 exprFuncArcTan(F32 slope)
{
	return DEG(atan(slope));
}

// Returns the angle of <y> / <x> in degrees
AUTO_EXPR_FUNC(util) ACMD_NAME(ArcTan2);
F32 exprFuncArcTan2(F32 y, F32 x)
{
	return DEG(atan2(y, x));
}

static int enableExprConsolePrint = false;
AUTO_CMD_INT(enableExprConsolePrint, enableExprConsolePrint) ACMD_CMDLINE;

// DEBUGGING: prints the passed in string to the server console
AUTO_EXPR_FUNC(util) ACMD_NAME(ConsolePrint);
void exprFuncConsolePrint(const char* str)
{
	if(enableExprConsolePrint)
		printf("%s\n", str);
}

// Returns the current timestamp. Use this with TimeSince() to get useful times, the numbers
// by themselves mean nothing
AUTO_EXPR_FUNC(util) ACMD_NAME(GetCurTimeStamp);
S64 exprFuncGetCurTimeStamp(ExprContext *context)
{
	S64 time;

	if(context->partitionIsSet && g_PartitionTimeFunc)
		time = g_PartitionTimeFunc(exprContextGetPartition(context));
	else
		time = ABS_TIME;

	return time;
}

void exprSetPartitionTimeCallback(S64IntFunc cb)
{
	g_PartitionTimeFunc = cb;
}

// Returns the time (in seconds) that has passed since the timestamp passed in. Useful mostly
// with timestamps saved in variables or curStateTracker.lastEntryTime
AUTO_EXPR_FUNC(util) ACMD_NAME(TimeSince);
F32 exprFuncTimeSince(ExprContext *context, S64 time)
{
	S64 timeSince;
	
	if(context->partitionIsSet && g_PartitionTimeFunc)
	{
		S64 ptime = g_PartitionTimeFunc(exprContextGetPartition(context));
		timeSince = ptime ? ptime - time : UINT_MAX;
	}
	else
		timeSince = ABS_TIME_SINCE(time);

	return ABS_TIME_TO_SEC(timeSince);
}

//same as the above, but input time is secsSince2000
AUTO_EXPR_FUNC(util) ACMD_NAME(TimeSinceSS2000);
U32 exprFuncTimeSinceSS2000(U32 iInTime)
{
	return timeSecondsSince2000() - iInTime;
}

// Returns the number of days since January 1st, 2000.
AUTO_EXPR_FUNC(util) ACMD_NAME(TimeDaysSince2000);
int exprFuncDaysSince2000(void);
AUTO_EXPR_FUNC(util) ACMD_NAME(DaysSince2000);
int exprFuncDaysSince2000(void)
{
	return (int)(timeSecondsSince2000()/86400);
}

// Returns the number of hours since January 1st, 2000.
AUTO_EXPR_FUNC(util) ACMD_NAME(TimeHoursSince2000);
int exprFuncHoursSince2000(void);
AUTO_EXPR_FUNC(util) ACMD_NAME(HoursSince2000);
int exprFuncHoursSince2000(void)
{
	return (int)(timeSecondsSince2000()/3600);
}

// Returns the smaller value of the two passed in numbers
AUTO_EXPR_FUNC(util) ACMD_NAME(Min);
F32 exprFuncMin(F32 a, F32 b)
{
	return (a < b) ? a : b;
}

// Returns the bigger value of the two passed in numbers
AUTO_EXPR_FUNC(util) ACMD_NAME(Max);
F32 exprFuncMax(F32 a, F32 b)
{
	return (a > b) ? a : b;
}

// Clamps the number <v> between <min> and <max>
AUTO_EXPR_FUNC(util) ACMD_NAME(Clamp);
F32 exprFuncClamp(F32 v, F32 min, F32 max)
{
	return (v < min) ? min : (v > max) ? max : v;
}

// Returns true if the number <v> is in the range [<min>..<max>).  Note that it's inclusive of min, and exclusive of max.
AUTO_EXPR_FUNC(util) ACMD_NAME(RangeInEx);
S32 exprFuncRangeInEx(F32 v, F32 min, F32 max)
{
	return (v>=min && v<max);
}

// Returns the absolute value of the number <v>
AUTO_EXPR_FUNC(util) ACMD_NAME(Abs);
F32 exprFuncAbs(F32 v)
{
	return fabs(v);
}

// Returns the ceiling of the number <v>
AUTO_EXPR_FUNC(util) ACMD_NAME(Ceil);
F32 exprFuncCeil(F32 v)
{
	return ceilf(v);
}

// Returns the floor of the number <v>
AUTO_EXPR_FUNC(util) ACMD_NAME(Floor);
F32 exprFuncFloor(F32 v)
{
	return floorf(v);
}

// Returns the natural log of the number <v>
AUTO_EXPR_FUNC(util) ACMD_NAME(Log);
F32 exprFuncLog(F32 v)
{
	return log(v);
}

// Returns the base 2 log of the number <v>
AUTO_EXPR_FUNC(util) ACMD_NAME(Log2);
F32 exprFuncLog2(F32 v)
{
	static F32 fLog2 = 0;
	if(fLog2==0)
		fLog2 = log(2.f);
	if(v==0)
		return 0;
	return log(v)/fLog2;
}

// Returns a random integer
AUTO_EXPR_FUNC(util) ACMD_NAME(RandomIntFullRange);
S32 exprFuncRandomIntFullRange()
{
	return randomInt();
}

// Returns a random integer between <min> and <max> (inclusive)
AUTO_EXPR_FUNC(util) ACMD_NAME(RandomInt);
S32 exprFuncRandomInt(S32 min, S32 max)
{
	return randomIntRange(min, max);
}

// Returns a random integer between <min> and <max> (inclusive), using the provided random seed
AUTO_EXPR_FUNC(util) ACMD_NAME(RandomIntSeeded);
S32 exprFuncRandomIntSeeded(S32 min, S32 max, S32 seed)
{
	U32 tmp = (U32)seed;
	return randomIntRangeSeeded(&tmp, RandType_LCG, min, max);
}

// Returns a random float between <min> and <max> (actually [min, max), but close enough)
AUTO_EXPR_FUNC(util) ACMD_NAME(RandomFloat);
F32 exprFuncRandomFloat(F32 min, F32 max)
{
	float mag = max - min;
	float randFloat = min + (randomPositiveF32() * mag);

	return randFloat;
}

// Replaces each number n in the input string with n random numbers. Consecutive numbers
// are delimited by a space character
// Good for making random techy numbers in STO
AUTO_EXPR_FUNC(util) ACMD_NAME(RandomIntString);
const char* exprFuncRandomIntString(const char* pchNumbers)
{
	static char* s_estr = NULL;
	estrClear(&s_estr);
	while (pchNumbers && *pchNumbers)
	{
		if (*pchNumbers >= '0' && *pchNumbers <= '9')
		{
			int iLen = CLAMP((*pchNumbers) - '0', 1, 9);
			int iRand = randomIntRange(pow(10, iLen-1), (pow(10, iLen)-1));
			static char s_temp[10];
			sprintf(s_temp, "%d", iRand);
			estrAppend2(&s_estr, s_temp);
			if (*(pchNumbers+1) >= '0' && *(pchNumbers+1) <= '9')
				estrConcatChar(&s_estr, ' ');
		}
		else
		{
			estrConcatChar(&s_estr, *pchNumbers);
		}
		pchNumbers++;
	}
	return s_estr;
}

// Return a new string that is the result of concatenating the two given strings.
AUTO_EXPR_FUNC(util) ACMD_NAME(StringConcat);
const char* exprFuncStringConcat(ExprContext* context, const char *a, const char* b)
{
	size_t sz = strlen(a) + strlen(b) + 1;
	char *result = exprContextAllocScratchMemory(context, sz);
	strcpy_s(result, sz, a);
	strcat_s(result, sz, b);
	return result;
}

// Return the length of a string
AUTO_EXPR_FUNC(util) ACMD_NAME(StringLength);
S32 exprFuncStringLength(ExprContext* context, const char *a)
{
	if (!a)
		return 0;
	return (S32)strlen(a);
}

// Convert a string to a floating point number, or 0 if the string is not valid.
AUTO_EXPR_FUNC(util) ACMD_NAME(StringToFloat);
F32 exprFuncStringToFloat(const char* pchString)
{
	char* end;
	return (F32)strtod(pchString, &end);
}

// Convert a floating point value to its string representation, with two
// digits after the decimal point.
AUTO_EXPR_FUNC(util) ACMD_NAME(FloatToString);
const char *exprFloatToString(ExprContext *context, F32 value)
{  
	const int maxfloatsize = 64;
	char *output = exprContextAllocScratchMemory(context, maxfloatsize);
	snprintf_s(output, maxfloatsize, "%0.2f", value);
	return output;
}

// Deprecated - use IntToString.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenIntToString);
const char* exprFuncIntToString(ExprContext *pContext, S64 a);

// Deprecated - use IntToString.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenS64ToString);
const char* exprFuncIntToString(ExprContext *pContext, S64 a);

// Return a string printout of the given integer
AUTO_EXPR_FUNC(util) ACMD_NAME(IntToString);
const char* exprFuncIntToString(ExprContext *pContext, S64 a)
{
	const int maxint64size = 32;
	char *output = exprContextAllocScratchMemory(pContext, maxint64size);
	snprintf_s(output, maxint64size, "%"FORM_LL"i", a);
	return output;
}

// Return a string printout of the given integer
AUTO_EXPR_FUNC(util) ACMD_NAME(IntToStringWithZeros);
const char* exprFuncIntToStringWithZeros(ExprContext *pContext, S64 a, U32 width)
{
	const int maxint64size = 32;
	char *output = exprContextAllocScratchMemory(pContext, maxint64size);

	if(width<1)
		width = 1;
	if(width>32)
		width = 32;

	snprintf_s(output, maxint64size, "%0*"FORM_LL"i", width, a);

	return output;
}

// Return a int from a given string
AUTO_EXPR_FUNC(util) ACMD_NAME(StringToInt);
int exprFuncStringToInt(const char *pchInt)
{
	char *pchErr;
	int iVal = 0;
	iVal = strtol(pchInt, &pchErr, 10);
	return *pchErr ? -1 : iVal;
}

// Return a 64-bit int from a given string
AUTO_EXPR_FUNC(util) ACMD_NAME(StringToInt64);
S64 exprFuncStringToInt64(const char *pchInt)
{
	if(pchInt)
		return _atoi64(pchInt);
	else
		return -1;
}

// Convert all lowercase characters to uppercase
AUTO_EXPR_FUNC(util) ACMD_NAME(ToUpper);
const char *exprFuncToUpper(ExprContext* context, const char *s)
{
	size_t wsz, sz = strlen(s) + 1;
	wchar_t *wcs = (wchar_t *)alloca(sz * sizeof(wchar_t));
	char *result;
	UTF8ToWideStrConvert(s, wcs, (int)sz);
	for (wsz = 0; wcs[wsz]; wsz++)
		wcs[wsz] = UnicodeToUpper(wcs[wsz]);
	result = exprContextAllocScratchMemory(context, wsz * 6 + 1);
	WideToUTF8StrConvert(wcs, result, (int)(wsz * 6 + 1));
	return result;
}

// Convert all uppercase characters to lowercase
AUTO_EXPR_FUNC(util) ACMD_NAME(ToLower);
const char *exprFuncToLower(ExprContext* context, const char *s)
{
	size_t wsz, sz = strlen(s) + 1;
	wchar_t *wcs = (wchar_t *)alloca(sz * sizeof(wchar_t));
	char *result;
	UTF8ToWideStrConvert(s, wcs, (int)sz);
	for (wsz = 0; wcs[wsz]; wsz++)
		wcs[wsz] = UnicodeToLower(wcs[wsz]);
	result = exprContextAllocScratchMemory(context, wsz * 6 + 1);
	WideToUTF8StrConvert(wcs, result, (int)(wsz * 6 + 1));
	return result;
}

// Returns a random point between the passed in min and max coords
AUTO_EXPR_FUNC(util) ACMD_NAME(RandomPoint);
void exprFuncRandomPoint(ACMD_EXPR_LOC_MAT4_OUT matOut, F32 xMin, F32 xMax, F32 yMin, F32 yMax, F32 zMin, F32 zMax)
{
	matOut[3][0] = exprFuncRandomFloat(xMin, xMax);
	matOut[3][1] = exprFuncRandomFloat(yMin, yMax);
	matOut[3][2] = exprFuncRandomFloat(zMin, zMax);
}

// Stores a string variable with the specified name
AUTO_EXPR_FUNC(util) ACMD_NAME(SetStringVar);
void exprFuncSetStringVar(ExprContext* context, const char* varName, const char* value)
{
	const char* allocName = allocAddString(varName);

	exprContextSetStringVarEx(context, allocName, value, false, __FILE__, __LINE__);
}

// Returns the value of a string variable, returns "" if not found
AUTO_EXPR_FUNC(util) ACMD_NAME(GetStringVar);
const char* exprFuncGetStringVar(ExprContext* context, const char* varName)
{
	MultiVal *varValue = NULL;
	const char* strValue = 0;
	bool success = false;

	varValue = exprContextGetSimpleVarPooled(context,varName);

	if(varValue)
		strValue = MultiValGetString(varValue,&success);

	return success ? strValue : "";
}

// Stores an integer variable with the specified name
AUTO_EXPR_FUNC(util) ACMD_NAME(SetIntVar);
void exprFuncSetIntVar(ExprContext* context, const char* varName, int value)
{
	exprContextSetIntVarEx(context, varName, value, false, __FILE__, __LINE__);
}

// Returns the value of an integer variable, returns 0 if not found
AUTO_EXPR_FUNC(util) ACMD_NAME(GetIntVar);
int exprFuncGetIntVar(ExprContext* context, const char* varName)
{
	MultiVal *varValue = NULL;
	S64 intValue = 0;
	bool success = false;

	varValue = exprContextGetSimpleVarPooled(context,varName);

	if(varValue)
		intValue = MultiValGetInt(varValue,&success);

	return success ? intValue : 0;
}

// Stores a float variable with the specified name
AUTO_EXPR_FUNC(util) ACMD_NAME(SetFloatVar);
void exprFuncSetFloatVar(ExprContext* context, const char* varName, float value)
{
	exprContextSetFloatVarEx(context, varName, value, false, __FILE__, __LINE__);
}

// Returns the value of a float variable, returns 0 if not found
AUTO_EXPR_FUNC(util) ACMD_NAME(GetFloatVar);
float exprFuncGetFloatVar(ExprContext* context, const char* varName)
{
	MultiVal *varValue = NULL;
	F64 floatValue = 0;
	bool success = false;

	varValue = exprContextGetSimpleVarPooled(context,varName);

	if(varValue)
		floatValue = MultiValGetFloat(varValue,&success);

	return success ? floatValue : 0;
}

// Deprecated, use Int.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenFloatToInt);
S32 exprFuncInt(ExprContext* context, CMultiVal *pmv);

// Returns the integer value of a variable, or 0 if the value cannot be coerced to an integer.
AUTO_EXPR_FUNC(util) ACMD_NAME(Int);
S32 exprFuncInt(ExprContext* context, CMultiVal *pmv)
{
	bool tst = false;
	S32 i = MultiValGetInt(pmv, &tst);
	return tst ? i : 0;
}

// Returns the string value of a variable, or "" if the value cannot be coerced to an integer.
AUTO_EXPR_FUNC(util) ACMD_NAME(String);
const char *exprFuncString(ExprContext* context, CMultiVal *pmv)
{
	bool tst = false;
	const char *str = MultiValGetString(pmv, &tst);
	return tst ? str : "";
}

// Stores a point variable with the passed in name
AUTO_EXPR_FUNC(util) ACMD_NAME(SetPointVar);
void exprFuncSetPointVar(ExprContext* context, const char* varName, ACMD_EXPR_LOC_MAT4_IN matIn)
{
	MultiVal val = {0};
	val.type = MULTI_MAT4_F;
	val.ptr = matIn;
	exprContextSetSimpleVarEx(context, allocAddString(varName), &val, false, __FILE__, __LINE__);
}

// Returns an integer value with the specified bit set
AUTO_EXPR_FUNC(util) ACMD_NAME(Bit);
S32 exprFuncBit(ExprContext* context, S32 iBit)
{
	return iBit >= 0 ? 1 << iBit : 0;
}

// Returns the point referenced by a point variable, errors if not found
AUTO_EXPR_FUNC(util) ACMD_NAME(GetPointVar);
ExprFuncReturnVal exprFuncGetPointVar(ExprContext* context, ACMD_EXPR_LOC_MAT4_OUT matOut, const char* varName, ACMD_EXPR_ERRSTRING_STATIC errStr)
{
	MultiVal* val = exprContextGetSimpleVarPooled(context, varName);

	if(val && val->type == MULTI_MAT4_F)
	{
		copyMat4(val->vecptr, matOut);
		return ExprFuncReturnFinished;
	}
	else
	{
		static char* errBuf = NULL;
		estrPrintf(&errBuf, "Point Variable %s not found or not a point", varName);
		*errStr = errBuf;
		return ExprFuncReturnError;
	}
}

// WARNING: Makes a point out of the passed in coordinates. You most likely want to be using
// normal expression "coord:x,y,z" type syntax in your expression rather than using this function
AUTO_EXPR_FUNC(util) ACMD_NAME(MakePointFromCoords);
void exprFuncMakePointFromCoords(ACMD_EXPR_LOC_MAT4_OUT matOut, F32 x, F32 y, F32 z)
{
	matOut[3][0] = x;
	matOut[3][1] = y;
	matOut[3][2] = z;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCGetExternIntVar(ExprContext* context, ACMD_EXPR_INT_OUT outint,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if(ExprFuncReturnFinished == exprContextExternVarSC(context, category, name, NULL, NULL, MULTI_INT, NULL, false, errString))
	{
		*outint = 0;
		return ExprFuncReturnFinished;
	}

	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCGetExternFloatVar(ExprContext* context, ACMD_EXPR_FLOAT_OUT outfloat,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if(ExprFuncReturnFinished == exprContextExternVarSC(context, category, name, NULL, NULL, MULTI_FLOAT, NULL, false, errString))
	{
		*outfloat = 0;
		return ExprFuncReturnFinished;
	}

	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCGetExternStringVar(ExprContext* context, ACMD_EXPR_STRING_OUT outstr,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	if(ExprFuncReturnFinished == exprContextExternVarSC(context, category, name, NULL, NULL, MULTI_STRING, NULL, false, errString))
	{
		*outstr = MULTI_DUMMY_STRING;
		return ExprFuncReturnFinished;
	}

	return ExprFuncReturnError;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprFuncSCGetExternPointVar(ExprContext* context, ACMD_EXPR_LOC_MAT4_OUT outloc,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING errString)
{
    char *subErrString = NULL;
    if(ExprFuncReturnFinished == exprContextExternVarSC(context, category, name, NULL, NULL, MULTIOP_LOC_STRING, NULL, false, &subErrString))
	{
		copyMat4(unitmat, outloc);
        if (errString)
        {
            estrAppend2(errString, subErrString);
        }
		return ExprFuncReturnFinished;
	}

    if (errString)
    {
        estrAppend2(errString, subErrString);
    }

	return ExprFuncReturnError;
}

// Looks up float specified by <name> in registered category <category>. Usually used with
// encounters to specify things like AnimLists or powers
AUTO_EXPR_FUNC(util) ACMD_NAME(GetExternFloatVar) ACMD_EXPR_STATIC_CHECK(exprFuncSCGetExternFloatVar);
ExprFuncReturnVal exprFuncGetExternFloatVar(ExprContext* context, ACMD_EXPR_FLOAT_OUT outfloat,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;

	retval = exprContextGetExternVar(context, category, name, MULTI_FLOAT, &answer, errString);

	*outfloat = answer.floatval;

	return retval;
}

// Looks up int specified by <name> in registered category <category>. Usually used with
// encounters to specify things like AnimLists or powers
AUTO_EXPR_FUNC(util) ACMD_NAME(GetExternIntVar) ACMD_EXPR_STATIC_CHECK(exprFuncSCGetExternIntVar);
ExprFuncReturnVal exprFuncGetExternIntVar(ExprContext* context, ACMD_EXPR_INT_OUT outint,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;

	retval = exprContextGetExternVar(context, category, name, MULTI_INT, &answer, errString);

	*outint = answer.intval;

	return retval;
}

// Looks up string specified by <name> in registered category <category>. Usually used with
// encounters to specify things like AnimLists or powers
AUTO_EXPR_FUNC(util) ACMD_NAME(GetExternStringVar) ACMD_EXPR_STATIC_CHECK(exprFuncSCGetExternStringVar);
ExprFuncReturnVal exprFuncGetExternStringVar(ExprContext* context, ACMD_EXPR_STRING_OUT outstr,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;

	retval = exprContextGetExternVar(context, category, name, MULTI_STRING, &answer, errString);

	*outstr = answer.str;

	return retval;
}

// Looks up <name> in registered category <category> for the point specified. Usually used in
// encounters. Note that you still need the point tag inside the string in your encounter.
// E.g. you need to type "namedpoint:MyPoint" rather then "MyPoint"
AUTO_EXPR_FUNC(util) ACMD_NAME(GetExternPointVar) ACMD_EXPR_STATIC_CHECK(exprFuncSCGetExternPointVar);
ExprFuncReturnVal exprFuncGetExternPointVar(ExprContext* context, ACMD_EXPR_LOC_MAT4_OUT outloc,
					const char* category, const char* name, ACMD_EXPR_ERRSTRING errString)
{
	MultiVal answer = {0};
	ExprFuncReturnVal retval;
    char *subErrString = NULL;

	retval = exprContextGetExternVar(context, category, name, MULTIOP_LOC_STRING, &answer, &subErrString);

	if(retval == ExprFuncReturnFinished)
	{
		int ret = exprMat4FromLocationString(context, answer.str, outloc, 0, context->curExpr ? context->curExpr->filename : "");

		if(!ret)
		{
			if(errString)
            {
				estrPrintf(errString, "Unable to resolve point var: %s", answer.str);
            }
			MultiValClear(&answer);
			return ExprFuncReturnError;
		}
	}
    else if ( subErrString )
    {
        if(errString)
        {
            estrPrintf(errString, "%s", subErrString);
        }
    }

	MultiValClear(&answer);
	return retval;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(PointFromString);
ExprFuncReturnVal exprFuncPointFromString(ExprContext* context, ACMD_EXPR_LOC_MAT4_OUT outLoc,
											const char* str, ACMD_EXPR_ERRSTRING errString)
{
	MultiVal answer = {0};
	int ret = exprMat4FromLocationString(context, str, outLoc, 0, context->curExpr ? context->curExpr->filename : "");

	if(!ret)
	{
		estrPrintf(errString, "Unable to resolve point: %s", str);
		return ExprFuncReturnError;
	}

	return ExprFuncReturnFinished;
}

// Overrides the variable to the given value in the given category
AUTO_EXPR_FUNC(util) ACMD_NAME(OverrideExternStringVar);
ExprFuncReturnVal exprFuncOverrideExternStringVar(ExprContext* context, const char* category, const char* name, const char* value)
{
	MultiVal *val = MultiValCreate();

	val->type = MULTI_STRING;
	val->str = strdup(value);

	return exprContextOverrideExternVar(context, category, name, val);
}

// Overrides the variable to the given value in the given category
AUTO_EXPR_FUNC(util) ACMD_NAME(OverrideExternIntVar);
ExprFuncReturnVal exprFuncOverrideExternIntVar(ExprContext* context, const char* category, const char* name, S32 value)
{
	MultiVal *val = MultiValCreate();

	val->type = MULTI_INT;
	val->intval = value;

	return exprContextOverrideExternVar(context, category, name, val);
}

// Overrides the variable to the given value in the given category
AUTO_EXPR_FUNC(util) ACMD_NAME(OverrideExternFloatVar);
ExprFuncReturnVal exprFuncOverrideExternFloatVar(ExprContext* context, const char* category, const char* name, F32 value)
{
	MultiVal *val = MultiValCreate();

	val->type = MULTI_FLOAT;
	val->floatval = value;

	return exprContextOverrideExternVar(context, category, name, val);
}

// Returns whether <str1> contains the string <str2>
AUTO_EXPR_FUNC(util) ACMD_NAME(StrContains);
int exprFuncStrContains(const char *str1, const char *str2)
{
	return (strstri(str1, str2) ? 1 : 0);
}

// Removes whitespace from the beginning and the end of <str>
AUTO_EXPR_FUNC(util) ACMD_NAME(StrTrim);
const char *exprFuncStrTrim(ExprContext *context, const char *str)
{
	const char *pchStart = str;
	const char *end = str ? strchr(str, '\0') : NULL;
	const char *pchEnd = end - 1;
	char *pchResult = NULL;

	if (!end)
		return str;

	while (*pchStart && isspace((unsigned char)*pchStart))
		pchStart++;

	while (pchEnd > pchStart && isspace((unsigned char)*pchEnd))
		pchEnd--;

	if (pchStart <= str && end <= pchEnd + 1)
		return str;

	pchResult = exprContextAllocScratchMemory(context, (pchEnd + 1 - pchStart) + 1);
	strncpy_s(pchResult, (pchEnd + 1 - pchStart) + 1, pchStart, pchEnd + 1 - pchStart);
	return pchResult;
}

// Removes whitespace from the beginning of <str>
AUTO_EXPR_FUNC(util) ACMD_NAME("StrTrimHead");
const char *exprFuncStrTrimHead(ExprContext *context, const char *str)
{
	const char *pchStart = str;
	char *pchResult = NULL;
	S32 iLen;

	if (!str)
		return str;

	while (*pchStart && isspace((unsigned char)*pchStart))
		pchStart++;

	if (pchStart <= str)
		return str;

	iLen = (S32)strlen(str) + 1;
	pchResult = exprContextAllocScratchMemory(context, iLen);
	strcpy_s(pchResult, iLen, pchStart);
	return pchResult;
}

// Removes whitespace from the end of <str>
AUTO_EXPR_FUNC(util) ACMD_NAME("StrTrimTail");
const char *exprFuncStrTrimTail(ExprContext *context, const char *str)
{
	const char *end = str ? strchr(str, '\0') : NULL;
	const char *pchEnd = end - 1;
	char *pchResult = NULL;

	if (!end)
		return str;

	while (pchEnd > str && isspace((unsigned char)*pchEnd))
		pchEnd--;

	if (end <= pchEnd + 1)
		return str;

	pchResult = exprContextAllocScratchMemory(context, (pchEnd + 1 - str) + 1);
	strncpy_s(pchResult, (pchEnd + 1 - str) + 1, str, pchEnd - str);
	return pchResult;
}

// Reverse the contents of <str>
AUTO_EXPR_FUNC(util) ACMD_NAME("StrReverse");
const char *exprFuncStrReverse(ExprContext *context, const char *str)
{
	const char *end = str ? strchr(str, '\0') : NULL;
	const char *pchEnd = end - 1;
	char *pchResult = NULL;
	char *lo, *hi;

	if (!end)
		return str;

	pchResult = exprContextAllocScratchMemory(context, pchEnd - str + 1);
	strcpy_s(pchResult, pchEnd - str + 1, str);

	lo = pchResult;
	hi = pchResult + (pchEnd - str);

	while (lo < hi)
	{
		char tmp = *lo;
		*lo = *hi;
		*hi = tmp;
		lo++;
		hi--;
	}

	return pchResult;
}

// Repeat the contents of <str> <num> times
AUTO_EXPR_FUNC(util) ACMD_NAME("StrRepeat");
const char *exprFuncStrRepeat(ExprContext *context, const char *str, int num)
{
	const char *end = str ? strchr(str, '\0') : NULL;
	const char *pchEnd = end - 1;
	char *pchResult = NULL;
	size_t bufSize;

	if (!end)
		return str;
	if (num <= 0)
		return "";

	bufSize = (pchEnd - str) * num + 1;
	pchResult = exprContextAllocScratchMemory(context, bufSize);
	*pchResult = '\0';

	while (num-- > 0)
		strcat_s(pchResult, bufSize, str);

	return pchResult;
}

// Get a substring of <str> starting from <pos> until the end of the string. A negative number starts from the end of the string.
AUTO_EXPR_FUNC(util) ACMD_NAME("StrSubString");
const char *exprFuncStrSubString(ExprContext *context, const char *str, S64 pos)
{
	const char *end = str ? strchr(str, '\0') : NULL;
	char *pchResult = NULL;

	if (pos < 0)
	{
		if (end - str < -pos)
			return str;
		str = end + pos;
	}
	else
	{
		if (end - str < pos)
			return "";
		str += pos;
	}

	pchResult = exprContextAllocScratchMemory(context, end - str + 1);
	strcpy_s(pchResult, end - str + 1, str);
	return pchResult;
}

// Get a substring of <str> starting from <begin> (inclusive) to <end> (exclusive). A negative number starts from the end of the string.
AUTO_EXPR_FUNC(util) ACMD_NAME("StrSubPart");
const char *exprFuncStrSubPart(ExprContext *context, const char *str, S64 begin, S64 end)
{
	S64 len = (S64)strlen(str);
	const char *pchEnd = str + len - 1;
	char *pchResult = NULL;

	begin = begin < 0 ? begin + len : begin;
	end = end < 0 ? end + len : end;

	if (end > len)
		end = len;
	if (begin >= len || end <= begin)
		return "";

	pchResult = exprContextAllocScratchMemory(context, (end - begin) + 1);
	strncpy_s(pchResult, (end - begin) + 1, str + begin, (end - begin));
	return pchResult;
}

// Get the length of <str>
AUTO_EXPR_FUNC(util) ACMD_NAME("StrLength");
S64 exprFuncStrLength(const char *str)
{
	return (S64)strlen(str);
}

// Find the index of the first occurrence of <str2> in <str1>, or a negative number if not found.
AUTO_EXPR_FUNC(util) ACMD_NAME("StrFind");
S64 exprFuncStrFind(const char *str1, const char *str2)
{
	const char *pch = strstri(str1, str2);
	return pch ? (S64)(pch - str1) : -1;
}

// Find the index of the next occurrence of <str2> in <str1> starting from <pos>, or a negative number if not found.
AUTO_EXPR_FUNC(util) ACMD_NAME("StrFindNext");
S64 exprFuncStrFindNext(const char *str1, const char *str2, S64 pos)
{
	if (str1)
	{
		S64 len, start;
		const char *pch;
		ANALYSIS_ASSUME(str1 != NULL);
		len = (S64)strlen(str1);
		start = MAX(0, MIN(pos, len));
		pch = strstri(str1 + start, str2);
		if (pch)
		{
			return (S64)(pch - str1);
		}
	}
	
	return -1;
}

// Find the index of the preceding occurrence of <str2> in <str1> starting from <pos>, or a negative number if not found.
AUTO_EXPR_FUNC(util) ACMD_NAME("StrFindPrevious");
S64 exprFuncStrFindPrevious(const char *str1, const char *str2, S64 pos)
{
	const char *end = str1 ? strchr(str1, '\0') : NULL;
	S64 len = (S64)(end - str1);
	int len2 = (int)strlen(str2);

	pos = pos < len ? pos : len;
	if (pos < 0)
		return -1;

	do
	{
		if (strnicmp(str1 + pos, str2, len2) == 0)
			return pos;
		pos--;
	}
	while (pos > 0);
	return -1;
}

// Find the index of the last occurrence of <str2> in <str1>, or a negative number if not found.
AUTO_EXPR_FUNC(util) ACMD_NAME("StrFindLast");
S64 exprFuncStrFindLast(const char *str1, const char *str2)
{
	return exprFuncStrFindPrevious(str1, str2, strlen(str1));
}

int DEFAULT_LATELINK_exprFuncHelperEntIsAlive(Entity* be)
{
	devassertmsg(0, "Can't use GetEntArrayVar unless you define what it means for an entity to be alive");
	return false;
}

U32 DEFAULT_LATELINK_exprFuncHelperGetEntRef(Entity* be)
{
	devassertmsg(0, "Can't use GetEntArrayVar unless you override how to get entity refs");
	return false;
}

Entity* DEFAULT_LATELINK_exprFuncHelperBaseEntFromEntityRef(int iPartitionIdx, U32 ref)
{
	devassertmsg(0, "Can't use GetEntArrayVar unless you override how to get entities from entity refs");
	return false;
}

int DEFAULT_LATELINK_exprFuncHelperShouldExcludeFromEntArray(Entity* e)
{
	devassertmsg(0, "Can't use GetEntArrayVar unless you override which entities to exclude");
	return false;
}

int DEFAULT_LATELINK_exprFuncHelperShouldIncludeInEntArray(Entity* e, int getDead, int getAll)
{
	devassertmsg(0, "You must override which entities to include");
	return false;
}

// Stores an ent array for future reference
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(SetEntArrayVar);
void exprFuncSetEntArrayVar(ExprContext* context, ACMD_EXPR_ENTARRAY_IN entsIn, const char* varName)
{
	//TODO: at some point make this not create an earray, copy it and then destroy the first one
	MultiVal* storeVal = MultiValCreate();
	int i, num = eaSize(entsIn);

	storeVal->type = MULTI_INTARRAY_F;
	eaiSetSize(&storeVal->intptr, num);
	for(i = 0; i < num; i++)
		storeVal->intptr[i] = exprFuncHelperGetEntRef((*entsIn)[i]);

	exprContextSetSimpleVarEx(context, allocAddString(varName), storeVal, false, __FILE__, __LINE__);
	MultiValDestroy(storeVal);
}

ExprFuncReturnVal exprFuncGetEntArrayVarHelper(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* varName, ACMD_EXPR_ERRSTRING_STATIC errstr,
											   int all, int dead)
{
	int i;
	MultiVal* var = exprContextGetSimpleVarPooled(context, varName);

	if(!var)
		return ExprFuncReturnFinished;

	if(var->type != MULTI_INTARRAY_F)
	{
		*errstr = "Called GetEntArrayVar() on a non-entarray variable";
		return ExprFuncReturnError;
	}

	for(i = eaiSize(&var->intptr)-1; i >= 0; i--)
	{
		Entity* be = exprFuncHelperBaseEntFromEntityRef(iPartitionIdx, var->intptr[i]);
		if(be &&
		   (dead || exprFuncHelperEntIsAlive(be)) &&
		   (all || !exprFuncHelperShouldExcludeFromEntArray(be)))
		{
			eaPush(entsOut, be);
		}
	}

	return ExprFuncReturnFinished;
}

// Returns a new ent array filled with the contents of one stored previously. All dead/untargetable/
// unselectable/invisible entities are automatically filtered out
AUTO_EXPR_FUNC(entity,gameutil) ACMD_NAME(GetEntArrayVar);
ExprFuncReturnVal exprFuncGetEntArrayVar(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* varName, ACMD_EXPR_ERRSTRING_STATIC errstr)
{
	return exprFuncGetEntArrayVarHelper(context, iPartitionIdx, entsOut, varName, errstr, false, false);
}

// Returns a new ent array filled with the contents of one stored previously. All dead entities
// are automatically filtered out, but untargetable/unselectable/invisible ones are left in
AUTO_EXPR_FUNC(entity,gameutil) ACMD_NAME(GetEntArrayVarAll);
ExprFuncReturnVal exprFuncGetEntArrayVarAll(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* varName, ACMD_EXPR_ERRSTRING_STATIC errstr)
{
	return exprFuncGetEntArrayVarHelper(context, iPartitionIdx, entsOut, varName, errstr, true, false);
}

// Returns a new ent array filled with the contents of one stored previously. Returns all
// entities even dead ones
AUTO_EXPR_FUNC(entity,gameutil) ACMD_NAME(GetEntArrayVarDeadAll);
ExprFuncReturnVal exprFuncGetEntArrayVarDeadAll(ExprContext* context, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT entsOut, const char* varName, ACMD_EXPR_ERRSTRING_STATIC errstr)
{
	return exprFuncGetEntArrayVarHelper(context, iPartitionIdx, entsOut, varName, errstr, true, true);
}

// Returns the number of entities in an ent array
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntCount);
int exprFuncEntCount(ACMD_EXPR_ENTARRAY_IN entsIn)
{
	return eaSize(entsIn);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntArrayUnion);
void exprFuncEntArrayUnion(ACMD_EXPR_ENTARRAY_IN_OUT entsIn1, ACMD_EXPR_ENTARRAY_IN entsIn2)
{
	FOR_EACH_IN_EARRAY(*entsIn2, Entity, e)
	{
		eaPushUnique(entsIn1, e);
	}
	FOR_EACH_END;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(EntArrayIntersect);
void exprFuncEntArrayIntersect(ACMD_EXPR_ENTARRAY_IN_OUT entsInOut, ACMD_EXPR_ENTARRAY_IN entsIn2)
{
	FOR_EACH_IN_EARRAY(*entsIn2, Entity, e)
	{
		if(eaFind(entsInOut, e)==-1)
			eaRemoveFast(entsInOut, FOR_EACH_IDX(-, e));
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(*entsInOut, Entity, e)
	{
		if(eaFind(entsIn2, e)==-1)
			eaRemoveFast(entsInOut, FOR_EACH_IDX(-, e));
	}
	FOR_EACH_END;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(ChooseInt);
int exprChooseInt(int which, int a, int b)
{
	return which ? a : b;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(ChooseInt64);
S64 exprChooseInt64(int which, S64 a, S64 b)
{
	return which ? a : b;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(ChooseString);
const char *exprChooseString(int which, const char *a, const char *b)
{
	return which ? a : b;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(ChooseFloat);
F64 exprChooseFloat(int which, F64 a, F64 b)
{
	return which ? a : b;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(Vec);
void exprFuncVec3(ACMD_EXPR_LOC_MAT4_OUT matOut, F32 x, F32 y, F32 z)
{
	matOut[3][0] = x;
	matOut[3][1] = y;
	matOut[3][2] = z;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(Vec4);
void exprFuncVec4(ExprContext *pContext, ACMD_EXPR_VEC4_OUT outVec4, F32 x, F32 y, F32 z, F32 w)
{
	outVec4[0] = x;
	outVec4[1] = y;
	outVec4[2] = z;
	outVec4[3] = w;
}

// Return a 32 bit CRC of the given string.
AUTO_EXPR_FUNC(util) ACMD_NAME(CRC32);
U32 exprFuncCRC(ExprContext *pContext, const char *pch)
{
	return cryptAdler32(pch, strlen(pch));
}


AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprDeclareTypeCheck(ExprContext *pContext, const char *pchVar, const char *pchType, ACMD_EXPR_ERRSTRING errEstr)
{
	ParseTable *pTable = ParserGetTableFromStructName(pchType);
	if (!pTable)
	{
		estrPrintf(errEstr, "Invalid object type: %s.", pchType);
		return ExprFuncReturnError;
	}
	else if (!exprContextHasVar(pContext, pchVar))
	{
		estrPrintf(errEstr, "Invalid variable name: %s.", pchVar);
		return ExprFuncReturnError;
	}
	else
	{
		exprContextSetPointerVarPooled(pContext, pchVar, NULL, pTable, true, true);
		return ExprFuncReturnFinished;
	}
}

// Deprecated equivalent to DeclareType.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenDeclareVar) ACMD_EXPR_STATIC_CHECK(exprDeclareTypeCheck);
ExprFuncReturnVal exprDeclareType(ExprContext *pContext, const char *pchVar, const char *pchType, ACMD_EXPR_ERRSTRING errEstr);

static bool s_bExprDeclareTypeDebug = true;
AUTO_CMD_INT(s_bExprDeclareTypeDebug, ExprDeclareTypeDebug);

// Declare the type of a variable. Only useful when a variable can be one of several types;
// this usually only happens in UI expressions.
AUTO_EXPR_FUNC(util) ACMD_NAME(DeclareType) ACMD_EXPR_STATIC_CHECK(exprDeclareTypeCheck);
ExprFuncReturnVal exprDeclareType(ExprContext *pContext, const char *pchVar, const char *pchType, ACMD_EXPR_ERRSTRING errEstr)
{
	if (s_bExprDeclareTypeDebug && isDevelopmentMode())
	{
		ParseTable *pTable = ParserGetTableFromStructName(pchType);
		ParseTable *pIntended = NULL;
		ExprVarEntry *pEntry = exprContextGetVarPointerAndType(pContext, pchVar, &pIntended);
		if (pTable != pIntended)
		{
			estrPrintf(errEstr, "%s: Declared as %s but is actually %s", pchVar, pTable ? pTable->name : "No Table", pIntended ? pIntended->name : "No Table");
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

// Use this to check whether a handle is loaded by the ref system. ex: IsReferenceLoaded("ItemDef", pItem.hItemDef) . An invalid handle or type will yield 0.
AUTO_EXPR_FUNC(util) ACMD_NAME(IsReferenceLoaded);
int exprIsReferenceLoaded(const char* pchType, const char* pchReference)
{
	if (!RefSystem_ReferentFromString(pchType, pchReference))
	{
		//this reference is not in the dictionary pchType (yet).
		return 0;
	}
	return 1;
}

#define MAX_ACCESS_LEVEL_FOR_EXPRESSION_CALLABLE_COMMANDS 0

// Run a command.
AUTO_EXPR_FUNC(util) ACMD_NAME(RunCommand);
int exprRunCommand(ExprContext *pContext, const char *pchCommand)
{
	return globCmdParseAndReturn(pchCommand, NULL, 0, MAX_ACCESS_LEVEL_FOR_EXPRESSION_CALLABLE_COMMANDS, CMD_CONTEXT_HOWCALLED_UNSPECIFIED, NULL);
}


// Return the next string in the given list with cycling, e.g.
//    NextStringInList("A", "A B C") -> "B"
//    NextStringInList("C", "A B C") -> "A"
AUTO_EXPR_FUNC(util) ACMD_NAME(NextStringInList);
const char *exprNextStringInList(ExprContext *pContext, const char *pchString, const char *pchList)
{
	size_t szLength = strlen(pchList) + 1;
	char *pchListCopy = alloca(szLength);
	char *pchContext;
	char *pchStart;
	bool bNext = false;
	strcpy_s(pchListCopy, szLength, pchList);
	pchStart = strtok_r(pchListCopy, " ,\t\r\n", &pchContext);
	pchListCopy = pchStart;
	do 
	{
		if (bNext)
			return exprContextAllocString(pContext, pchStart);
		if (!stricmp(pchStart, pchString))
			bNext = true;
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	return exprContextAllocString(pContext, pchListCopy);
}

// Return the previous string in the given list with cycling, e.g.
//    PreviousStringInList("C", "A B C") -> "B"
//    PreviousStringInList("A", "A B C") -> "C"
AUTO_EXPR_FUNC(util) ACMD_NAME(PreviousStringInList);
const char *exprPreviousStringInList(ExprContext *pContext, const char *pchString, const char *pchList)
{
	size_t szLength = strlen(pchList) + 1;
	char *pchListCopy = alloca(szLength);
	char *pchContext;
	char *pchStart;
	const char *pchLast = NULL;
	strcpy_s(pchListCopy, szLength, pchList);
	pchStart = strtok_r(pchListCopy, " ,\t\r\n", &pchContext);
	do 
	{
		if (!stricmp(pchStart, pchString) && pchLast)
			return exprContextAllocString(pContext, pchLast);
		pchLast = pchStart;
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));

	// The first one in the list was focused, so focus the last one.
	return exprContextAllocString(pContext, pchLast);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprStaticDefineIntGetKeyCheck(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchName, S32 iValue, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	if (!pList)
	{
		estrPrintf(errEstr, "Invalid StaticDefine Name: %s", pchName);
		return ExprFuncReturnError;
	}

	*ppchOut = MULTI_DUMMY_STRING;
	return ExprFuncReturnFinished;
}

// Convert a StaticDefineInt value into a string key (untranslated).
AUTO_EXPR_FUNC(util) ACMD_NAME("StaticDefineIntGetKey") ACMD_EXPR_STATIC_CHECK(exprStaticDefineIntGetKeyCheck);
ExprFuncReturnVal exprStaticDefineIntGetKey(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, const char *pchName, S32 iValue, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	*ppchOut = "Invalid StaticDefine/Key";
	if (pList)
	{
		const char *pch = StaticDefineIntRevLookup(pList, iValue);
		if (pch)
			*ppchOut = pch;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprStaticDefineIntGetIntCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, const char *pchName, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	if (!pList)
	{
		estrPrintf(errEstr, "Invalid StaticDefine Name: %s", pchName);
		return ExprFuncReturnError;
	}
	if (!stricmp(pchKey, MULTI_DUMMY_STRING) && !strStartsWith(pchKey, MULTI_DUMMY_STRING) && !strEndsWith(pchKey, MULTI_DUMMY_STRING))
	{
		if (StaticDefineInt_FastStringToInt(pList, pchKey, INT_MIN) == INT_MIN)
		{
			estrPrintf(errEstr, "Invalid StaticDefine Key '%s' for '%s'", pchKey, pchName);
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

// Convert a StaticDefineInt key into a value.
AUTO_EXPR_FUNC(util) ACMD_NAME(StaticDefineIntGetInt) ACMD_EXPR_STATIC_CHECK(exprStaticDefineIntGetIntCheck);
ExprFuncReturnVal exprStaticDefineIntGetInt(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, const char *pchName, const char *pchKey, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	*piOut = -1;
	if (pList)
	{
		int iOut = StaticDefineInt_FastStringToInt(pList, pchKey, INT_MIN);
		if (iOut == INT_MIN)
		{
			estrPrintf(errEstr, "Invalid StaticDefine Key '%s' for '%s'", pchKey, pchName);
			return ExprFuncReturnError;
		}
		else
			*piOut = iOut;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprStaticDefineIntGetIntDefaultCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, const char *pchName, const char *pchKey, S32 iDefault, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	if (!pList)
	{
		estrPrintf(errEstr, "Invalid StaticDefine Name: %s", pchName);
		return ExprFuncReturnError;
	}
	if (!stricmp(pchKey, MULTI_DUMMY_STRING) && !strStartsWith(pchKey, MULTI_DUMMY_STRING) && !strEndsWith(pchKey, MULTI_DUMMY_STRING))
	{
		if (StaticDefineInt_FastStringToInt(pList, pchKey, INT_MIN) == INT_MIN)
		{
			estrPrintf(errEstr, "Invalid StaticDefine Key '%s' for '%s'", pchKey, pchName);
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

// Convert a StaticDefineInt key into a value.
AUTO_EXPR_FUNC(util) ACMD_NAME(StaticDefineIntGetIntDefault) ACMD_EXPR_STATIC_CHECK(exprStaticDefineIntGetIntDefaultCheck);
ExprFuncReturnVal exprStaticDefineIntGetIntDefault(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, const char *pchName, const char *pchKey, S32 iDefault, ACMD_EXPR_ERRSTRING errEstr)
{
	StaticDefineInt *pList = FindNamedStaticDefine(pchName);
	*piOut = iDefault;
	if (pList)
	{
		int iOut = StaticDefineInt_FastStringToInt(pList, pchKey, INT_MIN);
		if (iOut != INT_MIN)
			*piOut = iOut;
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal exprColorCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, const char *pchName, ACMD_EXPR_ERRSTRING errEstr)
{
	if (*pchName != '#' && !stricmp(pchName, MULTI_DUMMY_STRING) && !strStartsWith(pchName, MULTI_DUMMY_STRING) && !strEndsWith(pchName, MULTI_DUMMY_STRING))
	{
		if (StaticDefineInt_FastStringToInt(ColorEnum, pchName, INT_MIN) == INT_MIN)
		{
			estrPrintf(errEstr, "Invalid color name: %s", pchName);
			return ExprFuncReturnError;
		}
	}
	return ExprFuncReturnFinished;
}

// Get the color value for the given name. If the named color is missing it will return white.
AUTO_EXPR_FUNC(util) ACMD_NAME(Color) ACMD_EXPR_STATIC_CHECK(exprColorCheck);
ExprFuncReturnVal exprColor(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, const char *pchName, ACMD_EXPR_ERRSTRING errEstr)
{
	*piOut = -1;
	if (*pchName == '#')
	{
		// Parse "HTML" color, uses SMF's algorithm
		sscanf(pchName, "#%X", piOut);
		if (strlen(pchName) <= 7)
			// #1234567
			*piOut = (*piOut << 8) | 0xff;
	}
	else
	{
		int iOut = StaticDefineInt_FastStringToInt(ColorEnum, pchName, INT_MIN);
		if (iOut != INT_MIN)
		{
			*piOut = iOut;
		}
	}
	return ExprFuncReturnFinished;
}

// Convert a color value from an int packed form to a byte packed form (i.e. endian swap if necessary)
AUTO_EXPR_FUNC(util) ACMD_NAME(RGBAIntSwapBytes);
U32 exprRGBAIntSwapBytes(U32 uRGBA)
{
	return ((uRGBA >> 24) & 0x000000ff) | ((uRGBA >> 8) & 0x0000ff00) | ((uRGBA << 8) & 0x00ff0000) | ((uRGBA << 24) & 0xff000000);
}

// Return the time in seconds since 2000 given a date. Pass "now" for the current time. Pass "serverNow" for current server time.
// e.g. TimeSecondsSince2000("now") > TimeSecondsSince2000("2009-09-05")
AUTO_EXPR_FUNC(util) ACMD_NAME(TimeSecondsSince2000);
S64 exprTimeSecondsSince2000(ExprContext *pContext, const char *time)
{
	if (!stricmp("now", time))
	{
		return timeSecondsSince2000();
	}
	else if (!stricmp("servernow", time))
	{
		return timeServerSecondsSince2000();
	}
	else
	{
		S64 iValue = timeGetSecondsSince2000FromDateString(time);
		if (iValue == 0)
		{
			ErrorFilenamef(
				exprContextGetBlameFile(pContext),
				"%s is not a valid time string (YYYY-MM-DD HH:MM, or \"now\" or \"serverNow\" )", time);
		}
		return iValue;
	}
}

AUTO_EXPR_FUNC(util) ACMD_NAME(TimeLocalOffsetFromUTC);
S64 exprTimeLocalOffsetFromUTC()
{
	return timeLocalOffsetFromUTC();
}

static int sDebugTemp = 0;
AUTO_EXPR_FUNC(util) ACMD_NAME("Debug_BreakPoint");
void Debug_BreakPoint(int i)
{
	if (i == 0)
	{
		sDebugTemp = i;
	}

	return;
}

void UpdateCRCFromStringSafe(const char *pStr)
{
	if (pStr && pStr[0])
	{
		cryptAdler32Update_IgnoreCase(pStr, (int)strlen(pStr));
	}
}


void UpdateCRCFromExprFuncArg(const ExprFuncArg *pArg)
{
	cryptAdler32Update_AutoEndian((char*)(&pArg->type), sizeof(MultiValType));
	UpdateCRCFromStringSafe(pArg->name);
	UpdateCRCFromStringSafe(pArg->staticCheckType);
	if (pArg->ptrType)
	{
		UpdateCRCFromStringSafe(ParserGetTableName(pArg->ptrType));
	}
	
	cryptAdler32Update_AutoEndian((char*)&pArg->scTypeCategory, sizeof(pArg->scTypeCategory));
	cryptAdler32Update_AutoEndian((char*)&pArg->allowNULLPtr, sizeof(pArg->allowNULLPtr));
}

void UpdateCRCFromExprFuncDesc(const ExprFuncDesc *pFunc)
{
	int bHasFunc = !!pFunc->pExprStaticCheckFunc;
	int i;

	UpdateCRCFromStringSafe(pFunc->funcName);
	
	for (i=0; i < ARRAY_SIZE(pFunc->tags); i++)
	{
		if(!pFunc->tags[i].str)
		{
			break;
		}
		UpdateCRCFromStringSafe(pFunc->tags[i].str);
	}

	for (i=0; i < ARRAY_SIZE(pFunc->args); i++)
	{
		if (!pFunc->args[i].type)
		{
			break;
		}
		UpdateCRCFromExprFuncArg(&pFunc->args[i]);
	}

	cryptAdler32Update_AutoEndian((char *)&bHasFunc, sizeof(bHasFunc));

	cryptAdler32Update_AutoEndian((char*)(&pFunc->returnType), sizeof(MultiValType));

}

U32 exprFuncGetCRC(const char *pchFuncName)
{
	ExprFuncDesc *funcDesc = exprGetFuncDesc(NULL, allocAddString(pchFuncName));
	if (!funcDesc)
	{
		return 0;
	}
	cryptAdler32Init();
	UpdateCRCFromExprFuncDesc(funcDesc);
	return cryptAdler32Final();
}

bool exprFuncCanArgBeConverted(ExprFuncArg *pSource, ExprFuncArg *pDest)
{
	MultiValType sourceType = pSource->type;

	// Do int->float conversion
	if (pDest->type == MULTI_FLOAT && sourceType == MULTI_INT)
		sourceType = MULTI_FLOAT;

	if (pDest->type != sourceType)
		return false;

	if (stricmp(pDest->ptrTypeName, pSource->ptrTypeName) != 0)
	{
		return false;
	}

	if (pDest->staticCheckType && pSource->staticCheckType)
	{
		if (pDest->scTypeCategory != pSource->scTypeCategory)
			return false;
		return stricmp(pDest->staticCheckType, pSource->staticCheckType) == 0;
	}
	return true;
}

// Return the distance that the mouse is from the gen's unpadded screen box.
AUTO_EXPR_FUNC(util) ACMD_NAME(GetHueFromRGB);
F32 exprGetHueFromRGB(F32 fR, F32 fG, F32 fB)
{
	Vec3 vRGB = { fR, fG, fB };
	Vec3 vHSV;
	rgbToHsv(vRGB, vHSV);
	return vHSV[0];
}

// Return the distance that the mouse is from the gen's unpadded screen box.
AUTO_EXPR_FUNC(util) ACMD_NAME(GetSaturationFromRGB);
F32 exprGetSaturationFromRGB(F32 fR, F32 fG, F32 fB)
{
	Vec3 vRGB = { fR, fG, fB };
	Vec3 vHSV;
	rgbToHsv(vRGB, vHSV);
	return vHSV[1];
}

// Return the distance that the mouse is from the gen's unpadded screen box.
AUTO_EXPR_FUNC(util) ACMD_NAME(GetValueFromRGB);
F32 exprGetValueromRGB(F32 fR, F32 fG, F32 fB)
{
	Vec3 vRGB = { fR, fG, fB };
	Vec3 vHSV;
	rgbToHsv(vRGB, vHSV);
	return vHSV[2];
}

// Interpolate a number From -> To. Where Mod controls the interpolation.
AUTO_EXPR_FUNC(util) ACMD_NAME(LinearInterpolate);
F32 exprLinearInterpolate(F32 fFrom, F32 fTo, F32 fMod)
{
	return fFrom + (fTo - fFrom) * fMod;
}

// Returns a point's x-coord
AUTO_EXPR_FUNC(util) ACMD_NAME(PointGetX);
F32 exprFuncPointGetX(ACMD_EXPR_LOC_MAT4_IN pointIn)
{
	return pointIn[3][0];
}

// Returns a point's x-coord
AUTO_EXPR_FUNC(util) ACMD_NAME(PointGetY);
F32 exprFuncPointGetY(ACMD_EXPR_LOC_MAT4_IN pointIn)
{
	return pointIn[3][1];
}

// Returns a point's x-coord
AUTO_EXPR_FUNC(util) ACMD_NAME(PointGetZ);
F32 exprFuncPointGetZ(ACMD_EXPR_LOC_MAT4_IN pointIn)
{
	return pointIn[3][2];
}

// Returns a point offset from the original point by + or - up to the amount specified for each axis
AUTO_EXPR_FUNC(util) ACMD_NAME(PointAddRandomOffset);
void exprFuncPointAddRandomOffset(ACMD_EXPR_LOC_MAT4_OUT pointOut, ACMD_EXPR_LOC_MAT4_IN pointIn, F32 x, F32 y, F32 z)
{
	copyMat4(pointIn, pointOut);

	pointOut[3][0] += randomF32() * x;
	pointOut[3][1] += randomF32() * y;
	pointOut[3][2] += randomF32() * z;
}

// Returns a point offset from the original point by the FULL distance.
AUTO_EXPR_FUNC(util) ACMD_NAME(PointAddOffset);
void exprFuncPointAddOffset(ACMD_EXPR_LOC_MAT4_OUT pointOut, ACMD_EXPR_LOC_MAT4_IN pointIn, F32 x, F32 y, F32 z)
{
	copyMat4(pointIn, pointOut);

	pointOut[3][0] += x;
	pointOut[3][1] += y;
	pointOut[3][2] += z;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(StringStripTags);
const char *exprStringStripTags(ExprContext *context, const char* pchString, bool bPrettyPrint)
{
	static char* estrResult = NULL;
	estrClear(&estrResult);
	if (bPrettyPrint) {
		StringStripTagsPrettyPrint(pchString, &estrResult);
	} else {
		StringStripTagsSafe(pchString, &estrResult);
	}
	return exprContextAllocString(context, estrResult);
}

AUTO_EXPR_FUNC(util) ACMD_NAME(StringStripFontTags);
const char *exprStringStripFontTags(ExprContext *context, const char* pchString)
{
	static char* estrResult = NULL;
	estrClear(&estrResult);
	StringStripFontTags(pchString, &estrResult);
	return exprContextAllocString(context, estrResult);
}

AUTO_EXPR_FUNC(util) ACMD_NAME("IsProductionEditMode");
bool exprIsProductionEditMode( void )
{
	return isProductionEditMode();
}

#include "ExpressionFunc_h_ast.c"
