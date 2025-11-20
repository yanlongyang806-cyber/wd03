#include "billing.h"
#include "error.h"
#include "estring.h"
#include "StringUtil.h"
#include "timing.h"
#include "vindicia.h"

struct Namespace namespaces[] =
{   // {"ns-prefix", "ns-name"}
	{"SOAP-ENV", "http://schemas.xmlsoap.org/soap/envelope/"},
	{"SOAP-ENC", "http://schemas.xmlsoap.org/soap/encoding/"},
	{"xsi", "http://www.w3.org/2001/XMLSchema-instance"},
	{"xsd", "http://www.w3.org/2001/XMLSchema"},

	{"vin", "http://soap.vindicia.com/Vindicia"},
	{"abl", "http://soap.vindicia.com/AutoBill"},
	{"acc", "http://soap.vindicia.com/Account"},
	{"act", "http://soap.vindicia.com/Activity"},
	{"add", "http://soap.vindicia.com/Address"},
	{"bpl", "http://soap.vindicia.com/BillingPlan"},
	{"cgb", "http://soap.vindicia.com/Chargeback"},
	{"ecs", "http://soap.vindicia.com/ElectronicSignature"},
	{"ent", "http://soap.vindicia.com/Entitlement"},
	{"etp", "http://soap.vindicia.com/EmailTemplate"},
	{"met", "http://soap.vindicia.com/MetricStatistics"},
	{"prd", "http://soap.vindicia.com/Product"},
	{"pym", "http://soap.vindicia.com/PaymentMethod"},
	{"pyp", "http://soap.vindicia.com/PaymentProvider"},
	{"rfd", "http://soap.vindicia.com/Refund"},
	{"trn", "http://soap.vindicia.com/Transaction"},

	{NULL, NULL}
}; 

bool vindiciaObjtoXML(char **estr, void *pFuncData, SoapPutFunc func, const char *tagname)
{
	bool bRet = false;
	struct soap *s;
	char *eInnerStr = NULL;
	const char *err = NULL;
	bool success;

	PERFINFO_AUTO_START_FUNC();

	s = callocStruct(struct soap);

	soap_init1(s, SOAP_IO_CRYPTIC_STRING | SOAP_C_UTFSTRING);
	s->estring = &eInnerStr;
	s->prolog = "";
	s->fmalloc = btSoapAlloc;
	soap_begin_send(s);
	func(s, pFuncData, tagname, NULL);
	soap_end_send(s); 
	soap_destroy(s);
	soap_done(s);
	btSoapFreeAllocs(s);

	if(eInnerStr)
	{
		estrPrintf(estr, 
			"<soapenv:Envelope xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" xmlns:soapenv=\"http://schemas.xmlsoap.org/soap/envelope/\">\n"
			"<soapenv:Header/>\n"
			"<soapenv:Body>\n"
			"%s\n"
			"</soapenv:Body>\n"
			"</soapenv:Envelope>\n",
			eInnerStr);

		estrDestroy(&eInnerStr);
		bRet = true;
	}
	free(s);

	success = UTF8StringIsValid(*estr, &err);
	devassertmsgf(success, "Generated invalid UTF-8 at position %lu", err ? (unsigned long)(err - *estr) : 0);

	PERFINFO_AUTO_STOP_FUNC();

	return bRet;
}

static void cleanupSoapResultObj(struct soap *s)
{
	PERFINFO_AUTO_START_FUNC();
	soap_end_recv(s);
	soap_destroy(s);
	soap_end(s);
	soap_done(s);
	btSoapFreeAllocs(s);
	PERFINFO_AUTO_STOP();
}

