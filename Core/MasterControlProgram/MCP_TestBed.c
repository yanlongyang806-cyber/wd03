//this file is a convenient place to put bits of test code so that you can then compile and test just the MCP
#include "net/net.h"
#include "wininclude.h"
#include <shlobj.h>
#include "textparser.h"
#include "file.h"
#include "EString.h"
#include "earray.h"
#include "cmdparse.h"
#include "mcp_testbed_c_ast.h"
#include "structNet.h"
#include <share.h>
#include "stringCache.h"
#include "structnet.h"
#include "expressionMinimal.h"
#include "String.h"



AUTO_STRUCT;
typedef struct DifTestChildStruct
{
	int foo;
	int bar;
} DifTestChildStruct;

/*

AUTO_STRUCT;
typedef struct PathTestStruct
{
	DifTestChildStruct **ppKids;
	int *pInts;
} PathTestStruct;

AUTO_RUN;
void testPaths(void) {
	PathTestStruct *ts = StructCreate(parse_PathTestStruct);
	ParseTable *pti;
	int col, ind;

	

	if (ParserResolvePath(".Kids[0]", parse_PathTestStruct, ts, &pti, &col, NULL, &ind, NULL, NULL, 0))
	{
		printf("%d %d\n", col, ind);
	}
	else
	{
		printf("did not resolve.\n");
	}

	if (ParserResolvePath(".Ints[0]", parse_PathTestStruct, ts, &pti, &col, NULL, &ind, NULL, NULL, 0))
	{
		printf("%d %d\n", col, ind);
	}
	else
	{
		printf("did not resolve.\n");
	}
}


AUTO_STRUCT;
typedef struct DifTestKeyedChildStruct
{
	char name[32]; AST(KEY)
	int foo;
	int ints[5];
	DifTestChildStruct **ppLotsOfChildren;
	DifTestChildStruct embeddedChild;
	DifTestChildStruct *pMaybeChild;
	int bit : 1;
	char *pOptString;
	MultiVal embeddedMV;
	MultiVal MVArray[5];

} DifTestKeyedChildStruct;



AUTO_STRUCT;
typedef struct DifTestStruct
{
	MultiVal MVArray[5];
	int x;
	float f;
	char s[50];
	int fsa[5];
	char **ppStrings;
	DifTestChildStruct *pChild;
	DifTestChildStruct **ppChildren;
	DifTestKeyedChildStruct **ppKeyedChildren;

} DifTestStruct;


AUTO_RUN_LATE;
void testBinaryDiff(void)
{
	char *estr = 0;
	char *estrb = 0;
	StructDiff *diff;
	DifTestStruct *ts  = StructCreate(parse_DifTestStruct);
	DifTestKeyedChildStruct *pKeyedChild1;
	DifTestKeyedChildStruct *pKeyedChild2;
	DifTestKeyedChildStruct *pKeyedChild3;


	ts->f = 1.23;
	ts->x = -2;
	sprintf(ts->s, "test");

	ts->fsa[0] = 92;
	ts->fsa[1] = 66;
	ts->fsa[2] = 47;

	ts->pChild = StructCreate(parse_DifTestChildStruct);
	ts->pChild->bar = 2;
	ts->pChild->foo = 6;

	pKeyedChild1 = StructCreate(parse_DifTestKeyedChildStruct);
	pKeyedChild2 = StructCreate(parse_DifTestKeyedChildStruct);
	pKeyedChild3 = StructCreate(parse_DifTestKeyedChildStruct);

	sprintf(pKeyedChild1->name, "one");
	sprintf(pKeyedChild2->name, "two");
	sprintf(pKeyedChild3->name, "three");
	
	pKeyedChild1->foo = 1;
	pKeyedChild2->ints[0] = 2;
	pKeyedChild2->ints[1] = 5;

	pKeyedChild1->pMaybeChild = StructCreate(parse_DifTestChildStruct);
	pKeyedChild1->pMaybeChild->foo = 19;



	eaIndexedEnable(&ts->ppKeyedChildren, parse_DifTestKeyedChildStruct);
	
	eaIndexedAdd(&ts->ppKeyedChildren, pKeyedChild1);
	eaIndexedAdd(&ts->ppKeyedChildren, pKeyedChild2);
	eaIndexedAdd(&ts->ppKeyedChildren, pKeyedChild3);

	estrStackCreate(&estr);
	estrStackCreate(&estrb);

	StructWriteTextDiff(&estr, parse_DifTestStruct, NULL, ts, "", 0, 0, TEXTDIFFFLAG_INVERTEXCLUDEFLAGS);

	diff = StructMakeDiff(parse_DifTestStruct, NULL, ts, 0, 0, true, false);
	StructWriteTextDiffFromBDiff(&estrb, diff);
	StructDestroyDiff(&diff);


	estrDestroy(&estr);
	estrDestroy(&estrb);
}


AUTO_STRUCT;
typedef struct TestColor {
	Vec4 color;								AST( STRUCTPARAM )
	F32 fRandomWeight;						AST( STRUCTPARAM )
} TestColor;

*/



/*			

bool applyChange(DifTestStruct *pStruct, int iIndex)
{
	switch (iIndex)
	{
	case 0:
		pStruct->x += 5;
		break;
	case 1:
		pStruct->f += 2.0f;
		break;
	case 2:
		pStruct->s[0] = 'c';
		break;
	case 3:
		pStruct->fsa[3] = 4;
		break;
	case 4:
		pStruct->pChild = StructCreate(parse_DifTestChildStruct);
		break;
	case 5:
		pStruct->pChild->foo += 1;
		pStruct->x += 1;
		break;
	case 6:
		StructDestroy(parse_DifTestChildStruct, pStruct->pChild);
		pStruct->pChild = NULL;
		break;
	case 7:
		{
			DifTestChildStruct *pChild1 = StructCreate(parse_DifTestChildStruct);
			DifTestChildStruct *pChild2 = StructCreate(parse_DifTestChildStruct);
			DifTestChildStruct *pChild3 = StructCreate(parse_DifTestChildStruct);

			pChild1->foo = 1;
			pChild2->foo = 2;
			pChild3->foo = 3;

			eaPush(&pStruct->ppChildren, pChild1);
			eaPush(&pStruct->ppChildren, pChild2);
			eaPush(&pStruct->ppChildren, pChild3);
		}
		break;
	case 8:
		StructDestroy(parse_DifTestChildStruct, pStruct->ppChildren[1]);
		eaRemove(&pStruct->ppChildren, 1);
		break;
	case 9:
		{
			DifTestKeyedChildStruct *pKeyedChild1 = StructCreate(parse_DifTestKeyedChildStruct);
			DifTestKeyedChildStruct *pKeyedChild2 = StructCreate(parse_DifTestKeyedChildStruct);
			DifTestKeyedChildStruct *pKeyedChild3 = StructCreate(parse_DifTestKeyedChildStruct);

			sprintf(pKeyedChild1->name, "foo");
			sprintf(pKeyedChild2->name, "bar");
			sprintf(pKeyedChild3->name, "wakka");

			eaIndexedEnable(&pStruct->ppKeyedChildren, parse_DifTestKeyedChildStruct);
			eaIndexedAdd(&pStruct->ppKeyedChildren, pKeyedChild1);
			eaIndexedAdd(&pStruct->ppKeyedChildren, pKeyedChild2);
			eaIndexedAdd(&pStruct->ppKeyedChildren, pKeyedChild3);
		}
		break;

	case 10:
		pStruct->ppKeyedChildren[1]->foo += 5;
		break;

	case 11:
		{
			DifTestKeyedChildStruct *pKeyedChild4 = StructCreate(parse_DifTestKeyedChildStruct);
	
			sprintf(pKeyedChild4->name, "wappa");

			eaIndexedAdd(&pStruct->ppKeyedChildren, pKeyedChild4);
		}
		break;

	case 12:
		{
			DifTestKeyedChildStruct *pKeyedChild4;
	
			StructDestroy(parse_DifTestKeyedChildStruct, pStruct->ppKeyedChildren[1]);
			eaRemove(&pStruct->ppKeyedChildren, 1);

			pKeyedChild4 = StructCreate(parse_DifTestKeyedChildStruct);
	
			sprintf(pKeyedChild4->name, "abba");

			eaIndexedAdd(&pStruct->ppKeyedChildren, pKeyedChild4);

			pKeyedChild4 = StructCreate(parse_DifTestKeyedChildStruct);
	
			sprintf(pKeyedChild4->name, "aaaa");

			eaIndexedAdd(&pStruct->ppKeyedChildren, pKeyedChild4);

			pKeyedChild4 = StructCreate(parse_DifTestKeyedChildStruct);
	
			sprintf(pKeyedChild4->name, "acca");

			eaIndexedAdd(&pStruct->ppKeyedChildren, pKeyedChild4);
		}
		break;

	case 13:
		assert(eaSize(&pStruct->ppKeyedChildren) >= 5);
		pStruct->ppKeyedChildren[0]->bit++;
		pStruct->ppKeyedChildren[0]->pOptString = strdup("this is a test");
		pStruct->ppKeyedChildren[1]->embeddedChild.bar = 3;
		pStruct->ppKeyedChildren[2]->ints[2] = 1;
		pStruct->ppKeyedChildren[3]->pMaybeChild = StructCreate(parse_DifTestChildStruct);
		pStruct->ppKeyedChildren[3]->pMaybeChild->foo = 15;
		eaPush(&pStruct->ppKeyedChildren[2]->ppLotsOfChildren, StructCreate(parse_DifTestChildStruct));
		pStruct->ppKeyedChildren[2]->ppLotsOfChildren[0]->bar = 32;
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[2]->ppLotsOfChildren));
//		MultiValSetInt(&pStruct->ppKeyedChildren[4]->embeddedMV, 100);
		MultiValSetString(&pStruct->MVArray[2], "This is a test");
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[0]->ppLotsOfChildren));
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[1]->ppLotsOfChildren));
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[2]->ppLotsOfChildren));
		break;

	case 14:
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[0]->ppLotsOfChildren));
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[1]->ppLotsOfChildren));
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[2]->ppLotsOfChildren));
		StructDestroy(parse_DifTestKeyedChildStruct, pStruct->ppKeyedChildren[1]);
		eaRemove(&pStruct->ppKeyedChildren, 1);
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[0]->ppLotsOfChildren));
		printf("size of lotsofchildren is %d\n", eaSize(&pStruct->ppKeyedChildren[1]->ppLotsOfChildren));

		break;

	case 15:
		pStruct->x++;
		break;

	case 16:

		StructDestroy(parse_DifTestKeyedChildStruct, pStruct->ppKeyedChildren[0]);
		eaRemove(&pStruct->ppKeyedChildren, 0);
		
		break;

	default:
		return false;
	}

	return true;
}

DifTestStruct oldStruct = {0}, targetStruct = {0}, newStruct = {0}, tempStruct = {0};

AUTO_RUN_LATE;
void doDifTesting(void)
{
	int iIndex = 0;
	//Packet* pkt;
	char *estr = 0;
	StructDiff* diff;
	NetLink dummy_link = {0};
	dummy_link.flags = LINK_PACKET_VERIFY;

	estrStackCreate(&estr);


#define CHECK_NEWSTRUCT StructCopyAll(parse_DifTestStruct, &newStruct, &tempStruct); StructDeInit(parse_DifTestStruct, &newStruct); StructCopyAll(parse_DifTestStruct, &tempStruct, &newStruct); StructDeInit(parse_DifTestStruct, &tempStruct);
	while (applyChange(&newStruct, iIndex++)) 
	{

		CHECK_NEWSTRUCT
		CHECK_NEWSTRUCT

		estrClear(&estr);
		diff = StructMakeDiff(parse_DifTestStruct, &oldStruct, &newStruct, 0,0,true, false);
		StructWriteTextDiffFromBDiff(&estr, diff);
		printf("%s\n\n", estr);

		
		//pkt = pktCreateRaw(&dummy_link);
		//ParserSend(parse_DifTestStruct, pkt, &oldStruct, &newStruct, SENDDIFF_FLAG_COMPAREBEFORESENDING, 0, 0, NULL);

		//CHECK_NEWSTRUCT

		//pktSetIndex(pkt, 0);
		//ParserRecv(parse_DifTestStruct, pkt, &targetStruct, RECVDIFF_FLAG_COMPAREBEFORESENDING);

		//CHECK_NEWSTRUCT

		//assertmsgf(StructCompare(parse_DifTestStruct, &newStruct, &targetStruct, 0, 0, 0) == 0, "Diffing failure in setup %d", iIndex-1);

		//StructCopyAll(parse_DifTestStruct, &targetStruct, &oldStruct);

		//CHECK_NEWSTRUCT

		//pktFree(&pkt);
		
	} 

}

*/

