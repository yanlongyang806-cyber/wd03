//this file contains test crap for structparser, and should eventually be empty



#include "Stringutil.h"

typedef struct dumbStruct dumbStruct;

typedef struct foo foo;


AUTO_STRUCT;
typedef struct objPathTestStruct1
{
	int x;
	float f;
	char *pString;
	foo *pFoo; AST(LATEBIND)
	U32 testBit1 : 1;
	U32 testBit2 : 1;
} objPathTestStruct1;

/*
AUTO_STRUCT;
typedef struct copyTestStruct1
{
	int x;
	float f;
	char test[256];
} copyTestStruct1;


AUTO_STRUCT;
typedef struct copyTestStruct2
{
	int x;
	float f;
	char test[256];
	int *pX;
} copyTestStruct2;

AUTO_STRUCT;
typedef struct copyTestStruct3
{
	int x;
	float f;
	char test[256];
	REF_TO(copyTestStruct1) hT1;
} copyTestStruct3;

AUTO_STRUCT;
typedef struct copyTestStruct4
{
	int x;
	float f;
	char test[256];
	copyTestStruct1 embedded1;
} copyTestStruct4;

AUTO_STRUCT;
typedef struct copyTestStruct5
{
	int x;
	float f;
	char test[256];
	copyTestStruct2 embedded2;
} copyTestStruct5;

AUTO_STRUCT;
typedef struct copyTestStruct6
{
	int x;
	float f;
	char test[256];
	copyTestStruct1 *pIncluded;
} copyTestStruct6;

AUTO_RUN;
void copyTestThingy(void)
{
	copyTestStruct1 *pC11, *pC12;
	copyTestStruct2 *pC21, *pC22;

	printf("Can copy fast? %d %d %d %d %d %d\n",
		ParserCanTpiBeCopiedFast(parse_copyTestStruct1),
		ParserCanTpiBeCopiedFast(parse_copyTestStruct2),
		ParserCanTpiBeCopiedFast(parse_copyTestStruct3),
		ParserCanTpiBeCopiedFast(parse_copyTestStruct4),
		ParserCanTpiBeCopiedFast(parse_copyTestStruct5),
		ParserCanTpiBeCopiedFast(parse_copyTestStruct6)
		);


	pC11 = StructCreate(parse_copyTestStruct1);
	pC12 = StructCreate(parse_copyTestStruct1);
	pC21 = StructCreate(parse_copyTestStruct2);
	pC22 = StructCreate(parse_copyTestStruct2);

	StructCopyAll(parse_copyTestStruct1, pC11, pC12);
	StructCopyAll(parse_copyTestStruct2, pC21, pC22);


}
*/






/*


AUTO_STRUCT;
typedef struct keyTest1
{
	int foo;
	int bar;
} keyTest1;

AUTO_STRUCT;
typedef struct keyTest2
{
	int foo;
	int bar; AST(KEY)
} keyTest2;

AUTO_RUN;
void keyTest(void)
{
	int key = ParserGetTableKeyColumn(parse_keyTest1);
	key = ParserGetTableKeyColumn(parse_keyTest2);
	key = ParserGetTableObjectTypeColumn(parse_keyTest1);
	key++;
}
*/
/*
AUTO_STRUCT;
typedef struct matTest
{
	Mat3 m3;
	Mat4 m4;
} matTest;

AUTO_COMMAND;
void matrixStructparserTest(void)
{
	F32 rotVec[] = { 10, 20, 30 };
	matTest *pMatTest = StructCreate(parse_matTest);

	matTest *pMat2Test = StructCreate(parse_matTest);

	identityMat4(pMatTest->m4);
	identityMat3(pMatTest->m3);

	rotateMat3( rotVec, pMatTest->m3);
	rotateMat3( rotVec, pMatTest->m4);

	pMatTest->m4[3][0] = 1;
	pMatTest->m4[3][1] = 2;
	pMatTest->m4[3][2] = 3;

	ParserWriteTextFile("mattest.txt", parse_matTest, pMatTest, 0, 0);

	ParserReadTextFile("mattest.txt", parse_matTest, pMat2Test);
	

	StructDestroy(parse_matTest, pMatTest);
	StructDestroy(parse_mat2Test, pMatTest);
}

*/



/*
AUTO_STRUCT;
typedef struct objPathTestStruct2
{
	int foo;
	float bar;
	objPathTestStruct1 **ppObjects;
} objPathTestStruct2;

AUTO_STRUCT;
typedef struct objPathTestStruct3
{
	int wakka;
	float wappa;
	objPathTestStruct2 obj;
} objPathTestStruct3;

AUTO_RUN;
void objPathTest(void)
{
	MultiValType eType;

	eType = objPathGetType(".wakka", parse_objPathTestStruct3);
	eType = objPathGetType(".obj.foo", parse_objPathTestStruct3);
	eType = objPathGetType(".obj.bar", parse_objPathTestStruct3);
	eType = objPathGetType(".obj.Objects[3]",  parse_objPathTestStruct3);
	eType = objPathGetType(".obj.Objects[3].x",  parse_objPathTestStruct3);
	eType = objPathGetType(".obj.Objects[3].String",  parse_objPathTestStruct3);
}
*/

