/***************************************************************************



***************************************************************************/

#include "earray.h"
#include "EString.h"
#include "file.h"
#include "rand.h"
#include "StashTable.h"
#include "TestHarness.h"
#include "timing.h"
#include "utils.h"
#include "utils/Stackwalk.h"
#include "wininclude.h"
#include "WorkerThread.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

void testResetCase(TestCase *test)
{
	int i;

	for (i = 0; i < eaSize(&test->ppChildren); i++)
	{
		testResetCase(test->ppChildren[i]);
	}

	if (test->estrReturnMessage)
	{
		estrClear(&test->estrReturnMessage);
	}
	if (test->estrStackTrace)
	{
		estrClear(&test->estrStackTrace);
	}
	if (test->estrExpr)
	{
		estrClear(&test->estrExpr);
	}
	test->testStatus = TEST_UNKNOWN;
	test->iCurChildNum = -1;


}

TestHarness gTestHarness = {0};

// Register a test with the test harness
void testRegister(const char *testName, const char *parentName, TestCaseCB run, TestCaseCB setup, TestCaseCB teardown)
{
	TestCase *newCase;
	assertmsg(!gTestHarness.pCurCaseBeingTested,"You can't register new tests while a test is running!");
	if (!gTestHarness.testLookup)
	{
		gTestHarness.testLookup = stashTableCreateWithStringKeys(100, StashDefault);
	}
	if (stashFindPointer(gTestHarness.testLookup,testName,&newCase))
	{
		assertmsgf(0,"You can't register two tests named %s",testName);
	}
	newCase = calloc(sizeof(TestCase),1);
	strcpy(newCase->testName,testName);
	newCase->setUpFunc = setup;
	newCase->runFunc = run;
	newCase->tearDownFunc = teardown;
	stashAddPointer(gTestHarness.testLookup,testName,newCase,false);
	eaPush(&gTestHarness.testCases,newCase);

	if (parentName)
	{
		TestCase *pParent = testFindCase(parentName);

		assertmsgf(pParent, "Parent test case %s unrecognized\n", parentName);
	
		newCase->pParent = pParent;

		eaPush(&pParent->ppChildren, newCase);

		assertmsgf(!(pParent->setUpFunc && newCase->setUpFunc || pParent->tearDownFunc && newCase->tearDownFunc),
			"Conflict between test case %s and parent %s... they can't both have setup/teardown funcs", testName, parentName);

		if (pParent->setUpFunc)
		{
			newCase->setUpFunc = pParent->setUpFunc;
		}

		if (pParent->tearDownFunc)
		{
			newCase->tearDownFunc = pParent->tearDownFunc;
		}
	}
}

TestCase *testFindCase(const char *name)
{
	TestCase *test;
	if (stashFindPointer(gTestHarness.testLookup,name,&test))
	{
		return test;
	}
	return NULL;
}

