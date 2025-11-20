// Fulfill xfers using HTTP requests instead of PCL packets.

// This code reimplements most of client-side HTTP, on top of Cryptic NetLinks.  HttpClient could be
// used, except that it does not support keep-alive pipelining, which is crucial to good performance.
// The careful header parsing implementation from HttpLib is used, but HttpLib is not used otherwise.

#include <errno.h>

#include <WinError.h>

#include "BlockEarray.h"
#include "earray.h"
#include "estring.h"
#include "httputil.h"
#include "NameValuePair.h"
#include "net.h"
#include "patchdb.h"
#include "patchxfer.h"
#include "patchxferhttp.h"
#include "patchxferhttp_opt.h"
#include "pcl_client_internal.h"
#include "StringUtil.h"
#include "timing.h"
#include "timing_profiler.h"
#include "url.h"
#include "utilitiesLib.h"
#include "statistics.h"
#include "trivia.h"
#include "url.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FileSystem););

// See also defines in patchxferhttp_opt.c.

// Maximum number of tries to retry an xfer when an HTTP connection glitches.
#define PATCHXFERHTTP_MAX_HTTP_RETRIES 3

// Maximum number of tries to retry an xfer when that HTTP xfer fails.
#define PATCHXFERHTTP_MAX_HTTP_FAILS 3

// Total number of acceptable failures before aborting HTTP patching.
#define PATCHXFERHTTP_MAX_FAILURE_PERCENT 25
// Reset periodicity of failure checking
#define PATCHXFERHTTP_FAILURE_PERIOD 2000

// Maximum amount of problem reports to submit.
#define PATCHXFERHTTP_MAX_PROBLEM_REPORTS 10

// If requests have more than this many ranges, split the requests.
#define PATCHXFERHTTP_MAX_RANGES_PER_REQUEST 25


// ************************
// Debugging stuff

// Extra HTTP xfer verification.
#ifdef PATCHXFERHTTP_EXTRA_VERIFY
#define debugverifyhttpbytes verifyhttpbytes
#else
#define debugverifyhttpbytes(X) ((void)0);
#endif

// If set, send "whole file" requests as ranges.
bool patchxferhttpforcerange = false;
AUTO_CMD_INT(patchxferhttpforcerange, patchxferhttpforcerange);

// If set, send all requests as a request for the whole file, even if we only need a subrange.
bool patchxferhttpforcerangeaswholefile = false;
AUTO_CMD_INT(patchxferhttpforcerangeaswholefile, patchxferhttpforcerangeaswholefile);

// End of debugging stuff
// ************************


// Link disconnect reason to use for the peer closing the keep-alive connection.
#define PATCHXFERHTTP_SERVER_REQUEST_CLOSE "Server requested close"


// Add some basic headers to a request.
static void xferHttpAppendBasicHeaders(char **estrRequest, const char *filename, PatchXferrer *xferrer)
{
	estrConcatf(estrRequest, "User-Agent: CrypticPatchClient/(%s)\r\n", GetUsefulVersionString());
	if(!xferrer->http_server || !xferrer->http_server[0])
		estrAppend2(estrRequest, "Host:\r\n");
	else if(xferrer->http_port == 0 || xferrer->http_port == 80)
		estrConcatf(estrRequest, "Host: %s\r\n", xferrer->http_server);
	else
		estrConcatf(estrRequest, "Host: %s:%u\r\n", xferrer->http_server, xferrer->http_port);
}

// Generate request.
static void xferHttpGenerateRequest(char **estrRequest, const char *filename, PatchXferrer *xferrer, const char *ranges)
{
	char *escaped = NULL;

	estrStackCreate(&escaped);
	urlEscape(filename, &escaped, false, true);
	estrConcatf(estrRequest, "GET %s HTTP/1.1\r\n", escaped);
	estrDestroy(&escaped);
	xferHttpAppendBasicHeaders(estrRequest, filename, xferrer);
	if (ranges)
		estrConcatf(estrRequest, "Range: bytes=%s\r\n", ranges);
	estrAppend2(estrRequest, "\r\n");
}

static PatchXferrerHttpConnection *xferHttpFindXferInConnections(PatchXferrer *xferrer, PatchXfer *xfer);

// Create a new connection.
// Note: This does not actually create the NetLink.  It will be created later, as it is needed.
PatchXferrerHttpConnection *xferHttpCreateConnection(PatchXferrer *xferrer, const char *http_server, U16 port, const char *prefix)
{
	PatchXferrerHttpConnection *connection = calloc(1, sizeof(PatchXferrerHttpConnection));
	connection->xferrer = xferrer;
	if (http_server)
	{
		connection->http_server = http_server ? strdup(http_server) : NULL;
		connection->http_port = port;
		connection->path_prefix = prefix ? strdup(prefix) : NULL;
	}
	connection->net_bytes_free = xferrer->max_net_bytes;
	eaPush(&xferrer->http_connections, connection);
	debugverifyhttpbytes(xferrer);
	return connection;
}

// Print some interesting trivia.
static void xferHttpPrintTrivia(PatchXferrer *xferrer)
{
	triviaPrintf("http_server", "%s", xferrer->http_server);
	triviaPrintf("http_port", "%d", xferrer->http_port);
	triviaPrintf("requests", "%d", xferrer->http_requests_reset);
	triviaPrintf("fails", "%d", xferrer->http_fails_reset);
	triviaPrintf("http_fails", "%d", xferrer->http_fails);
	triviaPrintf("http_bytes_received", "%"FORM_LL"u", xferrer->http_bytes_received);
}

// Report a problem to the Patch Server.
#define xferHttpXferReportProblemf(xferrer, xfer, key, format, ...) \
	xferHttpXferReportProblemf_dbg(xferrer, xfer, key, FORMAT_STRING_CHECKED(format), __VA_ARGS__)
static void xferHttpXferReportProblemf_dbg(PatchXferrer *xferrer, PatchXfer *xfer,
										   const char *key, FORMAT_STR const char *format, ...)
{
	if (xferrer->http_problem_reports++ < PATCHXFERHTTP_MAX_PROBLEM_REPORTS)
	{
		char *details = NULL;
		bool lastreport = xferrer->http_problem_reports == PATCHXFERHTTP_MAX_PROBLEM_REPORTS;
		estrStackCreate(&details);
		estrGetVarArgs(&details, format);
		xferHttpPrintTrivia(xferrer);
		if (xfer->http_header_debug)
			triviaPrintf("http_headers", "%s", xfer->http_header_debug);
		if (xfer->http_mime_debug)
			triviaPrintf("mime_headers", "%s", xfer->http_mime_debug);
		if (lastreport)
			triviaPrintf("last_report", "1");
		if (xfer->http_request_debug)
			triviaPrintf("http_request_debug", "%s", xfer->http_request_debug);
		if (xfer->http_range_debug)
			triviaPrintf("http_range_debug", "%s", xfer->http_range_debug);
		ErrorDetailsf("(%s) %s", xfer->filename_to_get, details);
		Errorf("HTTP patching problem (%s): %s", key, "");
		pcllog(xferrer->client, PCLLOG_WARNING, "HTTP patching problem (%s) \"%s\": %s", xfer->filename_to_get, key, details);
		pclSendLog(xferrer->client, "PCLHttpProblem", "key \"%s\" details \"%s\" filename \"%s\" headers \"%s\" mime \"%s\" request \"%s\" range \"%s\"",
			key, details, xfer->filename_to_get,
			xfer->http_header_debug ? xfer->http_header_debug : "", xfer->http_mime_debug ? xfer->http_mime_debug : "",
			xfer->http_request_debug ? xfer->http_request_debug : "", xfer->http_range_debug ? xfer->http_range_debug : "");
		estrDestroy(&details);
	}
}

