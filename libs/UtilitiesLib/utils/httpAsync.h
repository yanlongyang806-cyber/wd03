#pragma once
GCC_SYSTEM

typedef struct NetComm NetComm;
typedef struct UrlArgument UrlArgument;
typedef struct UrlArgumentList UrlArgumentList;

typedef void(*haCallback)(const char *response, int len, int response_code, void *userdata);
typedef void(*haTimeout)(void *userdata);

// This function is intended to be used to perform an HTTPS request. The URL specified in
// (*argsIn)->pBaseURL MUST be of the form "https://host[:port][/path]". If the "https" protocol
// designation is not present, this function will devassert and return without doing anything.
//
// In order to perform an HTTPS request, you must have an stunnel host set up somewhere and
// configured to connect to the actual intended destination. You pass the hostname and port
// for that stunnel configuration to this function. The host and port specified in the URL here
// technically don't matter, since they're specified in the stunnel configuration.
U32 haSecureRequestEx(SA_PARAM_OP_VALID NetComm *comm,
	SA_PARAM_NN_STR const char *tunnel_host,
	U32 tunnel_port,
	SA_PARAM_NN_VALID UrlArgumentList **argsIn,
	SA_PARAM_NN_VALID haCallback cb,
	SA_PARAM_NN_VALID haTimeout timeout_cb,
	int timeout,
	SA_PARAM_OP_VALID void *userdata,
	bool bLogRequest);
#define haSecureRequestLogged(comm, tunnel_host, tunnel_port, argsIn, cb, timeout_cb, timeout, userdata) haSecureRequestEx(comm, tunnel_host, tunnel_port, argsIn, cb, timeout_cb, timeout, userdata, true)
#define haSecureRequest(comm, tunnel_host, tunnel_port, argsIn, cb, timeout_cb, timeout, userdata) haSecureRequestEx(comm, tunnel_host, tunnel_port, argsIn, cb, timeout_cb, timeout, userdata, false)

// This function provides UNSECURED HTTP CONNECTIONS ONLY
//
// This function is intended to be used to perform an unsecured HTTP request. The URL
// specified in (*argsIn)->pBaseURL MUST be of the form "[http://]host[:port][/path]". Any
// requested protocol designation other than "http" will cause the function to devassert and
// return without doing anything.
U32 haRequestEx(SA_PARAM_OP_VALID NetComm *comm,
	SA_PARAM_NN_VALID UrlArgumentList **argsIn,
	SA_PARAM_OP_VALID haCallback cb,
	SA_PARAM_OP_VALID haTimeout timeout_cb,
	int timeout,
	SA_PARAM_OP_VALID void *userdata,
	bool bLogRequest);
#define haRequestLogged(comm, argsIn, cb, timeout_cb, timeout, userdata) haRequestEx(comm, argsIn, cb, timeout_cb, timeout, userdata, true)
#define haRequest(comm, argsIn, cb, timeout_cb, timeout, userdata) haRequestEx(comm, argsIn, cb, timeout_cb, timeout, userdata, false)