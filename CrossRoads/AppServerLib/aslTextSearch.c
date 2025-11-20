#include "../../3rdparty/sqlite/sqlite3.h"
#include "estring.h"
#include "AslTextSearch.h"
#include "timing_profiler.h"
#include "utils.h"

#ifdef _WIN64
#pragma comment(lib, "sqliteX64.lib")
#else
#pragma comment(lib, "sqlite.lib")
#endif
/*
(5:25:58 PM) awerner@sol/sol:   CREATE VIRTUAL TABLE ugcProjectDescriptions USING fts3(containerID, description);  INSERT INTO ugcProjectDescriptions (containerID, description) VALUES(13424, 'This is my fun level');   SELECT ContainerID FROM ugcProjectDescription WHERE description MATCH 'dragon';  
(5:26:28 PM) Noah Kantrowitz: SELECT ContainerID FROM ugcProjectDescription WHERE description MATCH 'dragon' LIMIT 10;
(5:28:17 PM) Noah Kantrowitz: SELECT ContainerID FROM ugcProjectDescription WHERE description MATCH ? LIMIT 10;
*/

typedef struct sqlite3 sqlite3;

typedef struct AslTextSearchManager
{
	char name[64];
	sqlite3 *pSQLite3;
} AslTextSearchManager;

//returns true on success
bool doSQLiteStatement(sqlite3 *pSQLite3, char *pFmt, ...)
{
	char *pStatementString = NULL;
	sqlite3_stmt *pStatement = NULL;
	const char *pTemp = NULL;
	bool bSuccess = false;
	bool bDone = false;

	estrGetVarArgs(&pStatementString, pFmt);

	if (sqlite3_prepare_v2(pSQLite3, pStatementString, estrLength(&pStatementString), 
		&pStatement, &pTemp) != SQLITE_OK)
	{
		estrDestroy(&pStatementString);
		return false;
	}

	estrDestroy(&pStatementString);

	while (!bDone)
	{
		switch (sqlite3_step(pStatement))
		{
		case SQLITE_DONE:
			bSuccess = true;
			bDone = true;
			break;
		case SQLITE_BUSY:
			break;
		default:
			//handle error here
			bDone = true;
			break;
		}
	}

	sqlite3_finalize(pStatement);

	return bSuccess;
}



		






AslTextSearchManager *aslTextSearch_Init(const char *pName)
{
	AslTextSearchManager *pManager = calloc(sizeof(AslTextSearchManager), 1);
	if (sqlite3_open(
		":memory:",   /* Database filename (UTF-8) */
		&pManager->pSQLite3) != SQLITE_OK)
	{
		free(pManager);
		return NULL;
	}

	strcpy(pManager->name, pName);

	if (!doSQLiteStatement(pManager->pSQLite3, "CREATE VIRTUAL TABLE %s USING fts3(ID, description, tokenize=porter);", pName))
	{
		free(pManager);
		return NULL;
	}

	return pManager;
}


bool aslTextSearch_AddString(AslTextSearchManager *pManager, U64 iID, const char *pString, bool bStringIsEffectivelyStatic)
{
	char *pStatementString = NULL;
	sqlite3_stmt *pStatement = NULL;
	char *pTemp;
	bool bSuccess = false;
	bool bDone = false;
	int iRes;

	PERFINFO_AUTO_START_FUNC();	

	if (!pString)
	{
		pString = "";
	}

	estrPrintf(&pStatementString, "INSERT INTO %s (ID, description) VALUES(%llu, ?);",
		pManager->name, iID);

	if ((iRes = sqlite3_prepare_v2(pManager->pSQLite3, pStatementString, estrLength(&pStatementString), 
		&pStatement, &pTemp)) != SQLITE_OK)
	{
		estrDestroy(&pStatementString);
		PERFINFO_AUTO_STOP();	

		return false;
	}

	if ((iRes = sqlite3_bind_text(pStatement, 1, pString, (int)(strlen(pString) + 1), bStringIsEffectivelyStatic ? SQLITE_STATIC : SQLITE_TRANSIENT)) != SQLITE_OK)
	{
		sqlite3_finalize(pStatement);
		estrDestroy(&pStatementString);
		PERFINFO_AUTO_STOP();	
		return false;
	}

	estrDestroy(&pStatementString);

	while (!bDone)
	{
		switch (sqlite3_step(pStatement))
		{
		case SQLITE_DONE:
			bSuccess = true;
			bDone = true;
			break;
		case SQLITE_BUSY:
			break;
		default:
			//handle error here
			bDone = true;
			break;
		}
	}

	sqlite3_finalize(pStatement);
	PERFINFO_AUTO_STOP();	

	return bSuccess;
}

