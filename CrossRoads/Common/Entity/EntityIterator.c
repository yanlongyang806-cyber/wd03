/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "EntityIterator.h"

#include "GlobalTypes.h"
#include <stdarg.h>
#include "entitysysteminternal.h"
#include "estring.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

EntityIterator gEntityIterators[MAX_ENTITY_ITERATORS];
EntityIterator *gpFirstFreeEntityIterator;

static __forceinline bool EntityIterator_CheckFlags(EntityIterator *pIterator, Entity *pEntity)
{
	if (FlagsMatchAll(pEntity->myEntityFlags,pIterator->iFlagsToRequire) &&
		FlagsMatchNone(pEntity->myEntityFlags,pIterator->iFlagsToExclude))
	{
		return true;
	}
	return false;
}


static __forceinline bool EntityIterator_CheckPartition(EntityIterator *pIterator, Entity *pEntity)
{
	if ((PARTITION_ANY == pIterator->iPartitionIdx) || (pEntity->iPartitionIdx_UseAccessor == pIterator->iPartitionIdx))
	{
		return true;
	}
	return false;
}


Entity *EntityIterator_GetNextEntity_NewType(EntityIterator *pIterator)
{
	do
	{
		pIterator->eCurType = pIterator->eTypeList[pIterator->eTypeListIndex++];

		if (!pIterator->eCurType)
		{
			pIterator->eCurType = GLOBALTYPE_MAXTYPES;
			return NULL;
		}

		assert(pIterator->eCurType < ARRAY_SIZE(gpEntityTypeLists));
		pIterator->ptr.pLastNodeReturned = gpEntityTypeLists[pIterator->eCurType];

		while (pIterator->ptr.pLastNodeReturned)
		{
			Entity *pEntity;

			pEntity = ENTITY_FROM_LISTNODE(pIterator->ptr.pLastNodeReturned);

			if (EntityIterator_CheckFlags(pIterator, pEntity) && EntityIterator_CheckPartition(pIterator, pEntity))
			{
				return pEntity;
			}

			pIterator->ptr.pLastNodeReturned = pIterator->ptr.pLastNodeReturned->pNext;
		}

	} while (1);
}


Entity *EntityIteratorGetNext(EntityIterator *pIterator)
{
	Entity *pEntity;

	ASSERT_INITTED();
	assert(pIterator);
	assert(pIterator->eCurType != ENTITYTYPE_INVALID);

	if (pIterator->eCurType == GLOBALTYPE_MAXTYPES)
	{
		return NULL;
	}

	if (pIterator->ptr.pLastNodeReturned == NULL)
	{
		return EntityIterator_GetNextEntity_NewType(pIterator);
	}

	do
	{
		pIterator->ptr.pLastNodeReturned = pIterator->ptr.pLastNodeReturned->pNext;

		if (pIterator->ptr.pLastNodeReturned == NULL)
		{
			return EntityIterator_GetNextEntity_NewType(pIterator);
		}

		pEntity = ENTITY_FROM_LISTNODE(pIterator->ptr.pLastNodeReturned);

		if (EntityIterator_CheckFlags(pIterator, pEntity) && EntityIterator_CheckPartition(pIterator, pEntity))
		{
			return pEntity;
		}
	} while (1);
}


void EntityIterator_Backup(EntityIterator *pIterator)
{
	if (pIterator->ptr.pLastNodeReturned)
	{
		if (pIterator->ptr.pLastNodeReturned->pPrev)
		{
			pIterator->ptr.pLastNodeReturned = pIterator->ptr.pLastNodeReturned->pPrev;
		}
		else
		{
			pIterator->ptr.pLastNodeReturned = NULL;
			pIterator->eTypeListIndex--;
		}
	}
}


