#ifndef CRYPTIC_VINDICIA_H
#define CRYPTIC_VINDICIA_H

#define VINDICIA_VERSION			"3.4"
#define VINDICIA_SUCCESS_MESSAGE	"OK"
#define VINDICIA_TIMEOUT_GREEN_MAX	1000			// Will be green on the console if less than this
#define VINDICIA_TIMEOUT_YELLOW_MAX	2000			// Will be yellow on the console if less than this, otherwise red
#define VINDICIA_CHARGEBACK_PROBABILITY_MIN 0		// Minimum chargeback probability minimum
#define VINDICIA_CHARGEBACK_PROBABILITY_MAX 100		// Minimum chargeback probability maximum
#define VINDICIA_MIN_ALERT_DATA		10				// Minimum amount of data in a FloatAverager before alerting for low ratio
#define VINDICIA_MIN_ALERT_SECONDS	20				// Minimum amount of data in a FloatAverager before alerting for low ratio
#define ADMIN_IP "127.0.0.1"						// IP used for the admin interface for things like purchases
#define VINDICIA_DIVISION_KEY_NAME	"vin:Division"	// Used to set key-values on SOAP objects to route them to specific divisions

// Vindicia return codes
#define VINDICIA_SUCCESS_CODE			200			// Success
#define VINDICIA_PARTIAL_CONTENT		206			// Partial failure
#define VINDICIA_BAD_REQUEST_CODE		400			// Generic failure
#define VINDICIA_VALIDATION_FAILED		402			// Validation failure
#define VINDICIA_NOT_FOUND_CODE			404			// Not found, some used for arbitrary errors
#define VINDICIA_NOT_ALLOWED			405			// Operation not allowed
#define VINDICIA_SERVER_ERROR_CODE		500			// Internal server error, sometimes returned as a "record not found" response

// Core SOAP code
#include "wininclude.h"
#include "SOAPInterface/stdsoap2.h"

// Vindicia WSDL-generated wrappers
#include "SOAPInterface/VindiciaStructsH.h"

// ------------------------------------------------------------------------------------------------------------
// Helper macros and structs

typedef int (*SoapPutFunc)(struct soap*, const void *, const char*, const char*);
typedef void* (*SoapGetFunc)(struct soap*, void *, const char*, const char*);
#define VINDICIA_CALL_NAME(NS, OBJNAME) #NS ## ":" ## #OBJNAME
#define VO2X_OBJ(NS, OBJNAME) soap_put_ ## NS ## __ ## OBJNAME, VINDICIA_CALL_NAME(NS, OBJNAME)
#define VX2O_OBJ(NS, OBJNAME) sizeof( struct NS ## __ ## OBJNAME ), soap_get_ ## NS ## __ ## OBJNAME, VINDICIA_CALL_NAME(NS, OBJNAME)

typedef struct VindiciaXMLtoObjResult
{
	struct soap soapObj; // Used in object creation; Remembers what to free()
	void *pObj;          // Actual created object (free()'d in Destroy())
} VindiciaXMLtoObjResult;

// ------------------------------------------------------------------------------------------------------------
// Examples:
//     vindiciaObjtoXML(&estring_xml_output, pObj, VO2X_OBJ(objNS, objName));
//     res = vindiciaXMLtoObj(valid_xml_response,  VX2O_OBJ(objNS, objName));
//     vindiciaXMLtoObjResultDestroy(&res);

typedef struct BillingTransaction BillingTransaction;

bool vindiciaObjtoXML(char **estr, void *pObj, /* use VO2X_OBJ() for the rest */ SoapPutFunc func, const char *tagname);
SA_RET_OP_STR const char * vindiciaGetFault(SA_PARAM_NN_STR const char *pResponse);
static VindiciaXMLtoObjResult * vindiciaXMLtoObj(const char *xml, /* use VX2O_OBJ() for the rest */ int sizeofObj, SoapGetFunc func, const char *tagname);
void vindiciaXMLtoObjResultDestroy(VindiciaXMLtoObjResult **ppRes);

#define VINDICIA_TYPE VX2O_OBJ
struct VindiciaXMLtoObjResult * vindiciaCreateResponse(BillingTransaction *pTrans, /* use VINDICIA_TYPE() for the rest */ int sizeofObj, SoapGetFunc func, const char *tagname);

SA_RET_NN_VALID struct vin__Authentication * getVindiciaAuth(void);

#endif
