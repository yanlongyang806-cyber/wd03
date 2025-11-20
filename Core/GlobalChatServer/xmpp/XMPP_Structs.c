#include "XMPP_Structs.h"
#include "ChatServer/xmppShared.h"
#include "AutoGen/XMPP_Structs_h_ast.h"
#include "AutoGen/xmppShared_h_ast.h"
#include "AutoGen/xmppTypes_h_ast.h"

// Destroy array without destroying members.
AUTO_FIXUPFUNC;
TextParserResult FixupXmppClientNode(XmppClientNode *node, enumTextParserFixupType type, void *pExtraData)
{
	if (type == FIXUPTYPE_DESTRUCTOR)
		eaDestroy(&node->resources);
	return PARSERESULT_SUCCESS;
}


#include "AutoGen/XMPP_Structs_h_ast.c"