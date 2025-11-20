// Fulfill xfers using HTTP requests instead of PCL packets.

#ifndef CRYPTIC_PATCHXFERHTTP_H
#define CRYPTIC_PATCHXFERHTTP_H
GCC_SYSTEM

typedef struct PatchXfer PatchXfer;
typedef struct PatchXferrer PatchXferrer;

// Extra HTTP xfer verification.
//#define PATCHXFERHTTP_EXTRA_VERIFY 1

// A single link to an HTTP patch server
typedef struct PatchXferrerHttpConnection
{
	PatchXferrer *xferrer;
	NetLink *http_link;				// Link for HTTP
	PatchXfer **http_requests;		// ordered queue of pending http requests

	char *http_server;
	U16 http_port;
	char *path_prefix;

	bool http_closed;				// If true, the connection is being closed, and no more requests can be sent.
	char *http_response;			// server response string
	U32 net_bytes_free;				// number of bytes we can request from this link
	U32 successful_requests;		// number of successful requests on this connection

	bool parsed_header;				// if set, we've parsed a header
	U32 http_wait_for_size;			// if set, we're waiting on this much data before proceeding
	bool range_response;			// true if this is a range response
	bool multirange_response;		// true if this is a multirange response
	char *separator;				// if multipart/byteranges, the boundary
	int content_length;				// if parsed_header, Content-Length of header
	int	range_total_size;			// if this is a range response, the total size
	int content_position;			// offset of the first byte of http_response in the body
	int entity_position;			// offset of the first byte of http_response in the logical entity
	int range_position;				// offset of the first byte of http_response in the range
	bool in_mime_part;				// true if we're inside of a mime part

	// cpu ticks of the first req on the current HTTP connection
	S64 http_stat_first_req_sent;
	// cpu ticks of the last send req
	S64 http_stat_req_sent;
} PatchXferrerHttpConnection;

// Return true if we should use HTTP for this xfer.
bool xferHttpShouldUseHttp(PatchXferrer *xferrer, PatchXfer *xfer);

// Request data blocks over HTTP.
bool xferHttpReqDataBlocks(PatchXferrer *xferrer, PatchXfer *xfer);

// Record an xfer failure.  This may cause fail-over to PCL.
void xferHttpXferRecordFail(PatchXferrer *xferrer, PatchXfer *xfer, const char *reason);

// Collect some HTTP network transfer statistics.
void xferHttpRecordNetStats(PatchXferrer * xferrer);

// Prepare reporting for some HTTP network transfer statistics.
void xferHttpReportNetStats(PatchXferrer * xferrer, char **estrLogLine);

// Destroy a HTTP connection.
void xferHttpDestroyConnection(PatchXferrer * xferrer, PatchXferrerHttpConnection *connection);

// Remove an HTTP xfer from its connection.
void xferHttpRemove(PatchXferrer * xferrer, PatchXfer * xfer);

// Return total number of bytes received.
U64 xferHttpBytesReceived(PatchXferrer *xferrer);

#endif  // CRYPTIC_PATCHXFERHTTP_H