/*
AUTO_STRUCT;
typedef struct bitDefTest
{
	U32 bit1 : 1; 
	U32 bit2 : 1; AST(DEF(1))
	U32 bit3 : 1; AST(DEF(0))
} bitDefTest;

AUTO_RUN;
void testBitDef(void)
{
	bitDefTest *pTest = StructCreate(parse_bitDefTest);
}*/
/*
AUTO_STRUCT;
typedef struct dirtyC
{
	int wakka;
} dirtyC;

AUTO_STRUCT;
typedef struct dirtyD
{
	DirtyBit dirtyBit;
	int wakka;
} dirtyD;


AUTO_STRUCT;
typedef struct dirtyB
{
	DirtyBit dirtyBit;
	dirtyC *pC;
	dirtyD *pD;
} dirtyB;

AUTO_STRUCT;
typedef struct dirtyA
{
	int foo;
	dirtyB *pB;
} dirtyA;

AUTO_STRUCT;
typedef struct dirtyTop
{
	int foo;
	dirtyA *pA;
} dirtyTop;




AUTO_RUN_LATE;
void DirtyBitRecurseTest(void)
{
	printf("Top: %d, children %d\n", ParserTableHasDirtyBit(parse_dirtyTop), ParserChildrenHaveDirtyBit(parse_dirtyTop));
	printf("A: %d, children %d\n", ParserTableHasDirtyBit(parse_dirtyA), ParserChildrenHaveDirtyBit(parse_dirtyA));
	printf("B: %d, children %d\n", ParserTableHasDirtyBit(parse_dirtyB), ParserChildrenHaveDirtyBit(parse_dirtyB));
	printf("C: %d, children %d\n", ParserTableHasDirtyBit(parse_dirtyC), ParserChildrenHaveDirtyBit(parse_dirtyC));
	printf("D: %d, children %d\n", ParserTableHasDirtyBit(parse_dirtyD), ParserChildrenHaveDirtyBit(parse_dirtyD));
}*/
/*
AUTO_STRUCT;
typedef struct bigStringTestStruct
{
	int foo;
	char *pString;
} bigStringTestStruct;

AUTO_STRUCT;
typedef struct bigStringTestStructList
{
	bigStringTestStruct **ppTestStructs;
} bigStringTestStructList;


void bigStringTest(void)
{
	int i;
	size_t iLen;
	bigStringTestStructList list = {0};

	FILE *pOutFile = fopen("c:/bigstringtest.txt", "wt");
	fprintf(pOutFile, "{\nTestStructs\n{\nfoo 7\nString \"");
	for (i=0; i < 1000000; i++)
	{
		fprintf(pOutFile, "a");
	}
	fprintf(pOutFile, "\"\n}\n}");
	fclose(pOutFile);

	ParserReadTextFile("c:/bigstringtest.txt", parse_bigStringTestStructList, &list, 0);
	iLen = strlen(list.ppTestStructs[0]->pString);
	i = 0;
}
*/
/*
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct mpTest
{
	int x;
	int y;
} mpTest;

AUTO_STRUCT;
typedef struct mpOuterTest
{
	int foo;
	int bar;
	mpTest *pInnerTest;
} mpOuterTest;

MP_DEFINE(mpTest);

AUTO_RUN_FILE;
void mpTestFunc(void)
{
	mpOuterTest *pTest1;
//	mpOuterTest *pTest2;

	MP_CREATE(mpTest, 100);

	pTest1 = StructCreate(parse_mpOuterTest);
	pTest1->pInnerTest = StructCreate(parse_mpTest);

	pTest1->pInnerTest->x = 5;
	pTest1->foo = 7;

	ParserWriteTextFile("c:\\test.txt", parse_mpOuterTest, pTest1, 0, 0);

	StructDestroy(parse_mpOuterTest, pTest1);

	pTest1 = StructCreate(parse_mpOuterTest);
	ParserReadTextFile("c:\\test.txt", parse_mpOuterTest, pTest1, 0);

	StructDestroy(parse_mpOuterTest, pTest1);

}*/
/*
AUTO_STRUCT AST_SINGLETHREADED_MEMPOOL;
typedef struct usedFieldTest 
{
	int x;
	int y;
	U32 testUsedField[1];				AST( USEDFIELD)

	U8 test2[34];
	U16 test3[117];

} usedFieldTest;

MP_DEFINE(usedFieldTest);

AUTO_RUN_FILE;
void asdfasdfsadfsadf(void)
{
	usedFieldTest *pTest;

	MP_CREATE(usedFieldTest, 1024);

	pTest = StructCreate(parse_usedFieldTest);
	pTest->x = 5;
	ParserWriteTextFile("c:\\test.txt", parse_usedFieldTest, pTest, 0, 0);
	StructDestroy(parse_usedFieldTest, pTest);

	pTest = StructCreate(parse_usedFieldTest);
	ParserReadTextFile("c:\\test.txt", parse_usedFieldTest, pTest, 0);
	StructDestroy(parse_usedFieldTest, pTest);


	TestTextParser();
}
*/
/*
AUTO_STRUCT;
typedef struct InnerTest
{
	char *pName; AST(KEY)
	int iFoo;
} InnerTest;

AUTO_STRUCT;
typedef struct OuterTest
{
	InnerTest **ppInners;
} OuterTest;

AUTO_RUN;
void wildcardTest(void)
{
	OuterTest *pOuter = StructCreate(parse_OuterTest);
	WildCardQueryResult **ppResults = NULL;
	char *pResultString = NULL;
	bool bResult;

	InnerTest *pInner = StructCreate(parse_InnerTest);
	pInner->pName = strdup("Happy");
	pInner->iFoo = 17;
	eaPush(&pOuter->ppInners, pInner);

	pInner = StructCreate(parse_InnerTest);
	pInner->pName = strdup("Happy");
	pInner->iFoo = 17;
	eaPush(&pOuter->ppInners, pInner);

	pInner = StructCreate(parse_InnerTest);
	pInner->pName = strdup("Sad");
	pInner->iFoo = 14;
	eaPush(&pOuter->ppInners, pInner);

	pInner = StructCreate(parse_InnerTest);
	pInner->pName = strdup("Kooky");
	pInner->iFoo = 19;
	eaPush(&pOuter->ppInners, pInner);

	pInner = StructCreate(parse_InnerTest);
	pInner->pName = strdup("Wacky");
	pInner->iFoo = 30;
	eaPush(&pOuter->ppInners, pInner);


	bResult = objDoWildCardQuery(".Inners[*].Foo", parse_OuterTest, pOuter, &ppResults, &pResultString);

}*/

/*
AUTO_STRUCT;
typedef struct intArrayTest
{
	int *pInts;
} intArrayTest;

AUTO_RUN_FILE;
void intArrayTestFunc(void)
{
	intArrayTest test  = {0};


	ParserReadTextFile("c:\\test.txt", parse_intArrayTest, &test, 0);
}
*/
/*
AUTO_RUN_FILE;
void fileListTest(void)
{
	TPFileList *pList = TPFileList_ReadDirectory("c:\\test\\");
	TPFileList_WriteDirectory("c:\\test2\\", pList);
	StructDestroy(parse_TPFileList, pList);
}
*/

/*

AUTO_STRUCT;
typedef struct FilterPairStruct
{
	char *pCharName; AST(STRUCTPARAM)
	char *pAccountName; AST(STRUCTPARAM)
} FilterPairStruct;

AUTO_STRUCT;
typedef struct FilterStruct
{
	char **ppDirtyWords;
	FilterPairStruct **ppPairs;
} FilterStruct;

int CountChars(const char *pStr, char c)
{
	int iCount = 0;
	while (*pStr)
	{
		if (*pStr == c)
		{
			iCount++;
		}
		pStr++;
	}

	return iCount;
}

void FilterStructReformattingCB(char *pInString, char **ppOutString)
{
	char *pReadHead = pInString;

	estrConcatf(ppOutString, "{\n");

	while (pReadHead)
	{
		char *pEOL = strchr(pReadHead, '\n');
		int iNumQuotes;

		if (pEOL)
		{
			*pEOL = 0;
		}

		iNumQuotes = CountChars(pReadHead, '"');

		if (iNumQuotes == 2)
		{
			estrConcatf(ppOutString, "DirtyWords %s\n", pReadHead);
		}
		else if (iNumQuotes == 4)
		{
			estrConcatf(ppOutString, "Pairs %s\n", pReadHead);
		}
		else if (iNumQuotes != 0)
		{
			Errorf("Invalid line %s found during filter struct reforamtting", pReadHead);
		}

		if (pEOL)
		{
			pReadHead = pEOL + 1;
		}
		else
		{
			pReadHead = NULL;
		}
	}
	estrConcatf(ppOutString, "}\n");
}	






AUTO_RUN_FILE;
void FilterStructTestFunc(void)
{
	FilterStruct *pFilter = StructCreate(parse_FilterStruct);

	ParserSetReformattingCallback(parse_FilterStruct, FilterStructReformattingCB);

	ParserReadTextFile("c:\\test.txt", parse_FilterStruct, pFilter, 0);
}
*/