// Fail a test, and give the resulting error message
int testFailFunc(const char *expr, const char *message,const char* filename, unsigned lineno)
{
	static char stackBuffer[10000];

	assertmsg(gTestHarness.pCurCaseBeingTested, "testFailFunc called at a bad time");
	

	if (gTestHarness.pCurCaseBeingTested->testStatus == TEST_FAILED)
	{
		// We already failed
		return 0;
	}

	estrConcatf(&gTestHarness.pCurCaseBeingTested->estrReturnMessage,"%s",message);

	estrAppend2(&gTestHarness.pCurCaseBeingTested->estrExpr,expr);
	strcpy(gTestHarness.pCurCaseBeingTested->errorFile,filename);
	gTestHarness.pCurCaseBeingTested->errorLine = lineno;
	if (gTestHarness.pCurCaseBeingTested->testStatus == TEST_SETUP)
	{
		superassertf(expr,"FATAL ERROR: Test %s failed with message %s during setup step",false,filename,lineno,gTestHarness.pCurCaseBeingTested->testName,gTestHarness.pCurCaseBeingTested->estrReturnMessage);
	}
	else if (gTestHarness.pCurCaseBeingTested->testStatus != TEST_RUNNING)
	{
		superassertf(expr,"FATAL ERROR: Test %s failed with message %s during teardown step",false,filename,lineno,gTestHarness.pCurCaseBeingTested->testName,gTestHarness.pCurCaseBeingTested->estrReturnMessage);
	}

	// Do something about stack traces here
	gTestHarness.pCurCaseBeingTested->testStatus = TEST_FAILED;

	stackWalkDumpStackToBuffer(SAFESTR(stackBuffer), NULL, NULL, NULL, NULL);
	estrConcatf(&gTestHarness.pCurCaseBeingTested->estrStackTrace,"%s",stackBuffer);

	return 0;
}
int testFailFuncf(const char *expr,const char *messageformat,const char* filename, unsigned lineno,...)
{

	va_list va;
	static char stackBuffer[10000];

	assertmsg(gTestHarness.pCurCaseBeingTested, "testFailFuncf called at a bad time");


	if (gTestHarness.pCurCaseBeingTested->testStatus == TEST_FAILED)
	{
		// We already failed
		return 0;
	}

	va_start( va, lineno );
	estrConcatfv(&gTestHarness.pCurCaseBeingTested->estrReturnMessage,messageformat,va);
	va_end( va );

	estrAppend2(&gTestHarness.pCurCaseBeingTested->estrExpr,expr);
	strcpy(gTestHarness.pCurCaseBeingTested->errorFile,filename);
	gTestHarness.pCurCaseBeingTested->errorLine = lineno;
	if (gTestHarness.pCurCaseBeingTested->testStatus == TEST_SETUP)
	{
		superassertf(expr,"FATAL ERROR: Test %s failed with message %s during setup step",false,filename,lineno,gTestHarness.pCurCaseBeingTested->testName,gTestHarness.pCurCaseBeingTested->estrReturnMessage);
	}
	else if (gTestHarness.pCurCaseBeingTested->testStatus != TEST_RUNNING)
	{
		superassertf(expr,"FATAL ERROR: Test %s failed with message %s during teardown step",false,filename,lineno,gTestHarness.pCurCaseBeingTested->testName,gTestHarness.pCurCaseBeingTested->estrReturnMessage);
	}

	// Do something about stack traces here
	gTestHarness.pCurCaseBeingTested->testStatus = TEST_FAILED;

	stackWalkDumpStackToBuffer(SAFESTR(stackBuffer), NULL, NULL, NULL, NULL);
	estrConcatf(&gTestHarness.pCurCaseBeingTested->estrStackTrace,"%s",stackBuffer);

	return 0;
}

char *GetTabString(int iLength)
{
	int i;
	static char retString[12];
	if (iLength > 11)
	{
		iLength = 11;
	}


	for (i=0; i < iLength; i++)
	{
		retString[i] = '\t';
	}

	retString[i] = 0;

	return retString;
}

enum TestThreadCmdMsg
{
	TestThreadCmdMsg_Run = WT_CMD_USER_START,
	TestThreadCmdMsg_RunFinished,
};

typedef struct TestThreadRunParameters
{
	int seq;					// Test sequence number
	TestCaseCB func;			// Test function to run
	const char *name;			// Name of test
} TestThreadRunParameters;

typedef struct TestThreadFinishedStatus
{
	int seq;					// Test sequence number
} TestThreadFinishedStatus;

static void thread_RunTest(void *user_data, void *data, WTCmdPacket *packet)
{
	TestThreadRunParameters *run = data;
	TestThreadFinishedStatus done = {0};

	// Run test.
	__try 
	{
		run->func();
	}
#pragma warning(suppress:6320)		//Exception-filter is the constant...
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		ErrorfForceCallstack("%s: Exception caught: %s", run->name, GetExceptionName(GetExceptionCode()));
		testFailFunc(0, "Test threw exception", __FILE__, __LINE__);
	}

	// Notify main thread that we're done.
	done.seq = run->seq;
	wtQueueMsg(gTestHarness.testThread, TestThreadCmdMsg_RunFinished, &done, sizeof(done));
}

