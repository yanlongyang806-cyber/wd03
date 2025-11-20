#pragma warning (disable : 6011 6211)

#include "snmp_pp/snmp_pp.h"
#include "agent_pp/agent++.h"
#include "agent_pp/snmp_pp_ext.h"

#include <agent_pp/agent++.h>
#include <agent_pp/snmp_group.h>
#include <agent_pp/system_group.h>
#include <agent_pp/snmp_target_mib.h>
#include <agent_pp/snmp_notification_mib.h>
#include <agent_pp/notification_originator.h>
#include <agent_pp/mib_complex_entry.h>
#include <agent_pp/v3_mib.h>

extern "C" {
#include "earray.h"
#include "estring.h"
#include "snmplib.h"
}

#pragma comment(lib, "snmp++.lib")
#pragma comment(lib, "agent++.lib")

#define SYSTEM_DESCRIPTION_OID "1.3.6.1.2.1.1.1.0"

// -------------------------------------------------------------------------------------------------
// Client stuff

static bool bInitialized = false;
static void basicSnmpInit(void)
{
	if(bInitialized)
		return;

	Snmp::socket_startup();

	bInitialized = true;
}

static bool snmpGetVB(const char *location, int port, const char *communitystr, const char *oidstr, Vb &vb)
{
	basicSnmpInit();

	UdpAddress address(location);
	if (!address.valid())
	{
		printf("snmpGetVB(): ERROR: Invalid address.\n");
		return false;
	}

	Oid oid(oidstr);
	if (!oid.valid())
	{
		printf("snmpGetVB(): ERROR: Invalid oid.\n");
		return false;
	}

	int status;
	Snmp snmp(status, 0, (address.get_ip_version() == Address::version_ipv6));

	if ( status != SNMP_CLASS_SUCCESS)
	{
		printf("snmpGetVB(): ERROR: Failed to create SNMP session.\n");
		return false;
	}

	OctetStr community(communitystr);
	Pdu pdu;
	vb.set_oid(oid);
	pdu += vb;

	address.set_port(port);
	CTarget ctarget(address);
	ctarget.set_version(version1);
	ctarget.set_retry(1);
	ctarget.set_timeout(100);
	ctarget.set_readcommunity(community);

	status = snmp.get( pdu, ctarget);

	bool bSuccess = (status == SNMP_CLASS_SUCCESS);
	if(bSuccess)
		pdu.get_vb(vb,0);

	return bSuccess;
}

bool snmpGetString(const char *location, int port, const char *communitystr, const char *oidstr, char **estr)
{
	Vb vb;
	bool bSuccess = snmpGetVB(location, port, communitystr, oidstr, vb);

	// Instead of making it an error condition to ask for ints or floats with snmpGetString, I just
	// allow it and leverage get_printable_value() to do a smart conversion for me. 

	if(bSuccess)
		estrPrintf(estr, "%s", vb.get_printable_value());

	return bSuccess;
}

bool snmpGetInt(const char *location, int port, const char *communitystr, const char *oidstr, int *output)
{
	Vb vb;
	bool bSuccess = snmpGetVB(location, port, communitystr, oidstr, vb);

	if(bSuccess)
		bSuccess = (vb.get_value(*output) == SNMP_CLASS_SUCCESS);

	return bSuccess;
}

// -------------------------------------------------------------------------------------------------
// Server stuff

typedef struct SnmpValueHandle 
{
	MibLeaf *pMibLeaf;
} SnmpValueHandle;

static SnmpValueHandle **ppHandles = NULL;


static Snmpx *spMainSnmpX = NULL;
static Mib *spMib = NULL;
static RequestList *spReqList = NULL;

bool snmpServerInit(int port, const char *system_description)
{
	Snmp::socket_startup();

	if(spMainSnmpX)
		snmpServerShutdown();

	int status;
	spMainSnmpX = new Snmpx(status, port);

	if (status != SNMP_CLASS_SUCCESS)
	{
		snmpServerShutdown();
		return false;
	}

	spMib = new Mib();
	spReqList = new RequestList();

	// register requestList for outgoing requests
	spMib->set_request_list(spReqList);

	MibLeaf *m = new MibLeaf(SYSTEM_DESCRIPTION_OID, READONLY, 1);
	m->set_value(OctetStr(system_description));
	spMib->add(m);

	spMib->init();
	spReqList->set_snmp(spMainSnmpX);

	return true;
}

void snmpServerShutdown(void)
{
	for(int i=0; i<eaSize((void***)&ppHandles);i++)
	{
		free(ppHandles[i]);
	}
	eaClear((cEArrayHandle*)&ppHandles);

	if(spMib)
	{
		delete spMib;
		spMib = NULL;
	}

	if(spReqList)
	{
		delete spReqList;
		spReqList = NULL;
	}

	if(spMainSnmpX)
	{
		delete spMainSnmpX;
		spMainSnmpX = NULL;
	}
}

void snmpServerOncePerFrame(void)
{
	Request* req;

	if(!spMainSnmpX || !spReqList || !spMib)
		return;

	req = spReqList->receive(2);

	if (req)
	{
		spMib->process_request(req);
	}
	else
	{
		spMib->cleanup();
	}
}

SnmpValueHandle * snmpServerCreateString(const char *oidstr, const char *initial_value)
{
	if(!spMainSnmpX || !spReqList || !spMib)
		return NULL;

	SnmpValueHandle *pHandle = (SnmpValueHandle *)calloc(sizeof(SnmpValueHandle), 1);

	pHandle->pMibLeaf = new MibLeaf(oidstr, READONLY, 1);
	pHandle->pMibLeaf->set_value(OctetStr(initial_value));
	spMib->add(pHandle->pMibLeaf);

	eaPush((cEArrayHandle*)&ppHandles, pHandle);

	return pHandle;
}

SnmpValueHandle * snmpServerCreateInt(const char *oidstr, int initial_value)
{
	if(!spMainSnmpX || !spReqList || !spMib)
		return NULL;

	SnmpValueHandle *pHandle = (SnmpValueHandle *)calloc(sizeof(SnmpValueHandle), 1);

	pHandle->pMibLeaf = new MibLeaf(oidstr, READONLY, 1);
	pHandle->pMibLeaf->set_value(SnmpInt32(initial_value));
	spMib->add(pHandle->pMibLeaf);

	eaPush((cEArrayHandle*)&ppHandles, pHandle);

	return pHandle;
}

void snmpServerDeleteHandle(SnmpValueHandle *handle)
{
	if(!spMainSnmpX || !spReqList || !spMib)
		return;

	spMib->remove(handle->pMibLeaf->get_oid());

	eaFindAndRemoveFast((cEArrayHandle*)&ppHandles, handle);
	free(handle);
}

void snmpServerSetValueString(SnmpValueHandle *handle, const char *value)
{
	if(!spMainSnmpX || !spReqList || !spMib)
		return;

	handle->pMibLeaf->set_value(OctetStr(value));
}

void snmpServerSetValueInt(SnmpValueHandle *handle, int value)
{
	if(!spMainSnmpX || !spReqList || !spMib)
		return;

	handle->pMibLeaf->set_value(SnmpInt32(value));
}