/*
AUTO_STRUCT;
typedef struct innerBinLayoutTest
{
	int x;
	int y;
	int z;
	int w;
	char *pFoo;
} innerBinLayoutTest;


AUTO_STRUCT;
typedef struct binLayoutTest
{
	int x;
	int y;
	innerBinLayoutTest *pInner1;
	int z;
	int w;
	innerBinLayoutTest *pInner2;
	char *pFoo;
} binLayoutTest;

AUTO_RUN_FILE;
void binLayoutTestFunc(void)
{
	char layoutFileName[CRYPTIC_MAX_PATH];

	binLayoutTest *pTest = StructCreate(parse_binLayoutTest);
	pTest->x = 4;

	sprintf(layoutFileName, "%s/BinLayoutFiles/%s_layout.txt", fileLocalDataDir(), "layoutTest.bin");

	ParserWriteBinaryFile("bin/LayoutTest.bin", "c:\\test.bin.layout.txt", parse_binLayoutTest, pTest, NULL, NULL, NULL, 0, 0, NULL, 0, 0);
}
*/
/*
AUTO_COMMAND;
void testMultiValCmd(int x, float f, char *pStr)
{
	printf("%d %f %s\n", x, f, pStr);
}

AUTO_RUN;
void MultiValCmdRun(void)
{
	MultiVal **ppMultiVals = NULL;
	CmdContext context = {0};

	MultiVal *pNew = MultiValCreate();
	MultiValSetInt(pNew, 14);
	eaPush(&ppMultiVals, pNew);

	pNew = MultiValCreate();
	MultiValSetFloat(pNew, 35.0f);
	eaPush(&ppMultiVals, pNew);

	pNew = MultiValCreate();
	MultiValSetString(pNew, "hello world");
	eaPush(&ppMultiVals, pNew);

	context.access_level = 9;

	cmdExecuteWithMultiVals(&gGlobalCmdList, "testMultiValCmd", &context, &ppMultiVals);

	eaDestroyEx(&ppMultiVals, MultiValDestroy);

}
	
*/
/*
AUTO_STRUCT;
typedef struct SCTestInner
{
	int x; 
} SCTestInner;

AUTO_STRUCT;
typedef struct SCTestOuter
{
	int foo;
	SCTestInner *pInner; AST(SERVER_ONLY)
} SCTestOuter;

AUTO_RUN;
void SCTest(void)
{
	SCTestOuter *pOuter1 = StructCreate(parse_SCTestOuter);
	SCTestOuter *pOuter2 = StructCreate(parse_SCTestOuter);
	pOuter1->foo = 7;

	pOuter1->pInner = StructCreate(parse_SCTestInner);
	pOuter1->pInner->x = 3;

	StructCopy(parse_SCTestOuter, pOuter1, pOuter2, STRUCTCOPYFLAG_ALWAYS_RECURSE, 0, TOK_SERVER_ONLY);
}
*/
/*
AUTO_RUN_FILE;
void shortCutTest(void)
{
	char deskTop[MAX_PATH];
	SHGetSpecialFolderPath(NULL, deskTop, CSIDL_DESKTOPDIRECTORY, 0);

	createShortcut(strdup("c:\\src\\utilities\\bin\\continuousbuilder.exe"), strdup("c:\\continuousbuilder\\FC CONTINUOUS"), 0, NULL, strdup("-foo"), strdup("Fightclub Continuous Builder"));
}*/
/*

AUTO_STRUCT;
typedef struct innerDirty
{
	DirtyBit bDirty;

	int x;
	int y;
} innerDirty;

AUTO_STRUCT;
typedef struct outerDirty
{
	innerDirty *pInner;
	int foo;
} outerDirty;

AUTO_RUN;
void DirtyTestFunc(void)
{
	Packet *pPak;
	outerDirty *pOuter1, *pOuter2, *pOuter3;

	pOuter1 = StructCreate(parse_outerDirty);
	pOuter1->pInner = StructCreate(parse_innerDirty);

	pOuter2 = StructClone(parse_outerDirty, pOuter1);
	pOuter3 = StructClone(parse_outerDirty, pOuter1);

	assert(pOuter2);
	assert(pOuter3);

	pOuter2->pInner->x = 5;
	pOuter2->foo = 7;

	pPak = pktCreateTemp(NULL);
	ParserSend(parse_outerDirty, pPak, pOuter1, pOuter2, SENDDIFF_FLAG_COMPAREBEFORESENDING, 0, 0,
				NULL);
	ParserRecv(parse_outerDirty, pPak, pOuter3, RECVDIFF_FLAG_COMPAREBEFORESENDING);
	pktFree(&pPak);


	pOuter2->pInner->bDirty = true;

	pPak = pktCreateTemp(NULL);
	ParserSend(parse_outerDirty, pPak, pOuter1, pOuter2, SENDDIFF_FLAG_COMPAREBEFORESENDING, 0, 0,
				NULL);
	ParserRecv(parse_outerDirty, pPak, pOuter3, RECVDIFF_FLAG_COMPAREBEFORESENDING);
	pktFree(&pPak);


}



AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
void JoeDragoIsMyBuddy(void)
{



}

AUTO_STRUCT AST_THREADSAFE_MEMPOOL;
typedef struct TsmTestStruct
{
	int x;
	int y;
} TsmTestStruct;

#include "threadsafememorypool.h"

TSMP_DEFINE(TsmTestStruct);

AUTO_RUN;
void TsmTestInit(void)
{
	TSMP_CREATE(TsmTestStruct, 256);
}

AUTO_RUN_LATE;
void TsmTest(void)
{
	TsmTestStruct *pTest = StructCreate(parse_TsmTestStruct);

	StructDestroy(parse_TsmTestStruct, pTest);
}
*/
/*
AUTO_ENUM;
typedef enum enumMood
{
	MOOD_HAPPY,
	MOOD_SAD
} enumMood;

AUTO_STRUCT;
typedef struct inner1
{
	int x;
	int y;
} inner1;

AUTO_STRUCT;
typedef struct inner2
{
	int y;
	int x;
	int z;
} inner2;

AUTO_STRUCT;
typedef struct ts1
{
	int testArray[10];
	float f;
	char *pFoo;
	int x;
	int y;
	int z;
	char *pMood;
	int *pTestEArray;
	inner1 embeddedStruct;
	inner1 *pOptionalStruct;
	inner1 **ppEArrayOfStructs;
} ts1;

AUTO_STRUCT;
typedef struct ts2
{
	inner2 embeddedStruct;
	inner2 *pOptionalStruct;
	inner2 **ppEArrayOfStructs;
	int y;
	int z;
	int *pTestEArray;
	int x;
	float f;
	char *pFoo;
	int testArray[10];
	enumMood eMood;
} ts2;

AUTO_RUN;
void send2tpitest(void)
{
	ts1 t1 = { { 2, 4, 6, 8, 10, 12, 14, 16, 18, 20 }, 3.5, "hahaha", 12, 30, 180, "SAD", NULL, { 5, 10 }};
	ts2 t2 = {0};
	inner1 *pInner;

	Packet *pPack = pktCreateTemp(NULL);

	ea32Push(&t1.pTestEArray, 14);
	ea32Push(&t1.pTestEArray, 140);
	ea32Push(&t1.pTestEArray, 1400);

	t1.pOptionalStruct = StructCreate(parse_inner1);
	t1.pOptionalStruct->x = 20;
	t1.pOptionalStruct->y = 40;

	pInner = StructCreate(parse_inner1);
	pInner->x = 100;
	pInner->y = 200;
	eaPush(&t1.ppEArrayOfStructs, pInner);
	eaPush(&t1.ppEArrayOfStructs, NULL);
	eaPush(&t1.ppEArrayOfStructs, NULL);
	pInner = StructCreate(parse_inner1);
	pInner->x = 400;
	pInner->y = 800;
	eaPush(&t1.ppEArrayOfStructs, pInner);
	


	ParserSendStruct(parse_ts1, pPack, &t1);

	ParserRecv2tpis(pPack, parse_ts1, parse_ts2, &t2);
}
*/
/*
AUTO_RUN;
void printfTest(void)
{
	char *pStr = NULL;
	int x;

	estrPrintfUnsafe(&pStr, "%Afoo%A", 0, 1234);

	x = 0;

}

AUTO_STRUCT;
typedef struct eTestStruct
{
	int x;
	char temp[32];
} eTestStruct;

AUTO_RUN;
void eTestFunc(void)
{
	eTestStruct t1 = { 7, "seven"};
	eTestStruct t2 = { 3, "three"};

	eTestStruct **ppTestStructs = NULL;
	eTestStruct **ppOutTestStructs = NULL;
	char *pTestStr = NULL;

	eaPush(&ppTestStructs, &t1);
	eaPush(&ppTestStructs, &t2);

	eaStructArrayToString(&ppTestStructs, parse_eTestStruct, &pTestStr);
	eaStructArrayFromString(&ppOutTestStructs, parse_eTestStruct, pTestStr);
}
*/


/*
AUTO_RUN_FILE;
void gzTest(void)
{
	int i;
	FILE *pFile = fopen("c:\\temp.gz", "wbz");
	printf("\n\n\nZlib write test: c:\\temp.gz\n");
	for (i=0; i < 100; i++)
	{
		Sleep(1000);
		fprintf(pFile, "%d\n", i);
		printf("%d\n", i);
	}
	fclose(pFile);
}


#undef FILE
#undef fclose
#undef fprintf


AUTO_RUN;
void fsopenTest(void)
{
	int i;
	FILE *pFile = _fsopen("c:\\temp.txt", "wb", _SH_DENYWR);
	printf("\n\n\nDirect write test: c:\\temp.txt\n");
	for (i=0; i < 100; i++)
	{
		Sleep(1000);
		fprintf(pFile, "%d\n", i);
		printf("%d\n", i);
	}

	fclose(pFile);
}
*/
/*
AUTO_STRUCT;
typedef struct testNoPool
{
	int x;
	char **ppStrings;
} testNoPool;

AUTO_STRUCT;
typedef struct testPool
{
	int x;
	char **ppStrings; AST(POOL_STRING)
} testPool;


AUTO_RUN;
void asdfasdfas(void)
{
	testNoPool tnp = {0};
	testPool tp = {0};
	Packet *pPak;
	int iResult;

	eaPush(&tp.ppStrings, (char*)allocAddString("foo"));
	eaPush(&tp.ppStrings, (char*)allocAddString("bar"));
	eaPush(&tp.ppStrings, (char*)allocAddString(""));
	eaPush(&tp.ppStrings, (char*)allocAddString("asdfasf"));

	pPak = pktCreateTemp(NULL);
	ParserSendStruct(parse_testPool, pPak, &tp);
	iResult = ParserRecv2tpis(pPak, parse_testPool, parse_testNoPool, &tnp);
}
	
*/
/*
AUTO_STRUCT;
typedef struct IgnoreTestInner
{
	int x;
	float f;
	char s[256];
} IgnoreTestInner;

AUTO_STRUCT;
typedef struct IgnoreTest
{
	int y;
	float z;
	char s2[256];
	IgnoreTestInner inner1;

	IgnoreTestInner **ppOtherInners;
} IgnoreTest;


AUTO_RUN_FILE;
void IgnoreTestFunc(void)
{
	IgnoreTest *pIgnoreTest = StructCreate(parse_IgnoreTest);

	ParserReadTextFile("c:\\temp\\temp.txt", parse_IgnoreTest, pIgnoreTest, PARSER_IGNORE_ALL_UNKNOWN);

}*/
/*
AUTO_RUN_FILE;
void QueryableProcTest(void)
{
	QueryableProcessHandle *pHandle = StartQueryableProcess("gimme -glvfold c:\\fightclub\\data", true, false, false,  "c:\\temp\\svntest.txt");

	while (!QueryableProcessComplete(&pHandle, NULL))
	{
		Sleep(1000);
	}

	{
		int iBrk = 0;
	}
}*/
/*
AUTO_RUN;
void testXcopy(void)
{
	QueryableProcessHandle *pHandle = StartQueryableProcess("xcopy c:\\temp\\*.* c:\\temp2", true, false, false, "c:\\test.txt");

	while (!QueryableProcessComplete(&pHandle, NULL))
	{
		Sleep(1);
	}
}*/

