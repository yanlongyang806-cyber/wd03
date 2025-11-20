// These are internal routines that would be in patchxferhttp.c, but they are performance-critical.

#include "earray.h"
#include "error.h"
#include "patchxfer.h"
#include "patchxferhttp.h"
#include "patchxferhttp_opt.h"
#include "pcl_client_internal.h"
#include "StringUtil.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

// Maximum number of simultaneous requests to pipeline.
#define PATCHXFERHTTP_MAX_REQUESTS 25

// Maximum number of connections to a particular server
#define PATCHXFERHTTP_MAX_CONNECTIONS 2

#ifdef PATCHXFERHTTP_EXTRA_VERIFY
#define debugverifyhttpbytes verifyhttpbytes
#else
#define debugverifyhttpbytes(X) ((void)0);
#endif

// Verify all HTTP xfers.
bool verifyhttpbytes(PatchXferrer * xferrer)
{
	PERFINFO_AUTO_START_FUNC();

	EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
	{
		int net_bytes = xferrer->max_net_bytes;
		EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections[i]->http_requests, j, m);
		{
			PatchXfer *xfer = xferrer->http_connections[i]->http_requests[j];
			int k;
			for (k = 0; k < j; ++k)
				devassert(xfer != xferrer->http_connections[i]->http_requests[k]);
			devassert(xfer->use_http);
			devassert(!xfer->http_server || *xfer->http_server);
			devassert(xfer->http_bytes_requested >= 0);
			net_bytes -= xfer->http_bytes_requested;
		}
		EARRAY_FOREACH_END;
		devassert(net_bytes == xferrer->http_connections[i]->net_bytes_free);
	}
	EARRAY_FOREACH_END;

	// For extra debug
#ifdef EXTRA_VERIFY
	EARRAY_CONST_FOREACH_BEGIN(xferrer->xfers, j, m);
	{
		PatchXfer *xfer = xferrer->xfers[j];
		if (xfer->http_sent)
		{
			bool found = false;
			EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, k, o);
			{
				PatchXferrerHttpConnection *connection = xferrer->http_connections[k];
				int index = eaFind(&connection->http_requests, xfer);
				if (index != -1)
				{
					found = true;
					break;
				}
			}
			EARRAY_FOREACH_END;
			devassert(found);
		}
	}
	EARRAY_FOREACH_END;
#endif

	PERFINFO_AUTO_STOP();
	return true;
}

