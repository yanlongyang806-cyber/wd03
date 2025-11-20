#include "transactionoutcomes.h"
#include "autogen/serverlib_structparsertests_c_ast.h"
#include "MemoryPool.h"

typedef struct recurseStruct1 recurseStruct1;
typedef struct recurseStruct2 recurseStruct2;
DirtyBit myBit;


AUTO_STRUCT;
typedef struct recurseStruct1
{
	float f1; AST(FLOAT_HUNDREDTHS)
	recurseStruct1 **pStruct1s;
	recurseStruct2 **pStruct2s;
} recurseStruct1;

AUTO_STRUCT;
typedef struct recurseStruct2
{
	recurseStruct1 **pStruct1s;
	DirtyBit myDirtyBit;
	recurseStruct2 **pStruct2s;

} recurseStruct2;

/*
AUTO_RUN;
void dirtyBitTest(void)
{	 
	recurseStruct2 testStruct = {0};
	
	bool bHasDirtyBit;
	bool bDirtyBitIsSet;
	
	bHasDirtyBit = ParserTableHasDirtyBitAndGetIt(parse_recurseStruct2, &testStruct, &bDirtyBitIsSet);
	entity_SetDirtyBit(parse_recurseStruct2, &testStruct);
	bHasDirtyBit = ParserTableHasDirtyBitAndGetIt(parse_recurseStruct2, &testStruct, &bDirtyBitIsSet);
	ParserClearDirtyBit(parse_recurseStruct2, &testStruct);
	bHasDirtyBit = ParserTableHasDirtyBitAndGetIt(parse_recurseStruct2, &testStruct, &bDirtyBitIsSet);

	bHasDirtyBit = ParserTableHasDirtyBitAndGetIt(parse_recurseStruct1, &testStruct, &bDirtyBitIsSet);
}
*/

typedef struct fooStruct fooStruct;

AUTO_STRUCT;
typedef struct unownedTest
{
	fooStruct *pFoo; AST(UNOWNED LATEBIND)
} unownedTest;



AUTO_COMMAND_REMOTE_SLOW(char*);
void slowTest(int x, SlowRemoteCommandID iID)
{
}


AUTO_STRUCT AST_CONTAINER;
typedef struct InnerContainer 
{
	const int foo; AST(PERSIST)
	const int bar; AST(PERSIST)
} InnerContainer;

AUTO_STRUCT;
typedef struct InnerStruct
{
	int x;
	int y;
} InnerStruct;

AUTO_STRUCT AST_CONTAINER AST_FOR_ALL(PERSIST);
typedef struct FakeContainer 
{
	const int x; 
	const int y; 
	const int z; 
	const int w; 
	const int superCrazy; 
	CONST_OPTIONAL_STRUCT(InnerContainer) pMyInnerContainer;
	CONST_OPTIONAL_STRUCT(InnerContainer) pOtherInnerContainer;
//	CONST_OPTIONAL_STRUCT(InnerStruct) pInnerStruct;
//	CONST_OPTIONAL_STRUCT(InnerStruct) pOtherInnerStruct;
	CONST_EARRAY_OF(InnerContainer) ppInnerContainers; 
	CONST_STRING_EARRAY ppStrings;
} FakeContainer;

typedef struct wibba wibba;

AUTO_STRUCT;
typedef struct FakeNonContainer
{
	int x;
	float f; AST(DEF(1.5f))
	char foo[128]; AST(DEF(test))
	wibba *pMyWibba; AST(LATEBIND)
} FakeNonContainer;

AUTO_STRUCT;
typedef struct FakeFlatEmbedTest
{
	float f1; AST(DEF(2.5f))
	FakeNonContainer nonContainer1; AST(EMBEDDED_FLAT(Hahahaha))
	FakeNonContainer nonContainer2; AST(EMBEDDED_FLAT(Hohohoho))
	float f2; AST(DEF(3.5f))
} FakeFlatEmbedTest;
/*
AUTO_RUN;
void flatEmbedTestFunc(void)
{
	FakeFlatEmbedTest *pStruct = StructCreate(parse_FakeFlatEmbedTest);
	StructDestroy(parse_FakeFlatEmbedTest, pStruct);
}
*/

