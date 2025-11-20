#pragma once

enum
{
	TO_TESTCLIENT_CMD_RESULT = COMM_MAX_CMD, //Indicates game messages are following
	TO_TESTCLIENT_CMD_COMMAND, //an AUTO_COMMAND in the testclient has been called from the gameclient
	TO_TESTCLIENT_CMD_UPDATE, // an update about the state of the Test Client
};

enum
{
	FROM_TESTCLIENT_CMD_SENDCOMMAND = COMM_MAX_CMD,
};
