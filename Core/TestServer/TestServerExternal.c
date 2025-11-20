#include "TestServerExternal.h"
#include "GlobalComm.h"
#include "MultiplexedNetLinkList.h"
#include "net.h"
#include "netpacketutil.h"
#include "StashTable.h"
#include "TestClientCommon.h"
#include "TestServerExpression.h"
#include "TestServerGlobal.h"
#include "TestServerIntegration.h"
#include "TestServerLua.h"
#include "TestServerMetric.h"
#include "TestServerSharded.h"

typedef struct TestServerExternalLink
{
	bool bIsMultiplexLink;

	void *pUserdata;
	TestServer_ExternalLink_MessageCB *pMessageCallback;
	TestServer_ExternalLink_DisconnectCB *pDisconnectCallback;

	StashTable pStashUserdata;
	StashTable pStashMessageCallbacks;
	StashTable pStashDisconnectCallbacks;
} TestServerExternalLink;

Packet *TestServer_GetDestinationPacket(NetLink *link, int index, int cmd)
{
	Packet *pkt = NULL;

	if(index == -1)
	{
		pkt = pktCreate(link, cmd);
	}
	else
	{
		pkt = CreateMultiplexedNetLinkListPacket(link, index, cmd, NULL);
	}

	return pkt;
}

void *TestServer_GetLinkUserData(NetLink *link, int index)
{
	TestServerExternalLink *pExternalLink = linkGetUserData(link);
	void *user_data = pExternalLink->pUserdata;

	if(pExternalLink->bIsMultiplexLink && index > -1)
	{
		stashIntFindPointer(pExternalLink->pStashUserdata, index, &user_data);
	}

	return user_data;
}

TestServer_ExternalLink_MessageCB *TestServer_GetLinkMessageCallback(NetLink *link, int index)
{
	TestServerExternalLink *pExternalLink = linkGetUserData(link);
	TestServer_ExternalLink_MessageCB *pCallback = pExternalLink->pMessageCallback;

	if(pExternalLink->bIsMultiplexLink && index > -1)
	{
		stashIntFindPointer(pExternalLink->pStashMessageCallbacks, index, (void **)&pCallback);
	}

	return pCallback;
}

TestServer_ExternalLink_DisconnectCB *TestServer_GetLinkDisconnectCallback(NetLink *link, int index)
{
	TestServerExternalLink *pExternalLink = linkGetUserData(link);
	TestServer_ExternalLink_DisconnectCB *pCallback = pExternalLink->pDisconnectCallback;

	if(pExternalLink->bIsMultiplexLink && index > -1)
	{
		stashIntFindPointer(pExternalLink->pStashDisconnectCallbacks, index, (void **)&pCallback);
	}

	return pCallback;
}

void TestServer_SetLinkUserData(NetLink *link, int index, void *user_data)
{
	TestServerExternalLink *pExternalLink = linkGetUserData(link);

	if(pExternalLink->bIsMultiplexLink && index > -1)
	{
		stashIntAddPointer(pExternalLink->pStashUserdata, index, user_data, true);
	}
	else
	{
		pExternalLink->pUserdata = user_data;
	}
}

void TestServer_SetLinkMessageCallback(NetLink *link, int index, TestServer_ExternalLink_MessageCB *pCallback)
{
	TestServerExternalLink *pExternalLink = linkGetUserData(link);

	if(pExternalLink->bIsMultiplexLink && index > -1)
	{
		stashIntAddPointer(pExternalLink->pStashMessageCallbacks, index, pCallback, true);
	}
	else
	{
		pExternalLink->pMessageCallback = pCallback;
	}
}

void TestServer_SetLinkDisconnectCallback(NetLink *link, int index, TestServer_ExternalLink_DisconnectCB *pCallback)
{
	TestServerExternalLink *pExternalLink = linkGetUserData(link);

	if(pExternalLink->bIsMultiplexLink && index > -1)
	{
		stashIntAddPointer(pExternalLink->pStashDisconnectCallbacks, index, pCallback, true);
	}
	else
	{
		pExternalLink->pDisconnectCallback = pCallback;
	}
}

