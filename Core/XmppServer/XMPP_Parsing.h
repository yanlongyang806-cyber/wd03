// Parse XML data coming from clients into meaningful XMPP.
// It passes the data off to XMPP_Gateway for processing.

#pragma once

#include "XMLParsing.h"

typedef struct XMPP_Parser XMPP_Parser;

// Create an XMPP parser for a new client.
XMPP_Parser *XMPP_ParserCreate(void *userData, const XML_Char *encoding, bool plaintextPermitted);

// Frees all parts of the XMPP_Parser.
void XMPP_ParserDestroy(XMPP_Parser *stream);

// Restart the XMPP parser.
void XMPP_ParserRestart(XMPP_Parser *stream);

// Parse an XMPP fragment.
void XMPP_ParserParse(XMPP_Parser *xp, const char *buf, int len);

// Notify the XMPP parser that the link is secure.
void XMPP_ParserSecure(XMPP_Parser *stream);

// Notify the parser that authentication is complete.
void XMPP_ParserAuthComplete(XMPP_Parser *xp, bool success);

void XMPP_ParserSetLoginState(XMPP_Parser *xp, XMPP_LoginState eState);