AUTO_STRUCT;
typedef struct AutoIndexTest
{
	U32 testBit : 1; AST(DEF(1))
	int foo;
	int bar;
	FakeNonContainer containers[20]; AST(AUTO_INDEX(containers))
} AutoIndexTest;


void nonHelper(NOCONST(InnerContainer) *pStruct)
{

}

void otherFakeHelperFunc(NOCONST(InnerContainer) *pStruct);


AUTO_TRANS_HELPER
ATR_LOCKS(pStruct, ".*")
ATR_LOCKS(pOtherStruct, ".Foo");
void fakeHelperFunc(int x, ATH_ARG NOCONST(InnerContainer) *pStruct, int y, ATH_ARG NOCONST(InnerContainer) *pOtherStruct)
{
	if (pStruct)
	{
		otherFakeHelperFunc(pStruct);
		pOtherStruct->foo += pStruct->foo;

	
	}

	if (!pStruct)
	{

	}



}

AUTO_TRANS_HELPER
ATR_LOCKS(pStruct, ".Bar");
void otherFakeHelperFunc(ATH_ARG NOCONST(InnerContainer) *pStruct)
{
	pStruct->bar += 5;
}




AUTO_TRANSACTION
ATR_LOCKS(pFakeContainer, ".*");
enumTransactionOutcome structCopyTest(ATR_ARGS, ATR_ALLOW_FULL_LOCK NOCONST(FakeContainer) *pFakeContainer)
{

	if (pFakeContainer)
	{
		fakeHelperFunc(7, pFakeContainer->pMyInnerContainer, 10, pFakeContainer->pOtherInnerContainer);

		pFakeContainer->z = 3;
		return TRANSACTION_OUTCOME_SUCCESS;

		if (pFakeContainer->w)
		{

		}
	}

	/*if (!pFakeContainer)
	{

	}*/


	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt1, ".X")
ATR_LOCKS(pEnt2, ".Y")
ATR_LOCKS(ppListOfEnts, "(none)"); //Nothing locked... unusual and worrying, but not necessarily illegal
enumTransactionOutcome myFunc(ATR_ARGS, NOCONST(FakeContainer) *pEnt1, const int foo, FakeNonContainer *pNonContainer, int bar, NOCONST(FakeContainer) *pEnt2, CONST_EARRAY_OF(NOCONST(FakeContainer)) ppListOfEnts)
{


	pEnt1->x = 17;
	pEnt2->y = 33;

//	fakeFunction(pEnt1);

//	eaIndexedGetUsingString(&pEnt1->ppInnerContainers, parse_InnerContainer, "test");

	return TRANSACTION_OUTCOME_SUCCESS;

}


AUTO_TRANSACTION
ATR_LOCKS(pMyEnt, ".W, .Ppinnercontainers, .X, .Y")
ATR_LOCKS(pMyOtherEnt, ".Y")
ATR_LOCKS(ppListOfEnts, ".Y, .X, []ppInnerContainers[]");
enumTransactionOutcome myOtherFunc(ATR_ARGS, int foo, NOCONST(FakeContainer) *pMyEnt, float pBar, NOCONST(FakeContainer) *pMyOtherEnt, ATR_ALLOW_FULL_LOCK CONST_EARRAY_OF(NOCONST(FakeContainer)) ppListOfEnts)
{

	int i;

	myFunc(ATR_RECURSE, pMyEnt, 17, NULL, 38, pMyOtherEnt, ppListOfEnts);
	myFunc(ATR_RECURSE, pMyEnt, 17, NULL, 38, pMyEnt, ppListOfEnts);

	pMyEnt->w = 6;

	pMyEnt->ppInnerContainers[0]->foo = 3;

	//eaIndexedGetOrCreateUsingInt(&pMyOtherEnt->ppInnerContainers, foo);


	for (i=0; i < eaSize(&ppListOfEnts); i++)
	{
		eaIndexedGetUsingInt_FailOnNULL(&ppListOfEnts[i]->ppInnerContainers, 7);
		myFunc(ATR_RECURSE, ppListOfEnts[3], 17, NULL, 38, ppListOfEnts[5], ppListOfEnts);
	}


	return TRANSACTION_OUTCOME_SUCCESS;


}

