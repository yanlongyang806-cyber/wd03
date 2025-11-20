#include "net.h"
#include "sock.h"
#include "netprivate.h"
#include "earray.h"
#include "netlink.h"

#include "netreceive.h"
#include "timing.h"
#include "EventTimingLog.h"

// PHLEEEEAAAASEEEE! 
// dont compare return values to -1! 
// WSA is the ONLY API allowing it!

void commCheckBsdAccepts(NetComm *comm)
{
	int			i;
	NetListen	*nl;
	NetLink		*link;
	SOCKET		sock;
#ifdef _IOCP
	if (comm->completionPort)
		return;
#endif
	PERFINFO_AUTO_START_FUNC();

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		nl = comm->listen_list[i];
		if (nl->listen_sock == INVALID_SOCKET)
			continue;
		while( (sock = accept(nl->listen_sock,0,0)) != INVALID_SOCKET )
		{
#if _PS3
            if((int)sock < 0)
                break;
#endif

			link = linkCreate(sock,nl, nl->eLinkType, nl->creationFile, nl->creationLine);
			linkActivate(link,0);
		}
		{
			int wsaError = WSAGetLastError();

			if (wsaError != WSAEWOULDBLOCK)
				printf("wsa accept: %d\n",wsaError);
		}
	}

	PERFINFO_AUTO_STOP();
}

int safeRecv(SOCKET sock,void *data,int size,int flags)
{
	int	amt;

	if (sock==INVALID_SOCKET)
		return 0;
	amt = recv(sock,data,size,flags);
	if (amt == 0)
		return -1;

    if (amt < 0)
	{
		int wsaError = WSAGetLastError();

		if (isDisconnectError(wsaError))
			return -1;

		if(wsaError != WSAEWOULDBLOCK)
			printWinErr("WSARecv", __FILE__, __LINE__, wsaError);
        return 0;
    }
	return amt;
}

void bsdProcessPackets(NetComm *comm, S32 timeoutOverrideMilliseconds)
{
	int			i,j,setsize=0;
	NetListen	*nl;
	NetLink		*link;
#ifdef _IOCP
	if (comm->completionPort)
		return;
#endif
	PERFINFO_AUTO_START_FUNC();
	
	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_BEGIN);
	
	comm->last_check_ms = timeGetTime();

	for(i=0;i<eaSize(&comm->listen_list);i++)
	{
		nl = comm->listen_list[i];
		for(j=0;j<eaSize(&nl->links);j++)
		{
			link = nl->links[j];
			if (link->sock==INVALID_SOCKET || link->disconnected || !link->recv_pak)
				continue;
			for(;;)
			{
				int		bytes_xferred;
				Packet	*recv_pkt;

				recv_pkt = link->recv_pak;
				bytes_xferred = safeRecv(link->sock,recv_pkt->data + recv_pkt->size,recv_pkt->max_size - recv_pkt->size,0);
				linkGrowRecvBuf(link,bytes_xferred);
				if (bytes_xferred < 0)
				{
					linkQueueRemove(link, "bytes_xferred < 0 from safeRecv in bsdProcessPackets");
				}
				if (bytes_xferred <= 0)
					break;
				else
				{
					int		ok;

					link->stats.recv.real_packets++;
					link->stats.recv.real_bytes+=bytes_xferred;
					link->stats.recv.last_time_ms = comm->last_check_ms;
					ok = linkReceiveCore(link,bytes_xferred);
					if (!ok)
						linkQueueRemove(link, "asyncRead or linkAsyncReadRaw failed in bsdProcessPackets");
				}
			}
		}
	}
	if(timeoutOverrideMilliseconds >= 0){
		Sleep(timeoutOverrideMilliseconds);
	}
	else if(comm->timeout_msecs){
		Sleep(comm->timeout_msecs);
	}

	etlAddEvent(NULL, __FUNCTION__, ELT_CODE, ELTT_END);

	PERFINFO_AUTO_STOP();
}