// PatchXferHttp-specific analog to xferOkToRequestBytes().
bool xferHttpOkToRequestBytes(PatchXferrer *xferrer, PatchXfer *xfer, PatchXferrerHttpConnection **use_connection, int amount, bool whole_file)
{
	PatchXferrerHttpConnection *connection = NULL;
	unsigned matching_connections = 0;

	debugverifyhttpbytes(xferrer);

	pclMSpf("xferHttpOkToRequestBytes(): file %s amount %d", xfer->filename_to_get, amount);

	// If there's no change, do nothing.
	if (amount == 0)
		return true;

	// If the caller provided a connection, we should use it.
	if (use_connection)
		connection = *use_connection;

	// If we're releasing bytes, and the connection was not specified, find it.
	if (!connection && amount < 0)
	{
		EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
		{
			EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections[i]->http_requests, j, m);
			{
				if (xferrer->http_connections[i]->http_requests[j] == xfer)
				{
					connection = xferrer->http_connections[i];
					goto found;
				}
			}
			EARRAY_FOREACH_END;
		}
		EARRAY_FOREACH_END;
found:
		devassert(connection);
	}

	// If we still don't have a connection, we must be trying to acquire bytes.
	// Try to find a connection that has enough free bytes to send this request.
	// If that fails, just try to find a connection with free bytes.
	// In both cases, prefer the connection with the most bytes free.
	if (!connection)
	{
		EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
		{
			PatchXferrerHttpConnection *conn = xferrer->http_connections[i];
			if (!conn->http_closed
				&& eaSize(&conn->http_requests) < PATCHXFERHTTP_MAX_REQUESTS
				&& !stricmp_safe(conn->http_server, xfer->http_server) && conn->http_port == xfer->http_port && !stricmp_safe(conn->path_prefix, xfer->path_prefix))
			{
				++matching_connections;
				if (conn->net_bytes_free >= (U32)amount && (!connection || conn->net_bytes_free > connection->net_bytes_free))
					connection = conn;
			}
		}
		EARRAY_FOREACH_END;
		if (!connection && whole_file)
		{
			EARRAY_CONST_FOREACH_BEGIN(xferrer->http_connections, i, n);
			{
				PatchXferrerHttpConnection *conn = xferrer->http_connections[i];
				if (!conn->http_closed
					&& eaSize(&conn->http_requests) < PATCHXFERHTTP_MAX_REQUESTS
					&& !stricmp_safe(conn->http_server, xfer->http_server) && conn->http_port == xfer->http_port && !stricmp_safe(conn->path_prefix, xfer->path_prefix))
				{
					if (conn->net_bytes_free > 0 && (!connection || conn->net_bytes_free > connection->net_bytes_free))
						connection = conn;
				}
			}
			EARRAY_FOREACH_END;
		}
	}

	// Decide if it would be appropriate to open up an additional parallel connection for this xfer.
	if (amount > 0 && matching_connections < PATCHXFERHTTP_MAX_CONNECTIONS && (!connection || eaSize(&connection->http_requests)))
		connection = xferHttpCreateConnection(xferrer, xfer->http_server, xfer->http_port, xfer->path_prefix);

	// If we haven't found a connection by now, fail.
	devassert(amount > 0 || connection);
	if (!connection)
		return false;
	devassert(amount < 0 || connection->net_bytes_free > 0);
	devassert(amount > 0 || connection->net_bytes_free < xferrer->max_net_bytes);
	devassert(amount < 0 || connection->net_bytes_free >= (U32)amount || whole_file);

	// Update free bytes, overloading if necessary.
	if (amount < 0)
	{
		int extra = amount + xfer->http_overload_bytes;
		int refund;
		if (extra > 0)
		{
			xfer->http_overload_bytes = extra;
			refund = 0;
		}
		else
		{
			xfer->http_overload_bytes = 0;
			refund = extra;
		}
		connection->net_bytes_free -= refund;
		xfer->bytes_requested += refund;
		xfer->http_bytes_requested += refund;
	}
	else if (connection->net_bytes_free < (U32)amount)
	{
		int extra = amount - connection->net_bytes_free;
		connection->net_bytes_free = 0;
		xfer->http_overload_bytes += extra;
		xfer->bytes_requested += amount - extra;
		xfer->http_bytes_requested += amount - extra;
	}
	else
	{
		connection->net_bytes_free -= amount;
		xfer->bytes_requested += amount;
		xfer->http_bytes_requested += amount;
	}

	// Record this in the cumulative count.
	if (amount > 0)
		xfer->cum_bytes_requested += amount;

	// Add this xfer to the http request queue, if necessary.
	if (amount > 0 && (!use_connection || !*use_connection))
		eaPush(&connection->http_requests, xfer);

	verifyhttpbytes(xferrer);

	return true;
}

// Figure out what the next block to get is.
void xferHttpGetCurrentBlock(PatchXfer *xfer, U32 *req_index, U32 *block_index)
{
	U32 block;

	PERFINFO_AUTO_START_FUNC();

	*req_index = 0;
	*block_index = 0;

	block = 0;
	for(;;)
	{
		U32 new_block = block + xfer->block_reqs[2**req_index+1];
		if (new_block > xfer->blocks_so_far)
			break;
		block += xfer->block_reqs[2**req_index+1];
		++*req_index;
	}
	*block_index = xfer->blocks_so_far - block;

	PERFINFO_AUTO_STOP();
}

// Return true if the entire file has been requested for download.
bool xferHttpReqIsWholeFile(PatchXfer *xfer)
{
	bool compressed = xfer->get_compressed;
	U32 block_size = xfer->print_sizes[xfer->print_idx];
	int i;
	U64 offset = 0;
	bool whole_file = true;

	devassert(xfer->get_compressed);
	devassert(xfer->num_block_reqs >= 0);

	for (i = 0; i != xfer->num_block_reqs; ++i)
	{
		U32 start_block = xfer->block_reqs[2*i];
		U32 block_count = xfer->block_reqs[2*i+1];
		if (start_block * block_size > offset)
		{
			whole_file = false;
			break;
		}
		offset = (start_block + block_count) * block_size;

		// Verify our assumption that PCL always generates block requests in order.
		devassert(i == xfer->num_block_reqs - 1 || xfer->block_reqs[2*(i+1)] >= start_block);
	}

	whole_file = whole_file && offset >= xfer->com_block_len;
	return whole_file;
}