enumTransactionOutcome testFunc1(ATR_ARGS, NOCONST(FakeContainer) *pMyEnt);
enumTransactionOutcome testFunc2(ATR_ARGS, NOCONST(FakeContainer) *pMyEnt);
enumTransactionOutcome testFunc3(ATR_ARGS, NOCONST(FakeContainer) *pMyEnt);


AUTO_TRANSACTION
ATR_LOCKS(pMyEnt, ".X, .Z, .W, .Y");
enumTransactionOutcome testFunc1(ATR_ARGS, NOCONST(FakeContainer) *pMyEnt)
{
	

	pMyEnt->x = 5;

	testFunc2(ATR_RECURSE, pMyEnt);
	testFunc3(ATR_RECURSE, pMyEnt);
	return TRANSACTION_OUTCOME_SUCCESS;

	
}


AUTO_TRANSACTION
ATR_LOCKS(pMyEnt, ".Z, .W");
enumTransactionOutcome testFunc2(ATR_ARGS, NOCONST(FakeContainer) *pMyEnt)
{
	

	pMyEnt->z = 5;
	pMyEnt->w = 5;
	return TRANSACTION_OUTCOME_SUCCESS;

	

}


AUTO_TRANSACTION
ATR_LOCKS(pMyEnt, ".Y, .X, .Z, .W");
enumTransactionOutcome testFunc3(ATR_ARGS, NOCONST(FakeContainer) *pMyEnt)
{
	

	if (pMyEnt->y > 0)
	{
		testFunc1(ATR_RECURSE, pMyEnt);
		testFunc3(ATR_RECURSE, pMyEnt);
	}
	return TRANSACTION_OUTCOME_SUCCESS;

	
}


/*
AUTO_COMMAND;
Vec3 *TestVec3Func(void)
{
	static Vec3 retVal = { 3.0f, 4.0f, 5.0f };
	return &retVal;
}


AUTO_STRUCT;
typedef struct ownTest1
{
	int x;
	int y;
} ownTest1;

AUTO_STRUCT;
typedef struct ownTest2
{
	ownTest1 *pUnOwned; AST(STRUCT(parse_ownTest1, UNOWNED))
	ownTest1 *pOwned; 
} ownTest2;

AUTO_COMMAND;
void unownedtest(void)
{
	ownTest1 test1 = { 4, 5 };
	ownTest2 *p2;
	ownTest2 *p3;
	char *pTestString = NULL;

	p2 = StructCreate(parse_ownTest2);
	p2->pOwned = &test1;
	p2->pUnOwned = &test1;

	p3 = StructCreate(parse_ownTest2);
	StructCopyFields(parse_ownTest2, p2, p3, 0, 0);


	objPathGetEString(".UnOwned.x", parse_ownTest2, p3, 
					   &pTestString);

	StructDestroy(parse_ownTest2, p3);



	{
		int x = 0;
	}
}
*/

AUTO_COMMAND_QUEUED();
void queueTest(int x, float y)
{

}

AUTO_ENUM;
typedef enum enumEmotions
{
	HAPPY,
	SAD,
	CRAZY,
	ANGRY
} enumEmotions;

AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct emotionStruct
{
	enumEmotions emo1; AST(DEF(HAPPY))
	enumEmotions emo2; AST(DEF(SAD))
	enumEmotions emo3; AST(DEF(CRAZY))
	enumEmotions emo4; AST(DEF(ANGRY))
} emotionStruct;

MP_DEFINE(emotionStruct);

