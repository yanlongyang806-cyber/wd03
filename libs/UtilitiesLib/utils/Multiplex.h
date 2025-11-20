/***************************************************************************



***************************************************************************/

#pragma once
//contains global stuff for the "multiplex" system, as implemented in Multiplexer.c/h, LinkToMultiplexer.c/h, and MultiplexedNetLinkList.c/h

typedef enum
{
	MULTIPLEX_COMMAND_I_WANT_TO_REGISTER_WITH_SERVER,

	MULTIPLEX_COMMAND_REGISTER_SUCCEEDED,
	MULTIPLEX_COMMAND_REGISTER_FAILED,

	MULTIPLEX_COMMAND_SINGLE_SERVER_DIED,

	//this is the message that the multiplexer sends to something like the
	//transaction server
	MULTIPLEX_COMMAND_SERVER_WANTS_TO_REGISTER_WITH_YOU,
	//this is the message the transaction server sends back
	MULTIPLEX_COMMAND_REGISTRATION_REQUEST_RECEIVED,

	MULTIPLEX_COMMAND_PING,
	MULTIPLEX_COMMAND_PING_RETURN,

	MULTIPLEX_COMMAND_SEND_PACKET_TO_MULTIPLE_RECIPIENTS,

	MULTIPLEX_COMMAND_LAST
} enumMultiplexCommand;


#define BITS_FOR_MULTIPLEX_COMMAND 4





//constant IDs used by a few constant servers to simplify handshaking
typedef enum
{
	MULTIPLEX_CONST_ID_TRANSACTION_SERVER,
	MULTIPLEX_CONST_ID_LOG_SERVER,
	MULTIPLEX_CONST_ID_TEST_SERVER,

	MULTIPLEX_CONST_ID_MAX
};