static bool aslTextSearch_Search_inner(AslTextSearchManager *pManager, const char *pString, aslTextSearchCB pCB, void *pUserData)
{
	char *pStatementString = NULL;
	sqlite3_stmt *pStatement = NULL;
	char *pTemp;
	bool bSuccess = false;
	bool bDone = false;
	int iRes;

	PERFINFO_AUTO_START_FUNC();	


	estrPrintf(&pStatementString, "SELECT ID FROM %s WHERE description MATCH ?;", pManager->name);

	if ((iRes = sqlite3_prepare_v2(pManager->pSQLite3, pStatementString, estrLength(&pStatementString), 
		&pStatement, &pTemp)) != SQLITE_OK)
	{
		estrDestroy(&pStatementString);
		PERFINFO_AUTO_STOP();	

		return false;
	}

	if ((iRes = sqlite3_bind_text(pStatement, 1, pString, (int)(strlen(pString) + 1), SQLITE_STATIC)) != SQLITE_OK)
	{
		sqlite3_finalize(pStatement);
		estrDestroy(&pStatementString);
		PERFINFO_AUTO_STOP();	
		return false;
	}

	estrDestroy(&pStatementString);

	while (!bDone)
	{
		U32 iFoundID;
		switch (sqlite3_step(pStatement))
		{
		case SQLITE_DONE:
			bSuccess = true;
			bDone = true;
			break;
		case SQLITE_BUSY:
			break;
		case SQLITE_ROW:
			iFoundID = sqlite3_column_int64(pStatement, 0);
			if (!pCB(iFoundID, pUserData))
			{
				bSuccess = true;
				bDone = true;
			}
			break;
			


		default:
			//handle error here
			bDone = true;
			break;
		}
	}

	sqlite3_finalize(pStatement);

	PERFINFO_AUTO_STOP();	


	return bSuccess;
}

bool aslTextSearch_Search(AslTextSearchManager *pManager, const char *pString, aslTextSearchCB pCB, void *pUserData)
{
	char *pFixedUp = NULL;
	U32 i;
	bool bRetVal;

	if (aslTextSearch_Search_inner(pManager, pString, pCB, pUserData))
	{
		return true;
	}

	estrStackCreate(&pFixedUp);
	estrCopy2(&pFixedUp, pString);
	for (i=0; i < estrLength(&pFixedUp); i++)
	{
		if (!(isalnum(pFixedUp[i]) || pFixedUp[i] == '_'))
		{
			pFixedUp[i] = ' ';
		}
		else
		{
			pFixedUp[i] = tolower(pFixedUp[i]);
		}
	}

	bRetVal = aslTextSearch_Search_inner(pManager, pFixedUp, pCB, pUserData);
	estrDestroy(&pFixedUp);
	return bRetVal;
}	




bool aslTextSearch_RemoveString(AslTextSearchManager *pManager, U64 iID)
{
	bool bRetVal;
	PERFINFO_AUTO_START_FUNC();	
	bRetVal = doSQLiteStatement(pManager->pSQLite3, "DELETE FROM %s WHERE ID=%llu;",pManager->name, iID);
	PERFINFO_AUTO_STOP();
	return bRetVal;
}


bool testSearchCB(U64 iInt, void *pUserData)
{
	printf("Found %"FORM_LL"d\n", iInt);
	return true;
}

AUTO_COMMAND;
void SQLiteTest(void)
{
	AslTextSearchManager *pManager;

	system("erase c:\\temp\\sql.txt");

	assert((pManager = aslTextSearch_Init("test")));
	assert(aslTextSearch_AddString(pManager, 1, "This is string one", true));
	assert(aslTextSearch_AddString(pManager, 2, "This is string two", true));
	assert(aslTextSearch_AddString(pManager, 3, "This is string three", true));
	assert(aslTextSearch_AddString(pManager, 3000000000, "This is string three billion", true));

	printf("Searching for: string\n");
	aslTextSearch_Search(pManager, "string", testSearchCB, NULL);

	printf("Searching for: three string\n");
	aslTextSearch_Search(pManager, "three string", testSearchCB, NULL);

	aslTextSearch_RemoveString(pManager, 2);

	printf("Searching for: string\n");
	aslTextSearch_Search(pManager, "string", testSearchCB, NULL);



}



void aslTextSearch_Cleanup(AslTextSearchManager *pManager)
{
	sqlite3_close(pManager->pSQLite3);
	free(pManager);
}