// Record an xfer failure.  This may cause fail-over to PCL.
void xferHttpXferRecordFail(PatchXferrer *xferrer, PatchXfer *xfer, const char *reason)
{
	PCL_LogLevel loglevel = PCLLOG_FILEONLY;
	devassert(!xfer->http_sent);

	// Record per-xfer fail statistics.
	++xfer->http_fail;

	// Record per-xferrer failure statistics
	++xferrer->http_fails_reset;
	++xferrer->http_fails;

	// Log failure.
	if (xfer->http_fail == PATCHXFERHTTP_MAX_HTTP_FAILS)
		loglevel = PCLLOG_WARNING;
	pcllog(xferrer->client, loglevel, "HTTP patching failed for %s: %s", xfer->filename_to_get, reason);

	// If there have been too many HTTP failures, give up.
	if (xferrer->http_requests_reset > PATCHXFERHTTP_FAILURE_PERIOD)
	{
		if (100 * xferrer->http_fails_reset > PATCHXFERHTTP_MAX_FAILURE_PERCENT * xferrer->http_requests_reset)
		{
			pcllog(xferrer->client, PCLLOG_ERROR, "Excessive HTTP patching failures: requests %d fails %d", xferrer->http_requests_reset,
				xferrer->http_fails_reset);
			pclSendLog(xferrer->client, "PCLHttpTooManyFailures", "");
			xferHttpPrintTrivia(xferrer);
			Errorf("Excessive HTTP patching failures");
			xferrer->use_http = false;
		}
		xferrer->http_fails_reset = 0;
		xferrer->http_requests_reset = 0;
	}
}

// Abort a failed HTTP xfer.
static void xferHttpXferFail(PatchXferrer *xferrer, PatchXferrerHttpConnection *connection, PatchXfer *xfer, const char *reason)
{
	// Shut down HTTP patching.
	xfer->http_sent = false;
	xfer->split_requests = 0;
	xferHttpOkToRequestBytes(xferrer, xfer, &connection, -(xfer->http_bytes_requested + xfer->http_overload_bytes), false);
	xferClear(xfer);
	xfer->state = XFER_REQ_FILEINFO;

	// Record the failure.
	xferHttpXferRecordFail(xferrer, xfer, reason);
}

// Reset an HTTP request.
static void xferHttpXferRetry(PatchXferrer *xferrer, PatchXferrerHttpConnection *connection, PatchXfer *xfer)
{
	if (xfer->http_retries > PATCHXFERHTTP_MAX_HTTP_RETRIES)
	{
		xferHttpXferFail(xferrer, connection, xfer, "too many retries on HTTP xfer");
		return;
	};

	--xferrer->http_requests_reset;
	xfer->http_sent = false;
	++xfer->http_retries;
	xferHttpOkToRequestBytes(xferrer, xfer, &connection, -(xfer->http_bytes_requested + xfer->http_overload_bytes), false);
}

// Return true if we should use HTTP for this xfer.
bool xferHttpShouldUseHttp(PatchXferrer *xferrer, PatchXfer *xfer)
{
	return xferrer->use_http && xfer->http_fail < PATCHXFERHTTP_MAX_HTTP_FAILS
		&& xfer->actual_rev >= 0 && !xfer->slipstream_available;
}

// Zero net statistics.
static void xferHttpZeroNetStats(PatchXferrer *xferrer)
{
	EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
	{
		xferrer->http_connections[i]->http_stat_first_req_sent = 0;
		xferrer->http_connections[i]->http_stat_req_sent = 0;
	}
	EARRAY_FOREACH_END;
	beaSetSize(&xferrer->http_vec_first_req, 0);
	beaSetSize(&xferrer->http_vec_sub_req, 0);
	beaSetSize(&xferrer->http_vec_reqs_sent, 0);
	xferrer->http_stat_reqs_sent = 0;
	beaSetSize(&xferrer->http_vec_reqs_recv, 0);
	xferrer->http_stat_reqs_recv = 0;
	beaSetSize(&xferrer->http_vec_sent_total, 0);
	xferrer->http_stat_sent_total = 0;
	beaSetSize(&xferrer->http_vec_sent_error, 0);
	xferrer->http_stat_sent_error = 0;
	beaSetSize(&xferrer->http_vec_recv_total, 0);
	xferrer->http_stat_recv_total = 0;
	beaSetSize(&xferrer->http_vec_recv_overhead, 0);
	xferrer->http_stat_recv_overhead = 0;
	beaSetSize(&xferrer->http_vec_recv_error, 0);
	xferrer->http_stat_recv_error = 0;
	beaSetSize(&xferrer->http_vec_ranges, 0);
	xferrer->http_stat_ranges = 0;
	beaSetSize(&xferrer->http_vec_multiranges, 0);
	xferrer->http_stat_multiranges = 0;
}

// Collect some HTTP network transfer statistics.
void xferHttpRecordNetStats(PatchXferrer *xferrer)
{
	*(U64 *)beaPushEmpty(&xferrer->http_vec_reqs_sent) = xferrer->http_stat_reqs_sent;
	xferrer->http_stat_reqs_sent = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_reqs_recv) = xferrer->http_stat_reqs_recv;
	xferrer->http_stat_reqs_recv = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_sent_total) = xferrer->http_stat_sent_total;
	xferrer->http_stat_sent_total = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_sent_error) = xferrer->http_stat_sent_error;
	xferrer->http_stat_sent_error = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_recv_total) = xferrer->http_stat_recv_total;
	xferrer->http_stat_recv_total = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_recv_overhead) = xferrer->http_stat_recv_overhead;
	xferrer->http_stat_recv_overhead = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_recv_error) = xferrer->http_stat_recv_error;
	xferrer->http_stat_recv_error = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_ranges) = xferrer->http_stat_ranges;
	xferrer->http_stat_ranges = 0;
	*(U64 *)beaPushEmpty(&xferrer->http_vec_multiranges) = xferrer->http_stat_multiranges;
	xferrer->http_stat_multiranges = 0;
}

// Collect some HTTP network transfer statistics.
void xferHttpReportNetStats(PatchXferrer *xferrer, char **estrLogLine)
{
	statisticsLogStatsF32(estrLogLine, xferrer->http_vec_first_req, "first_req");
	statisticsLogStatsF32(estrLogLine, xferrer->http_vec_sub_req, "sub_req");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_reqs_sent, "reqs_sent");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_reqs_recv, "reqs_recv");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_sent_total, "sent_total");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_sent_error, "sent_error");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_recv_total, "recv_total");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_recv_overhead, "recv_overhead");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_ranges, "ranges");
	statisticsLogStatsU64(estrLogLine, xferrer->http_vec_multiranges, "multiranges");
	xferHttpZeroNetStats(xferrer);
}

// Destroy a HTTP connection.
void xferHttpDestroyConnection(PatchXferrer *xferrer, PatchXferrerHttpConnection *connection)
{
	bool removed = false;
	debugverifyhttpbytes(xferrer);
	EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
	{
		if (xferrer->http_connections[i] == connection)
		{
			eaRemove(&xferrer->http_connections, i);
			removed = true;
			break;
		}
	}
	EARRAY_FOREACH_END;
	if (!removed)
	{
		devassert(removed);
		return;
	}
	if (connection->http_link)
	{
		linkSetUserData(connection->http_link, NULL);
		linkRemove_wReason(&connection->http_link, "destroying");
	}
	estrDestroy(&connection->http_response);
	eaDestroy(&connection->http_requests);
	free(connection->separator);
	free(connection);
	debugverifyhttpbytes(xferrer);
}