static void RunTestDone(void *user_data, void *data, WTCmdPacket *packet)
{
	TestThreadFinishedStatus *run = data;
	// Timed out test result; ignore it.
	if (run->seq != gTestHarness.currentTest)
		return;

	gTestHarness.testThreadRunning = false;
}

// Create a test thread, if there isn't already one.
static void InitTestThread()
{
	// Do nothing if we already have one.
	if (gTestHarness.testThread)
		return;

	// Initialize thread.
	gTestHarness.testThread = wtCreate(16, 16, NULL, "TestHarnessThread");
	wtRegisterCmdDispatch(gTestHarness.testThread, TestThreadCmdMsg_Run, thread_RunTest);
	wtRegisterMsgDispatch(gTestHarness.testThread, TestThreadCmdMsg_RunFinished, RunTestDone);
	wtSetThreaded(gTestHarness.testThread, !gTestHarness.disableThreading, 0, false);
	wtStart(gTestHarness.testThread);
}

// When a test is running, the main thread relinquishes all control to the test thread until the run is complete.
// There is no other synchronization, so the main thread is responsible for staying hands off until the test thread is done.
static void RunTestCase(TestCaseCB func, const char *name)
{
	TestThreadRunParameters run = {0};
	int timer;

	// Make a test thread, if we're not going to reuse the one from the previous test.
	InitTestThread();

	// Send test to test thread.
	gTestHarness.testThreadRunning = true;
	run.func = func;
	run.seq = gTestHarness.currentTest;
	run.name = name;
	wtQueueCmd(gTestHarness.testThread, TestThreadCmdMsg_Run, &run, sizeof(run));
	timer = timerAlloc();
	
	// Wait for test thread to complete.
	while (gTestHarness.testThreadRunning)
	{
		// Wait for a response from the thread.
		wtMonitor(gTestHarness.testThread);
		Sleep(1);

		// Check for the timeout.
		if (gTestHarness.timeoutMs && timerElapsed(timer) * 1000 > gTestHarness.timeoutMs)
		{
			// Abandon this WorkerThread entirely.
			gTestHarness.testThread = NULL;
			testFailFunc(0, "Test timed out", __FILE__, __LINE__);
			break;
		}
	}
}