/*

AUTO_STRUCT AST_CONTAINER;
typedef struct testContainer
{
	const int x; AST(PERSIST NAME(happyFunX))
	char *pString; 
} testContainer;

LATELINK;
int lateLinkTest(int x);

int DEFAULT_LATELINK_lateLinkTest(int x);
int DEFAULT_LATELINK_lateLinkTest(int x);

int DEFAULT_LATELINK_lateLinkTest(int x)
{
	return 7;
}*/
/*
typedef struct wakka wakka;
typedef struct foobar foobar;
AUTO_COMMAND;
wakka *CrazyCommand(foobar *pFooBar)
{
	return NULL;
}*/
/*
LATELINK_OVERRIDE(int, lateLinkTest, void)
{
	printf("x = %d, f = %f\n", x, f);
	return (int)(x + f + 1);
}
*/

/*

typedef struct innerFoo innerFoo;
typedef struct fakeStruct fakeStruct;

//MAKE SURE TO ALWAYS LEAVE AT LEAST ONE AUTO_STRUCT IN THIS FILE
AUTO_STRUCT;
typedef struct innerFoo
{
	int x; 
	int y; AST(NAME("Field: "))
	innerFoo *pFoo;
	REF_TO(const innerFoo) hFoo;
	
} innerFoo;*/
/*
bool FixupInnerFoo(innerFoo *pInnerFoo, enumTextParserFixupType eFixupType)
{
	return true;
}

AUTO_STRUCT;
typedef struct uselessTestStruct
{
	innerFoo *pFoo2; AST(LATEBIND)
	innerFoo **ppFoos; AST(LATEBIND)
} uselessTestStruct;



AUTO_STRUCT;
typedef struct biggerUselessTestStruct
{
	int x;
	uselessTestStruct mainStruct; AST(EMBEDDED_FLAT)
	int y;
} biggerUselessTestStruct;


AUTO_COMMAND;
innerFoo *TestCommandWithStruct(uselessTestStruct *pStruct)
{
	return NULL;
}




AUTO_STRUCT AST_CONTAINER AST_NONCONST_PREFIXSUFFIX("foo\nfoo", "bar\nbar");
typedef struct happyTestContainer
{
	const int foo; AST(PERSIST)
	const int bar;
} happyTestContainer;
*/
/*
AUTO_STRUCT;
typedef struct outerFoo
{
	innerFoo *pFoo1;
	innerFoo *pFoo2; AST(ALWAYS_ALLOC)
	innerFoo *pFoo3;
	innerFoo *pFoo4; AST(ALWAYS_ALLOC)
} outerFoo;*/
/*
NameList *pTestNameList = NULL;

AUTO_RUN;
void initTestNameList(void)
{
	pTestNameList = CreateNameList_Bucket();
	NameList_Bucket_AddName(pTestNameList, "Alex");
	NameList_Bucket_AddName(pTestNameList, "AlexWerner");
	NameList_Bucket_AddName(pTestNameList, "Ben");
	NameList_Bucket_AddName(pTestNameList, "BenZiegler");
	NameList_Bucket_AddName(pTestNameList, "CW");
	NameList_Bucket_AddName(pTestNameList, "Jimb");
	NameList_Bucket_AddName(pTestNameList, "Raoul");
	NameList_Bucket_AddName(pTestNameList, "Sam");
	NameList_Bucket_AddName(pTestNameList, "SamThompson");
}


AUTO_COMMAND;
void testNameListFunc(int x, ACMD_NAMELIST(pTestNameList) char *pString, int y)
{


}


AUTO_COMMAND;
void testPowerFunc(ACMD_NAMELIST("PowerDef", REFDICTIONARY) char *pString)
{

}


AUTO_COMMAND ACMD_LIST(tempCmdList);
void temp1(void)
{

}

AUTO_COMMAND ACMD_LIST(tempCmdList);
void temp2(void)
{

}


AUTO_COMMAND;
void testTextureFunc(ACMD_NAMELIST(g_basicTextures_ht, STASHTABLE) char *pString)
{

}



AUTO_COMMAND;
void testEnumFunc(ACMD_NAMELIST(ShaderDataTypeEnum, STATICDEFINE) char *pString)
{


}

*/
/*
CmdList tempCmdList;

AUTO_COMMAND;
void testCommandFunc(ACMD_NAMELIST(tempCmdList, COMMANDLIST) char *pString)
{


}

*/