// Remove an HTTP xfer from its connection.
void xferHttpRemove(PatchXferrer * xferrer, PatchXfer * xfer)
{
	debugverifyhttpbytes(xferrer);
	if (!xfer->use_http)
		return;
	EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
	{
		EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections[i]->http_requests, j, m);
		{
			if (xferrer->http_connections[i]->http_requests[j] == xfer)
			{
				eaRemove(&xferrer->http_connections[i]->http_requests, j);
				debugverifyhttpbytes(xferrer);
				return;
			}
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;
	devassertmsg(0, "http xfer not found");
}

// Return total number of bytes received.
U64 xferHttpBytesReceived(PatchXferrer *xferrer)
{
	U64 count = 0;
	EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
	{
		if (xferrer->http_connections[i]->http_link)
		{
			const LinkStats *stats = linkStats(xferrer->http_connections[i]->http_link);
			count += stats->recv.real_bytes;
		}
	}
	EARRAY_FOREACH_END;
	return count;
}

// Parse a response range.
static bool xferHttpParseRange(const char *range, unsigned long *begin, unsigned long *end, unsigned long *size, bool *bad_unit)
{
	static const char bytes_range[] = "bytes ";
	const char *i;
	const char *next;

	// Check unit.
	*bad_unit = false;
	if (!strStartsWith(range, bytes_range))
	{
		*bad_unit = true;
		return false;
	}
	i = range + sizeof(bytes_range) - 1;

	// Get begin.
	errno = 0;
	*begin = strtoul(i, (char **)&next, 10);
	if (errno)
		return false;
	i = next;

	// Get end.
	if (*i != '-')
		return false;
	++i;
	*end = strtoul(i, (char **)&next, 10);
	if (errno)
		return false;
	i = next;

	// Get size.
	if (*i != '/')
		return false;
	++i;
	if (*i == '*')
	{
		*size = -1;
		++i;
	}
	else
	{
		*size = strtoul(i, (char **)&next, 10);
		if (errno)
			return false;
		i = next;
	}
	if (*i)
		return false;
	return true;
}

// Save headers for debugging later, in quoted form.
static void xferHttpSaveDebugHeaders(PatchXfer *xfer, const char *headers, const char *headers_end)
{
	char *shortHeaders = NULL;
	const int max_headers = 4*1024;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&shortHeaders);
	estrConcat(&shortHeaders, headers, MIN(headers_end - headers, max_headers));
	estrClear(&xfer->http_header_debug);
	estrAppendEscaped(&xfer->http_header_debug, shortHeaders);
	estrDestroy(&shortHeaders);

	PERFINFO_AUTO_STOP_FUNC();
}