static void RunTestRecurse(TestCase *pTest, int iRecurseDepth)
{
	int i;
	U32 testTimer;

	pTest->iNumChildrenSucceeded = 0;
	pTest->myRunFuncSucceeded = 1;

	if (pTest->runFunc)
	{
		gTestHarness.pCurCaseBeingTested = pTest;
		
		printf("%sRunning Test \"%s\"... ", GetTabString(iRecurseDepth), pTest->testName);

		PERFINFO_AUTO_START("TestSetup",1);
		pTest->testStatus = TEST_SETUP;
		if (pTest->setUpFunc)
		{
			pTest->setUpFunc();
		}
		PERFINFO_AUTO_STOP();

		pTest->testStatus = TEST_RUNNING;

		testTimer = timerAlloc();
		timerStart(testTimer);
		PERFINFO_AUTO_START_STATIC(pTest->testName, &pTest->piStatic, 1);
			
		RunTestCase(pTest->runFunc, pTest->testName);
		
		PERFINFO_AUTO_STOP();
		pTest->elapsedTime = timerElapsed(testTimer);
		timerFree(testTimer);
		printf("done, %f ms\n", pTest->elapsedTime*1000);

		if (pTest->testStatus == TEST_RUNNING)
		{
			// If we didn't fail, we passed
			pTest->testStatus = TEST_PASSED;
		}
		else
		{
			pTest->myRunFuncSucceeded = 0;
		}

		PERFINFO_AUTO_START("TestTearDown",1);
		if (pTest->tearDownFunc)
		{
			pTest->tearDownFunc();
		}
		PERFINFO_AUTO_STOP();

		gTestHarness.pCurCaseBeingTested = NULL;
	}
	else
	{
		pTest->testStatus = TEST_PASSED;
	}

	if (pTest->testStatus == TEST_PASSED)
	{
		int iNumSucceeded = 0;
		
		pTest->testStatus = TEST_RUNNING_CHILDREN;

		for (i=0; i < eaSize(&pTest->ppChildren); i++)
		{
			pTest->iCurChildNum = i;
			printf("%sAbout to run child test %d/%d of %s (%d succeeded)\n",
				GetTabString(iRecurseDepth), i+1, eaSize(&pTest->ppChildren), pTest->testName, iNumSucceeded);

			RunTestRecurse(pTest->ppChildren[i], iRecurseDepth + 1);

			if (pTest->ppChildren[i]->testStatus == TEST_PASSED)
			{
				iNumSucceeded++;
			}
			else
			{
				printf("%sChild test %d/%d (%s) FAILED check \"%s\" at %s:%d with message \"%s\"\n",
					GetTabString(iRecurseDepth), i+1, eaSize(&pTest->ppChildren), pTest->ppChildren[i]->testName, pTest->ppChildren[i]->estrExpr, pTest->ppChildren[i]->errorFile, pTest->ppChildren[i]->errorLine, pTest->ppChildren[i]->estrReturnMessage);
				if (!eaSize(&pTest->ppChildren[i]->ppChildren))
					ErrorfInternal(true, pTest->ppChildren[i]->errorFile, pTest->ppChildren[i]->errorLine,
						"Child test %s FAILED check \"%s\" with message \"%s\"\n",
						pTest->ppChildren[i]->testName, pTest->ppChildren[i]->estrExpr, pTest->ppChildren[i]->estrReturnMessage);
			}
		}

		pTest->iNumChildrenSucceeded = iNumSucceeded;

		if (iNumSucceeded == eaSize(&pTest->ppChildren))
		{
			pTest->testStatus = TEST_PASSED;
		}
		else
		{
			estrPrintf(&pTest->estrReturnMessage, "(%d/%d) children PASSED, %d FAILED\n", iNumSucceeded, eaSize(&pTest->ppChildren), eaSize(&pTest->ppChildren) - iNumSucceeded);
			pTest->testStatus = TEST_FAILED;
		}

	}
}


void testDoRecurseCounting(TestCase *pTest)
{
	int i;


	if (pTest->runFunc)
	{
		pTest->iTotalNumTests_recurse = 1;
		pTest->iNumTestsSucceeded_recurse = pTest->myRunFuncSucceeded ? 1 : 0;
	}
	else
	{
		pTest->iTotalNumTests_recurse = 0;
		pTest->iNumTestsSucceeded_recurse = 0;
	}


	for (i = 0; i < eaSize(&pTest->ppChildren); i++)
	{
		testDoRecurseCounting(pTest->ppChildren[i]);
		pTest->iTotalNumTests_recurse += pTest->ppChildren[i]->iTotalNumTests_recurse;
		pTest->iNumTestsSucceeded_recurse += pTest->ppChildren[i]->iNumTestsSucceeded_recurse;
	}
}