/*
AUTO_STRUCT;
typedef struct innerTestStruct
{
	int x; AST(STRUCTPARAM)
	int y; AST(STRUCTPARAM)
	int z; AST(STRUCTPARAM)
	int w;
} innerTestStruct;

AUTO_STRUCT;
typedef struct outerTestStruct
{
	innerTestStruct embedded1;
	innerTestStruct embedded2;
	innerTestStruct embedded3;
	innerTestStruct embedded4;
	innerTestStruct *pOptional1;
	innerTestStruct *pOptional2;
	innerTestStruct *pOptional3;
	innerTestStruct *pOptional4;
	innerTestStruct *pOptional5;
	innerTestStruct **ppEArray;
} outerTestStruct;

AUTO_RUN_FILE;
void test(void)
{
	outerTestStruct *pOuter = StructCreate(parse_outerTestStruct);
	outerTestStruct *pOuter2 = StructCreate(parse_outerTestStruct);
	int iCmp;
	int i;

	pOuter->embedded2.x = 11;
	pOuter->embedded3.w = 4;
	pOuter->embedded4.x = 3;
	pOuter->embedded4.y = 3;
	pOuter->embedded4.w = 3;


	pOuter->pOptional1 = StructCreate(parse_innerTestStruct);
	pOuter->pOptional2 = StructCreate(parse_innerTestStruct);
	pOuter->pOptional3 = StructCreate(parse_innerTestStruct);
	pOuter->pOptional4 = StructCreate(parse_innerTestStruct);

	pOuter->pOptional2->x = 11;
	pOuter->pOptional3->w = 4;
	pOuter->pOptional4->x = 3;
	pOuter->pOptional4->y = 3;
	pOuter->pOptional4->w = 3;


	for (i=0; i < 10; i++)
	{
		innerTestStruct *pInner = StructCreate(parse_innerTestStruct);

		if (i == 2)
		{
			pInner->x = 5;
			pInner->z = 3;
		}
		else if (i == 8)
		{
			pInner->x = 5;
			pInner->y = 3;
			pInner->w = 3;
		}
		else if (i == 5)
		{
			pInner->w = 3;
		}

		eaPush(&pOuter->ppEArray, pInner);
	}

	ParserWriteTextFile("c:\\temp\\test.txt", parse_outerTestStruct, pOuter, 0, 0);
	ParserReadTextFile("c:\\temp\\test.txt", parse_outerTestStruct, pOuter2, 0);

	iCmp = StructCompare(parse_outerTestStruct, pOuter, pOuter2, 0, 0, 0);
}
*/


/*
typedef struct Entity Entity;

AUTO_EXPR_FUNC(FUNCS) ACMD_NAME(BenTestFunc1);
S32 benTestExprFunc1(float z, SA_PARAM_OP_VALID Entity *pEnt, const char *param, float f)
{
	return 0;
}


AUTO_EXPR_FUNC(FUNCS) ACMD_NAME(BenTestFunc2);
S32 benTestExprFunc2(ACMD_EXPR_SELF Entity *pEnt, float z, const char *param, int x)
{
	return 0;
}
*/
/*
#include "blockEarray.h"

AUTO_STRUCT;
typedef struct beaTestStruct
{
	int x;
	char *pStr; AST(ESTRING DEF("this is a test"))
	U8 y; AST(DEF(5))
} beaTestStruct;

typedef struct beaOtherTestStruct
{
	int x;
	char str[16];
} beaOtherTestStruct;

AUTO_RUN;
void beaTest(void)
{
	int i;
	beaTestStruct *pStructs = NULL;
	beaOtherTestStruct *pOtherStructs = NULL;

	for (i=0; i < 10; i++)
	{
		beaTestStruct *pNewStruct = beaPushEmptyStruct(&pStructs, parse_beaTestStruct);
		pNewStruct->x = i;
		estrPrintf(&pNewStruct->pStr, "%d", i);
	}

	beaInsertEmptyStruct(&pStructs, parse_beaTestStruct, 10);
	beaInsertEmptyStruct(&pStructs, parse_beaTestStruct, 0);
	beaInsertEmptyStruct(&pStructs, parse_beaTestStruct, 5);

	for (i=0; i < 10; i++)
	{
		beaOtherTestStruct *pOtherStruct = beaPushEmpty(&pOtherStructs);
		pOtherStruct->x = i;
		sprintf(pOtherStruct->str, "%d", i);
	}

	beaInsertEmpty(&pOtherStructs, 10);
	beaInsertEmpty(&pOtherStructs, 0);
	beaInsertEmpty(&pOtherStructs, 5);
}
*/
/*
#include "blockearray.h"

AUTO_STRUCT;
typedef struct mvTestStruct
{
	MultiVal mv;
	MultiVal **ppMvEarray;
	MultiVal *pMvBlockArray;
} mvTestStruct;

AUTO_RUN_FILE;
void mvTest(void)
{
	while (1)
	{
		mvTestStruct *pStruct = StructCreate(parse_mvTestStruct);
		mvTestStruct *pStruct2 = StructCreate(parse_mvTestStruct);
		mvTestStruct *pStruct3 = StructCreate(parse_mvTestStruct);
		
		mvTestStruct *pStruct4;

		int i;
		int result;

		MultiValSetInt(&pStruct->mv, 5);


		for (i=0; i < 10; i++)
		{
			MultiVal *pMV = MultiValCreate();
			MultiValSetInt(pMV, i);
			eaPush(&pStruct->ppMvEarray, pMV);
		}

		beaSetSize(&pStruct->pMvBlockArray, 10);
		for (i=0;i < 10; i++)
		{
			MultiValSetInt(&pStruct->pMvBlockArray[i], i);
		}

		ParserWriteTextFile("c:\\temp\\test.txt", parse_mvTestStruct, pStruct, 0, 0);

		ParserReadTextFile("c:\\temp\\test.txt", parse_mvTestStruct, pStruct2, 0);

		result = StructCompare(parse_mvTestStruct, pStruct, pStruct2, 0, 0, 0);
		
		ParserWriteBinaryFile("c:\\temp\\test.bin", NULL, parse_mvTestStruct, pStruct, NULL, NULL, NULL, 0, 0, NULL, 0, 0);

		ParserOpenReadBinaryFile(NULL, "c:\\temp\\test.bin", parse_mvTestStruct, pStruct3, NULL, NULL, NULL, 0, 0, 0);

		result = StructCompare(parse_mvTestStruct, pStruct, pStruct3, 0, 0, 0);

		pStruct4 = StructClone(parse_mvTestStruct, pStruct);
		result = StructCompare(parse_mvTestStruct, pStruct, pStruct4, 0, 0, 0);

		StructDestroy(parse_mvTestStruct, pStruct);
		StructDestroy(parse_mvTestStruct, pStruct2);
		StructDestroy(parse_mvTestStruct, pStruct3);
		StructDestroy(parse_mvTestStruct, pStruct4);
	}

}
*/

/*

#include "blockEarray.h"

AUTO_STRUCT;
typedef struct dumbTestInner
{
	int x; AST(DEF(5))
} dumbTestInner;

AUTO_STRUCT;
typedef struct beaStructTestInner
{
	int x;
	float f;
	char *pFoo; AST(ESTRING)
	int bob; AST(DEF(12343))
} beaStructTestInner;

AUTO_STRUCT;
typedef struct beaStructTestOuter
{
	beaStructTestInner *pBea; AST(BLOCK_EARRAY)

	dumbTestInner **ppDumbs;
} beaStructTestOuter;

int giCount = 0;

AUTO_FIXUPFUNC;
TextParserResult FixupbeaStructTestInner(beaStructTestInner *pInner, enumTextParserFixupType type, void *pExtraData)
{
	if (type == FIXUPTYPE_CONSTRUCTOR)
	{
		printf("Constructed %d\n", ++giCount);
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult FixupdumbTestInner(dumbTestInner *pInner, enumTextParserFixupType type, void *pExtraData)
{
	if (type == FIXUPTYPE_CONSTRUCTOR)
	{
		printf("Dumb\n");
	}
	return PARSERESULT_SUCCESS;
}


AUTO_RUN_FILE;
void beaStructTest(void)
{
	int i;
	int result = 0;
	Packet *pak;



	beaStructTestOuter *pOuter = StructCreate(parse_beaStructTestOuter);
	beaStructTestOuter *pOuter2 = StructCreate(parse_beaStructTestOuter);
	beaStructTestOuter *pOuter3 = StructCreate(parse_beaStructTestOuter);
	beaStructTestOuter *pOuter4;
	beaStructTestOuter *pOuter5 = StructCreate(parse_beaStructTestOuter);
	beaStructTestOuter *pOuter6 = StructCreate(parse_beaStructTestOuter);


	for (i=0; i < 10; i++)
	{
		beaPushEmptyStruct(&pOuter->pBea, parse_beaStructTestInner);
		pOuter->pBea[i].x = i;
		pOuter->pBea[i].f = i;
		estrPrintf(&pOuter->pBea[i].pFoo, "%d", i);
	}
	giCount = 0;

	for (i=0; i < 3; i++)
	{
		eaPush(&pOuter->ppDumbs, StructCreate(parse_dumbTestInner));
	}

	printf("--TextFile--\n");

	ParserWriteTextFile("c:\\temp\\test.txt", parse_beaStructTestOuter, pOuter, 0, 0);
	ParserReadTextFile("c:\\temp\\test.txt", parse_beaStructTestOuter, pOuter2, 0);
	

	result = StructCompare(parse_beaStructTestOuter, pOuter, pOuter2, 0, 0, 0);
	giCount = 0;	


	printf("--BinaryFile--\n");

	ParserWriteBinaryFile("c:\\temp\\test.bin", NULL, parse_beaStructTestOuter, pOuter, NULL, NULL, NULL, 0, 0, NULL, 0, 0);

	ParserOpenReadBinaryFile(NULL, "c:\\temp\\test.bin", parse_beaStructTestOuter, pOuter3, NULL, NULL, NULL, 0, 0, 0);

	result = StructCompare(parse_beaStructTestOuter, pOuter, pOuter3, 0, 0, 0);

	giCount = 0;
	printf("--StructClone--\n");
	pOuter4 = StructClone(parse_beaStructTestOuter, pOuter);
	result = StructCompare(parse_beaStructTestOuter, pOuter, pOuter4, 0, 0, 0);


	giCount = 0;
	printf("--SendRecv--\n");

	pak = pktCreateTemp(NULL);
	ParserSendStruct(parse_beaStructTestOuter, pak, pOuter);
	ParserRecv(parse_beaStructTestOuter, pak, pOuter5, 0);
	pktFree(&pak);

	result = StructCompare(parse_beaStructTestOuter, pOuter, pOuter5, 0, 0, 0);

	giCount = 0;
	printf("--StructCompress--\n");
	StructCompress(parse_beaStructTestOuter, pOuter, pOuter6, NULL, NULL);
	result = StructCompare(parse_beaStructTestOuter, pOuter, pOuter6, 0, 0, 0);

	StructDestroy(parse_beaStructTestOuter, pOuter);
	StructDestroy(parse_beaStructTestOuter, pOuter2);
	StructDestroy(parse_beaStructTestOuter, pOuter3);
	StructDestroy(parse_beaStructTestOuter, pOuter4);
	StructDestroy(parse_beaStructTestOuter, pOuter5);
	StructDestroy(parse_beaStructTestOuter, pOuter6);

	{
		int iBrk = 0;
	}
}
*/

