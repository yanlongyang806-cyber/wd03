// These are internal routines that would be in patchxferhttp.c, but they are performance-critical.

#ifndef CRYPTIC_PATCHXFERHTTP_OPT_H
#define CRYPTIC_PATCHXFERHTTP_OPT_H

typedef struct PatchXfer PatchXfer;
typedef struct PatchXferrer PatchXferrer;
typedef struct PatchXferrerHttpConnection PatchXferrerHttpConnection;

// Verify all HTTP xfers.
bool verifyhttpbytes(PatchXferrer * xferrer);

// PatchXferHttp-specific analog to xferOkToRequestBytes().
bool xferHttpOkToRequestBytes(PatchXferrer *xferrer, PatchXfer *xfer, PatchXferrerHttpConnection **use_connection, int amount, bool whole_file);

// Figure out what the next block to get is.
void xferHttpGetCurrentBlock(PatchXfer *xfer, U32 *req_index, U32 *block_index);

// Return true if the entire file has been requested for download.
bool xferHttpReqIsWholeFile(PatchXfer *xfer);


// The following are defined in patchxferhttp.c, but have prototypes here so they can be called within patchxferhttp_opt.c.

// Create a new connection.
// Note: This does not actually create the NetLink.  It will be created later, as it is needed.
PatchXferrerHttpConnection *xferHttpCreateConnection(PatchXferrer *xferrer, const char *http_server, U16 port, const char *prefix);

#endif  // CRYPTIC_PATCHXFERHTTP_OPT_H