static void TestServer_HandleMessage_Internal(Packet *pkt, int cmd, NetLink *link, int index, TestServerExternalLink *pExternalLink)
{
	if(index > -1 && !pExternalLink->pStashUserdata)
	{
		pExternalLink->bIsMultiplexLink = true;
		pExternalLink->pStashUserdata = stashTableCreateInt(0);
		pExternalLink->pStashDisconnectCallbacks = stashTableCreateInt(0);
		pExternalLink->pStashMessageCallbacks = stashTableCreateInt(0);
	}

	switch(cmd)
	{
	case TO_TESTSERVER_SET_GLOBAL:
		{
			const char *pcScope = pktGetStringTemp(pkt);
			const char *pcName = pktGetStringTemp(pkt);
			TestServerGlobalType eType = pktGetU32(pkt);
			bool bPersist = pktGetBool(pkt);

			switch(eType)
			{
			case TSGV_Integer:
				{
					int val = pktGetU32(pkt);
					TestServer_SetGlobal_Integer(pcScope, pcName, -1, val);
				}
			xcase TSGV_Boolean:
				{
					int val = pktGetBool(pkt);
					TestServer_SetGlobal_Boolean(pcScope, pcName, -1, val);
				}
			xcase TSGV_Float:
				{
					float val = pktGetFloat(pkt);
					TestServer_SetGlobal_Float(pcScope, pcName, -1, val);
				}
			xcase TSGV_String:
				{
					const char *val = pktGetStringTemp(pkt);
					TestServer_SetGlobal_String(pcScope, pcName, -1, val);
				}
			xcase TSGV_Password:
				{
					const char *val = pktGetStringTemp(pkt);
					TestServer_SetGlobal_Password(pcScope, pcName, -1, val);
				}
			}

			TestServer_PersistGlobal(pcScope, pcName, bPersist);
		}
	xcase TO_TESTSERVER_GET_GLOBAL:
		{
			const char *pcScope = pktGetStringTemp(pkt);
			const char *pcName = pktGetStringTemp(pkt);
			Packet *pak = TestServer_GetDestinationPacket(link, index, FROM_TESTSERVER_HERE_IS_GLOBAL);

			TestServer_SendGlobal(pcScope, pcName, pak);
			pktSend(&pak);
		}
	xcase TO_TESTSERVER_PUSH_METRIC:
		{
			const char *pcScope = pktGetStringTemp(pkt);
			const char *pcName = pktGetStringTemp(pkt);
			float val = pktGetFloat(pkt);
			bool bPersist = pktGetBool(pkt);
			Packet *pak = TestServer_GetDestinationPacket(link, index, FROM_TESTSERVER_HERE_IS_METRIC);

			TestServer_PushMetric(pcScope, pcName, val);
			TestServer_SendGlobal(pcScope, pcName, pak);
			pktSend(&pak);
		}
	xcase TO_TESTSERVER_GET_METRIC:
		{
			const char *pcScope = pktGetStringTemp(pkt);
			const char *pcName = pktGetStringTemp(pkt);
			Packet *pak = TestServer_GetDestinationPacket(link, index, FROM_TESTSERVER_HERE_IS_METRIC);
			
			TestServer_SendGlobal(pcScope, pcName, pak);
			pktSend(&pak);
		}
	xcase TO_TESTSERVER_CLEAR_GLOBAL:
		{
			const char *pcScope = pktGetStringTemp(pkt);
			const char *pcName = pktGetStringTemp(pkt);
			
			TestServer_ClearGlobal(pcScope, pcName);
		}
	xcase TO_TESTSERVER_I_AM_A_TESTCLIENT:
		{
			TestServer_SetLinkMessageCallback(link, index, TestServer_Client_MessageCB);
			TestServer_SetLinkDisconnectCallback(link, index, TestServer_Client_DisconnectCB);
		}
	xdefault:
		{
			TestServer_ExternalLink_MessageCB *pCallback = TestServer_GetLinkMessageCallback(link, index);
			void *pUserdata = TestServer_GetLinkUserData(link, index);

			if(pCallback)
			{
				pCallback(pkt, cmd, link, index, pUserdata);
			}
		}
		break;
	}
}

void TestServer_ExternalMessage(Packet *pkt, int cmd, NetLink *link, TestServerExternalLink *pExternalLink)
{
	TestServer_HandleMessage_Internal(pkt, cmd, link, -1, pExternalLink);
}

void TestServer_MultiplexedExternalMessage(Packet *pkt, int cmd, int index, NetLink *link, TestServerExternalLink *pExternalLink)
{
	TestServer_HandleMessage_Internal(pkt, cmd, link, index, pExternalLink);
}

static void TestServer_HandleDisconnect_internal(NetLink *link, int index, TestServerExternalLink *pExternalLink)
{
	TestServer_ExternalLink_DisconnectCB *pCallback = TestServer_GetLinkDisconnectCallback(link, index);
	void *pUserdata = TestServer_GetLinkUserData(link, index);

	if(pCallback)
	{
		pCallback(link, index, pUserdata);
	}
}

void TestServer_ExternalDisconnect(NetLink *link, TestServerExternalLink *pExternalLink)
{
	if(pExternalLink->bIsMultiplexLink)
	{
		StashTableIterator iter = {0};
		StashElement pElem;

		stashGetIterator(pExternalLink->pStashDisconnectCallbacks, &iter);

		while(stashGetNextElement(&iter, &pElem))
		{
			TestServer_HandleDisconnect_internal(link, stashElementGetIntKey(pElem), pExternalLink);
		}

		stashTableDestroy(pExternalLink->pStashDisconnectCallbacks);
		stashTableDestroy(pExternalLink->pStashMessageCallbacks);
		stashTableDestroy(pExternalLink->pStashUserdata);
	}

	TestServer_HandleDisconnect_internal(link, -1, pExternalLink);
}

void TestServer_MultiplexedExternalDisconnect(NetLink *link, int index, TestServerExternalLink *pExternalLink)
{
	TestServer_HandleDisconnect_internal(link, index, pExternalLink);
	stashIntRemovePointer(pExternalLink->pStashDisconnectCallbacks, index, NULL);
	stashIntRemovePointer(pExternalLink->pStashMessageCallbacks, index, NULL);
	stashIntRemovePointer(pExternalLink->pStashUserdata, index, NULL);
}

void TestServer_InitExternal(void)
{
	PrepareForMultiplexedNetLinkListMode(TestServer_MultiplexedExternalMessage, TestServer_MultiplexedExternalDisconnect, TestServer_ExternalMessage, TestServer_ExternalDisconnect);

	if(!commListen(commDefault(), LINKTYPE_DEFAULT, LINK_MEDIUM_LISTEN, DEFAULT_TESTSERVER_PORT, MultiplexedNetLinkList_Wrapper_HandleMsg, NULL, MultiplexedNetLinkList_Wrapper_ClientDisconnect, sizeof(TestServerExternalLink)))
	{
		assertmsgf(0, "Couldn't open listen socket on port %d!", DEFAULT_TESTSERVER_PORT);
	}
}