EntityIterator *GetEntityIterator_Internal(int iPartitionIdx, EntityFlags iFlagsToRequire, EntityFlags iFlagsToExclude, char *pFile, int iLineNum)
{	
	EntityIterator *pIterator;
	int i;

	ASSERT_INITTED();

	pIterator = gpFirstFreeEntityIterator;


	//if this assert is hit, it's probably because someone is failing to call EntityIterator_Release, or else there
	//might be some savage nexted EntityIterator action going on
	if (!pIterator)
	{
		char *pErrorString = NULL;
	
		estrPrintf(&pErrorString, "No free entity iterators. Someone is probably not releasing them. They are currently owned by: ");

		for (i=0; i < MAX_ENTITY_ITERATORS; i++)
		{
			char shortFileName[CRYPTIC_MAX_PATH];
			getFileNameNoDir(shortFileName, gEntityIterators[i].pDbgFile);
			estrConcatf(&pErrorString, " %s(%d) ", shortFileName,gEntityIterators[i].iDbgLine); 
		}

		assertmsg(pIterator, pErrorString);
	}

	gpFirstFreeEntityIterator = (EntityIterator *)(pIterator->ptr.pLastNodeReturned);

	ZeroStruct(pIterator);

	pIterator->iPartitionIdx = iPartitionIdx;
	pIterator->iFlagsToRequire = iFlagsToRequire;
	pIterator->iFlagsToExclude = iFlagsToExclude;

	pIterator->pDbgFile = pFile;
	pIterator->iDbgLine = iLineNum;

	return pIterator;
}


EntityIterator *entGetIteratorAllTypesEx(char *pFile, int iLine, int iPartitionIdx, EntityFlags iFlagsToRequire, EntityFlags iFlagsToExclude)
{
	int i;
	EntityIterator *pIterator = GetEntityIterator_Internal(iPartitionIdx, iFlagsToRequire, iFlagsToExclude, pFile, iLine);

	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		if (GlobalTypeParent(i) == GLOBALTYPE_ENTITY)
		{
			pIterator->eTypeList[pIterator->eTypeListIndex++] = i;
		}
	}
	pIterator->eTypeListIndex = 0;

	return pIterator;
}


EntityIterator *entGetIteratorSingleTypeEx(char *pFile, int iLine, int iPartitionIdx, EntityFlags iFlagsToRequire, EntityFlags iFlagsToExclude, GlobalType eType)
{
	EntityIterator *pIterator = GetEntityIterator_Internal(iPartitionIdx, iFlagsToRequire, iFlagsToExclude, pFile, iLine);

	pIterator->eTypeList[0] = eType;

	return pIterator;
}


EntityIterator *entGetIteratorMultipleTypesEx(char *pFile, int iLine, int iPartitionIdx, EntityFlags iFlagsToRequire, EntityFlags iFlagsToExclude, ...)
{
	va_list		list;
	U32 eCurType;
	EntityIterator *pIterator;

	va_start(list, iFlagsToExclude);

	pIterator = GetEntityIterator_Internal(iPartitionIdx, iFlagsToRequire, iFlagsToExclude, pFile, iLine);

	while ((eCurType = va_arg(list, GlobalType)) != GLOBALTYPE_NONE)
	{
		assert(pIterator->eTypeListIndex < GLOBALTYPE_MAXTYPES);
		assert(eCurType > GLOBALTYPE_NONE && eCurType < GLOBALTYPE_MAXTYPES);
		pIterator->eTypeList[pIterator->eTypeListIndex++] = eCurType;
	}

	pIterator->eTypeListIndex = 0;

	va_end(list);

	return pIterator;
}


void EntityIteratorRelease(EntityIterator *pEntityIterator)
{
	ASSERT_INITTED();
	assert(pEntityIterator);

	pEntityIterator->eCurType = ENTITYTYPE_INVALID;
	pEntityIterator->ptr.pNext = gpFirstFreeEntityIterator;
	gpFirstFreeEntityIterator = pEntityIterator;
}


int CountEntitiesOfType(GlobalType eType)
{
	int iCount = 0;
	Entity *pEnt;

	EntityIterator *pIterator = entGetIteratorSingleType(PARTITION_ANY, 0, 0, eType);

	while ((pEnt = EntityIteratorGetNext(pIterator)))
	{
		iCount++;
	}

	EntityIteratorRelease(pIterator);

	return iCount;
}