VindiciaXMLtoObjResult * vindiciaXMLtoObj(const char *incoming_xml, int sizeofObj, SoapGetFunc func, const char *tagname)
{
	const char *xml;
	bool success;
	const char *err = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Make sure this is valid UTF-8 data.
	success = UTF8StringIsValid(incoming_xml, &err);
	if (!success)
		AssertOrAlert("ACCOUNT_SERVER_UTF8", "Received invalid UTF-8 at position %lu", err ? (unsigned long)(err - incoming_xml) : 0);

	// -----------------------------------------------
	// Skip past the soap:Envelope and soap:Body
	xml = strstr(incoming_xml, ":Body");
	if(!xml)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	while(*xml && (*xml != '<'))
		xml++;
	// -----------------------------------------------

	if(*xml)
	{
		VindiciaXMLtoObjResult *pResult = callocStruct(struct VindiciaXMLtoObjResult);
		pResult->pObj = calloc(1, sizeofObj);

		soap_init1(&pResult->soapObj, SOAP_IO_CRYPTIC_STRING | SOAP_C_UTFSTRING);
		pResult->soapObj.fmalloc = btSoapAlloc;
		soap_begin(&pResult->soapObj);
		pResult->soapObj.input = xml;
		soap_begin_recv(&pResult->soapObj);

		if (!func(&pResult->soapObj, pResult->pObj, tagname, NULL))
		{
			cleanupSoapResultObj(&pResult->soapObj);
			free(pResult);
			PERFINFO_AUTO_STOP_FUNC();
			return NULL;
		}

		PERFINFO_AUTO_STOP_FUNC();
		return pResult;
	}

	PERFINFO_AUTO_STOP_FUNC();

	return NULL;
}

SA_RET_OP_STR const char * vindiciaGetFault(SA_PARAM_NN_STR const char *pResponse)
{
	static char *pFault = NULL;
	struct soap *soapObj = NULL;
	bool success;
	const char *err = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!verify(pResponse) || !verify(*pResponse))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Make sure this is valid UTF-8 data.
	success = UTF8StringIsValid(pResponse, &err);
	if (!success)
		AssertOrAlert("ACCOUNT_SERVER_UTF8", "Received invalid UTF-8 at position %lu", err ? (unsigned long)(err - pResponse) : 0);

	estrClear(&pFault);

	soapObj = callocStruct(struct soap);

	soap_init1(soapObj, SOAP_IO_CRYPTIC_STRING | SOAP_C_UTFSTRING);
	soapObj->fmalloc = btSoapAlloc;
	soap_begin(soapObj);
	soapObj->input = pResponse;
	soap_begin_recv(soapObj);
	
	soap_envelope_begin_in(soapObj);
	soap_recv_header(soapObj);
	soap_body_begin_in(soapObj);
	soap_getfault(soapObj);

	if (!soapObj->error)
	{
		const char **pFaultRaw = soap_faultstring(soapObj);
		if (pFaultRaw && *pFaultRaw)
			estrPrintf(&pFault, "%s", *pFaultRaw);
	}

	cleanupSoapResultObj(soapObj);
	free(soapObj);

	PERFINFO_AUTO_STOP_FUNC();
	return pFault;
}

void vindiciaXMLtoObjResultDestroy(VindiciaXMLtoObjResult **ppRes)
{
	VindiciaXMLtoObjResult *pResult = *ppRes;
	PERFINFO_AUTO_START_FUNC();
	cleanupSoapResultObj(&pResult->soapObj);
	free(pResult->pObj);
	free(pResult);
	*ppRes = NULL;
	PERFINFO_AUTO_STOP();
}

SA_RET_NN_VALID struct vin__Authentication * getVindiciaAuth(void)
{
	static struct vin__Authentication *auth = NULL;

	if(!auth)
	{
		auth = callocStruct(struct vin__Authentication);
		auth->login    = strdup(billingGetConfig()->vindiciaLogin);
		auth->password = strdup(billingGetConfig()->vindiciaPassword);
		auth->version  = VINDICIA_VERSION;
	}
	return auth;
}

#define VINDICIA_TYPE VX2O_OBJ
VindiciaXMLtoObjResult * vindiciaCreateResponse(BillingTransaction *pTrans, /* use VINDICIA_TYPE() for the rest */ int sizeofObj, SoapGetFunc func, const char *tagname)
{
	PERFINFO_AUTO_START_FUNC();
	if(pTrans->result == BTR_SUCCESS)
	{
		VindiciaXMLtoObjResult *pRes = vindiciaXMLtoObj(pTrans->response, sizeofObj, func, tagname);
		if(pRes)
		{
			PERFINFO_AUTO_STOP();
			return pRes;
		}
		else
		{
			pTrans->result = BTR_FAILURE;
			estrPrintf(&pTrans->resultString, "Received response, couldn't turn into type [%s]", tagname);
			AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE_XML_ERROR", "Received response, couldn't turn into type [%s]", tagname);
		}
	}

	PERFINFO_AUTO_STOP();
	return NULL;
}