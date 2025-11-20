/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include <stdio.h>
#include <conio.h>

#include "sysutil.h"
#include <math.h>
#include "DatabaseTest.h"


DatabaseTestState gDBTestState;
StringQueryList * gDBQueryList = NULL;

// *********************************************************************************
// *********************************************************************************
// main, etc.
// *********************************************************************************

AUTO_CMD_INT(gDBTestState.totalUsers,TotalUsers);
AUTO_CMD_INT(gDBTestState.concurrentUsers,ConcurrentUsers);
AUTO_CMD_INT(gDBTestState.totalTransactions,TotalTransactions);
AUTO_CMD_INT(gDBTestState.testingEnabled,DatabaseTestMode);
AUTO_CMD_SENTENCE(gDBTestState.testDir, DatabaseTestDir);

AUTO_RUN;
void SetupDefaultValues(void)
{
	gDBTestState.totalUsers = 5000;
	gDBTestState.concurrentUsers = 2000;
	gDBTestState.totalTransactions = 100000;
}