AUTO_COMMAND;
int CountEntitiesOfNamedType(char *pName)
{
	GlobalType eType = NameToGlobalType(pName);

	if (eType == GLOBALTYPE_NONE)
	{
		return 0;
	}
	else
	{
		return CountEntitiesOfType(eType);
	}
}


//This is some test-case data for AUTO_STRUCT and AUTO_ENUM. Please leave it here for now. Thanks

/*
AUTO_STRUCT;
typedef struct CrazyThing2
{
	int foo; AST( CLIENT_ONLY ))
	int bar; AST( SERVER_ONLY ))


	int wappa; AST( NO_TRANSACT ))
	int wakka; 
} CrazyThing2;
#include "entityiterator_c_ast.c"




AST_MACRO(( INT1, FORMAT_IP FORMAT_LVWIDTH(3) DEFAULT(3) ))
AST_MACRO(( INT12, FORMAT_KBYTES FORMAT_LVWIDTH(5) DEFAULT(14) ))

AUTO_STRUCT;
typedef struct CrazyThing
{
	int foo; AST( INT1 ))
	int bar; AST( INT12 ))
	int wappa; AST( INT12 ))

	int wakka; AST( INT1 ))
} CrazyThing;



AUTO_STRUCT;
typedef struct CrazyThing2
{
	int foo; //AST( USERFLAG(flag1) USERFLAG(superlongflag3) ))
	int bar; 

AST_PREFIX(( ))

	int wappa; 
	int wakka; 
} CrazyThing2;

AST_PREFIX(( ))

AUTO_STRUCT;
typedef struct BigMysteriousThing
{
	int foo;
} BigMysteriousThing;

//StaticDefine mySubTable[] = {0};
//StaticDefine myOtherSubTable[] = {0};


AUTO_STRUCT AST_IGNORE(old1) AST_IGNORE(old2) AST_STARTTOK("") AST_ENDTOK(SuperLongEndTokenLetsTryItOutandSee) ;
typedef struct TestStruct
{
	int x;// AST(SUBTABLE(mySubTable) FORMAT_IP FORMAT_LVWIDTH(3) DEFAULT(29 + 13)))
	float fHappyVariable; AST( FORMAT_UI_RESIZABLE FORMAT_UI_RIGHT FLOAT_TENTHS DEFAULT(10.3)))
	char *pString; AST(STRUCTPARAM FORMAT_LVWIDTH(6)))


	float *pTestArray;

	char *pTestFileName; AST(FILENAME))
	char *pTestCurrentFile; AST(CURRENTFILE NAME(CurrentFile)))


	char *pTestString; AST( DEFAULT("c:\\foo\\happy.txt") ))

	int *lotsOfInts;// AST(SUBTABLE(myOtherSubTable)))

	CrazyThing thing; AST(STRUCT(parse_CrazyThing)))

	int iTimeStamp; AST(TIMESTAMP))
	int iLineNum; AST(LINENUM MINBITS(4)))

	int iFlags; AST(FLAGS DEFAULT(14)))

	U8 boolFlag; AST(BOOLFLAG DEFAULT(194)))

	int ***test[15][64]; NO_AST

	int usedFieldSize;
	void *pUsedField; AST(USEDFIELD(usedFieldSize)))



	BigMysteriousThing things[2]; AST( INDEX(0, firstThing) INDEX(1, secondThing)))

	BigMysteriousThing otherThingy; AST(RAW(25)))

	int iPointerSize; 
	
	void *pCrazyPointer; AST(POINTER(iPointerSize)))

	REF_TO(BigMysteriousThing) hPower; AST(REFDICT(PowerDictionary)))

	int testArray[1000];

	Mat4 m;
	Quat q;
	Vec3 v3;
	Vec2 v2;

	U8 iColor[3]; AST(RGB))
	float fPos[3]; AST(VEC3))

//	TokenizerParams **ppSomeUnparsedData;
//	TokenizerFunctionCall **ppSomeFunctionCall;

//	StashTable myStashTable;

	char **ppLotsOfStrings;

} TestStruct;


AUTO_ENUM 
typedef enum 
{
	MAGICSET_ALPHA,				ENAMES(Alpha Beta Unlimited)
	MAGICSET_ARABIAN_NIGHTS,	ENAMES(ArabianNights Arabians)
	MAGICSET_ANTIQUITIES,		ENAMES(Antiquities)
	MAGICSET_LEGENDS,			ENAMES(Legends)
	MAGICSET_THEDARK,			ENAMES(TheDark Dark)
	MAGICSET_FALLENEMPIRES,		ENAMES(Fallen FallenEmpires)
} magicSet;

AUTO_ENUM
typedef enum 
{
	BUG_BEETLE,
	BUG_LADYBUG,
	BUG_GOLIATHBEETLE,
} bugType;

AUTO_STRUCT;
typedef struct oldNewSetStruct
{
	magicSet oldSet;
	magicSet newSet;
} oldNewSetStruct;

AUTO_ENUM
typedef enum
{ 
	DAYOFWEEK_MONDAY,		ENAMES(Monday Mon)
	DAYOFWEEK_TUESDAY,		ENAMES(Tuesday Tues Tue)
	DAYOFWEEK_WEDNESDAY,	ENAMES(Wednesday Humpday Wed)
	DAYOFWEEK_THURSDAY,		ENAMES(Thursday Thurs Thur Thu)
	DAYOFWEEK_FRIDAY,		ENAMES(Friday Fri)
	DAYOFWEEK_SATURDAY,		ENAMES(Saturday Sat)
	DAYOFWEEK_SUNDAY,		ENAMES(Sunday Sun)
} DayOfWeek;


AUTO_STRUCT ;
typedef struct Appointment
{
	int hour;
	int minute;
	DayOfWeek day;
} Appointment;

*/