/*
#include "referenceSystem.h"


AUTO_STRUCT;
typedef struct innerBlob
{
	char *pName; AST(KEY)
	int x;
	int y;
} innerBlob;

AUTO_STRUCT;
typedef struct outerBlob
{
	REF_TO(innerBlob) hInner;
} outerBlob;

AUTO_RUN_LATE;
void compareTest(void)
{
	DictionaryHandle hHandle = RefSystem_RegisterSelfDefiningDictionary("testInnerBlob", false, parse_innerBlob, true, false, NULL);

	innerBlob *pInner1 = StructCreate(parse_innerBlob);
	innerBlob *pInner2 = StructCreate(parse_innerBlob);

	outerBlob *pOuter1 = StructCreate(parse_outerBlob);
	outerBlob *pOuter2 = StructCreate(parse_outerBlob);

	int iResult;

	pInner1->pName = strdup("Inner1");
	pInner2->pName = strdup("Inner2");

	RefSystem_AddReferent(hHandle, pInner1->pName, pInner1);
	RefSystem_AddReferent(hHandle, pInner2->pName, pInner2);

	iResult = StructCompare(parse_outerBlob, pOuter1, pOuter2, 0, 0, 0);

	SET_HANDLE_FROM_REFDATA(hHandle, "Inner1", pOuter1->hInner);
	SET_HANDLE_FROM_REFDATA(hHandle, "Inner1", pOuter2->hInner);

	iResult = StructCompare(parse_outerBlob, pOuter1, pOuter2, 0, 0, 0);

	SET_HANDLE_FROM_REFDATA(hHandle, "Inner1", pOuter1->hInner);
	SET_HANDLE_FROM_REFDATA(hHandle, "Inner2", pOuter2->hInner);

	iResult = StructCompare(parse_outerBlob, pOuter1, pOuter2, 0, 0, 0);

	SET_HANDLE_FROM_REFDATA(hHandle, "Inner3", pOuter1->hInner);
	iResult = StructCompare(parse_outerBlob, pOuter1, pOuter2, 0, 0, 0);

	SET_HANDLE_FROM_REFDATA(hHandle, "Inner3", pOuter2->hInner);
	iResult = StructCompare(parse_outerBlob, pOuter1, pOuter2, 0, 0, 0);

	SET_HANDLE_FROM_REFDATA(hHandle, "Inner4", pOuter2->hInner);
	iResult = StructCompare(parse_outerBlob, pOuter1, pOuter2, 0, 0, 0);

	{
		int iBrk = 0;
	}
}*/
/*
AUTO_STRUCT;
typedef struct innerFoo
{
	int x;
	int y;
} innerFoo;

AUTO_STRUCT;
typedef struct outerFoo
{
	innerFoo **ppFoos;
} outerFoo;

innerFoo *MakeInner(int x, int y)
{
	innerFoo *pRetVal = StructCreate(parse_innerFoo);
	pRetVal->x = x;
	pRetVal->y = y;

	return pRetVal;
}

AUTO_RUN;
void fooTest(void)
{
	Packet *pPak;
	int iResult;
	int i;

	outerFoo *pServerOld = StructCreate(parse_outerFoo);
	outerFoo *pServerNew = StructCreate(parse_outerFoo);
	outerFoo *pClient = StructCreate(parse_outerFoo);

	eaPush(&pServerNew->ppFoos, MakeInner(3, 5));

	pPak = pktCreateTemp(NULL);
	ParserSend(parse_outerFoo, pPak, pServerOld, pServerNew, SENDDIFF_FLAG_COMPAREBEFORESENDING, 0, 0, NULL);
	ParserRecv(parse_outerFoo, pPak, pClient, RECVDIFF_FLAG_COMPAREBEFORESENDING);
	iResult = StructCompare(parse_outerFoo, pServerNew, pClient, 0, 0, 0);
	StructCopy(parse_outerFoo, pServerNew, pServerOld, 0, 0, 0);
	pktFree(&pPak);

	for (i=0; i < 10; i++)
	{
		eaPush(&pServerNew->ppFoos, MakeInner(i, i*2));
	}

	pPak = pktCreateTemp(NULL);
	ParserSend(parse_outerFoo, pPak, pServerOld, pServerNew, SENDDIFF_FLAG_COMPAREBEFORESENDING, 0, 0, NULL);
	ParserRecv(parse_outerFoo, pPak, pClient, RECVDIFF_FLAG_COMPAREBEFORESENDING);
	iResult = StructCompare(parse_outerFoo, pServerNew, pClient, 0, 0, 0);
	StructCopy(parse_outerFoo, pServerNew, pServerOld, 0, 0, 0);
	pktFree(&pPak);

	for (i=0; i < 3; i++)
	{
		StructDestroy(parse_innerFoo, pServerNew->ppFoos[0]);
		eaRemove(&pServerNew->ppFoos, 0);
	}

	pPak = pktCreateTemp(NULL);
	ParserSend(parse_outerFoo, pPak, pServerOld, pServerNew, SENDDIFF_FLAG_COMPAREBEFORESENDING, 0, 0, NULL);
	ParserRecv(parse_outerFoo, pPak, pClient, RECVDIFF_FLAG_COMPAREBEFORESENDING);
	iResult = StructCompare(parse_outerFoo, pServerNew, pClient, 0, 0, 0);
	StructCopy(parse_outerFoo, pServerNew, pServerOld, 0, 0, 0);
	pktFree(&pPak);

	pServerNew->ppFoos[eaSize(&pServerNew->ppFoos)-1]->x++;
	StructDestroy(parse_innerFoo, pClient->ppFoos[0]);
	eaRemove(&pClient->ppFoos, 0);
	StructDestroy(parse_innerFoo, pClient->ppFoos[0]);
	eaRemove(&pClient->ppFoos, 0);



	pPak = pktCreateTemp(NULL);
	ParserSend(parse_outerFoo, pPak, pServerOld, pServerNew, SENDDIFF_FLAG_COMPAREBEFORESENDING, 0, 0, NULL);
	ParserRecv(parse_outerFoo, pPak, pClient, RECVDIFF_FLAG_COMPAREBEFORESENDING);
	iResult = StructCompare(parse_outerFoo, pServerNew, pClient, 0, 0, 0);
	StructCopy(parse_outerFoo, pServerNew, pServerOld, 0, 0, 0);
	pktFree(&pPak);

}
*/
/*
#include "referenceSystem.h"

AUTO_STRUCT;
typedef struct testReferent
{
	int iVal;
	char name[32];
} testReferent;

AUTO_STRUCT;
typedef struct TestHandlesStruct
{
	REF_TO(testReferent) hRef1;
	REF_TO(testReferent) hRef2;
	REF_TO(testReferent) hRef3;
} TestHandlesStruct;

AUTO_RUN_LATE;
void dictLockTest(void)
{
	int i;
	TestHandlesStruct testHandles = {0};
	testReferent *pTest;

	testReferent *pTestReferents[21];

	const char *pTemp;


	DictionaryHandle *pDictHandle = RefSystem_RegisterSelfDefiningDictionary("testReferent", false, parse_testReferent, true, false, NULL);

	for (i=0; i < 20; i++)
	{
		pTest = StructCreate(parse_testReferent);
		sprintf(pTest->name, "%d", i);
		pTest->iVal = i;

		RefSystem_AddReferent(pDictHandle, pTest->name, pTest);
	}

	SET_HANDLE_FROM_STRING(pDictHandle, "1", testHandles.hRef1);
	SET_HANDLE_FROM_STRING(pDictHandle, "2", testHandles.hRef2);
	SET_HANDLE_FROM_STRING(pDictHandle, "20", testHandles.hRef3);

	pTest = StructCreate(parse_testReferent);
	sprintf(pTest->name, "20");
	pTest->iVal = 20;

	RefSystem_AddReferent(pDictHandle, pTest->name, pTest);

	for (i=0; i < 21; i++)
	{
		testReferent *pOld;
		
		pTest = StructCreate(parse_testReferent);
		sprintf(pTest->name, "%d", i);
		pTest->iVal = i + 100;

		pOld = RefSystem_ReferentFromString(pDictHandle, pTest->name);
		assert(pOld);

		RefSystem_MoveReferent(pTest, pOld);

		pTestReferents[i] = pTest;
	}

	RefSystem_CheckIntegrity();
	RefSystem_LockDictionaryReferents(pDictHandle);
	RefSystem_CheckIntegrity();

	SET_HANDLE_FROM_STRING(pDictHandle, "3", testHandles.hRef1);
	SET_HANDLE_FROM_STRING(pDictHandle, "4", testHandles.hRef2);
	SET_HANDLE_FROM_STRING(pDictHandle, "50", testHandles.hRef3);

	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef1);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef2);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef3);


	SET_HANDLE_FROM_REFERENT(pDictHandle, pTestReferents[10], testHandles.hRef1);
	SET_HANDLE_FROM_REFERENT(pDictHandle, pTestReferents[11], testHandles.hRef2);
	SET_HANDLE_FROM_REFERENT(pDictHandle, NULL, testHandles.hRef3);

	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef1);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef2);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef3);


	SET_HANDLE_FROM_STRING(pDictHandle, "3", testHandles.hRef1);
	SET_HANDLE_FROM_STRING(pDictHandle, "4", testHandles.hRef2);
	SET_HANDLE_FROM_STRING(pDictHandle, "50", testHandles.hRef3);

	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef1);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef2);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef3);

	SET_HANDLE_FROM_STRING(pDictHandle, "51", testHandles.hRef1);
	SET_HANDLE_FROM_STRING(pDictHandle, "52", testHandles.hRef2);
	SET_HANDLE_FROM_STRING(pDictHandle, "9", testHandles.hRef3);

	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef1);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef2);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef3);

	SET_HANDLE_FROM_STRING(pDictHandle, "3", testHandles.hRef1);
	SET_HANDLE_FROM_STRING(pDictHandle, "4", testHandles.hRef2);
	SET_HANDLE_FROM_STRING(pDictHandle, "50", testHandles.hRef3);

	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef1);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef2);
	pTemp = REF_STRING_FROM_HANDLE(testHandles.hRef3);

	RefSystem_CheckIntegrity();


//	RefSystem_AddReferent(pDictHandle, "this will fail", NULL);

}
*/
/*
AUTO_STRUCT;
typedef struct bitRedundantTest
{
	U32 iTest1 : 1; AST(ADDNAMES(foo, bar))
	U32 iTest2 : 1; AST(ADDNAMES(wakka, wappa))
} bitRedundantTest;

int GetNonRedundantColumnNumFromRedundantColumn(ParseTable *pTPI, int iRedundantColumn);

void FindAllNamesForColumn(ParseTable tpi[], int iColumn, const char ***pppOutNames);


AUTO_RUN;
void redundantTest(void)
{
	char **ppNames1 = NULL;
	char **ppNames2 = NULL;
	printf("%d, %d, %d, %d\n", 
		GetNonRedundantColumnNumFromRedundantColumn(parse_bitRedundantTest, 3),
		GetNonRedundantColumnNumFromRedundantColumn(parse_bitRedundantTest, 4),
		GetNonRedundantColumnNumFromRedundantColumn(parse_bitRedundantTest, 6),
		GetNonRedundantColumnNumFromRedundantColumn(parse_bitRedundantTest, 7));

	FindAllNamesForColumn(parse_bitRedundantTest, 2, &ppNames1);
	FindAllNamesForColumn(parse_bitRedundantTest, 5, &ppNames2);

	{
		int iBrk = 0;
	}
}*/
/*
#include "Structnet.h"
#include "serverlib.h"

typedef struct SafeSendTest1 SafeSendTest1;
typedef struct SafeSendTestInner1 SafeSendTestInner1;

AUTO_STRUCT;
typedef struct SafeSendTest1
{
	int x:1;
	int y:5;
	int z:10;
	int w:14;
	SafeSendTest1 *pSubStruct;

} SafeSendTest1;

AUTO_STRUCT;
typedef struct SafeSendTestInner1
{
	float f;
	float f2;
	SafeSendTest1 *pSubStruct; AST(STRUCT_NORECURSE)
} SafeSendTestInner1;


typedef struct SafeSendTest2 SafeSendTest2;
typedef struct SafeSendTestInner2 SafeSendTestInner2;

AUTO_STRUCT;
typedef struct SafeSendTest2
{
	SafeSendTest2 *pSubStruct;


	int w:14;
	int z;
	int y;
	int x:3;
} SafeSendTest2;


AUTO_STRUCT;
typedef struct SafeSendTestInner2
{
	SafeSendTest2 *pSubStruct; AST(STRUCT_NORECURSE)
	float f2;
	float f;
} SafeSendTestInner2;

SafeSendTest1 test1 = { 1, 30, 800, 1050};
SafeSendTest2 test2 = {0};
SafeSendTest1 innerTest1 = { 2, 3, 4, 5};



void safeSendTestHandleMsg(Packet *pak,int cmd, NetLink *link,void *pUserData)
{
	switch (cmd)
	{
	case LAUNCHERQUERY_VERBOSEPROCLISTLOGGING:
		ParserRecvStructSafe(parse_SafeSendTest2, pak, &test2);
		break;
	}

}

#include "utilitieslib.h"*/
/*
AUTO_RUN_FILE;
void safeSendTest(void)
{
	NetListen *pSafeSendTestListen;
	NetLink *pLink;
	Packet *pPak;

	test1.pSubStruct = &innerTest1;


	while (!(pSafeSendTestListen = commListen(commDefault(),LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH,7000,
			safeSendTestHandleMsg, NULL, NULL, 0)))
	{
		Sleep(1);
	}

	pLink = commConnectWait(commDefault(), LINKTYPE_SHARD_NONCRITICAL_20MEG, LINK_FORCE_FLUSH, "localhost", 7000, NULL, NULL, NULL, 0, 5.0f);

	pPak = pktCreate(pLink, LAUNCHERQUERY_VERBOSEPROCLISTLOGGING);
	ParserSendStructSafe(parse_SafeSendTest1, pPak, &test1);
	pktSend(&pPak);

	linkFlush(pLink);

	while (1)
	{
		commMonitor(commDefault());
		utilitiesLibOncePerFrame(1,1);
		Sleep(1);
	}
	
}
*/
/*
AUTO_ENUM;
typedef enum HappyMoods
{
	MOOD_HAPPY,
	MOOD_JOYFUL,
	MOOD_GRINNING,

	MOOD_LAST, EIGNORE
} HappyMoods;

AUTO_ENUM AEN_APPEND_OTHER_TO_ME(HappyMoods);
typedef enum OtherHappyMoods
{
	MOOD_SILLY = MOOD_LAST,
	MOOD_YAPPY,
	MOOD_WACKY,
} OtherHappyMoods;

AUTO_RUN;
void enumAppendTest(void)
{
	char **ppKeys = NULL;
	int *piVals = NULL;
	int i;

	DefineFillAllKeysAndValues(HappyMoodsEnum, &ppKeys, &piVals);

	for (i=0; i < eaSize(&ppKeys); i++)
	{
		printf("%d (%d) : %s (%s)\n", piVals[i], StaticDefineIntGetInt(HappyMoodsEnum, ppKeys[i]), ppKeys[i], StaticDefineIntRevLookup(HappyMoodsEnum, piVals[i]));
	}

	eaDestroy(&ppKeys);
	ea32Destroy(&piVals);


	DefineFillAllKeysAndValues(OtherHappyMoodsEnum, &ppKeys, &piVals);

	for (i=0; i < eaSize(&ppKeys); i++)
	{
		printf("%d (%d) : %s (%s)\n", piVals[i], StaticDefineIntGetInt(OtherHappyMoodsEnum, ppKeys[i]), ppKeys[i], StaticDefineIntRevLookup(OtherHappyMoodsEnum, piVals[i]));
	}

	eaDestroy(&ppKeys);
	ea32Destroy(&piVals);


}*/