// Run a set of tests, all the ones that match the given string. Won't return until they're done
void testRunMatchedCasesEx(const char *matchString, bool onlyScanTopLevel)
{
	int i;
	U32 runTimer;
		
	assertmsg(!gTestHarness.pCurCaseBeingTested,"You can't recursively run test cases!");

	printf("Test Run \"%s\" started\n",matchString);
	runTimer = timerAlloc();
	timerStart(runTimer);

	for (i = 0; i < eaSize(&gTestHarness.runningTests); i++)
	{
		testResetCase(gTestHarness.runningTests[i]);
	}
	eaClear(&gTestHarness.runningTests);
	strcpy(gTestHarness.currentMatch,matchString);
	for (i = 0; i < eaSize(&gTestHarness.testCases); i++)
	{
		TestCase *test = gTestHarness.testCases[i];

		if (!onlyScanTopLevel || !test->pParent)
		{
			if (matchExact(matchString,test->testName))
			{
				int index = randomIntRange(0,eaSize(&gTestHarness.runningTests));
				// Insert into a random position
				eaInsert(&gTestHarness.runningTests,test,index);
			}
		}
	}
	for (gTestHarness.currentTest = 0; 
		gTestHarness.currentTest < eaSize(&gTestHarness.runningTests);
		gTestHarness.currentTest++)
	{
		RunTestRecurse(gTestHarness.runningTests[gTestHarness.currentTest], 0);
	}
	gTestHarness.elapsedTime = timerElapsed(runTimer);
	timerFree(runTimer);

	printf("Test Run complete\n");
}

int testReportToConsole(void)
{
	int total = 0, succeeded = 0;
	int i;
	
	assertmsg(!gTestHarness.pCurCaseBeingTested,"Can't report tests during a test!");

	printf("Test run \"%s\" started at %s\n",gTestHarness.currentMatch,timeGetLocalDateString());

	for (i = 0; i < eaSize(&gTestHarness.runningTests); i++)
	{
		TestCase *test = gTestHarness.runningTests[i];
		testDoRecurseCounting(test);

		if (test->testStatus == TEST_PASSED)
		{
			printf("  %s: PASSED, elapsed %f seconds\n",test->testName,test->elapsedTime);
		}
		else
		{
			printf("  %s: FAILED check \"%s\" at %s:%d with message \"%s\"\n",
				test->testName,test->estrExpr,test->errorFile,test->errorLine,test->estrReturnMessage);
		}

		succeeded += test->iNumTestsSucceeded_recurse;
		total += test->iTotalNumTests_recurse;
	}

	printf("Test run complete, elapsed %f seconds, %d/%d succeeded\n\n", gTestHarness.elapsedTime, succeeded, total);

	return total - succeeded;
}

void testReportToFile(char *fileName)
{
	FILE *file;
	int i;
	int total = 0, succeeded = 0;
	char buf[MAX_PATH];
	char fullPath[MAX_PATH];

	fileSpecialDir("testreports", SAFESTR(buf));

	assertmsg(!gTestHarness.pCurCaseBeingTested,"Can't report tests during a test!");

	if (fileIsAbsolutePath(fileName))
	{
		strcpy(fullPath,buf);
	}
	else
	{
		sprintf(fullPath,"%s/%s.report",buf,fileName);
	}

	makeDirectoriesForFile(fullPath);

	assertmsg(file = fopen(fullPath,"a"),"Error writing out test report!");

	fprintf(file,"Test run \"%s\" started at %s\n",gTestHarness.currentMatch,timeGetLocalDateString());

	for (i = 0; i < eaSize(&gTestHarness.runningTests); i++)
	{
		TestCase *test = gTestHarness.runningTests[i];
		testDoRecurseCounting(test);

		if (test->testStatus == TEST_PASSED)
		{
			fprintf(file, "  %s: PASSED, elapsed %f seconds\n",test->testName,test->elapsedTime);
		}
		else
		{
			fprintf(file, "  %s: FAILED check \"%s\" at %s:%d with message \"%s\"\n",
				test->testName,test->estrExpr,test->errorFile,test->errorLine,test->estrReturnMessage);
		}

		succeeded += test->iNumTestsSucceeded_recurse;
		total += test->iTotalNumTests_recurse;
	}

	fprintf(file,"Test run complete, elapsed %f seconds, %d/%d succeeded\n\n",gTestHarness.elapsedTime, succeeded, total);

	fclose(file);
}