/*




extern ATR_FuncDef ATR_entitylib_AllFuncs[];

void AutoTransTest()
{
	FakeContainer container1 = { 1, 2, 3, 4};
	FakeContainer container2 = {5, 6, 7, 8};

	float fTestFloat = 353.0f;

	char *pOutString = NULL;

	SetATRArg_Int(0, 14);
	SetATRArg_Container(1, &container1);
	SetATRArg_FloatPtr(2, &fTestFloat);
	SetATRArg_Container(3, &container2);

	ATR_entitylib_AllFuncs[1].pWrapperFunc(&pOutString);
}
*/
/*
AST_PREFIX(WIKI(AUTO))
AUTO_STRUCT WIKI("This is an inner test struct");
typedef struct innerTestStruct
{
	//an int
	int foo;

	//a happy float
	float f;
} innerTestStruct;


AUTO_STRUCT WIKI("This is a hot struct") AST_NO_PREFIX_STRIP;
typedef struct oldNewSetStruct
{
	int iTest1; 

	//this comment is about
	//
	//the variable named y
	int y; 
	//as is this one

	//this one is about z
	//
	float z; 

	
	int k; 
	//how about k

	//now, a test inner struct
	innerTestStruct *pTestStruct; AST(WIKILINK)

} oldNewSetStruct;
AST_PREFIX()



AUTO_ENUM;
typedef enum 
{
	BUG_BEETLE = (1 << 2),
	BUG_LADYBUG  = (1 << 3),
	BUG_GOLIATHBEETLE = (1 << 4)
} bugType;
*/

/*

AUTO_STRUCT;
typedef struct oldNewSetStruct
{
	int iTest1 : 1; 

	//this comment is about
	//
	//the variable named y
	int iTest2 : 1; 
	//as is this one

	//this one is about z
	//
	float fTest3; 

	
	int iTest4; AST( VOLATILE_REF )
	//how about k

	int iTest5 : 1; AST(NAME(Foo, Bar, Wakka))

} oldNewSetStruct;
*/
AUTO_STRUCT AST_IGNORE(foo) AST_IGNORE_STRUCTPARAM(bar);
typedef struct otherStruct
{
	float f;
	int x;
} otherStruct;