/*
AUTO_STRUCT;
typedef struct outerFoo
{
	innerFoo *pFoo1;
	innerFoo *pFoo2; AST(ALWAYS_ALLOC)
	innerFoo *pFoo3;
	innerFoo *pFoo4; AST(ALWAYS_ALLOC LATEBIND)
} outerFoo;

AUTO_COMMAND;
void testAlwaysAlloc(ACMD_IGNORE fakeStruct *pFake1, int x, ACMD_IGNORE fakeStruct *pFake2, ACMD_IGNORE fakeStruct *pFake3, float f, ACMD_IGNORE fakeStruct *pFake4, char *pString)
{
	outerFoo *pFoo = StructCreate(parse_outerFoo);
	StructDestroy(parse_outerFoo, pFoo);
}

AUTO_COMMAND;
bool testConst(const outerFoo *pFoo)
{
	return true;
}


AUTO_STRUCT;
typedef struct StructParamTest
{
	int x; AST(STRUCTPARAM)
	char *pFileName; AST(CURRENTFILE)
} StructParamTest;
*/
/*
AUTO_STRUCT AST_FIXUPFUNC(ts1fixup);
typedef struct fixupTestStruct1
{
	int x;
	int y;
} fixupTestStruct1;

AUTO_STRUCT AST_FIXUPFUNC(ts2fixup);
typedef struct fixupTestStruct2
{
	fixupTestStruct1 sub1;
	fixupTestStruct1 *pSub2;
	int foo;
} fixupTestStruct2;

bool ts1fixup(fixupTestStruct1 *pStruct, enumTextParserFixupType eFixupType)
{
	if (eFixupType == FIXUPTYPE_POST_RELOAD)
	{
		pStruct->x += 1;
	}

	return true;
}

bool ts2fixup(fixupTestStruct2 *pStruct, enumTextParserFixupType eFixupType)
{
	if (eFixupType == FIXUPTYPE_POST_RELOAD)
	{
		pStruct->foo += 1;
	}

	return true;
}

AUTO_RUN;
void tpFixupTest(void)
{
	fixupTestStruct1 ts1 = { 3, 4 };
	fixupTestStruct2 ts2 = { { 5, 6}, &ts1, 7 };

	FixupStructLeafFirst(parse_fixupTestStruct2, &ts2, FIXUPTYPE_POST_RELOAD);
}
*/
/*
AUTO_EXPR_FUNC(ulExprFuncs) ACMD_NAME(TimeSince);
F32 exprTimeSince(S64 time)
{
 return 0.0f;
}*/
/*
AUTO_STRUCT;
typedef struct templateTestStruct
{
	int x; AST(DEF(2))
	int y; AST(DEF(3))
} templateTestStruct;

AUTO_RUN;
void templateTest(void)
{
	templateTestStruct myTemplate = { 4, 5 };
	templateTestStruct *pTest1, *pTest2;

	pTest1 = StructCreate(parse_templateTestStruct);

	ParserSetTemplateStruct(parse_templateTestStruct, &myTemplate);

	pTest2 = StructCreate(parse_templateTestStruct);
}
*/
/*
//does something silly
AUTO_COMMAND ACMD_CATEGORY(SillyCommands);
void doSomethingSilly(void)
{
}
*/
/*
AUTO_STRUCT;
typedef struct tokTestStruct
{
	int x;
	float f;
	char *pString1;
	char *pString2;
	char *pString3;
	int y;
} tokTestStruct;

void tokTest(void)
{
	char *pTestString = "\"blah blah blah\" \"ha ha ha\" \" This should be a quoted \\\" string with \\\" some quotes inside it\" ";
	char workString[1024];
	char *tokens[256];
	int iCount;
	char *pEString = NULL;

	tokTestStruct testStruct = { 3, 5.0f, "This string has \" both kinds <& of &> \" quotes", "This string has \" normal \" quotes", "This string <& &> has <& <& <&<&<& &> special quotes", 5};


	ParserWriteTextEscaped(&pEString, parse_tokTestStruct, &testStruct, 0, 0);

	strcpy(workString, pTestString);

	iCount = tokenize_line_quoted(workString, tokens, NULL);
}
*/

AUTO_RUN;
void tokTest2(void)
{
	char *ppTestStrings[] = 
	{
		"simple test with no quotes   a   asdfasdf   jdkfjefj389893240958    52343432       ",
		"",
		"     ",
		"a",
		"this string has a quoted string \"right here\" ",
		"\"\"",
		"\"                a trickier \\\"test\"",
		"<&              the other kind of quotes     &> 1234 3 8 \"\" \" hi \" <&&> <& asdf asdf asfd asdf asdf asdf asdfsfsadfawef\" \"&>"
	};

	char workString[1024];
	char *pArgs[50];
	int i;
	int iCount;

	for (i=0; i < ARRAY_SIZE(ppTestStrings); i++)
	{
		strcpy(workString, ppTestStrings[i]);
		iCount = TokenizeLineRespectingStrings(pArgs, workString);
	}
}


#include "autogen/structparsertest_c_ast.c"