AUTO_STRUCT;
typedef struct testwfnames
{
                const char **image_name_list;                                AST( FILENAME )
} testwfnames;

/*
AUTO_STRUCT AST_STARTTOK("") AST_ENDTOK("\n");
typedef struct badInnerStruct
{
	int x; AST(STRUCTPARAM)
	int y; AST(STRUCTPARAM)
	int z;
} badInnerStruct;

AUTO_STRUCT;
typedef struct badOuterStruct
{
	badInnerStruct *pInner1;
	badInnerStruct inner2;
	badInnerStruct **ppInner3;
} badOuterStruct;

badInnerStruct *badInnerCreate(int x)
{
	badInnerStruct *pRetVal = StructCreate(parse_badInnerStruct);
	pRetVal->x = pRetVal->y = pRetVal->z = x;
	return pRetVal;
}


AUTO_RUN;
void badStringTest(void)
{
	badOuterStruct *pOuter1 = StructCreate(parse_badOuterStruct);
	badOuterStruct *pOuter2 = StructCreate(parse_badOuterStruct);
	char *pStr = NULL;

	pOuter1->pInner1 = badInnerCreate(1);
	StructCopy(parse_badInnerStruct, badInnerCreate(2), &pOuter1->inner2, 0, 0, 0);
	eaPush(&pOuter1->ppInner3, badInnerCreate(3));
	eaPush(&pOuter1->ppInner3, badInnerCreate(4));

	ParserWriteText(&pStr, parse_badOuterStruct, pOuter1, 0, 0, 0);
	ParserReadText(pStr, parse_badOuterStruct, pOuter2, 0);

	{
		int iBrk = 0;
	}
}
*/
/*
#include "IntFIFO.h"
#include "rand.h"

AUTO_RUN;
void PointerFIFOTest(void)
{
	int iCount;
	int i;
	PointerFIFO *pFIFO = PointerFIFO_Create(1);

	for (iCount = 100; iCount < 5000; iCount++)
	{
		int iNumPushed = 0;
		int iNumRead = 0;
		void *pTemp;

		for (i = 0; i < iCount; i++)
		{
			pTemp = (void*)((intptr_t)i);
			PointerFIFO_Push(pFIFO, pTemp);
			iNumPushed++;

			while (iNumRead < iNumPushed && randomPositiveF32() < 0.33f)
			{	
				assert(PointerFIFO_Get(pFIFO, &pTemp));
				assert(pTemp == ((void*)((intptr_t)iNumRead)));
				iNumRead++;
			}
		}

		while (iNumRead < iNumPushed)
		{
			assert(PointerFIFO_Get(pFIFO, &pTemp));
			assert(pTemp == ((void*)((intptr_t)iNumRead)));			
			iNumRead++;
		}

		assert(!PointerFIFO_Get(pFIFO, &pTemp));
	}
}
*/

/*
AUTO_ENUM;
typedef enum emotionTest
{
	FOO_HAPPY = 1,
	FOO_SAD = 4,
	FOO_ANGRY = 11,
} emotionTest;

AUTO_RUN;
void perfTestThing(void)
{
	int i;
	for (i = -2; i < 15; i++)
	{
		const char *pTempChar;

		printf("Trying with i = %d\n", i);

		COARSE_AUTO_START_STATIC_DEFINE(pTempChar, i, emotionTestEnum);
		COARSE_AUTO_STOP_STATIC_DEFINE(pTempChar);
		printf("tempchar is %s\n", pTempChar);
	}

	{
		int iBrk = 0;
	}
}
*/
/*
AUTO_RUN;
void estrBuffTest(void)
{
	char buf1[89];
	char buf2[89];
	char buf3[89];
	char buf4[89];

	char *pEstr1;
	char *pEstr2;
	char *pEstr3;
	char *pEstr4;

	int i;

	memset(buf1, '$', 89);
	memset(buf2, '$', 89);
	memset(buf3, '$', 89);
	memset(buf4, '$', 89);
	estrBufferCreate(&pEstr1, buf1, 88);
	estrBufferCreate(&pEstr2, buf2, 88);
	estrBufferCreate(&pEstr3, buf3, 88);
	estrBufferCreate(&pEstr4, buf4, 88);

	for (i = 0; i < 50; i++)
	{
		estrConcatChar(&pEstr1, 'A');
		assert(strlen(pEstr1) == estrLength(&pEstr1));
	}

	for (i = 0; i < 100; i++)
	{
		estrConcatChar(&pEstr2, 'A');
		assert(strlen(pEstr2) == estrLength(&pEstr2));
	}

	for (i = 0; i < 100; i++)
	{
		estrConcatChar(&pEstr3, 'A');
		assert(strlen(pEstr3) == estrLength(&pEstr3));
	}

	for (i = 0; i < 100; i++)
	{
		estrConcatChar(&pEstr4, 'A');
		assert(strlen(pEstr4) == estrLength(&pEstr4));
	}

	estrDestroy(&pEstr1);
	estrDestroy(&pEstr2);
	estrDestroy(&pEstr3);
	estrDestroy(&pEstr4);

	assert(buf1[88] == '$');
	assert(buf2[88] == '$');
	assert(buf3[88] == '$');
	assert(buf4[88] == '$');

}
*/

/*

AUTO_STRUCT AST_IGNORE(y) AST_IGNORE(foo);
typedef struct IgnoreTestStruct
{
	int x;
	//int y;
//	char *pFoo;
} IgnoreTestStruct;

AUTO_STRUCT;
typedef struct IgnoreTestStructs
{
	IgnoreTestStruct **ppTestStructs;
} IgnoreTestStructs;

AUTO_FIXUPFUNC;
TextParserResult IgnoreTestStruct_ParserFixup(IgnoreTestStruct *pStruct, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_HERE_IS_IGNORED_FIELD:
		{
			SpecialIgnoredField *pField = (SpecialIgnoredField*)pExtraData;

			{
				int iBrk = 0;
			}
		}
	}

	return 1;
}
			



AUTO_RUN_FILE;
void IgnoreTestYay(void)
{
	IgnoreTestStructs *pTestStructs = StructCreate(parse_IgnoreTestStructs);

	ParserSetWantSpecialIgnoredFieldCallbacks(parse_IgnoreTestStruct, "foo");
	ParserSetWantSpecialIgnoredFieldCallbacks(parse_IgnoreTestStruct, "y");


	ParserReadTextFile("c:\\temp\\IgnoreTest.txt", parse_IgnoreTestStructs, pTestStructs, 0);

	{
		int iBrk = 0;
	}
}*/

#if 0
#include "Regex.h"
#include "rand.h"

AUTO_RUN;
void RegExTest(void)
{
	bool bRet = RegExSimpleMatch("test/ui/foobee/doobee", ".*/ui/.*");
	bRet = RegExSimpleMatch("bummbbuyu", ".*/ui/.*");

}
#endif

#if 0


void assertCmpMem(char *pBlock1, char *pBlock2, int iSize)
{
	int i;
	for (i = 0; i < iSize; i++)
	{
		assert(pBlock1[i] == pBlock2[i]);
	}
}