/*
AUTO_RUN;
int testAutoRunFunc(void)
{
	int x = 7;
	x += 3;
	return 0;
}
*/
/*
//dummyFunc does lots of amazing things
//It takes various arguments and does magic things to them that go "kabam" and 'splat'
//and
//so forth
AUTO_COMMAND ACMD_CATEGORY(dummy);
int dummyFunc1(int x, float f, Vec3 *pVec, Vec3 otherVec, Mat4 *pMat, Mat4 otherMat)
{
	x = 5;
	f = 10.0f;
	
	return 0;
}

//TestFunc1 is awesome and cool
AUTO_COMMAND ACMD_CATEGORY(test);
float testFunc1(int x, float f, Vec3 *pVec, Vec3 otherVec, Mat4 *pMat, Mat4 otherMat)
{
	x = 5;
	f = 10.0f;
	
	return 3.5f;
}


//dummyFunc2 does lots of amazing things
//It could kick chuck norris's ass
AUTO_COMMAND ACMD_CATEGORY(dummy);
char *dummyFunc2(int x, float f, Vec3 *pVec, Vec3 otherVec, Mat4 *pMat, Mat4 otherMat)
{
	x = 5;
	f = 10.0f;
	
	return "test";
}
*/


/*
#define CONST_EARRAY_OF(const type) type * const * const;
#define CONST_INT_EARRAY const int * const 
#define CONST_FLOAT_EARRAY const float * const
#define CONST_OPTIONAL_STRUCT([const]type) type * const
#define CONST_STRING_MODIFIABLE const char * const
#define CONST_STRING_POOLED const char * const
CONST_REF_TO
const int
const float
const bool
*/

/*
AUTO_STRUCT;
typedef struct testStruct
{
	U8 int1;
	U8 bit2 : 1;
	U8 int2;
	U8 bit3 : 1;
	U8 int3;
	U8 bit1 : 1;
} testStruct;

AUTO_COMMAND;
void testBitWrite(void)
{
	testStruct myStruct;
	int i;
	char fileName[MAX_PATH];

	for (i=0; i < 100; i++)
	{
		U32 iVal = ((U32)rand()) % 2;
		myStruct.bit1 = myStruct.int1 = iVal;
		iVal = rand() % 2;
		myStruct.bit2 = myStruct.int2 = iVal;
		iVal = rand() % 2;
		myStruct.bit3 =myStruct.int3 =  iVal;

		sprintf(fileName, "testbit%d.txt", i);

		ParserWriteTextFile(fileName, parse_testStruct, &myStruct, 0, 0);
	}
}

AUTO_COMMAND;
void testBitRead(void)
{
	testStruct myStruct;
	int i;
	char fileName[MAX_PATH];

	for (i=0; i < 100; i++)
	{
		sprintf(fileName, "testbit%d.txt", i);

		memset(&myStruct, 0, sizeof(testStruct));

		ParserReadTextFile(fileName, parse_testStruct, &myStruct);

		assertmsg(myStruct.int1 == myStruct.bit1 && myStruct.int2 == myStruct.bit2 && myStruct.int3 == myStruct.bit3, "Bitfield corruption");
	}
}

*/
/*		
AUTO_COMMAND ACMD_LIST(foo);
void testCommand1(void)
{


}
AUTO_COMMAND ACMD_LIST(foo);
void testCommand2(void)
{


}
AUTO_COMMAND ACMD_LIST(bar);
void testCommand3(void)
{


}
*/
/*
AUTO_STRUCT;
typedef struct multiValTestStruct
{
	MultiVal myVal;
	MultiVal myVals[15];
	MultiVal **ppMyValsEArray;
} multiValTestStruct;
*/