// Parse the HTTP response header.
static bool xferHttpHandleHeader(PatchXferrerHttpConnection *connection, PatchXfer *xfer, unsigned long *content_length, int *header_size)
{
	PatchXferrer *xferrer = connection->xferrer;
	NetLink *link = connection->http_link;
	const char *connection_str;
	char *header_end;
	char c;
	HttpResponse *response;
	const char *content_length_str;
	const char *content_length_str_end;
	bool connection_close;
	const char *content_transfer_encoding;
	const char *transfer_encoding;
	const char *content_range;
	bool bad_unit = false;
	const char *content_type;
	const char content_type_multirange[] = "multipart/byteranges;";

	PERFINFO_AUTO_START_FUNC();

	pclMSpf("xferHttpHandleHeader %s", xfer->filename_to_write);

	// Locate headers.
	header_end = strstr(connection->http_response, "\r\n\r\n");
	if (!header_end)
	{
		// Incomplete header; wait for the rest to arrive.
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}
	header_end += 4;
	c = header_end[0];
	header_end[0] = 0;
	xferHttpSaveDebugHeaders(xfer, connection->http_response, header_end);

	// Parse HTTP response header.
	response = hrParseResponse(connection->http_response);
	header_end[0] = c;
	if (!response)
	{
		xferHttpXferReportProblemf(xferrer, xfer,
			"InvalidHeader", "");
		linkRemove_wReason(&link, "Received invalid HTTP header");
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Check status code
	if(response->code != 200 && !(xfer->sent_range_request && response->code == 206))
	{
		xferHttpXferReportProblemf(xferrer, xfer,
			"BadHttpResponse", "code %d reason %s", response->code, response->reason);
		linkRemove_wReason(&link, "Response code not success");
		StructDestroySafe(parse_HttpResponse, &response);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Get Content-Length.
	*header_size = header_end - connection->http_response;
	content_length_str = hrpFindHeader(response, "Content-Length");
	if (!content_length_str)
	{
		xferHttpXferReportProblemf(xferrer, xfer,
			"ContentLengthMissing", "");
		linkRemove_wReason(&link, "Content-Length missing");
		StructDestroySafe(parse_HttpResponse, &response);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}
	errno = 0;
	*content_length = strtoul(content_length_str, (char **)&content_length_str_end, 10);
	if (errno || *content_length_str_end || *content_length > INT_MAX)
	{
		xferHttpXferReportProblemf(xferrer, xfer,
			"ContentLengthInvalid", "string \"%s\"", content_length_str);
		linkRemove_wReason(&link, "Invalid Content-Length");
		StructDestroySafe(parse_HttpResponse, &response);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Check if the server is requesting that the connection be closed.
	connection_str = hrpFindHeader(response, "Connection");
	connection_close = !stricmp_safe(connection_str, "close");
	if (connection_close && !connection->http_closed)
	{
		connection->http_closed = true;
		EARRAY_CONST_FOREACH_BEGIN(connection->http_requests, i, n);
		{
			if (i)
				xferHttpXferRetry(xferrer, connection, connection->http_requests[i]);
		}
		EARRAY_FOREACH_END;
		eaRemoveRange(&connection->http_requests, 1, eaSize(&connection->http_requests) - 1);
	}

	// Make sure there's no entity transformation.
	content_transfer_encoding = hrpFindHeader(response, "Content-Transfer-Encoding");
	transfer_encoding = hrpFindHeader(response, "Transfer-Encoding");
	if (content_transfer_encoding || transfer_encoding)
	{
		xferHttpXferReportProblemf(xferrer, xfer,
			"TransferEncoding", "\"%s\" \"%s\"", content_transfer_encoding, transfer_encoding);
		linkRemove_wReason(&link, "Server tried to use a content encoding");
		StructDestroySafe(parse_HttpResponse, &response);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Check for a range response.
	content_range = hrpFindHeader(response, "Content-Range");
	if (content_range)
	{
		unsigned long begin, end, size;
		if (!xfer->sent_range_request)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"UnexpectedRange", "%s", content_range);
			linkRemove_wReason(&link, "Got Content-Range but did not ask for a range");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		if (!xferHttpParseRange(content_range, &begin, &end, &size, &bad_unit))
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				bad_unit ? "UnknownRangeUnit" : "RangeParseError", "%s", content_range);
			linkRemove_wReason(&link, bad_unit ? "Unknown range unit" : "Unknown to parse range");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		if (size == -1)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"RangeUnknownSize", "",);
			linkRemove_wReason(&link, "Server reports entity size is unknown, which we do not support");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		if (xfer->com_len != size)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"RangeEntitySizeIncorrect", "expected %d actual %d", xfer->com_len, size);
			linkRemove_wReason(&link, "Incorrect entity size in Range");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		connection->range_total_size = end - begin + 1;
		if (connection->range_total_size != *content_length)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"ContentLengthWrongForRange", "");
			linkRemove_wReason(&link, "Content-Length doesn't match Range size");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		connection->range_response = true;
		connection->entity_position = begin;
	}

	// Check for a multipart range response.
	content_type = hrpFindHeader(response, "Content-Type");
	if (content_type && strStartsWith(content_type, content_type_multirange))
	{
		const char boundary_specifier[] = "boundary=";
		const char *boundary = strstri(content_type += sizeof(content_type_multirange) - 1, boundary_specifier);
		if (!xfer->sent_multi_range)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"UnexpectedMultirange", "content_type %s", content_type);
			linkRemove_wReason(&link, "Got multipart/byteranges but did not ask for multiple ranges");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		devassert(xfer->sent_range_request);
		if (content_range)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"MultipartRangeWithRange", "");
			linkRemove_wReason(&link, "multipart/byteranges with Content-Range");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		if (!boundary)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"ByterangesMissingBoundary", "");
			linkRemove_wReason(&link, "multipart/byteranges missing boundary specifier");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		boundary += sizeof(boundary_specifier) - 1;
		if (!boundary || !*boundary)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"ByterangesEmptyBoundary", "");
			linkRemove_wReason(&link, "multipart/byteranges empty boundary specifier");
			StructDestroySafe(parse_HttpResponse, &response);
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		connection->separator = strdupf("\r\n--%s", boundary);
		connection->range_response = true;
		connection->multirange_response = true;
	}
	if (response->code == 200 && content_range || response->code == 206 && !content_range && !connection->multirange_response)
	{
		xferHttpXferReportProblemf(xferrer, xfer,
			"ResponseCodeMismatch", "code %d", response->code);
		linkRemove_wReason(&link, "Range header presence does not match response code");
		StructDestroySafe(parse_HttpResponse, &response);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Verify length.
	devassert(xfer->get_compressed);
	if (!connection->range_response && xfer->com_len != *content_length)
	{
		xferHttpXferReportProblemf(xferrer, xfer,
			"ContentLengthIncorrect", "expected %d actual %d", xfer->com_len, *content_length);
		linkRemove_wReason(&link, "Incorrect Content-Length");
		StructDestroySafe(parse_HttpResponse, &response);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Record header statistics.
	if (connection->range_response)
		++xferrer->http_stat_ranges;
	if (connection->multirange_response)
		++xferrer->http_stat_multiranges;

	// Everything parsed OK, continue parsing.
	PERFINFO_AUTO_STOP_FUNC();
	StructDestroySafe(parse_HttpResponse, &response);
	return true;
}

// Helper function for xferHttpNextSeparator(): return the location of a separator, or null if not found.
static char *xferHttpFindSeparator(char *body, const char *separator, bool final)
{
	char *result;
	char *full_separator = NULL;
	estrStackCreate(&full_separator);
	estrPrintf(&full_separator, "%s%s\r\n", separator, final ? "--" : "");
	result = memstr(body, full_separator, estrLength(&body));
	estrDestroy(&full_separator);
	return result;
}

// Find the next MIME separator.
static bool xferHttpNextSeparator(char *body, const char *separator, char **boundary, char **end, bool *final)
{
	char *normal_separator;
	char *final_separator;

	PERFINFO_AUTO_START_FUNC();

	normal_separator = xferHttpFindSeparator(body, separator, false);
	final_separator = xferHttpFindSeparator(body, separator, true);

	if (!normal_separator && !final_separator)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	if (normal_separator && (!final_separator || normal_separator < final_separator))
	{
		*boundary = normal_separator;
		*end = normal_separator + 2;
		*final = false;
	}
	else
	{
		*boundary = final_separator;
		*end = final_separator + 4;
		*final = true;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

// Return true if the final block in the final block request of an xfer is smaller than a normal-sized block.
static bool xferHttpIsFinalReqBlockSmall(PatchXfer *xfer)
{
	U32 block_size = xfer->print_sizes[xfer->print_idx];
	U32 last_block_req = xfer->num_block_reqs - 1;
	bool small;
	devassert(xfer->get_compressed);
	small = (xfer->block_reqs[last_block_req*2] + xfer->block_reqs[last_block_req*2+1])*block_size > xfer->com_len;
	devassert(!small || xfer->com_len % block_size);
	return small;
}

// Save MIME part headers for debugging later, in quoted form.
static void xferHttpSaveDebugMime(PatchXfer *xfer, const char *headers, const char *headers_end)
{
	char *shortHeaders = NULL;
	const int max_headers = 4*1024;

	PERFINFO_AUTO_START_FUNC();

	estrStackCreate(&shortHeaders);
	estrConcat(&shortHeaders, headers, MIN(headers_end - headers, max_headers));
	estrClear(&xfer->http_mime_debug);
	estrAppendEscaped(&xfer->http_mime_debug, shortHeaders);
	estrDestroy(&shortHeaders);

	PERFINFO_AUTO_STOP_FUNC();
}

// Update a connection's entity_position.
// Returning false indicates that not enough data was available to update the position, unless error is also set,
// which indicates there was a syntactical problem with the stream that will not be corrected by waiting for
// more data.
// Sadly, this partially duplicates the header parsing in HttpClient and HttpLib, but until there is some
// unified, bulletproof, high-performance Cryptic HTTP 1.1 library, it is necessary.
static bool xferHttpUpdateConnectionPosition(PatchXferrerHttpConnection *connection, PatchXfer *xfer, const char **error)
{
	PERFINFO_AUTO_START_FUNC();

	// Only multirange responses need positon adjustment.
	if (connection->multirange_response)
	{
		if (!connection->in_mime_part)
		{
			static const char content_range_prefix[] = "Content-Range:";
			char *boundary;
			char *pos;
			char *header_end;
			bool final;
			char *content_range_header;
			char *content_range_header_end;
			char *content_range = NULL;
			bool bad_unit = false;
			unsigned long begin, end, size;
			bool success;
			int trim_amount;

			// Find the next separator.
			success = xferHttpNextSeparator(connection->http_response, connection->separator, &boundary, &pos, &final);
			// FIXME: Perhaps we should do something when connection->http_response != boundary.
			if (!success)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
			if (final)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return true;
			}

			// Find the end of the headers.
			header_end = strstr(pos, "\r\n\r\n");
			if (!header_end)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
			xferHttpSaveDebugMime(xfer, pos, header_end + 4);
			
			// Find the Content-Range header.
			*header_end = 0;
			content_range_header = strstri(pos, content_range_prefix);
			*header_end = '\r';
			if (!content_range_header)
			{
				*error = "Part missing Content-Range";
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
			content_range_header += sizeof(content_range_prefix) - 1;
			while (*content_range_header == ' ' || *content_range_header == '\t')
				++content_range_header;
			content_range_header_end = strstr(content_range_header, "\r\n");
			while (content_range_header_end > content_range_header && 
				(content_range_header_end[-1] == ' ' || content_range_header_end[-1] == '\t'))
				--content_range_header_end;
			if (header_end - connection->http_response + 4 + connection->content_position > connection->content_length)
			{
				*error = "Unterminated MIME body in response";
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}

			// Parse the Content-Range header.
			estrStackCreate(&content_range);
			estrConcat(&content_range, content_range_header, content_range_header_end - content_range_header);
			success = xferHttpParseRange(content_range, &begin, &end, &size, &bad_unit);
			estrDestroy(&content_range);
			if (!success)
			{
				if (bad_unit)
					*error = "Unknown range unit";
				else
					*error = "Unable to parse MIME part range header";
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}

			// Get the position.
			if (size != xfer->com_len)
			{
				*error = "Range part entity size mismatch";
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
			connection->range_total_size = end - begin + 1;
			connection->entity_position = begin;
			connection->range_position = 0;

			// Trim the header.
			trim_amount = header_end - connection->http_response + 4;
			connection->content_position += trim_amount;
			estrRemove(&connection->http_response, 0, trim_amount);

			// We have entered the body of the MIME part.
			connection->in_mime_part = true;
			connection->xferrer->http_mime_bytes += trim_amount;
		}
	}

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

// Remove amount of bytes from the beginning of the HTTP response.
static void xferHttpResponseTrimLeadingBytes(PatchXferrerHttpConnection *connection, PatchXfer *xfer, int amount)
{
	PERFINFO_AUTO_START_FUNC();
	connection->content_position += amount;
	devassert(connection->content_position <= connection->content_length);
	connection->entity_position += amount;
	devassert(connection->entity_position <= (int)xfer->com_len);
	if (connection->range_response)
	{
		connection->range_position += amount;
		devassert(connection->range_position <= connection->range_total_size);
	}
	devassert((int)estrLength(&connection->http_response) >= amount);
	estrRemove(&connection->http_response, 0, amount);
	PERFINFO_AUTO_STOP_FUNC();
}

// devassert if the bytes_request doesn't match the number of blocks received.
static void xferHttpXferAssertBytesRequestedIsConsistent(PatchXfer *xfer)
{
	U32 block_size = xfer->print_sizes[xfer->print_idx];
	int last_block;
	int requested;
	int block_total;

	// Calculate the size of the last block.
	last_block = xfer->com_len % block_size;
	if (!last_block || !xferHttpIsFinalReqBlockSmall(xfer))
		last_block = block_size;

	// Special case if all blocks are transferred.
	if (xfer->blocks_so_far == xfer->blocks_total)
	{
		devassert(xfer->http_bytes_requested == 0 && xfer->http_overload_bytes == 0);
		return;
	}
	devassert(xfer->blocks_so_far < xfer->blocks_total);

	// Outstanding blocks should match bytes requested.
	requested = xfer->http_bytes_requested + xfer->http_overload_bytes;
	block_total = (xfer->blocks_total - xfer->blocks_so_far - 1)*block_size + last_block;
	devassert(requested == block_total);
}

// Copy data from the HTTP response into the xfer.
static void xferHttpFillXferFromResponse(PatchXferrer *xferrer, PatchXfer *xfer, PatchXferrerHttpConnection *connection, U32 start_block, U32 available_bytes)
{
	U32 block_size;
	U32 bytes_to_copy;
	U32 blocks_to_copy;

	PERFINFO_AUTO_START_FUNC();

	devassert(xfer->get_compressed);

	// Determine how much should be copied.
	block_size = xfer->print_sizes[xfer->print_idx];
	blocks_to_copy = available_bytes/block_size;
	bytes_to_copy = blocks_to_copy * block_size;
	if (xferHttpIsFinalReqBlockSmall(xfer) && xfer->blocks_so_far + 1 == xfer->blocks_total && available_bytes - bytes_to_copy == xfer->com_len % block_size)
	{
		++blocks_to_copy;
		bytes_to_copy = available_bytes;
	}

	// Copy the data.
	memcpy(xfer->new_data + start_block * block_size, connection->http_response, bytes_to_copy);

	// Update the xfer.
	xferHttpXferAssertBytesRequestedIsConsistent(xfer);
	xferHttpOkToRequestBytes(xferrer, xfer, &connection, -(int)bytes_to_copy, false);
	xfer->blocks_so_far += blocks_to_copy;
	xferHttpXferAssertBytesRequestedIsConsistent(xfer);
	devassert((int)xfer->blocks_so_far < xfer->blocks_total || xfer->bytes_requested == 0);

	// Remove the copied data from the HTTP response buffer.
	xferrer->http_body_bytes += bytes_to_copy;
	xfer->total_bytes_received += bytes_to_copy;
	xferHttpResponseTrimLeadingBytes(connection, xfer, bytes_to_copy);

	PERFINFO_AUTO_STOP_FUNC();
}

// Extract file patch data from the body.
//
// The position of the first remaining byte of the response is tracked in three logical streams:
// xfer->content_position - Position within the HTTP body
// xfer->entity_position - Position within the actual file being transferred (different from content_position for range responses)
// xfer->range_position - Position within the particular range or subpart range currently being processed (mostly interesting for multipart range responses)
//
// Note: When we request multiple ranges from the server, we request them in file order, and expect
// them to come back in file order.  It is OK if the ranges come back altered in various ways, such as
// made larger, merged, or promoted to a single range or entire file response, but the ranges do
// need to come back in order.  If this does not happen, HTTP patching will fail for that file, and have
// to be retried without ranges.  Additionally, each compressed block must lie within exactly
// one subpart block.  These restrictions can be lifted by complicating the implementation with
// some additional tracking, which we will do if we find a web server that needs it.
static bool xferHttpHandleBody(PatchXferrerHttpConnection *connection, PatchXfer *xfer)
{
	PatchXferrer *xferrer;
	bool compressed;
	U32 block_size;
	NetLink *link;

	PERFINFO_AUTO_START_FUNC();

	xferrer = connection->xferrer;
	compressed = xfer->get_compressed;
	block_size = xfer->print_sizes[xfer->print_idx];
	link = connection->http_link;

	for (;;)
	{
		U32 req_index, block_index;
		U32 start_block;
		U32 block_count;
		bool success;
		const char *error = NULL;
		int start_block_bytes;
		U32 bytes_to_copy, bytes_left_to_copy_in_req;

		// Update position.
		// This will trim any non-data stuff, such as multipart MIME headers.
		devassert(connection->content_position <= connection->content_length);
		devassert(connection->entity_position <= (int)xfer->com_len);
		devassert(connection->range_position <= connection->range_total_size);
		success = xferHttpUpdateConnectionPosition(connection, xfer, &error);
		if (!success)
		{
			if (error)
			{
				xferHttpXferReportProblemf(xferrer, xfer,
					"PositionUpdateError", "%s", error);
				linkRemove_wReason(&link, error);
			}
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		// Check if we're at the last separator for a multi-part response.
		if (connection->multirange_response && !connection->in_mime_part)
		{
			char *boundary;
			char *boundary_end;
			bool final;

			// Wait until the entire rest of the body has arrived.
			if (connection->content_position + (int)estrLength(&connection->http_response) < connection->content_length)
				return false;

			// Verify that the final separator is OK.
			success = xferHttpNextSeparator(connection->http_response, connection->separator, &boundary, &boundary_end, &final);
			devassert(success && final);

			// Remove the rest of this response.
			estrRemove(&connection->http_response, 0, connection->content_length - connection->content_position);
			connection->content_position = connection->content_length;
			break;																										// Exit.
		}

		// Validate position and sizes.
		// At this point, from the beginning of the response
		// to the smaller of range_total_size (for ranges) or content_length (for a complete response)
		// is valid data, subject to the amount of data actually in the response buffer, and any amount already
		// trimmed off the front.
		if (connection->range_response && connection->range_total_size + connection->content_position - connection->range_position > connection->content_length
			|| connection->multirange_response && connection->range_total_size + connection->content_position - connection->range_position >= connection->content_length
			|| connection->range_response && connection->range_total_size + connection->entity_position - connection->range_position > (int)xfer->com_len)
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"PositionOverflow", "range_total_size %d content_position %d content_length %d entity_position %d range_position %d com_len %lu",
				connection->range_total_size,
				connection->content_position,
				connection->content_length,
				connection->entity_position,
				connection->range_position,
				xfer->com_len);
			linkRemove_wReason(&link, "Position overflow");
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		// If we're done receiving the file, just trim everything until we get to the end of the response.
		// If there is nothing to trim, this xfer is done.
		if (xfer->http_bytes_requested == 0)
		{
			int amount_to_trim = MIN((int)estrLength(&connection->http_response),
				connection->content_length - connection->content_position);
			amount_to_trim = MIN(amount_to_trim, connection->range_total_size - connection->range_position);
			if (amount_to_trim)
				xferHttpResponseTrimLeadingBytes(connection, xfer, amount_to_trim);
			xferrer->http_extra_bytes += amount_to_trim;
			if (connection->content_position == connection->content_length)
				break;																										// Exit.
			if (connection->range_position == connection->range_total_size)
			{
				connection->in_mime_part = false;
				continue;
			}
			PERFINFO_AUTO_STOP_FUNC();
		}

		// Find current block.
		xferHttpGetCurrentBlock(xfer, &req_index, &block_index);
		start_block = xfer->block_reqs[2*req_index] + block_index;
		block_count = xfer->block_reqs[2*req_index+1];
		start_block_bytes = start_block * block_size;

		// Trim any unnecessary leading bytes.
		if (connection->entity_position < start_block_bytes)
		{
			int extra = start_block_bytes - connection->entity_position;
			extra = MIN(extra, connection->content_length - connection->content_position);
			if (connection->multirange_response)
				extra = MIN(extra, connection->range_total_size - connection->range_position);
			extra = MIN(extra, (int)estrLength(&connection->http_response));
			if (extra)
				xferHttpResponseTrimLeadingBytes(connection, xfer, extra);
			xferrer->http_extra_bytes += extra;
		}

		// Enter a new range, if necessary.
		if (connection->multirange_response && connection->range_position == connection->range_total_size)
		{
			connection->in_mime_part = false;
			continue;
		}
		if (!estrLength(&connection->http_response))
		{
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		// Verify that the range, if any, contains a complete block.
		// Note that there's a special case for a final, undersized block.
		if (connection->range_response && connection->range_total_size - connection->range_position < (int)block_size
			&& !(xferHttpIsFinalReqBlockSmall(xfer) && (int)xfer->blocks_so_far == xfer->blocks_total - 1 && connection->range_total_size - (int)connection->range_position >= (int)(xfer->com_len % block_size)))
		{
			xferHttpXferReportProblemf(xferrer, xfer,
				"BlockPastSubpartBoundary", "");
			linkRemove_wReason(&link, "Block extends past subpart boundary");
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		// If there's not enough data to fill a single block, wait for more data to arrive.
		// Note that there's a special case for a final, undersized block.
		if (xferHttpIsFinalReqBlockSmall(xfer) && xfer->blocks_so_far == xfer->blocks_total - 1)
		{
			if (estrLength(&connection->http_response) < xfer->com_len % block_size)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
		}
		else if (estrLength(&connection->http_response) < block_size)
		{
			connection->http_wait_for_size = block_size;  // Optimization: there's no point processing again until we have at least a block.
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}

		// Copy the data.
		bytes_to_copy = MIN((int)estrLength(&connection->http_response), connection->content_length - connection->content_position);
		bytes_left_to_copy_in_req = (block_count - block_index) * block_size;
		if (xferHttpIsFinalReqBlockSmall(xfer) && req_index == xfer->num_block_reqs - 1)
			bytes_left_to_copy_in_req += xfer->com_len % block_size - block_size;
		devassert((int)bytes_left_to_copy_in_req <= connection->content_length - connection->content_position);
		bytes_to_copy = MIN(bytes_to_copy, bytes_left_to_copy_in_req);
		if (connection->multirange_response)
			bytes_to_copy = MIN(bytes_to_copy, (U32)(connection->range_total_size - connection->range_position));
		if (bytes_to_copy)
			xferHttpFillXferFromResponse(xferrer, xfer, connection, start_block, bytes_to_copy);
	}

	// The file has been completely downloaded and copied, and the HTTP response buffer now starts with the beginning of the next response, if any.
	pclMSpf("xferHttpHandleBody done for %s", xfer->filename_to_write);
	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

// Finish processing an xfer.
static void xferHttpFinish(PatchXferrer *xferrer, PatchXfer *xfer)
{
	devassert(xfer->blocks_so_far == xfer->blocks_total);
	xfer->state = XFER_REQ_WRITE;
	xfer->use_http = false;
	xfer->http_sent = false;
	xfer->sent_range_request = false;
	xfer->sent_multi_range = false;
	++xferrer->successful_requests;
	++xferrer->http_stat_reqs_recv;
}

// Process HTTP response data.
static void xferHttpHandleData(PatchXferrerHttpConnection *connection)
{
	PatchXferrer *xferrer = connection->xferrer;
	PatchXfer *xfer;

	PERFINFO_AUTO_START_FUNC();

	// If we're waiting for a certain amount of data to arrive before continuing processing, return.
	if (connection->http_wait_for_size && estrLength(&connection->http_response) < connection->http_wait_for_size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	connection->http_wait_for_size = 0;

	// Process each HTTP response until we reach an incomplete HTTP response, or run out of data.
	while (estrLength(&connection->http_response))
	{
		bool continue_parsing;
		S64 now;
		F32 duration;

		// The first waiting response will match the first request we've sent.
		devassert(connection->http_requests);
		xfer = connection->http_requests[0];

		// Parse the header.
		if (!connection->parsed_header)
		{
			int header_size;
			continue_parsing = xferHttpHandleHeader(connection, xfer, &connection->content_length, &header_size);
			if (!continue_parsing)
			{
				PERFINFO_AUTO_STOP_FUNC();
				return;
			}
			connection->parsed_header = true;
			xferrer->http_header_bytes += header_size;
			xfer->total_bytes_received += header_size;
			estrRemove(&connection->http_response, 0, header_size);
		}

		// Parse the entity body.
		continue_parsing = xferHttpHandleBody(connection, xfer);
		if (!continue_parsing)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}
		debugverifyhttpbytes(xferrer);

		// Clear request-parsing state.
		connection->parsed_header = false;
		connection->content_length = 0;
		connection->range_response = false;
		connection->multirange_response = false;
		SAFE_FREE(connection->separator);
		connection->range_total_size = 0;
		connection->http_wait_for_size = 0;
		connection->content_position = 0;
		connection->entity_position = 0;
		connection->range_position = 0;
		connection->in_mime_part = false;

		// If there are multiple requests for this xfer, process the next request.
		debugverifyhttpbytes(xferrer);
		if (xfer->split_requests)
		{
			--xfer->split_requests;
			continue;
		}

		// Xfer downloaded successfully; remove from connection pending xfers.
		eaRemove(&connection->http_requests, 0);

		// Update xfer.
		xferHttpFinish(xferrer, xfer);

		// Update connection statistics.
		++connection->successful_requests;

		// Record timing statistics.
		now = timerCpuTicks64();
		if (connection->http_stat_first_req_sent)
		{
			duration = timerSeconds64(now - connection->http_stat_first_req_sent);
			*(F32 *)beaPushEmpty(&xferrer->http_vec_first_req) = duration;
		}
		duration = timerSeconds64(now - connection->http_stat_req_sent);
		*(F32 *)beaPushEmpty(&xferrer->http_vec_sub_req) = duration;

		// Check if the connection should be closed.
		if (connection->http_closed)
			linkFlushAndClose(&connection->http_link, PATCHXFERHTTP_SERVER_REQUEST_CLOSE);
	}

	PERFINFO_AUTO_STOP_FUNC();
}

// Fail all xfers.
static void xferHttpFailAllXfers(PatchXferrer *xferrer, const char *reason)
{
	debugverifyhttpbytes(xferrer);

	// Fail each xfer.
	EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
	{
		PatchXferrerHttpConnection *connection = xferrer->http_connections[i];
		EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections[i]->http_requests, j, m);
		{
			PatchXfer *xfer = connection->http_requests[j];
			xferHttpXferFail(xferrer, connection, xfer, reason);
		}
		EARRAY_FOREACH_END;
		eaDestroy(&connection->http_requests);

		// Destroy input buffer.
		estrDestroy(&connection->http_response);
	}
	EARRAY_FOREACH_END;

	// Stop any outstanding xfers that are about to use HTTP.
	EARRAY_CONST_FOREACH_BEGIN(xferrer->xfers, i, n);
	{
		xferrer->xfers[i]->use_http = false;
	}
	EARRAY_FOREACH_END;

	debugverifyhttpbytes(xferrer);
}

// Handle HTTP response data.
static void xferHttpHandleMsg(Packet *pak, int cmd, NetLink *link, void *user_data)
{
	PatchXferrerHttpConnection *connection = user_data;
	char *data;
	U32 len;

	PERFINFO_AUTO_START_FUNC();

	// If there's no userdata, it's because the xferrer has been destroyed.
	if (!connection)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// If there are no pending requests, we have a problem; abort.
	if (!eaSize(&connection->http_requests))
	{
		linkRemove_wReason(&link, "Unexpected HTTP response");
		return;
	}

	// Get data, and append it to the current buffer.
	data = pktGetStringRaw(pak);
	len = pktGetSize(pak);
	estrConcat(&connection->http_response, data, len);
	connection->xferrer->http_stat_recv_total += len;

	// Update xferrer timestamp because there was activity.
	connection->xferrer->timestamp = timerCpuSeconds();

	// Handle the HTTP response data.
	xferHttpHandleData(connection);

	PERFINFO_AUTO_STOP_FUNC();
}

// Handle connection opening.
static void xferHttpConnectCallback(NetLink *link, void *user_data)
{
	PatchXferrerHttpConnection *connection = user_data;

	// If there's no userdata, it's because the xferrer has been destroyed.
	if (!connection)
		return;

	pcllog(connection->xferrer->client, PCLLOG_FILEONLY, "HTTP patching connected to %s:%u",
		connection->http_server ? connection->http_server : connection->xferrer->http_server,
		connection->http_server ? connection->http_port : connection->xferrer->http_port);
}

// Handle disconnect.
static void xferHttpDisconnectCallback(NetLink *link, void *user_data)
{
	PatchXferrerHttpConnection *connection = user_data;
	char *reason = NULL;
	PatchXferrer *xferrer;
	const LinkStats *stats;
	int result;
	int i = 0;
	PCL_LogLevel pclloglevel = PCLLOG_INFO;
	int error_code;

	PERFINFO_AUTO_START_FUNC();

	// If there's no userdata, it's because the xferrer has been destroyed.
	if (!connection)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}
	xferrer = connection->xferrer;
	debugverifyhttpbytes(xferrer);
	devassert(!connection->http_closed || eaSize(&connection->http_requests) <= 1);

	estrStackCreate(&reason);
	linkGetDisconnectReason(link, &reason);
	error_code = linkGetDisconnectErrorCode(link);

	if (connection->successful_requests &&
		(  error_code == WSAECONNRESET
		|| error_code == 997
		|| error_code == 0 && strstri(reason, PATCHXFERHTTP_SERVER_REQUEST_CLOSE))
		|| error_code == 0 && strstri(reason, "socket was shutdown"))
		pclloglevel = PCLLOG_FILEONLY;
	pcllog(xferrer->client, pclloglevel, "HTTP patching disconnected from %s:%u: %s",
		connection->http_server ? connection->http_server : connection->xferrer->http_server,
		connection->http_server ? connection->http_port : connection->xferrer->http_port,
		reason);

	// Reset all xfers.
	while (eaSize(&connection->http_requests))
	{
		PatchXfer *xfer = connection->http_requests[0];
		xferrer->http_stat_sent_error += xfer->http_req_size;
		if (i == 0 && xfer->blocks_so_far == 0 && connection->successful_requests)
			xferHttpXferFail(xferrer, connection, xfer, reason);
		else
			xferHttpXferRetry(xferrer, connection, xfer);
		eaRemove(&connection->http_requests, 0);
		++i;
	}
	eaDestroy(&connection->http_requests);
	estrDestroy(&reason);

	// Clear input buffer.
	xferrer->http_stat_recv_error += estrLength(&connection->http_response);
	estrDestroy(&connection->http_response);

	// Save stats.
	stats = linkStats(link);
	if (stats)
		xferrer->http_bytes_received += stats->recv.real_bytes;

	// Remove connection.
	devassert(eaSize(&connection->http_requests) == 0);
	result = eaFindAndRemove(&xferrer->http_connections, connection);
	devassert(result >= 0);
	verifyhttpbytes(xferrer);
	SAFE_FREE(connection->separator);
	free(connection);
	PERFINFO_AUTO_STOP_FUNC();
}

// Calculate the ranges that should be requested for an xfer.
static bool xferHttpCalculateRanges(char ***eaEstrRanges, PatchXfer *xfer)
{
	bool compressed = xfer->get_compressed;
	U32 block_size = xfer->print_sizes[xfer->print_idx];
	int request_index = 0;
	int range_count = 0;
	int i;
	bool multi = false;

	devassert(!eaSize(eaEstrRanges));
	devassert(xfer->num_block_reqs >= 0);
	for (i = 0; i != xfer->num_block_reqs; ++i)
	{
		U32 start_block = xfer->block_reqs[2*i];
		U32 block_count = xfer->block_reqs[2*i+1];
		U32 start, end;
		int j;

		// Try to merge this block request with subsequent contiguous block requests.
		for (j = i + 1; i < xfer->num_block_reqs; ++j)
		{
			if (start_block + block_count != xfer->block_reqs[2*j])
			{
				--j;
				break;
			}
			block_count += xfer->block_reqs[2*j+1];
		}

		// Split the request, if necessary.
		if (++range_count > PATCHXFERHTTP_MAX_RANGES_PER_REQUEST)
		{
			++request_index;
			range_count = 0;
		}
		eaSetSize(eaEstrRanges, request_index + 1);

		// If there are multiple ranges, note this and add a comma.
		if (estrLength(&(*eaEstrRanges)[request_index]))
		{
			multi = true;
			estrConcatChar(&(*eaEstrRanges)[request_index], ',');
		}

		// Create the block request.
		devassert(block_count);
		start = start_block * block_size;
		end = start + block_count * block_size - 1;
		if (end >= xfer->com_len)
		{
			devassert(end - xfer->com_len + 1 < block_size);
			end = xfer->com_len - 1;
		}

		// FIXME: This causes crashes in core_vsnprintf.  estrConcatf(estrRanges, "%lu-%lu", start, end);
		estrConcatf(&(*eaEstrRanges)[request_index], "%u-%u", start, end);
		i = j;
	}

	return multi;
}

// devassert if this xfer is not the next to be requested
static void xferHttpAssertNextRequest(PatchXferrerHttpConnection *connection, PatchXfer *xfer)
{
	bool found = false;
	EARRAY_CONST_FOREACH_BEGIN(connection->http_requests, i, n);
	{
		if (connection->http_requests[i] == xfer)
		{
			found = true;
			break;
		}
		else
			devassert(connection->http_requests[i]->http_sent);
	}
	EARRAY_FOREACH_END;
	devassert(found);
}

// Send request.
static void xferHttpSendRequest(NetLink *link, PatchXferrer *xferrer, PatchXferrerHttpConnection *connection, PatchXfer *xfer, bool whole_file)
{
	char *filename = NULL;
	char *request = NULL;
	Packet *pak;
	S64 now;
	int prefix_length;
	char **ranges = NULL;
	int current_range = 0;

	PERFINFO_AUTO_START_FUNC();

	pclMSpf("xferHttpSendRequest %s", xfer->filename_to_write);

	xferHttpAssertNextRequest(connection, xfer);

	// Create filename.
	estrStackCreate(&filename);
	if (xferrer->path_prefix && xferrer->path_prefix[0] && xferrer->path_prefix[0] != '/')
		estrConcatChar(&filename, '/');
	estrAppend2(&filename, xferrer->path_prefix);
	estrConcatChar(&filename, '/');
	prefix_length = estrLength(&filename);
	estrAppend2(&filename, xfer->filename_to_get);
	string_tolower(filename + prefix_length);
	estrConcatf(&filename, "/r%d.hz", xfer->actual_rev);

	// Calculate ranges.
	if (!patchxferhttpforcerangeaswholefile && (!whole_file || patchxferhttpforcerange))
	{
		xfer->sent_range_request = true;
		xfer->sent_multi_range = xferHttpCalculateRanges(&ranges, xfer);
		devassert(eaSize(&ranges) && estrLength(&ranges[0]));
	}
	else if (patchxferhttpforcerangeaswholefile && patchxferhttpforcerange)
	{
		xfer->sent_range_request = true;
		eaSetSize(&ranges, 1);
		estrPrintf(&ranges[0], "0-%d", xfer->com_len-1);
	}
	devassert(xfer->http_bytes_requested + xfer->http_overload_bytes == xfer->com_len || xfer->sent_range_request || patchxferhttpforcerangeaswholefile);

	// Save some xfer information.
	xfer->used_http = true;
	++xferrer->http_requests_reset;
	xfer->http_sent = true;
	estrCopy2(&xfer->http_request_debug, filename);
	estrClear(&xfer->http_range_debug);
	xfer->split_requests = 0;

	// Send the request, splitting if necessary.
	// The reason for this loop is that some servers have a maximum limit of headers size, and a file with lots of bindiffing
	// going on can generate a very large number of range requests, which may make the header excessively long.  For those requests,
	// we split the request into several smaller requests.  The response handler seamlessly handles this, collapsing each adjancent response
	// for the same xfer into the xfer data.
	do 
	{

		// Generate request.
		estrStackCreate(&request);
		if (ranges)
		{
			if (current_range)
				estrAppend2(&xfer->http_range_debug, "[split]");
			estrAppend2(&xfer->http_range_debug, ranges[current_range]);
		}
		xferHttpGenerateRequest(&request, filename, xferrer, ranges ? ranges[current_range] : NULL);

		// Send request.
		pak = pktCreateRaw(connection->http_link);
		pktSendBytesRaw(pak, request, estrLength(&request));
		xfer->total_bytes_sent += estrLength(&request);
		pktSendRaw(&pak);
		xferrer->http_stat_sent_total += estrLength(&request);
		now = timerCpuTicks64();

		// Record some per-split information.
		if (ranges && current_range)
			++xfer->split_requests;
		if (!connection->http_stat_first_req_sent)
			connection->http_stat_first_req_sent = now;
		connection->http_stat_req_sent = now;
		xfer->http_req_sent = now;
		xfer->http_req_size = estrLength(&request);
		++xferrer->http_stat_reqs_sent;
		estrDestroy(&request);

		++current_range;
	}
	while (ranges && current_range < eaSize(&ranges));

	estrDestroy(&filename);
	eaDestroyEString(&ranges);

	PERFINFO_AUTO_STOP_FUNC();
}

// Open the HTTP link, or return a pointer to the existing one.
static NetLink *xferHttpGetLink(PatchXferrer *xferrer, PatchXferrerHttpConnection *connection)
{
	if (!connection->http_link)
	{
		connection->http_link = commConnect(xferrer->comm,
			LINKTYPE_UNSPEC,
			LINK_RAW,
			xferrer->http_server,
			xferrer->http_port,
			xferHttpHandleMsg,
			xferHttpConnectCallback,
			xferHttpDisconnectCallback,
			sizeof(PatchXferrer *));
		if (!connection->http_link)
		{
			pcllog(xferrer->client, PCLLOG_ERROR, "Bad host when trying to connect to server for HTTP patching: %s:%d",
				xferrer->http_server, xferrer->http_port);
			pclSendLog(xferrer->client, "PCLConnectFailed", "server \"%s\" port %d", xferrer->http_server, xferrer->http_port);
			triviaPrintf("http_server", "%s", xferrer->http_server);
			triviaPrintf("http_port", "%d", xferrer->http_port);
			Errorf("Bad host when trying to connect to server for HTTP patching");
			xferrer->use_http = false;
			xferHttpFailAllXfers(xferrer, "DNS lookup failure");
			return NULL;
		}
		linkSetUserData(connection->http_link, connection);
	}
	return connection->http_link;
}

// Search for an xfer in our connections.
static PatchXferrerHttpConnection *xferHttpFindXferInConnections(PatchXferrer *xferrer, PatchXfer *xfer)
{
	debugverifyhttpbytes(xferrer);
	EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
	{
		PatchXferrerHttpConnection *connection = xferrer->http_connections[i];
		int j = eaFind(&connection->http_requests, xfer);
		if (j != -1)
			return connection;
	}
	EARRAY_FOREACH_END;
	return NULL;
}

// Determine the number of bytes that need to be requested to satisfy this xfer.
static int xferHttpBytesToRequest(PatchXfer *xfer)
{
	U32 block_size = xfer->print_sizes[xfer->print_idx];
	int last_block;
	int bytes_to_request;

	devassert(xfer->blocks_total);

	// Calculate the size of the last block.
	last_block = xfer->com_len % block_size;
	if (!last_block || !xferHttpIsFinalReqBlockSmall(xfer))
		last_block = block_size;

	// Get the number of bytes to request.
	bytes_to_request = (xfer->blocks_total - 1)*block_size + last_block;
	devassert(bytes_to_request <= (int)xfer->com_len);
	return bytes_to_request;
}

// Trim trailing blocks.
// Due to the way that bindiffing works, if a file was bindiffed, it will be padded with blocks up to block_len.
// That is, blocks of the smallest available block size will be used to pad the file up to the size of the file, in
// the largest available blocks.  One of the blocks may be incompletely filled, if the file size is not a multiple of
// the block size.  In addition, up to (biggest_block_size/smallest_block_size-1) blocks may be entirely past the end
// of the file.  PCL would request these as if they were regular blocks, and they would come back as all zeros; these
// would be put in the xfer's file data, but ultimately wouldn't be written to disk, because only the actual file
// length will be written.
// In HTTP, the source entities are the actual file length, not the padded block length.  So, to simplify the
// implementation, the requests for these excess trailing blocks are simply eliminated, and the only special handling
// necessary is for the final partially-filled block.
// Note: This function requires that the block requests are in order.
static void xferHttpTrimTrailingBlocks(PatchXfer *xfer)
{
	U32 block_size;

	if (xfer->http_trimmed_blocks)
		return;

	PERFINFO_AUTO_START_FUNC();

	block_size = xfer->print_sizes[xfer->print_idx];
	devassert(xfer->blocks_so_far == 0);

	// Keep removing blocks until there are no extra blocks.
	for(;;)
	{
		int index = xfer->num_block_reqs - 1;
		U32 start_block = xfer->block_reqs[2*index];
		U32 block_count = xfer->block_reqs[2*index+1];

		// If there are no extra blocks, there's nothing to do, or we're done.
		devassert(block_count);
		if ((start_block + block_count - 1) * block_size < xfer->com_len)
			break;																// Exit.

		// Remove a block.
		--xfer->block_reqs[2*index+1];
		if (xfer->block_reqs[2*index+1] == 0)
		{
			xfer->block_reqs[2*index] = 0x1337bab3;  // Debug marker
			--xfer->num_block_reqs;
		}
		--xfer->blocks_total;
	}

	devassert(xfer->get_compressed);
	devassert(xfer->num_block_reqs);

	xfer->http_trimmed_blocks = true;

	PERFINFO_AUTO_STOP();
}

// If this is a re-request because the previous xfer failed part-way through, update the block requests so
// we only request stuff that we don't have yet.
static void xferHttpUpdateBlocksSoFar(PatchXfer *xfer)
{
	// Only do this if there we've gotten blocks already.
	if (!xfer->blocks_so_far)
		return;
	devassert(xfer->http_retries);
	devassert(xfer->blocks_so_far < xfer->blocks_total);
	devassert(xfer->num_block_reqs > 0);

	// Remove blocks one by one.
	while (xfer->blocks_so_far)
	{
		U32 *block_start = xfer->block_reqs;
		U32 *block_count = xfer->block_reqs + 1;

		// Remove the first block.
		devassert(*block_count);
		++*block_start;
		--*block_count;
		--xfer->blocks_so_far;
		--xfer->blocks_total;

		// If that was the last block in the request, remove this request.
		if (!*block_count)
		{
			--xfer->num_block_reqs;
			memmove(xfer->block_reqs, xfer->block_reqs + 2, 2*xfer->num_block_reqs * sizeof(*xfer->block_reqs));
			xfer->block_reqs[2*xfer->num_block_reqs] = 0xde1e7edd;
			xfer->block_reqs[2*xfer->num_block_reqs+1] = 0xabadcafe;
		}
	}

	devassert(xfer->num_block_reqs > 0);
	devassert(xfer->blocks_total > 0);
	devassert(xfer->block_reqs[1] > 0);
}

// Request data blocks over HTTP.
bool xferHttpReqDataBlocks(PatchXferrer *xferrer, PatchXfer *xfer)
{
	NetLink *link;
	PatchXferrerHttpConnection *connection;
	bool whole_file;
	int bytes_to_request;
	bool allocated;
	int i;

	PERFINFO_AUTO_START_FUNC();

	debugverifyhttpbytes(xferrer);
	devassert(xfer->get_compressed);
	devassert(xfer->state == XFER_REQ_DATA);

	// If we've requested all blocks, and are just waiting for data, return.
	if (xfer->http_sent)
	{
		connection = xferHttpFindXferInConnections(xferrer, xfer);
		devassert(connection);
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// If this is a re-request because the previous xfer failed part-way through, update the block requests so
	// we only request stuff that we don't have yet.
	devassert(xfer->blocks_total);
	xferHttpUpdateBlocksSoFar(xfer);

	// If there's no blocks left, it's because all required blocks were received in a multi-part request, but
	// there was a connection error before we were able to consume the parts of the response that didn't hold
	// data we needed.  In any case, everything is OK, and this xfer is done.
	if (!xfer->blocks_total)
	{
		xferHttpFinish(xferrer, xfer);
		PERFINFO_AUTO_STOP_FUNC();
		return true;
	}

	// Trim trailing blocks.
	xferHttpTrimTrailingBlocks(xfer);

	// Check if this request is for the whole file.
	whole_file = xferHttpReqIsWholeFile(xfer);
	bytes_to_request = xferHttpBytesToRequest(xfer);
	devassert(!whole_file || bytes_to_request == xfer->com_len);

	// Assign this xfer to a connection, if not already assigned.
	connection = xferHttpFindXferInConnections(xferrer, xfer);
	if (!connection)
	{
		// Note: Currently, we always call xferHttpOkToRequestBytes() with whole_file as true, because we never
		// do more than one request for a single file.  If we wanted to make non-whole_files do fractional
		// requests, and avoid overloading net bytes, we would have to write a splitter of some sort,
		// and pass whole_file instead of true here.
		allocated = xferHttpOkToRequestBytes(xferrer, xfer, &connection, bytes_to_request, true);
		if (!allocated)
		{
			PERFINFO_AUTO_STOP_FUNC();
			return false;
		}
		xferHttpXferAssertBytesRequestedIsConsistent(xfer);
		connection = xferHttpFindXferInConnections(xferrer, xfer);
		devassert(connection);
	}
	
	// Get or create HTTP link.
	link = xferHttpGetLink(xferrer, connection);

	// If we're still connecting, do nothing.
	if (!linkConnected(link))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	// Make sure all previous requests are sent.
	for (i = 0; connection->http_requests[i] != xfer; ++i)
	{
		if (!connection->http_requests[i]->http_sent)
			xferHttpReqDataBlocks(xferrer, connection->http_requests[i]);
	}

	// Send HTTP requests for the blocks we need.
	xferHttpSendRequest(link, xferrer, connection, xfer, whole_file);

	debugverifyhttpbytes(xferrer);

	PERFINFO_AUTO_STOP_FUNC();

	return true;
}