void TPBBTestBlock(char *pRAM1, int iSize1)
{
	int iSize2;
	TextParserBinaryBlock *pBlock = TextParserBinaryBlock_CreateFromMemory(pRAM1, iSize1, false);
	char *pRAM2 = TextParserBinaryBlock_PutIntoMallocedBuffer(pBlock, &iSize2);
	assert(iSize1 == iSize2);
	assertCmpMem(pRAM1, pRAM2, iSize1);

	free(pRAM2);
	StructDestroy(parse_TextParserBinaryBlock, pBlock);

	pBlock = TextParserBinaryBlock_CreateFromMemory(pRAM1, iSize1, true);
	pRAM2 = TextParserBinaryBlock_PutIntoMallocedBuffer(pBlock, &iSize2);
	assert(iSize1 == iSize2);
	assertCmpMem(pRAM1, pRAM2, iSize1);

	free(pRAM2);
	StructDestroy(parse_TextParserBinaryBlock, pBlock);

	pBlock = TextParserBinaryBlock_CreateFromMemory(pRAM1, iSize1, false);
	TextParserBinaryBlock_PutIntoFile(pBlock, "c:\\temp\\tpbb.bin");
	StructDestroy(parse_TextParserBinaryBlock, pBlock);

	pBlock = TextParserBinaryBlock_CreateFromFile("c:\\temp\\tpbb.bin", false);
	pRAM2 = TextParserBinaryBlock_PutIntoMallocedBuffer(pBlock, &iSize2);
	assert(iSize1 == iSize2);
	assertCmpMem(pRAM1, pRAM2, iSize1);

	free(pRAM2);
	StructDestroy(parse_TextParserBinaryBlock, pBlock);
}

#include "rand.h"

AUTO_RUN_FILE;
void TPBBTest(void)
{
	int i, iSize;
	char *pBuf;
	Packet *pPak;
	TextParserBinaryBlock *pBlock;

	for (iSize = 10; iSize < 10000; iSize += 57)
	{
		pBuf = malloc(iSize);
		for (i = 0; i < iSize; i++)
		{
			pBuf[i] = randomIntRange(0, 255);
		}

		TPBBTestBlock(pBuf, iSize);

		free(pBuf);
	}

	iSize = 5000000;
	pBuf = malloc(iSize);
	for (i = 0; i < iSize; i++)
	{
		pBuf[i] = randomIntRange(0, 255);
	}

	pBlock = TextParserBinaryBlock_CreateFromMemory(pBuf, iSize, false);

	pPak = pktCreateTemp(NULL);
	pktSetHasVerify(pPak, false);

	pktSendStruct(pPak, pBlock, parse_TextParserBinaryBlock);

	pktFree(&pPak);

	ParserWriteBinaryFile("c:\\temp\\test.bin", NULL, parse_TextParserBinaryBlock, pBlock, NULL, NULL, NULL, 0, 0, NULL, 0); 

	StructDestroy(parse_TextParserBinaryBlock, pBlock);
	pBlock = TextParserBinaryBlock_CreateFromFile("c:\\temp\\test.bin", false);
}

#endif
/*
#define FNAME1 "c:/temp/timing.txt"
#define FNAME2 "c:/night/data/bin/alignments.bin"



AUTO_RUN_FILE;
void fileTimeTest(void)
{
	FILE *pOutFile = fopen(FNAME1, "wt");
	S64 iFileTime1, iFileTime2;
	fprintf(pOutFile, "hooey\n");
	fclose(pOutFile);
	iFileTime1 = fileLastChangedSS2000AltStat(FNAME1);

	pOutFile = fopen(FNAME2, "wt");
	fprintf(pOutFile, "hooey\n");
	fclose(pOutFile);
	iFileTime2 = fileLastChangedSS2000AltStat(FNAME2);

	printf("%s is %d seconds old\n", FNAME1, (int)((S64)timeSecondsSince2000_ForceRecalc() - iFileTime1));
	printf("%s is %d seconds old\n", FNAME2, (int)((S64)timeSecondsSince2000_ForceRecalc() - iFileTime2));

}*/
	/*
AUTO_RUN;
void eatest(void)
{
	int *piTestInts = NULL;
	int i;

	for (i = 0; i < 30000; i++)
	{
		ea32Push(&piTestInts, i);
		assert(ea32Size(&piTestInts) == i + 1);
		assert(piTestInts[i] == i);
	}

	for (i = 30000; i > 2; i--)
	{
		assert(piTestInts[i-1] == i-1);
		ea32SetCapacity(&piTestInts, i - 1);
		assert(piTestInts[i-2] == i-2);
	}
}
*/
/*
#include "ScratchStack.h"
AUTO_RUN;
void ScratchStackReallocTest(void)
{
	void *pBuf = ScratchAlloc(100);
	void *pBuf2;
	bool bSuccess = ScratchPerThreadReAllocInPlaceIfPossible(pBuf, 200);
	bSuccess = ScratchPerThreadReAllocInPlaceIfPossible(pBuf, 200000000);
	pBuf2 = ScratchAlloc(100);
	bSuccess = ScratchPerThreadReAllocInPlaceIfPossible(pBuf, 300);
	bSuccess = ScratchPerThreadReAllocInPlaceIfPossible(pBuf2, 300);
}
*/
/*
AUTO_RUN;
void ScratchEstringTest(void)
{
	char *pTemp = NULL;
	int i;
	estrStackCreate(&pTemp);

	for (i = 0; i < 1000000; i++)
	{
		estrConcatf(&pTemp, "%d ", i);
	}
}
*/

static int *pEarrays[2048] = {0};

AUTO_COMMAND;
void TestPushMany(int iCount)
{
	int i, j;

	for (i = 0; i < iCount; i++)
	{
		for (j = 0; j < ARRAY_SIZE(pEarrays); j++)
		{
			ea32Push(&(pEarrays[j]), i);
		}
	}
}

AUTO_COMMAND;
void TestPopMany(int iCount)
{
	int j;

	for (j = 0; j < ARRAY_SIZE(pEarrays); j++)
	{
		int iSize = ea32Size(&pEarrays[j]);
		int iNewSize = iSize - iCount;
		if (iNewSize < 0)
		{
			iNewSize = 0;
		}

		ea32SetCapacity(&pEarrays[j], iNewSize);
	}
	
}

#include "rand.h"


/*
AUTO_STRUCT;
typedef struct sptestinner
{
	int x; AST(STRUCTPARAM)
	int y; AST(STRUCTPARAM)
	float f; AST(STRUCTPARAM)
	char *pStr; AST(STRUCTPARAM POOL_STRING)
	int z; AST(STRUCTPARAM)
	int w;
} sptestinner;

AUTO_STRUCT;
typedef struct sptestouter
{
	sptestinner **ppInners;
} sptestouter;

sptestinner *createInner(int x, int y, float f, char *pStr, int z, int w)
{
	sptestinner *pInner = StructCreate(parse_sptestinner);
	pInner->x = x;
	pInner->y = y;
	pInner->z = z;
	pInner->w = w;
	pInner->pStr = pStr;
	pInner->f = f;

	return pInner;
}

AUTO_RUN;
void sptest(void)
{
	sptestouter *pOuter = StructCreate(parse_sptestouter);
	sptestinner *pInner = createInner(0, 0, 0, NULL, 0, 0);
	char *pStr = NULL;

	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(1, 0, 0, NULL, 0, 0);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 1, 0, NULL, 0, 0);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 0, 1, NULL, 0, 0);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 0, 0, "hi there", 0, 0);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 0, 0, NULL, 1, 0);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 0, 0, NULL, 0, 1);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(1, 0, 0, NULL, 0, 1);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 1, 0, NULL, 0, 1);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 0, 1, NULL, 0, 1);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 0, 0, "foo", 0, 1);
	eaPush(&pOuter->ppInners, pInner);

	pInner = createInner(0, 0, 0, NULL, 1, 1);
	eaPush(&pOuter->ppInners, pInner);

	ParserWriteText(&pStr, parse_sptestouter, pOuter, 0, 0, 0);

	pStr = pStr;


}
*/
/*
#include "ThreadSafeMemoryPool.h"
#include "ThreadManager.h"

typedef struct TestWidget
{
	int x;
	int y;
} TestWidget;

TSMP_DEFINE(TestWidget);

static int iDoneCount = 0;

static DWORD WINAPI TSMPTest_Thread(LPVOID lpParam)
{
	TestWidget **ppWidgets = NULL;
	int i;
	int j;

	for (j = 0; j < 100000; j++)
	{
		int iNumToCreate = randomIntRange(50,100);
		int iNumToDestroy = randomIntRange(50,100);

		for (i = 0; i < iNumToCreate; i++)
		{
			if (eaSize(&ppWidgets) < 200)
			{
				TestWidget *pWidget = TSMP_ALLOC(TestWidget);
				eaPush(&ppWidgets, pWidget);
			}
		}

		for (i = 0; i < iNumToDestroy; i++)
		{
			if (eaSize(&ppWidgets))
			{
				TestWidget *pWidget = eaPop(&ppWidgets);
				TSMP_FREE(TestWidget, pWidget);
			}
		}

		threadSafeMemoryPoolCompact( &tsmemPoolTestWidget);
	}

	while (eaSize(&ppWidgets))
	{
		TestWidget *pWidget = eaPop(&ppWidgets);
		TSMP_FREE(TestWidget, pWidget);
	}


	InterlockedIncrement(&iDoneCount);
	return 0;
}

AUTO_RUN_LATE;
void TSMPTest(void)
{
	TestWidget **ppWidgets = NULL;
	int i;


	TSMP_CREATE(TestWidget, 10);

	for (i = 0; i < 10; i++)
	{
		ManagedThread *pThread = tmCreateThreadEx(TSMPTest_Thread, (void*)((INT_PTR)i), 256 * 1024, 0);

	}

	while (iDoneCount < 10)
	{ Sleep(1); }

	threadSafeMemoryPoolCompact( &tsmemPoolTestWidget);

}*/