/*

int gAlexTestX;
U64 gAlexTestX64;
float fAlexFloat = 0.0f;
char fAlexString[1024] = "Test String";

void dumbCallback(CMDARGS)
{
	int x;

	x = 0;
}

AUTO_CMD_INT(gAlexTestX, sillyLittleX) ACMD_ACCESSLEVEL(4) ACMD_CALLBACK(dumbCallback);
AUTO_CMD_INT(gAlexTestX64, sillyBigX);
AUTO_CMD_FLOAT(fAlexFloat, sillyFloat) ACMD_HIDE;
AUTO_CMD_STRING(fAlexString, sillyString);

//Here is a fun comment for the sentence command
AUTO_CMD_SENTENCE(fAlexString, sillySentence);

*/
/*

AUTO_COMMAND;
void testFunc(ACMD_SENTENCE pSentence)
{
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(sillyLittleX);
void errorTestFunc(Cmd *pCmd)
{
	int x;

	x = 5;
}
AUTO_COMMAND;
void vecTestFunc(int x, float f, char *pString, Vec3 vec, Mat4 mat)
{

	int i;

	i = x;
}

AUTO_COMMAND;
int testReturnIntFunc(int x)
{
	return x + 7;
}
*/

/*
AUTO_STRUCT;
typedef struct testStruct
{
	int x; AST(STRUCTPARAM)
	int y; 
	MultiVal myVal;
} testStruct;
*/
/*
AUTO_COMMAND;
void SetTeamSize(int playerNum)
{
}
*/
/*
AUTO_COMMAND;
int GetTeamSize(int playerNum)
{
	return 17;
}

AUTO_COMMAND;
float GetFPS(void)
{
	return 3.5f;
}

AUTO_CMD_FLOAT(fAlexFloat, AlexFloat);

int iAlexInt = 56;
AUTO_CMD_INT(iAlexInt, AlexInt);

*/
/*
AUTO_STRUCT;
typedef struct testStruct
{
	int x;
	float y;
	Color myColor[2]; AST(RGBA INDEX(0, foreground) INDEX(1, background))
} testStruct;
*/



/*

AUTO_STRUCT;
typedef struct SubPet
{
	int x;
	int y; 
	float foo;
} SubPet;


AUTO_STRUCT;
typedef struct Pet
{
	SubPet **mySubPet; 

	//The name of the pet
	char petName[128]; AST(NAME(oldPetName, superOldPetName))
	
	//how many hit points the pet has
	int iPetHP;

	union 
	{
		int x;
		float y; AST(REDUNDANTNAME)
	}; 


} Pet;*/