void emotionTest(void)
{

	emotionStruct test1 = { HAPPY, HAPPY, HAPPY, HAPPY };
	emotionStruct test2 = { SAD, SAD, SAD, SAD };

	emotionStruct *pCopy1, *pCopy2;

	MP_CREATE(emotionStruct, 1024);

	pCopy1 = StructCreate(parse_emotionStruct);
	pCopy2 = StructCreate(parse_emotionStruct);

	ParserWriteTextFile("c:\\emotest1.txt", parse_emotionStruct, &test1, 0, 0);
	ParserWriteTextFile("c:\\emotest2.txt", parse_emotionStruct, &test2, 0, 0);

	ParserReadTextFile("c:\\emotest1.txt", parse_emotionStruct, pCopy1, 0);
	ParserReadTextFile("c:\\emotest2.txt", parse_emotionStruct, pCopy2, 0);
}


AUTO_STRUCT;
typedef struct testKeyStruct
{
	char *pName; AST(NAME(hi) NAME(ho) NAME(wakka) REQUIRED SELF_ONLY)
} testKeyStruct;


typedef U32 fakeInt;
typedef U8 fakeU8;

AUTO_COMMAND;
void fakeCommandTestingTypes(ACMD_FORCETYPE(U32) fakeInt myInt, ACMD_FORCETYPE(U8) fakeU8 myU8)
{

}

int x1;
int x2;

AUTO_CMD_INT(x1, sillyX1);
AUTO_CMD_INT(x2, sillyX2);