/*
AUTO_RUN;
void allocfreetest(void)
{
	char *pBuf = malloc(20000);
	free(pBuf);
}*/
/*
#include "referencesystem.h"
#include "message.h"

AUTO_ENUM;
typedef enum enumFlagTest
{
	FLAG_HAPPY = 1 << 0,
	FLAG_SAD = 1 << 1,
	FLAG_DOOPY = 1 << 2,
	FLAG_ANGRY = 1 << 3,
} enumFlagTest;

AUTO_STRUCT;
typedef struct JsonTest
{
	int x;
	char *pStr;

	enumFlagTest mood; AST(FLAGS)
	REF_TO(Message) hMessage;
} JsonTest;

AUTO_RUN_LATE;
void jsonTestFunc(void)
{
	JsonTest *pStruct = StructCreate(parse_JsonTest);
	char *pOutStr=  NULL;
	
	pStruct->x = 3;
	pStruct->pStr = strdup("Hi there\n");
	pStruct->mood = FLAG_HAPPY | FLAG_ANGRY;
	SET_HANDLE_FROM_REFDATA("Message", "ThisIsNotAMessage", pStruct->hMessage);

	ParserWriteJSON(&pOutStr, parse_JsonTest, pStruct, 0,
		0, 0);

	{
		int iBrk = 0;
	}


}

#include "objPath.h"


AUTO_STRUCT;
typedef struct XpathChild1
{
	int foo;
	int bar;
	
} XpathChild1;

AUTO_STRUCT;
typedef struct XpathChild2
{
	int wakka;
	int wappa;
} XpathChild2;

AUTO_STRUCT;
typedef struct XpathParent
{
	int mubba;
	int wubba;
	XpathChild1 *pChild1; AST(ALWAYS_ALLOC FORMATSTRING(XPATH_FLATTEN_INTO_PARENT=1))
	XpathChild2 *pChild2; AST(ALWAYS_ALLOC FORMATSTRING(XPATH_FLATTEN_INTO_PARENT=1))
} XpathParent;

AUTO_STRUCT;
typedef struct XpathGrandParent
{
	int hoppa;
	int woppa;
	XpathParent *pParent;
} XpathGrandParent;

AUTO_RUN;
void XpathTest(void)
{
	ParseTable *pTable;
	int iColumn;
	int iIndex;
	void *pStructPtr;
	bool bResult;
	XpathGrandParent *pGrandParent = StructCreate(parse_XpathGrandParent);

	pGrandParent->pParent = StructCreate(parse_XpathParent);
	bResult = objPathResolveField(".parent.foo", parse_XpathGrandParent, pGrandParent, &pTable, &iColumn, &pStructPtr, &iIndex, 0);
	bResult = objPathResolveField(".parent.mubba", parse_XpathGrandParent, pGrandParent, &pTable, &iColumn, &pStructPtr, &iIndex, 0);
	bResult = objPathResolveField(".parent.wappa", parse_XpathGrandParent, pGrandParent, &pTable, &iColumn, &pStructPtr, &iIndex, 0);
}


*/
/*
AUTO_STRUCT;
typedef struct SafeSendTest1
{
	int x;
	char str1[32];
	int y;
	char str3[100];
} SafeSendTest1;

AUTO_STRUCT;
typedef struct SafeSendTest2
{
	char str1[24];
	int y;
	char str4[100]; AST(NAME("str3"))
	int z;
	char str2[60];
} SafeSendTest2;

AUTO_RUN;
void SafeSendTest(void)
{
	SafeSendTest1 test1 = { 5, "hi", 7, "Wabbaoaoaafd"};
	SafeSendTest2 test2 = { 0 };

	Packet *pPak = pktCreateTemp(NULL);

	ParserSendStructAsCheckedNameValuePairs(pPak, parse_SafeSendTest1, &test1);

	ParserReceiveStructAsCheckedNameValuePairs(pPak, parse_SafeSendTest2, &test2);

	{
		int iBrk = 0;
	}

}*/
/*
#include "crypt.h"

AUTO_RUN;
void AESTest(void)
{
	char *pKey = "1234567890123456789012345678901234567890";
	char *pData = "Hi there this is a fun happy test hurray";

	int iEncodedSize;
	void *pEncoded = AESEncode(pKey, 40, pData, strlen(pData) + 1, &iEncodedSize);
	char *pDecoded;
	int iDecodedSize;
	assert(pEncoded);

	pDecoded = AESDecode(pKey, 40, pEncoded, iEncodedSize, &iDecodedSize);

	{
		int iBrk = 0;
	}
}
*/

#include "Expression.h"

AUTO_EXPR_FUNC(util);
int manyArgs12(int x1, int x2, int x3, int x4, int x5, int x6, int x7, int x8, int x9, int x10, int x11, int x12)
{
	return 7;
}

AUTO_EXPR_FUNC(util);
int manyArgs11(int x1, int x2, int x3, int x4, int x5, int x6, int x7, int x8, int x9, int x10, int x11)
{
	return 7;
}


AUTO_COMMAND;
void manyArgsTest(void)
{
	int iRet = exprEvaluateRawString("manyArgs11(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11)");
	iRet = exprEvaluateRawString("manyArgs12(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12)");
}



#include "../../libs/patchclientlib/pcl_client_wt.h"
#include "../../libs/patchclientlib/pcl_client.h"

void testVersionsCB(PatchVersionInfo **ppVersions, PCL_ErrorCode error, char *pErrorDetails, void *pUserData)
{
	int iBrk = 0;



}


AUTO_COMMAND;
void GetVersionsTest(void)
{
	ThreadedPCL_GetPatchVersions("PatchMaster", "ShardLauncher", testVersionsCB, NULL);
}

void testGetFileIntoRAMCB(void *pFileData, int iFileSize, PCL_ErrorCode error, char *pErrorDetails, void *pUserData)
{
	int iBrk = 0;
}

AUTO_COMMAND;
void GetFileTest(void)
{
	ThreadedPCL_GetFileIntoRAM(PCL_DEFAULT_PATCHSERVER, "StarTrekServer", "ST_25_20120605_0352", "data/server/ShardLauncher_ConfigOptions.txt.foo", testGetFileIntoRAMCB, NULL);
}
/*
AUTO_STRUCT;
typedef struct ArrayTest
{
	Mat3 testMatrix; AST(AS_MATRIX)
	Mat4 testMat4; AST(AS_MATRIX)
} ArrayTest;
*/
/*
#define TYPE_parse_ArrayTest ArrayTest
ParseTable parse_ArrayTest[] =
{
	{ "ArrayTest", 	TOK_IGNORE | TOK_PARSETABLE_INFO, sizeof(ArrayTest), 0, NULL, 0, NULL },
	{ "{",			TOK_START, 0 },
	{ "rot_testArray1", TOK_REDUNDANTNAME | TOK_MAT4PYR_ROT(ArrayTest, testArray1), NULL },
	{ "pos_testArray1",TOK_REDUNDANTNAME | TOK_MAT4PYR_POS(ArrayTest, testArray1), NULL },
	{ "rot_testArray2",TOK_MAT4PYR_ROT(ArrayTest, testArray2), NULL },
	{ "pos_testArray2",TOK_MAT4PYR_POS(ArrayTest, testArray2), NULL },
	{ "testArray1",	TOK_FIXED_ARRAY | TOK_F32_X, offsetof(ArrayTest, testArray1),  12, NULL },
	{ "}",			TOK_END, 0 },
	{ "", 0, 0 }
};*/
/*
#include "ScratchStack.h"
#include "ThreadManager.h"


static DWORD WINAPI testScratchThread(LPVOID lpParam)
{
	void *pFoo = ScratchAlloc(2000000);
	ScratchFree(pFoo);
	return 0;
}


AUTO_RUN_FILE;
void arrayTestFunc(void)
{
	ArrayTest *pArrayTest;

	pArrayTest = StructCreate(parse_ArrayTest);
	
	
	ParserReadTextFile("c:\\temp\\test.txt", parse_ArrayTest, pArrayTest, 0);
	ParserWriteTextFile("c:\\temp\\test2.txt", parse_ArrayTest, pArrayTest, 0, 0);
	ParserReadTextFile("c:\\temp\\test2.txt", parse_ArrayTest, pArrayTest, 0);
	ParserWriteTextFile("c:\\temp\\test3.txt", parse_ArrayTest, pArrayTest, 0, 0);

	assert(tmCreateThread(testScratchThread, (void*)0x12345));


}*/
/*
#include "crypt.h"

#define MIN_DATA_LENGTH 1
#define MAX_DATA_LENGTH 5000

void RandomizeMemory(char *pBuf, int iSize)
{
	int i;

	for (i = 0; i < iSize; i++)
	{
		pBuf[i] = randomIntRange(0, 255);
	}
}


AUTO_RUN;
void EncryptDecryptTest(void)
{
	int iDataLen;

	char emptyKey[32] = "";
	char key[32];
	char wrongKey[32];

	for (iDataLen = MIN_DATA_LENGTH; iDataLen < MAX_DATA_LENGTH; iDataLen++)
	{
		char *pData = malloc(iDataLen);
		char *pDecoded = malloc(iDataLen);
		int iEncryptedBufSize = AESEncode_GetEncodeBufferSizeFromDataSize(iDataLen);
		char *pEncoded = malloc(iEncryptedBufSize);
		int iRet;

		RandomizeMemory(pData, iDataLen);
		RandomizeMemory(key, 32);
		
		do
		{
			RandomizeMemory(wrongKey, 32);
		}
		while (memcmp(key, wrongKey, 32) == 0);

		iRet = AESEncodeIntoBuffer(key, pData, iDataLen, pEncoded, iEncryptedBufSize);
		assert(iRet);

		iRet = AESDecodeIntoBuffer(key, pEncoded, iEncryptedBufSize, pDecoded, iDataLen);
		assert(iRet == iDataLen && memcmp(pData, pDecoded, iDataLen) == 0);

		iRet = AESDecodeIntoBuffer(wrongKey, pEncoded, iEncryptedBufSize, pDecoded, iDataLen);
		assert(iRet == 0 || memcmp(pData, pDecoded, iDataLen) != 0);

		iRet = AESDecodeIntoBuffer(emptyKey, pEncoded, iEncryptedBufSize, pDecoded, iDataLen);
		assert(iRet == 0 || memcmp(pData, pDecoded, iDataLen) != 0);

		free(pData);
		free(pDecoded);
		free(pEncoded);
	}
}

*/


#if 0 //some test code for Alex's managed file send/receive stuff

static void HandleMsg(Packet* pak, int cmd, NetLink* link, void *pUserData)
{
	if (linkFileSendingMode_ReceiveHelper(link, cmd, pak)) return;

	printf("Got command %d\n", cmd);

}

static void ErrorCB(char *pError)
{
	printf("ERROR: %s\n", pError);
}

void ReceiveCB(int iCmd, char *pFileName)
{
	printf("Cmd %d: file %s received\n", iCmd, pFileName);
}

static void ConnectCB(NetLink* link,void *pUserData)
{
	linkFileSendingMode_InitReceiving(link, ErrorCB);
	linkFileSendingMode_RegisterCallback(link, 50, "c:\\temp2", ReceiveCB);
	printf("A file receiving link was established!\n");
}


AUTO_COMMAND;
void ReceiveTest(void)
{
	if (commListenBoth(commDefault(), LINKTYPE_UNSPEC,  LINK_FORCE_FLUSH, 7900, HandleMsg, ConnectCB, NULL, 0, NULL, NULL))
	{
		printf("SUCCESS! Listening for files\n");
	}
	else
	{
		printf("FAILURE!\n");
	}
}

static NetLink *pSendLink = NULL;

void testTickFunc(void)
{
	linkFileSendingMode_Tick(pSendLink);
}

void pErrorCB(char *pFileName, char *pMessage, void *pUserData)
{
	printf("ERROR while sending %s: %s", pFileName, pMessage);
}

void pSuccessCB(char *pFileName, char *pMessage, void *pUserData)
{
	printf("SUCCESS: Send file %s", pFileName);
}

AUTO_COMMAND;
void SendTest(char *pFileName)
{
	char fullFileName[CRYPTIC_MAX_PATH];
	Packet *pSuccessPacket;
	sprintf(fullFileName, "c:\\temp\\%s", pFileName);
	if (!pSendLink)
	{
		pSendLink = commConnectWait(commDefault(), LINKTYPE_UNSPEC, LINK_FORCE_FLUSH, "localhost", 7900, NULL, NULL, NULL, 0, 10);
		if (!pSendLink)
		{
			printf("ERROR! Couldn't connect for file sending\n");
			return;
		}

		linkFileSendingMode_InitSending(pSendLink);
		UtilitiesLib_AddExtraTickFunction(testTickFunc);
		printf("SUCCESS! Connected for file sending\n");
	}

	pSuccessPacket = pktCreate(pSendLink, 51);
	pktSendBits(pSuccessPacket, 32, 10);
	linkFileSendingMode_SendManagedFile(pSendLink, 50, fullFileName, pFileName, 0, pErrorCB, NULL, pSuccessCB, NULL, pSuccessPacket);
}

#endif


#include "mcp_testbed_c_ast.c"