// Tests for the test system

int testVal = 0;

AUTO_TEST();
void TestTestHarness_WillPass(void)
{
	testAssertMsg(testVal == 0,"This test should pass");
}

AUTO_TEST();
void TestTestHarness_WillFail(void)
{
	testAssertMsg(testVal == 1,"This test should fail");
}


void TestTestHarness_PassWithSetup_SetUp(void)
{
	testVal = 2;
}

void TestTestHarness_PassWithSetup_TearDown(void)
{
	testVal = 0;
}

AUTO_TEST(TestTestHarness_PassWithSetup_SetUp, TestTestHarness_PassWithSetup_TearDown);
void TestTestHarness_PassWithSetup(void)
{
	testAssertMsg(testVal == 2,"This will pass if setup works");
}


AUTO_TEST_GROUP(TestTestHarness_Group, NULL);

int testInt = 5;
char testString[10] = "foo";

AUTO_TEST_BLOCK
(
	ASSERT(COMPARE_INT, testInt == 4);
	ASSERT(COMPARE_STRING, strcmp(testString, "foo") == 0);
);
	



AUTO_TEST_CHILD(TestTestHarness_Group);
void TestTestHarness_Child1(void)
{
}
AUTO_TEST_CHILD(TestTestHarness_Group);
void TestTestHarness_Child2(void)
{
}
AUTO_TEST_CHILD(TestTestHarness_Group);
void TestTestHarness_Child3(void)
{
}
AUTO_TEST_CHILD(TestTestHarness_Group);
void TestTestHarness_Child4(void)
{
	testAssertMsg(0, "This should fail");
}
AUTO_TEST_CHILD(TestTestHarness_Group);
void TestTestHarness_Child5(void)
{
}

/*
AUTO_RUN;
int RegisterTestTestHarness(void)
{
	testRegister("TestTestHarness_WillPass",NULL,TestTestHarness_WillPass,NULL);
	testRegister("TestTestHarness_WillFail",NULL,TestTestHarness_WillFail,NULL);
	testRegister("TestTestHarness_PassWithSetup",TestTestHarness_PassWithSetup_SetUp,TestTestHarness_PassWithSetup,TestTestHarness_PassWithSetup_TearDown);

	return 1;
}
*/

// Test the test harness itself
AUTO_COMMAND ACMD_CATEGORY(Debug);
void TestTestHarness(void)
{
	testRunMatchedCases("TestTestHarness_*");
	testReportToConsole();
}

// Run all tests matching match string
AUTO_COMMAND ACMD_CATEGORY(Test);
void testRun(const char* match)
{
	char fileName[1000];
	char *ast;
	strcpy(fileName,match);
	if (ast = strchr(fileName,'*'))
	{
		*ast = '\0';
	}

	testRunMatchedCases(match);

	testReportToFile(fileName);
}

// Run Tests matching string and report to console
AUTO_COMMAND ACMD_CATEGORY(Test);
void testRunToConsole(const char* match)
{
	testRunMatchedCases(match);

	testReportToConsole();
}

// Run Tests matching string, and also write profiling data
AUTO_COMMAND ACMD_CATEGORY(Test);
void testRunTimed(const char* match)
{
	char fileName[1000];
	char *ast;
	strcpy(fileName,match);
	if (ast = strchr(fileName,'*'))
	{
		*ast = '\0';
	}

	timerRecordStart(fileName);
	testRunMatchedCases(match);
	timerRecordEnd();

	testReportToFile(fileName);
}

// Threading is enabled by default; call with false to disable.
void testEnableThreading(bool enabled)
{
	gTestHarness.disableThreading = !enabled;
}

// Set the per-thread timeout in milliseconds.
void testSetTimeout(U32 ms)
{
	gTestHarness.timeoutMs = ms;
}

#include "autogen/testharness_c_atest.c"