AUTO_COMMAND;
void fakeFunc(int x)
{
	int y = x;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(sillyX1) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(sillyX2) ACMD_I_AM_THE_ERROR_FUNCTION_FOR(fakeFunc);
void errorTestFunc(void)
{
	int x;

	x = 5;
}



/*
AUTO_ENUM;
typedef enum enumPetType
{
	PETTYPE_DOG, ENAMES(dog:with:colons | dog:with:more:Colons | dog)
	PETTYPE_CAT,
	PETTYPE_IGUANA,
} enumPetType;
	

typedef struct LateLinkedFoo LateLinkedFoo;
AUTO_STRUCT;
typedef struct Pet
{
	LateLinkedFoo *pFoo; AST(LATEBIND)
	int iColor;
	float fTest;
	enumPetType eType; AST( POLYPARENTTYPE(20) )
} Pet;

AUTO_STRUCT;
typedef struct Dog
{
	Pet pet; AST( POLYCHILDTYPE( PETTYPE_DOG) )

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

AUTO_COMMAND ACMD_SERVERCMD;
void petTestCmd(int x, float f, enumPetType ePetType)
{
}*/

/*
AUTO_STARTUP(pets,1) ASTRT_DEPS(foo, wakka);
void initPets(void)
{
	int x = 0;
}

AUTO_STARTUP(foo) ASTRT_DEPS(bar);
void initFoo(void)
{
	int x = 0;
}
AUTO_STARTUP(bar);
void initBar(void)
{
	int x = 0;
}
AUTO_STARTUP(wakka) ASTRT_CANCELLEDBY(nowakka);
void initWakka(void)
{
	int x = 0;
}

AUTO_STARTUP(nowakka);
void initNoWakka(void)
{
	int x = 0;
}
*/
//AUTO_RUN_ANON(printf("Hi there, from %s!\n", __FILE__););
/*
AUTO_STRUCT;
typedef struct InnerFoo1
{
	int a;
	int b;
} InnerFoo1;

AUTO_STRUCT;
typedef struct InnerFoo2
{
	char *b;
	float a;
} InnerFoo2;

AUTO_STRUCT;
typedef struct Foo1
{
	InnerFoo1 innerFoo;
	int x;
	int y;
	float f;
	char str[32];
	int *testearray;
} Foo1;

AUTO_STRUCT;
typedef struct Foo2
{
	char str[32];
	float notf; AST(NAME(a,b,c,d,f))
	char x[32];
	char **testearray;
	//int y;
	char str2[32];
	InnerFoo2 *innerFoo;
} Foo2;

AUTO_RUN;
void test2tpicopying(void)
{
	Foo1 foo1 = { {3, 4}, 1, 2, 3.0f, "test" };
	Foo2 foo2 = {0};
	char *pResultString = NULL;
	enumCopy2TpiResult eResult;

	ea32Push(&foo1.testearray, 14);
	ea32Push(&foo1.testearray, 15);
	ea32Push(&foo1.testearray, 16);
	ea32Push(&foo1.testearray, 17);

	eResult = StructCopyFields2tpis(parse_Foo1, &foo1, parse_Foo2, &foo2, 0, 0, &pResultString);

	estrDestroy(&pResultString);
	StructDeInit(parse_Foo1, &foo1);
	StructDeInit(parse_Foo2, &foo2);
}
*/
/*
typedef struct OptTestStruct3 OptTestStruct3;

AUTO_STRUCT;
typedef struct OptTestStruct1
{
	int x;
	float f;
	char str[256];
	int y;
	OptTestStruct3 *pStruct3_1; 
	OptTestStruct3 *pStruct3_2; 
	OptTestStruct3 *pStruct3_3; 
} OptTestStruct1;


AUTO_STRUCT;
typedef struct OptTestStruct2
{
	int foo;
	int bar; AST(NO_WRITE)
	OptTestStruct1 *pStruct1; 
	Pet *pPet; 
} OptTestStruct2;

AUTO_STRUCT;
typedef struct OptTestStruct3
{
	int wakka;
	int wappa;
	OptTestStruct1 *pStruct1; 
	OptTestStruct2 *pStruct2; 
} OptTestStruct3;

AUTO_RUN_LATE;
void optTPITest(void)
{
//	ParseTableGraph *pGraph = MakeParseTableGraph(parse_OptTestStruct3, 0, TOK_NO_WRITE);


	ParseTable *pOptimized1 = ParserMakeOptimizedParseTable(parse_OptTestStruct1, 0, TOK_NO_WRITE, 0);
}

*/
/*
AUTO_STRUCT;
typedef struct braceTestStruct
{
	int x; AST(STRUCTPARAM)
	int y; AST(STRUCTPARAM)
	int z;
} braceTestStruct;

AUTO_STRUCT;
typedef struct braceTestStructList
{
	braceTestStruct **ppBraceTestStructs;
} braceTestStructList;

AUTO_RUN;
void braceTest(void)
{
	char *pEString1 = NULL;
	braceTestStruct s1 = {1, 2, 3};
	braceTestStruct s2 = {4, 5, 0};
	braceTestStruct s3 = {6, 7, 0};

	braceTestStructList list = {0};

	eaPush(&list.ppBraceTestStructs, &s1);
	eaPush(&list.ppBraceTestStructs, &s2);
	eaPush(&list.ppBraceTestStructs, &s3);

	ParserWriteText(&pEString1, parse_braceTestStructList, &list, 0, 0, 0);
	
}*/
/*

AUTO_STRUCT;
typedef struct dummydummy
{
	int x;
} dummydummy;

AUTO_STRUCT;
typedef struct innerDefTestStruct
{
	REF_TO(dummydummy) dummyRef; AST(STRUCTPARAM)
} innerDefTestStruct;
AUTO_STRUCT;
typedef struct innerDefTestStruct2
{
	int x; AST(STRUCTPARAM)
} innerDefTestStruct2;

AUTO_STRUCT;
typedef struct defTesterStruct
{
	U8 foo1[4];
	int foo2[4];
	U16 foo3[4];
	U64 foo4[4];
	float foo5[4];
} defTesterStruct;

void defaultTest(void)
{
	defTesterStruct test = {{1}};

	ParserWriteTextFile("c:/temp/test.txt", parse_defTesterStruct, &test, 0, 0);
}
*/
/*
AUTO_COMMAND_REMOTE ACMD_MULTIPLE_RECIPIENTS;
void MultiPrintTest(char *pMessage)
{
	printf("%s\n", pMessage);
}*/

#ifdef _XBOX


AUTO_COMMAND;
void ifTestCommand1(void)
{

}

#else

AUTO_COMMAND;
void ifTestCommand2(void)
{

}

#endif

AUTO_ENUM;
typedef enum enumMoodType
{
	MOOD_HAPPYSAD,
	MOOD_HAPPYHAPPY,
} enumMoodType;

#include "autogen/serverlib_structparsertests_c_ast.c"