/*
AUTO_ENUM;
typedef enum enumPetType
{
	PETTYPE_DOG, ENAMES(dog:with:colons | dog:with:more:Colons | dog)
	PETTYPE_CAT,
	PETTYPE_IGUANA,
} enumPetType;
	

AUTO_STRUCT;
typedef struct Pet
{
	int iColor;
	float fTest;
	enumPetType eType; AST( POLYPARENTTYPE )
} Pet;

AUTO_STRUCT;
typedef struct Dog
{
	Pet pet; AST( POLYCHILDTYPE( PETTYPE_DOG) COMMAND("foo", "bar") )

	int iBreed;

	
} Dog;

AUTO_STRUCT;
typedef struct Cat
{
	Pet pet; AST( POLYCHILDTYPE( PETTYPE_CAT) )

	float fWhiskerLength;
	
} Cat;

AUTO_STRUCT;
typedef struct Iguana
{
	Pet pet; AST( POLYCHILDTYPE( PETTYPE_IGUANA ) )

	int iNumLittleIguanaBabies;
} Iguana;


AUTO_STRUCT;
typedef struct PetList
{
	Pet onePet;
	Pet *pMaybePet;
	Pet **ppPets;
} PetList;



AUTO_COMMAND_QUEUED(pTestQueue);
void MyHappyDumbFunc(int x, float f, char *pString, ACMD_POINTER void*** pDog, Vec3 vec1, Vec3 *pVec2)
{
	printf("x: %d  f: %f  string: %s   v1: %f %f %f   v2: %f %f %f\n", x, f, pString,
		vec1[0], vec1[1], vec1[2], (*pVec2)[0], (*pVec2)[1], (*pVec2)[2]);
}
CommandQueue *pTestQueue;

AUTO_RUN;
int InitTestQueue(void)
{
	pTestQueue = CommandQueue_Create(128, false);

	return 0;
}
CmdList testCmdSetOne;
CmdList testCmdSetTwo;
CmdList testCmdSetThree;
AUTO_COMMAND ACMD_NAME(PutCommandsIntoQueue, "CommandsIntoQueue", CommandsInQueue, "QueueCommand...Thing") ACMD_LIST(testCmdSetOne) ACMD_LIST(testCmdSetTwo) ACMD_LIST(testCmdSetThree) ACMD_GLOBAL ACMD_CLIENTONLY;
void PutCommandsIntoQueue(int iNumCommands)
{
	int i;

	for (i=0; i < iNumCommands; i++)
	{
		char tempString[16];

		Vec3 vec1;
		Vec3 vec2;

		sprintf(tempString, "%d", i);

		vec1[0] = i;
		vec1[1] = i * 2;
		vec1[2] = i * 4;

		vec2[0] = i;
		vec2[1] = i * 3;
		vec2[2] = i * 9;

		QueuedCommand_MyHappyDumbFunc(i, (float)i, tempString, NULL, vec1, &vec2);
	}
}

AUTO_COMMAND ACMD_LIST(testCmdSetTwo, testCmdSetThree);
void ExecuteQueue(void)
{
	CommandQueue_ExecuteAllCommands(pTestQueue);
}

AUTO_COMMAND;
enumPetType ladidaCommand(enumPetType eType1, int ack, enumPetType eType2)
{
	return (enumPetType)0;

}

AUTO_COMMAND;
void commandWithQuat(Quat q1, Quat *pQ2)
{


}

AUTO_STRUCT AST_CONTAINER;
typedef struct blahMultiBlah
{
	CONST_MULTIVAL m; AST(PERSIST)
} blahMultiBlah;
*/
/*
AUTO_STRUCT;
typedef struct stringTesterStruct
{
	char *pString1;
	char *pString2; AST( POOL_STRING )
	char *pString3; AST( ESTRING )
} stringTesterStruct;

AUTO_COMMAND;
void testStrings(void)
{
	stringTesterStruct *pStringTester = StructAlloc(parse_stringTesterStruct);

	ParserReadTextFile("stringtest.txt", parse_stringTesterStruct, pStringTester);

	StructDestroy(parse_stringTesterStruct, pStringTester);
}
*/
/*

AUTO_EXPR_FUNC(fakeList);
void foo(void)
{
 
}

AUTO_EXPR_FUNC(fakeList2);
void foo2(ACMD_EXPR_ENTARRAY_IN_OUT array1)
{

}

AUTO_EXPR_FUNC(fakeList);
ExprFuncReturnVal foo3(int x, float f, ACMD_EXPR_ENTARRAY_IN array1, ACMD_EXPR_INT_OUT pInt)
{
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(fakeList);
ExprFuncReturnVal foo4(ACMD_EXPR_STRING_OUT pString)
{
 	return ExprFuncReturnFinished;

}
*/

/*
AUTO_STRUCT;
typedef struct DumbHappyStruct
{
	int x;
	int y;
} DumbHappyStruct;

AUTO_COMMAND ACMD_TESTCLIENT;
void DumbHappyCommand(DumbHappyStruct *pStruct)
{

}*/
/*
AUTO_ENUM AEN_NO_PREFIX_STRIPPING;
typedef enum funEnum
{
	HAPPY_FUN,
	HAPPY_SAD,
	HAPPY_MAD,

} funEnum;

AUTO_ENUM AEN_APPEND_TO(funEnum);
typedef enum moreFunEnum
{
	HAPPY_KOOKOO = HAPPY_MAD + 1,
	HAPPY_WACKY,
	HAPPY_SILLY,
} moreFunEnum;*/


#include "AutoGen/entityiterator_c_ast.c"


