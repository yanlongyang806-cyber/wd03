/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include <stdio.h>
#include <conio.h>



#include <math.h>

#include "TestTransHarness.h"
#include "ServerLib.h"
#include "DatabaseTest.h"
#include "objTransactions.h"
#include "ControllerScriptingSupport.h"


SimpleServer gSimpleServer;

void UpdateTestTransTitle(void)
{
	char buf[200];
	sprintf(buf, "TestTransClient ID %d", gSimpleServer.iServerID);
	setConsoleTitle(buf);
}

void handleHelloWorld(Packet* pak, NetLink* link)
{
	char temp[256];

	pktGetString(pak,temp,sizeof(temp));

	printf("Got back string: %s\n", temp);
}


static char *sOutcomeStrings[4] =
{
	"NONE",
	"FAILURE",
	"SUCCESS",
	"UNDEFINED",
};

void GenerateDummyTransactions(void)
{
	static TransactionReturnVal returnVal;
	static bool bCurrentlyWaitingForReturnVal = false;
	

	if (bCurrentlyWaitingForReturnVal)
	{
		if (returnVal.eOutcome != TRANSACTION_OUTCOME_NONE)
		{
			int i;

			printf("Transaction returned\n");
			bCurrentlyWaitingForReturnVal = false;
			
			for (i=0; i < returnVal.iNumBaseTransactions; i++)
			{
				printf("%d: %s  \"%s\"\n", i, sOutcomeStrings[returnVal.pBaseReturnVals[i].eOutcome], 
					returnVal.pBaseReturnVals[i].returnString ? returnVal.pBaseReturnVals[i].returnString : "NULL");
			}
			ReleaseReturnValData(gSimpleServer.pLocalTransactionManager, &returnVal);
		}
	}

	// This is for testing the TestTransServer
	if (rand() % 100 < 15)
	{
		int iNumTransactionsToGenerate = rand() % 3 + 1;
		
		int i,j;

		for (j=0; j < iNumTransactionsToGenerate; j++)
		{

#define TEST_TRANSACTION_MAX_SIZE 10

			BaseTransaction testTransactions[TEST_TRANSACTION_MAX_SIZE] = {0};
			char testStrings[TEST_TRANSACTION_MAX_SIZE][512];

			int iNumBaseTransactions = 1 + rand() % (TEST_TRANSACTION_MAX_SIZE - 1);

			for (i=0; i < iNumBaseTransactions; i++)
			{
				bool bTime = rand()%100 > 50;
				bool bInc = rand()%100 > 50;
				int iAmount = rand()%100;
			
				if (rand() % 100 < 10)
				{
					sprintf(testStrings[i], "blah blah");
				}
				else if (rand() % 100 < 5)
				{
					sprintf(testStrings[i], "incboth %d", rand() % 20 + 1);
				}
				else
				{
					sprintf(testStrings[i], "%s %s %d", bTime ? "time" : "money", bInc ? "inc" : "dec", iAmount);
				}
				
				testTransactions[i].pData = testStrings[i];
			
//				testTransactions[i].recipient.containerID = rand() % OBJECTS_PER_SERVER + gSimpleServer.iServerID * OBJECTS_PER_SERVER + 1;
				testTransactions[i].recipient.containerID = rand() % (OBJECTS_PER_SERVER * 10) + 1 + OBJECTS_PER_SERVER;
				testTransactions[i].recipient.containerType = GLOBALTYPE_ENTITYPLAYER;
				testTransactions[i].pRequestedTransVariableNames = NULL;
			}


			if (bCurrentlyWaitingForReturnVal)
			{
				RequestNewTransaction_Deprecated(gSimpleServer.pLocalTransactionManager, "TestTransClient", iNumBaseTransactions, testTransactions,
					(enumTransactionType)(rand() % (TRANS_TYPE_COUNT - 1) + 1), NULL
					);
			}
			else
			{
				bCurrentlyWaitingForReturnVal = true;

				RequestNewTransaction_Deprecated(gSimpleServer.pLocalTransactionManager, "TestTransClient", iNumBaseTransactions, testTransactions,
					(enumTransactionType)(rand() % (TRANS_TYPE_COUNT - 1) + 1), &returnVal
					);
			}

		}
	}
}

void BasicTransactionTest_SetUp(void)
{
	int i;
	int *pIDs;

	gSimpleServer.iServerID = 1;

	InitSimpleServer(&gSimpleServer);

	pIDs = malloc(sizeof(int) * OBJECTS_PER_SERVER);
	assert(pIDs);

	for (i=0; i < OBJECTS_PER_SERVER; i++)
	{
		pIDs[i] = gSimpleServer.iServerID * OBJECTS_PER_SERVER + i + 1;
	}

	RegisterMultipleObjectsWithTransactionServer(gSimpleServer.pLocalTransactionManager, GLOBALTYPE_ENTITYPLAYER, OBJECTS_PER_SERVER, pIDs);

	gObjectTransactionManager.localManager = gSimpleServer.pLocalTransactionManager;

	free(pIDs);
}

void BasicTransactionTest_TearDown(void)
{
	//DestroyLocalTransactionManager(gSimpleServer.pLocalTransactionManager);
}

AUTO_COMMAND;
void BasicTransactionTest(void)
{
	int i;
	BasicTransactionTest_SetUp();
	for (i = 0; i < 10000; i++)
	{
		serverLibOncePerFrame();
		commMonitor(commDefault());
		UpdateSimpleServer(&gSimpleServer);
		GenerateDummyTransactions();
	}
	BasicTransactionTest_TearDown();
	ControllerScript_Succeeded();
}

